#!/usr/bin/env python3
"""M6 size / DCE / thread-start budget check (spec/M6-hardening.md §3–§4).

Windows PE budgets (B-01..B-04, B-06) and peak working set (B-05) are enforced
when the built artifact is a PE. On Linux this script still builds every
artifact, prints sizes, enforces B-07 thread-start symbols, unused-math DCE
(B-06 equality vs hello), and fails if a non-parallel / non-renderPath program
pulls farm_par / FARM_ENABLE_THREADS.

Usage: check_size_budgets.py <farmc> <repo-root> [--peak-ram]
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile


B01 = 20480
B02 = 98304
B03 = 307200
B04 = 65536
B05 = 67108864
B06 = 20480

# Thread-start names from the Farmos threading runtime (M6 §4.1).
# Do not scan for WaitForMultipleObjects: the CRT may mention it without farm_par.
THREAD_START = (
    b"_beginthreadex",
    b"_beginthread",
    b"CreateThread",
    b"pthread_create",
    b"thrd_create",
)
LINK_THREAD_MARKERS = ("farm_par", "FARM_ENABLE_THREADS", "-pthread")


def fail(msg: str) -> None:
    print(f"FAIL size_budgets: {msg}")
    sys.exit(1)


def is_pe(data: bytes) -> bool:
    return len(data) >= 2 and data[:2] == b"MZ"


def build(farmc: str, src: str, out: str, extra=None, cwd=None) -> subprocess.CompletedProcess:
    cmd = [farmc, "build", src, "-o", out]
    if extra:
        cmd.extend(extra)
    p = subprocess.run(cmd, cwd=cwd, capture_output=True)
    if p.returncode != 0:
        err = p.stderr.decode("utf-8", errors="replace")
        fail(f"build failed ({p.returncode}) for {src}\n{err}")
    return p


def verbose(farmc: str, src: str, out: str) -> str:
    p = subprocess.run([farmc, "build", src, "-o", out, "-v"], capture_output=True)
    if p.returncode != 0:
        fail(f"verbose build failed ({p.returncode}) for {src}\n"
             f"{p.stderr.decode('utf-8', errors='replace')}")
    return p.stderr.decode("utf-8", errors="replace")


def maybe_strip(path: str) -> None:
    strip = shutil.which("llvm-strip") or shutil.which("strip")
    if not strip:
        return
    subprocess.run([strip, "--strip-all", path], capture_output=True)


def assert_no_thread_start(label: str, log: str, blob: bytes) -> None:
    for m in LINK_THREAD_MARKERS:
        if m in log:
            fail(f"{label}: verbose link mentions {m!r}\n{log}")
    for m in THREAD_START:
        if m in blob:
            fail(f"{label}: binary contains thread-start symbol {m.decode('ascii')}")


def peak_rss_bytes(cmd, cwd) -> int:
    """Best-effort peak RSS. Windows: not used here (see .ps1). Linux: /usr/bin/time or rusage."""
    if shutil.which("/usr/bin/time"):
        p = subprocess.run(
            ["/usr/bin/time", "-f", "%M", *cmd],
            cwd=cwd, capture_output=True, text=True,
        )
        if p.returncode != 0:
            fail(f"peak-ram program exited {p.returncode}\n{p.stderr}")
        # %M is KiB max RSS
        lines = [ln.strip() for ln in p.stderr.splitlines() if ln.strip()]
        if not lines:
            fail("peak-ram: /usr/bin/time produced no max-RSS line")
        return int(lines[-1]) * 1024
    p = subprocess.run(cmd, cwd=cwd, capture_output=True)
    if p.returncode != 0:
        fail(f"peak-ram program exited {p.returncode}")
    # rusage not available portably from stdout; skip numeric on this host.
    return -1


def report(oid: str, label: str, value: int, budget: int, unit: str, enforce: bool) -> None:
    status = "OK" if value <= budget else "OVER"
    if value < 0:
        print(f"{oid}  {label}: (not measured)  budget {budget} {unit}  SKIP")
        return
    print(f"{oid}  {label}: {value} {unit}  budget {budget} {unit}  {status}")
    if enforce and value > budget:
        fail(f"{oid} {label}: {value} {unit} exceeds {budget} {unit}")


def write_src(path: str, text: str) -> None:
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("farmc")
    ap.add_argument("repo")
    ap.add_argument("--peak-ram", action="store_true",
                    help="run B-05 Cornell 800x600 peak-RSS (slow)")
    args = ap.parse_args()
    farmc = args.farmc
    root = args.repo

    hello_src = os.path.join(root, "examples", "01_hello.fm")
    cube_src = os.path.join(root, "examples", "02_rotating_cube.fm")
    glass_src = os.path.join(root, "examples", "04_glass_mirror.fm")
    demo_src = os.path.join(root, "examples", "05_stacking_bounce.fm")
    cornell_src = os.path.join(root, "examples", "m4_cornell_800x600.fm")

    with tempfile.TemporaryDirectory(prefix="farmc_m6_size_") as td:
        unused_math = os.path.join(td, "unused_math.fm")
        write_src(unused_math, (
            'import { Vector3, Matrix4, Quaternion } from "farmos:math";\n'
            "function main(): int {\n"
            '  println("Hello, Farmos");\n'
            "  return 0;\n"
            "}\n"
        ))
        scene_hello = os.path.join(td, "scene_hello.fm")
        write_src(scene_hello, (
            'import { Scene } from "farmos:scene";\n'
            "function main(): int {\n"
            "  const s: Scene = new Scene();\n"
            '  println("Hello, Farmos");\n'
            "  return 0;\n"
            "}\n"
        ))

        hello = os.path.join(td, "hello")
        unused = os.path.join(td, "unused_math")
        cube = os.path.join(td, "cube")
        glass = os.path.join(td, "glass")
        demo = os.path.join(td, "demo")
        scene = os.path.join(td, "scene_hello")

        build(farmc, hello_src, hello)
        build(farmc, unused_math, unused)
        build(farmc, cube_src, cube)
        build(farmc, glass_src, glass)
        build(farmc, demo_src, demo)
        build(farmc, scene_hello, scene)
        for p in (hello, unused, cube, glass, demo, scene):
            maybe_strip(p)

        hs = os.path.getsize(hello)
        us = os.path.getsize(unused)
        cs = os.path.getsize(cube)
        gs = os.path.getsize(glass)
        ds = os.path.getsize(demo)
        ss = os.path.getsize(scene)
        delta = ds - ss

        with open(hello, "rb") as f:
            hb = f.read()
        pe = is_pe(hb)
        enforce_pe = pe  # Windows PE budgets; Linux ELF sizes are informative only

        print("M6 size / DCE / thread-start budgets")
        print(f"platform pe={pe}  (PE budgets enforced={enforce_pe})")
        report("B-01", "hello (examples/01_hello.fm)", hs, B01, "B", enforce_pe)
        report("B-02", "cube (examples/02_rotating_cube.fm)", cs, B02, "B", enforce_pe)
        report("B-03", "glass+mirror (examples/04_glass_mirror.fm)", gs, B03, "B", enforce_pe)
        report("B-04", "S_demo - S_scene", delta, B04, "B", enforce_pe)
        print(f"      S_demo={ds} B  S_scene={ss} B")
        report("B-06", "unused farmos:math import", us, B06, "B", enforce_pe)

        if hs != us:
            fail(f"B-06 DCE: unused-math size {us} != hello {hs}")
        print(f"B-06  unused-math DCE: size matches hello ({hs} B)  OK")

        hlog = verbose(farmc, hello_src, os.path.join(td, "hello_v"))
        ulog = verbose(farmc, unused_math, os.path.join(td, "unused_v"))
        slog = verbose(farmc, scene_hello, os.path.join(td, "scene_v"))
        clog = verbose(farmc, cube_src, os.path.join(td, "cube_v"))
        with open(os.path.join(td, "hello_v"), "rb") as f:
            hv = f.read()
        with open(unused, "rb") as f:
            ub = f.read()
        with open(scene, "rb") as f:
            sb = f.read()
        with open(cube, "rb") as f:
            cb = f.read()

        assert_no_thread_start("B-07 hello", hlog, hv)
        assert_no_thread_start("B-07 unused-math", ulog, ub)
        assert_no_thread_start("B-07 scene-only hello", slog, sb)
        assert_no_thread_start("B-07 rotating-cube (render, not renderPath)", clog, cb)
        print("B-07  no thread-start imports on hello / unused-math / scene-only / cube  OK")

        if "farm_par" in hlog or "FARM_ENABLE_THREADS" in hlog:
            fail("hello pulled parallel runtime")
        if "farm_math.c" in hlog or "farm_scene.c" in hlog or "farm_ray.c" in hlog or "farm_physics.c" in hlog:
            fail("hello linked a stdlib unit beyond farm_rt.c")
        if "farm_math.c" in ulog:
            fail("unused math import linked farm_math.c")
        print("DCE  hello / unused-math link only farm_rt.c  OK")

        if args.peak_ram:
            if not os.path.isfile(cornell_src):
                fail(f"missing {cornell_src}")
            cornell = os.path.join(td, "cornell")
            build(farmc, cornell_src, cornell)
            peak = peak_rss_bytes([cornell], cwd=td)
            report("B-05", "Cornell 800x600 renderPath peak RSS", peak, B05, "B", peak >= 0)
        else:
            print("B-05  Cornell 800x600 peak RAM: skipped (pass --peak-ram on TommyLaptop)")

    print("PASS size_budgets")
    return 0


if __name__ == "__main__":
    sys.exit(main())

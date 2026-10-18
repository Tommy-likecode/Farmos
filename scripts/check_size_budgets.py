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
import time


B01 = 20480
B02 = 98304
B03 = 307200
B04 = 65536
B05 = 67108864
B06 = 20480
B08 = 409600  # M7 GPU-linked demo (OQ-M7-04 A)
B09 = 512000  # M9 RT-linked demo (OQ-M9-06 A)
B10 = 614400  # M10 denoise-linked demo (OQ-M10-07 A)

VULKAN_MARKERS = (b"vulkan-1.dll", b"libvulkan.so.1", b"vkGetInstanceProcAddr")

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


def assert_no_vulkan_sym(label: str, log: str, blob: bytes) -> None:
    for m in ("farm_vk.c", "FARM_ENABLE_VULKAN", "farm_vk_rt.c", "FARM_ENABLE_VULKAN_RT"):
        if m in log:
            fail(f"{label}: verbose link mentions {m}")
    for m in VULKAN_MARKERS:
        if m in blob:
            fail(f"{label}: binary contains Vulkan loader symbol {m.decode('ascii')}")


def assert_no_vulkan_rt_sym(label: str, log: str) -> None:
    for m in ("farm_vk_rt.c", "FARM_ENABLE_VULKAN_RT"):
        if m in log:
            fail(f"{label}: verbose link mentions {m}")


def assert_no_thread_start(label: str, log: str, blob: bytes) -> None:
    for m in LINK_THREAD_MARKERS:
        if m in log:
            fail(f"{label}: verbose link mentions {m!r}\n{log}")
    for m in THREAD_START:
        if m in blob:
            fail(f"{label}: binary contains thread-start symbol {m.decode('ascii')}")


def _peak_from_proc_status(pid: int) -> int:
    """Linux VmHWM (peak RSS) in bytes. 0 if /proc is gone."""
    try:
        with open(f"/proc/{pid}/status", "r", encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("VmHWM:"):
                    return int(line.split()[1]) * 1024
    except (FileNotFoundError, ProcessLookupError, OSError, ValueError, IndexError):
        return 0
    return 0


def _peak_windows_working_set(proc: subprocess.Popen) -> int:
    """PeakWorkingSetSize via GetProcessMemoryInfo on the still-open handle."""
    import ctypes
    from ctypes import wintypes

    class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    get_info = ctypes.windll.psapi.GetProcessMemoryInfo
    get_info.argtypes = [
        wintypes.HANDLE,
        ctypes.POINTER(PROCESS_MEMORY_COUNTERS),
        wintypes.DWORD,
    ]
    get_info.restype = wintypes.BOOL
    pmc = PROCESS_MEMORY_COUNTERS()
    pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
    handle = int(getattr(proc, "_handle", 0) or 0)
    if not handle:
        return 0
    if not get_info(handle, ctypes.byref(pmc), pmc.cb):
        return 0
    return int(pmc.PeakWorkingSetSize)


def peak_rss_bytes(cmd, cwd) -> int:
    """Peak RSS / working set sampled while the child is alive.

    Reading PeakWorkingSet64 only after WaitForExit can be 0 on Windows.
    Linux uses /proc/<pid>/status VmHWM (or GNU time %M as fallback).
    """
    if os.name == "nt":
        p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        peak = 0
        while True:
            try:
                peak = max(peak, _peak_windows_working_set(p))
            except Exception:
                pass
            if p.poll() is not None:
                break
            time.sleep(0.005)
        try:
            peak = max(peak, _peak_windows_working_set(p))
        except Exception:
            pass
        err = p.stderr.read() if p.stderr else b""
        if p.returncode != 0:
            fail(f"peak-ram program exited {p.returncode}\n"
                 f"{err.decode('utf-8', errors='replace')}")
        if peak <= 0:
            fail("B-05 peak working set was 0 (measurement failed)")
        return peak

    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    peak = 0
    while True:
        peak = max(peak, _peak_from_proc_status(p.pid))
        if p.poll() is not None:
            break
        time.sleep(0.005)
    err = p.stderr.read() if p.stderr else b""
    if p.returncode != 0:
        fail(f"peak-ram program exited {p.returncode}\n"
             f"{err.decode('utf-8', errors='replace')}")
    if peak <= 0 and shutil.which("/usr/bin/time"):
        tp = subprocess.run(
            ["/usr/bin/time", "-f", "%M", *cmd],
            cwd=cwd, capture_output=True, text=True,
        )
        if tp.returncode != 0:
            fail(f"peak-ram program exited {tp.returncode}\n{tp.stderr}")
        lines = [ln.strip() for ln in tp.stderr.splitlines() if ln.strip()]
        if not lines:
            fail("peak-ram: /usr/bin/time produced no max-RSS line")
        peak = int(lines[-1]) * 1024
    if peak <= 0:
        fail("B-05 peak RSS was 0 (measurement failed)")
    return peak


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
    gpu_src = os.path.join(root, "examples", "06_vulkan_cornell.fm")
    rt_src = os.path.join(root, "examples", "07_vulkan_rt_cornell.fm")

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
            'import { Scene, PerspectiveCamera, Renderer } from "farmos:scene";\n'
            "function main(): int {\n"
            "  const scene: Scene = new Scene();\n"
            "  const camera: PerspectiveCamera = new PerspectiveCamera(50, 1, 0.1, 100);\n"
            "  camera.position.z = 4;\n"
            "  const renderer: Renderer = new Renderer(8, 8);\n"
            "  renderer.render(scene, camera);\n"
            "  return 0;\n"
            "}\n"
        ))

        hello = os.path.join(td, "hello")
        unused = os.path.join(td, "unused_math")
        cube = os.path.join(td, "cube")
        glass = os.path.join(td, "glass")
        demo = os.path.join(td, "demo")
        scene = os.path.join(td, "scene_hello")

        denoise_src = os.path.join(td, "denoise_demo.fm")
        write_src(denoise_src, (
            'import { Scene, PerspectiveCamera, Renderer, PATH_DENOISE_SPATIAL } from "farmos:scene";\n'
            'import { Color } from "farmos:math";\n'
            "function main(): int {\n"
            "  const scene: Scene = new Scene();\n"
            "  scene.setBackground(new Color(0x4488ff));\n"
            "  const camera: PerspectiveCamera = new PerspectiveCamera(60, 1, 0.1, 100);\n"
            "  camera.position.z = 5;\n"
            "  const renderer: Renderer = new Renderer(8, 8);\n"
            "  renderer.setSamples(1);\n"
            "  renderer.setPathDenoise(PATH_DENOISE_SPATIAL);\n"
            "  renderer.renderPath(scene, camera);\n"
            "  return 0;\n"
            "}\n"
        ))

        gpu = os.path.join(td, "gpu")
        rt = os.path.join(td, "rt")
        denoise = os.path.join(td, "denoise")
        build(farmc, hello_src, hello)
        build(farmc, unused_math, unused)
        build(farmc, cube_src, cube)
        build(farmc, glass_src, glass)
        build(farmc, demo_src, demo)
        build(farmc, scene_hello, scene)
        build(farmc, gpu_src, gpu)
        build(farmc, rt_src, rt)
        build(farmc, denoise_src, denoise)
        for p in (hello, unused, cube, glass, demo, scene, gpu, rt, denoise):
            maybe_strip(p)

        hs = os.path.getsize(hello)
        us = os.path.getsize(unused)
        cs = os.path.getsize(cube)
        gs = os.path.getsize(glass)
        ds = os.path.getsize(demo)
        ss = os.path.getsize(scene)
        gps = os.path.getsize(gpu)
        rps = os.path.getsize(rt)
        dns = os.path.getsize(denoise)
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
        report("B-08", "GPU-linked vulkan cornell (examples/06_vulkan_cornell.fm)", gps, B08, "B", enforce_pe)
        report("B-09", "RT-linked vulkan cornell (examples/07_vulkan_rt_cornell.fm)", rps, B09, "B", enforce_pe)
        report("B-10", "denoise-linked path demo", dns, B10, "B", enforce_pe)
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
        glog = verbose(farmc, glass_src, os.path.join(td, "glass_v"))
        with open(os.path.join(td, "hello_v"), "rb") as f:
            hv = f.read()
        with open(unused, "rb") as f:
            ub = f.read()
        with open(scene, "rb") as f:
            sb = f.read()
        with open(cube, "rb") as f:
            cb = f.read()
        with open(os.path.join(td, "glass_v"), "rb") as f:
            gb = f.read()

        assert_no_thread_start("B-07 hello", hlog, hv)
        assert_no_thread_start("B-07 unused-math", ulog, ub)
        assert_no_thread_start("B-07 scene-only hello", slog, sb)
        assert_no_thread_start("B-07 rotating-cube (render, not renderPath)", clog, cb)
        print("B-07  no thread-start imports on hello / unused-math / scene-only / cube  OK")

        assert_no_vulkan_sym("M7 hello", hlog, hv)
        assert_no_vulkan_sym("M7 unused-math", ulog, ub)
        assert_no_vulkan_sym("M7 scene-only hello", slog, sb)
        assert_no_vulkan_sym("M7 rotating-cube", clog, cb)
        assert_no_vulkan_sym("M7 glass+mirror CPU", glog, gb)
        print("M7   no Vulkan loader symbols on hello / unused-math / scene / cube / CPU glass  OK")

        gpolog = verbose(farmc, gpu_src, os.path.join(td, "gpu_v"))
        rtlog = verbose(farmc, rt_src, os.path.join(td, "rt_v"))
        assert_no_vulkan_rt_sym("M9 compute-only GPU cornell", gpolog)
        if "farm_vk_rt.c" not in rtlog or "FARM_ENABLE_VULKAN_RT" not in rtlog:
            fail("M9 RT-linked demo did not link farm_vk_rt.c")
        if "farm_vk.c" not in rtlog:
            fail("M9 RT-linked demo did not link farm_vk.c (compute fallback)")
        print("M9   compute-only omits farm_vk_rt.c; RT-linked includes RT + compute fallback  OK")

        ver = subprocess.run([farmc, "version"], capture_output=True)
        vout = ver.stdout.decode("utf-8", errors="replace")
        if "1.01" not in vout:
            fail(f"farmc version does not contain 1.01: {vout!r}")
        clog = os.path.join(root, "CHANGELOG.md")
        if not os.path.isfile(clog):
            fail("missing CHANGELOG.md")
        with open(clog, encoding="utf-8") as f:
            cltxt = f.read()
        if "v1.01" not in cltxt and "1.01" not in cltxt:
            fail("CHANGELOG.md has no v1.01 entry")
        print(f"M7   farmc version: {vout.strip()}  OK")

        if "farm_par" in hlog or "FARM_ENABLE_THREADS" in hlog:
            fail("hello pulled parallel runtime")
        if "farm_math.c" in hlog or "farm_scene.c" in hlog or "farm_ray.c" in hlog or "farm_physics.c" in hlog or "farm_vk.c" in hlog or "farm_vk_rt.c" in hlog or "farm_denoise.c" in hlog:
            fail("hello linked a stdlib unit beyond farm_rt.c")
        if "farm_math.c" in ulog:
            fail("unused math import linked farm_math.c")
        if "farm_vk.c" in glog or "farm_vk_rt.c" in glog:
            fail("CPU glass+mirror linked farm_vk.c / farm_vk_rt.c")
        if "farm_denoise.c" in glog or "FARM_ENABLE_DENOISE" in glog:
            fail("CPU glass+mirror linked farm_denoise.c")
        dlog = verbose(farmc, denoise_src, os.path.join(td, "denoise_v"))
        if "farm_denoise.c" not in dlog or "FARM_ENABLE_DENOISE" not in dlog:
            fail("M10 denoise-linked demo did not link farm_denoise.c")
        print("DCE  hello / unused-math link only farm_rt.c  OK")
        print("M10  glass+mirror omits farm_denoise.c; denoise-linked includes it  OK")

        if args.peak_ram:
            if not os.path.isfile(cornell_src):
                fail(f"missing {cornell_src}")
            cornell = os.path.join(td, "cornell")
            build(farmc, cornell_src, cornell)
            peak = peak_rss_bytes([cornell], cwd=td)
            report("B-05", "Cornell 800x600 renderPath peak RSS", peak, B05, "B", True)
        else:
            print("B-05  Cornell 800x600 peak RAM: skipped (pass --peak-ram on TommyLaptop)")

    print("PASS size_budgets")
    return 0


if __name__ == "__main__":
    sys.exit(main())

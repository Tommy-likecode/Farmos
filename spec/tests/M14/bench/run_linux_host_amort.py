#!/usr/bin/env python3
"""Linux host-amortization harness (no XeSS). M14 reuses the M13 inproc meter.

Compares process-per-frame wall vs in-process FARMOS_PATH_FRAME_MS samples
on a tiny CPU still. Prints error_count and frames_done.

Frozen command:
  python3 spec/tests/M14/bench/run_linux_host_amort.py build/farmc --out out/m14_linux
"""
from __future__ import annotations

import argparse
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path

FM = """\
import {{ Scene, PerspectiveCamera, Renderer }} from "farmos:scene";
import {{ Color }} from "farmos:math";

function main(): int {{
  const scene: Scene = new Scene();
  scene.setBackground(new Color(0x4488ff));
  const camera: PerspectiveCamera = new PerspectiveCamera(60, 1, 0.1, 100);
  camera.position.z = 5;
  const renderer: Renderer = new Renderer({w}, {h});
  renderer.setSamples({spp});
  renderer.setMaxBounces(2);
  let i: int = 0;
  while (i < {frames}) {{
    renderer.renderPath(scene, camera);
    i = i + 1;
  }}
  return 0;
}}
"""

FRAME_RE = re.compile(r"^frame_ms=([0-9.]+)\s*$", re.M)


def repo_runtime() -> Path:
    return Path(__file__).resolve().parents[4] / "runtime"


def build(farmc: Path, src: Path, out_exe: Path) -> None:
    out_exe.parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("FARM_RUNTIME", str(repo_runtime()))
    p = subprocess.run([str(farmc), "build", str(src), "-o", str(out_exe)],
                       capture_output=True, text=True, env=env)
    if p.returncode != 0:
        sys.stderr.write(p.stdout or "")
        sys.stderr.write(p.stderr or "")
        raise SystemExit(f"build failed: {src}")


def run_exe(exe: Path, cwd: Path) -> tuple[int, float, str]:
    env = os.environ.copy()
    env.setdefault("FARM_RUNTIME", str(repo_runtime()))
    env["FARMOS_PATH_FRAME_MS"] = "1"
    t0 = time.perf_counter()
    p = subprocess.run([str(exe)], cwd=str(cwd), env=env, capture_output=True, text=True)
    dt = time.perf_counter() - t0
    out = (p.stdout or "") + (p.stderr or "")
    return p.returncode, dt, out


def parse_frame_ms(text: str) -> list[float]:
    return [float(x) for x in FRAME_RE.findall(text)]


def write_src(path: Path, w: int, h: int, spp: int, frames: int) -> None:
    path.write_text(FM.format(w=w, h=h, spp=spp, frames=frames), encoding="utf-8")


def median(xs: list[float]) -> float:
    return statistics.median(xs) if xs else 0.0


def run_case(farmc: Path, out: Path, tag: str, w: int, h: int, spp: int,
             warmup: int, timed: int) -> dict:
    errors = 0
    src1 = out / f"{tag}_1f.fm"
    srcn = out / f"{tag}_inproc.fm"
    exe1 = out / f"{tag}_1f"
    exen = out / f"{tag}_inproc"
    write_src(src1, w, h, spp, 1)
    write_src(srcn, w, h, spp, warmup + timed)
    build(farmc, src1, exe1)
    build(farmc, srcn, exen)

    proc_walls = []
    proc_frame = []
    for i in range(warmup + timed):
        rc, wall, text = run_exe(exe1, out)
        ms = parse_frame_ms(text)
        if rc != 0 or len(ms) != 1:
            errors += 1
            print(f"  {tag} proc {i+1} rc={rc} frames={len(ms)}")
            continue
        if i >= warmup:
            proc_walls.append(wall * 1000.0)
            proc_frame.append(ms[0])
    in_rc, in_wall, in_text = run_exe(exen, out)
    in_ms = parse_frame_ms(in_text)
    if in_rc != 0 or len(in_ms) != warmup + timed:
        errors += 1
        print(f"  {tag} inproc rc={in_rc} frames={len(in_ms)} want={warmup+timed}")
        timed_in = []
    else:
        timed_in = in_ms[warmup:]

    frames_done = len(proc_walls) + len(timed_in)
    proc_med = median(proc_walls)
    in_med = median(timed_in)
    ratio = (proc_med / in_med) if in_med > 0 else 0.0
    def rng(xs: list[float]) -> str:
        if not xs:
            return "n/a"
        return f"{min(xs):.3f}-{max(xs):.3f}"

    print(f"{tag} {w}x{h} spp={spp} errors={errors} frames_done={frames_done}")
    print(f"  process_per_frame_median_ms={proc_med:.3f} n={len(proc_walls)} range={rng(proc_walls)}")
    print(f"  inproc_frame_median_ms={in_med:.3f} n={len(timed_in)} range={rng(timed_in)}")
    print(f"  proc/inproc={ratio:.3f}")
    print(f"  inproc_process_wall_ms={in_wall*1000.0:.3f}")
    return {
        "tag": tag,
        "errors": errors,
        "frames_done": frames_done,
        "proc_median_ms": proc_med,
        "inproc_median_ms": in_med,
        "ratio": ratio,
        "w": w,
        "h": h,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("farmc")
    ap.add_argument("--out", default="out/m14_linux")
    args = ap.parse_args()
    farmc = Path(args.farmc).resolve()
    out = Path(args.out)
    if not out.is_absolute():
        out = (Path.cwd() / out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    print(f"farmc={farmc}")
    print(f"out={out}")
    print(f"load={os.popen('uptime').read().strip()}")

    easy = run_case(farmc, out, "easy", 8, 8, 1, warmup=2, timed=5)
    target = run_case(farmc, out, "target", 32, 32, 1, warmup=2, timed=5)
    errors = easy["errors"] + target["errors"]
    frames = easy["frames_done"] + target["frames_done"]
    print(f"error_count={errors}")
    print(f"frames_done={frames}")
    if errors:
        print("FAIL host_amort (errors)")
        return 1
    if target["ratio"] <= 1.0 or easy["ratio"] <= 1.0:
        print("FAIL host_amort (process-per-frame not slower than in-process)")
        return 1
    if easy["ratio"] < target["ratio"]:
        print("NOTE easy ratio < target ratio (spawn share may be lower on tiny work)")
    print("PASS host_amort")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

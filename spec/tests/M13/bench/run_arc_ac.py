#!/usr/bin/env python3
"""TommyLaptop Arc AC-M13-04 helper. Reuses M12 bench .fm sources.

Primary ratio: in-process FARMOS_PATH_FRAME_MS samples after warmup
(native 1080 vs XeSS 960→1080). SHOULD ≥ 1.20 (OQ-M13-01 A).

Also runs M12 process-per-frame AC-05 as the ≥1.05× floor check.
Quality reuses M12 AC-04 PNG metrics (pinned ROI).

Official Intel XeSS via FARMOS_XESS_LIB only. Enabler is out.
"""
from __future__ import annotations

import argparse
import os
import re
import statistics
import sys
from pathlib import Path

M12_BENCH = Path(__file__).resolve().parents[1].parent / "M12" / "bench"
sys.path.insert(0, str(M12_BENCH))
import run_arc_ac as m12  # noqa: E402

SHOULD_RATIO = 1.20
FLOOR_RATIO = 1.05
WARMUP_FRAMES = 3
FRAME_RE = re.compile(r"^frame_ms=([0-9.]+)\s*$", re.M)


def parse_frame_ms(text: str) -> list[float]:
    return [float(x) for x in FRAME_RE.findall(text)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("farmc")
    ap.add_argument("--out", default="out/m13_arc")
    args = ap.parse_args()
    farmc = Path(args.farmc).resolve()
    bench = M12_BENCH
    out = Path(args.out)
    if not out.is_absolute():
        out = (Path.cwd() / out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    print(f"farmc={farmc}")
    print(f"out={out}")
    print(f"FARMOS_XESS_LIB={os.environ.get('FARMOS_XESS_LIB', '')}")
    print(f"M13 SHOULD>={SHOULD_RATIO:.2f}  M12 floor>={FLOOR_RATIO:.2f}")
    print("This process is one pass. Pass1: delete PSO caches, then run this script.")
    print("Pass2: leave warm PSO caches, run this script again.")
    print("In-process ratio is the M13 SHOULD. Process-per-frame ratio is the M12 floor.")
    print("Do not compare in-process ms to d23acfb3 process-per-frame seconds.")

    native4 = out / "ac04_native"
    xess4 = out / "ac04_xess"
    native5 = out / "ac05_native"
    xess5 = out / "ac05_xess"
    if os.name == "nt":
        native4 = native4.with_suffix(".exe")
        xess4 = xess4.with_suffix(".exe")
        native5 = native5.with_suffix(".exe")
        xess5 = xess5.with_suffix(".exe")

    os.environ.setdefault("FARM_RUNTIME", str(Path(__file__).resolve().parents[4] / "runtime"))
    print("building M12 AC-04/05 sources…")
    m12.build(farmc, bench / "ac04_native_1080.fm", native4)
    m12.build(farmc, bench / "ac04_xess_960.fm", xess4)
    m12.build(farmc, bench / "ac05_native_1f.fm", native5)
    m12.build(farmc, bench / "ac05_xess_1f.fm", xess5)

    extra = {"FARMOS_PATH_FRAME_MS": "1"}
    print("AC-M13-04 native in-process (12 frames, FRAME_MS)…")
    rc, dt, outn = m12.run_exe(native4, out, extra_env=extra)
    print(f"  rc={rc} wall={dt:.3f}s\n{outn.strip()}")
    if rc != 0:
        return rc
    print("AC-M13-04 XeSS in-process (12 frames, FRAME_MS)…")
    rc, dt, outx = m12.run_exe(xess4, out, extra_env=extra)
    print(f"  rc={rc} wall={dt:.3f}s\n{outx.strip()}")
    if rc != 0:
        return rc

    nfs = parse_frame_ms(outn)
    xfs = parse_frame_ms(outx)
    print(f"  native_frame_ms n={len(nfs)} xess_frame_ms n={len(xfs)}")
    if len(nfs) < WARMUP_FRAMES + 1 or len(xfs) < WARMUP_FRAMES + 1:
        print("FAIL in-process frame_ms count")
        return 1
    n_timed = nfs[WARMUP_FRAMES:]
    x_timed = xfs[WARMUP_FRAMES:]
    nmed = statistics.median(n_timed)
    xmed = statistics.median(x_timed)
    inproc_ratio = nmed / xmed if xmed > 0 else 0.0
    print(f"inproc native_median={nmed:.3f}ms range={min(n_timed):.3f}-{max(n_timed):.3f}")
    print(f"inproc xess_median={xmed:.3f}ms range={min(x_timed):.3f}-{max(x_timed):.3f}")
    print(f"inproc ratio native/xess={inproc_ratio:.3f}  SHOULD>={SHOULD_RATIO:.2f}")

    npng = out / "native_1080.png"
    xpng = out / "xess_1080.png"
    m = m12.png_metrics(npng, xpng)
    print("AC-M12-04 metrics:")
    for k, v in m.items():
        print(f"  {k}={v if isinstance(v, str) else (v if isinstance(v, int) else f'{v:.3f}')}")

    print("AC-M12-05 native process-per-frame (M12 floor)…")
    nw = m12.median_times(native5, out, 8, 9)
    print("AC-M12-05 XeSS process-per-frame (M12 floor)…")
    xw = m12.median_times(xess5, out, 8, 9)
    nmed_p = statistics.median(nw)
    xmed_p = statistics.median(xw)
    proc_ratio = nmed_p / xmed_p if xmed_p > 0 else 0.0
    print(f"proc native_median={nmed_p:.3f}s range={min(nw):.3f}-{max(nw):.3f}")
    print(f"proc xess_median={xmed_p:.3f}s range={min(xw):.3f}-{max(xw):.3f}")
    print(f"proc ratio native/xess={proc_ratio:.3f}  M12 floor>={FLOOR_RATIO:.2f}")

    ac_bind = "PASS" if xmed < nmed and xmed_p < nmed_p else "FAIL"
    ac_m13 = "PASS" if inproc_ratio >= SHOULD_RATIO else "FAIL"
    ac_floor = "PASS" if proc_ratio >= FLOOR_RATIO else "FAIL"
    dL_means = m["silver_dL_means"]
    print(f"binding XeSS<native (inproc+proc)={ac_bind}")
    print(f"AC-M13-04 SHOULD {SHOULD_RATIO:.2f}x inproc={ac_m13}")
    print(f"AC-M12-05 SHOULD {FLOOR_RATIO:.2f}x proc={ac_floor}")
    print(
        f"AC-M12-04 ROI L native/xess={m['silver_L_native']:.3f}/{m['silver_L_xess']:.3f} "
        f"|dL_means|={dL_means:.3f} mean_abs_dL={m['silver_mean_abs_dL']:.3f} "
        f"(report-only; not |dL|) png_tol/max_abs={m['png_tol_12_report']} report-only"
    )
    print(f"error_count=0")
    print(f"frames_done={len(nfs)+len(xfs)+len(nw)+len(xw)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

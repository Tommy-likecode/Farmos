#!/usr/bin/env python3
"""TommyLaptop Arc AC-M12-04 / AC-M12-05 helper.

AC-M12-04: in-process 12-frame 64 spp stills (native 1080 vs XeSS 960->1080).
AC-M12-05: process-per-frame; warmup 8 + timed 9; ratio = native_median / xess_median.

Does not merge. Official Intel XeSS via FARMOS_XESS_LIB / libxess.dll only.
Enabler / Swapper / OptiScaler are out.
"""
from __future__ import annotations

import argparse
import os
import statistics
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROI = (940, 520, 980, 560)  # binding AC-04 box x=940:980,y=520:560 @1080p (40×40)
PNG_TOL = 12
SHOULD_RATIO = 1.05  # OQ-M12-12 A; historical 1.5 / 1.2 superseded (report-only)


def decode_rgb8(path: Path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"not a PNG: {path}")
    pos = 8
    width = height = None
    ctype = None
    idat = b""
    while pos + 8 <= len(data):
        ln = struct.unpack(">I", data[pos : pos + 4])[0]
        tag = data[pos + 4 : pos + 8]
        chunk = data[pos + 8 : pos + 8 + ln]
        pos += 12 + ln
        if tag == b"IHDR":
            width, height, bit, ctype = struct.unpack(">IIBB", chunk[:10])
            if bit != 8 or ctype not in (2, 6):
                raise SystemExit(f"need 8-bit RGB/RGBA, got bit={bit} ctype={ctype}")
        elif tag == b"IDAT":
            idat += chunk
        elif tag == b"IEND":
            break
    if width is None:
        raise SystemExit("missing IHDR")
    raw = zlib.decompress(idat)
    bpp = 3 if ctype == 2 else 4
    stride = 1 + width * bpp
    pix = []
    for y in range(height):
        row = raw[y * stride : (y + 1) * stride]
        if row[0] != 0:
            raise SystemExit(f"unsupported filter {row[0]}")
        for x in range(width):
            i = 1 + x * bpp
            pix.append((row[i], row[i + 1], row[i + 2]))
    return width, height, pix


def mean_L(pix, w, x0, y0, x1, y1) -> float:
    acc = 0.0
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = pix[y * w + x]
            acc += 0.2126 * r + 0.7152 * g + 0.0722 * b
            n += 1
    return acc / n


def png_metrics(native: Path, xess: Path):
    nw, nh, np = decode_rgb8(native)
    xw, xh, xp = decode_rgb8(xess)
    if (nw, nh) != (xw, xh):
        raise SystemExit(f"size mismatch {nw}x{nh} vs {xw}x{xh}")
    max_abs = 0
    sum_abs = 0.0
    n = nw * nh * 3
    for i in range(nw * nh):
        for c in range(3):
            d = abs(np[i][c] - xp[i][c])
            if d > max_abs:
                max_abs = d
            sum_abs += d
    x0, y0, x1, y1 = ROI
    ln = mean_L(np, nw, x0, y0, x1, y1)
    lx = mean_L(xp, xw, x0, y0, x1, y1)
    dL = 0.0
    m = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            rn, gn, bn = np[y * nw + x]
            rx, gx, bx = xp[y * nw + x]
            Ln = 0.2126 * rn + 0.7152 * gn + 0.0722 * bn
            Lx = 0.2126 * rx + 0.7152 * gx + 0.0722 * bx
            dL += abs(Ln - Lx)
            m += 1
    return {
        "max_abs_channel": max_abs,
        "mean_abs_rgb": sum_abs / n,
        "silver_L_native": ln,
        "silver_L_xess": lx,
        "silver_dL_means": abs(lx - ln),
        "silver_mean_abs_dL": dL / m,
        "png_tol_12_report": max_abs,
    }


def run_exe(exe: Path, cwd: Path, extra_env=None) -> tuple[int, float, str]:
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    t0 = time.perf_counter()
    p = subprocess.run([str(exe)], cwd=str(cwd), env=env, capture_output=True, text=True)
    dt = time.perf_counter() - t0
    out = (p.stdout or "") + (p.stderr or "")
    return p.returncode, dt, out


def build(farmc: Path, src: Path, out_exe: Path) -> None:
    out_exe.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(farmc), "build", str(src), "-o", str(out_exe)]
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout or "")
        sys.stderr.write(p.stderr or "")
        raise SystemExit(f"build failed: {src}")


def median_times(exe: Path, cwd: Path, warmup: int, timed: int) -> list[float]:
    walls = []
    for i in range(warmup):
        rc, dt, out = run_exe(exe, cwd)
        print(f"  warmup {i+1}/{warmup} rc={rc} wall={dt:.3f}s")
        if rc != 0:
            raise SystemExit(f"warmup failed rc={rc}\n{out}")
    for i in range(timed):
        rc, dt, out = run_exe(exe, cwd)
        print(f"  timed  {i+1}/{timed} rc={rc} wall={dt:.3f}s")
        if rc != 0:
            raise SystemExit(f"timed failed rc={rc}\n{out}")
        walls.append(dt)
    return walls


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("farmc")
    ap.add_argument("--out", default="out/m12_arc")
    args = ap.parse_args()
    farmc = Path(args.farmc).resolve()
    root = Path(__file__).resolve().parents[3]
    bench = Path(__file__).resolve().parent
    out = Path(args.out)
    if not out.is_absolute():
        out = (Path.cwd() / out).resolve()
    out.mkdir(parents=True, exist_ok=True)

    print(f"farmc={farmc}")
    print(f"out={out}")
    print(f"FARMOS_XESS_LIB={os.environ.get('FARMOS_XESS_LIB', '')}")

    native4 = out / "ac04_native"
    xess4 = out / "ac04_xess"
    native5 = out / "ac05_native"
    xess5 = out / "ac05_xess"
    if os.name == "nt":
        native4 = native4.with_suffix(".exe")
        xess4 = xess4.with_suffix(".exe")
        native5 = native5.with_suffix(".exe")
        xess5 = xess5.with_suffix(".exe")

    print("building AC-M12-04/05…")
    build(farmc, bench / "ac04_native_1080.fm", native4)
    build(farmc, bench / "ac04_xess_960.fm", xess4)
    build(farmc, bench / "ac05_native_1f.fm", native5)
    build(farmc, bench / "ac05_xess_1f.fm", xess5)

    print("AC-M12-04 native (12 in-process frames)…")
    rc, dt, outn = run_exe(native4, out)
    print(f"  rc={rc} wall={dt:.3f}s\n{outn.strip()}")
    if rc != 0:
        return rc
    print("AC-M12-04 XeSS (12 in-process frames)…")
    rc, dt, outx = run_exe(xess4, out)
    print(f"  rc={rc} wall={dt:.3f}s\n{outx.strip()}")
    if rc != 0:
        return rc

    npng = out / "native_1080.png"
    xpng = out / "xess_1080.png"
    m = png_metrics(npng, xpng)
    print("AC-M12-04 metrics:")
    for k, v in m.items():
        print(f"  {k}={v if isinstance(v, str) else (v if isinstance(v, int) else f'{v:.3f}')}")

    print("AC-M12-05 native process-per-frame…")
    nw = median_times(native5, out, 8, 9)
    print("AC-M12-05 XeSS process-per-frame…")
    xw = median_times(xess5, out, 8, 9)
    nmed = statistics.median(nw)
    xmed = statistics.median(xw)
    ratio = nmed / xmed if xmed > 0 else 0.0
    print(f"native_median={nmed:.3f}s range={min(nw):.3f}-{max(nw):.3f}")
    print(f"xess_median={xmed:.3f}s range={min(xw):.3f}-{max(xw):.3f}")
    print(f"ratio native/xess={ratio:.3f}  SHOULD>={SHOULD_RATIO}  binding=wall_xess<wall_native")
    ac05 = "PASS" if xmed < nmed else "FAIL"
    ac05_should = "PASS" if ratio >= SHOULD_RATIO else "FAIL"
    dL_means = m["silver_dL_means"]
    print(f"AC-M12-05 binding (strictly faster)={ac05}")
    print(f"AC-M12-05 SHOULD {SHOULD_RATIO:.2f}x={ac05_should}")
    print(
        f"AC-M12-04 ROI L native/xess={m['silver_L_native']:.3f}/{m['silver_L_xess']:.3f} "
        f"|dL_means|={dL_means:.3f} mean_abs_dL={m['silver_mean_abs_dL']:.3f} "
        f"(report-only; not |dL|) png_tol/max_abs={m['png_tol_12_report']} report-only"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Mean luminance of an 8-bit RGB PNG crop (M11 silver ROI helper).

Usage:
  measure_roi.py <png> [x0 y0 x1 y1]

Default crop is the full image. L = 0.2126 R + 0.7152 G + 0.0722 B in 0–255.
CLAIM artwork still uses x=940:980, y=520:560 on 1920×1080.
"""
from __future__ import annotations

import struct
import sys
import zlib


def decode_rgb8(path: str):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"not a PNG: {path}")
    pos = 8
    width = height = None
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
    pixels = []
    for y in range(height):
        row = raw[y * stride : (y + 1) * stride]
        if row[0] != 0:
            raise SystemExit(f"unsupported filter {row[0]}")
        for x in range(width):
            i = 1 + x * bpp
            pixels.append((row[i], row[i + 1], row[i + 2]))
    return width, height, pixels


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = sys.argv[1]
    w, h, pix = decode_rgb8(path)
    if len(sys.argv) >= 6:
        x0, y0, x1, y1 = (int(sys.argv[i]) for i in range(2, 6))
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    x0 = max(0, min(w, x0))
    x1 = max(0, min(w, x1))
    y0 = max(0, min(h, y0))
    y1 = max(0, min(h, y1))
    if x1 <= x0 or y1 <= y0:
        raise SystemExit("empty crop")
    n = 0
    acc = 0.0
    ar = ag = ab = 0.0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = pix[y * w + x]
            acc += 0.2126 * r + 0.7152 * g + 0.0722 * b
            ar += r
            ag += g
            ab += b
            n += 1
    mean_l = acc / n
    print(f"png={path}")
    print(f"size={w}x{h}")
    print(f"roi=x{x0}:{x1},y{y0}:{y1} n={n}")
    print(f"mean_rgb={ar / n:.3f},{ag / n:.3f},{ab / n:.3f}")
    print(f"mean_L={mean_l:.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

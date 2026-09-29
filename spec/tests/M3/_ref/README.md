# M3 reference golden generator (NOT product code)

`raster_ref.py` is a **specification reference** used only to generate deterministic
golden PNG files and SHA-256 digests for `spec/tests/M3/*.golden.png`.

It is **not** `farmc`, not the Farmos runtime, and not stdlib. Poteto MUST implement
the normative algorithm in `spec/M3-scene.md` §12 inside the product; matching these
goldens is the acceptance check.

Regenerate:

```
python3 raster_ref.py
```

Writes `../NNN_*.golden.png` and `goldens_sha256.txt`.

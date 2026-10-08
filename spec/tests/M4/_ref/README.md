# M4 reference path tracer (NOT product code)

`path_ref.py` generates the golden PNGs and SHA-256 lines for `spec/tests/M4`.
It is not `farmc`, not the runtime, and not the stdlib.

It implements the shading, sampling, and camera rules in `spec/M4-ray.md` and
is normative for numeric goldens the way `spec/tests/M3/_ref/raster_ref.py` is
for M3, including expression order. Intersection here is a **brute-force**
closest-hit scan. The product MUST still build the BVH in M4-ray.md; for these
scenes a correct BVH returns the same hit as the scan (same tie-break).

The PNG writer is the M3 §11.3 canonical writer copied from `raster_ref.py`.
`raster_ref.py` is not modified.

`albedo.png` is a 4×4 RGB8 checker (red / blue) written by this script. Fixture
`010_albedo_texture.fm` loads it as `_ref/albedo.png` with CWD = `spec/tests/M4`.

Regenerate from anywhere:

```
python3 spec/tests/M4/_ref/path_ref.py
```

Each `scene_NNN` function is the Python twin of `NNN_*.fm` (same geometry,
materials, camera, sample indices, and bounce limit).

# M15 CLAIM — liquid / MP4

**Status:** PENDING (Linux CPU fixtures + size/DCE in this PR). Windows PE size and TommyLaptop ffmpeg duration AC are CLAIM-PENDING until a Windows run.

**Context tip:** master `8b19896b`. Product GPU tip `4d7523ec` unchanged.

## Sim stencil (binding; matches `liquid_ref.py`)

Collocated shallow-water on an XZ grid. Cell `k = i + j*nx` holds `h`, `u`, `w` (IEEE 754 binary64, no `-ffast-math`).

1. Momentum: interior `u -= g * (h[i+1]-h[i-1]) / (2 hx) * dt` and the same for `w` on Z. Wall cells force `u=0` on `i=0,nx-1` and `w=0` on `j=0,nz-1`.
2. Continuity: upwind face mass flux; domain-border faces are 0 (reflective / zero normal flux).
3. Clamp `h = max(h, 0)` and zero velocity in dry cells.
4. `g = 9.81`. Default `LiquidWorld` dt = `1/60`.

Mesh sync after each successful `step` if `linkMesh` is set:

- Vertex `k = i + j*nx` at local `( (i+0.5)*hx - 0.5*nx*hx, h[k], (j+0.5)*hz - 0.5*nz*hz )`.
- Geometry is rebuilt to an `nx*nz` XZ grid on first sync if the current vertex count differs.
- Quad winding is CCW from +Y (`a,d,c` / `a,c,b`) so the M3 raster back-face test keeps the surface visible from above.
- Mesh scale / object pose are not written.

## MP4 (`Renderer.saveMP4`)

Every `render()` while `farm_video.c` is linked (`-DFARM_ENABLE_VIDEO`) appends the display RGB8 buffer.

`saveMP4(path)` writes a raw `rgb24` temp and runs:

```
ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s WxH -r 30 -i <temp.rgb>
  -c:v libx264 -preset veryfast -pix_fmt yuv420p -movflags +faststart <path>
```

If libx264 is missing, retry `-c:v mpeg4 -pix_fmt yuv420p`.

`FARMOS_VIDEO_STRICT=1` and missing/failed ffmpeg (or `FARMOS_VIDEO_FORCE_FAIL=1`) traps **123** `runtime error: MP4 encode failed`. Soft product path: missing ffmpeg is a no-op when STRICT is unset.

Harness: `# requires_ffmpeg: true` soft-skips. `# mp4:` probes duration ≈ 5.0 s, 30 fps, 150 frames, 640×360. No MP4 SHA.

## TommyLaptop (Windows) path

1. Put `ffmpeg` on PATH (https://ffmpeg.org). `ffprobe` optional but used by the harness `# mp4:` check.
2. `farmc build examples\08_liquid_5s.fm -o liquid_5s.exe`
3. `.\liquid_5s.exe` writes `liquid_5s.mp4` in CWD (~5 s @ 30 fps @ 640×360).
4. Size ACs (`S_liq − S_scene ≤ 96 KiB`, hello ≤ 20 KiB, video glue ≤ 32 KiB) are PE-enforced by `scripts\check_size_budgets.ps1`.

## Floors not touched

M12 ≥1.05× / M13 ≥1.20× / M14 ≥1.50×, trap 117–122, M5 body / RD-M5-04 fluids OUT of M5, no PE-bundled H.264, no window / frame-gen / DLSS.

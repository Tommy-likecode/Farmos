# M15 CLAIM — liquid / MP4 — PASS

**Verdict:** **CLAIM PASS** (PM recorded from TommyLaptop Windows verification, Asia/Shanghai 2026-10-31)  
**Do not merge.** HOLD until eggtooth **AUDIT PASS**. Product GPU tip `4d7523ec` **unchanged**. No PE-bundled H.264. Official ffmpeg on PATH only.

| Field | Value |
|-------|--------|
| PR | #25 draft https://github.com/Tommy-likecode/Farmos/pull/25 |
| Branch | `cursor/m15-liquid-mp4-7365` |
| Tip (measured Windows) | `93b50c45d25a6c8cb122a86805055cc83cfcff80` |
| Master / context tip | `8b19896b` |
| Product GPU tip | `4d7523ec` (**unchanged**) |
| Host | TommyLaptop Windows, Asia/Shanghai 2026-10-31 |
| Checkout | `D:\Tommy\Farmos` @ `93b50c45` |
| Rebuild | `cmake --build build --config Release` → `farmc.exe` OK |
| Spec | `spec/M15-liquid-mp4.md` **FINAL** (OQ-M15-01..10 A) |
| Video | External `ffmpeg` on PATH (libx264). No PE-bundled codec. |

## Binding meters (explicit)

1. **MP4 (OQ-M15-06 A):** `Renderer.saveMP4` via external ffmpeg. Demo `out\liquid_5s.mp4`: codec **h264**, **640×360**, `r_frame_rate=30/1`, **150** frames, duration **5.000000** s. Process **EXIT=0**. No bitstream SHA.
2. **Hello ≤ 20 KiB:** PE **13824** ≤ 20480.
3. **S_liq − S_scene ≤ 96 KiB:** **4096** (S_liq=33792, S_scene=29696) ≤ 98304.
4. **saveMP4 glue ≤ 32 KiB:** **512** (S_vid=34304 − S_liq=33792) ≤ 32768.
5. **Unused liquid DCE:** OK (size script). Prior B-01..B-11 / M7–M12 gates OK.
6. **Linux:** `ctest -R '^m15_'` **15/15**; unused-scene DCE hello=14512; ELF S_liq−S_scene **4104**, video glue **88** (informational; PE enforce is Windows).
7. **Floors not touched:** M12 ≥1.05× / M13 ≥1.20× / M14 ≥1.50×; traps 117–122; M5 body / RD-M5-04 fluids OUT of M5; no window / frame-gen / DLSS/Enabler.

## Windows measured (tip `93b50c45`)

| Gate | Value | Budget | Result |
|------|-------|--------|--------|
| `out\m15_liquid_5s.exe` | 38400 B PE; EXIT=0 | — | **PASS** |
| ffprobe duration | **5.000000** s | ≈ 5.0 s | **PASS** |
| ffprobe fps / frames / size | 30/1 · 150 · 640×360 · h264 | 30 fps · 150 · 640×360 | **PASS** |
| hello | **13824** B | ≤ 20480 | **PASS** |
| S_liq − S_scene | **4096** B (33792 − 29696) | ≤ 98304 | **PASS** |
| saveMP4 glue | **512** B (34304 − 33792) | ≤ 32768 | **PASS** |
| unused liquid DCE | OK | byte-identical unused import | **PASS** |
| B-01..B-11 / M7–M12 | OK | prior floors | **PASS** |

`scripts\check_size_budgets.ps1 -Farmc build\Release\farmc.exe -RepoRoot .` → **PASS size_budgets**.

Demo path on TommyLaptop:

```
farmc build examples\08_liquid_5s.fm -o out\m15_liquid_5s.exe
.\out\m15_liquid_5s.exe
# wrote out\liquid_5s.mp4
```

## Linux verification (same tip lineage; no PE enforce)

| Command | Result |
|---------|--------|
| `ctest --test-dir build -R '^m15_'` | **15/15 PASS** (`# mp4:` 5.0 s / 30 fps / 150 / 640×360) |
| `python3 scripts/test_unused_scene_dce.py build/farmc .` | **PASS** hello=14512 |
| `python3 scripts/check_size_budgets.py build/farmc .` | **PASS** — ELF S_liq−S_scene **4104**; glue **88**; hello **14512** |

## Sim stencil (binding; matches `liquid_ref.py`)

Collocated shallow-water on an XZ grid. Cell `k = i + j*nx` holds `h`, `u`, `w` (IEEE 754 binary64, no `-ffast-math`).

1. Momentum: interior `u -= g * (h[i+1]-h[i-1]) / (2 hx) * dt` and the same for `w` on Z. Wall cells force `u=0` on `i=0,nx-1` and `w=0` on `j=0,nz-1`.
2. Continuity: upwind face mass flux; domain-border faces are 0 (reflective / zero normal flux).
3. Clamp `h = max(h, 0)` and zero velocity in dry cells.
4. `g = 9.81`. Default `LiquidWorld` dt = `1/60`. Two `step()` per display frame @ 30 fps.

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

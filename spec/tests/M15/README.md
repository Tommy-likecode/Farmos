# Farmos M15 liquid / MP4 test fixtures

Sources use the M1 TypeScript-style surface, M2 math, M3 scene + Renderer,
and the M15 liquid module (`import { … } from "farmos:liquid"`) plus
`Renderer.saveMP4` (external ffmpeg; OQ-M15-06 A). See
`spec/M15-liquid-mp4.md` (**FINAL** — OQ-M15-01..10 all **accepted A**).

**Status:** **FINAL** (PM/user 2026-10-31: OQ-M15-01..10 A). Binding ACs in
`M15-liquid-mp4.md` §7. poteto implementing after PM assign.

Cite (do not rewrite) prior floors:

- hello ≤ **20 KiB**; unused liquid / video DCE
- M5 RD-M5-04: fluids remain OUT **of M5**; M15 is additive
- M12–M14 perf / quality / OUT lists MUST NOT be weakened by this feature work
- Traps 117–122 unchanged; M15 trap **123**-family
- Context tip: master `8b19896b`; product `4d7523ec` (informational)

This file documents **M15-only** outline notes. It does not replace
`spec/tests/README.md`. Harness kinds `run`, `run_approx`, `runtime_trap`,
`run_png` keep prior meanings. New header (binding):

| Header | When to soft-skip |
|--------|-------------------|
| `# requires_ffmpeg: true` | `ffmpeg` not on PATH / encode probe fails |

## Layout

```
spec/tests/M15/
  README.md                 (this file — FINAL outline)
  _ref/liquid_ref.py        (oracle — poteto ships with CLAIM)
  NNN_name.fm               (MAY add when coding)
  NNN_name.expected
```

## Planned fixture themes

| Theme | kind (likely) | Notes |
|-------|---------------|-------|
| Import smoke | run | `farmos:liquid` names resolve |
| Empty / default step | run / run_approx | Finite heights |
| Ripple / dam-break band | run_approx | Oracle heights (§3.1) |
| Mesh sync | run_approx | Linked Mesh Y / displacement |
| Invalid dt / resolution | runtime_trap | exit **123** `invalid liquid parameter` |
| Use after dispose | runtime_trap | exit **105** |
| MP4 smoke | run (+ `# requires_ffmpeg`) | exit 0; ~150 frames / ~5.0 s / 640×360 via ffprobe; two steps/frame |
| MP4 STRICT missing ffmpeg | runtime_trap | 123 `MP4 encode failed` under `FARMOS_VIDEO_STRICT=1` |
| DCE / size | script AC | hello ≤20; S_liq − S_scene ≤ 96 KiB |

**Video goldens:** do **not** require SHA-256 of `.mp4`. Prefer duration /
frame-count / resolution / exit. MAY extract first/last PNG for smoke.

## Working directory

Harness MUST run M15 programs with CWD = `spec/tests/M15` when local
fixtures exist.

## Fixtures

Oracle: `spec/tests/M15/_ref/liquid_ref.py` (collocated SWE; CLAIM stencil).
CPU liquid fixtures always run. `# requires_ffmpeg: true` soft-skips MP4 when
`ffmpeg` is not on PATH. `# mp4:` asks the harness to probe duration / fps /
frame count / 640×360 (no bitstream SHA).

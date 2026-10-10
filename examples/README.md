# Farmos example programs (M6)

Normative names (`spec/M6-hardening.md` §9). Each MUST build with `farmc` and smoke-run.

| File | Shape | Budget |
|------|-------|--------|
| `01_hello.fm` | M1 hello | ≤ 20 KiB stripped |
| `02_rotating_cube.fm` | M3 rotating cube → PNG | ≤ 96 KiB |
| `03_cornell_path.fm` | M4 Cornell `renderPath` | (hero / docs) |
| `04_glass_mirror.fm` | M4 glass + mirror | ≤ 300 KiB |
| `05_stacking_bounce.fm` | M5 stack + bounce | S_demo − S_scene ≤ 64 KiB |

Bodies are production demos derived from `spec/tests/M3|M4|M5` fixtures. Smoke: `scripts/run_examples.ps1` / `scripts/run_examples.sh`. `examples/m4_cornell_800x600.fm` is the B-05 peak-RAM driver (not part of the 01..05 smoke set).

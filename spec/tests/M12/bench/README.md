# M12 Arc bench (TommyLaptop)

Prefer `PATH_BACKEND_VULKAN_RT` on Intel Arc. Official Intel XeSS only
(`FARMOS_XESS_LIB` / `runtime/third_party/xess/libxess.dll`). Enabler / Swapper /
OptiScaler stay **OUT**.

Fixtures keep `PATH_GLOSSY_VNDF`, 64 spp, `PATH_DENOISE_SPATIAL`, and locked
silver-still composition with **fixed** artwork positions (no `random01`).

| Lane | Setup | Program |
|------|--------|---------|
| Native | 1920×1080, `PATH_UPSCALE_OFF` | `ac04_native_1080.fm` (12 in-process) / `ac05_native_1f.fm` |
| Upscale | render 960×540 → display 1920×1080, `PATH_UPSCALE_XESS` | `ac04_xess_960.fm` / `ac05_xess_1f.fm` |

```
python3 spec/tests/M12/bench/run_arc_ac.py path/to/farmc --out out/m12_arc
```

Record median wall-clock after warm-up (≥8 frames). Primary quality: silver
ball-center ROI (`x=940:980,y=520:560`) mean L both lanes in [80,140] and
|ΔL| ≤ 5.0. Report mean_abs; `# png_tol: 12` / max_abs are **report-only**.
Do **not** alter silver-still composition.

AC-M12-04 / AC-M12-05 are eggtooth / TommyLaptop gates. Linux without SDK does
not run this bench.

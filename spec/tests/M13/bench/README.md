# M13 Arc bench notes

Reuse M12 fixtures under `spec/tests/M12/bench/` for the silver still
(960×540→1920×1080, `PATH_GLOSSY_VNDF`). Do not duplicate artwork.

TommyLaptop Arc command:

```
python3 spec/tests/M13/bench/run_arc_ac.py path/to/farmc --out out/m13_arc
```

Primary timing is in-process `renderPath` samples from
`FARMOS_PATH_FRAME_MS=1` after dropping warmup frames. Process-per-frame
AC-M12-05 still runs as the M12 ≥1.05× floor check.

Pass1: delete `%TEMP%/farmos_vk_rt_pso_*.bin` and
`%TEMP%/farmos_xess_pso_*.bin` then run the script.
Pass2: run again with those caches present.

SHOULD ≥ **1.20×** both passes (OQ-M13-01 A / OQ-M13-02 A).
M12 floor ≥ **1.05×** and XeSS wall strictly < native still bind.

Linux without SDK:

```
python3 spec/tests/M13/bench/run_linux_host_amort.py path/to/farmc --out out/m13_linux
```

# M14 Arc bench notes

Reuse M12 fixtures under `spec/tests/M12/bench/` for the silver still
(960×540→1920×1080, `PATH_GLOSSY_VNDF`). Do not duplicate artwork.

TommyLaptop Arc command:

```
python3 spec/tests/M14/bench/run_arc_ac.py path/to/farmc --out out/m14_arc
```

Primary timing is in-process `renderPath` samples from
`FARMOS_PATH_FRAME_MS=1` after dropping warmup frames. After H2, those
samples are GPU-pipeline: XeSS 1080p host download is flushed on
`savePNG`, not inside timed `renderPath`. GPU XeSS execute is still
timed. Process-per-frame AC-M12-05 still runs as the M12 ≥1.05× floor
(and already includes `savePNG` / flush). Do not claim equal-work vs
`945c13ae`. No flush-every-frame secondary inproc meter.

Pass1: delete `%TEMP%/farmos_vk_rt_pso_*.bin` and
`%TEMP%/farmos_xess_pso_*.bin` then run the script.
Pass2: run again with those caches present.

SHOULD ≥ **1.50×** both passes (OQ-M14-01 A / OQ-M14-02 A).
M13 inproc ≥ **1.20×** and M12 process-per-frame ≥ **1.05×** remain floors.
XeSS wall strictly < native still binds.

Do **not** compare in-process ms to `d23acfb3` process-per-frame seconds.

Linux without SDK:

```
python3 spec/tests/M14/bench/run_linux_host_amort.py path/to/farmc --out out/m14_linux
```

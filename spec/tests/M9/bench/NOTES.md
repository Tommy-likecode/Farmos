# M9 silver-still HW RT bench notes

**Status: AC-M9-12..14 pending TommyLaptop / poteto.**

This Linux CI / cloud-agent host does not provide a hardware ray-tracing device
(`VK_KHR_ray_query` + acceleration structures). No Windows wall-clock numbers
are recorded here.

Post-M8 compute pain point (user, TommyLaptop): ~32 s at 1920×1080 / 256 spp.
M9 primary gate is median ≤ 4 s after one warm-up on that host with
`PATH_BACKEND_VULKAN_RT` actually running (not a silent compute/CPU downgrade).
Secondary gate is ≥ 8× vs the post-M8 compute baseline on the same host.

Fill this file after the TommyLaptop session:

| Field | Value |
|-------|--------|
| Host | TommyLaptop |
| GPU | (fill) |
| farmc SHA | (fill) |
| HW RT median ms (compat defaults) | pending |
| HW RT median ms (RR/MIS if used) | pending |
| Post-M8 compute median ms | pending (~32 s class) |
| Speedup vs compute | pending |
| Cold first-call ms (AS build) | pending |
| Downgrade? | must be no for the AC run |

`farmc version`: `farmc 1.01`.

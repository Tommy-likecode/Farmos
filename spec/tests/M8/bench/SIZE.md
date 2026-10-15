# AC-M8-10 size / DCE notes

Measured on the M8 Linux cloud agent (ELF, stripped via `llvm-strip --strip-all` / `strip --strip-all` when available) with `python3 scripts/check_size_budgets.py build/farmc .`.

Windows PE budgets are the normative ACs; Linux numbers are informative but show the same DCE gates.

| Artifact | Size (this Linux ELF) | Budget | Status |
|----------|----------------------|--------|--------|
| Hello `examples/01_hello.fm` | 14512 B | ≤ 20 KiB (20480) | OK |
| CPU glass+mirror `examples/04_glass_mirror.fm` | 55648 B | ≤ 300 KiB (307200) | OK |
| GPU-linked `examples/06_vulkan_cornell.fm` | 153984 B | ≤ 400 KiB (409600) | OK |

DCE:

- Hello / unused-math size match (14512 B); hello links only `farm_rt.c`.
- No Vulkan loader symbols (`vulkan-1.dll` / `libvulkan.so.1` / `vkGetInstanceProcAddr`) on hello, unused-math, scene-only, rotating-cube, or CPU glass+mirror.
- CPU glass+mirror does **not** link `farm_vk.c`.
- No thread-start imports on hello / unused-math / scene-only / cube (B-07).
- `farmc version` still contains `1.01`.

M8 RR / MIS / adaptive / GPU BVH live in `farm_ray.c` / `farm_vk.c`, reachable only from path-tracing / Vulkan opt-in units, so hello DCE still holds.

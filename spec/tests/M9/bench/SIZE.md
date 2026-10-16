# AC-M9-10 size / DCE notes

Measured on the M9 Linux cloud agent (ELF, stripped via `llvm-strip --strip-all` /
`strip --strip-all` when available) with `python3 scripts/check_size_budgets.py build/farmc .`.

Windows PE budgets are the normative ACs; Linux numbers are informative but show
the same DCE gates.

Fill after `local_size_budgets` / the size script on this tree.

| Artifact | Budget |
|----------|--------|
| Hello `examples/01_hello.fm` | ≤ 20 KiB; no Vulkan/RT symbols |
| CPU glass+mirror `examples/04_glass_mirror.fm` | ≤ 300 KiB; no Vulkan at load |
| Compute-only GPU-linked `examples/06_vulkan_cornell.fm` | ≤ 400 KiB; no `farm_vk_rt.c` |
| RT-linked `examples/07_vulkan_rt_cornell.fm` | ≤ **500 KiB** |

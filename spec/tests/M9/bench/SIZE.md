# AC-M9-10 size / DCE notes

Measured on the M9 Linux cloud agent (ELF, stripped via `llvm-strip --strip-all` /
`strip --strip-all` when available) with `python3 scripts/check_size_budgets.py build/farmc .`.

Windows PE budgets are the normative ACs; Linux numbers are informative but show
the same DCE gates. Script result: **PASS size_budgets**.

| Artifact | Linux ELF | Budget | Status |
|----------|-----------|--------|--------|
| Hello `examples/01_hello.fm` | 14512 B | ≤ 20 KiB; no Vulkan/RT symbols | OK (DCE) |
| CPU glass+mirror `examples/04_glass_mirror.fm` | 59744 B | ≤ 300 KiB; no Vulkan at load | OK (DCE) |
| Compute-only GPU-linked `examples/06_vulkan_cornell.fm` | 153984 B | ≤ 400 KiB; no `farm_vk_rt.c` | OK (DCE) |
| RT-linked `examples/07_vulkan_rt_cornell.fm` | 235944 B | ≤ **500 KiB** | OK (DCE) |

B-04 `S_demo − S_scene` = 12288 B (budget 65536 B). B-06 unused-math size matches hello.
PE budgets are not enforced on ELF; Windows TommyLaptop still gates B-01/B-03/B-08/B-09.

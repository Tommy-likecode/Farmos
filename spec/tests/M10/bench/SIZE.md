# AC-M10-09 size / DCE notes

Windows PE budgets are normative. Linux ELF sizes are informative.

| Artifact | Budget |
|----------|--------|
| hello | ≤ 20 KiB; no Vulkan / RT / denoise symbols |
| CPU glass+mirror | ≤ 300 KiB; no `farm_denoise.c` |
| Compute-linked | ≤ 400 KiB |
| RT-linked | ≤ 500 KiB |
| Denoise-linked (path + `PATH_DENOISE_SPATIAL`) | ≤ **600 KiB** |

See `scripts/check_size_budgets.py` B-10. TommyLaptop PE numbers go in NOTES when measured.

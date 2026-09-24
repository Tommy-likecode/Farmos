# Vendored Ryu (float -> shortest decimal)

- Upstream: https://github.com/ulfjack/ryu
- Commit: 4c0618b0e44f7ef027ebae05d2cc7812048f7c8f (2026-02-10)
- License: dual Apache-2.0 OR Boost Software License 1.0 (see LICENSE-Apache2, LICENSE-Boost).
- Files copied unmodified from `ryu/`: d2s.c, ryu.h, common.h, digit_table.h, d2s_intrinsics.h, d2s_small_table.h.
- Built with RYU_OPTIMIZE_SIZE (small tables) via `#include "ryu/d2s.c"` from runtime/farm_rt.c.
- Reference: Ulf Adams, "Ryu: fast float-to-string conversion", PLDI 2018.

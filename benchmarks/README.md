# Farmos benchmarks (M6)

Informative timings only. CI MUST assert each driver exits 0 (`spec/M6-hardening.md` §10).

| ID | Intent | Driver (poteto) |
|----|--------|-----------------|
| BM-a | Empty loop | `benchmarks/a_empty_loop.fm` |
| BM-b | M2 vector ops | `benchmarks/b_vector_ops.fm` |
| BM-c | M5 600-step stack | `benchmarks/c_stack_600.fm` |
| BM-d | M4 tiny path render | `benchmarks/d_tiny_path.fm` |

Entry script (Windows): `scripts\run_benchmarks.ps1`. Linux: `scripts/run_benchmarks.sh`. CI asserts each process **exit 0**; printed wall times are informative (no golden times).

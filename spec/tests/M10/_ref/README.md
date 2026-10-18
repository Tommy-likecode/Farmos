# M10 reference notes

CPU goldens with **denoise OFF** reuse M4 / M7 / M8 / M9 twin SHA-256 values (see
`spec/tests/M4/_ref/goldens_sha256.txt` and M7 `_ref`). The M4 oracle remains
authoritative for compat.

Denoise-ON stills compare against a **256-spp undenoised** reference (locked
silver-still composition) with `# png_tol: 8` under OQ-M10-03 A, or smoke-only
for small harness fixtures (004).

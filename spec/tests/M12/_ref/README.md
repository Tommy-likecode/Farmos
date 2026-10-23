# M12 reference assets

CPU OFF twin 001 reuses the M4-001 SHA (see `spec/tests/M4/_ref/goldens_sha256.txt`).

XeSS/FSR ON goldens are Arc-side: Linux CI soft-skips `# requires_xess:` /
`# requires_fsr:` when the vendor SDK is absent. Do not check in Enabler /
Swapper / OptiScaler / NGX blobs. Vendor SDK DLLs stay **external** (fetch
script + side-by-side load); they do not count toward the Farmos PE cap.

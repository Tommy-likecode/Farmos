# CLAIM: silver orbit frame0 ball is black

**Status:** PASS — root cause identified  
**Assignment:** investigate-metal-black (PM)  
**Tip:** `9c5eebd` (PR #17 draft, not merged)  
**Investigator:** code + spec + still/orbit diff + closed PRs #12/#13 + CPU-side NEE reconstruction  
**Not done:** 72-frame re-encode; artwork env-brighten / RectAreaLight / background edits; renderer patch (see §6)

---

## CLAIM (PASS)

The silver sphere is **not** missing an RT-only env/area-light path, and denoise is **not** the thing that first turns it black.

`metalness=1`, `roughness=0.04` is a **rough conductor** under M4. Rough surfaces never take the delta-mirror continuation, never sample GGX for the bounce, and metals zero the Lambert weight. The ball is lit **only** by GGX next-event estimation of the two `RectAreaLight`s. At the **image-center** hit, that lobe points back at the camera / `scene.background`, where there is no area light, so unbiased NEE ≈ 0. Sparse white specks are the overhead key light’s GGX fireflies on a different part of the sphere (~80 px above center at 1080p), not a filled silver material.

This is **specified M4 behavior**, copied identically into Vulkan compute and HW RT shaders. Linux CPU 8 spp orbit smoke already showed a black silver ball. v3 env-brighten and v4 reflection-card failed for the same reason (they light Lamberts / add a mesh the metal never bounces to).

Oct 14 “v1 silver” is the **locked lighting mood** (large softboxes, no sun, grey cyclorama). It is not a filled chrome ball: the same roughness-0.04 contract applies at 256 spp, only with more fireflies. The first still draft used `roughness=0` (true mirror → ~106/255 from background at ball center). Softening to 0.04 dropped that path.

---

## 1. Still vs orbit — deltas that matter

| | `examples/artwork_silver_still.fm` | `examples/artwork_silver_orbit.fm` (frame 0) |
|---|---|---|
| Camera | `(4.55, 2.25, 5.35)` lookAt `(0, 0.52, 0)` | same (`angle=0`) |
| Silver | albedo `(0.92, 0.93, 0.95)`, metal 1, rough **0.04**, r=0.52 at `(0, 0.52, 0)` | **identical** |
| Lights | key `RectAreaLight(0xffffff, 6.5, 8.0, 6.5)` @ `(0.15, 5.8, 0.4)`; fill `0xffe6cc, 3.2, 3.5, 2.6` @ `(-2.8, 3.1, 2.0)` | **identical** |
| Background | `(0.42, 0.45, 0.50)` | **identical** |
| Mattes | `random01()` in ranges | **midpoints** of those ranges |
| spp | **256** | **64** (was 8) |
| Denoise | **off** (never called) | **`PATH_DENOISE_SPATIAL`** |
| Backend | prefer `VULKAN_RT` else compute else CPU | same |

**Do not matter for a black chrome ball:** fixed vs random matte XZ (objects sit off-center; they cannot replace missing glossy env). Frame-0 camera matches the still.

**Do matter for how obvious the fireflies are:** 256 vs 64/8 spp; denoise on vs off (preserves salt-and-pepper, does not invent env).

**Historical backend:** still landed in `a8a7554` (2026-10-14) with `PATH_BACKEND_VULKAN` only. `PATH_BACKEND_VULKAN_RT` was added to the still in M9 (`52b1aeb`, 2026-10-18). A TommyLaptop PNG from 2026-10-14 is **compute (or CPU), not HW RT**.

---

## 2. Integrator (all backends)

M4 is explicit:

- `spec/M4-ray.md:43` — “Indirect GGX rays on rough surfaces” out of scope; rough indirect is **one cosine-hemisphere diffuse ray**.
- `spec/M4-ray.md:166` — `rough == 0` is delta; `rough > 0` is rough; **no roughness epsilon** (OQ-M4-08 A).
- `spec/M4-ray.md:533-558` — bounce weight `(1−Fview)*(1−metal)*(1−trans)*albedo`. “Metal … therefore send no diffuse indirect; **a rough metal is lit only by the direct specular lobe**.”

CPU (`runtime/farm_ray.c`):

```991:1014:runtime/farm_ray.c
    } else if (rough == 0.0) {
      throughput = cmul(throughput, schlick(f0_of(albedo, metal), ndotv));
      d = reflect_dir(d, n);
      ...
    } else {
      ... cosine_sample ...
      V3 weight = v3((1.0-fv.x)*(1.0-metal)*(1.0-trans)*albedo.x, ...);
      throughput = cmul(throughput, weight);
```

HW RT compute (`runtime/farm_vk_rt_path.comp`) is the same BSDF; only closest/any-hit is `rayQuery`:

```516:533:runtime/farm_vk_rt_path.comp
    } else if (rough == 0.0) {
      throughput *= schlick(f0_of(albedo, metal), ndotv);
      d = reflect_dir(d, n);
      ...
    } else {
      ... cosine_sample ...
      vec3 weight = (vec3(1.0) - fv) * (1.0 - metal) * (1.0 - trans) * albedo;
      throughput *= weight;
```

`diff` of `farm_vk_path.comp` vs `farm_vk_rt_path.comp`: traversal only (software BVH vs `VK_KHR_ray_query`). `direct_one` GGX, RectArea uniform NEE, `ambient_term` with `(1-metal)`, material pack `m[4]=roughness; m[5]=metalness` (`farm_vk.c:718` / `farm_vk_rt.c:1300`) match.

Ambient is also zero on metals (`farm_vk_rt_path.comp:415-417`). Background is added **only on a miss** (`farm_vk_rt_path.comp:457-458`).

Fixture `spec/tests/M4/007_metal_vs_rough.fm` is the gold picture of this contract: roughness 0 reflects the red backdrop; roughness 0.45 metal is a dark ball with a directional highlight.

---

## 3. Why ball-center mean ≈ 1/255 (reconstruction)

Camera frame 0, 1920×1080, FOV 34°, pixel (960.5, 540.5) hits the sphere at the facing point. Geometric normal ≈ toward camera. Mirror direction ≈ **back at the camera** (into `background`, not into the key).

Unbiased estimates (uniform-area Riemann sum of the **same** NEE estimator as M4 §10.5, unoccluded):

| Shading point | What it “sees” in the GGX lobe | Linear Y | 8-bit after clamp |
|---|---|---|---|
| Ball center (screen 960, 540) | camera / bg; lights out of lobe | **1.8e-5** | **0** |
| Same point if `roughness==0` (delta + bg) | `F0 * (0.42, 0.45, 0.50)` | **0.416** | **106** |
| Key-light highlight (screen ~958, **461**) | huge 8×6.5 key | **1.71** | **255** (if the estimator hits) |

GGX at roughness 0.04: `alpha = rough² = 0.0016`, `D_peak ≈ 1.24e5`. Fraction of the key’s **area** inside a 1% `D_peak` cone at the highlight ≈ **0.000125**. Expected “hot” NEE hits:

| spp | E[hot samples / highlight pixel] |
|---|---|
| 8 | 0.001 |
| 64 | 0.008 |
| 256 | 0.032 |

So even the **still at 256 spp** is salt-and-pepper in the highlight (a hot sample clamps to 255; the rest stay 0). Ball **center** stays ~0 at every spp. User’s ~1/255 center mean is this floor plus a rare firefly / 8-bit noise, not a broken tonemapper.

PNG quantize is `clamp01` then `floor(c*255+0.5)` (`farm_vk_rt.c:871-875`, `farm_ray.c:59-60`). NaN is not special-cased (NaN→0 on typical casts) but fireflies are finite HDR spikes, which is what “sparse highlight fireflies” looks like. **Tonemap/NaN is not the primary kill.**

---

## 4. Hypotheses — verified or discarded

| # | Hypothesis | Verdict |
|---|---|---|
| H1 | Missing env / area-light **reflection** for metals on the RT path | **CONFIRMED as the lighting model**, not RT-specific. Missing glossy **bounce** (env + meshes) on every backend. Area-light **NEE exists** and is the only metal lighting; it does not cover the ball center. |
| H2 | `PATH_DENOISE_SPATIAL` crushing specular to black | **Discarded as root cause.** GPU A-trous (`farm_vk_denoise.comp`, `SIGMA=0.35`, 3 iters step 1/2/4) is an HDR range filter: fireflies (`ΔL ≫ 0.35`) stay isolated; dark neighbors stay dark. Orbit agent Linux **CPU 8 spp** already had a black silver ball. Denoise cannot reconstruct a lobe that was never sampled. |
| H3 | Tonemap / NaN→0 / HDR clamp killing specular | **Secondary only.** Clamp turns rare unbiased Y≈1.7 hits into binary 0/255 fireflies; it does not zero a filled silver. Center was already ~0 in linear. |
| H4 | Wrong specular lobe / MIS on RT vs compute vs CPU | **Discarded as primary.** Same `direct_one` / same uniform RectArea pdf. Default sampling is `PATH_SAMPLING_M4` (orbit/still never call `setPathSampling`). M8 MIS weights **Lambert** pdf only (`farm_vk_rt_path.comp:401-408`) — irrelevant to metalness=1. Residual RT self-hit (`any_hit` `T_MIN=1e-6`, `RAY_EPS=1e-4`) could *darken* NEE further on Arc; it cannot explain CPU black. |
| H5 | Still also black on current master+RT | **Expected.** Same `.fm` materials. Oct 14 PNG predates M9. Re-render still @ 256 spp on CPU/compute/RT should all show black center + sparse upper fireflies. |

Closed product history (do not reopen):

- PR **#12** v3: brighter bg/wall/ceiling + hotter area lights → **whole-frame washout** (Lamberts pick up the extra light; metal still has no env bounce).
- PR **#13** v4: white **mesh** card + key 6.5→7.2 → silver stayed black (card is not a light; rough metal does not bounce to it).

---

## 5. Recommended FIX (renderer, not artwork)

**Do not** change `artwork_*.fm` lights, background, or env-brighten variants.

To get a **filled silver ball at v1 mood** (center ~100/255, soft highlights, floor/wall unchanged):

1. **Primary (product):** add **glossy / GGX (VNDF) continuation** for `metalness>0` (or all non-delta conductors) so roughness 0.04 reflects the existing `(0.42, 0.45, 0.50)` backdrop and nearby mattes. Must **not** be folded into M4 default SHA: new `PATH_SAMPLING_*` or `setPathGlossyIndirect` (M4 `007` right-hand sphere would otherwise pick up the red bg). This is an integrator extension, not a one-line RT bug.
2. **Secondary:** GGX/VNDF **light sampling + MIS** so the key-light highlight is not a 1/8000-area firefly (`spp ≫ 1/0.000125` to fill by brute force). Fixes sparkle; **does not** silver the ball center.
3. **Not recommended:** `setRoughness(0)` (already rejected as too sharp); raising RectArea / bg (v3); a mesh card (v4); hoping denoise or 64→256 spp fills the center.

**No renderer patch in this PR.** The behavior matches `spec/M4-ray.md`. A “minimal safe fix” that preserves goldens does not exist as a shader typo; shipping (1) needs an additive API + fixture policy.

---

## 6. TommyLaptop A/B (already running separately) — exact experiments

Host has no `VK_KHR_ray_query`. These confirm the reconstruction; they are not needed to identify the cause.

| ID | Program | spp | denoise | backend | Measure | Predict |
|---|---|---|---|---|---|---|
| A1 | still.fm (locked) | 256 | off | force CPU, then `VULKAN`, then `VULKAN_RT` + `FARMOS_PATH_GPU_STRICT=1` | mean RGB/Y in **32×32 at (960,540)**; max/mean in band **y=440–480, x=900–1020**; count pixels with Y>200 | All three: center **0–2/255**; highlight = sparse 255 fireflies, not a silver disk. RT ≉ unique failure. |
| A2 | orbit.fm `FARMOS_FRAME=0` | 64 | **off** vs **spatial** | same as A1 | same ROIs | Center still ~0 either way. Denoise does not create silver; may only change firefly isolation. |
| A3 | orbit.fm diagnostic **only**: `silverMat.setRoughness(0)` (do not ship) | 8 | off | CPU or RT | center 32×32 | Center **~100–110/255**, sharp env/floor reflections. Proves the missing **delta** path, not lighting. |
| A4 | Do **not** re-run v3/v4 artwork. | | | | | |

Stdout must print `cpu` / `vulkan` / `vulkan_rt` (already in both examples). Save PNGs; report ROI means, not whole-frame means (floor is bright and will swamp the ball).

---

## 7. Evidence gaps (narrow)

- Byte-for-byte Oct 14 `D:\Tommy\Farmos\out\artwork_silver_still.png` vs a current 256 spp RT still (should both be black at center if v1 was this composition).
- Whether Arc `rayQuery` any-hit self-shadows the sphere more than compute (A1 RT vs compute highlight **count**, not center).
- No 1080p CPU still was rendered in this cloud VM (`libstdc++` link on the image is incomplete); NEE reconstruction is analytic/grid, not a farmc PNG.

Those gaps do **not** reopen H1–H4. They only quantify RT vs compute firefly counts.

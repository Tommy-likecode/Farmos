#pragma once
#include "farm_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* M12 native Vulkan upscale glue (XeSS primary, FSR2 optional).
   Vendor SDK DLLs are loaded at runtime and do not count toward PE size. */

int farm_upscale_mode_available(int64_t mode);
int farm_upscale_probe_xess(void);
int farm_upscale_probe_fsr(void);

/* XeSS Vk requirements so the path-tracer instance/device can be borrowed.
   isPathUpscaleAvailable does not map libxess; these queries LoadLibrary when
   the process already wants XeSS so the official SDK extras land on the RT
   device (hardcoded extras broke borrow on Arc). Returns 0 unless wanted. */
int farm_upscale_xess_required_instance(unsigned* count, const char* const** names,
                                       unsigned* min_api);
int farm_upscale_xess_required_device(void* inst, void* phys, unsigned* count,
                                     const char* const** names, void** features);

/* Map libxess; then CreateContext + xessVKInit + persistent images while the
   path GPU is in flight (after vkQueueSubmit, before wait). */
void farm_upscale_prefetch_xess(void);
void farm_upscale_prepare_xess(farm_Renderer* r);

/* After denoise: try XeSS/FSR when mode != OFF and a GPU backend ran.
   CPU / missing SDK / FORCE_FAIL: leave render-res unless STRICT → trap 122. */
void farm_renderer_apply_path_upscale(farm_Renderer* r, farm_Scene* scene,
                                     farm_PerspectiveCamera* camera);

#ifdef __cplusplus
}
#endif

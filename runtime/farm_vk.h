#pragma once
#include "farm_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if this process has probed a usable Vulkan 1.1+ compute device. Sticky. */
int farm_vk_probe(void);

/* Run M4 integrator on GPU. Returns 1 on success (accum + framebuffer updated). */
int farm_vk_render_path(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera);

/* Borrow this backend's VkInstance/device/queue for XeSS when already live. */
int farm_vk_export_handles(void** inst, void** phys, void** dev, void** queue, unsigned* qfam);

#ifdef __cplusplus
}
#endif

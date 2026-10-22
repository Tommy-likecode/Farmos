#pragma once
#include "farm_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if this process has probed a device with VK_KHR_acceleration_structure +
   VK_KHR_ray_query (and a compute queue that can build AS). Sticky. */
int farm_vk_rt_probe(void);

/* M9 HW RT: compute dispatch + rayQuery against a Vulkan TLAS.
   Returns 1 on success (accum + framebuffer updated). */
int farm_vk_rt_render_path(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera);

/* Borrow this backend's VkInstance/device/queue for XeSS when already live. */
int farm_vk_rt_export_handles(void** inst, void** phys, void** dev, void** queue, unsigned* qfam);

#ifdef __cplusplus
}
#endif

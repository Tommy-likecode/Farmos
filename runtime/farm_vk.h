#pragma once
#include "farm_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if this process has probed a usable Vulkan 1.1+ compute device. Sticky. */
int farm_vk_probe(void);

/* Run M4 integrator on GPU. Returns 1 on success (accum + framebuffer updated). */
int farm_vk_render_path(farm_Renderer* r, farm_Scene* scene, farm_PerspectiveCamera* camera);

#ifdef __cplusplus
}
#endif

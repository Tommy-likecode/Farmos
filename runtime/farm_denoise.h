#pragma once
#include "farm_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Farmos-owned spatial denoise (A-trous / cross-bilateral class).
   Applied to HDR means; framebuffer is updated; accum stays noisy. */
void farm_renderer_apply_path_denoise(farm_Renderer* self);

#ifdef __cplusplus
}
#endif

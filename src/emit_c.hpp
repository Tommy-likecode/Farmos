#pragma once
#include "sema.hpp"
namespace farm {
std::string emit_c(Program& prog);
// True when a user module actually uses a farmos:scene type (not merely imports a name).
bool program_uses_scene(Program& prog);
// True when a reachable user function contains a `parallel` statement.
bool program_uses_parallel(Program& prog);
// True when a user module calls Renderer.renderPath or progressive preview (gates farm_ray.c).
bool program_uses_ray(Program& prog);
// True when denoise API / PATH_DENOISE_SPATIAL is reachable (gates farm_denoise.c).
bool program_uses_denoise(Program& prog);
// True when upscale API / PATH_UPSCALE_* is reachable (gates farm_upscale.c).
bool program_uses_upscale(Program& prog);
// True when a reachable user module opts into the Vulkan path backend (gates farm_vk.c).
bool program_uses_vulkan(Program& prog);
// True when HW RT (backend 2 / isVulkanRayTracingAvailable) is reachable (gates farm_vk_rt.c).
bool program_uses_vulkan_rt(Program& prog);
// True when a user module actually uses a farmos:physics type or BODY_* const.
bool program_uses_physics(Program& prog);
// True when a user module actually uses a farmos:math type (not merely imports a name).
bool program_uses_math(Program& prog);
// True when a user module calls farmos:math random01() (gates farm_math.c without scene).
bool program_uses_random01(Program& prog);
}

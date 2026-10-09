#pragma once
#include "sema.hpp"
namespace farm {
std::string emit_c(Program& prog);
// True when a user module actually uses a farmos:scene type (not merely imports a name).
bool program_uses_scene(Program& prog);
// True when a reachable user function contains a `parallel` statement.
bool program_uses_parallel(Program& prog);
// True when a user module calls Renderer.renderPath (gates farm_ray.c).
bool program_uses_ray(Program& prog);
// True when a user module actually uses a farmos:physics type or BODY_* const.
bool program_uses_physics(Program& prog);
}

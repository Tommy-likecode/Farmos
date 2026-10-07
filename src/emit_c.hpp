#pragma once
#include "sema.hpp"
namespace farm {
std::string emit_c(Program& prog);
// True when a user module actually uses a farmos:scene type (not merely imports a name).
bool program_uses_scene(Program& prog);
}

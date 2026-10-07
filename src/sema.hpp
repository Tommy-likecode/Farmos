#pragma once
#include "ast.hpp"

namespace farm {

struct Program {
  std::vector<Module> modules;
};

bool analyze_program(Program& prog);
void analyze_conflicts(Program& prog);

} // namespace farm

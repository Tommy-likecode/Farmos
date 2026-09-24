#pragma once
#include "ast.hpp"

namespace farm {

struct Program {
  std::vector<Module> modules; // [0] is main
  // resolved type decls keyed by name (global across linked modules, simple M1)
  std::unordered_map<std::string, StructDecl*> structs;
  std::unordered_map<std::string, ClassDecl*> classes;
  std::unordered_map<std::string, FunctionDecl*> functions;
  std::unordered_map<std::string, ConstDecl*> consts;
};

bool analyze_program(Program& prog);

} // namespace farm

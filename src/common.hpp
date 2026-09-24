#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <limits>

namespace fs = std::filesystem;

namespace farm {

struct SourceLoc {
  int line = 1;
  int col = 1;
};

struct Diagnostic {
  std::string path;
  SourceLoc loc;
  std::string code;
  std::string message;
};

inline std::vector<Diagnostic>& diags() {
  static std::vector<Diagnostic> d;
  return d;
}

inline bool& diag_cascade_stop() {
  static bool stop = false;
  return stop;
}

inline void error_at(const std::string& path, SourceLoc loc, const std::string& code, const std::string& msg) {
  if (diag_cascade_stop()) return;
  diags().push_back(Diagnostic{path, loc, code, msg});
  if (code == "E0505") diag_cascade_stop() = true;
}

inline void emit_diagnostics() {
  for (auto& d : diags()) {
    std::cerr << d.path << ":" << d.loc.line << ":" << d.loc.col
              << ": error[" << d.code << "]: " << d.message << "\n";
  }
}

inline bool has_errors() { return !diags().empty(); }
inline void clear_diags() { diags().clear(); diag_cascade_stop() = false; }

} // namespace farm

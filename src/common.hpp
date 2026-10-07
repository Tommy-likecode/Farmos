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
  bool is_warning = false;
  bool has_note = false;
  std::string note_path;
  SourceLoc note_loc;
  std::string note_message;
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
  Diagnostic d;
  d.path = path; d.loc = loc; d.code = code; d.message = msg; d.is_warning = false;
  diags().push_back(std::move(d));
  // E0505 (undefined name) and E0001 (ill-formed UTF-8): report once, suppress the cascade.
  if (code == "E0505" || code == "E0001") diag_cascade_stop() = true;
}

inline void warning_at(const std::string& path, SourceLoc loc, const std::string& code, const std::string& msg) {
  Diagnostic d;
  d.path = path; d.loc = loc; d.code = code; d.message = msg; d.is_warning = true;
  diags().push_back(std::move(d));
}

inline void attach_note(Diagnostic& d, const std::string& path, SourceLoc loc, const std::string& msg) {
  d.has_note = true;
  d.note_path = path;
  d.note_loc = loc;
  d.note_message = msg;
}

inline int diag_code_num(const std::string& c) {
  if (c.size() < 2) return 0;
  try { return std::stoi(c.substr(1)); } catch (...) { return 0; }
}

inline void sort_diagnostics() {
  std::sort(diags().begin(), diags().end(), [](const Diagnostic& a, const Diagnostic& b) {
    if (a.path != b.path) return a.path < b.path;
    if (a.loc.line != b.loc.line) return a.loc.line < b.loc.line;
    if (a.loc.col != b.loc.col) return a.loc.col < b.loc.col;
    // E before W, then code number.
    char ak = a.code.empty() ? 'E' : a.code[0];
    char bk = b.code.empty() ? 'E' : b.code[0];
    if (ak != bk) return ak < bk;
    return diag_code_num(a.code) < diag_code_num(b.code);
  });
}

inline void emit_diagnostics(bool werror = false) {
  sort_diagnostics();
  int errors_printed = 0;
  for (auto& d : diags()) {
    bool as_error = !d.is_warning || werror;
    if (as_error) {
      if (errors_printed >= 20 && !d.is_warning) continue;
      if (!d.is_warning) errors_printed++;
    }
    const char* sev = as_error ? "error" : "warning";
    std::cerr << d.path << ":" << d.loc.line << ":" << d.loc.col
              << ": " << sev << "[" << d.code << "]: " << d.message << "\n";
    if (d.has_note) {
      std::cerr << d.note_path << ":" << d.note_loc.line << ":" << d.note_loc.col
                << ": note: " << d.note_message << "\n";
    }
  }
}

inline bool has_errors() {
  for (auto& d : diags()) if (!d.is_warning) return true;
  return false;
}
inline bool has_warnings() {
  for (auto& d : diags()) if (d.is_warning) return true;
  return false;
}
inline void clear_diags() { diags().clear(); diag_cascade_stop() = false; }

} // namespace farm

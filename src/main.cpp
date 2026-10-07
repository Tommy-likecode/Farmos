#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "emit_c.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <atomic>
#include <chrono>
#include <random>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace farm {

// Driver I/O / configuration failures (section 7.2 exit 3). Distinct from ICE (exit 4).
struct DriverError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

#ifdef _WIN32
// UTF-8 <-> UTF-16. Never use the ANSI/ACP code page for farmc path I/O: TEMP may contain
// characters (U+00DF, emoji, ZWJ, ...) that have no ACP mapping and would throw
// "No mapping for the Unicode character exists in the target multi-byte code page".
static std::wstring utf8_to_wide(const std::string& u8) {
  if (u8.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8.data(), (int)u8.size(), nullptr, 0);
  if (n <= 0) throw DriverError("invalid UTF-8 in path");
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, u8.data(), (int)u8.size(), &w[0], n);
  return w;
}
static std::string wide_to_utf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  if (n <= 0) return {};
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}
// Build a native fs::path from UTF-8 without ACP (wstring ctor on Windows).
static fs::path path_from_utf8(const std::string& u8) { return fs::path(utf8_to_wide(u8)); }
// UTF-8 display / command-line form of a path (never path::string(), which is ACP on MSVC).
static std::string path_to_utf8(const fs::path& p) { return wide_to_utf8(p.wstring()); }

// TEMP/TMP via GetEnvironmentVariableW, else GetTempPathW. Never fs::temp_directory_path()
// (MSVC converts the wide TEMP through ACP and throws on ß / emoji / ZWJ).
static fs::path win_temp_directory() {
  wchar_t buf[32768];
  for (const wchar_t* name : {L"TEMP", L"TMP"}) {
    DWORD n = GetEnvironmentVariableW(name, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n > 0 && n < sizeof(buf) / sizeof(buf[0])) {
      while (n > 0 && (buf[n - 1] == L'\\' || buf[n - 1] == L'/')) buf[--n] = 0;
      fs::path p(buf);
      std::error_code ec;
      if (fs::is_directory(p, ec)) return p;
    }
  }
  DWORD n = GetTempPathW((DWORD)(sizeof(buf) / sizeof(buf[0])), buf);
  if (n == 0 || n >= sizeof(buf) / sizeof(buf[0]))
    throw DriverError("cannot resolve temporary directory (GetTempPathW failed)");
  while (n > 0 && (buf[n - 1] == L'\\' || buf[n - 1] == L'/')) buf[--n] = 0;
  return fs::path(buf);
}
#else
static fs::path path_from_utf8(const std::string& u8) { return fs::path(u8); }
static std::string path_to_utf8(const fs::path& p) { return p.string(); }
#endif

static std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss; ss << in.rdbuf();
  std::string s = ss.str();
  // strip UTF-8 BOM
  if (s.size() >= 3 && (unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF)
    s.erase(0, 3);
  return s;
}

static bool is_relative_fm(const std::string& path) {
  if (path.size() < 4 || path.substr(path.size()-3) != ".fm") return false;
  if (!path.empty() && (path[0]=='/' || (path.size()>1 && path[1]==':'))) return false;
  // bare package name without ./ or ../
  if (path.find('/')==std::string::npos && path.find('\\')==std::string::npos) return false;
  return true;
}

static fs::path resolve_import(const fs::path& from_file, const std::string& rel) {
  return fs::weakly_canonical(from_file.parent_path() / rel);
}

// Display path for diagnostics: prefer cwd-relative with forward slashes (spec section 8.1 / tests README).
static std::string to_diag_path(const fs::path& p) {
  auto as_generic_utf8 = [](const fs::path& x) {
    std::string s = path_to_utf8(x);
    for (char& c : s) if (c == '\\') c = '/';
    return s;
  };
  std::error_code ec;
  fs::path abs = fs::weakly_canonical(p, ec);
  if (ec) abs = fs::absolute(p, ec);
  if (ec) return as_generic_utf8(p);
  fs::path rel = fs::relative(abs, fs::current_path(), ec);
  if (!ec && !rel.empty()) {
    std::string s = as_generic_utf8(rel);
    if (s != ".." && s.rfind("../", 0) != 0) return s;
  }
  return as_generic_utf8(abs);
}


// M1-core section 2.1: full UTF-8 well-formedness check over the raw bytes (Unicode Table 3-7).
// Returns false and sets `bad` to the byte offset of the first byte of the first ill-formed sequence:
// stray continuation bytes, C0/C1/F5..FF, overlongs (E0 80..9F, F0 80..8F), surrogates (ED A0..BF),
// > U+10FFFF (F4 90..), and truncated sequences (including at EOF).
static bool validate_utf8(const std::string& s, size_t& bad) {
  const size_t n = s.size();
  size_t i = 0;
  while (i < n) {
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80) { ++i; continue; }
    size_t len; unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) len = 2;
    else if (c == 0xE0) { len = 3; lo = 0xA0; }
    else if ((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) len = 3;
    else if (c == 0xED) { len = 3; hi = 0x9F; }
    else if (c == 0xF0) { len = 4; lo = 0x90; }
    else if (c >= 0xF1 && c <= 0xF3) len = 4;
    else if (c == 0xF4) { len = 4; hi = 0x8F; }
    else { bad = i; return false; }                  // 80..BF stray, C0, C1, F5..FF
    if (i + 1 >= n) { bad = i; return false; }       // truncated at EOF
    unsigned char c1 = (unsigned char)s[i + 1];
    if (c1 < lo || c1 > hi) { bad = i; return false; }
    for (size_t k = 2; k < len; ++k) {
      if (i + k >= n || (((unsigned char)s[i + k]) & 0xC0) != 0x80) { bad = i; return false; }
    }
    i += len;
  }
  return true;
}

// Line/column of byte offset `off` using the same rules as the lexer and all other diagnostics:
// CR LF / lone CR / LF are one line break; column = 1 + number of Unicode scalars before `off`
// on that line (bytes before `off` are known well-formed).
static SourceLoc byte_loc(const std::string& s, size_t off) {
  SourceLoc l{1, 1};
  for (size_t i = 0; i < off && i < s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (c == '\n') { l.line++; l.col = 1; }
    else if (c == '\r') { if (i + 1 < off && s[i + 1] == '\n') ++i; l.line++; l.col = 1; }
    else if ((c & 0xC0) != 0x80) l.col++;
  }
  return l;
}

// Check if import path is a farmos: builtin module
static bool is_farmos_builtin(const std::string& path) {
  return path.rfind("farmos:", 0) == 0;
}

// Create synthetic farmos:scene module
static Module create_farmos_scene_module(const std::vector<Module>& existing_modules) {
  Module m;
  m.path = "farmos:scene";
  m.diag_path = "farmos:scene";
  m.is_main = false;
  
  // No imports needed - will wire math types manually in bind_imports
  
  // For M3, we define classes (reference types)
  // Object3D, Scene, PerspectiveCamera, Mesh, BoxGeometry, SphereGeometry, PlaneGeometry,
  // MeshBasicMaterial, MeshStandardMaterial, AmbientLight, DirectionalLight, PointLight, Renderer
  
  // Object3D class
  {
    ClassDecl cd;
    cd.name = "Object3D";
    cd.c_sym = "farm_Object3D";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    // Object3D is the base - no base_class
    
    // Fields - use display names for type resolution
    cd.fields.push_back(FieldDecl{"position", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"rotation", Type::ty_struct("Euler"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"quaternion", Type::ty_struct("Quaternion"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"scale", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"matrix", Type::ty_struct("Matrix4"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"matrixWorld", Type::ty_struct("Matrix4"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"matrixAutoUpdate", Type::ty_bool(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"visible", Type::ty_bool(), SourceLoc{1, 1}});
    
    // Constructor - exactly one, no args
    {
      MethodDecl md;
      md.name = cd.name;
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // add method
    {
      MethodDecl md;
      md.name = "add";
      md.params.push_back(Param{"child", Type::ty_class("Object3D"), SourceLoc{1,1}});
      md.ret = Type::ty_class("Object3D");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      // Return self for chaining - emit_c will handle
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // remove method
    {
      MethodDecl md;
      md.name = "remove";
      md.params.push_back(Param{"child", Type::ty_class("Object3D"), SourceLoc{1,1}});
      md.ret = Type::ty_class("Object3D");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // addAt method
    {
      MethodDecl md;
      md.name = "addAt";
      md.params.push_back(Param{"child", Type::ty_class("Object3D"), SourceLoc{1,1}});
      md.params.push_back(Param{"index", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_class("Object3D");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // childCount method
    {
      MethodDecl md;
      md.name = "childCount";
      md.ret = Type::ty_int();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // getChild method
    {
      MethodDecl md;
      md.name = "getChild";
      md.params.push_back(Param{"index", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_class("Object3D");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // lookAt method (x, y, z)
    {
      MethodDecl md;
      md.name = "lookAt";
      md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // lookAt method (Vector3) - overload
    {
      MethodDecl md;
      md.name = "lookAt";
      md.params.push_back(Param{"target", Type::ty_struct("Vector3"), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // updateMatrix method
    {
      MethodDecl md;
      md.name = "updateMatrix";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    // updateMatrixWorld method
    {
      MethodDecl md;
      md.name = "updateMatrixWorld";
      md.params.push_back(Param{"force", Type::ty_bool(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // Scene class
  {
    ClassDecl cd;
    cd.name = "Scene";
    cd.c_sym = "farm_Scene";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Scene is-a Object3D
    cd.fields.push_back(FieldDecl{"hasBackground", Type::ty_bool(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"background", Type::ty_struct("Color"), SourceLoc{1, 1}});
    
    // Constructor - exactly one, no args
    {
      MethodDecl md;
      md.name = cd.name;
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    {
      MethodDecl md;
      md.name = "setBackground";
      md.params.push_back(Param{"color", Type::ty_struct("Color"), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "add";
      md.params.push_back(Param{"child", Type::ty_class("Object3D"), SourceLoc{1,1}});
      md.ret = Type::ty_class("Object3D");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // PerspectiveCamera class
  {
    ClassDecl cd;
    cd.name = "PerspectiveCamera";
    cd.c_sym = "farm_PerspectiveCamera";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Camera is-a Object3D
    cd.fields.push_back(FieldDecl{"position", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"rotation", Type::ty_struct("Euler"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"quaternion", Type::ty_struct("Quaternion"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"scale", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"fov", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"aspect", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"near", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"far", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"position", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    
    // Constructor - exactly one (fov, aspect, near, far)
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"fov", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"aspect", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"near", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"far", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    {
      MethodDecl md;
      md.name = "lookAt";
      md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  
  // BoxGeometry
  {
    ClassDecl cd;
    cd.name = "BoxGeometry";
    cd.c_sym = "farm_BoxGeometry";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"width", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"height", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"depth", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    m.classes.push_back(std::move(cd));
  }
  
  // SphereGeometry
  {
    ClassDecl cd;
    cd.name = "SphereGeometry";
    cd.c_sym = "farm_SphereGeometry";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"radius", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"widthSegments", Type::ty_int(), SourceLoc{1,1}});
      md.params.push_back(Param{"heightSegments", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    m.classes.push_back(std::move(cd));
  }
  
  // PlaneGeometry
  {
    ClassDecl cd;
    cd.name = "PlaneGeometry";
    cd.c_sym = "farm_PlaneGeometry";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"width", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"height", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    m.classes.push_back(std::move(cd));
  }
  
  // MeshBasicMaterial
  {
    ClassDecl cd;
    cd.name = "MeshBasicMaterial";
    cd.c_sym = "farm_MeshBasicMaterial";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.fields.push_back(FieldDecl{"color", Type::ty_struct("Color"), SourceLoc{1, 1}});
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"color", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    m.classes.push_back(std::move(cd));
  }
  
  // MeshStandardMaterial
  {
    ClassDecl cd;
    cd.name = "MeshStandardMaterial";
    cd.c_sym = "farm_MeshStandardMaterial";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.fields.push_back(FieldDecl{"color", Type::ty_struct("Color"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"roughness", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"metalness", Type::ty_float(), SourceLoc{1, 1}});
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"color", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "setRoughness";
      md.params.push_back(Param{"roughness", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "setMetalness";
      md.params.push_back(Param{"metalness", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    m.classes.push_back(std::move(cd));
  }
  
  // Mesh
  {
    ClassDecl cd;
    cd.name = "Mesh";
    cd.c_sym = "farm_Mesh";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Mesh is-a Object3D
    cd.fields.push_back(FieldDecl{"geometry", Type::ty_class("BoxGeometry"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"material", Type::ty_class("MeshBasicMaterial"), SourceLoc{1, 1}});
    
    // Constructor - exactly one (geometry: BoxGeometry, material: MeshBasicMaterial)
    // Type checker will allow subtype/compatible assignments
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"geometry", Type::ty_class("BoxGeometry"), SourceLoc{1,1}});
      md.params.push_back(Param{"material", Type::ty_class("MeshBasicMaterial"), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // AmbientLight - single constructor (1 arg, most common)
  {
    ClassDecl cd;
    cd.name = "AmbientLight";
    cd.c_sym = "farm_AmbientLight";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Light is-a Object3D
    cd.fields.push_back(FieldDecl{"color", Type::ty_struct("Color"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"intensity", Type::ty_float(), SourceLoc{1, 1}});
    
    // Constructor - exactly one (color_hex)
    // Emit_c will handle 2-arg variant via runtime dispatch
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"color", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // DirectionalLight - single constructor (1 arg)
  {
    ClassDecl cd;
    cd.name = "DirectionalLight";
    cd.c_sym = "farm_DirectionalLight";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Light is-a Object3D
    cd.fields.push_back(FieldDecl{"color", Type::ty_struct("Color"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"intensity", Type::ty_float(), SourceLoc{1, 1}});
    
    // Constructor - exactly one (color_hex)
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"color", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // PointLight - single constructor (1 arg)
  {
    ClassDecl cd;
    cd.name = "PointLight";
    cd.c_sym = "farm_PointLight";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    cd.base_class = "Object3D";  // M3: Light is-a Object3D
    cd.fields.push_back(FieldDecl{"color", Type::ty_struct("Color"), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"intensity", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"distance", Type::ty_float(), SourceLoc{1, 1}});
    cd.fields.push_back(FieldDecl{"decay", Type::ty_float(), SourceLoc{1, 1}});
    
    // Constructor - exactly one (color_hex)
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"color", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  // Renderer
  {
    ClassDecl cd;
    cd.name = "Renderer";
    cd.c_sym = "farm_Renderer";
    cd.exported = true;
    cd.loc = SourceLoc{1, 1};
    
    // Constructor - exactly one (width, height)
    {
      MethodDecl md;
      md.name = cd.name;
      md.params.push_back(Param{"width", Type::ty_int(), SourceLoc{1,1}});
      md.params.push_back(Param{"height", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      md.is_ctor = true;
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    {
      MethodDecl md;
      md.name = "setSize";
      md.params.push_back(Param{"width", Type::ty_int(), SourceLoc{1,1}});
      md.params.push_back(Param{"height", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "render";
      md.params.push_back(Param{"scene", Type::ty_class("Scene"), SourceLoc{1,1}});
      md.params.push_back(Param{"camera", Type::ty_class("PerspectiveCamera"), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "savePNG";
      md.params.push_back(Param{"path", Type::ty_string(), SourceLoc{1,1}});
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    {
      MethodDecl md;
      md.name = "dispose";
      md.ret = Type::ty_void();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      cd.methods.push_back(std::move(md));
    }
    
    m.classes.push_back(std::move(cd));
  }
  
  return m;
}

// Create synthetic farmos:math module
static Module create_farmos_math_module() {
  Module m;
  m.path = "farmos:math";
  m.diag_path = "farmos:math";
  m.is_main = false;
  
  // Vector3 struct
  StructDecl v3;
  v3.name = "Vector3";
  v3.exported = true;
  v3.loc = SourceLoc{1, 1};
  v3.fields.push_back(FieldDecl{"x", Type::ty_float(), SourceLoc{1, 1}});
  v3.fields.push_back(FieldDecl{"y", Type::ty_float(), SourceLoc{1, 1}});
  v3.fields.push_back(FieldDecl{"z", Type::ty_float(), SourceLoc{1, 1}});
  
  // M2: Add methods to Vector3
  // Note: Can't use final c_sym yet since it's assigned later. Use display name and fix in sema.
  // add(v: Vector3): Vector3 - mutates this, returns this
  {
    MethodDecl md;
    md.name = "add";
    // Use raw type with display name; sema will resolve to c_sym
    auto v_type = Type::ty_struct("Vector3");
    md.params.push_back(Param{"v", v_type, SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    block->loc = SourceLoc{1, 1};
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // multiplyScalar(s: float): Vector3
  {
    MethodDecl md;
    md.name = "multiplyScalar";
    md.params.push_back(Param{"s", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    block->loc = SourceLoc{1, 1};
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // dot(v: Vector3): float
  {
    MethodDecl md;
    md.name = "dot";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // cross(v: Vector3): Vector3
  {
    MethodDecl md;
    md.name = "cross";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // length(): float
  {
    MethodDecl md;
    md.name = "length";
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // lengthSq(): float
  {
    MethodDecl md;
    md.name = "lengthSq";
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // normalize(): Vector3
  {
    MethodDecl md;
    md.name = "normalize";
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // clone(): Vector3
  {
    MethodDecl md;
    md.name = "clone";
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // lerp(v: Vector3, alpha: float): Vector3
  {
    MethodDecl md;
    md.name = "lerp";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.params.push_back(Param{"alpha", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // equals(v: Vector3): bool
  {
    MethodDecl md;
    md.name = "equals";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_bool();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // set(x, y, z: float): Vector3
  {
    MethodDecl md;
    md.name = "set";
    md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
    md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
    md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // distanceTo(v: Vector3): float
  {
    MethodDecl md;
    md.name = "distanceTo";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // distanceToSquared(v: Vector3): float
  {
    MethodDecl md;
    md.name = "distanceToSquared";
    md.params.push_back(Param{"v", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // applyMatrix4(m: Matrix4): Vector3
  {
    MethodDecl md;
    md.name = "applyMatrix4";
    md.params.push_back(Param{"m", Type::ty_struct("Matrix4"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // applyQuaternion(q: Quaternion): Vector3
  {
    MethodDecl md;
    md.name = "applyQuaternion";
    md.params.push_back(Param{"q", Type::ty_struct("Quaternion"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // crossVectors(a: Vector3, b: Vector3): Vector3
  {
    MethodDecl md;
    md.name = "crossVectors";
    md.params.push_back(Param{"a", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.params.push_back(Param{"b", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator+ (via __farm_op_add)
  {
    MethodDecl md;
    md.name = "__farm_op_add";
    md.params.push_back(Param{"other", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator- (via __farm_op_sub)
  {
    MethodDecl md;
    md.name = "__farm_op_sub";
    md.params.push_back(Param{"other", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator* (scalar) (via __farm_op_mul)
  {
    MethodDecl md;
    md.name = "__farm_op_mul";
    md.params.push_back(Param{"scalar", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator/ (scalar) (via __farm_op_div)
  {
    MethodDecl md;
    md.name = "__farm_op_div";
    md.params.push_back(Param{"scalar", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator== (via __farm_op_eq)
  {
    MethodDecl md;
    md.name = "__farm_op_eq";
    md.params.push_back(Param{"other", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_bool();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // operator!= (via __farm_op_neq)
  {
    MethodDecl md;
    md.name = "__farm_op_neq";
    md.params.push_back(Param{"other", Type::ty_struct("Vector3"), SourceLoc{1,1}});
    md.ret = Type::ty_bool();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // unary operator- (via __farm_op_neg)
  {
    MethodDecl md;
    md.name = "__farm_op_neg";
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  // reverse operator* (scalar * vector) (via __farm_op_rmul)
  {
    MethodDecl md;
    md.name = "__farm_op_rmul";
    md.params.push_back(Param{"scalar", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector3");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v3.methods.push_back(std::move(md));
  }
  
  m.structs.push_back(std::move(v3));
  
  // Matrix4
  {
    StructDecl m4;
    m4.name = "Matrix4";
    m4.exported = true;
    m4.loc = SourceLoc{1, 1};
    // elements: float[16]
    auto m4_elem_type = Type::ty_float();
    auto m4_arr_type = std::make_shared<Type>();
    m4_arr_type->kind = TypeKind::FixedArray;
    m4_arr_type->elem = m4_elem_type;
    m4_arr_type->fixed_len = 16;
    m4.fields.push_back(FieldDecl{"elements", m4_arr_type, SourceLoc{1, 1}});
    
    // Default constructor creates identity matrix
    // No explicit constructor needed - will be handled in emit_c
    
    // determinant(): float
    {
      MethodDecl md;
      md.name = "determinant";
      md.ret = Type::ty_float();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // makeTranslation(x, y, z: float): Matrix4
    {
      MethodDecl md;
      md.name = "makeTranslation";
      md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // makeScale(x, y, z: float): Matrix4
    {
      MethodDecl md;
      md.name = "makeScale";
      md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // multiply(m: Matrix4): Matrix4
    {
      MethodDecl md;
      md.name = "multiply";
      md.params.push_back(Param{"m", Type::ty_struct("Matrix4"), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // invert(): Matrix4
    {
      MethodDecl md;
      md.name = "invert";
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // transpose(): Matrix4
    {
      MethodDecl md;
      md.name = "transpose";
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // set(n11..n44: 16 floats): Matrix4
    {
      MethodDecl md;
      md.name = "set";
      for (int i = 0; i < 16; i++) {
        md.params.push_back(Param{"n" + std::to_string(i), Type::ty_float(), SourceLoc{1,1}});
      }
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // makeRotationY(theta: float): Matrix4
    {
      MethodDecl md;
      md.name = "makeRotationY";
      md.params.push_back(Param{"theta", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    // makeRotationX(theta: float): Matrix4
    {
      MethodDecl md;
      md.name = "makeRotationX";
      md.params.push_back(Param{"theta", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix4");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m4.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(m4));
  }
  
  // Matrix3
  {
    StructDecl m3;
    m3.name = "Matrix3";
    m3.exported = true;
    m3.loc = SourceLoc{1, 1};
    // elements: float[9]
    auto m3_elem_type = Type::ty_float();
    auto m3_arr_type = std::make_shared<Type>();
    m3_arr_type->kind = TypeKind::FixedArray;
    m3_arr_type->elem = m3_elem_type;
    m3_arr_type->fixed_len = 9;
    m3.fields.push_back(FieldDecl{"elements", m3_arr_type, SourceLoc{1, 1}});
    
    // determinant(): float
    {
      MethodDecl md;
      md.name = "determinant";
      md.ret = Type::ty_float();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m3.methods.push_back(std::move(md));
    }
    
    // makeScale(sx: float, sy: float): Matrix3
    {
      MethodDecl md;
      md.name = "makeScale";
      md.params.push_back(Param{"sx", Type::ty_float(), SourceLoc{1,1}});
      md.params.push_back(Param{"sy", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Matrix3");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      m3.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(m3));
  }
  
  // Quaternion
  {
    StructDecl q;
    q.name = "Quaternion";
    q.exported = true;
    q.loc = SourceLoc{1, 1};
    q.fields.push_back(FieldDecl{"x", Type::ty_float(), SourceLoc{1, 1}});
    q.fields.push_back(FieldDecl{"y", Type::ty_float(), SourceLoc{1, 1}});
    q.fields.push_back(FieldDecl{"z", Type::ty_float(), SourceLoc{1, 1}});
    q.fields.push_back(FieldDecl{"w", Type::ty_float(), SourceLoc{1, 1}});
    
    // multiply(q: Quaternion): Quaternion
    {
      MethodDecl md;
      md.name = "multiply";
      md.params.push_back(Param{"q", Type::ty_struct("Quaternion"), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Quaternion");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      q.methods.push_back(std::move(md));
    }
    
    // equals(q: Quaternion): bool
    {
      MethodDecl md;
      md.name = "equals";
      md.params.push_back(Param{"q", Type::ty_struct("Quaternion"), SourceLoc{1,1}});
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      q.methods.push_back(std::move(md));
    }
    
    // setFromAxisAngle(axis: Vector3, angle: float): Quaternion
    {
      MethodDecl md;
      md.name = "setFromAxisAngle";
      md.params.push_back(Param{"axis", Type::ty_struct("Vector3"), SourceLoc{1,1}});
      md.params.push_back(Param{"angle", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Quaternion");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      q.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(q));
  }
  
  // Vector2 struct
  StructDecl v2;
  v2.name = "Vector2";
  v2.exported = true;
  v2.loc = SourceLoc{1, 1};
  v2.fields.push_back(FieldDecl{"x", Type::ty_float(), SourceLoc{1, 1}});
  v2.fields.push_back(FieldDecl{"y", Type::ty_float(), SourceLoc{1, 1}});
  
  // length(): float
  {
    MethodDecl md;
    md.name = "length";
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  // normalize(): Vector2
  {
    MethodDecl md;
    md.name = "normalize";
    md.ret = Type::ty_struct("Vector2");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  // add(v: Vector2): Vector2
  {
    MethodDecl md;
    md.name = "add";
    md.params.push_back(Param{"v", Type::ty_struct("Vector2"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector2");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  // multiplyScalar(s: float): Vector2
  {
    MethodDecl md;
    md.name = "multiplyScalar";
    md.params.push_back(Param{"s", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector2");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  // applyMatrix3(m: Matrix3): Vector2
  {
    MethodDecl md;
    md.name = "applyMatrix3";
    md.params.push_back(Param{"m", Type::ty_struct("Matrix3"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector2");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  // operator+ (via __farm_op_add)
  {
    MethodDecl md;
    md.name = "__farm_op_add";
    md.params.push_back(Param{"other", Type::ty_struct("Vector2"), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector2");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v2.methods.push_back(std::move(md));
  }
  
  m.structs.push_back(std::move(v2));
  
  // Vector4 struct
  StructDecl v4;
  v4.name = "Vector4";
  v4.exported = true;
  v4.loc = SourceLoc{1, 1};
  v4.fields.push_back(FieldDecl{"x", Type::ty_float(), SourceLoc{1, 1}});
  v4.fields.push_back(FieldDecl{"y", Type::ty_float(), SourceLoc{1, 1}});
  v4.fields.push_back(FieldDecl{"z", Type::ty_float(), SourceLoc{1, 1}});
  v4.fields.push_back(FieldDecl{"w", Type::ty_float(), SourceLoc{1, 1}});
  
  // dot(v: Vector4): float
  {
    MethodDecl md;
    md.name = "dot";
    md.params.push_back(Param{"v", Type::ty_struct("Vector4"), SourceLoc{1,1}});
    md.ret = Type::ty_float();
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v4.methods.push_back(std::move(md));
  }
  
  // multiplyScalar(s: float): Vector4
  {
    MethodDecl md;
    md.name = "multiplyScalar";
    md.params.push_back(Param{"s", Type::ty_float(), SourceLoc{1,1}});
    md.ret = Type::ty_struct("Vector4");
    md.loc = SourceLoc{1, 1};
    auto block = std::make_unique<Stmt>();
    block->kind = StmtKind::Block;
    md.body = std::move(block);
    v4.methods.push_back(std::move(md));
  }
  
  m.structs.push_back(std::move(v4));
  
  // Color
  {
    StructDecl c;
    c.name = "Color";
    c.exported = true;
    c.loc = SourceLoc{1, 1};
    c.fields.push_back(FieldDecl{"r", Type::ty_float(), SourceLoc{1, 1}});
    c.fields.push_back(FieldDecl{"g", Type::ty_float(), SourceLoc{1, 1}});
    c.fields.push_back(FieldDecl{"b", Type::ty_float(), SourceLoc{1, 1}});
    
    // setHex(hex: int): Color
    {
      MethodDecl md;
      md.name = "setHex";
      md.params.push_back(Param{"hex", Type::ty_int(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Color");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      c.methods.push_back(std::move(md));
    }
    
    // multiplyScalar(s: float): Color
    {
      MethodDecl md;
      md.name = "multiplyScalar";
      md.params.push_back(Param{"s", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Color");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      c.methods.push_back(std::move(md));
    }
    
    // getHex(): int
    {
      MethodDecl md;
      md.name = "getHex";
      md.ret = Type::ty_int();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      c.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(c));
  }
  
  // Euler
  {
    StructDecl e;
    e.name = "Euler";
    e.exported = true;
    e.loc = SourceLoc{1, 1};
    e.fields.push_back(FieldDecl{"x", Type::ty_float(), SourceLoc{1, 1}});
    e.fields.push_back(FieldDecl{"y", Type::ty_float(), SourceLoc{1, 1}});
    e.fields.push_back(FieldDecl{"z", Type::ty_float(), SourceLoc{1, 1}});
    e.fields.push_back(FieldDecl{"order", Type::ty_string(), SourceLoc{1, 1}});
    m.structs.push_back(std::move(e));
  }
  
  // Ray
  {
    StructDecl r;
    r.name = "Ray";
    r.exported = true;
    r.loc = SourceLoc{1, 1};
    r.fields.push_back(FieldDecl{"origin", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    r.fields.push_back(FieldDecl{"direction", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    
    // at(t: float): Vector3
    {
      MethodDecl md;
      md.name = "at";
      md.params.push_back(Param{"t", Type::ty_float(), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Vector3");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      r.methods.push_back(std::move(md));
    }
    
    // intersectSphere(s: Sphere): RayHit
    {
      MethodDecl md;
      md.name = "intersectSphere";
      md.params.push_back(Param{"s", Type::ty_struct("Sphere"), SourceLoc{1,1}});
      md.ret = Type::ty_struct("RayHit");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      r.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(r));
  }
  
  // Sphere
  {
    StructDecl s;
    s.name = "Sphere";
    s.exported = true;
    s.loc = SourceLoc{1, 1};
    s.fields.push_back(FieldDecl{"center", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    s.fields.push_back(FieldDecl{"radius", Type::ty_float(), SourceLoc{1, 1}});
    
    // containsPoint(p: Vector3): bool
    {
      MethodDecl md;
      md.name = "containsPoint";
      md.params.push_back(Param{"p", Type::ty_struct("Vector3"), SourceLoc{1,1}});
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      s.methods.push_back(std::move(md));
    }
    
    // intersectsSphere(s: Sphere): bool
    {
      MethodDecl md;
      md.name = "intersectsSphere";
      md.params.push_back(Param{"s", Type::ty_struct("Sphere"), SourceLoc{1,1}});
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      s.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(s));
  }
  
  // Box3
  {
    StructDecl b;
    b.name = "Box3";
    b.exported = true;
    b.loc = SourceLoc{1, 1};
    b.fields.push_back(FieldDecl{"min", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    b.fields.push_back(FieldDecl{"max", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    
    // isEmpty(): bool
    {
      MethodDecl md;
      md.name = "isEmpty";
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    // expandByPoint(p: Vector3): Box3
    {
      MethodDecl md;
      md.name = "expandByPoint";
      md.params.push_back(Param{"p", Type::ty_struct("Vector3"), SourceLoc{1,1}});
      md.ret = Type::ty_struct("Box3");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    // containsPoint(p: Vector3): bool
    {
      MethodDecl md;
      md.name = "containsPoint";
      md.params.push_back(Param{"p", Type::ty_struct("Vector3"), SourceLoc{1,1}});
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    // getCenter(): Vector3
    {
      MethodDecl md;
      md.name = "getCenter";
      md.ret = Type::ty_struct("Vector3");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    // getSize(): Vector3
    {
      MethodDecl md;
      md.name = "getSize";
      md.ret = Type::ty_struct("Vector3");
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    // intersectsBox(box: Box3): bool
    {
      MethodDecl md;
      md.name = "intersectsBox";
      md.params.push_back(Param{"box", Type::ty_struct("Box3"), SourceLoc{1,1}});
      md.ret = Type::ty_bool();
      md.loc = SourceLoc{1, 1};
      auto block = std::make_unique<Stmt>();
      block->kind = StmtKind::Block;
      md.body = std::move(block);
      b.methods.push_back(std::move(md));
    }
    
    m.structs.push_back(std::move(b));
  }
  
  // RayHit
  {
    StructDecl rh;
    rh.name = "RayHit";
    rh.exported = true;
    rh.loc = SourceLoc{1, 1};
    rh.fields.push_back(FieldDecl{"hit", Type::ty_bool(), SourceLoc{1, 1}});
    rh.fields.push_back(FieldDecl{"point", Type::ty_struct("Vector3"), SourceLoc{1, 1}});
    rh.fields.push_back(FieldDecl{"distance", Type::ty_float(), SourceLoc{1, 1}});
    m.structs.push_back(std::move(rh));
  }
  
  return m;
}

struct Loader {
  Program prog;
  std::unordered_map<std::string, int> loaded; // canonical path -> index
  std::vector<std::string> stack;

  bool load_module(const fs::path& path, bool is_main) {
    std::error_code ec;
    fs::path can = fs::weakly_canonical(path, ec);
    std::string key = path_to_utf8(can);
    if (loaded.count(key)) {
      // cycle if in stack
      for (auto& s : stack) if (s==key) {
        error_at(key, SourceLoc{1,1}, "E0302", "cyclic module import");
        return false;
      }
      return true;
    }
    if (!fs::exists(can)) {
      error_at(path_to_utf8(path), SourceLoc{1,1}, "E0304", "module `" + path_to_utf8(path) + "` not found");
      return false;
    }
    std::string src = read_file(can);  // leading UTF-8 BOM accepted and stripped (section 2.1)
    std::string dpath = to_diag_path(path);
    {
      size_t bad = 0;
      if (!validate_utf8(src, bad)) {
        // Exactly one E0001; common.hpp suppresses any further (cascade) diagnostics.
        error_at(dpath, byte_loc(src, bad), "E0001", "invalid UTF-8 sequence");
        return false;
      }
    }
    stack.push_back(key);
    Lexer lex(dpath, src);
    Parser parser(lex);
    Module m = parser.parse_module();
    m.path = key;
    m.diag_path = dpath;
    m.is_main = is_main;
    int idx = (int)prog.modules.size();
    prog.modules.push_back(std::move(m));
    loaded[key] = idx;

    // Process imports by index/value: recursive load_module may reallocate prog.modules.
    {
      size_t nimp = prog.modules[idx].imports.size();
      for (size_t ii = 0; ii < nimp; ++ii) {
        ImportDecl im = prog.modules[idx].imports[ii];  // copy; do not hold refs across load
        
        // Handle farmos: builtin modules
        if (is_farmos_builtin(im.path)) {
          if (im.path == "farmos:math") {
            // Ensure farmos:math is loaded once
            if (!loaded.count("farmos:math")) {
              Module math_mod = create_farmos_math_module();
              int mid = (int)prog.modules.size();
              prog.modules.push_back(std::move(math_mod));
              loaded["farmos:math"] = mid;
            }
            continue;
          } else if (im.path == "farmos:scene") {
            // Ensure farmos:math is loaded first (scene depends on math types)
            if (!loaded.count("farmos:math")) {
              Module math_mod = create_farmos_math_module();
              int math_mid = (int)prog.modules.size();
              prog.modules.push_back(std::move(math_mod));
              loaded["farmos:math"] = math_mid;
            }
            // Now load scene
            if (!loaded.count("farmos:scene")) {
              Module scene_mod = create_farmos_scene_module(prog.modules);
              int mid = (int)prog.modules.size();
              prog.modules.push_back(std::move(scene_mod));
              loaded["farmos:scene"] = mid;
            }
            continue;
          } else {
            error_at(prog.modules[idx].diag_path, im.loc, "E0304",
                     "unknown builtin module `" + im.path + "`");
            continue;
          }
        }
        
        if (!is_relative_fm(im.path)) {
          error_at(prog.modules[idx].diag_path, im.loc, "E0301",
                   "invalid module path `" + im.path + "`");
          continue;
        }
        fs::path dep = resolve_import(can, im.path);
        if (!load_module(dep, false)) continue;
      }
    }
    stack.pop_back();
    return true;
  }

  void bind_imports() {
    for (size_t i = 0; i < prog.modules.size(); ++i) {
      auto& mod = prog.modules[i];
      mod.id = (int)i;
      mod.prefix = "m" + std::to_string(i);
      mod.vis_structs.clear();
      mod.vis_classes.clear();
      mod.vis_functions.clear();
      mod.vis_consts.clear();

      auto put_unique = [&](auto& map, const std::string& name, auto* ptr, SourceLoc loc) {
        if (map.count(name)) {
          error_at(mod.diag_path, loc, "E0502", "duplicate definition of `" + name + "`");
          return;
        }
        map[name] = ptr;
      };

      for (auto& s : mod.structs) {
        s.module_id = mod.id;
        s.c_sym = mod.prefix + "_" + s.name;
        put_unique(mod.vis_structs, s.name, &s, s.loc);
      }
      for (auto& c : mod.classes) {
        c.module_id = mod.id;
        if (c.c_sym.empty()) {
          c.c_sym = mod.prefix + "_" + c.name;
        }
        put_unique(mod.vis_classes, c.name, &c, c.loc);
      }
      for (auto& f : mod.functions) {
        f.module_id = mod.id;
        f.c_sym = "fn_" + mod.prefix + "_" + f.name;
        put_unique(mod.vis_functions, f.name, &f, f.loc);
      }
      for (auto& c : mod.consts) {
        c.module_id = mod.id;
        c.c_sym = "v_" + mod.prefix + "_" + c.name;
        put_unique(mod.vis_consts, c.name, &c, c.loc);
      }
    }

    // Special handling: wire farmos:scene to see farmos:math structs
    Module* scene_mod = nullptr;
    Module* math_mod = nullptr;
    for (auto& m : prog.modules) {
      if (m.path == "farmos:scene") scene_mod = &m;
      else if (m.path == "farmos:math") math_mod = &m;
    }
    if (scene_mod && math_mod) {
      // Make all math structs visible to scene module
      for (auto& pair : math_mod->vis_structs) {
        scene_mod->vis_structs[pair.first] = pair.second;
      }
      // M3: Quaternion.set is required for Object3D.quaternion.set(...). Keep it off the
      // farmos:math module for M1/M2 so generated C matches master.
      for (auto& s : math_mod->structs) {
        if (s.name != "Quaternion") continue;
        bool has_set = false;
        for (auto& md : s.methods) if (md.name == "set") { has_set = true; break; }
        if (has_set) break;
        MethodDecl md;
        md.name = "set";
        md.params.push_back(Param{"x", Type::ty_float(), SourceLoc{1,1}});
        md.params.push_back(Param{"y", Type::ty_float(), SourceLoc{1,1}});
        md.params.push_back(Param{"z", Type::ty_float(), SourceLoc{1,1}});
        md.params.push_back(Param{"w", Type::ty_float(), SourceLoc{1,1}});
        md.ret = Type::ty_struct("Quaternion");
        md.loc = SourceLoc{1, 1};
        auto block = std::make_unique<Stmt>();
        block->kind = StmtKind::Block;
        md.body = std::move(block);
        s.methods.push_back(std::move(md));
        break;
      }
    }

    for (auto& mod : prog.modules) {
      std::unordered_set<std::string> imported;
      for (auto& im : mod.imports) {
        // Handle farmos: builtin modules
        if (is_farmos_builtin(im.path)) {
          Module* dep_m = nullptr;
          for (auto& x : prog.modules) if (x.path == im.path) { dep_m = &x; break; }
          if (!dep_m) continue;
          
          for (size_t ni = 0; ni < im.names.size(); ++ni) {
            const std::string& name = im.names[ni];
            SourceLoc nloc = (ni < im.name_locs.size()) ? im.name_locs[ni] : im.loc;
            if (imported.count(name)) {
              error_at(mod.path, nloc, "E0303", "duplicate import of `" + name + "`");
              continue;
            }
            imported.insert(name);
            
            auto clash = [&]() {
              return mod.vis_functions.count(name) || mod.vis_consts.count(name) ||
                     mod.vis_structs.count(name) || mod.vis_classes.count(name);
            };
            
            if (dep_m->vis_structs.count(name)) {
              if (clash()) {
                error_at(mod.diag_path, nloc, "E0510", "import `" + name + "` conflicts with existing definition");
                continue;
              }
              mod.vis_structs[name] = dep_m->vis_structs[name];
            } else if (dep_m->vis_classes.count(name)) {
              if (clash()) {
                error_at(mod.diag_path, nloc, "E0510", "import `" + name + "` conflicts with existing definition");
                continue;
              }
              mod.vis_classes[name] = dep_m->vis_classes[name];
            } else if (dep_m->vis_functions.count(name)) {
              if (clash()) {
                error_at(mod.diag_path, nloc, "E0510", "import `" + name + "` conflicts with existing definition");
                continue;
              }
              mod.vis_functions[name] = dep_m->vis_functions[name];
            } else if (dep_m->vis_consts.count(name)) {
              if (clash()) {
                error_at(mod.diag_path, nloc, "E0510", "import `" + name + "` conflicts with existing definition");
                continue;
              }
              mod.vis_consts[name] = dep_m->vis_consts[name];
            } else {
              error_at(mod.diag_path, nloc, "E0305", "`" + name + "` not found in module `" + im.path + "`");
            }
          }
          continue;
        }
        
        if (!is_relative_fm(im.path)) continue;
        fs::path dep = resolve_import(mod.path, im.path);
        std::error_code ec;
        std::string dep_key = path_to_utf8(fs::weakly_canonical(dep, ec));
        Module* dep_m = nullptr;
        for (auto& x : prog.modules) if (x.path == dep_key) { dep_m = &x; break; }
        if (!dep_m) continue;
        for (size_t ni = 0; ni < im.names.size(); ++ni) {
          const std::string& name = im.names[ni];
          SourceLoc nloc = (ni < im.name_locs.size()) ? im.name_locs[ni] : im.loc;
          if (imported.count(name)) {
            error_at(mod.path, nloc, "E0303", "duplicate import of `" + name + "`");
            continue;
          }
          imported.insert(name);

          auto clash = [&]() {
            return mod.vis_functions.count(name) || mod.vis_consts.count(name) ||
                   mod.vis_structs.count(name) || mod.vis_classes.count(name);
          };

          bool bound = false;
          for (auto& f : dep_m->functions) if (f.name == name) {
            if (!f.exported) error_at(mod.diag_path, nloc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
            else if (clash()) error_at(mod.diag_path, nloc, "E0502", "duplicate definition of `" + name + "`");
            else mod.vis_functions[name] = &f;
            bound = true; break;
          }
          if (bound) continue;
          for (auto& c : dep_m->consts) if (c.name == name) {
            if (!c.exported) error_at(mod.diag_path, nloc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
            else if (clash()) error_at(mod.diag_path, nloc, "E0502", "duplicate definition of `" + name + "`");
            else mod.vis_consts[name] = &c;
            bound = true; break;
          }
          if (bound) continue;
          for (auto& s : dep_m->structs) if (s.name == name) {
            if (!s.exported) error_at(mod.diag_path, nloc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
            else if (clash()) error_at(mod.diag_path, nloc, "E0502", "duplicate definition of `" + name + "`");
            else mod.vis_structs[name] = &s;
            bound = true; break;
          }
          if (bound) continue;
          for (auto& c : dep_m->classes) if (c.name == name) {
            if (!c.exported) error_at(mod.diag_path, nloc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
            else if (clash()) error_at(mod.diag_path, nloc, "E0502", "duplicate definition of `" + name + "`");
            else mod.vis_classes[name] = &c;
            bound = true; break;
          }
          if (!bound) {
            error_at(mod.diag_path, nloc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
          }
        }
      }
    }
  }
};


static std::string lower_ascii(std::string s) {
  for (char& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

static std::string trim_ws(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Basename with extension stripped, case-insensitive, is `cl` (or `clang-cl`, MSVC-style driver).
static bool token_is_msvc(const std::string& tok) {
  std::string base = lower_ascii(path_to_utf8(fs::path(tok).filename()));
  size_t dot = base.find_last_of('.');
  std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
  return stem == "cl" || stem == "clang-cl";
}

static bool is_file(const std::string& p) {
  std::error_code ec;
  return fs::is_regular_file(fs::path(p), ec) || fs::is_regular_file(fs::path(p + ".exe"), ec);
}

// Split a FARM_CC value into {compiler token, remaining args}. FARM_CC is a command prefix, so it
// may carry arguments ("cl /nologo") or padding ("cl "). Resolution order:
//   1. leading double quote: the quoted text is the token;
//   2. the whole trimmed value names an existing file: the whole value is the token (no args);
//   3. the shortest space-delimited prefix that names an existing file (unquoted paths with
//      spaces, e.g. C:\Program Files\LLVM\bin\clang.exe -O2), as Windows command resolution does;
//   4. otherwise the text up to the first whitespace (bare command name such as `cl` or `clang`).
struct CcSplit { std::string token, rest; };
static CcSplit split_cc(const std::string& cc_raw) {
  std::string t = trim_ws(cc_raw);
  CcSplit r;
  if (t.empty()) return r;
  if (t[0] == '"') {
    size_t e = t.find('"', 1);
    if (e == std::string::npos) { r.token = t.substr(1); return r; }
    r.token = t.substr(1, e - 1);
    r.rest = trim_ws(t.substr(e + 1));
    return r;
  }
  if (is_file(t)) { r.token = t; return r; }
  for (size_t pos = t.find(' '); pos != std::string::npos; pos = t.find(' ', pos + 1)) {
    std::string pre = t.substr(0, pos);
    if (is_file(pre)) { r.token = pre; r.rest = trim_ws(t.substr(pos + 1)); return r; }
  }
  size_t e = t.find_first_of(" \t");
  r.token = (e == std::string::npos) ? t : t.substr(0, e);
  r.rest = (e == std::string::npos) ? std::string() : trim_ws(t.substr(e + 1));
  return r;
}

// MSVC cl is unsupported as the C backend (spec section 7.1): driver configuration error, exit 3.
static bool is_msvc_cc(const std::string& cc_raw) {
  std::string t = trim_ws(cc_raw);
  if (t.empty()) return false;
  return token_is_msvc(split_cc(t).token);
}

// Command prefix actually executed: the resolved compiler token is always quoted, so an unquoted
// FARM_CC path containing spaces runs the intended executable.
static std::string cc_command_prefix(const std::string& cc_raw) {
  CcSplit c = split_cc(cc_raw);
  std::string r = "\"" + c.token + "\"";
  if (!c.rest.empty()) r += " " + c.rest;
  return r;
}

static std::string find_c_compiler() {
  const char* env = std::getenv("FARM_CC");
  if (env) {
    std::string cc = trim_ws(env);
    if (!cc.empty()) return cc;
  }
#ifdef _WIN32
  // Prefer clang from PATH via where.exe, return first line (full path)
  const char* cands[] = {"clang.exe", "clang", "gcc.exe", "gcc"};
  for (auto c : cands) {
    FILE* pipe = _popen((std::string("where ") + c + " 2>nul").c_str(), "r");
    if (!pipe) continue;
    char buf[1024];
    if (fgets(buf, sizeof(buf), pipe)) {
      _pclose(pipe);
      std::string path = buf;
      while (!path.empty() && (path.back()=='\n' || path.back()=='\r')) path.pop_back();
      if (!path.empty()) return "\"" + path + "\"";
    } else _pclose(pipe);
  }
#else
  const char* cands[] = {"clang", "gcc", "cc"};
  for (auto c : cands) {
    std::string cmd = std::string("command -v ") + c + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) == 0) return c;
  }
#endif
  return {};
}

// Per-invocation scratch directory: <TEMP>/farmc-<pid>-<counter>-<random>. Created with
// create_directory, which fails if the name already exists, so the directory is exclusively ours
// even across concurrent farmc processes. EVERY intermediate file (generated .c, .cmd wrappers,
// link output before the final move, `farmc run` binary) lives inside it, and the whole directory
// is removed by the destructor on success, failure, or exception (no fixed temp names).
class ScratchDir {
 public:
  ScratchDir() = default;
  ~ScratchDir() { cleanup(); }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
  const fs::path& path() {
    if (dir_.empty()) create();
    return dir_;
  }
  fs::path file(const std::string& name) { return path() / name; }
  fs::path unique_file(const std::string& stem, const std::string& ext) {
    return path() / (stem + "_" + std::to_string(++seq_) + ext);
  }
  void cleanup() {
    if (dir_.empty()) return;
    std::error_code ec;
    fs::remove_all(dir_, ec);
    dir_.clear();
  }
 private:
  void create() {
    static std::atomic<unsigned> counter{0};
#ifdef _WIN32
    unsigned long pid = (unsigned long)GetCurrentProcessId();
    fs::path base = win_temp_directory();
#else
    unsigned long pid = (unsigned long)getpid();
    fs::path base = fs::temp_directory_path();
#endif
    std::random_device rd;
    std::mt19937_64 rng(((uint64_t)rd() << 32) ^ (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ pid);
    for (int attempt = 0; attempt < 100; ++attempt) {
      char suf[17];
      std::snprintf(suf, sizeof(suf), "%016llx", (unsigned long long)rng());
      fs::path cand = base / ("farmc-" + std::to_string(pid) + "-" + std::to_string(++counter) + "-" + suf);
      std::error_code ec;
      if (fs::create_directory(cand, ec) && !ec) { dir_ = cand; return; }
    }
    throw DriverError("cannot create a unique temporary directory under '" + path_to_utf8(base) + "'");
  }
  fs::path dir_;
  unsigned seq_ = 0;
};

static ScratchDir& scratch() {
  static ScratchDir s;
  return s;
}

static int run_cmd(const std::string& cmd_utf8) {
#ifdef _WIN32
  // CreateProcessW + UTF-16 command line. No cmd.exe / .cmd: those round-trip through ACP.
  std::wstring wcmd = utf8_to_wide(cmd_utf8);
  std::vector<wchar_t> buf(wcmd.begin(), wcmd.end());
  buf.push_back(L'\0');
  STARTUPINFOW si;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi;
  ZeroMemory(&pi, sizeof(pi));
  if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    return -1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return (int)code;
#else
  return std::system(cmd_utf8.c_str());
#endif
}

static int compile_c_to_exe(const fs::path& c_file, const fs::path& rt_c, const fs::path& rt_h_dir,
                            const fs::path& out_exe, bool verbose, bool link_scene) {
  std::string cc = find_c_compiler();
  if (cc.empty()) {
    std::cerr << "farmc: no C compiler found (set FARM_CC)\n";
    return 3;
  }
  // Spec section 7.1: clang or gcc only. MSVC cl is a driver configuration error (exit 3).
  // Detect BEFORE invoking any process.
  if (is_msvc_cc(cc)) {
    std::cerr << "farmc: C compiler must be clang or gcc; MSVC cl is unsupported\n";
    return 3;
  }
  cc = cc_command_prefix(cc);
  // Size-oriented flags; NO -ffast-math. GNU statement-expressions require clang/gcc.
  // -ffp-contract=off: required for M3 PNG determinism; does not change generated C.
  fs::path rt_math_c = rt_h_dir / "farm_math.c";
  fs::path rt_scene_c = rt_h_dir / "farm_scene.c";

  auto append_scene_rt = [&](std::ostringstream& o) {
    // Link farm_math.c / farm_scene.c only for farmos:scene programs (M1/M2 match master).
    if (link_scene) {
      if (fs::exists(rt_math_c)) o << "\"" << path_to_utf8(rt_math_c) << "\" ";
      if (fs::exists(rt_scene_c)) o << "\"" << path_to_utf8(rt_scene_c) << "\" ";
    }
  };

  std::ostringstream cmd;
  cmd << cc << " -std=c11 -Os -flto -ffunction-sections -fdata-sections -ffp-contract=off "
      << "-I\"" << path_to_utf8(rt_h_dir) << "\" "
      << "\"" << path_to_utf8(c_file) << "\" \"" << path_to_utf8(rt_c) << "\" ";
  append_scene_rt(cmd);
  cmd << "-o \"" << path_to_utf8(out_exe) << "\" "
      << "-Wl,--gc-sections -s -lm";
  if (verbose) std::cerr << "farmc: " << cmd.str() << "\n";
  int rc = run_cmd(cmd.str());
  if (rc != 0) {
    // retry without LTO
    std::ostringstream cmd2;
    cmd2 << cc << " -std=c11 -Os -ffunction-sections -fdata-sections -ffp-contract=off "
         << "-I\"" << path_to_utf8(rt_h_dir) << "\" "
         << "\"" << path_to_utf8(c_file) << "\" \"" << path_to_utf8(rt_c) << "\" ";
    append_scene_rt(cmd2);
    cmd2 << "-o \"" << path_to_utf8(out_exe) << "\" "
         << "-Wl,--gc-sections -s -lm";
    if (verbose) std::cerr << "farmc: retry " << cmd2.str() << "\n";
    rc = run_cmd(cmd2.str());
  }
  if (rc != 0) {
    std::cerr << "farmc: C compiler failed\n";
    return 3;
  }
  return 0;
}


static fs::path default_runtime_dir() {
  // farmc.exe next to ../runtime or FARM_RUNTIME
#ifdef _WIN32
  {
    wchar_t wbuf[32768];
    DWORD n = GetEnvironmentVariableW(L"FARM_RUNTIME", wbuf, (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n > 0 && n < sizeof(wbuf) / sizeof(wbuf[0])) return fs::path(wbuf);
  }
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  fs::path exe = (n ? fs::path(buf) : fs::path(L"farmc.exe"));
#else
  if (const char* e = std::getenv("FARM_RUNTIME")) return fs::path(e);
  fs::path exe = fs::read_symlink("/proc/self/exe");
#endif
  fs::path cand = exe.parent_path() / "runtime";
  if (fs::exists(cand / "farm_rt.c")) return cand;
  cand = exe.parent_path().parent_path() / "runtime";
  if (fs::exists(cand / "farm_rt.c")) return cand;
  // source tree: build/../runtime
  cand = exe.parent_path().parent_path().parent_path() / "runtime";
  if (fs::exists(cand / "farm_rt.c")) return cand;
  return exe.parent_path() / "runtime";
}

static int cmd_build(std::vector<std::string> args) {
  std::string infile, outfile;
  bool do_emit_c = false, keep_c = false, verbose = false;
  std::string emit_c_path;
  for (size_t i=0;i<args.size();++i) {
    if (args[i]=="-o" && i+1<args.size()) outfile = args[++i];
    else if (args[i]=="--emit-c") {
      do_emit_c = true;
      if (i+1<args.size() && args[i+1][0]!='-') emit_c_path = args[++i];
    } else if (args[i]=="--keep-c") keep_c = true;
    else if (args[i]=="-v") verbose = true;
    else if (args[i][0]!='-') infile = args[i];
    else {
      std::cerr << "farmc: unknown option " << args[i] << "\n";
      return 2;
    }
  }
  if (infile.empty()) { std::cerr << "farmc build: missing file\n"; return 2; }
#ifdef FARMC_ENABLE_TEST_HOOKS
  // Test hook for section 7.2 exit 4 (internal compiler error path). Compiled ONLY into the
  // farmc_testhooks target; the shipped farmc has no hook and ignores FARMC_TEST_ICE.
  if (const char* ice = std::getenv("FARMC_TEST_ICE")) {
    if (std::string(ice) == "1") throw std::logic_error("FARMC_TEST_ICE test hook");
  }
#endif
  {
    // Section 7.2: unreadable/missing input is a driver I/O failure (exit 3), not a compile error.
    std::error_code ec;
    std::ifstream probe(fs::path(infile), std::ios::binary);
    if (!fs::is_regular_file(fs::path(infile), ec) || !probe) {
      std::cerr << "farmc: cannot read input file '" << infile << "'\n";
      return 3;
    }
  }

  if (!outfile.empty()) {
    // -o naming an existing directory: driver I/O failure (exit 3). Never delete or replace it,
    // and never write a stray "<dir>.exe" beside it.
    std::error_code ec;
    if (fs::is_directory(fs::path(outfile), ec)) {
      std::cerr << "farmc: output path '" << outfile << "' is a directory\n";
      return 3;
    }
  }

  clear_diags();
  Loader loader;
  if (!loader.load_module(infile, true)) {
    emit_diagnostics();
    return has_errors() ? 1 : 3;
  }
  loader.bind_imports();
  if (has_errors()) { emit_diagnostics(); return 1; }
  analyze_program(loader.prog);
  if (has_errors()) { emit_diagnostics(); return 1; }

  std::string csrc = farm::emit_c(loader.prog);

  fs::path inpath(infile);
  if (outfile.empty()) {
#ifdef _WIN32
    outfile = path_to_utf8(inpath.stem()) + ".exe";
#else
    outfile = path_to_utf8(inpath.stem());
#endif
  }

  fs::path tmp_c;
  if (!emit_c_path.empty()) tmp_c = emit_c_path;
  else tmp_c = scratch().file(path_to_utf8(inpath.stem()) + "_farmc_gen.c");
  {
    std::ofstream o(tmp_c, std::ios::binary);
    if (!o) { std::cerr << "farmc: cannot write " << path_to_utf8(tmp_c) << "\n"; return 3; }
    o << csrc;
  }
  if (do_emit_c && emit_c_path.empty()) {
    // default emit next to output
    fs::path p = fs::path(outfile).replace_extension(".c");
    std::ofstream o(p, std::ios::binary); o << csrc;
  }
  if (keep_c && emit_c_path.empty() && !do_emit_c) {
    fs::path p = fs::path(outfile).replace_extension(".c");
    std::ofstream o(p, std::ios::binary); o << csrc;
  }

  fs::path rt = default_runtime_dir();
  // Link into the private scratch dir, then move to the requested path. This handles section 7.1
  // (SHOULD): an -o without .exe still yields a PE at exactly that path (MinGW linkers append
  // .exe to suffix-less -o, so the scratch name always ends in .exe), and a failed link never
  // touches the destination or leaves partial files beside it.
  fs::path out_req(outfile);
  fs::path link_out = scratch().file("link_out.exe");
  bool link_scene = false;
  for (auto& m : loader.prog.modules) {
    if (m.path == "farmos:scene") { link_scene = true; break; }
    for (auto& imp : m.imports) if (imp.path == "farmos:scene") { link_scene = true; break; }
    if (link_scene) break;
  }
  int rc = compile_c_to_exe(tmp_c, rt / "farm_rt.c", rt, link_out, verbose, link_scene);
  if (rc == 0) {
    std::error_code ec;
    fs::rename(link_out, out_req, ec);
    if (ec) {  // e.g. TEMP on another volume: copy, then the scratch dir cleanup removes the source
      ec.clear();
      fs::copy_file(link_out, out_req, fs::copy_options::overwrite_existing, ec);
    }
    if (ec) {
      std::cerr << "farmc: cannot write output " << path_to_utf8(out_req) << ": " << ec.message() << "\n";
      rc = 3;
    }
  }
  scratch().cleanup();
  return rc;
}

static int cmd_run(std::vector<std::string> args) {
  std::string infile;
  std::vector<std::string> rest;
  bool saw_dd = false;
  for (auto& a : args) {
    if (a=="--") { saw_dd=true; continue; }
    if (!saw_dd && infile.empty() && a[0]!='-') infile = a;
    else if (saw_dd) rest.push_back(a);
  }
  if (infile.empty()) { std::cerr << "farmc run: missing file\n"; return 2; }
  // Private per-process run directory (not the build scratch dir, which cmd_build removes).
  ScratchDir run_dir;
  fs::path out = run_dir.file(path_to_utf8(fs::path(infile).stem()) + ".exe");
  std::vector<std::string> build_args = {infile, "-o", path_to_utf8(out)};
  int rc = cmd_build(build_args);
  if (rc != 0) return rc;
  std::ostringstream cmd;
  cmd << "\"" << path_to_utf8(out) << "\"";
  for (auto& a : rest) cmd << " \"" << a << "\"";
  rc = run_cmd(cmd.str());
  scratch().cleanup();
#ifdef _WIN32
  // system returns exit code directly-ish
  return rc;
#else
  if (WIFEXITED(rc)) return WEXITSTATUS(rc);
  return rc;
#endif
}

} // namespace farm

static int farmc_main(int argc, char** argv) {
  using namespace farm;
  if (argc < 2) {
    std::cout << "farmc - Farmos compiler\nUsage: farmc <build|run|version|help> ...\n";
    return 2;
  }
  std::string cmd = argv[1];
  std::vector<std::string> args;
  for (int i=2;i<argc;++i) args.push_back(argv[i]);
  if (cmd=="version") { std::cout << "farmc 0.1.0-m1\n"; return 0; }
  if (cmd=="help" || cmd=="-h" || cmd=="--help") {
    std::cout << "farmc build <file.fm> [-o out] [--emit-c [path]] [--keep-c] [-v]\n"
              << "farmc run <file.fm> [-- args...]\n"
              << "farmc version\n";
    return 0;
  }
  if (cmd=="build") return cmd_build(args);
  if (cmd=="run") return cmd_run(args);
  std::cerr << "farmc: unknown command " << cmd << "\n";
  return 2;
}

// Section 7.2: DriverError -> exit 3; any other escaped exception is ICE (exit 4).
int main(int argc, char** argv) {
  try {
    return farmc_main(argc, argv);
  } catch (const farm::DriverError& e) {
    std::cerr << "farmc: " << e.what() << "\n";
    return 3;
  } catch (const std::exception& e) {
    std::cerr << "farmc: internal compiler error: " << e.what() << "\n";
    return 4;
  } catch (...) {
    std::cerr << "farmc: internal compiler error: unknown exception\n";
    return 4;
  }
}

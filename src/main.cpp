#include "lexer.hpp"
#include "parser.hpp"
#include "sema.hpp"
#include "emit_c.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace farm {

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
  std::error_code ec;
  fs::path abs = fs::weakly_canonical(p, ec);
  if (ec) abs = fs::absolute(p, ec);
  if (ec) return p.generic_string();
  fs::path rel = fs::relative(abs, fs::current_path(), ec);
  if (!ec && !rel.empty()) {
    std::string s = rel.generic_string();
    if (s != ".." && s.rfind("../", 0) != 0) return s;
  }
  return abs.generic_string();
}


struct Loader {
  Program prog;
  std::unordered_map<std::string, int> loaded; // canonical path -> index
  std::vector<std::string> stack;

  bool load_module(const fs::path& path, bool is_main) {
    std::error_code ec;
    fs::path can = fs::weakly_canonical(path, ec);
    std::string key = can.string();
    if (loaded.count(key)) {
      // cycle if in stack
      for (auto& s : stack) if (s==key) {
        error_at(key, SourceLoc{1,1}, "E0302", "cyclic module import");
        return false;
      }
      return true;
    }
    if (!fs::exists(can)) {
      error_at(path.string(), SourceLoc{1,1}, "E0304", "module `" + path.string() + "` not found");
      return false;
    }
    std::string src = read_file(can);
    if (src.empty() && fs::file_size(can) != 0) {
      error_at(key, SourceLoc{1,1}, "E0304", "module not found");
      return false;
    }
    stack.push_back(key);
    std::string dpath = to_diag_path(path);
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
        c.c_sym = mod.prefix + "_" + c.name;
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

    for (auto& mod : prog.modules) {
      std::unordered_set<std::string> imported;
      for (auto& im : mod.imports) {
        if (!is_relative_fm(im.path)) continue;
        fs::path dep = resolve_import(mod.path, im.path);
        std::error_code ec;
        std::string dep_key = fs::weakly_canonical(dep, ec).string();
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

// MSVC cl / cl.exe (and clang-cl MSVC-style driver) are unsupported as the C backend (spec section 7.1).
static bool is_msvc_cc(const std::string& cc_raw) {
  std::string s = cc_raw;
  while (!s.empty() && (s.front() == '"' || s.front() == '\'')) s.erase(s.begin());
  while (!s.empty() && (s.back() == '"' || s.back() == '\'')) s.pop_back();
  std::string base = lower_ascii(fs::path(s).filename().string());
  // Extension-stripped stem, case-insensitive: cl.exe / cl.bat / cl.cmd / CL.EXE -> cl
  auto dot = base.find_last_of('.');
  std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
  if (stem == "cl") return true;
  if (stem == "clang-cl") return true;
  return false;
}


static std::string find_c_compiler() {
  const char* env = std::getenv("FARM_CC");
  if (env && *env) return env;
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

static int run_cmd(const std::string& cmd) {
#ifdef _WIN32
  // Write to a temporary .cmd to avoid cmd.exe /c quoting pitfalls with system()
  fs::path bat = fs::temp_directory_path() / "farmc_cc_run.cmd";
  {
    std::ofstream o(bat);
    o << "@echo off\r\n" << cmd << "\r\n";
  }
  std::string inv = "cmd.exe /C \"" + bat.string() + "\"";
  int rc = std::system(inv.c_str());
  std::error_code ec; fs::remove(bat, ec);
  return rc;
#else
  return std::system(cmd.c_str());
#endif
}

static int compile_c_to_exe(const fs::path& c_file, const fs::path& rt_c, const fs::path& rt_h_dir,
                            const fs::path& out_exe, bool verbose) {
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
  // Size-oriented flags; NO -ffast-math. GNU statement-expressions require clang/gcc.
  std::ostringstream cmd;
  cmd << cc << " -std=c11 -Os -flto -ffunction-sections -fdata-sections "
      << "-I\"" << rt_h_dir.string() << "\" "
      << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
      << "-o \"" << out_exe.string() << "\" "
      << "-Wl,--gc-sections -s -lm";
  if (verbose) std::cerr << "farmc: " << cmd.str() << "\n";
  int rc = run_cmd(cmd.str());
  if (rc != 0) {
    // retry without LTO
    std::ostringstream cmd2;
    cmd2 << cc << " -std=c11 -Os -ffunction-sections -fdata-sections "
         << "-I\"" << rt_h_dir.string() << "\" "
         << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
         << "-o \"" << out_exe.string() << "\" "
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
  if (const char* e = std::getenv("FARM_RUNTIME")) return fs::path(e);
#ifdef _WIN32
  char buf[MAX_PATH];
  DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  fs::path exe = (n ? fs::path(buf) : fs::path("farmc.exe"));
#else
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
    outfile = inpath.stem().string() + ".exe";
#else
    outfile = inpath.stem().string();
#endif
  }

  fs::path tmp_c;
  if (!emit_c_path.empty()) tmp_c = emit_c_path;
  else {
    tmp_c = fs::temp_directory_path() / (inpath.stem().string() + "_farmc_gen.c");
  }
  {
    std::ofstream o(tmp_c);
    if (!o) { std::cerr << "farmc: cannot write " << tmp_c << "\n"; return 3; }
    o << csrc;
  }
  if (do_emit_c && emit_c_path.empty()) {
    // default emit next to output
    fs::path p = fs::path(outfile).replace_extension(".c");
    std::ofstream o(p); o << csrc;
  }

  fs::path rt = default_runtime_dir();
  // Spec section 7.1 SHOULD: if -o has no .exe suffix, still produce a PE usable at exactly
  // that path. MinGW/clang linkers append .exe to suffix-less -o, so link to "<out>.exe"
  // and rename to the requested path.
  fs::path out_req(outfile);
  std::string ext_lc = lower_ascii(out_req.extension().string());
  bool needs_rename = (ext_lc != ".exe");
  fs::path link_out = needs_rename ? fs::path(outfile + ".exe") : out_req;
  int rc = compile_c_to_exe(tmp_c, rt / "farm_rt.c", rt, link_out, verbose);
  if (rc == 0 && needs_rename) {
    std::error_code ec;
    fs::remove(out_req, ec);
    ec.clear();
    fs::rename(link_out, out_req, ec);
    if (ec) {
      std::cerr << "farmc: cannot write output " << out_req.string() << ": " << ec.message() << "\n";
      rc = 3;
    }
  }
  if (!keep_c && emit_c_path.empty() && !do_emit_c) {
    std::error_code ec; fs::remove(tmp_c, ec);
  }
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
  fs::path out = fs::temp_directory_path() / "farmc_run_tmp.exe";
  int rc = cmd_build({infile, "-o", out.string()});
  if (rc != 0) return rc;
  std::ostringstream cmd;
  cmd << "\"" << out.string() << "\"";
  for (auto& a : rest) cmd << " \"" << a << "\"";
  rc = run_cmd(cmd.str());
#ifdef _WIN32
  // system returns exit code directly-ish
  return rc;
#else
  if (WIFEXITED(rc)) return WEXITSTATUS(rc);
  return rc;
#endif
}

} // namespace farm

int main(int argc, char** argv) {
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

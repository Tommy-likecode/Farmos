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
    Lexer lex(key, src);
    Parser parser(lex);
    Module m = parser.parse_module();
    m.path = key;
    m.is_main = is_main;
    int idx = (int)prog.modules.size();
    prog.modules.push_back(std::move(m));
    loaded[key] = idx;

    // process imports
    Module& mod = prog.modules[idx];
    for (auto& im : mod.imports) {
      if (!is_relative_fm(im.path)) {
        error_at(key, im.loc, "E0301", "invalid module path `" + im.path + "`");
        continue;
      }
      fs::path dep = resolve_import(can, im.path);
      if (!load_module(dep, false)) continue;
    }
    stack.pop_back();
    return true;
  }

  void bind_imports() {
    // For each import, check exports and inject into maps with visibility
    // Build export sets first — maps already collect all; filter by export when resolving names from other modules.
    // Simpler M1 approach: put only exported decls from non-main into global maps; main's all decls go in.
    // Actually collect_decls in sema puts everything. We'll rebuild maps properly here.

    prog.structs.clear();
    prog.classes.clear();
    prog.functions.clear();
    prog.consts.clear();

    // First pass: all local decls into per-module, then imports add aliases
    // Global map: name -> decl. Import brings name into importing module's scope via global map (M1 flat).

    struct PendingImport {
      std::string importer;
      ImportDecl* im;
      std::string dep_key;
    };
    std::vector<PendingImport> pending;

    for (auto& m : prog.modules) {
      auto add_all = [&](bool only_export) {
        for (auto& s : m.structs) if (!only_export || s.exported) {
          if (prog.structs.count(s.name)) error_at(m.path, s.loc, "E0502", "duplicate definition of `" + s.name + "`");
          prog.structs[s.name] = &s;
        }
        for (auto& c : m.classes) if (!only_export || c.exported) {
          if (prog.classes.count(c.name)) error_at(m.path, c.loc, "E0502", "duplicate definition of `" + c.name + "`");
          prog.classes[c.name] = &c;
        }
        for (auto& f : m.functions) if (!only_export || f.exported) {
          if (prog.functions.count(f.name)) error_at(m.path, f.loc, "E0502", "duplicate definition of `" + f.name + "`");
          prog.functions[f.name] = &f;
        }
        for (auto& c : m.consts) if (!only_export || c.exported) {
          if (prog.consts.count(c.name)) error_at(m.path, c.loc, "E0502", "duplicate definition of `" + c.name + "`");
          prog.consts[c.name] = &c;
        }
      };
      // Always register module's own decls (exported or not) for use within... 
      // Cross-module: only exports. Same module uses all.
      // Flat namespace: register ALL decls from all modules; import just checks export and duplicate import.
      for (auto& s : m.structs) {
        if (prog.structs.count(s.name)) error_at(m.path, s.loc, "E0502", "duplicate definition of `" + s.name + "`");
        prog.structs[s.name] = &s;
      }
      for (auto& c : m.classes) {
        if (prog.classes.count(c.name)) error_at(m.path, c.loc, "E0502", "duplicate definition of `" + c.name + "`");
        prog.classes[c.name] = &c;
      }
      for (auto& f : m.functions) {
        if (prog.functions.count(f.name)) error_at(m.path, f.loc, "E0502", "duplicate definition of `" + f.name + "`");
        prog.functions[f.name] = &f;
      }
      for (auto& c : m.consts) {
        if (prog.consts.count(c.name)) error_at(m.path, c.loc, "E0502", "duplicate definition of `" + c.name + "`");
        prog.consts[c.name] = &c;
      }
    }

    // Validate imports: exported, no duplicate names in import list
    for (auto& m : prog.modules) {
      std::unordered_set<std::string> imported;
      for (auto& im : m.imports) {
        if (!is_relative_fm(im.path)) continue;
        fs::path dep = resolve_import(m.path, im.path);
        std::error_code ec;
        std::string dep_key = fs::weakly_canonical(dep, ec).string();
        Module* dep_m = nullptr;
        for (auto& x : prog.modules) if (x.path == dep_key) { dep_m = &x; break; }
        if (!dep_m) continue;
        for (auto& name : im.names) {
          if (imported.count(name)) {
            error_at(m.path, im.loc, "E0303", "duplicate import of `" + name + "`");
            continue;
          }
          imported.insert(name);
          bool found=false, exported=false;
          for (auto& f : dep_m->functions) if (f.name==name) { found=true; exported=f.exported; break; }
          for (auto& c : dep_m->consts) if (c.name==name) { found=true; exported=c.exported; break; }
          for (auto& s : dep_m->structs) if (s.name==name) { found=true; exported=s.exported; break; }
          for (auto& c : dep_m->classes) if (c.name==name) { found=true; exported=c.exported; break; }
          if (!found || !exported)
            error_at(m.path, im.loc, "E0305", "`" + name + "` is not exported from `" + im.path + "`");
        }
      }
    }
  }
};

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
  // Size-oriented flags; NO -ffast-math
  std::ostringstream cmd;
  bool is_msvc = (cc.find("cl") != std::string::npos && cc.find("clang") == std::string::npos);
  if (is_msvc) {
    cmd << cc << " /nologo /std:c11 /O1 /GL /Gy /I\"" << rt_h_dir.string() << "\" "
        << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
        << "/Fe:\"" << out_exe.string() << "\" /link /LTCG /OPT:REF /OPT:ICF";
  } else {
    cmd << cc << " -std=c11 -Os -ffunction-sections -fdata-sections "
        << "-I\"" << rt_h_dir.string() << "\" "
        << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
        << "-o \"" << out_exe.string() << "\" "
        << "-Wl,--gc-sections -s";
    // try LTO
    // Note: some mingw need -flto; add it
    // Rebuild command with -flto
  }
  // Prefer LTO variant for non-msvc
  if (!is_msvc) {
    cmd.str("");
    cmd.clear();
    cmd << cc << " -std=c11 -Os -flto -ffunction-sections -fdata-sections "
        << "-I\"" << rt_h_dir.string() << "\" "
        << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
        << "-o \"" << out_exe.string() << "\" "
        << "-Wl,--gc-sections -s -lm";
  }
  if (verbose) std::cerr << "farmc: " << cmd.str() << "\n";
  int rc = run_cmd(cmd.str());
  if (rc != 0) {
    // retry without LTO
    if (!is_msvc) {
      std::ostringstream cmd2;
      cmd2 << cc << " -std=c11 -Os -ffunction-sections -fdata-sections "
           << "-I\"" << rt_h_dir.string() << "\" "
           << "\"" << c_file.string() << "\" \"" << rt_c.string() << "\" "
           << "-o \"" << out_exe.string() << "\" "
           << "-Wl,--gc-sections -s -lm";
      if (verbose) std::cerr << "farmc: retry " << cmd2.str() << "\n";
      rc = run_cmd(cmd2.str());
    }
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
  int rc = compile_c_to_exe(tmp_c, rt / "farm_rt.c", rt, outfile, verbose);
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
    std::cout << "farmc — Farmos compiler\nUsage: farmc <build|run|version|help> ...\n";
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

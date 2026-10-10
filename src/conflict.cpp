#include "sema.hpp"
#include <cstddef>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <cmath>

namespace farm {

namespace {

enum class RootK { Var, Site, Param, Unknown, Graph, Tree };
enum class OverlapK { Disjoint, Definite, Possible };
enum class IdxK { Const, Interval, Sym, Any, Len };

struct Origin {
  enum Kind { None, Site, Param, Unknown } kind = None;
  int id = 0;
  std::string cls;
  bool operator==(const Origin& o) const { return kind == o.kind && id == o.id && cls == o.cls; }
};

struct Step {
  enum Kind { Field, Index, Len } kind = Field;
  std::string field;
  IdxK idx = IdxK::Any;
  int64_t k = 0, lo = 0, hi = 0, off = 0;
  std::string sym;
};

struct Path {
  RootK root = RootK::Var;
  std::string var;
  Origin origin;
  std::string cls;
  std::vector<Step> steps;
  std::string display;
};

enum class CVKind { None, Int, Float, Bool, String, Struct, Array };

struct CVal {
  CVKind kind = CVKind::None;
  int64_t i = 0;
  double f = 0;
  bool b = false;
  std::string s;
  std::vector<std::pair<std::string, CVal>> fields;
  std::vector<CVal> elems;
  static CVal unk() { return {}; }
  bool known() const { return kind != CVKind::None; }
};

static uint64_t fbits(double d) {
  uint64_t u = 0;
  std::memcpy(&u, &d, sizeof(double));
  return u;
}

static bool cval_eq(const CVal& a, const CVal& b) {
  if (a.kind != b.kind || a.kind == CVKind::None) return false;
  switch (a.kind) {
    case CVKind::Int: return a.i == b.i;
    case CVKind::Float: return fbits(a.f) == fbits(b.f);
    case CVKind::Bool: return a.b == b.b;
    case CVKind::String: return a.s == b.s;
    case CVKind::Struct: {
      if (a.fields.size() != b.fields.size()) return false;
      for (size_t i = 0; i < a.fields.size(); ++i)
        if (a.fields[i].first != b.fields[i].first || !cval_eq(a.fields[i].second, b.fields[i].second))
          return false;
      return true;
    }
    case CVKind::Array: {
      if (a.elems.size() != b.elems.size()) return false;
      for (size_t i = 0; i < a.elems.size(); ++i)
        if (!cval_eq(a.elems[i], b.elems[i])) return false;
      return true;
    }
    default: return false;
  }
}

static CVal project(const CVal& v, const std::vector<Step>& extra) {
  CVal cur = v;
  for (auto& st : extra) {
    if (st.kind == Step::Field) {
      if (cur.kind != CVKind::Struct) return CVal::unk();
      bool found = false;
      for (auto& f : cur.fields) if (f.first == st.field) { cur = f.second; found = true; break; }
      if (!found) return CVal::unk();
    } else if (st.kind == Step::Index && st.idx == IdxK::Const) {
      if (cur.kind != CVKind::Array) return CVal::unk();
      if (st.k < 0 || (size_t)st.k >= cur.elems.size()) return CVal::unk();
      cur = cur.elems[(size_t)st.k];
    } else return CVal::unk();
  }
  return cur;
}

struct Access {
  std::string file;
  SourceLoc loc;
  Path path;
  bool rd = false;
  bool wr = false;
  CVal wval;
  int stmt_id = 0;
  std::string callee;
  bool through_call = false;
  bool possible_pref = false; // set later from overlap
};

struct Effect {
  bool rd = false, wr = false;
  Path path;
  CVal wval;
};

struct Summary {
  std::vector<Effect> effects;
  bool file_io = false;
  bool seeded = false;
};

struct Bind {
  TypePtr type;
  bool is_const = false;
  bool reassigned = false;
  bool task_local = false;
  Origin origin;
  Path vpath;
};

struct LoopIv {
  std::string name;
  int64_t lo = 0, hi = 0;
  bool valid = false;
};

static bool is_scene_sym(const std::string& n) {
  return n == "farm_Scene" || n == "farm_Object3D" ||
         n == "farm_PerspectiveCamera" || n == "farm_Mesh" ||
         n == "farm_BoxGeometry" || n == "farm_SphereGeometry" ||
         n == "farm_PlaneGeometry" || n == "farm_MeshBasicMaterial" ||
         n == "farm_MeshStandardMaterial" || n == "farm_AmbientLight" ||
         n == "farm_DirectionalLight" || n == "farm_PointLight" ||
         n == "farm_RectAreaLight" || n == "farm_Texture" ||
         n == "farm_Renderer";
}

static bool is_physics_sym(const std::string& n) {
  return n == "farm_World" || n == "farm_RigidBody" ||
         n == "farm_SphereCollider" || n == "farm_BoxCollider" ||
         n == "farm_PlaneCollider";
}

static bool is_stdlib_mod(const std::string& p) {
  return p == "farmos:math" || p == "farmos:scene" || p == "farmos:physics";
}

static bool is_object3d_sym(const std::string& n) {
  return n == "farm_Object3D" || n == "farm_Scene" || n == "farm_PerspectiveCamera" ||
         n == "farm_Mesh" || n == "farm_AmbientLight" || n == "farm_DirectionalLight" ||
         n == "farm_PointLight" || n == "farm_RectAreaLight";
}

static bool is_math_sym(const std::string& n) {
  auto pos = n.rfind('_');
  if (pos == std::string::npos) return false;
  std::string b = n.substr(pos + 1);
  return b == "Vector2" || b == "Vector3" || b == "Vector4" ||
         b == "Matrix3" || b == "Matrix4" || b == "Quaternion" ||
         b == "Color" || b == "Euler" || b == "Ray" ||
         b == "Sphere" || b == "Box3" || b == "RayHit";
}

static std::string math_base(const std::string& n) {
  auto pos = n.rfind('_');
  return pos == std::string::npos ? n : n.substr(pos + 1);
}

static std::string class_display(const std::string& c_sym) {
  if (c_sym.rfind("farm_", 0) == 0) return c_sym.substr(5);
  auto pos = c_sym.rfind('_');
  if (pos != std::string::npos && pos + 1 < c_sym.size()) return c_sym.substr(pos + 1);
  return c_sym;
}

static bool compatible_class(const std::string& a, const std::string& b) {
  if (a == b) return true;
  if (is_object3d_sym(a) && is_object3d_sym(b)) return true;
  return false;
}

static OverlapK idx_overlap(const Step& a, const Step& b) {
  auto ov = [&](IdxK ka, const Step& xa, IdxK kb, const Step& xb) -> OverlapK {
    if (ka == IdxK::Const && kb == IdxK::Const) return xa.k == xb.k ? OverlapK::Definite : OverlapK::Disjoint;
    if (ka == IdxK::Interval && kb == IdxK::Interval) {
      bool dis = xa.hi < xb.lo || xb.hi < xa.lo;
      return dis ? OverlapK::Disjoint : OverlapK::Possible;
    }
    if (ka == IdxK::Interval && kb == IdxK::Const) {
      bool dis = xb.k < xa.lo || xb.k > xa.hi;
      return dis ? OverlapK::Disjoint : OverlapK::Possible;
    }
    if (ka == IdxK::Const && kb == IdxK::Interval) {
      bool dis = xa.k < xb.lo || xa.k > xb.hi;
      return dis ? OverlapK::Disjoint : OverlapK::Possible;
    }
    if (ka == IdxK::Sym && kb == IdxK::Sym) {
      if (xa.sym == xb.sym) return xa.off == xb.off ? OverlapK::Definite : OverlapK::Disjoint;
      return OverlapK::Possible;
    }
    return OverlapK::Possible;
  };
  return ov(a.idx, a, b.idx, b);
}

static std::set<std::string> fresh_fns;

struct Analyzer {
  Program& prog;
  std::map<std::string, Summary> sums;
  std::map<Expr*, int> sites;
  int next_site = 1;
  std::set<int> detached;
  std::set<int> fn_sites_used_as_add_arg;
  std::set<int> fn_sites_passed;
  std::unordered_set<std::string> written_outer; // names written by any task of current block
  Module* cur_mod = nullptr;
  std::string file;
  FunctionDecl* cur_fn = nullptr;
  ClassDecl* cur_class = nullptr;
  StructDecl* cur_struct = nullptr;
  int stmt_counter = 0;
  bool collecting_summary = false;
  std::set<int> private_sites;
  std::unordered_map<std::string, Bind> env; // current bindings
  std::vector<std::unordered_map<std::string, Bind>> env_stack;
  std::vector<LoopIv> loops;
  int task_local_depth = 0; // >0 when walking a task body for the block being analysed
  std::unordered_set<std::string> task_locals;
  std::set<int> task_alloc_sites;

  ClassDecl* find_class_sym(const std::string& c_sym) {
    for (auto& m : prog.modules)
      for (auto& c : m.classes)
        if (c.c_sym == c_sym) return &c;
    return nullptr;
  }
  StructDecl* find_struct_sym(const std::string& c_sym) {
    for (auto& m : prog.modules)
      for (auto& s : m.structs)
        if (s.c_sym == c_sym) return &s;
    return nullptr;
  }
  FunctionDecl* find_fn_sym(const std::string& c_sym) {
    for (auto& m : prog.modules)
      for (auto& f : m.functions)
        if (f.c_sym == c_sym) return &f;
    return nullptr;
  }

  void push_env() { env_stack.push_back(env); }
  void pop_env() { env = env_stack.back(); env_stack.pop_back(); }

  int site_of(ExprPtr e) {
    if (!e) return 0;
    auto it = sites.find(e.get());
    if (it != sites.end()) return it->second;
    int id = next_site++;
    sites[e.get()] = id;
    return id;
  }

  static SourceLoc leftmost(ExprPtr e) {
    if (!e) return {};
    ExprPtr p = e;
    while (p) {
      if (p->kind == ExprKind::Index) p = p->lhs;
      else return p->loc;
    }
    return e->loc;
  }

  static std::string step_str(const Step& st) {
    if (st.kind == Step::Len) return ".#len";
    if (st.kind == Step::Field) return "." + st.field;
    if (st.idx == IdxK::Const) return "[" + std::to_string(st.k) + "]";
    return "[?]";
  }

  std::string path_str(const Path& p) {
    if (!p.display.empty()) return p.display;
    std::string s;
    switch (p.root) {
      case RootK::Var: s = p.var; break;
      case RootK::Graph: s = "#graph"; break;
      case RootK::Tree: s = "Tree(" + class_display(p.origin.cls) + ")"; break;
      case RootK::Site: s = class_display(p.cls); break;
      case RootK::Param:
      case RootK::Unknown: s = class_display(p.cls.empty() ? p.origin.cls : p.cls); break;
    }
    for (auto& st : p.steps) s += step_str(st);
    return s;
  }

  Path var_path(const std::string& name) {
    Path p; p.root = RootK::Var; p.var = name; p.display = name; return p;
  }
  Path origin_path(const Origin& o) {
    Path p;
    if (o.kind == Origin::Site) { p.root = RootK::Site; p.origin = o; p.cls = o.cls; }
    else if (o.kind == Origin::Param) { p.root = RootK::Param; p.origin = o; p.cls = o.cls; }
    else { p.root = RootK::Unknown; p.origin = o; p.cls = o.cls; }
    p.display = class_display(o.cls);
    return p;
  }
  Path with_field(Path p, const std::string& f) {
    Step st; st.kind = Step::Field; st.field = f;
    p.steps.push_back(st);
    if (p.display.empty()) p.display = path_str(p);
    else p.display += "." + f;
    return p;
  }
  Path with_index(Path p, const Step& ix) {
    p.steps.push_back(ix);
    if (p.display.empty()) p.display = path_str(p);
    else p.display += step_str(ix);
    return p;
  }
  Path with_len(Path p) {
    Step st; st.kind = Step::Len; st.idx = IdxK::Len;
    p.steps.push_back(st);
    if (p.display.empty()) p.display = path_str(p);
    else p.display += ".#len";
    return p;
  }

  OverlapK root_overlap(const Path& a, const Path& b) {
    if (a.root == RootK::Graph && b.root == RootK::Graph) return OverlapK::Definite;
    if (a.root == RootK::Graph || b.root == RootK::Graph) return OverlapK::Disjoint;
    if (a.root == RootK::Var && b.root == RootK::Var)
      return a.var == b.var ? OverlapK::Definite : OverlapK::Disjoint;
    if (a.root == RootK::Var || b.root == RootK::Var) return OverlapK::Disjoint;

    auto tree_vs_tree = [&](const Origin& o, const Origin& o2) -> OverlapK {
      if (o == o2) return OverlapK::Definite;
      // Spec 8.3: Disjoint only when both are distinct *detached* Sites.
      if (o.kind == Origin::Site && o2.kind == Origin::Site && o.id != o2.id &&
          detached.count(o.id) && detached.count(o2.id))
        return OverlapK::Disjoint;
      return OverlapK::Possible;
    };
    auto tree_vs_obj = [&](const Origin& t, const Path& obj) -> OverlapK {
      if (obj.origin == t) return OverlapK::Definite;
      // Spec 8.3: Disjoint if the object is a detached Site (022: cameraA, rendererA).
      if (obj.origin.kind == Origin::Site && detached.count(obj.origin.id))
        return OverlapK::Disjoint;
      if (obj.root == RootK::Site && detached.count(obj.origin.id))
        return OverlapK::Disjoint;
      return OverlapK::Possible;
    };

    if (a.root == RootK::Tree && b.root == RootK::Tree) return tree_vs_tree(a.origin, b.origin);
    if (a.root == RootK::Tree) return tree_vs_obj(a.origin, b);
    if (b.root == RootK::Tree) return tree_vs_obj(b.origin, a);

    std::string ac = a.cls.empty() ? a.origin.cls : a.cls;
    std::string bc = b.cls.empty() ? b.origin.cls : b.cls;
    if (!compatible_class(ac, bc)) return OverlapK::Disjoint;

    if (a.root == RootK::Site && b.root == RootK::Site)
      return a.origin.id == b.origin.id ? OverlapK::Definite : OverlapK::Disjoint;
    if (a.root == RootK::Param && b.root == RootK::Param && a.origin.id == b.origin.id)
      return OverlapK::Definite;
    if (a.root == RootK::Site && b.root == RootK::Site) return OverlapK::Disjoint;
    // Param vs Param (different), Param vs Site, any Unknown
    return OverlapK::Possible;
  }

  struct OverlapRes { OverlapK k = OverlapK::Disjoint; Path loc; };
  OverlapRes path_overlap(const Path& a, const Path& b) {
    OverlapRes r;
    OverlapK rk = root_overlap(a, b);
    if (rk == OverlapK::Disjoint) return r;
    size_t n = std::min(a.steps.size(), b.steps.size());
    OverlapK acc = rk;
    for (size_t i = 0; i < n; ++i) {
      const Step& sa = a.steps[i];
      const Step& sb = b.steps[i];
      if (sa.kind == Step::Len && sb.kind == Step::Len) continue;
      if (sa.kind == Step::Field && sb.kind == Step::Field) {
        if (sa.field != sb.field) return r;
        continue;
      }
      if (sa.kind == Step::Index && sb.kind == Step::Index) {
        OverlapK io = idx_overlap(sa, sb);
        if (io == OverlapK::Disjoint) return r;
        if (io == OverlapK::Possible) acc = OverlapK::Possible;
        continue;
      }
      // sibling different kinds (len vs index, field vs index, ...)
      return r;
    }
    r.k = acc;
    r.loc = a.steps.size() >= b.steps.size() ? a : b;
    return r;
  }

  CVal fold(ExprPtr e);

  bool is_const_binding_val(ExprPtr e, CVal* out) {
    if (!e || e->kind != ExprKind::Ident) return false;
    auto it = env.find(e->name);
    if (it != env.end() && it->second.is_const) {
      // look for folded init stored? we store origin only. Fold via... we need const values map
    }
    return false;
  }

  std::unordered_map<std::string, CVal> const_vals; // local + we fill from env

  CVal lookup_const(const std::string& name) {
    auto it = const_vals.find(name);
    if (it != const_vals.end()) return it->second;
    auto eit = env.find(name);
    if (eit != env.end() && eit->second.is_const) {
      auto cit = const_vals.find(name);
      if (cit != const_vals.end()) return cit->second;
    }
    // top-level const
    if (cur_mod) {
      auto it2 = cur_mod->vis_consts.find(name);
      if (it2 != cur_mod->vis_consts.end()) {
        std::string key = it2->second->c_sym;
        auto ct = const_vals.find(key);
        if (ct != const_vals.end()) return ct->second;
        CVal v = fold(it2->second->init);
        const_vals[key] = v;
        const_vals[name] = v;
        return v;
      }
    }
    return CVal::unk();
  }

  bool ident_is_const(const std::string& name) {
    auto it = env.find(name);
    if (it != env.end()) return it->second.is_const;
    if (cur_mod && cur_mod->vis_consts.count(name)) return true;
    return false;
  }

  Step index_form(ExprPtr e) {
    Step st; st.kind = Step::Index; st.idx = IdxK::Any;
    CVal v = fold(e);
    if (v.kind == CVKind::Int) { st.idx = IdxK::Const; st.k = v.i; return st; }
    // Interval: loop var or loop var +/- const
    auto as_loop = [&](ExprPtr x, int64_t* off) -> LoopIv* {
      if (!x) return nullptr;
      if (x->kind == ExprKind::Ident) {
        for (auto it = loops.rbegin(); it != loops.rend(); ++it)
          if (it->valid && it->name == x->name) { *off = 0; return &*it; }
      }
      if (x->kind == ExprKind::Binary && (x->op == TokKind::Plus || x->op == TokKind::Minus)) {
        if (x->lhs && x->lhs->kind == ExprKind::Ident) {
          CVal c = fold(x->rhs);
          if (c.kind == CVKind::Int) {
            for (auto it = loops.rbegin(); it != loops.rend(); ++it)
              if (it->valid && it->name == x->lhs->name) {
                *off = (x->op == TokKind::Plus) ? c.i : -c.i;
                return &*it;
              }
          }
        }
        if (x->op == TokKind::Plus && x->rhs && x->rhs->kind == ExprKind::Ident) {
          CVal c = fold(x->lhs);
          if (c.kind == CVKind::Int) {
            for (auto it = loops.rbegin(); it != loops.rend(); ++it)
              if (it->valid && it->name == x->rhs->name) { *off = c.i; return &*it; }
          }
        }
      }
      return nullptr;
    };
    int64_t off = 0;
    if (LoopIv* lv = as_loop(e, &off)) {
      st.idx = IdxK::Interval;
      st.lo = lv->lo + off;
      st.hi = lv->hi + off;
      return st;
    }
    // Sym: outer ident not written by any task, +/- const
    auto as_sym = [&](ExprPtr x, std::string* name, int64_t* o) -> bool {
      if (!x) return false;
      if (x->kind == ExprKind::Ident) {
        auto it = env.find(x->name);
        if (it != env.end() && !it->second.task_local && !written_outer.count(x->name) && !it->second.is_const) {
          *name = x->name; *o = 0; return true;
        }
      }
      if (x->kind == ExprKind::Binary && (x->op == TokKind::Plus || x->op == TokKind::Minus)) {
        if (x->lhs && x->lhs->kind == ExprKind::Ident) {
          CVal c = fold(x->rhs);
          if (c.kind == CVKind::Int) {
            auto it = env.find(x->lhs->name);
            if (it != env.end() && !it->second.task_local && !written_outer.count(x->lhs->name) && !it->second.is_const) {
              *name = x->lhs->name;
              *o = (x->op == TokKind::Plus) ? c.i : -c.i;
              return true;
            }
          }
        }
        if (x->op == TokKind::Plus && x->rhs && x->rhs->kind == ExprKind::Ident) {
          CVal c = fold(x->lhs);
          if (c.kind == CVKind::Int) {
            auto it = env.find(x->rhs->name);
            if (it != env.end() && !it->second.task_local && !written_outer.count(x->rhs->name) && !it->second.is_const) {
              *name = x->rhs->name; *o = c.i; return true;
            }
          }
        }
      }
      return false;
    };
    std::string sn; int64_t so = 0;
    if (as_sym(e, &sn, &so)) {
      st.idx = IdxK::Sym; st.sym = sn; st.off = so; return st;
    }
    return st;
  }

  bool skip_private_path(const Path& p) {
    if (p.root == RootK::Var) {
      auto it = env.find(p.var);
      if (it != env.end() && it->second.task_local) return true;
      if (task_locals.count(p.var)) return true;
    }
    if (p.root == RootK::Site && (private_sites.count(p.origin.id) || task_alloc_sites.count(p.origin.id)))
      return true;
    return false;
  }

  void publish_escaping_expr(ExprPtr e, bool dst_private) {
    if (!e || dst_private || task_local_depth <= 0) return;
    std::function<void(ExprPtr)> walk = [&](ExprPtr x) {
      if (!x) return;
      if (x->type && x->type->kind == TypeKind::Class) {
        Origin o = expr_origin(x);
        if (o.kind == Origin::Site) {
          task_alloc_sites.erase(o.id);
          private_sites.erase(o.id);
        }
      }
      walk(x->lhs);
      walk(x->rhs);
      for (auto& a : x->args) walk(a);
      for (auto& f : x->fields) walk(f.second);
    };
    walk(e);
  }

  void emit_acc(std::vector<Access>& out, SourceLoc loc, Path p, bool rd, bool wr, CVal wv,
                int sid, const std::string& callee = "", bool through = false) {
    if (skip_private_path(p)) return;
    Access a;
    a.file = file;
    a.loc = loc;
    a.path = std::move(p);
    a.rd = rd; a.wr = wr; a.wval = wr ? wv : CVal::unk();
    a.stmt_id = sid;
    a.callee = callee;
    a.through_call = through;
    out.push_back(std::move(a));
  }

  Origin expr_origin(ExprPtr e);
  Path lvalue_path(ExprPtr e, std::vector<Access>& acc, int sid, bool as_write, CVal wv);
  void collect_expr(ExprPtr e, std::vector<Access>& acc, int sid);
  void collect_stmt(StmtPtr s, std::vector<Access>& acc);
  void instantiate_call(ExprPtr call, std::vector<Access>& acc, int sid);
};

CVal Analyzer::fold(ExprPtr e) {
  if (!e) return CVal::unk();
  switch (e->kind) {
    case ExprKind::IntLit: { CVal v; v.kind = CVKind::Int; v.i = e->int_val; return v; }
    case ExprKind::FloatLit: { CVal v; v.kind = CVKind::Float; v.f = e->float_val; return v; }
    case ExprKind::BoolLit: { CVal v; v.kind = CVKind::Bool; v.b = e->bool_val; return v; }
    case ExprKind::StringLit: { CVal v; v.kind = CVKind::String; v.s = e->str_val; return v; }
    case ExprKind::Ident: {
      if (ident_is_const(e->name)) return lookup_const(e->name);
      return CVal::unk();
    }
    case ExprKind::Unary: {
      CVal r = fold(e->rhs);
      if (!r.known()) return CVal::unk();
      if (e->op == TokKind::Bang && r.kind == CVKind::Bool) { CVal v; v.kind = CVKind::Bool; v.b = !r.b; return v; }
      if (e->op == TokKind::Plus) return r;
      if (e->op == TokKind::Minus) {
        if (r.kind == CVKind::Int) { CVal v; v.kind = CVKind::Int; v.i = (int64_t)(0ull - (uint64_t)r.i); return v; }
        if (r.kind == CVKind::Float) { CVal v; v.kind = CVKind::Float; v.f = -r.f; return v; }
      }
      return CVal::unk();
    }
    case ExprKind::Binary: {
      CVal L = fold(e->lhs), R = fold(e->rhs);
      if (!L.known() || !R.known()) return CVal::unk();
      auto bin_i = [&](auto fn) { CVal v; v.kind = CVKind::Int; v.i = fn(L.i, R.i); return v; };
      auto bin_f = [&](auto fn) { CVal v; v.kind = CVKind::Float; v.f = fn(L.f, R.f); return v; };
      auto bin_b = [&](bool b) { CVal v; v.kind = CVKind::Bool; v.b = b; return v; };
      if (e->op == TokKind::Plus && L.kind == CVKind::String && R.kind == CVKind::String) {
        CVal v; v.kind = CVKind::String; v.s = L.s + R.s; return v;
      }
      if (L.kind == CVKind::Int && R.kind == CVKind::Int) {
        switch (e->op) {
          case TokKind::Plus: return bin_i([](int64_t a, int64_t b) { return (int64_t)((uint64_t)a + (uint64_t)b); });
          case TokKind::Minus: return bin_i([](int64_t a, int64_t b) { return (int64_t)((uint64_t)a - (uint64_t)b); });
          case TokKind::Star: return bin_i([](int64_t a, int64_t b) { return (int64_t)((uint64_t)a * (uint64_t)b); });
          case TokKind::Slash:
            if (R.i == 0) return CVal::unk();
            return bin_i([](int64_t a, int64_t b) { return a / b; });
          case TokKind::Percent:
            if (R.i == 0) return CVal::unk();
            return bin_i([](int64_t a, int64_t b) { return a % b; });
          case TokKind::EqEq: return bin_b(L.i == R.i);
          case TokKind::Neq: return bin_b(L.i != R.i);
          case TokKind::Lt: return bin_b(L.i < R.i);
          case TokKind::Le: return bin_b(L.i <= R.i);
          case TokKind::Gt: return bin_b(L.i > R.i);
          case TokKind::Ge: return bin_b(L.i >= R.i);
          default: break;
        }
      }
      if (L.kind == CVKind::Float && R.kind == CVKind::Float) {
        switch (e->op) {
          case TokKind::Plus: return bin_f([](double a, double b) { return a + b; });
          case TokKind::Minus: return bin_f([](double a, double b) { return a - b; });
          case TokKind::Star: return bin_f([](double a, double b) { return a * b; });
          case TokKind::Slash: return bin_f([](double a, double b) { return a / b; });
          case TokKind::EqEq: return bin_b(fbits(L.f) == fbits(R.f));
          case TokKind::Neq: return bin_b(fbits(L.f) != fbits(R.f));
          case TokKind::Lt: return bin_b(L.f < R.f);
          case TokKind::Le: return bin_b(L.f <= R.f);
          case TokKind::Gt: return bin_b(L.f > R.f);
          case TokKind::Ge: return bin_b(L.f >= R.f);
          default: break;
        }
      }
      if (L.kind == CVKind::Bool && R.kind == CVKind::Bool) {
        if (e->op == TokKind::AndAnd) return bin_b(L.b && R.b);
        if (e->op == TokKind::OrOr) return bin_b(L.b || R.b);
        if (e->op == TokKind::EqEq) return bin_b(L.b == R.b);
        if (e->op == TokKind::Neq) return bin_b(L.b != R.b);
      }
      if (L.kind == CVKind::String && R.kind == CVKind::String) {
        if (e->op == TokKind::EqEq) return bin_b(L.s == R.s);
        if (e->op == TokKind::Neq) return bin_b(L.s != R.s);
      }
      return CVal::unk();
    }
    case ExprKind::StructLit: {
      CVal v; v.kind = CVKind::Struct;
      for (auto& f : e->fields) {
        CVal fv = fold(f.second);
        if (!fv.known()) return CVal::unk();
        v.fields.push_back({f.first, fv});
      }
      return v;
    }
    case ExprKind::New: {
      // struct new with constant args
      if (e->type && e->type->kind == TypeKind::Struct) {
        auto* sd = find_struct_sym(e->type->name);
        if (!sd) return CVal::unk();
        CVal v; v.kind = CVKind::Struct;
        for (size_t i = 0; i < sd->fields.size(); ++i) {
          CVal fv = i < e->args.size() ? fold(e->args[i]) : CVal::unk();
          if (!fv.known()) return CVal::unk();
          v.fields.push_back({sd->fields[i].name, fv});
        }
        return v;
      }
      return CVal::unk();
    }
    case ExprKind::ArrayLit: {
      CVal v; v.kind = CVKind::Array;
      for (auto& a : e->args) {
        CVal ev = fold(a);
        if (!ev.known()) return CVal::unk();
        v.elems.push_back(ev);
      }
      return v;
    }
    default: return CVal::unk();
  }
}

Origin Analyzer::expr_origin(ExprPtr e) {
  Origin o; o.kind = Origin::Unknown;
  if (!e || !e->type || e->type->kind != TypeKind::Class) return o;
  o.cls = e->type->name;
  if (e->kind == ExprKind::New) {
    o.kind = Origin::Site; o.id = site_of(e); return o;
  }
  if (e->kind == ExprKind::Ident) {
    auto it = env.find(e->name);
    if (it != env.end()) return it->second.origin;
    o.kind = Origin::Unknown; return o;
  }
  if (e->kind == ExprKind::This) {
    auto it = env.find("this");
    if (it != env.end()) return it->second.origin;
    o.kind = Origin::Param; o.id = 0; o.cls = e->type->name; return o;
  }
  if (e->kind == ExprKind::Call && fresh_fns.count(e->mangled)) {
    o.kind = Origin::Site;
    o.id = site_of(e);
    return o;
  }
  o.kind = Origin::Unknown;
  return o;
}

static bool stmt_returns_only_fresh(StmtPtr s, const std::function<bool(ExprPtr)>& is_fresh_expr) {
  if (!s) return true;
  if (s->kind == StmtKind::Return) {
    if (!s->ret) return false;
    return is_fresh_expr(s->ret);
  }
  bool ok = true;
  if (s->then_b) ok = ok && stmt_returns_only_fresh(s->then_b, is_fresh_expr);
  if (s->else_b) ok = ok && stmt_returns_only_fresh(s->else_b, is_fresh_expr);
  for (auto& x : s->stmts) ok = ok && stmt_returns_only_fresh(x, is_fresh_expr);
  return ok;
}

} // namespace

// ---- rest of analyzer methods in same namespace farm ----
namespace {

bool is_mutating_math(const std::string& name) {
  static const std::set<std::string> m = {
    "set","copy","add","sub","multiplyScalar","divideScalar","multiply","divide",
    "normalize","lerp","applyMatrix3","applyMatrix4","applyQuaternion",
    "setFromEuler","setFromAxisAngle","setFromRotationMatrix","invert","transpose",
    "compose","makeTranslation","makeScale","makeRotation","makeRotationX","makeRotationY",
    "makeRotationZ","setHex","setRGB","expandByPoint","setFromPoints","cross","crossVectors",
    "clamp","clampScalar","clampLength","floor","ceil","round","negate","setLength",
    "multiplyVectors","addScaledVector","addVectors","subVectors","min","max",
    "setFromMatrixPosition","setFromMatrixScale","setFromMatrixColumn","extractRotation",
    "copyPosition","setFromUnitVectors","applyEuler","lookAt","makeEmpty","union_","intersect",
    "union","setFromSpherical","setFromCylindrical","random","setFromQuaternion",
    "premultiply","makeRotationFromEuler","makeRotationFromQuaternion"
  };
  return m.count(name) > 0;
}

Path Analyzer_append_sync_fields(Analyzer& A, Path base, const std::string& field) {
  (void)A;
  return A.with_field(base, field);
}

void Analyzer::collect_expr(ExprPtr e, std::vector<Access>& acc, int sid) {
  if (!e) return;
  switch (e->kind) {
    case ExprKind::Ident: {
      if (!e->mangled.empty()) break; // const/fn
      auto it = env.find(e->name);
      if (it == env.end()) break;
      // DynArray (and other by-ref) parameters use a Param vpath so summary
      // effects survive effects_to_summary and instantiate at the caller.
      Path p = (it->second.vpath.root == RootK::Param) ? it->second.vpath : var_path(e->name);
      emit_acc(acc, e->loc, p, true, false, {}, sid);
      break;
    }
    case ExprKind::This: {
      auto it = env.find("this");
      if (it != env.end()) emit_acc(acc, e->loc, it->second.vpath, true, false, {}, sid);
      break;
    }
    case ExprKind::Unary:
      collect_expr(e->rhs, acc, sid); break;
    case ExprKind::Binary:
      collect_expr(e->lhs, acc, sid); collect_expr(e->rhs, acc, sid); break;
    case ExprKind::Index: {
      collect_expr(e->rhs, acc, sid); // index
      // index variable read is part of e->rhs
      Path base;
      if (e->lhs->kind == ExprKind::Ident) {
        auto it = env.find(e->lhs->name);
        if (it != env.end()) base = it->second.vpath;
      } else {
        collect_expr(e->lhs, acc, sid);
        base = lvalue_path(e->lhs, acc, sid, false, {});
      }
      if (e->lhs->type && e->lhs->type->kind == TypeKind::DynArray)
        emit_acc(acc, leftmost(e->lhs), with_len(base), true, false, {}, sid);
      Step ix = index_form(e->rhs);
      emit_acc(acc, leftmost(e), with_index(base, ix), true, false, {}, sid);
      break;
    }
    case ExprKind::Field: {
      // Do not emit a whole-object read for `this`/`ident` bases of a field
      // access (`this.a` must not overlap `this.b`).
      if (e->lhs && e->lhs->kind != ExprKind::Ident && e->lhs->kind != ExprKind::This)
        collect_expr(e->lhs, acc, sid);
      if (e->lhs->type && e->lhs->type->kind == TypeKind::Class) {
        Origin o = expr_origin(e->lhs);
        Path p = origin_path(o);
        p.display = "";
        // rebuild display from source: leftmost of lhs + .field
        // use lhs ident name if possible
        if (e->lhs->kind == ExprKind::Ident) p.display = e->lhs->name;
        else if (e->lhs->kind == ExprKind::This) p.display = "this";
        else p.display = path_str(p);
        p = with_field(p, e->name);
        emit_acc(acc, leftmost(e), p, true, false, {}, sid);
      } else if (e->lhs->type && e->lhs->type->kind == TypeKind::Struct) {
        Path base = lvalue_path(e->lhs, acc, sid, false, {});
        emit_acc(acc, leftmost(e), with_field(base, e->name), true, false, {}, sid);
      }
      break;
    }
    case ExprKind::Call:
      instantiate_call(e, acc, sid);
      break;
    case ExprKind::New:
      for (auto& a : e->args) collect_expr(a, acc, sid);
      if (e->type && e->type->kind == TypeKind::Class) {
        int id = site_of(e);
        if (task_local_depth > 0) task_alloc_sites.insert(id);
      }
      break;
    case ExprKind::ArrayLit:
      for (auto& a : e->args) collect_expr(a, acc, sid);
      if (e->type && e->type->kind == TypeKind::DynArray) {
        int id = site_of(e);
        if (task_local_depth > 0) task_alloc_sites.insert(id);
      }
      break;
    case ExprKind::StructLit:
      for (auto& f : e->fields) collect_expr(f.second, acc, sid);
      break;
    default: break;
  }
}

Path Analyzer::lvalue_path(ExprPtr e, std::vector<Access>& acc, int sid, bool as_write, CVal wv) {
  if (!e) return {};
  if (e->kind == ExprKind::Ident) {
    auto it = env.find(e->name);
    if (it == env.end()) { Path p = var_path(e->name); return p; }
    return it->second.vpath;
  }
  if (e->kind == ExprKind::This) {
    auto it = env.find("this");
    if (it != env.end()) return it->second.vpath;
    Path p = origin_path(Origin{Origin::Param, 0, e->type ? e->type->name : ""});
    p.display = "this";
    return p;
  }
  if (e->kind == ExprKind::Field) {
    Path base = lvalue_path(e->lhs, acc, sid, false, {});
    if (e->lhs->type && e->lhs->type->kind == TypeKind::Class) {
      Origin o = expr_origin(e->lhs);
      base = origin_path(o);
      if (e->lhs->kind == ExprKind::Ident) base.display = e->lhs->name;
      else if (e->lhs->kind == ExprKind::This) base.display = "this";
    }
    Path p = with_field(base, e->name);
    (void)as_write; (void)wv; (void)sid;
    return p;
  }
  if (e->kind == ExprKind::Index) {
    collect_expr(e->rhs, acc, sid);
    Path base = lvalue_path(e->lhs, acc, sid, false, {});
    if (e->lhs->type && e->lhs->type->kind == TypeKind::DynArray)
      emit_acc(acc, leftmost(e->lhs), with_len(base), true, false, {}, sid);
    Step ix = index_form(e->rhs);
    return with_index(base, ix);
  }
  collect_expr(e, acc, sid);
  return {};
}

static std::string call_callee_name(ExprPtr call) {
  if (!call || call->kind != ExprKind::Call) return "";
  if (call->lhs && call->lhs->kind == ExprKind::Ident) return call->lhs->name;
  if (call->lhs && call->lhs->kind == ExprKind::Field) return call->lhs->name;
  return call->mangled;
}

void Analyzer::instantiate_call(ExprPtr call, std::vector<Access>& acc, int sid) {
  if (!call) return;
  SourceLoc cloc = call->lhs ? leftmost(call->lhs) : call->loc;
  std::string cname = call_callee_name(call);
  // builtins
  if (call->lhs && call->lhs->kind == ExprKind::Ident) {
    std::string n = call->lhs->name;
    if (n == "print" || n == "println") {
      for (auto& a : call->args) collect_expr(a, acc, sid);
      return;
    }
    if (n == "str" || n == "int" || n == "float") {
      for (auto& a : call->args) collect_expr(a, acc, sid);
      return;
    }
    if (n == "len") {
      for (size_t i = 1; i < call->args.size(); ++i) collect_expr(call->args[i], acc, sid);
      if (!call->args.empty() && call->args[0]->type && call->args[0]->type->kind == TypeKind::DynArray) {
        Path p = lvalue_path(call->args[0], acc, sid, false, {});
        emit_acc(acc, cloc, with_len(p), true, false, {}, sid, "len", true);
      }
      return;
    }
    if (n == "push") {
      for (size_t i = 1; i < call->args.size(); ++i) collect_expr(call->args[i], acc, sid);
      if (!call->args.empty()) {
        Path p = lvalue_path(call->args[0], acc, sid, false, {});
        emit_acc(acc, cloc, p, false, true, CVal::unk(), sid, "push", true);
        bool arr_private = skip_private_path(p);
        for (size_t i = 1; i < call->args.size(); ++i)
          publish_escaping_expr(call->args[i], arr_private);
      }
      return;
    }
  }

  for (auto& a : call->args) {
    // Class-typed and dynamic-array Ident/This args are covered by instantiated
    // summary effects; do not emit a whole-object/whole-array read.
    if (a && a->type &&
        (a->type->kind == TypeKind::Class || a->type->kind == TypeKind::DynArray) &&
        (a->kind == ExprKind::Ident || a->kind == ExprKind::This))
      continue;
    collect_expr(a, acc, sid);
  }
  if (call->lhs && call->lhs->kind == ExprKind::Field) {
    ExprPtr recv = call->lhs->lhs;
    if (recv && recv->kind != ExprKind::Ident && recv->kind != ExprKind::This)
      collect_expr(recv, acc, sid);
  }
  // Passing a task-allocated object into a call publishes it unless the
  // receiver of a method call is still task-private.
  {
    bool recv_private = false;
    if (call->lhs && call->lhs->kind == ExprKind::Field && call->lhs->lhs) {
      Path rp = lvalue_path(call->lhs->lhs, acc, sid, false, {});
      recv_private = skip_private_path(rp);
    }
    for (auto& a : call->args) publish_escaping_expr(a, recv_private);
  }

  std::string key = call->mangled;
  auto it = sums.find(key);
  Summary sum;
  if (it != sums.end()) sum = it->second;
  else {
    // math method empty summary fallback
    if (call->lhs && call->lhs->kind == ExprKind::Field &&
        call->lhs->lhs && call->lhs->lhs->type &&
        call->lhs->lhs->type->kind == TypeKind::Struct &&
        is_math_sym(call->lhs->lhs->type->name)) {
      std::string mn = call->lhs->name;
      if (is_mutating_math(mn) || (call->type && call->type->kind == TypeKind::Void) ||
          (call->type && call->type->kind == TypeKind::Struct &&
           call->type->name == call->lhs->lhs->type->name)) {
        Path recv = lvalue_path(call->lhs->lhs, acc, sid, false, {});
        emit_acc(acc, cloc, recv, false, true, CVal::unk(), sid, mn, true);
        return;
      } else {
        Path recv = lvalue_path(call->lhs->lhs, acc, sid, false, {});
        emit_acc(acc, cloc, recv, true, false, {}, sid, mn, true);
        return;
      }
    }
  }

  if (sum.file_io && task_local_depth > 0) {
    error_at(file, cloc, "E0805",
             "`" + cname + "` performs file output and cannot be called inside a task");
  }

  auto instantiate_path = [&](const Path& fp) -> Path {
    if (fp.root == RootK::Graph) return fp;
    if (fp.root == RootK::Tree) {
      Path out = fp;
      if (fp.origin.kind == Origin::Param) {
        int k = fp.origin.id;
        ExprPtr arg = nullptr;
        if (k == 0 && call->lhs && call->lhs->kind == ExprKind::Field) arg = call->lhs->lhs;
        else if (k > 0 && (size_t)(k - (call->lhs && call->lhs->kind == ExprKind::Field ? 0 : 0)) <= call->args.size()) {
          // Param numbering: methods this=0, args start at 1; functions args start at 0
        }
        bool is_method = call->lhs && call->lhs->kind == ExprKind::Field;
        if (is_method) {
          if (k == 0) arg = call->lhs->lhs;
          else if (k >= 1 && (size_t)(k - 1) < call->args.size()) arg = call->args[(size_t)(k - 1)];
        } else {
          if (k >= 0 && (size_t)k < call->args.size()) arg = call->args[(size_t)k];
        }
        if (arg) {
          Origin o = expr_origin(arg);
          out.origin = o;
          if (!o.cls.empty()) out.cls = o.cls;
        }
      }
      return out;
    }
    if (fp.root == RootK::Param || (fp.root == RootK::Unknown && fp.origin.kind == Origin::Param) ||
        fp.root == RootK::Site) {
      // handled below for Param
    }
    if (fp.root == RootK::Param || fp.origin.kind == Origin::Param) {
      int k = fp.origin.kind == Origin::Param ? fp.origin.id : (fp.root == RootK::Param ? fp.origin.id : -1);
      if (fp.root == RootK::Param) k = fp.origin.id;
      bool is_method = call->lhs && call->lhs->kind == ExprKind::Field;
      ExprPtr arg = nullptr;
      bool struct_this = is_method && call->lhs->lhs && call->lhs->lhs->type &&
                         call->lhs->lhs->type->kind == TypeKind::Struct;
      if (is_method) {
        if (k == 0) arg = call->lhs->lhs;
        else if (k >= 1 && (size_t)(k - 1) < call->args.size()) arg = call->args[(size_t)(k - 1)];
      } else {
        if (k >= 0 && (size_t)k < call->args.size()) arg = call->args[(size_t)k];
      }
      if (!arg) return fp;
      Path base;
      if (struct_this && k == 0) {
        base = lvalue_path(arg, acc, sid, false, {});
      } else if (arg->type && arg->type->kind == TypeKind::Class) {
        Origin o = expr_origin(arg);
        base = origin_path(o);
        if (arg->kind == ExprKind::Ident) base.display = arg->name;
        else if (arg->kind == ExprKind::This) base.display = "this";
      } else if (arg->type && arg->type->kind == TypeKind::DynArray) {
        base = lvalue_path(arg, acc, sid, false, {});
      } else {
        // by-value struct/primitive: callee-private unless struct this
        Path skip; skip.root = RootK::Var; skip.var = "#private"; skip.display = "#private";
        // mark as task-local-like by using a fake private var
        skip.var = "";
        return skip;
      }
      for (auto& st : fp.steps) {
        if (st.kind == Step::Field) base = with_field(base, st.field);
        else if (st.kind == Step::Len) base = with_len(base);
        else base = with_index(base, st);
      }
      return base;
    }
    if (fp.root == RootK::Unknown) return fp;
    if (fp.root == RootK::Var) return fp;
    return fp;
  };

  for (auto& ef : sum.effects) {
    Path ip = instantiate_path(ef.path);
    if (ip.var == "#private" || (ip.root == RootK::Var && ip.var.empty() && ip.display == "#private"))
      continue;
    emit_acc(acc, cloc, ip, ef.rd, ef.wr, ef.wval, sid, cname, true);
  }

  // rotation/quaternion sync already in summaries for scene field writes; method table includes them.
}

static bool name_assigned_in(StmtPtr s, const std::string& n) {
  if (!s) return false;
  if (s->kind == StmtKind::Assign && s->lhs && s->lhs->kind == ExprKind::Ident && s->lhs->name == n)
    return true;
  if (s->kind == StmtKind::Let && s->name == n) return false;
  if (name_assigned_in(s->then_b, n) || name_assigned_in(s->else_b, n) ||
      name_assigned_in(s->for_init, n) || name_assigned_in(s->for_update, n))
    return true;
  for (auto& x : s->stmts) if (name_assigned_in(x, n)) return true;
  return false;
}

void Analyzer::collect_stmt(StmtPtr s, std::vector<Access>& acc) {
  if (!s) return;
  int sid = ++stmt_counter;
  switch (s->kind) {
    case StmtKind::Block:
      push_env();
      for (auto& x : s->stmts) collect_stmt(x, acc);
      pop_env();
      break;
    case StmtKind::Let: case StmtKind::Const: {
      collect_expr(s->init, acc, sid);
      Bind b;
      TypePtr ty = s->decl_type;
      if ((!ty || ty->kind == TypeKind::Error) && s->init) ty = s->init->type;
      b.type = ty;
      b.is_const = s->kind == StmtKind::Const;
      b.task_local = task_local_depth > 0;
      b.vpath = var_path(s->name);
      if (ty && ty->kind == TypeKind::Class) {
        b.origin = expr_origin(s->init);
        if (s->init && s->init->kind == ExprKind::Ident) {
          auto it = env.find(s->init->name);
          if (it != env.end()) b.origin = it->second.origin;
        }
        // Only *this task's* allocations are private. Aliasing an outer/shared
        // Site (let q = p) must not hide subsequent field writes.
        bool fresh = s->init && (s->init->kind == ExprKind::New ||
                                 (s->init->kind == ExprKind::Call && fresh_fns.count(s->init->mangled)));
        if (fresh && b.origin.kind == Origin::Site && task_local_depth > 0)
          task_alloc_sites.insert(b.origin.id);
      } else if (ty && ty->kind == TypeKind::DynArray) {
        if (s->init && s->init->kind == ExprKind::Ident) {
          auto it = env.find(s->init->name);
          if (it != env.end()) b.vpath = it->second.vpath;
        }
      }
      if (b.task_local) task_locals.insert(s->name);
      if (b.is_const) {
        CVal cv = fold(s->init);
        if (cv.known()) const_vals[s->name] = cv;
      }
      env[s->name] = b;
      break;
    }
    case StmtKind::Assign: {
      collect_expr(s->rhs, acc, sid);
      CVal wv = CVal::unk();
      bool compound = s->assign_op != TokKind::Assign;
      if (!compound) wv = fold(s->rhs);
      Path lp = lvalue_path(s->lhs, acc, sid, true, wv);
      SourceLoc aloc = leftmost(s->lhs);
      if (compound) {
        emit_acc(acc, aloc, lp, true, true, CVal::unk(), sid);
      } else {
        emit_acc(acc, aloc, lp, false, true, wv, sid);
      }
      // rotation/quaternion sync
      auto add_sync = [&](const Path& base, const std::string& other) {
        emit_acc(acc, aloc, with_field(base, other), false, true, CVal::unk(), sid);
      };
      if (!lp.steps.empty() && lp.steps.back().kind == Step::Field) {
        std::string last = lp.steps.back().field;
        if (last == "rotation" || last == "x" || last == "y" || last == "z" || last == "order") {
          // if path contains rotation
          for (size_t i = 0; i < lp.steps.size(); ++i) {
            if (lp.steps[i].kind == Step::Field && lp.steps[i].field == "rotation") {
              Path q = lp;
              q.steps.resize(i);
              add_sync(q, "quaternion");
              break;
            }
          }
        }
        if (last == "quaternion" || last == "w") {
          for (size_t i = 0; i < lp.steps.size(); ++i) {
            if (lp.steps[i].kind == Step::Field && lp.steps[i].field == "quaternion") {
              Path q = lp;
              q.steps.resize(i);
              add_sync(q, "rotation");
              break;
            }
          }
        }
      }
      // reassignment of let class binding -> Unknown origin
      if (s->lhs && s->lhs->kind == ExprKind::Ident) {
        auto it = env.find(s->lhs->name);
        if (it != env.end() && !it->second.is_const) {
          it->second.reassigned = true;
          if (it->second.type && it->second.type->kind == TypeKind::Class) {
            it->second.origin.kind = Origin::Unknown;
            it->second.origin.cls = it->second.type->name;
          }
        }
      }
      // §4.8: storing a task-allocated object into a shared location publishes it.
      publish_escaping_expr(s->rhs, skip_private_path(lp));
      break;
    }
    case StmtKind::Expr:
      collect_expr(s->init, acc, sid);
      break;
    case StmtKind::If:
      collect_expr(s->cond, acc, sid);
      collect_stmt(s->then_b, acc);
      collect_stmt(s->else_b, acc);
      break;
    case StmtKind::While:
      collect_expr(s->cond, acc, sid);
      collect_stmt(s->then_b, acc);
      break;
    case StmtKind::For: {
      push_env();
      LoopIv iv; iv.valid = false;
      if (s->for_init) collect_stmt(s->for_init, acc);
      // counted loop?
      if (s->for_init && s->for_init->kind == StmtKind::Let &&
          s->for_cond && s->for_cond->kind == ExprKind::Binary &&
          s->for_update && s->for_update->kind == StmtKind::Assign) {
        std::string iname = s->for_init->name;
        CVal L = fold(s->for_init->init);
        bool lt = s->for_cond->op == TokKind::Lt || s->for_cond->op == TokKind::Le;
        if (L.kind == CVKind::Int && lt && s->for_cond->lhs && s->for_cond->lhs->kind == ExprKind::Ident &&
            s->for_cond->lhs->name == iname) {
          CVal H = fold(s->for_cond->rhs);
          bool plus1 = s->for_update->assign_op == TokKind::PlusEq &&
                       s->for_update->lhs && s->for_update->lhs->kind == ExprKind::Ident &&
                       s->for_update->lhs->name == iname;
          if (!plus1 && s->for_update->assign_op == TokKind::Assign &&
              s->for_update->lhs && s->for_update->lhs->kind == ExprKind::Ident &&
              s->for_update->lhs->name == iname &&
              s->for_update->rhs && s->for_update->rhs->kind == ExprKind::Binary &&
              s->for_update->rhs->op == TokKind::Plus &&
              s->for_update->rhs->lhs && s->for_update->rhs->lhs->kind == ExprKind::Ident &&
              s->for_update->rhs->lhs->name == iname) {
            CVal one = fold(s->for_update->rhs->rhs);
            plus1 = one.kind == CVKind::Int && one.i == 1;
          }
          CVal onee;
          if (s->for_update->assign_op == TokKind::PlusEq) onee = fold(s->for_update->rhs);
          else onee.kind = CVKind::Int, onee.i = 1;
          if (H.kind == CVKind::Int && plus1 && onee.kind == CVKind::Int && onee.i == 1 &&
              !name_assigned_in(s->then_b, iname)) {
            iv.valid = true; iv.name = iname; iv.lo = L.i;
            iv.hi = (s->for_cond->op == TokKind::Lt) ? (H.i - 1) : H.i;
            if (iv.lo > iv.hi) { iv.lo = 0; iv.hi = -1; }
          }
        }
      }
      loops.push_back(iv);
      collect_expr(s->for_cond, acc, sid);
      collect_stmt(s->then_b, acc);
      collect_stmt(s->for_update, acc);
      loops.pop_back();
      pop_env();
      break;
    }
    case StmtKind::Return:
      collect_expr(s->ret, acc, sid);
      break;
    case StmtKind::Parallel:
      // nested: analyse inner first (done by caller), then include inner shared accesses
      for (auto& t : s->stmts) {
        // collect inner task body as part of this (enclosing) task — not private to inner
        collect_stmt(t->then_b, acc);
      }
      break;
    case StmtKind::Task:
      collect_stmt(s->then_b, acc);
      break;
    default: break;
  }
}

static int conflict_class(const Access& x, const Access& y, OverlapK ov, Path* loc) {
  (void)loc;
  if (ov == OverlapK::Disjoint) return 0;
  bool wrwr = x.wr && y.wr;
  bool rw = (x.rd && y.wr) || (x.wr && y.rd);
  if (wrwr) {
    // §5.6: when write paths have different depth, compare values restricted
    // to the overlap location (project the shorter/whole write onto extra steps).
    CVal vx = x.wval, vy = y.wval;
    size_t nx = x.path.steps.size(), ny = y.path.steps.size();
    if (nx < ny) {
      std::vector<Step> extra(y.path.steps.begin() + (std::ptrdiff_t)nx, y.path.steps.end());
      vx = project(vx, extra);
    } else if (ny < nx) {
      std::vector<Step> extra(x.path.steps.begin() + (std::ptrdiff_t)ny, x.path.steps.end());
      vy = project(vy, extra);
    }
    bool same = vx.known() && vy.known() && cval_eq(vx, vy);
    if (same) return 2; // W0801
    return 4; // E0801 (outranks E0802 when both apply to the same later-task access)
  }
  if (rw) return 3; // E0802
  return 0;
}

struct Cand {
  int cls = 0;
  Access y, x;
  int i = 0, j = 0;
  Path loc;
  OverlapK ov = OverlapK::Definite;
  SourceLoc yloc, xloc;
  std::string yfile, xfile;
};

void report_block(Analyzer& A, const std::vector<std::vector<Access>>& tasks, const std::string& file) {
  int n = (int)tasks.size();
  std::vector<Cand> cands;
  for (int j = 1; j < n; ++j) {
    for (size_t yi = 0; yi < tasks[j].size(); ++yi) {
      const Access& y = tasks[j][yi];
      int best_i = -1;
      int best_cls = 0;
      size_t best_xi = 0;
      Path best_loc;
      OverlapK best_ov = OverlapK::Definite;
      for (int i = 0; i < j; ++i) {
        for (size_t xi = 0; xi < tasks[i].size(); ++xi) {
          const Access& x = tasks[i][xi];
          auto ov = A.path_overlap(x.path, y.path);
          if (ov.k == OverlapK::Disjoint) continue;
          int cls = conflict_class(x, y, ov.k, &ov.loc);
          if (cls == 0) continue;
          bool better = false;
          if (best_i < 0) better = true;
          else if (i < best_i) better = true;
          else if (i == best_i) {
            if (cls > best_cls) better = true;
            else if (cls == best_cls) {
              const Access& bx = tasks[i][best_xi];
              if (x.loc.line < bx.loc.line || (x.loc.line == bx.loc.line && x.loc.col < bx.loc.col))
                better = true;
            }
          }
          if (better) {
            best_i = i; best_cls = cls; best_xi = xi; best_loc = ov.loc; best_ov = ov.k;
          }
        }
      }
      if (best_i >= 0) {
        Cand c;
        c.cls = best_cls;
        c.y = y; c.x = tasks[best_i][best_xi];
        c.i = best_i + 1; c.j = j + 1;
        c.loc = best_loc; c.ov = best_ov;
        c.yloc = y.loc; c.xloc = c.x.loc;
        c.yfile = y.file; c.xfile = c.x.file;
        cands.push_back(c);
      }
    }
  }
  // suppress: same statement + same overlap loc -> keep highest class, earliest
  std::vector<char> drop(cands.size(), 0);
  for (size_t a = 0; a < cands.size(); ++a) {
    if (drop[a]) continue;
    for (size_t b = a + 1; b < cands.size(); ++b) {
      if (drop[b]) continue;
      if (cands[a].j == cands[b].j && cands[a].y.stmt_id == cands[b].y.stmt_id &&
          A.path_str(cands[a].loc) == A.path_str(cands[b].loc)) {
        // keep higher class, then earlier y loc
        bool keep_a = true;
        if (cands[b].cls > cands[a].cls) keep_a = false;
        else if (cands[b].cls == cands[a].cls) {
          if (cands[b].yloc.line < cands[a].yloc.line ||
              (cands[b].yloc.line == cands[a].yloc.line && cands[b].yloc.col < cands[a].yloc.col))
            keep_a = false;
        }
        if (keep_a) drop[b] = 1;
        else drop[a] = 1;
      }
    }
  }
  // identical source position of y: at most one diag per y loc (union already in accesses)
  for (size_t a = 0; a < cands.size(); ++a) {
    if (drop[a]) continue;
    for (size_t b = a + 1; b < cands.size(); ++b) {
      if (drop[b]) continue;
      if (cands[a].j == cands[b].j && cands[a].yloc.line == cands[b].yloc.line &&
          cands[a].yloc.col == cands[b].yloc.col && cands[a].yfile == cands[b].yfile &&
          A.path_str(cands[a].loc) == A.path_str(cands[b].loc)) {
        bool keep_a = cands[a].cls >= cands[b].cls;
        if (keep_a) drop[b] = 1; else drop[a] = 1;
      }
    }
  }

  for (size_t k = 0; k < cands.size(); ++k) {
    if (drop[k]) continue;
    Cand& c = cands[k];
    std::string loc = A.path_str(c.loc);
    bool poss = c.ov == OverlapK::Possible;
    std::string verb_x = c.x.wr ? "write" : "read";
    std::string verb_y = c.y.wr ? "write" : "read";
    std::string note = verb_x + " of `" + loc + "` by task " + std::to_string(c.i);
    if (c.x.through_call && !c.x.callee.empty())
      note += " through call to `" + c.x.callee + "`";
    note += " is here";
    if (c.cls == 4) {
      std::string msg = poss
        ? ("possibly conflicting writes to `" + loc + "`: task " + std::to_string(c.j) +
           " and task " + std::to_string(c.i) + " may write it with different or unknown values")
        : ("conflicting writes to `" + loc + "`: task " + std::to_string(c.j) +
           " and task " + std::to_string(c.i) + " write it with different or unknown values");
      error_at(c.yfile.empty() ? file : c.yfile, c.yloc, "E0801", msg);
      attach_note(diags().back(), c.xfile.empty() ? file : c.xfile, c.xloc, note);
    } else if (c.cls == 3) {
      std::string vj = c.y.wr ? "writes" : "reads";
      std::string vi = c.x.wr ? "writes" : "reads";
      std::string msg = poss
        ? ("possibly conflicting read and write of `" + loc + "`: task " + std::to_string(c.j) +
           " " + vj + " it while task " + std::to_string(c.i) + " " + vi + " it")
        : ("conflicting read and write of `" + loc + "`: task " + std::to_string(c.j) +
           " " + vj + " it while task " + std::to_string(c.i) + " " + vi + " it");
      error_at(c.yfile.empty() ? file : c.yfile, c.yloc, "E0802", msg);
      attach_note(diags().back(), c.xfile.empty() ? file : c.xfile, c.xloc, note);
    } else if (c.cls == 2) {
      std::string msg = poss
        ? ("tasks " + std::to_string(c.i) + " and " + std::to_string(c.j) + " may write the same value to `" + loc + "`")
        : ("tasks " + std::to_string(c.i) + " and " + std::to_string(c.j) + " write the same value to `" + loc + "`");
      warning_at(c.yfile.empty() ? file : c.yfile, c.yloc, "W0801", msg);
      attach_note(diags().back(), c.xfile.empty() ? file : c.xfile, c.xloc, note);
    }
  }
}

void seed_scene_summaries(Analyzer& A) {
  auto add_eff = [](Summary& s, bool rd, bool wr, Path p, CVal v = {}) {
    Effect e; e.rd = rd; e.wr = wr; e.path = p; e.wval = v; s.effects.push_back(e);
  };
  auto pthis = []() {
    Path p; p.root = RootK::Param; p.origin.kind = Origin::Param; p.origin.id = 0;
    p.origin.cls = "farm_Object3D"; p.cls = "farm_Object3D"; p.display = "this"; return p;
  };
  auto pthis_cls = [](const std::string& c) {
    Path p; p.root = RootK::Param; p.origin.kind = Origin::Param; p.origin.id = 0;
    p.origin.cls = c; p.cls = c; p.display = "this"; return p;
  };
  auto pfield = [&](Path base, const std::string& f) {
    return A.with_field(base, f);
  };
  Path graph; graph.root = RootK::Graph; graph.display = "#graph";

  auto seed_obj3d = [&](const std::string& pref, const std::string& cls) {
    Path th = pthis_cls(cls);
    Summary add; add.seeded = true;
    add_eff(add, true, false, graph);
    add_eff(add, false, true, graph, CVal::unk());
    A.sums[pref + "add"] = add;
    A.sums[pref + "remove"] = add;
    A.sums[pref + "addAt"] = add;
    Summary cc; cc.seeded = true;
    add_eff(cc, true, false, graph);
    A.sums[pref + "childCount"] = cc;
    A.sums[pref + "getChild"] = cc;
    Summary look; look.seeded = true;
    add_eff(look, true, false, pfield(th, "position"));
    add_eff(look, false, true, pfield(th, "rotation"), CVal::unk());
    add_eff(look, false, true, pfield(th, "quaternion"), CVal::unk());
    A.sums[pref + "lookAt"] = look;
    A.sums[pref + "lookAt_xyz"] = look;
    A.sums[pref + "lookAt_v"] = look;
    Summary um; um.seeded = true;
    add_eff(um, true, false, pfield(th, "position"));
    add_eff(um, true, false, pfield(th, "quaternion"));
    add_eff(um, true, false, pfield(th, "scale"));
    add_eff(um, false, true, pfield(th, "matrix"), CVal::unk());
    A.sums[pref + "updateMatrix"] = um;
    Summary umw; umw.seeded = true;
    add_eff(umw, true, false, pfield(th, "position"));
    add_eff(umw, true, false, pfield(th, "quaternion"));
    add_eff(umw, true, false, pfield(th, "scale"));
    add_eff(umw, true, false, pfield(th, "matrixAutoUpdate"));
    add_eff(umw, true, false, graph);
    Path tree; tree.root = RootK::Tree; tree.origin.kind = Origin::Param; tree.origin.id = 0;
    tree.origin.cls = cls; tree.cls = cls;
    add_eff(umw, false, true, A.with_field(tree, "matrix"), CVal::unk());
    add_eff(umw, false, true, A.with_field(tree, "matrixWorld"), CVal::unk());
    A.sums[pref + "updateMatrixWorld"] = umw;
    Summary sre; sre.seeded = true;
    add_eff(sre, false, true, pfield(th, "rotation"), CVal::unk());
    add_eff(sre, false, true, pfield(th, "quaternion"), CVal::unk());
    A.sums[pref + "setRotationFromEuler"] = sre;
    A.sums[pref + "setRotationFromQuaternion"] = sre;
  };
  seed_obj3d("farm_Object3D_", "farm_Object3D");

  Path sth = pthis_cls("farm_Scene");
  Summary sbg; sbg.seeded = true;
  add_eff(sbg, false, true, A.with_field(sth, "hasBackground"), CVal::unk());
  add_eff(sbg, false, true, A.with_field(sth, "background"), CVal::unk());
  A.sums["farm_Scene_setBackground"] = sbg;
  A.sums["farm_Scene_clearBackground"] = sbg;

  Path cth = pthis_cls("farm_PerspectiveCamera");
  Summary upm; upm.seeded = true;
  add_eff(upm, true, false, A.with_field(cth, "fov"));
  add_eff(upm, true, false, A.with_field(cth, "aspect"));
  add_eff(upm, true, false, A.with_field(cth, "near"));
  add_eff(upm, true, false, A.with_field(cth, "far"));
  add_eff(upm, false, true, A.with_field(cth, "projectionMatrix"), CVal::unk());
  A.sums["farm_PerspectiveCamera_updateProjectionMatrix"] = upm;
  A.sums["farm_PerspectiveCamera_updateMatrixWorld"] = A.sums["farm_Object3D_updateMatrixWorld"];

  // Renderer.render(this, scene, camera) — method this=0, scene=1, camera=2
  Summary rend; rend.seeded = true;
  add_eff(rend, true, false, graph);
  Path tscene; tscene.root = RootK::Tree; tscene.origin.kind = Origin::Param; tscene.origin.id = 1;
  tscene.origin.cls = "farm_Scene"; tscene.cls = "farm_Scene";
  add_eff(rend, true, false, tscene);
  add_eff(rend, false, true, A.with_field(tscene, "matrix"), CVal::unk());
  add_eff(rend, false, true, A.with_field(tscene, "matrixWorld"), CVal::unk());
  Path cam; cam.root = RootK::Param; cam.origin.kind = Origin::Param; cam.origin.id = 2;
  cam.origin.cls = "farm_PerspectiveCamera"; cam.cls = "farm_PerspectiveCamera"; cam.display = "camera";
  add_eff(rend, true, false, cam);
  add_eff(rend, false, true, A.with_field(cam, "matrix"), CVal::unk());
  add_eff(rend, false, true, A.with_field(cam, "matrixWorld"), CVal::unk());
  add_eff(rend, false, true, A.with_field(cam, "matrixWorldInverse"), CVal::unk());
  add_eff(rend, false, true, A.with_field(cam, "projectionMatrix"), CVal::unk());
  Path rth = pthis_cls("farm_Renderer");
  add_eff(rend, false, true, A.with_field(rth, "#frame"), CVal::unk());
  A.sums["farm_Renderer_render"] = rend;
  A.sums["farm_Renderer_renderPath"] = rend;

  Summary png; png.seeded = true; png.file_io = true;
  add_eff(png, true, false, A.with_field(rth, "#frame"));
  A.sums["farm_Renderer_savePNG"] = png;

  Summary sz; sz.seeded = true;
  add_eff(sz, false, true, A.with_field(rth, "width"), CVal::unk());
  add_eff(sz, false, true, A.with_field(rth, "height"), CVal::unk());
  add_eff(sz, false, true, A.with_field(rth, "#frame"), CVal::unk());
  A.sums["farm_Renderer_setSize"] = sz;

  Summary acc; acc.seeded = true;
  add_eff(acc, false, true, A.with_field(rth, "#frame"), CVal::unk());
  A.sums["farm_Renderer_resetAccumulation"] = acc;
  Summary ss; ss.seeded = true;
  add_eff(ss, false, true, A.with_field(rth, "samples"), CVal::unk());
  A.sums["farm_Renderer_setSamples"] = ss;
  Summary sb; sb.seeded = true;
  add_eff(sb, false, true, A.with_field(rth, "maxBounces"), CVal::unk());
  A.sums["farm_Renderer_setMaxBounces"] = sb;

  auto disp = [&](const std::string& cls, const std::string& key) {
    Summary d; d.seeded = true;
    Path th = pthis_cls(cls);
    add_eff(d, false, true, A.with_field(th, "#native"), CVal::unk());
    A.sums[key] = d;
  };
  disp("farm_Renderer", "farm_Renderer_dispose");
  disp("farm_BoxGeometry", "farm_BoxGeometry_dispose");
  disp("farm_SphereGeometry", "farm_SphereGeometry_dispose");
  disp("farm_PlaneGeometry", "farm_PlaneGeometry_dispose");
  disp("farm_MeshBasicMaterial", "farm_MeshBasicMaterial_dispose");
  disp("farm_MeshStandardMaterial", "farm_MeshStandardMaterial_dispose");

  auto matset = [&](const std::string& cls, const std::string& meth, const std::string& field) {
    Summary s; s.seeded = true;
    Path th = pthis_cls(cls);
    add_eff(s, false, true, A.with_field(th, field), CVal::unk());
    A.sums[cls + "_" + meth] = s;
  };
  matset("farm_MeshBasicMaterial", "set", "color");
  matset("farm_MeshStandardMaterial", "set", "color");
  matset("farm_MeshStandardMaterial", "setRoughness", "roughness");
  matset("farm_MeshStandardMaterial", "setMetalness", "metalness");
  matset("farm_MeshStandardMaterial", "setTransmission", "transmission");
  matset("farm_MeshStandardMaterial", "setIor", "ior");
  matset("farm_MeshStandardMaterial", "setEmissive", "emissive");
  matset("farm_MeshStandardMaterial", "setEmissive_hex", "emissive");
  matset("farm_MeshStandardMaterial", "setEmissiveIntensity", "emissiveIntensity");
  matset("farm_MeshStandardMaterial", "setMap", "map");

  auto phys_eff = [&](const std::string& key, const std::string& cls) {
    Summary s; s.seeded = true;
    Path th = pthis_cls(cls);
    add_eff(s, false, true, A.with_field(th, "#native"), CVal::unk());
    A.sums[key] = s;
  };
  phys_eff("farm_World_step", "farm_World");
  phys_eff("farm_World_add", "farm_World");
  phys_eff("farm_World_remove", "farm_World");
  phys_eff("farm_World_setGravity", "farm_World");
  phys_eff("farm_World_setFixedTimeStep", "farm_World");
  phys_eff("farm_World_dispose", "farm_World");
  phys_eff("farm_RigidBody_setMass", "farm_RigidBody");
  phys_eff("farm_RigidBody_setRestitution", "farm_RigidBody");
  phys_eff("farm_RigidBody_setFriction", "farm_RigidBody");
  phys_eff("farm_RigidBody_setLinearDamping", "farm_RigidBody");
  phys_eff("farm_RigidBody_setAngularDamping", "farm_RigidBody");
  phys_eff("farm_RigidBody_setCollider", "farm_RigidBody");
  phys_eff("farm_RigidBody_setObject", "farm_RigidBody");
  phys_eff("farm_RigidBody_clearObject", "farm_RigidBody");
  Summary bc; bc.seeded = true;
  add_eff(bc, true, false, pthis_cls("farm_World"));
  A.sums["farm_World_bodyCount"] = bc;
  Summary gbt; gbt.seeded = true;
  add_eff(gbt, true, false, pthis_cls("farm_RigidBody"));
  A.sums["farm_RigidBody_getBodyType"] = gbt;
}

static std::vector<FunctionDecl*> all_fns(Program& p) {
  std::vector<FunctionDecl*> v;
  for (auto& m : p.modules)
    for (auto& f : m.functions) v.push_back(&f);
  return v;
}

void collect_written_names(StmtPtr s, std::unordered_set<std::string>& w) {
  if (!s) return;
  if (s->kind == StmtKind::Assign && s->lhs) {
    ExprPtr e = s->lhs;
    while (e && (e->kind == ExprKind::Field || e->kind == ExprKind::Index)) e = e->lhs;
    if (e && e->kind == ExprKind::Ident) w.insert(e->name);
  }
  if (s->kind == StmtKind::Expr && s->init && s->init->kind == ExprKind::Call &&
      s->init->lhs && s->init->lhs->kind == ExprKind::Ident && s->init->lhs->name == "push") {
    if (!s->init->args.empty() && s->init->args[0]->kind == ExprKind::Ident)
      w.insert(s->init->args[0]->name);
  }
  collect_written_names(s->then_b, w);
  collect_written_names(s->else_b, w);
  collect_written_names(s->for_init, w);
  collect_written_names(s->for_update, w);
  for (auto& x : s->stmts) collect_written_names(x, w);
}

void analyze_parallel(Analyzer& A, StmtPtr par);

void walk_find_parallel(Analyzer& A, StmtPtr s) {
  if (!s) return;
  if (s->kind == StmtKind::Parallel) {
    // inner first; bind task-body lets so nested blocks see outer names
    for (auto& t : s->stmts) {
      A.push_env();
      walk_find_parallel(A, t->then_b);
      A.pop_env();
    }
    analyze_parallel(A, s);
    return;
  }
  if (s->kind == StmtKind::Block) {
    A.push_env();
    for (auto& x : s->stmts) walk_find_parallel(A, x);
    A.pop_env();
    return;
  }
  if (s->kind == StmtKind::Let || s->kind == StmtKind::Const || s->kind == StmtKind::Assign) {
    std::vector<Access> dummy;
    A.collect_stmt(s, dummy);
    return;
  }
  if (s->kind == StmtKind::For) {
    A.push_env();
    walk_find_parallel(A, s->for_init);
    walk_find_parallel(A, s->then_b);
    walk_find_parallel(A, s->for_update);
    A.pop_env();
    return;
  }
  if (s->kind == StmtKind::If || s->kind == StmtKind::While) {
    A.push_env();
    walk_find_parallel(A, s->then_b);
    A.pop_env();
    if (s->kind == StmtKind::If) {
      A.push_env();
      walk_find_parallel(A, s->else_b);
      A.pop_env();
    }
    return;
  }
  walk_find_parallel(A, s->then_b);
  walk_find_parallel(A, s->else_b);
  walk_find_parallel(A, s->for_init);
  walk_find_parallel(A, s->for_update);
  for (auto& x : s->stmts) walk_find_parallel(A, x);
}

void setup_fn_env(Analyzer& A, FunctionDecl& f, ClassDecl* cls, StructDecl* st) {
  A.env.clear();
  A.const_vals.clear();
  A.cur_fn = &f;
  A.cur_class = cls;
  A.cur_struct = st;
  A.task_locals.clear();
  A.task_alloc_sites.clear();
  A.private_sites.clear();
  A.loops.clear();
  if (cls) {
    Bind th;
    th.type = Type::ty_class(cls->c_sym);
    th.origin.kind = Origin::Param; th.origin.id = 0; th.origin.cls = cls->c_sym;
    th.vpath = A.origin_path(th.origin);
    th.vpath.display = "this";
    A.env["this"] = th;
  }
  if (st) {
    Bind th;
    th.type = Type::ty_struct(st->c_sym);
    th.vpath = A.var_path("this");
    th.vpath.display = "this";
    A.env["this"] = th;
  }
  for (size_t i = 0; i < f.params.size(); ++i) {
    Bind b;
    b.type = f.params[i].type;
    int k = cls ? (int)i + 1 : (int)i;
    if (b.type && b.type->kind == TypeKind::DynArray) {
      // §7.1: dynamic-array parameters are by-reference; keep as Param(k).
      b.vpath.root = RootK::Param;
      b.vpath.origin.kind = Origin::Param;
      b.vpath.origin.id = k;
      b.vpath.var = f.params[i].name;
      b.vpath.display = f.params[i].name;
    } else {
      b.vpath = A.var_path(f.params[i].name);
      if (b.type && b.type->kind == TypeKind::Class) {
        b.origin.kind = Origin::Param;
        b.origin.id = k;
        b.origin.cls = b.type->name;
      }
    }
    A.env[f.params[i].name] = b;
  }
}

void setup_method_env(Analyzer& A, ClassDecl& c, MethodDecl& m) {
  A.env.clear(); A.const_vals.clear(); A.cur_class = &c; A.cur_struct = nullptr;
  A.task_locals.clear(); A.task_alloc_sites.clear(); A.loops.clear();
  Bind th;
  th.type = Type::ty_class(c.c_sym);
  th.origin.kind = Origin::Param; th.origin.id = 0; th.origin.cls = c.c_sym;
  th.vpath = A.origin_path(th.origin); th.vpath.display = "this";
  A.env["this"] = th;
  for (size_t i = 0; i < m.params.size(); ++i) {
    Bind b; b.type = m.params[i].type;
    int k = (int)i + 1;
    if (b.type && b.type->kind == TypeKind::DynArray) {
      b.vpath.root = RootK::Param;
      b.vpath.origin.kind = Origin::Param;
      b.vpath.origin.id = k;
      b.vpath.var = m.params[i].name;
      b.vpath.display = m.params[i].name;
    } else {
      b.vpath = A.var_path(m.params[i].name);
      if (b.type && b.type->kind == TypeKind::Class) {
        b.origin.kind = Origin::Param; b.origin.id = k; b.origin.cls = b.type->name;
      }
    }
    A.env[m.params[i].name] = b;
  }
}

void setup_struct_env(Analyzer& A, StructDecl& s, MethodDecl& m) {
  A.env.clear(); A.const_vals.clear(); A.cur_class = nullptr; A.cur_struct = &s;
  A.task_locals.clear(); A.task_alloc_sites.clear(); A.loops.clear();
  Bind th; th.type = Type::ty_struct(s.c_sym); th.vpath = A.var_path("this"); th.vpath.display = "this";
  A.env["this"] = th;
  for (size_t i = 0; i < m.params.size(); ++i) {
    Bind b; b.type = m.params[i].type;
    int k = (int)i + 1;
    if (b.type && b.type->kind == TypeKind::DynArray) {
      b.vpath.root = RootK::Param;
      b.vpath.origin.kind = Origin::Param;
      b.vpath.origin.id = k;
      b.vpath.var = m.params[i].name;
      b.vpath.display = m.params[i].name;
    } else {
      b.vpath = A.var_path(m.params[i].name);
    }
    A.env[m.params[i].name] = b;
  }
}

void analyze_parallel(Analyzer& A, StmtPtr par) {
  A.written_outer.clear();
  for (auto& t : par->stmts) collect_written_names(t->then_b, A.written_outer);
  std::vector<std::vector<Access>> tasks;
  A.task_local_depth = 1;
  for (auto& t : par->stmts) {
    A.task_locals.clear();
    A.task_alloc_sites.clear();
    A.push_env();
    std::vector<Access> acc;
    A.collect_stmt(t->then_b, acc);
    tasks.push_back(std::move(acc));
    A.pop_env();
  }
  A.task_local_depth = 0;
  report_block(A, tasks, A.file);
}

Summary effects_to_summary(Analyzer& A, const std::vector<Access>& acc, bool file_io) {
  Summary s; s.file_io = file_io;
  // keep only Param/Graph/Tree/Unknown roots (§7.1: DynArray params are Param)
  std::map<std::string, Effect> merged;
  for (auto& a : acc) {
    Path p = a.path;
    if (p.root == RootK::Var) {
      auto it = A.env.find(p.var);
      if (it != A.env.end() && it->second.vpath.root == RootK::Param) {
        p.root = RootK::Param;
        p.origin = it->second.vpath.origin;
        p.cls = it->second.vpath.cls;
        p.display = it->second.vpath.display;
        for (auto& st : p.steps) p.display += Analyzer::step_str(st);
      } else {
        continue;
      }
    }
    if (p.root != RootK::Param && p.root != RootK::Graph &&
        p.root != RootK::Tree && p.root != RootK::Unknown &&
        p.root != RootK::Site)
      continue;
    if (p.root == RootK::Site) continue; // callee-private alloc
    std::string k = std::to_string((int)p.root) + "|" + p.display + "|" +
                    std::to_string(p.origin.id);
    for (auto& st : p.steps) k += Analyzer::step_str(st);
    Effect& e = merged[k];
    e.path = p;
    e.rd = e.rd || a.rd;
    e.wr = e.wr || a.wr;
    if (e.wr) {
      if (!e.wval.known()) e.wval = a.wval;
      else if (!cval_eq(e.wval, a.wval) || !a.wval.known()) e.wval = CVal::unk();
    }
  }
  for (auto& kv : merged) s.effects.push_back(kv.second);
  return s;
}

static bool summaries_eq(const Summary& a, const Summary& b) {
  if (a.file_io != b.file_io || a.effects.size() != b.effects.size()) return false;
  return true; // approximate; iteration uses size+file_io mostly
}

void compute_summaries(Analyzer& A) {
  seed_scene_summaries(A);
  // seed math mutating as write this
  for (auto& m : A.prog.modules) {
    if (m.path != "farmos:math") continue;
    for (auto& s : m.structs) {
      for (auto& md : s.methods) {
        std::string key = s.c_sym + "__" + md.name;
        Summary sum; sum.seeded = true;
        Path th; th.root = RootK::Param; th.origin.kind = Origin::Param; th.origin.id = 0;
        th.display = "this"; th.cls = s.c_sym;
        bool mut = is_mutating_math(md.name) ||
                   (md.ret && md.ret->kind == TypeKind::Void) ||
                   (md.ret && md.ret->kind == TypeKind::Struct && md.ret->name == s.c_sym);
        Effect e;
        e.path = th;
        if (mut) { e.wr = true; e.wval = CVal::unk(); }
        else e.rd = true;
        sum.effects.push_back(e);
        if (md.name == "decompose") {
          // writes three lvalue args: Param 1,2,3
          for (int k = 1; k <= 3; ++k) {
            Path p; p.root = RootK::Param; p.origin.kind = Origin::Param; p.origin.id = k;
            Effect w; w.wr = true; w.wval = CVal::unk(); w.path = p;
            sum.effects.push_back(w);
          }
        }
        A.sums[key] = sum;
      }
    }
  }

  // least fixed point over user functions/methods
  for (int iter = 0; iter < 32; ++iter) {
    bool ch = false;
    for (auto& m : A.prog.modules) {
      if (is_stdlib_mod(m.path)) continue;
      A.cur_mod = &m;
      A.file = m.diag_path.empty() ? m.path : m.diag_path;
      for (auto& f : m.functions) {
        FunctionDecl dummy = f;
        setup_fn_env(A, f, nullptr, nullptr);
        A.collecting_summary = true;
        std::vector<Access> acc;
        A.stmt_counter = 0;
        A.collect_stmt(f.body, acc);
        bool fio = false;
        for (auto& a : acc) (void)a;
        // FILE_IO from instantiated calls already emitted as E0805 only in tasks; track via sums
        Summary ns = effects_to_summary(A, acc, fio);
        // also propagate file_io from callees
        auto add_fio = [&](StmtPtr s, auto&& self) -> void {
          if (!s) return;
          auto chk = [&](ExprPtr e, auto&& se) -> void {
            if (!e) return;
            if (e->kind == ExprKind::Call) {
              auto it = A.sums.find(e->mangled);
              if (it != A.sums.end() && it->second.file_io) ns.file_io = true;
            }
            se(e->lhs, se); se(e->rhs, se);
            for (auto& a : e->args) se(a, se);
          };
          chk(s->init, chk); chk(s->cond, chk); chk(s->lhs, chk); chk(s->rhs, chk);
          chk(s->ret, chk);
          self(s->then_b, self); self(s->else_b, self);
          for (auto& x : s->stmts) self(x, self);
        };
        add_fio(f.body, add_fio);
        auto& old = A.sums[f.c_sym];
        if (old.effects.size() != ns.effects.size() || old.file_io != ns.file_io) ch = true;
        old = ns;
      }
      for (auto& c : m.classes) {
        for (auto& md : c.methods) {
          if (is_scene_sym(c.c_sym) || is_physics_sym(c.c_sym)) continue;
          setup_method_env(A, c, md);
          std::vector<Access> acc;
          A.stmt_counter = 0;
          A.collect_stmt(md.body, acc);
          // convert this var path to Param(0)
          for (auto& a : acc) {
            if (a.path.root == RootK::Var && a.path.var == "this") {
              a.path.root = RootK::Param;
              a.path.origin.kind = Origin::Param; a.path.origin.id = 0;
              a.path.origin.cls = c.c_sym; a.path.cls = c.c_sym;
            }
            if (a.path.root == RootK::Site && a.path.origin.kind == Origin::Param) {
              a.path.root = RootK::Param;
            }
          }
          Summary ns = effects_to_summary(A, acc, false);
          std::string key = c.c_sym + "__" + md.name;
          auto& old = A.sums[key];
          if (old.effects.size() != ns.effects.size()) ch = true;
          old = ns;
        }
      }
      for (auto& s : m.structs) {
        if (m.path == "farmos:math") continue;
        for (auto& md : s.methods) {
          setup_struct_env(A, s, md);
          std::vector<Access> acc;
          A.stmt_counter = 0;
          A.collect_stmt(md.body, acc);
          for (auto& a : acc) {
            if (a.path.root == RootK::Var && a.path.var == "this") {
              a.path.root = RootK::Param;
              a.path.origin.kind = Origin::Param; a.path.origin.id = 0;
              a.path.display = "this";
            }
          }
          Summary ns = effects_to_summary(A, acc, false);
          std::string key = s.c_sym + "__" + md.name;
          auto& old = A.sums[key];
          if (old.effects.size() != ns.effects.size()) ch = true;
          old = ns;
        }
      }
    }
    if (!ch) break;
  }
  A.collecting_summary = false;
}

void compute_fresh(Analyzer& A) {
  fresh_fns.clear();
  bool ch = true;
  while (ch) {
    ch = false;
    for (auto& m : A.prog.modules) {
      for (auto& f : m.functions) {
        if (!f.ret || f.ret->kind != TypeKind::Class) continue;
        bool ok = true;
        bool any = false;
        std::function<void(StmtPtr)> w = [&](StmtPtr s) {
          if (!s) return;
          if (s->kind == StmtKind::Return) {
            any = true;
            if (!s->ret) { ok = false; return; }
            if (s->ret->kind == ExprKind::New) return;
            if (s->ret->kind == ExprKind::Call && fresh_fns.count(s->ret->mangled)) return;
            ok = false;
          }
          w(s->then_b); w(s->else_b);
          for (auto& x : s->stmts) w(x);
        };
        w(f.body);
        if (ok && any) {
          if (!fresh_fns.count(f.c_sym)) { fresh_fns.insert(f.c_sym); ch = true; }
        }
      }
    }
  }
}

Origin orig_from_expr(Analyzer& A, ExprPtr e) {
  Origin o; o.kind = Origin::Unknown;
  if (!e || !e->type || e->type->kind != TypeKind::Class) return o;
  o.cls = e->type->name;
  if (e->kind == ExprKind::New) { o.kind = Origin::Site; o.id = A.site_of(e); return o; }
  if (e->kind == ExprKind::Call && fresh_fns.count(e->mangled)) {
    o.kind = Origin::Site; o.id = A.site_of(e); return o;
  }
  if (e->kind == ExprKind::Ident) {
    auto it = A.env.find(e->name);
    if (it != A.env.end()) return it->second.origin;
  }
  if (e->kind == ExprKind::This) {
    auto it = A.env.find("this");
    if (it != A.env.end()) return it->second.origin;
  }
  return o;
}

struct UseInfo { bool recv = false, field = false, render_ok = false, add_arg = false, other = false; };

void bind_let_for_scan(Analyzer& A, StmtPtr s) {
  Bind b;
  TypePtr ty = s->decl_type;
  if ((!ty || ty->kind == TypeKind::Error) && s->init) ty = s->init->type;
  b.type = ty;
  b.is_const = s->kind == StmtKind::Const;
  b.vpath = A.var_path(s->name);
  if (ty && ty->kind == TypeKind::Class) {
    b.origin = A.expr_origin(s->init);
    if (s->init && s->init->kind == ExprKind::Ident) {
      auto it = A.env.find(s->init->name);
      if (it != A.env.end()) b.origin = it->second.origin;
    }
  } else if (ty && ty->kind == TypeKind::DynArray) {
    if (s->init && s->init->kind == ExprKind::Ident) {
      auto it = A.env.find(s->init->name);
      if (it != A.env.end()) b.vpath = it->second.vpath;
    }
  }
  if (b.is_const) {
    CVal cv = A.fold(s->init);
    if (cv.known()) A.const_vals[s->name] = cv;
  }
  A.env[s->name] = b;
}

void scan_uses_expr(Analyzer& A, ExprPtr e, std::map<int, UseInfo>& uses) {
  if (!e) return;
  if (e->kind == ExprKind::Call) {
    std::string n = call_callee_name(e);
    if (e->lhs && e->lhs->kind == ExprKind::Field) {
      Origin o = orig_from_expr(A, e->lhs->lhs);
      if (o.kind == Origin::Site) uses[o.id].recv = true;
      if (n == "add" || n == "addAt") {
        if (!e->args.empty()) {
          Origin c = orig_from_expr(A, e->args[0]);
          if (c.kind == Origin::Site) uses[c.id].add_arg = true;
          for (size_t i = 1; i < e->args.size(); ++i) {
            Origin x = orig_from_expr(A, e->args[i]);
            if (x.kind == Origin::Site) uses[x.id].other = true;
          }
        }
      } else if ((n == "render" || n == "renderPath") && e->args.size() >= 2) {
        Origin s = orig_from_expr(A, e->args[0]);
        Origin c = orig_from_expr(A, e->args[1]);
        if (s.kind == Origin::Site) uses[s.id].render_ok = true;
        if (c.kind == Origin::Site) uses[c.id].render_ok = true;
        for (size_t i = 2; i < e->args.size(); ++i) {
          Origin x = orig_from_expr(A, e->args[i]);
          if (x.kind == Origin::Site) uses[x.id].other = true;
        }
      } else {
        for (auto& a : e->args) {
          Origin x = orig_from_expr(A, a);
          if (x.kind == Origin::Site) uses[x.id].other = true;
        }
      }
      scan_uses_expr(A, e->lhs->lhs, uses);
      for (auto& a : e->args) scan_uses_expr(A, a, uses);
      return;
    }
    if (e->lhs && e->lhs->kind == ExprKind::Ident) {
      for (auto& a : e->args) {
        Origin o = orig_from_expr(A, a);
        if (o.kind == Origin::Site) uses[o.id].other = true;
      }
    }
    for (auto& a : e->args) scan_uses_expr(A, a, uses);
    scan_uses_expr(A, e->lhs, uses);
    return;
  }
  if (e->kind == ExprKind::New) {
    for (auto& a : e->args) {
      Origin o = orig_from_expr(A, a);
      if (o.kind == Origin::Site) uses[o.id].other = true;
      scan_uses_expr(A, a, uses);
    }
    return;
  }
  if (e->kind == ExprKind::Field) {
    Origin o = orig_from_expr(A, e->lhs);
    if (o.kind == Origin::Site) uses[o.id].field = true;
    scan_uses_expr(A, e->lhs, uses);
    return;
  }
  scan_uses_expr(A, e->lhs, uses);
  scan_uses_expr(A, e->rhs, uses);
  for (auto& a : e->args) scan_uses_expr(A, a, uses);
  for (auto& fv : e->fields) scan_uses_expr(A, fv.second, uses);
}

void scan_uses_stmt(Analyzer& A, StmtPtr s, std::map<int, UseInfo>& uses) {
  if (!s) return;
  if (s->kind == StmtKind::Block) {
    A.push_env();
    for (auto& x : s->stmts) scan_uses_stmt(A, x, uses);
    A.pop_env();
    return;
  }
  if (s->kind == StmtKind::Let || s->kind == StmtKind::Const) {
    scan_uses_expr(A, s->init, uses);
    bind_let_for_scan(A, s);
    return;
  }
  if (s->kind == StmtKind::Assign) {
    scan_uses_expr(A, s->lhs, uses);
    scan_uses_expr(A, s->rhs, uses);
    if (s->lhs && s->lhs->kind == ExprKind::Ident) {
      auto it = A.env.find(s->lhs->name);
      if (it != A.env.end() && !it->second.is_const &&
          it->second.type && it->second.type->kind == TypeKind::Class) {
        it->second.origin.kind = Origin::Unknown;
        it->second.origin.cls = it->second.type->name;
      }
    }
    return;
  }
  if (s->kind == StmtKind::For) {
    A.push_env();
    scan_uses_stmt(A, s->for_init, uses);
    scan_uses_expr(A, s->for_cond, uses);
    scan_uses_stmt(A, s->then_b, uses);
    scan_uses_stmt(A, s->for_update, uses);
    A.pop_env();
    return;
  }
  if (s->kind == StmtKind::If || s->kind == StmtKind::While) {
    scan_uses_expr(A, s->cond, uses);
    A.push_env();
    scan_uses_stmt(A, s->then_b, uses);
    A.pop_env();
    if (s->kind == StmtKind::If) {
      A.push_env();
      scan_uses_stmt(A, s->else_b, uses);
      A.pop_env();
    }
    return;
  }
  if (s->kind == StmtKind::Parallel) {
    for (auto& t : s->stmts) {
      A.push_env();
      scan_uses_stmt(A, t->then_b, uses);
      A.pop_env();
    }
    return;
  }
  scan_uses_expr(A, s->init, uses);
  scan_uses_expr(A, s->cond, uses);
  scan_uses_expr(A, s->lhs, uses);
  scan_uses_expr(A, s->rhs, uses);
  scan_uses_expr(A, s->ret, uses);
  scan_uses_expr(A, s->for_cond, uses);
  scan_uses_stmt(A, s->then_b, uses);
  scan_uses_stmt(A, s->else_b, uses);
  scan_uses_stmt(A, s->for_init, uses);
  scan_uses_stmt(A, s->for_update, uses);
  for (auto& x : s->stmts) scan_uses_stmt(A, x, uses);
}

void compute_detached_sites(Analyzer& A, StmtPtr body) {
  A.detached.clear();
  std::map<int, UseInfo> uses;
  scan_uses_stmt(A, body, uses);
  for (auto& kv : uses) {
    if (!kv.second.add_arg && !kv.second.other) A.detached.insert(kv.first);
  }
}

} // namespace

// Patch expr_origin to use fresh_fns — redefine via wrapping collect

void analyze_conflicts(Program& prog) {
  Analyzer A{prog};
  compute_fresh(A);
  compute_summaries(A);

  // walk user functions for parallel blocks
  for (auto& m : prog.modules) {
    if (is_stdlib_mod(m.path)) continue;
    A.cur_mod = &m;
    A.file = m.diag_path.empty() ? m.path : m.diag_path;
    for (auto& f : m.functions) {
      setup_fn_env(A, f, nullptr, nullptr);
      compute_detached_sites(A, f.body);
      walk_find_parallel(A, f.body);
    }
    for (auto& c : m.classes) {
      if (is_scene_sym(c.c_sym) || is_physics_sym(c.c_sym)) continue;
      for (auto& md : c.methods) {
        setup_method_env(A, c, md);
        compute_detached_sites(A, md.body);
        walk_find_parallel(A, md.body);
      }
    }
    for (auto& s : m.structs) {
      for (auto& md : s.methods) {
        setup_struct_env(A, s, md);
        compute_detached_sites(A, md.body);
        walk_find_parallel(A, md.body);
      }
    }
  }
}

} // namespace farm

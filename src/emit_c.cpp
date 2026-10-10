#include "emit_c.hpp"
#include <sstream>
#include <cctype>
#include <cstdio>
#include <functional>
#include <set>
#include <unordered_set>
#include <vector>

namespace farm {

static bool is_scene_class_sym(const std::string& n) {
  return n == "farm_Scene" || n == "farm_Object3D" ||
         n == "farm_PerspectiveCamera" || n == "farm_Mesh" ||
         n == "farm_BoxGeometry" || n == "farm_SphereGeometry" ||
         n == "farm_PlaneGeometry" || n == "farm_MeshBasicMaterial" ||
         n == "farm_MeshStandardMaterial" || n == "farm_AmbientLight" ||
         n == "farm_DirectionalLight" || n == "farm_PointLight" ||
         n == "farm_RectAreaLight" || n == "farm_Texture" ||
         n == "farm_Renderer";
}

static bool is_physics_class_sym(const std::string& n) {
  return n == "farm_World" || n == "farm_RigidBody" ||
         n == "farm_SphereCollider" || n == "farm_BoxCollider" ||
         n == "farm_PlaneCollider";
}

static bool is_stdlib_mod(const std::string& p) {
  return p == "farmos:math" || p == "farmos:scene" || p == "farmos:physics";
}

static bool type_is_scene(const TypePtr& t) {
  if (!t) return false;
  if (t->kind == TypeKind::Class) return is_scene_class_sym(t->name);
  if (t->kind == TypeKind::DynArray || t->kind == TypeKind::FixedArray) return type_is_scene(t->elem);
  return false;
}

static bool type_is_physics(const TypePtr& t) {
  if (!t) return false;
  if (t->kind == TypeKind::Class) return is_physics_class_sym(t->name);
  if (t->kind == TypeKind::DynArray || t->kind == TypeKind::FixedArray) return type_is_physics(t->elem);
  return false;
}

static bool expr_uses_scene(ExprPtr e);
static bool stmt_uses_scene(StmtPtr s);

static bool expr_uses_scene(ExprPtr e) {
  if (!e) return false;
  if (type_is_scene(e->type)) return true;
  if (expr_uses_scene(e->lhs) || expr_uses_scene(e->rhs)) return true;
  for (auto& a : e->args) if (expr_uses_scene(a)) return true;
  for (auto& fv : e->fields) if (expr_uses_scene(fv.second)) return true;
  return false;
}

static bool stmt_uses_scene(StmtPtr s) {
  if (!s) return false;
  if (type_is_scene(s->decl_type)) return true;
  if (expr_uses_scene(s->init) || expr_uses_scene(s->cond) || expr_uses_scene(s->lhs) ||
      expr_uses_scene(s->rhs) || expr_uses_scene(s->for_cond) || expr_uses_scene(s->ret))
    return true;
  if (stmt_uses_scene(s->then_b) || stmt_uses_scene(s->else_b) ||
      stmt_uses_scene(s->for_init) || stmt_uses_scene(s->for_update))
    return true;
  for (auto& x : s->stmts) if (stmt_uses_scene(x)) return true;
  return false;
}

static bool expr_uses_ray(ExprPtr e) {
  if (!e) return false;
  if (e->kind == ExprKind::Call && e->lhs && e->lhs->kind == ExprKind::Field &&
      e->lhs->name == "renderPath")
    return true;
  if (expr_uses_ray(e->lhs) || expr_uses_ray(e->rhs)) return true;
  for (auto& a : e->args) if (expr_uses_ray(a)) return true;
  for (auto& fv : e->fields) if (expr_uses_ray(fv.second)) return true;
  return false;
}

static bool stmt_uses_ray(StmtPtr s) {
  if (!s) return false;
  if (expr_uses_ray(s->init) || expr_uses_ray(s->cond) || expr_uses_ray(s->lhs) ||
      expr_uses_ray(s->rhs) || expr_uses_ray(s->for_cond) || expr_uses_ray(s->ret))
    return true;
  if (stmt_uses_ray(s->then_b) || stmt_uses_ray(s->else_b) ||
      stmt_uses_ray(s->for_init) || stmt_uses_ray(s->for_update))
    return true;
  for (auto& x : s->stmts) if (stmt_uses_ray(x)) return true;
  return false;
}

bool program_uses_ray(Program& prog) {
  for (auto& m : prog.modules) {
    if (is_stdlib_mod(m.path)) continue;
    for (auto& f : m.functions) if (stmt_uses_ray(f.body)) return true;
    for (auto& c : m.classes)
      for (auto& md : c.methods) if (stmt_uses_ray(md.body)) return true;
    for (auto& s : m.structs)
      for (auto& md : s.methods) if (stmt_uses_ray(md.body)) return true;
    for (auto& op : m.operators) if (stmt_uses_ray(op.body)) return true;
  }
  return false;
}

bool program_uses_scene(Program& prog) {
  for (auto& m : prog.modules) {
    if (is_stdlib_mod(m.path)) continue;
    for (auto& f : m.functions) {
      for (auto& p : f.params) if (type_is_scene(p.type)) return true;
      if (type_is_scene(f.ret) || stmt_uses_scene(f.body)) return true;
    }
    for (auto& c : m.classes) {
      if (is_scene_class_sym(c.c_sym)) return true;
      for (auto& f : c.fields) if (type_is_scene(f.type)) return true;
      for (auto& md : c.methods) {
        for (auto& p : md.params) if (type_is_scene(p.type)) return true;
        if (type_is_scene(md.ret) || stmt_uses_scene(md.body)) return true;
      }
    }
    for (auto& s : m.structs) {
      for (auto& f : s.fields) if (type_is_scene(f.type)) return true;
    }
    for (auto& k : m.consts) {
      if (type_is_scene(k.type) || expr_uses_scene(k.init)) return true;
    }
  }
  return false;
}

static bool is_physics_const_sym(const std::string& n) {
  return n.find("BODY_DYNAMIC") != std::string::npos ||
         n.find("BODY_STATIC") != std::string::npos ||
         n.find("BODY_KINEMATIC") != std::string::npos;
}

static bool expr_uses_physics(ExprPtr e);
static bool stmt_uses_physics(StmtPtr s);

static bool expr_uses_physics(ExprPtr e) {
  if (!e) return false;
  if (type_is_physics(e->type)) return true;
  if (e->kind == ExprKind::Ident && is_physics_const_sym(e->mangled)) return true;
  if (e->kind == ExprKind::New && is_physics_class_sym(e->mangled)) return true;
  if (expr_uses_physics(e->lhs) || expr_uses_physics(e->rhs)) return true;
  for (auto& a : e->args) if (expr_uses_physics(a)) return true;
  for (auto& fv : e->fields) if (expr_uses_physics(fv.second)) return true;
  return false;
}

static bool stmt_uses_physics(StmtPtr s) {
  if (!s) return false;
  if (type_is_physics(s->decl_type)) return true;
  if (expr_uses_physics(s->init) || expr_uses_physics(s->cond) || expr_uses_physics(s->lhs) ||
      expr_uses_physics(s->rhs) || expr_uses_physics(s->for_cond) || expr_uses_physics(s->ret))
    return true;
  if (stmt_uses_physics(s->then_b) || stmt_uses_physics(s->else_b) ||
      stmt_uses_physics(s->for_init) || stmt_uses_physics(s->for_update))
    return true;
  for (auto& x : s->stmts) if (stmt_uses_physics(x)) return true;
  return false;
}

bool program_uses_physics(Program& prog) {
  for (auto& m : prog.modules) {
    if (is_stdlib_mod(m.path)) continue;
    for (auto& f : m.functions) {
      for (auto& p : f.params) if (type_is_physics(p.type)) return true;
      if (type_is_physics(f.ret) || stmt_uses_physics(f.body)) return true;
    }
    for (auto& c : m.classes) {
      if (is_physics_class_sym(c.c_sym)) return true;
      for (auto& f : c.fields) if (type_is_physics(f.type)) return true;
      for (auto& md : c.methods) {
        for (auto& p : md.params) if (type_is_physics(p.type)) return true;
        if (type_is_physics(md.ret) || stmt_uses_physics(md.body)) return true;
      }
    }
    for (auto& s : m.structs) {
      for (auto& f : s.fields) if (type_is_physics(f.type)) return true;
    }
    for (auto& k : m.consts) {
      if (type_is_physics(k.type) || expr_uses_physics(k.init)) return true;
    }
  }
  return false;
}

// C11 string literal body for arbitrary bytes. Printable ASCII passes through, except `\\`, `"`
// and `?` (trigraph guard). Every other byte (controls, NUL, DEL, all non-ASCII UTF-8 bytes) is
// emitted as a fixed 3-digit octal escape `\ooo`: an octal escape ends after at most 3 digits, so
// a following digit/hex character can never be absorbed (unlike `\x..`), and NUL is `\000`.
static std::string c_escape_bytes(const std::string& bytes) {
  std::string esc;
  esc.reserve(bytes.size() + 8);
  for (unsigned char c : bytes) {
    if (c >= 0x20 && c < 0x7F && c != '\\' && c != '"' && c != '?') esc += (char)c;
    else {
      char buf[5];
      std::snprintf(buf, sizeof(buf), "\\%03o", (unsigned)c);
      esc += buf;
    }
  }
  return esc;
}

// FarmString value with an explicit byte length (embedded NUL survives; never strlen-based).
static std::string c_string_value(const std::string& bytes) {
  return "((FarmString){ \"" + c_escape_bytes(bytes) + "\", (int64_t)" +
         std::to_string((int64_t)bytes.size()) + " })";
}


struct Emitter {
  Program& prog;
  std::ostringstream out;
  int tmp = 0;
  std::string fresh(const std::string& p="t") { return p + std::to_string(tmp++); }

  std::string sanitize(std::string s) {
    for (char& c : s) if (!std::isalnum((unsigned char)c)) c = '_';
    return s;
  }

  // M3: Set at the start of emit_all(). Non-scene programs MUST match master codegen.
  bool uses_scene = false;
  bool uses_physics = false;
  bool uses_parallel = false;
  bool user_imports_math = false;
  bool rt_math() const { return uses_scene || uses_physics; }
  std::unordered_map<std::string, std::string> cap_val;
  std::unordered_map<std::string, std::string> cap_ptr;
  std::string this_c = "this";

  std::string ident_val(const std::string& n) {
    auto it = cap_val.find(n);
    if (it != cap_val.end()) return it->second;
    return "v_" + n;
  }
  std::string ident_ptr(const std::string& n) {
    auto it = cap_ptr.find(n);
    if (it != cap_ptr.end()) return it->second;
    return "&v_" + n;
  }

  static bool is_object3d_sym(const std::string& n) {
    return n == "farm_Object3D" || n == "farm_Scene" || n == "farm_PerspectiveCamera" ||
           n == "farm_Mesh" || n == "farm_AmbientLight" || n == "farm_DirectionalLight" ||
           n == "farm_PointLight" || n == "farm_RectAreaLight";
  }

  static bool is_math_type_name(const std::string& n) {
    // Type::name is the c_sym, e.g. m1_Vector3
    auto pos = n.rfind('_');
    if (pos == std::string::npos) return false;
    std::string base = n.substr(pos + 1);
    return base == "Vector2" || base == "Vector3" || base == "Vector4" ||
           base == "Matrix3" || base == "Matrix4" || base == "Quaternion" ||
           base == "Color" || base == "Euler" || base == "Ray" ||
           base == "Sphere" || base == "Box3" || base == "RayHit";
  }

  static std::string math_base(const std::string& c_sym) {
    auto pos = c_sym.rfind('_');
    return pos == std::string::npos ? c_sym : c_sym.substr(pos + 1);
  }

  // farm_math.h field names when emitting against runtime math types (scene programs only).
  std::string runtime_field_name(const std::string& field, const std::string& struct_base) {
    if (struct_base == "Vector2" && (field == "x" || field == "y")) return field;
    if (struct_base == "Vector4" && field == "w") return "w";
    if (struct_base == "Quaternion" && field == "w") return "w";
    if (struct_base == "Color" && (field == "r" || field == "g" || field == "b")) return field;
    if (struct_base == "Euler" && field == "order") return "order";
    if (struct_base == "Matrix3" || struct_base == "Matrix4") {
      if (field == "elements") return "elements";
    }
    if (struct_base == "Ray" && (field == "origin" || field == "direction")) return field;
    if (struct_base == "Box3" && (field == "min" || field == "max")) return field;
    if (struct_base == "Sphere" && (field == "center" || field == "radius")) return field;
    if (struct_base == "RayHit" && (field == "hit" || field == "point" || field == "distance")) return field;
    return "f_" + field;
  }

  std::string struct_field_access(const std::string& field, const TypePtr& st) {
    if (rt_math() && st && st->kind == TypeKind::Struct && is_math_type_name(st->name))
      return runtime_field_name(field, math_base(st->name));
    return "f_" + field;
  }

  std::string scene_class_field(const std::string& field) {
    if (field == "matrixAutoUpdate") return "matrixAutoUpdate";
    if (field == "parent" || field == "children" || field == "type") return field;
    if (field == "roughness" || field == "metalness" || field == "transmission" ||
        field == "ior" || field == "emissiveIntensity" || field == "map") return field;
    if (field == "disposed" || field == "width" || field == "height") return field;
    if (field == "hasBackground" || field == "background") return field;
    if (field == "matrixWorldInverse" || field == "projectionMatrix") return field;
    return "f_" + field;
  }

  std::string c_type(const TypePtr& t) {
    switch (t->kind) {
      case TypeKind::Int: return "int64_t";
      case TypeKind::Float: return "double";
      case TypeKind::Bool: return "int8_t";
      case TypeKind::String: return "FarmString";
      case TypeKind::Void: return "void";
      case TypeKind::Struct:
        if (rt_math() && is_math_type_name(t->name))
          return "farm_" + math_base(t->name);
        return "struct Farm_" + t->name;
      case TypeKind::Class:
        if (uses_scene && is_scene_class_sym(t->name))
          return t->name + "*";
        if (uses_physics && is_physics_class_sym(t->name))
          return t->name + "*";
        return "struct Farm_" + t->name + "*";
      case TypeKind::DynArray: return "FarmDynArray";
      case TypeKind::FixedArray: return "FarmFixed_" + sanitize(t->str());
      default: return "int64_t";
    }
  }

  void emit_fixed_typedefs() {
    std::unordered_set<std::string> seen;
    std::function<void(TypePtr)> walk = [&](TypePtr t) {
      if (!t) return;
      if (t->kind == TypeKind::FixedArray) {
        std::string n = "FarmFixed_" + sanitize(t->str());
        if (!seen.count(n)) {
          seen.insert(n);
          walk(t->elem);
          out << "typedef struct { " << c_type(t->elem) << " data[" << t->fixed_len << "]; } " << n << ";\n";
        }
      } else if (t->kind == TypeKind::DynArray) walk(t->elem);
    };
    for (auto& m : prog.modules) {
      if (m.path == "farmos:scene" || m.path == "farmos:physics") continue;
      if (m.path == "farmos:math" && (rt_math() || !user_imports_math)) continue;
      for (auto& s : m.structs) for (auto& f : s.fields) walk(f.type);
      for (auto& c : m.classes) {
        for (auto& f : c.fields) walk(f.type);
        for (auto& md : c.methods) { for (auto& p: md.params) walk(p.type); walk(md.ret); }
      }
      for (auto& f : m.functions) {
        for (auto& p : f.params) walk(p.type);
        walk(f.ret);
      }
      for (auto& c : m.consts) walk(c.type);
      std::function<void(ExprPtr)> we = [&](ExprPtr e) {
        if (!e) return;
        walk(e->type);
        we(e->lhs); we(e->rhs);
        for (auto& a : e->args) we(a);
        for (auto& fv : e->fields) we(fv.second);
      };
      std::function<void(StmtPtr)> wse = [&](StmtPtr s) {
        if (!s) return;
        walk(s->decl_type);
        we(s->init); we(s->cond); we(s->lhs); we(s->rhs); we(s->for_cond); we(s->ret);
        wse(s->then_b); wse(s->else_b); wse(s->for_init); wse(s->for_update);
        for (auto& x : s->stmts) wse(x);
      };
      for (auto& f : m.functions) wse(f.body);
      for (auto& op : m.operators) wse(op.body);
      for (auto& c : m.classes) for (auto& md : c.methods) wse(md.body);
      for (auto& s : m.structs) for (auto& md : s.methods) wse(md.body);
    }
    walk(Type::ty_fixed(Type::ty_int(), 2));
    walk(Type::ty_fixed(Type::ty_int(), 3));
  }

  std::string emit_expr(ExprPtr e) {
    if (!e) return "0";
    switch (e->kind) {
      case ExprKind::IntLit: return "(int64_t)" + std::to_string(e->int_val) + "LL";
      case ExprKind::FloatLit: {
        std::ostringstream o; o.precision(17); o << e->float_val;
        std::string s = o.str();
        if (s.find('.')==std::string::npos && s.find('e')==std::string::npos && s.find('E')==std::string::npos) s += ".0";
        return s;
      }
      case ExprKind::BoolLit: return e->bool_val ? "((int8_t)1)" : "((int8_t)0)";
      case ExprKind::StringLit: return c_string_value(e->str_val);
      case ExprKind::Ident: return !e->mangled.empty() ? e->mangled : ident_val(e->name);
      case ExprKind::This: return this_c;
      case ExprKind::Unary: {
        // M2: Check for unary operator overload
        if (!e->mangled.empty()) {
          std::string rhs_val = emit_expr(e->rhs);
          std::string ty = c_type(e->type);
          std::string v = fresh("unary_result");
          out << ty << " " << v << " = " << e->mangled << "(&" << rhs_val << ");\n";
          return v;
        }
        
        auto a = emit_expr(e->rhs);
        if (e->op==TokKind::Bang) return "((int8_t)!(" + a + "))";
        if (e->op==TokKind::Minus) return "(-(" + a + "))";
        return "(+(" + a + "))";
      }
      case ExprKind::Binary: {
        // M2: Check for operator overload
        if (!e->mangled.empty()) {
          std::string lhs_val = emit_expr(e->lhs);
          std::string rhs_val = emit_expr(e->rhs);
          
          // M2: Free operator functions (test 036) - pass all params by value
          if (e->is_operator_call) {
            if (e->type->kind == TypeKind::Struct) {
              // Returns struct - need temp variable
              std::string ty = c_type(e->type);
              std::string v = fresh("op_result");
              out << ty << " " << v << " = " << e->mangled << "(" << lhs_val << ", " << rhs_val << ");\n";
              return v;
            } else {
              // Returns primitive (e.g. bool for ==, !=)
              return e->mangled + "(" + lhs_val + ", " + rhs_val + ")";
            }
          }
          
          // M2: Reverse operators (scalar * vector) - swap and pass scalar by value
          if (e->is_reverse_op) {
            // For reverse ops, lhs is scalar, rhs is struct
            if (e->type->kind == TypeKind::Struct) {
              std::string ty = c_type(e->type);
              std::string v = fresh("op_result");
              out << ty << " " << v << " = " << e->mangled << "(&" << rhs_val << ", " << lhs_val << ");\n";
              return v;
            } else {
              return e->mangled + "(&" + rhs_val + ", " + lhs_val + ")";
            }
          }
          
          // Normal method operators
          if (e->type->kind == TypeKind::Struct) {
            // Returns struct - need temp variable
            std::string ty = c_type(e->type);
            std::string v = fresh("op_result");
            out << ty << " " << v << " = " << e->mangled << "(&" << lhs_val << ", " << rhs_val << ");\n";
            return v;
          } else {
            // Returns primitive (e.g. bool for ==, !=)
            return e->mangled + "(&" + lhs_val + ", " + rhs_val + ")";
          }
        }
        
        auto a = emit_expr(e->lhs), b = emit_expr(e->rhs);
        switch (e->op) {
          case TokKind::OrOr: return "((" + a + ")||(" + b + "))";
          case TokKind::AndAnd: return "((" + a + ")&&(" + b + "))";
          case TokKind::EqEq:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)farm_str_eq(" + a + "," + b + "))";
            return "((int8_t)((" + a + ")==(" + b + ")))";
          case TokKind::Neq:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)!farm_str_eq(" + a + "," + b + "))";
            return "((int8_t)((" + a + ")!=(" + b + ")))";
          case TokKind::Lt:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)(farm_str_cmp(" + a + "," + b + ")<0))";
            return "((int8_t)((" + a + ")<(" + b + ")))";
          case TokKind::Le:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)(farm_str_cmp(" + a + "," + b + ")<=0))";
            return "((int8_t)((" + a + ")<=(" + b + ")))";
          case TokKind::Gt:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)(farm_str_cmp(" + a + "," + b + ")>0))";
            return "((int8_t)((" + a + ")>(" + b + ")))";
          case TokKind::Ge:
            if (e->lhs->type->kind==TypeKind::String) return "((int8_t)(farm_str_cmp(" + a + "," + b + ")>=0))";
            return "((int8_t)((" + a + ")>=(" + b + ")))";
          case TokKind::Plus:
            if (e->type->kind==TypeKind::String) return "farm_str_concat(" + a + "," + b + ")";
            if (e->type->kind==TypeKind::Int) return "((int64_t)((uint64_t)(" + a + ")+(uint64_t)(" + b + ")))";
            return "((" + a + ")+(" + b + "))";
          case TokKind::Minus:
            if (e->type->kind==TypeKind::Int) return "((int64_t)((uint64_t)(" + a + ")-(uint64_t)(" + b + ")))";
            return "((" + a + ")-(" + b + "))";
          case TokKind::Star:
            if (e->type->kind==TypeKind::Int) return "((int64_t)((uint64_t)(" + a + ")*(uint64_t)(" + b + ")))";
            return "((" + a + ")*(" + b + "))";
          case TokKind::Slash:
            if (e->type->kind==TypeKind::Int)
              return "({ int64_t __a=(" + a + "); int64_t __b=(" + b + "); farm_div0_check(__b); __a/__b; })";
            return "((" + a + ")/(" + b + "))";
          case TokKind::Percent:
            return "({ int64_t __a=(" + a + "); int64_t __b=(" + b + "); farm_div0_check(__b); __a%__b; })";
          default: return "0";
        }
      }
      case ExprKind::Index: {
        auto arr = emit_expr(e->lhs);
        auto idx = emit_expr(e->rhs);
        if (e->lhs->type->kind == TypeKind::FixedArray) {
          // M3: farm_Matrix3/4.elements is a C array, not FarmFixed_{}.data.
          bool scene_c_array = rt_math() && e->lhs->kind == ExprKind::Field &&
            e->lhs->lhs && e->lhs->lhs->type && e->lhs->lhs->type->kind == TypeKind::Struct &&
            is_math_type_name(e->lhs->lhs->type->name) && e->lhs->name == "elements";
          if (scene_c_array)
            return "({ int64_t __i=(" + idx + "); farm_bounds_check(__i, (int64_t)" + std::to_string(e->lhs->type->fixed_len) + "); (" + arr + ")[__i]; })";
          return "({ int64_t __i=(" + idx + "); farm_bounds_check(__i, (int64_t)" + std::to_string(e->lhs->type->fixed_len) + "); (" + arr + ").data[__i]; })";
        } else {
          std::string et = c_type(e->lhs->type->elem);
          return "({ int64_t __i=(" + idx + "); *(" + et + "*)farm_dyn_index(&(" + arr + "), __i); })";
        }
      }
      case ExprKind::Field: {
        auto base = emit_expr(e->lhs);
        if (e->lhs->type->kind == TypeKind::Class) {
          if (uses_scene && is_scene_class_sym(e->lhs->type->name)) {
            std::string field_access = "(" + base + ")->" + scene_class_field(e->name);
            if (is_object3d_sym(e->lhs->type->name)) {
              if (e->name == "rotation")
                return "({ sync_rotation_from_quaternion((farm_Object3D*)(" + base + ")); " + field_access + "; })";
              if (e->name == "quaternion")
                return "({ sync_quaternion_from_rotation((farm_Object3D*)(" + base + ")); " + field_access + "; })";
            }
            if (e->lhs->type->name == "farm_Mesh" && (e->name == "material" || e->name == "geometry"))
              return "(" + c_type(e->type) + ")" + field_access;
            return field_access;
          }
          if (uses_physics && is_physics_class_sym(e->lhs->type->name))
            return "(" + base + ")->f_" + e->name;
          return "(" + base + ")->f_" + e->name;
        }
        if (rt_math() && e->name == "order" && e->lhs->type && e->lhs->type->kind == TypeKind::Struct &&
            is_math_type_name(e->lhs->type->name) && math_base(e->lhs->type->name) == "Euler") {
          return "({ farm_Euler __eo = (" + base + "); farm_euler_order_string(&__eo); })";
        }
        if (e->lhs->kind == ExprKind::This)
          return "(" + base + ")->" + struct_field_access(e->name, e->lhs->type);
        return "(" + base + ")." + struct_field_access(e->name, e->lhs->type);
      }
      case ExprKind::Call: {
        if (e->lhs->kind==ExprKind::Ident) {
          std::string n = e->lhs->name;
          if (n=="print"||n=="println") {
            auto a = emit_expr(e->args[0]);
            std::string pref = (n=="println") ? "farm_println_" : "farm_print_";
            switch (e->args[0]->type->kind) {
              case TypeKind::Int: return pref + "int(" + a + ")";
              case TypeKind::Float: return pref + "float(" + a + ")";
              case TypeKind::Bool: return pref + "bool(" + a + ")";
              case TypeKind::String: return pref + "string(" + a + ")";
              case TypeKind::Struct: {
                // M2: For now, emit a helper function call instead of inline
                std::string struct_name = e->args[0]->type->name;
                
                // Find the struct definition
                StructDecl* sd = nullptr;
                std::string display_name = struct_name;
                for (auto& m : prog.modules) {
                  for (auto& s : m.structs) {
                    if (s.c_sym == struct_name) { 
                      sd = &s; 
                      display_name = s.name;
                      break; 
                    }
                  }
                  if (sd) break;
                }
                
                // Generate helper function name
                std::string helper = "farm_print_" + sanitize(display_name);
                if (n == "println") helper += "_ln";
                
                // Emit call to helper (will be generated separately)
                return helper + "(" + a + ")";
              }
              default: return "0";
            }
          }
          if (n=="len") {
            auto a = emit_expr(e->args[0]);
            if (e->args[0]->type->kind==TypeKind::String) return "farm_str_len(" + a + ")";
            if (e->args[0]->type->kind==TypeKind::FixedArray) return "(int64_t)" + std::to_string(e->args[0]->type->fixed_len);
            return "farm_dyn_len(&(" + a + "))";
          }
          if (n=="push") {
            auto a = emit_expr(e->args[0]);
            auto v = emit_expr(e->args[1]);
            std::string tmpv = fresh("pv");
            out << c_type(e->args[1]->type) << " " << tmpv << " = " << v << ";\n";
            return "(farm_dyn_push(&(" + a + "), &" + tmpv + "), (int)0)";
          }
          if (n=="str") {
            auto a = emit_expr(e->args[0]);
            switch (e->args[0]->type->kind) {
              case TypeKind::Int: return "farm_str_from_int(" + a + ")";
              case TypeKind::Float: return "farm_str_from_float(" + a + ")";
              case TypeKind::Bool: return "farm_str_from_bool(" + a + ")";
              default: return "farm_str_from_cstr(\"\")";
            }
          }
          if (n=="int") {
            auto a = emit_expr(e->args[0]);
            if (e->args[0]->type->kind==TypeKind::Float) return "farm_float_to_int(" + a + ")";
            return "farm_bool_to_int(" + a + ")";
          }
          if (n=="float") return "farm_int_to_float(" + emit_expr(e->args[0]) + ")";
          if (!e->mangled.empty() && e->mangled != n) {
            std::string call = e->mangled + "(";
            for (size_t i=0;i<e->args.size();++i) {
              if (i) call += ", ";
              call += emit_expr(e->args[i]);
            }
            call += ")";
            return call;
          }
          if (!e->mangled.empty()) {
            std::string call = e->mangled + "(";
            for (size_t i=0;i<e->args.size();++i) {
              if (i) call += ", ";
              call += emit_expr(e->args[i]);
            }
            call += ")";
            return call;
          }
        }
        if (e->lhs->kind==ExprKind::Field) {
          // For struct methods, pass &receiver; for class methods, pass receiver (already a pointer)
          std::string recv;
          if (e->lhs->lhs->type->kind == TypeKind::Struct) {
            // Struct method: need address of receiver
            if (e->lhs->lhs->is_lvalue) {
              // Receiver is an lvalue, pass its address directly
              recv = emit_lvalue_ptr(e->lhs->lhs);
            } else {
              // Receiver is an rvalue (temporary)
              // Check if it's a method call that returns a pointer (for chaining)
              if (e->lhs->lhs->kind == ExprKind::Call && 
                  e->lhs->lhs->lhs && 
                  e->lhs->lhs->lhs->kind == ExprKind::Field) {
                // This is a chained method call; the result is already a pointer
                recv = emit_expr(e->lhs->lhs);
              } else {
                // Regular rvalue, emit it first then store
                std::string recv_val = emit_expr(e->lhs->lhs);
                std::string tmp = fresh("rcv");
                out << c_type(e->lhs->lhs->type) << " " << tmp << " = " << recv_val << ";\n";
                recv = "&" + tmp;
              }
            }
          } else {
            // Class method: receiver is already a pointer
            recv = emit_expr(e->lhs->lhs);
          }

          // M3: Object3D hierarchy/lookAt live on farm_Object3D_* regardless of subclass.
          if (uses_scene && e->lhs->lhs->type->kind == TypeKind::Class &&
              is_object3d_sym(e->lhs->lhs->type->name) &&
              (e->mangled.find("farm_Object3D_") == 0)) {
            recv = "(farm_Object3D*)(" + recv + ")";
          }

          std::string call = e->mangled + "(" + recv;
          for (auto& a : e->args) {
            call += ", ";
            std::string arg_val = emit_expr(a);
            if (uses_scene && a->type && a->type->kind == TypeKind::Class && is_object3d_sym(a->type->name) &&
                (e->mangled.find("farm_Object3D_add") == 0 || e->mangled.find("farm_Object3D_remove") == 0 ||
                 e->mangled.find("farm_Object3D_addAt") == 0 || e->mangled == "farm_RigidBody_setObject")) {
              arg_val = "(farm_Object3D*)(" + arg_val + ")";
            }
            if (uses_physics && e->mangled == "farm_RigidBody_setObject" && a->type &&
                a->type->kind == TypeKind::Class && is_object3d_sym(a->type->name)) {
              arg_val = "(farm_Object3D*)(" + arg_val + ")";
            }
            call += arg_val;
          }
          call += ")";

          // M3: Quaternion.set / setFromAxisAngle on Object3D.quaternion must mark quaternion_dirty
          // so a subsequent rotation read syncs. Gated: only when the receiver is a scene-object field.
          if (uses_scene && e->lhs->lhs->type->kind == TypeKind::Struct &&
              is_math_type_name(e->lhs->lhs->type->name) && math_base(e->lhs->lhs->type->name) == "Quaternion" &&
              (e->lhs->name == "set" || e->lhs->name == "setFromAxisAngle") &&
              e->lhs->lhs->kind == ExprKind::Field && e->lhs->lhs->lhs &&
              e->lhs->lhs->lhs->type && e->lhs->lhs->lhs->type->kind == TypeKind::Class &&
              is_object3d_sym(e->lhs->lhs->lhs->type->name)) {
            std::string obj = emit_expr(e->lhs->lhs->lhs);
            out << "(void)(" << call << ");\n";
            out << "((farm_Object3D*)(" << obj << "))->quaternion_dirty = 1;\n";
            return "0";
          }
          return call;
        }
        return "0";
      }
      case ExprKind::ArrayLit: {
        if (e->type->kind == TypeKind::DynArray) {
          std::string v = fresh("da");
          out << "FarmDynArray " << v << "; farm_dyn_init(&" << v << ", (int64_t)sizeof(" << c_type(e->type->elem) << "));\n";
          for (auto& a : e->args) {
            std::string elv = emit_expr(a);
            std::string el = fresh("el");
            out << c_type(e->type->elem) << " " << el << " = " << elv << "; farm_dyn_push(&" << v << ", &" << el << ");\n";
          }
          return v;
        }
        std::string ty = c_type(e->type);
        std::string v = fresh("fa");
        out << ty << " " << v << ";\n";
        for (size_t i=0;i<e->args.size();++i) {
          std::string elv = emit_expr(e->args[i]);
          out << v << ".data[" << i << "] = " << elv << ";\n";
        }
        return v;
      }
      case ExprKind::StructLit: {
        std::string ty = c_type(e->type);
        std::string v = fresh("st");
        out << ty << " " << v << ";\n";
        StructDecl* lit_sd = nullptr;
        for (auto& mm : prog.modules) for (auto& ss : mm.structs) if (ss.c_sym == e->mangled) { lit_sd = &ss; break; }
        for (auto& fv : e->fields) {
          std::string arg_val = emit_expr(fv.second);
          if (rt_math() && lit_sd && lit_sd->name == "Euler" && fv.first == "order") {
            out << "farm_euler_set_order(&" << v << ", " << arg_val << ");\n";
            continue;
          }
          std::string fn = (rt_math() && lit_sd)
            ? runtime_field_name(fv.first, lit_sd->name) : ("f_" + fv.first);
          out << v << "." << fn << " = " << arg_val << ";\n";
        }
        return v;
      }
      case ExprKind::New: {
        if (e->type->kind == TypeKind::Class) {
          std::string v = fresh("obj");
          if ((uses_scene && is_scene_class_sym(e->type->name)) ||
              (uses_physics && is_physics_class_sym(e->type->name))) {
            std::vector<std::string> arg_values;
            for (auto& arg : e->args) arg_values.push_back(emit_expr(arg));
            out << e->mangled << "* " << v << " = " << e->mangled << "_new";
            if (!e->ctor_variant.empty()) out << "_" << e->ctor_variant;
            out << "(";
            for (size_t i = 0; i < arg_values.size(); ++i) {
              if (i) out << ", ";
              out << arg_values[i];
            }
            out << ");\n";
            return v;
          }
          out << "struct Farm_" << e->mangled << "* " << v << " = (struct Farm_" << e->mangled << "*)farm_arena_alloc(sizeof(struct Farm_" << e->mangled << "));\n";
          out << e->mangled << "__constructor(" << v;
          for (auto& a : e->args) out << ", " << emit_expr(a);
          out << ");\n";
          return v;
        }
        std::string ty = c_type(e->type);
        std::string v = fresh("st");
        StructDecl* sd = nullptr;
        for (auto& mm : prog.modules) for (auto& ss : mm.structs) if (ss.c_sym == e->mangled) { sd = &ss; break; }
        if (!sd) return "0";
        out << ty << " " << v << ";\n";
        if (e->args.size() == 0) {
          // M2: Default constructor - special handling for math types
          if (sd->name == "Matrix4") {
            // Identity matrix: diagonal = 1, rest = 0
            std::string el = rt_math() ? (v + ".elements") : (v + ".f_elements.data");
            out << "  for (int i = 0; i < 16; i++) " << el << "[i] = 0.0;\n";
            out << "  " << el << "[0] = 1.0;\n";   // [0,0]
            out << "  " << el << "[5] = 1.0;\n";   // [1,1]
            out << "  " << el << "[10] = 1.0;\n";  // [2,2]
            out << "  " << el << "[15] = 1.0;\n";  // [3,3]
          } else if (sd->name == "Matrix3") {
            // Identity matrix: diagonal = 1, rest = 0
            std::string el = rt_math() ? (v + ".elements") : (v + ".f_elements.data");
            out << "  for (int i = 0; i < 9; i++) " << el << "[i] = 0.0;\n";
            out << "  " << el << "[0] = 1.0;\n";   // [0,0]
            out << "  " << el << "[4] = 1.0;\n";   // [1,1]
            out << "  " << el << "[8] = 1.0;\n";   // [2,2]
          } else if (sd->name == "Quaternion") {
            // Identity quaternion: (0, 0, 0, 1)
            out << "  " << v << ".f_x = 0.0;\n";
            out << "  " << v << ".f_y = 0.0;\n";
            out << "  " << v << ".f_z = 0.0;\n";
            out << "  " << v << (rt_math() ? ".w" : ".f_w") << " = 1.0;\n";
          } else if (sd->name == "Box3") {
            // Empty box: min = +infinity, max = -infinity
            std::string mn = rt_math() ? ".min" : ".f_min";
            std::string mx = rt_math() ? ".max" : ".f_max";
            out << "  " << v << mn << ".f_x = 1.0/0.0;\n";  // +inf
            out << "  " << v << mn << ".f_y = 1.0/0.0;\n";
            out << "  " << v << mn << ".f_z = 1.0/0.0;\n";
            out << "  " << v << mx << ".f_x = -1.0/0.0;\n"; // -inf
            out << "  " << v << mx << ".f_y = -1.0/0.0;\n";
            out << "  " << v << mx << ".f_z = -1.0/0.0;\n";
          } else if (sd->name == "Euler") {
            // Default Euler: (0, 0, 0, "XYZ")
            out << "  " << v << ".f_x = 0.0;\n";
            out << "  " << v << ".f_y = 0.0;\n";
            out << "  " << v << ".f_z = 0.0;\n";
            if (rt_math())
              out << "  " << v << ".order[0]='X'; " << v << ".order[1]='Y'; " << v << ".order[2]='Z'; " << v << ".order[3]=0;\n";
            else
              out << "  " << v << ".f_order = (FarmString){.ptr=\"XYZ\", .len=3};\n";
          } else {
            // Default: zero all fields
            for (auto& f : sd->fields) {
              std::string fn = rt_math() ? runtime_field_name(f.name, sd->name) : ("f_" + f.name);
              if (f.type->kind == TypeKind::Struct) {
                // Nested struct - create default instance
                std::string nested_ty = c_type(f.type);
                std::string nested_v = fresh("st");
                out << nested_ty << " " << nested_v << ";\n";
                // Zero-initialize nested struct fields recursively
                StructDecl* nested_sd = nullptr;
                for (auto& mm : prog.modules) {
                  for (auto& ss : mm.structs) {
                    if (ss.c_sym == f.type->name) {
                      nested_sd = &ss;
                      break;
                    }
                  }
                  if (nested_sd) break;
                }
                if (nested_sd) {
                  for (auto& nf : nested_sd->fields) {
                    std::string zero_val = "0";
                    if (nf.type->kind == TypeKind::Float) zero_val = "0.0";
                    std::string nfn = rt_math()
                      ? runtime_field_name(nf.name, nested_sd->name)
                      : ("f_" + nf.name);
                    out << nested_v << "." << nfn << " = " << zero_val << ";\n";
                  }
                }
                out << v << "." << fn << " = " << nested_v << ";\n";
              } else {
                std::string zero_val = "0";
                if (f.type->kind == TypeKind::Float) zero_val = "0.0";
                out << v << "." << fn << " = " << zero_val << ";\n";
              }
            }
          }
        } else if (sd->name == "Color" && e->args.size() == 1) {
          // Color(hex: int) constructor
          std::string hex_val = emit_expr(e->args[0]);
          std::string r = rt_math() ? "r" : "f_r";
          std::string g = rt_math() ? "g" : "f_g";
          std::string b = rt_math() ? "b" : "f_b";
          out << v << "." << r << " = ((" << hex_val << " >> 16) & 255) / 255.0;\n";
          out << v << "." << g << " = ((" << hex_val << " >> 8) & 255) / 255.0;\n";
          out << v << "." << b << " = (" << hex_val << " & 255) / 255.0;\n";
        } else if (sd->name == "Euler" && e->args.size() == 3) {
          // Euler(x, y, z) constructor with default order "XYZ"
          for (size_t i=0; i<3; ++i) {
            std::string arg_val = emit_expr(e->args[i]);
            out << v << ".f_" << sd->fields[i].name << " = " << arg_val << ";\n";
          }
          if (rt_math())
            out << "  " << v << ".order[0]='X'; " << v << ".order[1]='Y'; " << v << ".order[2]='Z'; " << v << ".order[3]=0;\n";
          else
            out << v << ".f_order = (FarmString){.ptr=\"XYZ\", .len=3};\n";
        } else {
          // Explicit constructor with all arguments
          for (size_t i=0;i<e->args.size();++i) {
            std::string arg_val = emit_expr(e->args[i]);
            // Scene farm_Euler.order is char[4]; FarmString is not assignable (clang error).
            if (rt_math() && sd->name == "Euler" && sd->fields[i].name == "order") {
              out << "farm_euler_set_order(&" << v << ", " << arg_val << ");\n";
              continue;
            }
            std::string fn = rt_math() ? runtime_field_name(sd->fields[i].name, sd->name) : ("f_" + sd->fields[i].name);
            out << v << "." << fn << " = " << arg_val << ";\n";
          }
        }
        return v;
      }
      default: return "0";
    }
  }

  /* Pointer to lvalue storage; base/index evaluated exactly once (temps written to `out`). */
  std::string emit_lvalue_ptr(ExprPtr lv) {
    if (lv->kind == ExprKind::Ident) {
      return ident_ptr(lv->name);
    }
    if (lv->kind == ExprKind::This) {
      return this_c;
    }
    if (lv->kind == ExprKind::Field) {
      if (lv->lhs->type->kind == TypeKind::Class) {
        std::string base = emit_expr(lv->lhs);
        std::string bp = fresh("bp");
        out << c_type(lv->lhs->type) << " " << bp << " = " << base << ";\n";
        std::string fn = (uses_scene && is_scene_class_sym(lv->lhs->type->name))
          ? scene_class_field(lv->name) : ("f_" + lv->name);
        return "&(" + bp + "->" + fn + ")";
      }
      /* struct field: address of enclosing struct value, then field */
      std::string sp = emit_lvalue_ptr(lv->lhs);
      std::string fn = struct_field_access(lv->name, lv->lhs->type);
      return "&((" + sp + ")->" + fn + ")";
    }
    if (lv->kind == ExprKind::Index) {
      std::string ip = fresh("ix");
      std::string idx = emit_expr(lv->rhs);
      out << "int64_t " << ip << " = " << idx << ";\n";
      if (lv->lhs->type->kind == TypeKind::FixedArray) {
        std::string ap = emit_lvalue_ptr(lv->lhs);
        out << "farm_bounds_check(" << ip << ", (int64_t)" << lv->lhs->type->fixed_len << ");\n";
        if (rt_math() && lv->lhs->kind == ExprKind::Field && lv->lhs->name == "elements")
          return "&((*(" + ap + "))[" + ip + "])";
        return "&((" + ap + ")->data[" + ip + "])";
      }
      if (lv->lhs->kind == ExprKind::Ident) {
        std::string an = ident_val(lv->lhs->name);
        out << "farm_bounds_check(" << ip << ", " << an << ".len);\n";
        return "((" + c_type(lv->type) + "*)" + an + ".data) + " + ip;
      }
      if (lv->lhs->kind == ExprKind::Field) {
        std::string dp = emit_lvalue_ptr(lv->lhs);
        out << "farm_bounds_check(" << ip << ", (" << dp << ")->len);\n";
        return "((" + c_type(lv->type) + "*)(" + dp + ")->data) + " + ip;
      }
      std::string arr = emit_expr(lv->lhs);
      std::string at = fresh("da");
      out << "FarmDynArray " << at << " = " << arr << ";\n";
      out << "farm_bounds_check(" << ip << ", " << at << ".len);\n";
      return "((" + c_type(lv->type) + "*)" + at + ".data) + " + ip;
    }
    return "((void*)0)";
  }

  void emit_assign(ExprPtr lv, const std::string& rval) {
    // Scene Euler.order is char[4]; assign from FarmString via helper (validates, trap 104).
    if (rt_math() && lv->kind == ExprKind::Field && lv->name == "order" &&
        lv->lhs && lv->lhs->type && lv->lhs->type->kind == TypeKind::Struct &&
        is_math_type_name(lv->lhs->type->name) && math_base(lv->lhs->type->name) == "Euler") {
      std::string ep = emit_lvalue_ptr(lv->lhs);
      out << "farm_euler_set_order(" << ep << ", " << rval << ");\n";
    } else {
      std::string ptr = emit_lvalue_ptr(lv);
      out << "*(" << ptr << ") = " << rval << ";\n";
    }
    // M3: Object3D.rotation / .quaternion writes (whole field or x/y/z/order/w) mark dirty flags.
    if (uses_scene && lv->kind == ExprKind::Field && lv->lhs && lv->lhs->type &&
        lv->lhs->type->kind == TypeKind::Class && is_object3d_sym(lv->lhs->type->name)) {
      std::string obj_expr = emit_expr(lv->lhs);
      if (lv->name == "rotation")
        out << "((farm_Object3D*)(" << obj_expr << "))->rotation_dirty = 1;\n";
      else if (lv->name == "quaternion")
        out << "((farm_Object3D*)(" << obj_expr << "))->quaternion_dirty = 1;\n";
    }
    if (uses_scene && lv->kind == ExprKind::Field && lv->lhs && lv->lhs->kind == ExprKind::Field &&
        lv->lhs->lhs && lv->lhs->lhs->type && lv->lhs->lhs->type->kind == TypeKind::Class &&
        is_object3d_sym(lv->lhs->lhs->type->name)) {
      std::string parent_field = lv->lhs->name;
      std::string child_field = lv->name;
      std::string obj_expr = emit_expr(lv->lhs->lhs);
      if (parent_field == "rotation" && (child_field == "x" || child_field == "y" || child_field == "z" || child_field == "order"))
        out << "((farm_Object3D*)(" << obj_expr << "))->rotation_dirty = 1;\n";
      else if (parent_field == "quaternion" && (child_field == "x" || child_field == "y" || child_field == "z" || child_field == "w"))
        out << "((farm_Object3D*)(" << obj_expr << "))->quaternion_dirty = 1;\n";
    }
  }

  void emit_compound_assign(ExprPtr lv, TokKind op, ExprPtr rhs) {
    std::string ptr = emit_lvalue_ptr(lv);
    std::string pv = fresh("lv");
    out << c_type(lv->type) << " *" << pv << " = " << ptr << ";\n";
    std::string rv = emit_expr(rhs);
    std::string oldv = fresh("ov");
    out << c_type(lv->type) << " " << oldv << " = *" << pv << ";\n";
    TokKind bop = op==TokKind::PlusEq?TokKind::Plus:
      op==TokKind::MinusEq?TokKind::Minus:
      op==TokKind::StarEq?TokKind::Star:
      op==TokKind::SlashEq?TokKind::Slash:TokKind::Percent;
    std::string result;
    if (lv->type->kind == TypeKind::Int) {
      if (bop == TokKind::Plus)
        result = "((int64_t)((uint64_t)(" + oldv + ")+(uint64_t)(" + rv + ")))";
      else if (bop == TokKind::Minus)
        result = "((int64_t)((uint64_t)(" + oldv + ")-(uint64_t)(" + rv + ")))";
      else if (bop == TokKind::Star)
        result = "((int64_t)((uint64_t)(" + oldv + ")*(uint64_t)(" + rv + ")))";
      else if (bop == TokKind::Slash)
        result = "({ int64_t __b=(" + rv + "); farm_div0_check(__b); " + oldv + "/__b; })";
      else
        result = "({ int64_t __b=(" + rv + "); farm_div0_check(__b); " + oldv + "%__b; })";
    } else if (lv->type->kind == TypeKind::Float) {
      char o = bop==TokKind::Plus?'+':bop==TokKind::Minus?'-':bop==TokKind::Star?'*':'/';
      result = std::string("((") + oldv + ")" + o + "(" + rv + "))";
    } else if (lv->type->kind == TypeKind::String && bop == TokKind::Plus) {
      result = "farm_str_concat(" + oldv + "," + rv + ")";
    } else if (lv->type->kind == TypeKind::Struct) {
      // M2: Try operator overload for compound assignment
      std::string op_name;
      if (bop == TokKind::Plus) op_name = "__farm_op_add";
      else if (bop == TokKind::Minus) op_name = "__farm_op_sub";
      else if (bop == TokKind::Star) op_name = "__farm_op_mul";
      else if (bop == TokKind::Slash) op_name = "__farm_op_div";
      
      if (!op_name.empty()) {
        StructDecl* sd = nullptr;
        for (auto& mm : prog.modules) {
          for (auto& ss : mm.structs) {
            if (ss.c_sym == lv->type->name) {
              sd = &ss;
              break;
            }
          }
          if (sd) break;
        }
        if (sd) {
          for (auto& md : sd->methods) {
            if (md.name == op_name) {
              result = sd->c_sym + "__" + op_name + "(&" + oldv + ", " + rv + ")";
              break;
            }
          }
        }
      }
      if (result.empty()) result = oldv;
    } else {
      result = oldv;
    }
    out << "*" << pv << " = " << result << ";\n";
  }

  static void collect_parallels(StmtPtr s, std::vector<StmtPtr>& outp) {
    if (!s) return;
    if (s->kind == StmtKind::Parallel) {
      outp.push_back(s);
      for (auto& t : s->stmts) collect_parallels(t->then_b, outp);
      return;
    }
    collect_parallels(s->then_b, outp);
    collect_parallels(s->else_b, outp);
    collect_parallels(s->for_init, outp);
    collect_parallels(s->for_update, outp);
    for (auto& x : s->stmts) collect_parallels(x, outp);
  }

  struct ParEnvField {
    std::string name;
    TypePtr type;
    bool is_this = false;
  };

  std::vector<ParEnvField> parallel_env_fields(StmtPtr par) {
    std::vector<ParEnvField> fs;
    std::unordered_set<std::string> seen;
    for (auto& t : par->stmts) {
      for (size_t i = 0; i < t->captures.size(); ++i) {
        if (seen.count(t->captures[i])) continue;
        seen.insert(t->captures[i]);
        TypePtr ty = i < t->capture_types.size() ? t->capture_types[i] : Type::ty_int();
        fs.push_back(ParEnvField{t->captures[i], ty, false});
      }
      if (t->capture_this && !seen.count("\x01this")) {
        seen.insert("\x01this");
        fs.push_back(ParEnvField{"this", t->this_cap_type, true});
      }
    }
    return fs;
  }

  std::string env_struct_name(int pid) {
    return "FarmEnv_P" + std::to_string(pid);
  }
  std::string task_fn_name(int pid, int tidx) {
    return "farm_task_P" + std::to_string(pid) + "_T" + std::to_string(tidx);
  }
  std::string cap_field(const std::string& n) { return "c_" + sanitize(n); }

  void emit_task_function(StmtPtr par, StmtPtr task) {
    int pid = par->parallel_id;
    auto fields = parallel_env_fields(par);
    out << "static void " << task_fn_name(pid, task->task_index) << "(void *env_) {\n";
    if (!fields.empty()) {
      out << "  struct " << env_struct_name(pid) << " *env = (struct " << env_struct_name(pid) << " *)env_;\n";
      out << "  (void)env;\n";
    } else {
      out << "  (void)env_;\n";
    }
    auto saved_val = cap_val;
    auto saved_ptr = cap_ptr;
    auto saved_this = this_c;
    cap_val.clear();
    cap_ptr.clear();
    this_c = "this";
    for (size_t i = 0; i < task->captures.size(); ++i) {
      std::string f = cap_field(task->captures[i]);
      cap_val[task->captures[i]] = "(*(env->" + f + "))";
      cap_ptr[task->captures[i]] = "(env->" + f + ")";
    }
    if (task->capture_this) this_c = "env->this_";
    emit_stmt(task->then_b);
    cap_val = saved_val;
    cap_ptr = saved_ptr;
    this_c = saved_this;
    out << "}\n";
  }

  void emit_all_task_functions() {
    std::vector<StmtPtr> pars;
    for (auto& m : prog.modules) {
      if (is_stdlib_mod(m.path)) continue;
      for (auto& f : m.functions) collect_parallels(f.body, pars);
      for (auto& op : m.operators) collect_parallels(op.body, pars);
      for (auto& c : m.classes) for (auto& md : c.methods) collect_parallels(md.body, pars);
      for (auto& s : m.structs) for (auto& md : s.methods) collect_parallels(md.body, pars);
    }
    std::unordered_set<int> seen_env;
    for (auto& par : pars) {
      if (seen_env.count(par->parallel_id)) continue;
      seen_env.insert(par->parallel_id);
      auto fields = parallel_env_fields(par);
      if (fields.empty()) continue;
      out << "struct " << env_struct_name(par->parallel_id) << " {\n";
      for (auto& f : fields) {
        if (f.is_this) out << "  " << c_type(f.type) << " this_;\n";
        else out << "  " << c_type(f.type) << " *" << cap_field(f.name) << ";\n";
      }
      out << "};\n";
    }
    for (auto& par : pars) {
      for (auto& t : par->stmts)
        out << "static void " << task_fn_name(par->parallel_id, t->task_index) << "(void *env_);\n";
    }
    for (auto& par : pars) {
      for (auto& t : par->stmts) emit_task_function(par, t);
    }
  }

  void emit_parallel_stmt(StmtPtr s) {
    int pid = s->parallel_id;
    auto fields = parallel_env_fields(s);
    std::string envv = fresh("penv");
    if (!fields.empty()) {
      out << "struct " << env_struct_name(pid) << " " << envv << ";\n";
      for (auto& f : fields) {
        if (f.is_this) out << envv << ".this_ = " << this_c << ";\n";
        else out << envv << "." << cap_field(f.name) << " = " << ident_ptr(f.name) << ";\n";
      }
    }
    std::string ta = fresh("ptasks");
    out << "FarmParTask " << ta << "[" << s->stmts.size() << "];\n";
    for (size_t i = 0; i < s->stmts.size(); ++i) {
      auto& t = s->stmts[i];
      out << ta << "[" << i << "].fn = " << task_fn_name(pid, t->task_index) << ";\n";
      if (!fields.empty())
        out << ta << "[" << i << "].env = (void*)&" << envv << ";\n";
      else
        out << ta << "[" << i << "].env = ((void*)0);\n";
    }
    out << "farm_par_run(" << ta << ", " << (int)s->stmts.size() << ");\n";
  }

  void emit_stmt(StmtPtr s) {
    if (!s) return;
    switch (s->kind) {
      case StmtKind::Block:
        out << "{\n";
        for (auto& x : s->stmts) emit_stmt(x);
        out << "}\n";
        break;
      case StmtKind::Let: case StmtKind::Const: {
        std::string init = emit_expr(s->init);
        out << c_type(s->decl_type) << " v_" << s->name << " = " << init << ";\n";
        break;
      }
      case StmtKind::Assign: {
        if (s->assign_op == TokKind::Assign) {
          std::string r = emit_expr(s->rhs);
          emit_assign(s->lhs, r);
        } else {
          emit_compound_assign(s->lhs, s->assign_op, s->rhs);
        }
        break;
      }
      case StmtKind::Expr: {
        std::string e = emit_expr(s->init);
        out << "(void)(" << e << ");\n";
        break;
      }
      case StmtKind::If: {
        std::string c = emit_expr(s->cond);
        out << "if (" << c << ") ";
        emit_stmt(s->then_b);
        if (s->else_b) { out << " else "; emit_stmt(s->else_b); }
        break;
      }
      case StmtKind::While: {
        std::string c = emit_expr(s->cond);
        out << "while (" << c << ") ";
        emit_stmt(s->then_b);
        break;
      }
      case StmtKind::For: {
        out << "{\n";
        if (s->for_init) emit_stmt(s->for_init);
        std::string fc = s->for_cond ? emit_expr(s->for_cond) : "1";
        // Put the update in the C `for` increment so `continue` still runs it.
        std::string inc = "";
        if (s->for_update && s->for_update->kind == StmtKind::Assign &&
            s->for_update->lhs && s->for_update->lhs->kind == ExprKind::Ident) {
          std::string lv = ident_val(s->for_update->lhs->name);
          if (s->for_update->assign_op == TokKind::Assign) {
            inc = lv + " = " + emit_expr(s->for_update->rhs);
          } else {
            std::string rv = emit_expr(s->for_update->rhs);
            TokKind bop = s->for_update->assign_op==TokKind::PlusEq?TokKind::Plus:
              s->for_update->assign_op==TokKind::MinusEq?TokKind::Minus:
              s->for_update->assign_op==TokKind::StarEq?TokKind::Star:
              s->for_update->assign_op==TokKind::SlashEq?TokKind::Slash:TokKind::Percent;
            if (bop == TokKind::Plus)
              inc = lv + " = ((int64_t)((uint64_t)(" + lv + ")+(uint64_t)(" + rv + ")))";
            else if (bop == TokKind::Minus)
              inc = lv + " = ((int64_t)((uint64_t)(" + lv + ")-(uint64_t)(" + rv + ")))";
            else if (bop == TokKind::Star)
              inc = lv + " = ((int64_t)((uint64_t)(" + lv + ")*(uint64_t)(" + rv + ")))";
            else
              inc = "";
          }
        }
        if (!inc.empty()) {
          out << "for (; " << fc << "; " << inc << ") {\n";
          emit_stmt(s->then_b);
          out << "}\n}\n";
        } else {
          out << "for (; " << fc << "; ) {\n";
          emit_stmt(s->then_b);
          if (s->for_update) emit_stmt(s->for_update);
          out << "}\n}\n";
        }
        break;
      }
      case StmtKind::Break: out << "break;\n"; break;
      case StmtKind::Continue: out << "continue;\n"; break;
      case StmtKind::Return:
        if (s->ret) {
          std::string v = emit_expr(s->ret);
          out << "return " << v << ";\n";
        } else out << "return;\n";
        break;
      case StmtKind::Parallel:
        emit_parallel_stmt(s);
        break;
      case StmtKind::Task:
        emit_stmt(s->then_b);
        break;
    }
  }

  std::string emit_all() {
    uses_scene = program_uses_scene(prog);
    uses_physics = program_uses_physics(prog);
    uses_parallel = program_uses_parallel(prog);
    user_imports_math = false;
    for (auto& m : prog.modules) {
      if (is_stdlib_mod(m.path)) continue;
      for (auto& imp : m.imports) if (imp.path == "farmos:math") { user_imports_math = true; break; }
    }
    auto skip_math_structs = [&](const Module& m) {
      return m.path == "farmos:math" && (rt_math() || !user_imports_math);
    };
    auto skip_math_code = [&](const Module& m) {
      return m.path == "farmos:math" && !rt_math() && !user_imports_math;
    };

    out << "/* Generated by farmc */\n";
    out << "#include <stdint.h>\n";
    out << "#include <stdio.h>\n";
    out << "#include <math.h>\n";
    out << "#include \"farm_rt.h\"\n";
    if (uses_scene) {
      out << "#include \"farm_math.h\"\n";
      out << "#include \"farm_scene.h\"\n";
    } else if (uses_physics) {
      out << "#include \"farm_math.h\"\n";
    }
    if (uses_physics) {
      out << "#include \"farm_physics.h\"\n";
    }
    if (uses_parallel) {
      out << "#include \"farm_par.h\"\n";
    }
    out << "\n";
    // Emit fixed array typedefs first (structs may reference them)
    emit_fixed_typedefs();
    // Emit all structs first (so classes can reference them)
    for (auto& m : prog.modules) {
      for (auto& s : m.structs) {
        if (skip_math_structs(m)) continue;
        out << "struct Farm_" << s.c_sym << " {\n";
        for (auto& f : s.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    // Then emit all classes
    for (auto& m : prog.modules) {
      for (auto& c : m.classes) {
        if (is_scene_class_sym(c.c_sym) || is_physics_class_sym(c.c_sym)) continue;
        out << "struct Farm_" << c.c_sym << " {\n";
        for (auto& f : c.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    
    auto is_math_decl = [&](const StructDecl& s) {
      return s.name == "Vector2" || s.name == "Vector3" || s.name == "Vector4" ||
             s.name == "Matrix3" || s.name == "Matrix4" || s.name == "Quaternion" ||
             s.name == "Color" || s.name == "Euler" || s.name == "Ray" ||
             s.name == "Sphere" || s.name == "Box3" || s.name == "RayHit";
    };
    auto math_struct_c = [&](const StructDecl& s) -> std::string {
      if (rt_math() && is_math_decl(s)) return "farm_" + s.name;
      return "struct Farm_" + s.c_sym;
    };

    // M2: Forward declare struct print helpers  
    out << "/* M2 struct print helpers forward declarations */\n";
    for (auto& m : prog.modules) {
      if (skip_math_code(m)) continue;
      for (auto& s : m.structs) {
        out << "void farm_print_" << sanitize(s.name) << "(" << math_struct_c(s) << " v);\n";
        out << "void farm_print_" << sanitize(s.name) << "_ln(" << math_struct_c(s) << " v);\n";
      }
    }
    out << "/* End M2 forward declarations */\n";
    
    for (auto& m : prog.modules) {
      if (skip_math_code(m)) continue;
      for (auto& f : m.functions) {
        out << c_type(f.ret) << " " << f.c_sym << "(";
        for (size_t i=0;i<f.params.size();++i) {
          if (i) out << ", ";
          out << c_type(f.params[i].type) << " v_" << f.params[i].name;
        }
        out << ");\n";
      }
      // M2: Forward declare operators (test 036)
      for (auto& op : m.operators) {
        out << c_type(op.ret) << " " << op.c_sym << "(";
        for (size_t i=0;i<op.params.size();++i) {
          if (i) out << ", ";
          // Pass all parameters by value for operators
          out << c_type(op.params[i].type) << " v_" << op.params[i].name;
        }
        out << ");\n";
      }
      for (auto& c : m.classes) {
        if (is_scene_class_sym(c.c_sym) || is_physics_class_sym(c.c_sym)) continue;
        for (auto& md : c.methods) {
          std::string name = md.is_ctor ? (c.c_sym + "__constructor") : (c.c_sym + "__" + md.name);
          out << (md.is_ctor ? "void" : c_type(md.ret)) << " " << name << "(struct Farm_" << c.c_sym << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ");\n";
        }
      }
      // M2: Forward declare struct methods
      for (auto& s : m.structs) {
        for (auto& md : s.methods) {
          // If method is mutating (returns this), return pointer for chaining
          // Mutating methods: add, multiplyScalar, cross, normalize, lerp, set
          static const std::set<std::string> mutating_methods = 
            {"add", "multiplyScalar", "cross", "normalize", "lerp", "set", "sub", "divide",
             "applyMatrix3", "applyMatrix4", "applyQuaternion", 
             "makeTranslation", "makeScale", "makeRotation", "makeRotationY", "makeRotationX", "multiply", "invert", "transpose", "setHex", "setFromAxisAngle", "crossVectors", "expandByPoint"};
          std::string ret_type = c_type(md.ret);
          if (md.ret->kind == TypeKind::Struct && 
              md.ret->name == s.c_sym && 
              mutating_methods.count(md.name) > 0) {
            ret_type = math_struct_c(s) + "*";
          }
          out << ret_type << " " << s.c_sym << "__" << md.name << "(" << math_struct_c(s) << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ");\n";
        }
      }
    }
    for (auto& m : prog.modules) {
      if (skip_math_code(m) || m.path == "farmos:scene") continue;
      if (m.path == "farmos:physics" && !uses_physics) continue;
      for (auto& c : m.consts)
        out << "static " << c_type(c.type) << " " << c.c_sym << ";\n";
    }

    if (uses_parallel) emit_all_task_functions();

    out << "static void farm_init_globals(void) {\n";
    for (auto& m : prog.modules) {
      if (skip_math_code(m) || m.path == "farmos:scene") continue;
      if (m.path == "farmos:physics" && !uses_physics) continue;
      for (auto& c : m.consts) {
        std::string v = emit_expr(c.init);
        out << "  " << c.c_sym << " = " << v << ";\n";
      }
    }
    out << "}\n";

    for (auto& m : prog.modules) {
      if (skip_math_code(m)) continue;
      for (auto& f : m.functions) {
        out << c_type(f.ret) << " " << f.c_sym << "(";
        for (size_t i=0;i<f.params.size();++i) {
          if (i) out << ", ";
          out << c_type(f.params[i].type) << " v_" << f.params[i].name;
        }
        out << ") ";
        emit_stmt(f.body);
        out << "\n";
      }
      // M2: Define operators (test 036)
      for (auto& op : m.operators) {
        out << c_type(op.ret) << " " << op.c_sym << "(";
        for (size_t i=0;i<op.params.size();++i) {
          if (i) out << ", ";
          // Pass all parameters by value for operators
          out << c_type(op.params[i].type) << " v_" << op.params[i].name;
        }
        out << ") ";
        emit_stmt(op.body);
        out << "\n";
      }
      for (auto& c : m.classes) {
        if (is_scene_class_sym(c.c_sym) || is_physics_class_sym(c.c_sym)) continue;
        for (auto& md : c.methods) {
          std::string name = md.is_ctor ? (c.c_sym + "__constructor") : (c.c_sym + "__" + md.name);
          out << (md.is_ctor ? "void" : c_type(md.ret)) << " " << name << "(struct Farm_" << c.c_sym << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ") ";
          emit_stmt(md.body);
          out << "\n";
        }
      }
      
      // M2: Implement struct methods
      for (auto& s : m.structs) {
        for (auto& md : s.methods) {
          // If method is mutating (returns this), return pointer for chaining
          static const std::set<std::string> mutating_methods = 
            {"add", "multiplyScalar", "cross", "normalize", "lerp", "set", "sub", "divide", 
             "applyMatrix3", "applyMatrix4", "applyQuaternion", 
             "makeTranslation", "makeScale", "makeRotation", "makeRotationY", "makeRotationX", "multiply", "invert", "transpose", "setHex", "setFromAxisAngle", "crossVectors", "expandByPoint"};
          std::string ret_type = c_type(md.ret);
          bool returns_this_ptr = false;
          if (md.ret->kind == TypeKind::Struct && 
              md.ret->name == s.c_sym && 
              mutating_methods.count(md.name) > 0) {
            ret_type = math_struct_c(s) + "*";
            returns_this_ptr = true;
          }
          out << ret_type << " " << s.c_sym << "__" << md.name << "(" << math_struct_c(s) << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ") ";
          
          // Check if this is a farmos:math built-in method with empty body
          bool is_builtin = (md.body->kind == StmtKind::Block && 
                             md.body->stmts.empty() &&
                             (s.name.find("Vector") == 0 || s.name.find("Matrix") == 0 || 
                              s.name == "Quaternion" || s.name == "Color" || s.name == "Ray" || 
                              s.name == "Sphere" || s.name == "Box3")); // Math types
          
          if (is_builtin) {
            // Generate built-in implementation
            out << "{\n";
            std::string el = rt_math() ? "elements" : "f_elements.data";
            std::string qw = rt_math() ? "w" : "f_w";
            std::string v2x = rt_math() ? "x" : "f_x";
            std::string v2y = rt_math() ? "y" : "f_y";
            std::string orig = rt_math() ? "origin" : "f_origin";
            std::string dirn = rt_math() ? "direction" : "f_direction";
            std::string ctr = rt_math() ? "center" : "f_center";
            std::string rad = rt_math() ? "radius" : "f_radius";
            std::string bmin = rt_math() ? "min" : "f_min";
            std::string bmax = rt_math() ? "max" : "f_max";
            std::string hitf = rt_math() ? "hit" : "f_hit";
            std::string ptf = rt_math() ? "point" : "f_point";
            std::string distf = rt_math() ? "distance" : "f_distance";
            auto sty = [&](const std::string& n) {
              return rt_math() ? ("farm_" + n) : ("struct Farm_m1_" + n);
            };
            if (s.name == "Vector3" && md.name == "add") {
              out << "  this->f_x += v_v.f_x;\n";
              out << "  this->f_y += v_v.f_y;\n";
              out << "  this->f_z += v_v.f_z;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "multiplyScalar") {
              out << "  this->f_x *= v_s;\n";
              out << "  this->f_y *= v_s;\n";
              out << "  this->f_z *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "dot") {
              out << "  return this->f_x * v_v.f_x + this->f_y * v_v.f_y + this->f_z * v_v.f_z;\n";
            } else if (s.name == "Vector3" && md.name == "cross") {
              out << "  double ax = this->f_x, ay = this->f_y, az = this->f_z;\n";
              out << "  double bx = v_v.f_x, by = v_v.f_y, bz = v_v.f_z;\n";
              out << "  this->f_x = ay * bz - az * by;\n";
              out << "  this->f_y = az * bx - ax * bz;\n";
              out << "  this->f_z = ax * by - ay * bx;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "length") {
              out << "  return sqrt(this->f_x * this->f_x + this->f_y * this->f_y + this->f_z * this->f_z);\n";
            } else if (s.name == "Vector3" && md.name == "lengthSq") {
              out << "  return this->f_x * this->f_x + this->f_y * this->f_y + this->f_z * this->f_z;\n";
            } else if (s.name == "Vector3" && md.name == "normalize") {
              out << "  double len = sqrt(this->f_x * this->f_x + this->f_y * this->f_y + this->f_z * this->f_z);\n";
              out << "  if (len > 0.0) {\n";
              out << "    this->f_x /= len;\n";
              out << "    this->f_y /= len;\n";
              out << "    this->f_z /= len;\n";
              out << "  }\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "clone") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->f_x;\n";
              out << "  result.f_y = this->f_y;\n";
              out << "  result.f_z = this->f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "lerp") {
              out << "  this->f_x += (v_v.f_x - this->f_x) * v_alpha;\n";
              out << "  this->f_y += (v_v.f_y - this->f_y) * v_alpha;\n";
              out << "  this->f_z += (v_v.f_z - this->f_z) * v_alpha;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "equals") {
              out << "  return (this->f_x == v_v.f_x && this->f_y == v_v.f_y && this->f_z == v_v.f_z) ? 1 : 0;\n";
            } else if (s.name == "Vector3" && md.name == "set") {
              out << "  this->f_x = v_x;\n";
              out << "  this->f_y = v_y;\n";
              out << "  this->f_z = v_z;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "distanceTo") {
              out << "  double dx = this->f_x - v_v.f_x;\n";
              out << "  double dy = this->f_y - v_v.f_y;\n";
              out << "  double dz = this->f_z - v_v.f_z;\n";
              out << "  return sqrt(dx*dx + dy*dy + dz*dz);\n";
            } else if (s.name == "Vector3" && md.name == "distanceToSquared") {
              out << "  double dx = this->f_x - v_v.f_x;\n";
              out << "  double dy = this->f_y - v_v.f_y;\n";
              out << "  double dz = this->f_z - v_v.f_z;\n";
              out << "  return dx*dx + dy*dy + dz*dz;\n";
            } else if (s.name == "Vector2" && md.name == "length") {
              out << "  return sqrt(this->" << v2x << " * this->" << v2x << " + this->" << v2y << " * this->" << v2y << ");\n";
            } else if (s.name == "Vector2" && md.name == "normalize") {
              out << "  double len = sqrt(this->" << v2x << " * this->" << v2x << " + this->" << v2y << " * this->" << v2y << ");\n";
              out << "  if (len > 0.0) { this->" << v2x << " /= len; this->" << v2y << " /= len; }\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "add") {
              out << "  this->" << v2x << " += v_v." << v2x << ";\n";
              out << "  this->" << v2y << " += v_v." << v2y << ";\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "multiplyScalar") {
              out << "  this->" << v2x << " *= v_s;\n";
              out << "  this->" << v2y << " *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "applyMatrix3") {
              // Apply 3x3 matrix to (x, y, 1) homogeneous coordinate
              out << "  double* e = v_m." << el << ";\n";
              out << "  double x = e[0] * this->" << v2x << " + e[3] * this->" << v2y << " + e[6];\n";
              out << "  double y = e[1] * this->" << v2x << " + e[4] * this->" << v2y << " + e[7];\n";
              out << "  this->" << v2x << " = x;\n";
              out << "  this->" << v2y << " = y;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "__farm_op_add") {
              out << "  " << sty("Vector2") << " result;\n";
              out << "  result." << v2x << " = this->" << v2x << " + v_other." << v2x << ";\n";
              out << "  result." << v2y << " = this->" << v2y << " + v_other." << v2y << ";\n";
              out << "  return result;\n";
            } else if (s.name == "Vector4" && md.name == "dot") {
              out << "  return this->f_x * v_v.f_x + this->f_y * v_v.f_y + this->f_z * v_v.f_z + this->" << qw << " * v_v." << qw << ";\n";
            } else if (s.name == "Vector4" && md.name == "multiplyScalar") {
              out << "  this->f_x *= v_s;\n";
              out << "  this->f_y *= v_s;\n";
              out << "  this->f_z *= v_s;\n";
              out << "  this->" << qw << " *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "determinant") {
              // 4x4 matrix determinant (column-major order)
              out << "  double* m = this->" << el << ";\n";
              out << "  double n11=m[0], n12=m[4], n13=m[8],  n14=m[12];\n";
              out << "  double n21=m[1], n22=m[5], n23=m[9],  n24=m[13];\n";
              out << "  double n31=m[2], n32=m[6], n33=m[10], n34=m[14];\n";
              out << "  double n41=m[3], n42=m[7], n43=m[11], n44=m[15];\n";
              out << "  return n41*(n14*n23*n32 - n13*n24*n32 - n14*n22*n33 + n12*n24*n33 + n13*n22*n34 - n12*n23*n34) +\n";
              out << "         n42*(n13*n24*n31 - n14*n23*n31 + n14*n21*n33 - n11*n24*n33 - n13*n21*n34 + n11*n23*n34) +\n";
              out << "         n43*(n14*n22*n31 - n12*n24*n31 - n14*n21*n32 + n11*n24*n32 + n12*n21*n34 - n11*n22*n34) +\n";
              out << "         n44*(n12*n23*n31 - n13*n22*n31 + n13*n21*n32 - n11*n23*n32 - n12*n21*n33 + n11*n22*n33);\n";
            } else if (s.name == "Matrix4" && md.name == "makeTranslation") {
              // Set to translation matrix (column-major)
              out << "  double* m = this->" << el << ";\n";
              out << "  m[0] = 1.0; m[4] = 0.0; m[8]  = 0.0; m[12] = v_x;\n";
              out << "  m[1] = 0.0; m[5] = 1.0; m[9]  = 0.0; m[13] = v_y;\n";
              out << "  m[2] = 0.0; m[6] = 0.0; m[10] = 1.0; m[14] = v_z;\n";
              out << "  m[3] = 0.0; m[7] = 0.0; m[11] = 0.0; m[15] = 1.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeScale") {
              // Set to scale matrix (column-major)
              out << "  double* m = this->" << el << ";\n";
              out << "  m[0] = v_x; m[4] = 0.0;  m[8]  = 0.0;  m[12] = 0.0;\n";
              out << "  m[1] = 0.0; m[5] = v_y;  m[9]  = 0.0;  m[13] = 0.0;\n";
              out << "  m[2] = 0.0; m[6] = 0.0;  m[10] = v_z;  m[14] = 0.0;\n";
              out << "  m[3] = 0.0; m[7] = 0.0;  m[11] = 0.0;  m[15] = 1.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "multiply") {
              // this = this * m (column-major)
              out << "  double* a = this->" << el << ";\n";
              out << "  double* b = v_m." << el << ";\n";
              out << "  double a11=a[0], a12=a[4], a13=a[8],  a14=a[12];\n";
              out << "  double a21=a[1], a22=a[5], a23=a[9],  a24=a[13];\n";
              out << "  double a31=a[2], a32=a[6], a33=a[10], a34=a[14];\n";
              out << "  double a41=a[3], a42=a[7], a43=a[11], a44=a[15];\n";
              out << "  double b11=b[0], b12=b[4], b13=b[8],  b14=b[12];\n";
              out << "  double b21=b[1], b22=b[5], b23=b[9],  b24=b[13];\n";
              out << "  double b31=b[2], b32=b[6], b33=b[10], b34=b[14];\n";
              out << "  double b41=b[3], b42=b[7], b43=b[11], b44=b[15];\n";
              out << "  a[0]  = a11*b11 + a12*b21 + a13*b31 + a14*b41;\n";
              out << "  a[4]  = a11*b12 + a12*b22 + a13*b32 + a14*b42;\n";
              out << "  a[8]  = a11*b13 + a12*b23 + a13*b33 + a14*b43;\n";
              out << "  a[12] = a11*b14 + a12*b24 + a13*b34 + a14*b44;\n";
              out << "  a[1]  = a21*b11 + a22*b21 + a23*b31 + a24*b41;\n";
              out << "  a[5]  = a21*b12 + a22*b22 + a23*b32 + a24*b42;\n";
              out << "  a[9]  = a21*b13 + a22*b23 + a23*b33 + a24*b43;\n";
              out << "  a[13] = a21*b14 + a22*b24 + a23*b34 + a24*b44;\n";
              out << "  a[2]  = a31*b11 + a32*b21 + a33*b31 + a34*b41;\n";
              out << "  a[6]  = a31*b12 + a32*b22 + a33*b32 + a34*b42;\n";
              out << "  a[10] = a31*b13 + a32*b23 + a33*b33 + a34*b43;\n";
              out << "  a[14] = a31*b14 + a32*b24 + a33*b34 + a34*b44;\n";
              out << "  a[3]  = a41*b11 + a42*b21 + a43*b31 + a44*b41;\n";
              out << "  a[7]  = a41*b12 + a42*b22 + a43*b32 + a44*b42;\n";
              out << "  a[11] = a41*b13 + a42*b23 + a43*b33 + a44*b43;\n";
              out << "  a[15] = a41*b14 + a42*b24 + a43*b34 + a44*b44;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "transpose") {
              // Transpose in place
              out << "  double* m = this->" << el << ";\n";
              out << "  double tmp;\n";
              out << "  tmp = m[1]; m[1] = m[4]; m[4] = tmp;\n";
              out << "  tmp = m[2]; m[2] = m[8]; m[8] = tmp;\n";
              out << "  tmp = m[3]; m[3] = m[12]; m[12] = tmp;\n";
              out << "  tmp = m[6]; m[6] = m[9]; m[9] = tmp;\n";
              out << "  tmp = m[7]; m[7] = m[13]; m[13] = tmp;\n";
              out << "  tmp = m[11]; m[11] = m[14]; m[14] = tmp;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "invert") {
              // Invert 4x4 matrix (Gauss-Jordan elimination, simplified)
              out << "  double* m = this->" << el << ";\n";
              out << "  double n11=m[0], n12=m[4], n13=m[8],  n14=m[12];\n";
              out << "  double n21=m[1], n22=m[5], n23=m[9],  n24=m[13];\n";
              out << "  double n31=m[2], n32=m[6], n33=m[10], n34=m[14];\n";
              out << "  double n41=m[3], n42=m[7], n43=m[11], n44=m[15];\n";
              out << "  double t11 = n23*n34*n42 - n24*n33*n42 + n24*n32*n43 - n22*n34*n43 - n23*n32*n44 + n22*n33*n44;\n";
              out << "  double t12 = n14*n33*n42 - n13*n34*n42 - n14*n32*n43 + n12*n34*n43 + n13*n32*n44 - n12*n33*n44;\n";
              out << "  double t13 = n13*n24*n42 - n14*n23*n42 + n14*n22*n43 - n12*n24*n43 - n13*n22*n44 + n12*n23*n44;\n";
              out << "  double t14 = n14*n23*n32 - n13*n24*n32 - n14*n22*n33 + n12*n24*n33 + n13*n22*n34 - n12*n23*n34;\n";
              out << "  double det = n11*t11 + n21*t12 + n31*t13 + n41*t14;\n";
              out << "  if (fabs(det) < 1e-10) { for(int i=0;i<16;i++) m[i]=0.0; return this; }\n";
              out << "  double invDet = 1.0 / det;\n";
              out << "  m[0] = t11 * invDet;\n";
              out << "  m[1] = (n24*n33*n41 - n23*n34*n41 - n24*n31*n43 + n21*n34*n43 + n23*n31*n44 - n21*n33*n44) * invDet;\n";
              out << "  m[2] = (n22*n34*n41 - n24*n32*n41 + n24*n31*n42 - n21*n34*n42 - n22*n31*n44 + n21*n32*n44) * invDet;\n";
              out << "  m[3] = (n23*n32*n41 - n22*n33*n41 - n23*n31*n42 + n21*n33*n42 + n22*n31*n43 - n21*n32*n43) * invDet;\n";
              out << "  m[4] = t12 * invDet;\n";
              out << "  m[5] = (n13*n34*n41 - n14*n33*n41 + n14*n31*n43 - n11*n34*n43 - n13*n31*n44 + n11*n33*n44) * invDet;\n";
              out << "  m[6] = (n14*n32*n41 - n12*n34*n41 - n14*n31*n42 + n11*n34*n42 + n12*n31*n44 - n11*n32*n44) * invDet;\n";
              out << "  m[7] = (n12*n33*n41 - n13*n32*n41 + n13*n31*n42 - n11*n33*n42 - n12*n31*n43 + n11*n32*n43) * invDet;\n";
              out << "  m[8] = t13 * invDet;\n";
              out << "  m[9] = (n14*n23*n41 - n13*n24*n41 - n14*n21*n43 + n11*n24*n43 + n13*n21*n44 - n11*n23*n44) * invDet;\n";
              out << "  m[10] = (n12*n24*n41 - n14*n22*n41 + n14*n21*n42 - n11*n24*n42 - n12*n21*n44 + n11*n22*n44) * invDet;\n";
              out << "  m[11] = (n13*n22*n41 - n12*n23*n41 - n13*n21*n42 + n11*n23*n42 + n12*n21*n43 - n11*n22*n43) * invDet;\n";
              out << "  m[12] = t14 * invDet;\n";
              out << "  m[13] = (n13*n24*n31 - n14*n23*n31 + n14*n21*n33 - n11*n24*n33 - n13*n21*n34 + n11*n23*n34) * invDet;\n";
              out << "  m[14] = (n14*n22*n31 - n12*n24*n31 - n14*n21*n32 + n11*n24*n32 + n12*n21*n34 - n11*n22*n34) * invDet;\n";
              out << "  m[15] = (n12*n23*n31 - n13*n22*n31 + n13*n21*n32 - n11*n23*n32 - n12*n21*n33 + n11*n22*n33) * invDet;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "set") {
              // Set all 16 elements
              out << "  double* m = this->" << el << ";\n";
              for (int i = 0; i < 16; i++) {
                out << "  m[" << i << "] = v_n" << i << ";\n";
              }
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeRotationY") {
              out << "  double c = cos(v_theta);\n";
              out << "  double s = sin(v_theta);\n";
              out << "  double* e = this->" << el << ";\n";
              out << "  e[0] = c;  e[4] = 0; e[8] = s;  e[12] = 0;\n";
              out << "  e[1] = 0;  e[5] = 1; e[9] = 0;  e[13] = 0;\n";
              out << "  e[2] = -s; e[6] = 0; e[10] = c; e[14] = 0;\n";
              out << "  e[3] = 0;  e[7] = 0; e[11] = 0; e[15] = 1;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeRotationX") {
              out << "  double c = cos(v_theta);\n";
              out << "  double s = sin(v_theta);\n";
              out << "  double* e = this->" << el << ";\n";
              out << "  e[0] = 1; e[4] = 0;  e[8] = 0;  e[12] = 0;\n";
              out << "  e[1] = 0; e[5] = c;  e[9] = -s; e[13] = 0;\n";
              out << "  e[2] = 0; e[6] = s;  e[10] = c; e[14] = 0;\n";
              out << "  e[3] = 0; e[7] = 0;  e[11] = 0; e[15] = 1;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "applyMatrix4") {
              // Apply 4x4 matrix to (x, y, z, 1) homogeneous coordinate
              out << "  double* e = v_m." << el << ";\n";
              out << "  double x = this->f_x, y = this->f_y, z = this->f_z;\n";
              out << "  double w = e[3]*x + e[7]*y + e[11]*z + e[15];\n";
              out << "  w = (w != 0.0) ? 1.0 / w : 1.0;\n";
              out << "  this->f_x = (e[0]*x + e[4]*y + e[8]*z  + e[12]) * w;\n";
              out << "  this->f_y = (e[1]*x + e[5]*y + e[9]*z  + e[13]) * w;\n";
              out << "  this->f_z = (e[2]*x + e[6]*y + e[10]*z + e[14]) * w;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "applyQuaternion") {
              // Rotate vector by quaternion
              out << "  double qx = v_q.f_x, qy = v_q.f_y, qz = v_q.f_z, qw = v_q." << qw << ";\n";
              out << "  double x = this->f_x, y = this->f_y, z = this->f_z;\n";
              out << "  double ix =  qw*x + qy*z - qz*y;\n";
              out << "  double iy =  qw*y + qz*x - qx*z;\n";
              out << "  double iz =  qw*z + qx*y - qy*x;\n";
              out << "  double iw = -qx*x - qy*y - qz*z;\n";
              out << "  this->f_x = ix*qw + iw*-qx + iy*-qz - iz*-qy;\n";
              out << "  this->f_y = iy*qw + iw*-qy + iz*-qx - ix*-qz;\n";
              out << "  this->f_z = iz*qw + iw*-qz + ix*-qy - iy*-qx;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "crossVectors") {
              // this = a × b
              out << "  double ax = v_a.f_x, ay = v_a.f_y, az = v_a.f_z;\n";
              out << "  double bx = v_b.f_x, by = v_b.f_y, bz = v_b.f_z;\n";
              out << "  this->f_x = ay*bz - az*by;\n";
              out << "  this->f_y = az*bx - ax*bz;\n";
              out << "  this->f_z = ax*by - ay*bx;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_add") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->f_x + v_other.f_x;\n";
              out << "  result.f_y = this->f_y + v_other.f_y;\n";
              out << "  result.f_z = this->f_z + v_other.f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_sub") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->f_x - v_other.f_x;\n";
              out << "  result.f_y = this->f_y - v_other.f_y;\n";
              out << "  result.f_z = this->f_z - v_other.f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_mul") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->f_x * v_scalar;\n";
              out << "  result.f_y = this->f_y * v_scalar;\n";
              out << "  result.f_z = this->f_z * v_scalar;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_div") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->f_x / v_scalar;\n";
              out << "  result.f_y = this->f_y / v_scalar;\n";
              out << "  result.f_z = this->f_z / v_scalar;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_eq") {
              out << "  return (this->f_x == v_other.f_x && this->f_y == v_other.f_y && this->f_z == v_other.f_z) ? 1 : 0;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_neq") {
              out << "  return (this->f_x != v_other.f_x || this->f_y != v_other.f_y || this->f_z != v_other.f_z) ? 1 : 0;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_neg") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = -this->f_x;\n";
              out << "  result.f_y = -this->f_y;\n";
              out << "  result.f_z = -this->f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_rmul") {
              // scalar * vector (reverse multiply)
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = v_scalar * this->f_x;\n";
              out << "  result.f_y = v_scalar * this->f_y;\n";
              out << "  result.f_z = v_scalar * this->f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Quaternion" && md.name == "multiply") {
              // this = this * q
              out << "  double qax = this->f_x, qay = this->f_y, qaz = this->f_z, qaw = this->" << qw << ";\n";
              out << "  double qbx = v_q.f_x, qby = v_q.f_y, qbz = v_q.f_z, qbw = v_q." << qw << ";\n";
              out << "  this->f_x = qax*qbw + qaw*qbx + qay*qbz - qaz*qby;\n";
              out << "  this->f_y = qay*qbw + qaw*qby + qaz*qbx - qax*qbz;\n";
              out << "  this->f_z = qaz*qbw + qaw*qbz + qax*qby - qay*qbx;\n";
              out << "  this->" << qw << " = qaw*qbw - qax*qbx - qay*qby - qaz*qbz;\n";
              out << "  return this;\n";
            } else if (s.name == "Quaternion" && md.name == "equals") {
              out << "  return (this->f_x == v_q.f_x && this->f_y == v_q.f_y && this->f_z == v_q.f_z && this->" << qw << " == v_q." << qw << ") ? 1 : 0;\n";
            } else if (rt_math() && s.name == "Quaternion" && md.name == "set") {
              out << "  this->f_x = v_x;\n";
              out << "  this->f_y = v_y;\n";
              out << "  this->f_z = v_z;\n";
              out << "  this->" << qw << " = v_w;\n";
              out << "  return this;\n";
            } else if (s.name == "Quaternion" && md.name == "setFromAxisAngle") {
              out << "  double half = v_angle * 0.5;\n";
              out << "  double s = sin(half);\n";
              out << "  this->f_x = v_axis.f_x * s;\n";
              out << "  this->f_y = v_axis.f_y * s;\n";
              out << "  this->f_z = v_axis.f_z * s;\n";
              out << "  this->" << qw << " = cos(half);\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "setHex") {
              std::string cr = rt_math() ? "r" : "f_r";
              std::string cg = rt_math() ? "g" : "f_g";
              std::string cb = rt_math() ? "b" : "f_b";
              out << "  this->" << cr << " = ((v_hex >> 16) & 255) / 255.0;\n";
              out << "  this->" << cg << " = ((v_hex >> 8) & 255) / 255.0;\n";
              out << "  this->" << cb << " = (v_hex & 255) / 255.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "multiplyScalar") {
              std::string cr = rt_math() ? "r" : "f_r";
              std::string cg = rt_math() ? "g" : "f_g";
              std::string cb = rt_math() ? "b" : "f_b";
              out << "  this->" << cr << " *= v_s;\n";
              out << "  this->" << cg << " *= v_s;\n";
              out << "  this->" << cb << " *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "getHex") {
              std::string cr = rt_math() ? "r" : "f_r";
              std::string cg = rt_math() ? "g" : "f_g";
              std::string cb = rt_math() ? "b" : "f_b";
              out << "  int r = (int)(this->" << cr << " * 255.0);\n";
              out << "  int g = (int)(this->" << cg << " * 255.0);\n";
              out << "  int b = (int)(this->" << cb << " * 255.0);\n";
              out << "  return (r << 16) | (g << 8) | b;\n";
            } else if (s.name == "Ray" && md.name == "at") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->" << orig << ".f_x + this->" << dirn << ".f_x * v_t;\n";
              out << "  result.f_y = this->" << orig << ".f_y + this->" << dirn << ".f_y * v_t;\n";
              out << "  result.f_z = this->" << orig << ".f_z + this->" << dirn << ".f_z * v_t;\n";
              out << "  return result;\n";
            } else if (s.name == "Ray" && md.name == "intersectSphere") {
              out << "  " << sty("RayHit") << " result;\n";
              out << "  double dx = this->" << orig << ".f_x - v_s." << ctr << ".f_x;\n";
              out << "  double dy = this->" << orig << ".f_y - v_s." << ctr << ".f_y;\n";
              out << "  double dz = this->" << orig << ".f_z - v_s." << ctr << ".f_z;\n";
              out << "  double a = this->" << dirn << ".f_x*this->" << dirn << ".f_x + this->" << dirn << ".f_y*this->" << dirn << ".f_y + this->" << dirn << ".f_z*this->" << dirn << ".f_z;\n";
              out << "  double b = 2.0 * (dx*this->" << dirn << ".f_x + dy*this->" << dirn << ".f_y + dz*this->" << dirn << ".f_z);\n";
              out << "  double c = dx*dx + dy*dy + dz*dz - v_s." << rad << "*v_s." << rad << ";\n";
              out << "  double disc = b*b - 4.0*a*c;\n";
              out << "  if (disc < 0.0) { result." << hitf << " = 0; result." << ptf << ".f_x = result." << ptf << ".f_y = result." << ptf << ".f_z = 0.0; result." << distf << " = 0.0; return result; }\n";
              out << "  double t = (-b - sqrt(disc)) / (2.0*a);\n";
              out << "  if (t < 0.0) { result." << hitf << " = 0; result." << ptf << ".f_x = result." << ptf << ".f_y = result." << ptf << ".f_z = 0.0; result." << distf << " = 0.0; return result; }\n";
              out << "  result." << hitf << " = 1;\n";
              out << "  result." << ptf << ".f_x = this->" << orig << ".f_x + this->" << dirn << ".f_x * t;\n";
              out << "  result." << ptf << ".f_y = this->" << orig << ".f_y + this->" << dirn << ".f_y * t;\n";
              out << "  result." << ptf << ".f_z = this->" << orig << ".f_z + this->" << dirn << ".f_z * t;\n";
              out << "  result." << distf << " = t;\n";
              out << "  return result;\n";
            } else if (s.name == "Sphere" && md.name == "containsPoint") {
              out << "  double dx = v_p.f_x - this->" << ctr << ".f_x;\n";
              out << "  double dy = v_p.f_y - this->" << ctr << ".f_y;\n";
              out << "  double dz = v_p.f_z - this->" << ctr << ".f_z;\n";
              out << "  return (dx*dx + dy*dy + dz*dz <= this->" << rad << "*this->" << rad << ") ? 1 : 0;\n";
            } else if (s.name == "Sphere" && md.name == "intersectsSphere") {
              out << "  double dx = this->" << ctr << ".f_x - v_s." << ctr << ".f_x;\n";
              out << "  double dy = this->" << ctr << ".f_y - v_s." << ctr << ".f_y;\n";
              out << "  double dz = this->" << ctr << ".f_z - v_s." << ctr << ".f_z;\n";
              out << "  double dist = sqrt(dx*dx + dy*dy + dz*dz);\n";
              out << "  return (dist <= (this->" << rad << " + v_s." << rad << ")) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "isEmpty") {
              out << "  return (this->" << bmax << ".f_x < this->" << bmin << ".f_x || this->" << bmax << ".f_y < this->" << bmin << ".f_y || this->" << bmax << ".f_z < this->" << bmin << ".f_z) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "expandByPoint") {
              out << "  if (v_p.f_x < this->" << bmin << ".f_x) this->" << bmin << ".f_x = v_p.f_x;\n";
              out << "  if (v_p.f_y < this->" << bmin << ".f_y) this->" << bmin << ".f_y = v_p.f_y;\n";
              out << "  if (v_p.f_z < this->" << bmin << ".f_z) this->" << bmin << ".f_z = v_p.f_z;\n";
              out << "  if (v_p.f_x > this->" << bmax << ".f_x) this->" << bmax << ".f_x = v_p.f_x;\n";
              out << "  if (v_p.f_y > this->" << bmax << ".f_y) this->" << bmax << ".f_y = v_p.f_y;\n";
              out << "  if (v_p.f_z > this->" << bmax << ".f_z) this->" << bmax << ".f_z = v_p.f_z;\n";
              out << "  return this;\n";
            } else if (s.name == "Box3" && md.name == "containsPoint") {
              out << "  return (v_p.f_x >= this->" << bmin << ".f_x && v_p.f_x <= this->" << bmax << ".f_x &&\n";
              out << "          v_p.f_y >= this->" << bmin << ".f_y && v_p.f_y <= this->" << bmax << ".f_y &&\n";
              out << "          v_p.f_z >= this->" << bmin << ".f_z && v_p.f_z <= this->" << bmax << ".f_z) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "getCenter") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = (this->" << bmin << ".f_x + this->" << bmax << ".f_x) * 0.5;\n";
              out << "  result.f_y = (this->" << bmin << ".f_y + this->" << bmax << ".f_y) * 0.5;\n";
              out << "  result.f_z = (this->" << bmin << ".f_z + this->" << bmax << ".f_z) * 0.5;\n";
              out << "  return result;\n";
            } else if (s.name == "Box3" && md.name == "getSize") {
              out << "  " << sty("Vector3") << " result;\n";
              out << "  result.f_x = this->" << bmax << ".f_x - this->" << bmin << ".f_x;\n";
              out << "  result.f_y = this->" << bmax << ".f_y - this->" << bmin << ".f_y;\n";
              out << "  result.f_z = this->" << bmax << ".f_z - this->" << bmin << ".f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Box3" && md.name == "intersectsBox") {
              out << "  return (this->" << bmax << ".f_x >= v_box." << bmin << ".f_x && this->" << bmin << ".f_x <= v_box." << bmax << ".f_x &&\n";
              out << "          this->" << bmax << ".f_y >= v_box." << bmin << ".f_y && this->" << bmin << ".f_y <= v_box." << bmax << ".f_y &&\n";
              out << "          this->" << bmax << ".f_z >= v_box." << bmin << ".f_z && this->" << bmin << ".f_z <= v_box." << bmax << ".f_z) ? 1 : 0;\n";
            } else if (s.name == "Matrix3" && md.name == "determinant") {
              // 3x3 matrix determinant (column-major order)
              out << "  double* m = this->" << el << ";\n";
              out << "  double a=m[0], b=m[3], c=m[6];\n";
              out << "  double d=m[1], e=m[4], f=m[7];\n";
              out << "  double g=m[2], h=m[5], i=m[8];\n";
              out << "  return a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);\n";
            } else if (s.name == "Matrix3" && md.name == "makeScale") {
              // Set to scale matrix and return this for chaining
              out << "  double* m = this->" << el << ";\n";
              out << "  m[0] = v_sx; m[3] = 0.0;   m[6] = 0.0;\n";
              out << "  m[1] = 0.0;  m[4] = v_sy;  m[7] = 0.0;\n";
              out << "  m[2] = 0.0;  m[5] = 0.0;   m[8] = 1.0;\n";
              out << "  return this;\n";
            } else {
              // Default implementation based on return type
              if (md.ret->kind == TypeKind::Float) {
                out << "  return 0.0;\n";
              } else if (md.ret->kind == TypeKind::Int) {
                out << "  return 0;\n";
              } else if (md.ret->kind == TypeKind::Bool) {
                out << "  return 0;\n";
              } else if (md.ret->kind == TypeKind::Struct && md.ret->name == s.c_sym) {
                // Check if this is a mutating method
                static const std::set<std::string> mutating_methods = 
                  {"add", "multiplyScalar", "cross", "normalize", "lerp", "set", "sub", "divide", "makeScale"};
                if (mutating_methods.count(md.name) > 0) {
                  out << "  return this;\n";
                } else {
                  out << "  return *this;\n";
                }
              } else {
                out << "  return *this;\n";
              }
            }
            out << "}\n";
          } else {
            emit_stmt(md.body);
            out << "\n";
          }
        }
      }
      
      // M2: Implement struct print helpers
      for (auto& s : m.structs) {
        // print version (no newline)
        out << "void farm_print_" << sanitize(s.name) << "(" << math_struct_c(s) << " v) {\n";
        // Check for types with array fields
        if (s.name == "Matrix4" || s.name == "Matrix3") {
          out << "  printf(\"" << s.name << "{...}\");\n";
        } else {
          // M2: RayHit prints with field names
          bool with_field_names = (s.name == "RayHit");
          out << "  printf(\"" << s.name << "(\");\n";
          out << "  fflush(stdout);\n"; // Flush before unbuffered writes
          for (size_t i = 0; i < s.fields.size(); ++i) {
            if (i > 0) {
              out << "  printf(\", \");\n";
              out << "  fflush(stdout);\n";
            }
            auto& field = s.fields[i];
            if (with_field_names) {
              out << "  printf(\"" << field.name << ": \");\n";
              out << "  fflush(stdout);\n";
            }
            std::string facc = std::string("v.") + (rt_math() && is_math_decl(s)
              ? runtime_field_name(field.name, s.name) : ("f_" + field.name));
            if (rt_math() && s.name == "Euler" && field.name == "order") {
              out << "  printf(\"\\\"%s\\\"\", " << facc << ");\n";
              out << "  fflush(stdout);\n";
            } else if (field.type->kind == TypeKind::Float) {
              out << "  farm_print_float(" << facc << ");\n";
            } else if (field.type->kind == TypeKind::String) {
              out << "  printf(\"\\\"\");\n";  // Opening quote
              out << "  fflush(stdout);\n";
              out << "  farm_print_string(" << facc << ");\n";
              out << "  printf(\"\\\"\");\n";  // Closing quote
              out << "  fflush(stdout);\n";
            } else if (field.type->kind == TypeKind::Bool) {
              out << "  farm_print_bool(" << facc << ");\n";
            } else if (field.type->kind == TypeKind::Struct) {
              // For struct fields, find the base type name without module prefix
              std::string type_name = field.type->name;
              size_t pos = type_name.find_last_of('_');
              if (pos != std::string::npos && type_name.substr(0, pos).find("m") == 0) {
                type_name = type_name.substr(pos + 1);
              }
              out << "  farm_print_" << type_name << "(" << facc << ");\n";
            } else {
              out << "  farm_print_float(" << facc << ");\n"; // Default
            }
          }
          out << "  printf(\")\");\n";
          out << "  fflush(stdout);\n"; // Flush closing paren before function returns
        }
        out << "}\n";
        
        // println version (with newline)
        out << "void farm_print_" << sanitize(s.name) << "_ln(" << math_struct_c(s) << " v) {\n";
        out << "  farm_print_" << sanitize(s.name) << "(v);\n";
        out << "  putchar('\\n');\n";
        out << "  fflush(stdout);\n"; // Flush newline before returning
        out << "}\n";
      }
    }
    std::string main_sym = "fn_m0_main";
    for (auto& mm : prog.modules) if (mm.is_main) {
      for (auto& ff : mm.functions) if (ff.name == "main") { main_sym = ff.c_sym; break; }
    }
    out << "int main(void) {\n  farm_init_globals();\n  int64_t __rc = " << main_sym << "();\n  return (int)__rc;\n}\n";
    return out.str();
  }
};

std::string emit_c(Program& prog) {
  Emitter e{prog};
  return e.emit_all();
}

} // namespace farm

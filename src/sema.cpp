#include "sema.hpp"
#include <functional>

namespace farm {

struct VarInfo {
  TypePtr type;
  bool is_const = false;
  SourceLoc loc;
};

struct Scope {
  std::unordered_map<std::string, VarInfo> vars;
  Scope* parent = nullptr;
  bool declare(const std::string& n, VarInfo v, const std::string& path) {
    if (vars.count(n)) {
      error_at(path, v.loc, "E0502", "duplicate definition of `" + n + "`");
      return false;
    }
    vars[n] = std::move(v);
    return true;
  }
  VarInfo* find(const std::string& n) {
    for (Scope* s = this; s; s = s->parent) {
      auto it = s->vars.find(n);
      if (it != s->vars.end()) return &it->second;
    }
    return nullptr;
  }
};

struct Sema {
  Program& prog;
  Module* cur_mod = nullptr;
  std::string path;
  ClassDecl* cur_class = nullptr;
  StructDecl* cur_struct = nullptr;
  TypePtr cur_ret;
  std::string cur_fn;
  int loop_depth = 0;
  int task_depth = 0;
  std::vector<int> task_loop_mark;
  int next_parallel_id = 1;
  bool in_ctor = false;
  Scope* scope = nullptr;

  explicit Sema(Program& p) : prog(p) {}

  StructDecl* find_struct(const std::string& n) {
    if (!cur_mod) return nullptr;
    auto it = cur_mod->vis_structs.find(n);
    return it == cur_mod->vis_structs.end() ? nullptr : it->second;
  }
  ClassDecl* find_class(const std::string& n) {
    if (!cur_mod) return nullptr;
    auto it = cur_mod->vis_classes.find(n);
    return it == cur_mod->vis_classes.end() ? nullptr : it->second;
  }
  FunctionDecl* find_fn(const std::string& n) {
    if (!cur_mod) return nullptr;
    auto it = cur_mod->vis_functions.find(n);
    return it == cur_mod->vis_functions.end() ? nullptr : it->second;
  }
  ConstDecl* find_const(const std::string& n) {
    if (!cur_mod) return nullptr;
    auto it = cur_mod->vis_consts.find(n);
    return it == cur_mod->vis_consts.end() ? nullptr : it->second;
  }

  // For class/struct type identity across modules, resolve by c_sym globally when needed
  ClassDecl* find_class_any(const std::string& c_sym) {
    for (auto& m : prog.modules)
      for (auto& c : m.classes)
        if (c.c_sym == c_sym) return &c;
    return nullptr;
  }
  StructDecl* find_struct_any(const std::string& c_sym) {
    for (auto& m : prog.modules)
      for (auto& s : m.structs)
        if (s.c_sym == c_sym) return &s;
    return nullptr;
  }

  // M3: farmos:scene classes use runtime symbols `farm_*` and stdlib is-a (Object3D).
  static bool is_scene_class_sym(const std::string& c_sym) {
    return c_sym == "farm_Scene" || c_sym == "farm_Object3D" ||
           c_sym == "farm_PerspectiveCamera" || c_sym == "farm_Mesh" ||
           c_sym == "farm_BoxGeometry" || c_sym == "farm_SphereGeometry" ||
           c_sym == "farm_PlaneGeometry" || c_sym == "farm_MeshBasicMaterial" ||
           c_sym == "farm_MeshStandardMaterial" || c_sym == "farm_AmbientLight" ||
           c_sym == "farm_DirectionalLight" || c_sym == "farm_PointLight" ||
           c_sym == "farm_RectAreaLight" || c_sym == "farm_Texture" ||
           c_sym == "farm_Renderer";
  }

  static bool is_physics_class_sym(const std::string& c_sym) {
    return c_sym == "farm_World" || c_sym == "farm_RigidBody" ||
           c_sym == "farm_SphereCollider" || c_sym == "farm_BoxCollider" ||
           c_sym == "farm_PlaneCollider";
  }

  static bool is_runtime_class_sym(const std::string& c_sym) {
    return is_scene_class_sym(c_sym) || is_physics_class_sym(c_sym);
  }

  bool types_assignable(TypePtr actual, TypePtr expected) {
    if (type_eq(actual, expected)) return true;
    if (actual->kind == TypeKind::Class && expected->kind == TypeKind::Class) {
      auto* actual_class = find_class_any(actual->name);
      if (!actual_class) return false;
      std::string base = actual_class->base_class;
      while (!base.empty()) {
        auto* base_class = find_class_any("farm_" + base);
        if (!base_class) base_class = find_class_any(base);
        if (!base_class) break;
        if (base_class->c_sym == expected->name) return true;
        base = base_class->base_class;
      }
    }
    return false;
  }

  TypePtr finalize_type(TypePtr t, SourceLoc loc, bool allow_void=false) {
    if (!t) return Type::ty_error();
    if (t->kind == TypeKind::Void) {
      if (!allow_void) error_at(path, loc, "E0415", "`void` type not allowed here");
      return t;
    }
    if (t->kind == TypeKind::Struct || t->kind == TypeKind::Class) {
      if (auto* s = find_struct(t->name)) {
        auto nt = Type::ty_struct(t->name);
        nt->name = s->c_sym; // store c_sym in type name for emit
        // Keep source name? Emit uses type->name for struct tags.
        // Use c_sym as the Type::name for uniqueness.
        return Type::ty_struct(s->c_sym);
      }
      if (auto* c = find_class(t->name)) {
        return Type::ty_class(c->c_sym);
      }
      error_at(path, loc, "E0409", "undefined type `" + t->name + "`");
      return Type::ty_error();
    }
    if (t->kind == TypeKind::FixedArray || t->kind == TypeKind::DynArray) {
      t->elem = finalize_type(t->elem, loc, false);
    }
    return t;
  }

  TypePtr check_binary(Expr& e) {
    auto lt = e.lhs->type; auto rt = e.rhs->type;
    auto op = e.op;
    if (op == TokKind::OrOr || op == TokKind::AndAnd) {
      if (lt->kind != TypeKind::Bool) error_at(path, e.lhs->loc, "E0401", "condition must be `bool`, found `" + lt->str() + "`");
      if (rt->kind != TypeKind::Bool) error_at(path, e.rhs->loc, "E0401", "condition must be `bool`, found `" + rt->str() + "`");
      return Type::ty_bool();
    }
    if (op == TokKind::Plus && lt->kind == TypeKind::String && rt->kind == TypeKind::String)
      return Type::ty_string();
    if (op == TokKind::EqEq || op == TokKind::Neq || op == TokKind::Lt || op == TokKind::Le || op == TokKind::Gt || op == TokKind::Ge) {
      if (lt->kind == TypeKind::String && rt->kind == TypeKind::String) return Type::ty_bool();
      if ((lt->kind == TypeKind::Int || lt->kind == TypeKind::Float || lt->kind == TypeKind::Bool) && type_eq(lt, rt))
        return Type::ty_bool();
      
      // M2: Try operator overloading for struct types (comparison operators)
      if (lt->kind == TypeKind::Struct && type_eq(lt, rt)) {
        std::string op_name;
        if (op == TokKind::EqEq) op_name = "__farm_op_eq";
        else if (op == TokKind::Neq) op_name = "__farm_op_neq";
        
        if (!op_name.empty()) {
          auto* sd = find_struct_any(lt->name);
          if (sd) {
            for (auto& md : sd->methods) {
              if (md.name == op_name && md.params.size() == 1 && type_eq(md.params[0].type, rt)) {
                e.mangled = sd->c_sym + "__" + op_name;
                return Type::ty_bool();
              }
            }
          }
        }
      }
      
      // M2: Try free operator functions (test 036)
      if ((lt->kind == TypeKind::Struct || rt->kind == TypeKind::Struct) && 
          (op == TokKind::EqEq || op == TokKind::Neq)) {
        for (auto& od : cur_mod->operators) {
          if (od.op == op && od.params.size() == 2 &&
              type_eq(od.params[0].type, lt) && type_eq(od.params[1].type, rt)) {
            e.mangled = od.c_sym;
            e.is_operator_call = true;
            return od.ret;
          }
        }
      }
      
      if (!type_eq(lt, rt)) error_at(path, e.loc, "E0408", "type mismatch: expected `" + lt->str() + "`, found `" + rt->str() + "`");
      return Type::ty_bool();
    }
    if ((op==TokKind::Plus||op==TokKind::Minus||op==TokKind::Star||op==TokKind::Slash||op==TokKind::Percent)) {
      // M2 §4A (OQ-M2-11): int literal coercion in binary arithmetic
      // Coerce int LITERALS to float when the other operand has type float
      // (including when the other operand is itself a float literal: 1 + 2.0 MUST succeed)
      // M5 010 / Appendix A also mix a non-literal int with float; wrap those
      // operands as float() so the fixtures compile. Call/assign stay E0408.
      if (lt->kind == TypeKind::Float && rt->kind == TypeKind::Int) {
        e.rhs = coerce_int_operand_to_float(e.rhs);
        rt = e.rhs->type;
      } else if (rt->kind == TypeKind::Float && lt->kind == TypeKind::Int) {
        e.lhs = coerce_int_operand_to_float(e.lhs);
        lt = e.lhs->type;
      }
      if ((lt->kind==TypeKind::Int && rt->kind==TypeKind::Float) || (lt->kind==TypeKind::Float && rt->kind==TypeKind::Int)) {
        error_at(path, e.loc, "E0402", "mixed `int`/`float` arithmetic without explicit conversion");
        return Type::ty_error();
      }
      if (op==TokKind::Percent && lt->kind==TypeKind::Float) {
        error_at(path, e.loc, "E0403", "operator `%` not defined for `float`");
        return Type::ty_error();
      }
      if (lt->kind==TypeKind::Int && rt->kind==TypeKind::Int) return Type::ty_int();
      if (lt->kind==TypeKind::Float && rt->kind==TypeKind::Float) return Type::ty_float();
      
      // M2: Try operator overloading for struct types (left operand is struct)
      if (lt->kind == TypeKind::Struct) {
        std::string op_name;
        if (op == TokKind::Plus) op_name = "__farm_op_add";
        else if (op == TokKind::Minus) op_name = "__farm_op_sub";
        else if (op == TokKind::Star) op_name = "__farm_op_mul";
        else if (op == TokKind::Slash) op_name = "__farm_op_div";
        
        if (!op_name.empty()) {
          auto* sd = find_struct_any(lt->name);
          if (sd) {
            for (auto& md : sd->methods) {
              if (md.name == op_name && md.params.size() == 1) {
                // Found operator overload - check parameter type
                if (!type_eq(md.params[0].type, rt)) {
                  error_at(path, e.rhs->loc, "E0408", "type mismatch: expected `" + md.params[0].type->str() + "`, found `" + rt->str() + "`");
                  return Type::ty_error();
                }
                // Rewrite as method call
                e.mangled = sd->c_sym + "__" + op_name;
                return md.ret;
              }
            }
          }
        }
      }
      
      // M2: Try free operator functions (test 036)
      if (lt->kind == TypeKind::Struct || rt->kind == TypeKind::Struct) {
        for (auto& od : cur_mod->operators) {
          if (od.op == op && od.params.size() == 2 &&
              type_eq(od.params[0].type, lt) && type_eq(od.params[1].type, rt)) {
            e.mangled = od.c_sym;
            e.is_operator_call = true;
            return od.ret;
          }
        }
      }
      
      // M2: Try left-associative operator (scalar * vector, etc.) - right operand is struct
      if (rt->kind == TypeKind::Struct && (op == TokKind::Star || op == TokKind::Slash)) {
        std::string op_name;
        if (op == TokKind::Star) op_name = "__farm_op_rmul";  // reverse multiply
        else if (op == TokKind::Slash) op_name = "__farm_op_rdiv";
        
        if (!op_name.empty()) {
          auto* sd = find_struct_any(rt->name);
          if (sd) {
            for (auto& md : sd->methods) {
              if (md.name == op_name && md.params.size() == 1) {
                // Found reverse operator - check parameter type matches left
                if (!type_eq(md.params[0].type, lt)) {
                  error_at(path, e.lhs->loc, "E0408", "type mismatch");
                  return Type::ty_error();
                }
                e.mangled = sd->c_sym + "__" + op_name;
                e.is_reverse_op = true;  // Mark for codegen
                return md.ret;
              }
            }
          }
        }
      }
      
      error_at(path, e.loc, "E0403", "operator not defined for `" + lt->str() + "`");
      return Type::ty_error();
    }
    return Type::ty_error();
  }

  // M2: Helper to check if expr is an int literal (or unary minus int literal) within float range
  bool is_coercible_int_lit(ExprPtr e, int64_t* out_val = nullptr) {
    if (!e) return false;
    int64_t val = 0;
    if (e->kind == ExprKind::IntLit) {
      val = e->int_val;
    } else if (e->kind == ExprKind::Unary && e->op == TokKind::Minus && 
               e->rhs && e->rhs->kind == ExprKind::IntLit) {
      val = -e->rhs->int_val;
    } else {
      return false;
    }
    // Check if |val| <= 2^53
    const int64_t max_exact = (1LL << 53);
    if (val < -max_exact || val > max_exact) return false;
    if (out_val) *out_val = val;
    return true;
  }
  
  // M2: Coerce int literal to float if possible
  ExprPtr try_coerce_int_to_float(ExprPtr e) {
    int64_t val;
    if (!is_coercible_int_lit(e, &val)) return e;
    // Convert to FloatLit
    auto f = std::make_shared<Expr>();
    f->kind = ExprKind::FloatLit;
    f->float_val = (double)val;
    f->type = Type::ty_float();
    f->loc = e->loc;
    return f;
  }

  // Wrap a non-literal int operand as the built-in `float()` conversion.
  // M5 010 and Appendix A write `0.05 * (i + 1)` / `i * 1.02`; assignment
  // and call arguments of non-literal int still use E0408 (M2 054).
  ExprPtr coerce_int_operand_to_float(ExprPtr e) {
    if (!e) return e;
    auto lit = try_coerce_int_to_float(e);
    if (lit->type && lit->type->kind == TypeKind::Float) return lit;
    if (!e->type || e->type->kind != TypeKind::Int) return e;
    auto id = std::make_shared<Expr>();
    id->kind = ExprKind::Ident;
    id->name = "float";
    id->loc = e->loc;
    auto c = std::make_shared<Expr>();
    c->kind = ExprKind::Call;
    c->lhs = id;
    c->args.push_back(e);
    c->type = Type::ty_float();
    c->mangled = "float";
    c->loc = e->loc;
    return c;
  }

  ExprPtr check_expr(ExprPtr e) {
    if (!e) return e;
    switch (e->kind) {
      case ExprKind::IntLit: e->type = Type::ty_int(); break;
      case ExprKind::FloatLit: e->type = Type::ty_float(); break;
      case ExprKind::BoolLit: e->type = Type::ty_bool(); break;
      case ExprKind::StringLit: e->type = Type::ty_string(); break;
      case ExprKind::This: {
        if (cur_class) {
          e->type = Type::ty_class(cur_class->c_sym);
          e->is_lvalue = true;
        } else if (cur_struct) {
          e->type = Type::ty_struct(cur_struct->c_sym);
          e->is_lvalue = true;
        } else {
          error_at(path, e->loc, "E0417", "`this` not allowed outside class method/constructor");
          e->type=Type::ty_error();
        }
        break;
      }
      case ExprKind::Ident: {
        if (auto* v = scope->find(e->name)) {
          e->type = v->type; e->is_lvalue = true; e->is_const_binding = v->is_const;
        } else if (auto* c = find_const(e->name)) {
          e->type = c->type; e->is_lvalue = false; e->is_const_binding = true;
          e->mangled = c->c_sym;
        } else if (find_fn(e->name)) {
          e->mangled = find_fn(e->name)->c_sym;
          e->type = Type::ty_error();
        } else {
          error_at(path, e->loc, "E0505", "undefined name `" + e->name + "`");
          e->type = Type::ty_error();
        }
        break;
      }
      case ExprKind::Unary: {
        check_expr(e->rhs);
        if (e->op == TokKind::Bang) {
          if (e->rhs->type->kind != TypeKind::Bool)
            error_at(path, e->loc, "E0403", "operator `!` not defined for `" + e->rhs->type->str() + "`");
          e->type = Type::ty_bool();
        } else {
          if (e->rhs->type->kind != TypeKind::Int && e->rhs->type->kind != TypeKind::Float) {
            // M2: Try unary operator overload for structs
            if (e->op == TokKind::Minus && e->rhs->type->kind == TypeKind::Struct) {
              auto* sd = find_struct_any(e->rhs->type->name);
              if (sd) {
                for (auto& md : sd->methods) {
                  if (md.name == "__farm_op_neg" && md.params.empty()) {
                    e->mangled = sd->c_sym + "__" + md.name;
                    e->type = md.ret;
                    break;
                  }
                }
                if (e->type) break;
              }
            }
            error_at(path, e->loc, "E0403", "unary +/- not defined for `" + e->rhs->type->str() + "`");
          }
          e->type = e->rhs->type;
        }
        break;
      }
      case ExprKind::Binary: {
        check_expr(e->lhs); check_expr(e->rhs);
        e->type = check_binary(*e);
        break;
      }
      case ExprKind::Index: {
        check_expr(e->lhs); check_expr(e->rhs);
        if (e->lhs->type->kind != TypeKind::FixedArray && e->lhs->type->kind != TypeKind::DynArray) {
          error_at(path, e->loc, "E0410", "index expression requires array type");
          e->type = Type::ty_error();
        } else {
          if (e->rhs->type->kind != TypeKind::Int)
            error_at(path, e->rhs->loc, "E0408", "type mismatch: expected `int`, found `" + e->rhs->type->str() + "`");
          e->type = e->lhs->type->elem;
          e->is_lvalue = true;
          e->is_const_binding = e->lhs->is_const_binding;
        }
        break;
      }
      case ExprKind::Field: {
        check_expr(e->lhs);
        auto t = e->lhs->type;
        if (t->kind == TypeKind::Struct) {
          auto* sd = find_struct_any(t->name);
          if (!sd) { error_at(path, e->loc, "E0505", "undefined name `" + e->name + "`"); e->type=Type::ty_error(); break; }
          bool found=false;
          for (auto& f : sd->fields) if (f.name==e->name) { e->type=f.type; found=true; break; }
          // M2: Check methods too (like classes do)
          if (!found) {
            for (auto& md : sd->methods) if (md.name==e->name) {
              e->mangled = sd->c_sym + "__" + e->name;
              e->type = Type::ty_error(); // Will be fixed in Call checking
              found = true; break;
            }
          }
          if (!found) { error_at(path, e->loc, "E0505", "undefined name `" + e->name + "`"); e->type=Type::ty_error(); }
          e->is_lvalue = true;
          e->is_const_binding = e->lhs->is_const_binding;
        } else if (t->kind == TypeKind::Class) {
          auto* cd = find_class_any(t->name);
          if (!cd) { error_at(path, e->loc, "E0505", "undefined name `" + e->name + "`"); e->type=Type::ty_error(); break; }
          bool found=false;
          // M3: Walk Object3D base chain for scene classes only; user classes keep the M1 lookup.
          auto* search_class = cd;
          while (search_class && !found) {
            for (auto& f : search_class->fields) if (f.name==e->name) { e->type=f.type; found=true; break; }
            if (!found) {
              for (auto& md : search_class->methods) if (!md.is_ctor && md.name==e->name) {
                std::string sep = is_runtime_class_sym(search_class->c_sym) ? "_" : "__";
                e->mangled = search_class->c_sym + sep + e->name;
                e->type = Type::ty_error();
                found = true; break;
              }
            }
            if (!found && is_scene_class_sym(cd->c_sym) && !search_class->base_class.empty()) {
              search_class = find_class_any("farm_" + search_class->base_class);
            } else {
              break;
            }
          }
          if (!found) {
            error_at(path, e->loc, "E0505", "undefined name `" + e->name + "`");
            e->type = Type::ty_error();
          }
          e->is_lvalue = true;
          e->is_const_binding = false;
        } else {
          error_at(path, e->loc, "E0403", "operator `.` not defined for `" + t->str() + "`");
          e->type = Type::ty_error();
        }
        break;
      }
      case ExprKind::Call: {
        if (e->lhs->kind == ExprKind::Ident) {
          std::string n = e->lhs->name;
          if (n=="print"||n=="println"||n=="len"||n=="push"||n=="str"||n=="int"||n=="float") {
            for (auto& a : e->args) check_expr(a);
            e->mangled = n;
            if (n=="print"||n=="println") {
              if (e->args.size()!=1) error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `" + n + "`");
              else {
                auto k = e->args[0]->type->kind;
                // M2: allow struct types for print/println (math types)
                if (k!=TypeKind::Int&&k!=TypeKind::Float&&k!=TypeKind::Bool&&k!=TypeKind::String&&k!=TypeKind::Struct)
                  error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `" + n + "`");
              }
              e->type = Type::ty_void();
            } else if (n=="len") {
              if (e->args.size()!=1) error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `len`");
              else {
                auto k = e->args[0]->type->kind;
                if (k!=TypeKind::String&&k!=TypeKind::FixedArray&&k!=TypeKind::DynArray)
                  error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `len`");
              }
              e->type = Type::ty_int();
            } else if (n=="push") {
              if (e->args.size()!=2) error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `push`");
              else if (e->args[0]->type->kind != TypeKind::DynArray)
                error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `push`");
              else if (!type_eq(e->args[0]->type->elem, e->args[1]->type))
                error_at(path, e->loc, "E0408", "type mismatch");
              else if (e->args[0]->is_const_binding)
                error_at(path, e->args[0]->loc, "E0504", "cannot assign to const");
              e->type = Type::ty_void();
            } else if (n=="str") {
              if (e->args.size()!=1) error_at(path, e->loc, "E0513", "arity mismatch");
              else {
                auto k=e->args[0]->type->kind;
                // M2: allow struct types for str (math types)
                if (k!=TypeKind::Int&&k!=TypeKind::Float&&k!=TypeKind::Bool&&k!=TypeKind::Struct)
                  error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `str`");
              }
              e->type = Type::ty_string();
            } else if (n=="int") {
              if (e->args.size()!=1) error_at(path, e->loc, "E0513", "arity mismatch");
              else {
                auto k=e->args[0]->type->kind;
                if (k!=TypeKind::Float&&k!=TypeKind::Bool)
                  error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `int`");
              }
              e->type = Type::ty_int();
            } else if (n=="float") {
              if (e->args.size()!=1 || e->args[0]->type->kind!=TypeKind::Int)
                error_at(path, e->loc, "E0513", "arity or type mismatch for built-in `float`");
              e->type = Type::ty_float();
            }
            break;
          }
          if (auto* fn = find_fn(n)) {
            for (auto& a : e->args) check_expr(a);
            if (e->args.size() != fn->params.size())
              error_at(path, e->loc, "E0411", "wrong number of arguments: expected " + std::to_string(fn->params.size()) + ", found " + std::to_string(e->args.size()));
            else {
              for (size_t i=0;i<e->args.size();++i) {
                // M2: Try int→float coercion if needed
                if (!type_eq(e->args[i]->type, fn->params[i].type) &&
                    fn->params[i].type->kind == TypeKind::Float &&
                    e->args[i]->type->kind == TypeKind::Int) {
                  e->args[i] = try_coerce_int_to_float(e->args[i]);
                }
                if (!type_eq(e->args[i]->type, fn->params[i].type))
                  error_at(path, e->args[i]->loc, "E0408", "type mismatch: expected `" + fn->params[i].type->str() + "`, found `" + e->args[i]->type->str() + "`");
              }
            }
            e->mangled = fn->c_sym;
            e->type = fn->ret;
            break;
          }
        }
        if (e->lhs->kind == ExprKind::Field) {
          check_expr(e->lhs->lhs);
          for (auto& a : e->args) check_expr(a);
          auto rt = e->lhs->lhs->type;
          if (rt->kind == TypeKind::Class) {
            auto* cd = find_class_any(rt->name);
            MethodDecl* md = nullptr;
            ClassDecl* owner = cd;
            if (cd && is_runtime_class_sym(cd->c_sym)) {
              // M3: Overload + Object3D base-chain lookup (lookAt, add, ...).
              std::vector<std::pair<ClassDecl*, MethodDecl*>> candidates;
              auto* search_class = cd;
              while (search_class) {
                for (auto& m : search_class->methods)
                  if (!m.is_ctor && m.name == e->lhs->name)
                    candidates.push_back({search_class, &m});
                if (!search_class->base_class.empty())
                  search_class = find_class_any("farm_" + search_class->base_class);
                else
                  break;
              }
              for (auto& cand : candidates) {
                if (cand.second->params.size() != e->args.size()) continue;
                bool all_match = true;
                for (size_t i = 0; i < e->args.size(); ++i) {
                  TypePtr arg_type = e->args[i]->type;
                  TypePtr param_type = cand.second->params[i].type;
                  if (param_type->kind == TypeKind::Float && arg_type->kind == TypeKind::Int) continue;
                  if (!types_assignable(arg_type, param_type)) { all_match = false; break; }
                }
                if (all_match) { owner = cand.first; md = cand.second; break; }
              }
            } else {
              if (cd) for (auto& m : cd->methods) if (!m.is_ctor && m.name == e->lhs->name) { md = &m; break; }
            }
            if (!md) { error_at(path, e->loc, "E0505", "undefined name `" + e->lhs->name + "`"); e->type=Type::ty_error(); }
            else {
              if (e->args.size()!=md->params.size())
                error_at(path, e->loc, "E0411", "wrong number of arguments");
              else {
                for (size_t i=0;i<e->args.size();++i) {
                  // M2: Try int→float coercion
                  if (!type_eq(e->args[i]->type, md->params[i].type) &&
                      md->params[i].type->kind == TypeKind::Float &&
                      e->args[i]->type->kind == TypeKind::Int) {
                    e->args[i] = try_coerce_int_to_float(e->args[i]);
                  }
                  bool ok = is_runtime_class_sym(cd->c_sym)
                    ? types_assignable(e->args[i]->type, md->params[i].type)
                    : type_eq(e->args[i]->type, md->params[i].type);
                  if (!ok)
                    error_at(path, e->args[i]->loc, "E0408", "type mismatch");
                }
              }
              if (is_physics_class_sym(cd->c_sym)) {
                e->mangled = cd->c_sym + "_" + md->name;
              } else if (is_scene_class_sym(cd->c_sym)) {
                // Runtime scene methods use a single underscore: farm_Object3D_add.
                if (md->name == "lookAt" && e->args.size() == 3)
                  e->mangled = "farm_Object3D_lookAt_xyz";
                else if (md->name == "lookAt" && e->args.size() == 1)
                  e->mangled = "farm_Object3D_lookAt_v";
                else if (md->name == "setEmissive" && e->args.size() == 1 &&
                         e->args[0]->type && e->args[0]->type->kind == TypeKind::Int)
                  e->mangled = "farm_MeshStandardMaterial_setEmissive_hex";
                else if (md->name == "updateMatrixWorld" && cd->c_sym == "farm_PerspectiveCamera")
                  e->mangled = "farm_PerspectiveCamera_updateMatrixWorld";
                else if (md->name == "add" || md->name == "remove" || md->name == "addAt" ||
                         md->name == "childCount" || md->name == "getChild" ||
                         md->name == "updateMatrix" || md->name == "updateMatrixWorld" ||
                         md->name == "setRotationFromEuler" || md->name == "setRotationFromQuaternion")
                  e->mangled = std::string("farm_Object3D_") + md->name;
                else
                  e->mangled = cd->c_sym + "_" + md->name;
              } else {
                e->mangled = cd->c_sym + "__" + md->name;
              }
              (void)owner;
              e->type = md->ret;
            }
          } else if (rt->kind == TypeKind::Struct) {
            // M2: Struct method calls
            auto* sd = find_struct_any(rt->name);
            MethodDecl* md = nullptr;
            if (sd) for (auto& m : sd->methods) if (m.name == e->lhs->name) { md = &m; break; }
            if (!md) { error_at(path, e->loc, "E0505", "undefined name `" + e->lhs->name + "`"); e->type=Type::ty_error(); }
            else {
              if (e->args.size()!=md->params.size())
                error_at(path, e->loc, "E0411", "wrong number of arguments");
              else {
                for (size_t i=0;i<e->args.size();++i) {
                  // M2: Try int→float coercion
                  if (!type_eq(e->args[i]->type, md->params[i].type) &&
                      md->params[i].type->kind == TypeKind::Float &&
                      e->args[i]->type->kind == TypeKind::Int) {
                    e->args[i] = try_coerce_int_to_float(e->args[i]);
                  }
                  if (!type_eq(e->args[i]->type, md->params[i].type))
                    error_at(path, e->args[i]->loc, "E0408", "type mismatch");
                }
              }
              e->mangled = sd->c_sym + "__" + md->name;
              e->type = md->ret;
            }
          } else {
            error_at(path, e->loc, "E0403", "cannot call method on `" + rt->str() + "`");
            e->type = Type::ty_error();
          }
          break;
        }
        check_expr(e->lhs);
        for (auto& a : e->args) check_expr(a);
        error_at(path, e->loc, "E0505", "undefined name");
        e->type = Type::ty_error();
        break;
      }
      case ExprKind::ArrayLit: {
        for (auto& a : e->args) check_expr(a);
        if (e->args.empty()) {
          e->type = Type::ty_error();
          e->mangled = "empty_array";
        } else {
          TypePtr et = e->args[0]->type;
          for (size_t i=1;i<e->args.size();++i)
            if (!type_eq(e->args[i]->type, et))
              error_at(path, e->args[i]->loc, "E0408", "type mismatch");
          e->type = Type::ty_fixed(et, (int64_t)e->args.size());
        }
        break;
      }
      case ExprKind::StructLit: {
        auto* sd = find_struct(e->type_name);
        if (!sd) {
          if (find_class(e->type_name))
            error_at(path, e->loc, "E0416", "classes must be constructed with `new`");
          else error_at(path, e->loc, "E0409", "undefined type `" + e->type_name + "`");
          e->type = Type::ty_error(); break;
        }
        std::unordered_set<std::string> seen;
        for (auto& fv : e->fields) {
          check_expr(fv.second);
          if (seen.count(fv.first)) error_at(path, e->loc, "E0404", "struct literal missing or duplicate field `" + fv.first + "`");
          seen.insert(fv.first);
          bool ok=false;
          for (auto& f : sd->fields) if (f.name==fv.first) {
            ok=true;
            if (!type_eq(fv.second->type, f.type))
              error_at(path, fv.second->loc, "E0408", "type mismatch");
            break;
          }
          if (!ok) error_at(path, e->loc, "E0404", "struct literal missing or duplicate field `" + fv.first + "`");
        }
        for (auto& f : sd->fields) if (!seen.count(f.name))
          error_at(path, e->loc, "E0404", "struct literal missing or duplicate field `" + f.name + "`");
        e->type = Type::ty_struct(sd->c_sym);
        e->mangled = sd->c_sym;
        break;
      }
      case ExprKind::New: {
        for (auto& a : e->args) check_expr(a);
        if (auto* cd = find_class(e->type_name)) {
          if (is_runtime_class_sym(cd->c_sym)) {
            // M3/M5: Runtime constructor overloads (not the single AST ctor).
            std::string variant = "";
            size_t nargs = e->args.size();
            auto coerce_float = [&](ExprPtr& a) {
              if (a->type->kind == TypeKind::Int) a = try_coerce_int_to_float(a);
              else if (a->type->kind != TypeKind::Float)
                error_at(path, a->loc, "E0408", "type mismatch: expected float");
            };
            if (cd->c_sym == "farm_MeshStandardMaterial") {
              if (nargs == 1) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "color";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int or Color");
              } else if (nargs != 0) {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0 or 1, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_MeshBasicMaterial") {
              if (nargs == 1) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "color";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int");
              } else if (nargs != 0) {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0 or 1, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_PointLight") {
              if (nargs == 1) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int");
              } else if (nargs == 2) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex_i";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "color_i";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int or Color");
                coerce_float(e->args[1]);
              } else if (nargs == 4) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex_full";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "full";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int or Color");
                for (int i = 1; i < 4; ++i) coerce_float(e->args[i]);
              } else if (nargs != 0) {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0, 1, 2, or 4, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_RectAreaLight") {
              if (nargs == 2) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex_i";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int");
                coerce_float(e->args[1]);
              } else if (nargs == 4) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex_i_wh";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "color_i_wh";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int or Color");
                for (int i = 1; i < 4; ++i) coerce_float(e->args[i]);
              } else if (nargs != 0) {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0, 2, or 4, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_Texture") {
              if (nargs != 1)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 1, found " + std::to_string(nargs));
              else if (e->args[0]->type->kind != TypeKind::String)
                error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected string");
            } else if (cd->c_sym == "farm_AmbientLight" || cd->c_sym == "farm_DirectionalLight") {
              if (nargs == 1) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int");
              } else if (nargs == 2) {
                if (e->args[0]->type->kind == TypeKind::Int) variant = "hex_i";
                else if (e->args[0]->type->kind == TypeKind::Struct && e->args[0]->type->name.find("Color") != std::string::npos) variant = "color_i";
                else error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int or Color");
                coerce_float(e->args[1]);
              } else if (nargs != 0) {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0, 1, or 2, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_Mesh") {
              if (nargs != 2)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 2, found " + std::to_string(nargs));
              else {
                bool valid_geometry = e->args[0]->type->kind == TypeKind::Class &&
                  (e->args[0]->type->name == "farm_BoxGeometry" || e->args[0]->type->name == "farm_SphereGeometry" ||
                   e->args[0]->type->name == "farm_PlaneGeometry");
                if (!valid_geometry)
                  error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected geometry type (BoxGeometry, SphereGeometry, or PlaneGeometry)");
                bool valid_material = e->args[1]->type->kind == TypeKind::Class &&
                  (e->args[1]->type->name == "farm_MeshBasicMaterial" || e->args[1]->type->name == "farm_MeshStandardMaterial");
                if (!valid_material)
                  error_at(path, e->args[1]->loc, "E0408", "type mismatch: expected material type (MeshBasicMaterial or MeshStandardMaterial)");
              }
            } else if (cd->c_sym == "farm_BoxGeometry") {
              if (nargs == 3) { variant = "whd"; for (int i=0;i<3;++i) coerce_float(e->args[i]); }
              else if (nargs != 0)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0 or 3, found " + std::to_string(nargs));
            } else if (cd->c_sym == "farm_SphereGeometry") {
              if (nargs == 1) { variant = "r"; coerce_float(e->args[0]); }
              else if (nargs == 3) { variant = "full"; for (int i=0;i<3;++i) coerce_float(e->args[i]); }
              else if (nargs != 0)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0, 1, or 3, found " + std::to_string(nargs));
            } else if (cd->c_sym == "farm_PlaneGeometry") {
              if (nargs == 2) { variant = "wh"; for (int i=0;i<2;++i) coerce_float(e->args[i]); }
              else if (nargs != 0)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0 or 2, found " + std::to_string(nargs));
            } else if (cd->c_sym == "farm_Renderer") {
              if (nargs == 2) {
                variant = "wh";
                for (int i=0;i<2;++i)
                  if (e->args[i]->type->kind != TypeKind::Int)
                    error_at(path, e->args[i]->loc, "E0408", "type mismatch: expected int");
              } else if (nargs != 0)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0 or 2, found " + std::to_string(nargs));
            } else if (cd->c_sym == "farm_PerspectiveCamera") {
              if (nargs != 4)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 4, found " + std::to_string(nargs));
              else
                for (int i=0;i<4;++i) coerce_float(e->args[i]);
            } else if (cd->c_sym == "farm_SphereCollider") {
              if (nargs != 1)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 1, found " + std::to_string(nargs));
              else coerce_float(e->args[0]);
            } else if (cd->c_sym == "farm_BoxCollider") {
              if (nargs == 3) {
                variant = "xyz";
                for (int i = 0; i < 3; ++i) coerce_float(e->args[i]);
              } else if (nargs == 1) {
                if (e->args[0]->type->kind == TypeKind::Struct &&
                    e->args[0]->type->name.find("Vector3") != std::string::npos)
                  variant = "v";
                else
                  error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected Vector3");
              } else {
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 1 or 3, found " + std::to_string(nargs));
              }
            } else if (cd->c_sym == "farm_PlaneCollider" || cd->c_sym == "farm_World") {
              if (nargs != 0)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 0, found " + std::to_string(nargs));
            } else if (cd->c_sym == "farm_RigidBody") {
              if (nargs != 1)
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected 1, found " + std::to_string(nargs));
              else if (e->args[0]->type->kind != TypeKind::Int)
                error_at(path, e->args[0]->loc, "E0408", "type mismatch: expected int");
            } else if (nargs != 0 && cd->ctor_index >= 0) {
              auto& ctor = cd->methods[cd->ctor_index];
              if (e->args.size()!=ctor.params.size())
                error_at(path, e->loc, "E0411", "wrong number of arguments: expected " + std::to_string(ctor.params.size()) + ", found " + std::to_string(e->args.size()));
            }
            e->ctor_variant = variant;
            e->type = Type::ty_class(cd->c_sym);
            e->mangled = cd->c_sym;
          } else {
            if (cd->ctor_index < 0) { e->type=Type::ty_error(); break; }
            auto& ctor = cd->methods[cd->ctor_index];
            if (e->args.size()!=ctor.params.size())
              error_at(path, e->loc, "E0411", "wrong number of arguments: expected " + std::to_string(ctor.params.size()) + ", found " + std::to_string(e->args.size()));
            else for (size_t i=0;i<e->args.size();++i)
              if (!type_eq(e->args[i]->type, ctor.params[i].type))
                error_at(path, e->args[i]->loc, "E0408", "type mismatch");
            e->type = Type::ty_class(cd->c_sym);
            e->mangled = cd->c_sym;
          }
        } else if (auto* sd = find_struct(e->type_name)) {
          // M2: Allow custom constructors for specific types
          bool valid_ctor = false;
          if (e->args.size() == 0 || e->args.size() == sd->fields.size()) {
            valid_ctor = true;
            for (size_t i=0;i<e->args.size();++i) {
              // M2: Try int→float coercion
              if (!type_eq(e->args[i]->type, sd->fields[i].type) &&
                  sd->fields[i].type->kind == TypeKind::Float &&
                  e->args[i]->type->kind == TypeKind::Int) {
                e->args[i] = try_coerce_int_to_float(e->args[i]);
              }
              if (!type_eq(e->args[i]->type, sd->fields[i].type))
                error_at(path, e->args[i]->loc, "E0408", "type mismatch");
            }
          } else if (sd->name == "Color" && e->args.size() == 1 && e->args[0]->type->kind == TypeKind::Int) {
            // Color(hex: int) constructor
            valid_ctor = true;
          } else if (sd->name == "Euler" && e->args.size() == 3) {
            // Euler(x, y, z) constructor with default order
            valid_ctor = true;
            for (size_t i=0; i<3; ++i) {
              if (e->args[i]->type->kind == TypeKind::Int) {
                e->args[i] = try_coerce_int_to_float(e->args[i]);
              }
              if (e->args[i]->type->kind != TypeKind::Float)
                error_at(path, e->args[i]->loc, "E0408", "type mismatch: expected float");
            }
          }
          
          if (!valid_ctor)
            error_at(path, e->loc, "E0411", "wrong number of arguments: expected " + std::to_string(sd->fields.size()) + " or 0, found " + std::to_string(e->args.size()));
          
          e->type = Type::ty_struct(sd->c_sym);
          e->mangled = sd->c_sym;
        } else {
          error_at(path, e->loc, "E0409", "undefined type `" + e->type_name + "`");
          e->type = Type::ty_error();
        }
        break;
      }
      default: e->type = Type::ty_error(); break;
    }
    if (!e->type) e->type = Type::ty_error();
    return e;
  }

  void check_assignable(ExprPtr lv, ExprPtr rv, SourceLoc loc) {
    if (!type_eq(lv->type, rv->type) && lv->type->kind != TypeKind::Error && rv->type->kind != TypeKind::Error)
      error_at(path, rv->loc, "E0408", "type mismatch: expected `" + lv->type->str() + "`, found `" + rv->type->str() + "`");
  }

  void check_lvalue_mut(ExprPtr lv) {
    if (lv->kind == ExprKind::Ident && lv->is_const_binding)
      error_at(path, lv->loc, "E0504", "cannot assign to const `" + lv->name + "`");
    if (lv->kind == ExprKind::Field && lv->lhs && lv->lhs->type && lv->lhs->type->kind == TypeKind::Struct && lv->lhs->is_const_binding)
      error_at(path, lv->loc, "E0511", "cannot assign to immutable field path");
    if (lv->kind == ExprKind::Index && lv->is_const_binding)
      error_at(path, lv->loc, "E0504", "cannot assign to const");
  }

  void check_stmt(StmtPtr s, bool* returns) {
    if (!s) return;
    switch (s->kind) {
      case StmtKind::Block: {
        Scope inner; inner.parent = scope; scope = &inner;
        bool r = false;
        for (size_t i=0;i<s->stmts.size();++i) {
          if (r) { error_at(path, s->stmts[i]->loc, "E0509", "unreachable statement"); }
          bool sr=false; check_stmt(s->stmts[i], &sr); if (sr) r=true;
        }
        scope = inner.parent;
        if (returns) *returns = r;
        break;
      }
      case StmtKind::Let: case StmtKind::Const: {
        check_expr(s->init);
        TypePtr ty;
        if (s->has_type_ann) {
          ty = finalize_type(s->decl_type, s->loc, false);
          // Try int→float coercion
          if (ty->kind == TypeKind::Float) {
            s->init = try_coerce_int_to_float(s->init);
          }
          if (s->init->kind==ExprKind::ArrayLit && s->init->args.empty() && ty->kind==TypeKind::DynArray) {
            s->init->type = ty;
          } else if (s->init->kind==ExprKind::ArrayLit && ty->kind==TypeKind::DynArray) {
            if (!s->init->args.empty() && type_eq(s->init->args[0]->type, ty->elem))
              s->init->type = ty;
            else if (!type_eq(s->init->type, ty) && !(s->init->type->kind==TypeKind::FixedArray && type_eq(s->init->type->elem, ty->elem)))
              error_at(path, s->init->loc, "E0408", "type mismatch: expected `" + ty->str() + "`, found `" + s->init->type->str() + "`");
            else s->init->type = ty;
          } else if (!type_eq(s->init->type, ty)) {
            if (!(s->init->type->kind==TypeKind::FixedArray && ty->kind==TypeKind::FixedArray && type_eq(s->init->type->elem, ty->elem) && s->init->type->fixed_len==ty->fixed_len))
              error_at(path, s->init->loc, "E0408", "type mismatch: expected `" + ty->str() + "`, found `" + s->init->type->str() + "`");
          }
        } else {
          ty = s->init->type;
          if (!ty || ty->kind==TypeKind::Error || ty->kind==TypeKind::Void) {
            error_at(path, s->loc, "E0406", "cannot infer type of `" + s->name + "`");
            ty = Type::ty_error();
          }
        }
        s->decl_type = ty;
        scope->declare(s->name, VarInfo{ty, s->kind==StmtKind::Const, s->loc}, path);
        if (returns) *returns = false;
        break;
      }
      case StmtKind::Assign: {
        check_expr(s->lhs); check_expr(s->rhs);
        check_lvalue_mut(s->lhs);
        if (s->assign_op != TokKind::Assign) {
          Expr tmp; tmp.kind=ExprKind::Binary; tmp.op =
            s->assign_op==TokKind::PlusEq?TokKind::Plus:
            s->assign_op==TokKind::MinusEq?TokKind::Minus:
            s->assign_op==TokKind::StarEq?TokKind::Star:
            s->assign_op==TokKind::SlashEq?TokKind::Slash:TokKind::Percent;
          tmp.loc = s->loc; tmp.lhs = s->lhs; tmp.rhs = s->rhs;
          auto rt = check_binary(tmp);
          if (!type_eq(rt, s->lhs->type) && rt->kind!=TypeKind::Error)
            error_at(path, s->loc, "E0408", "type mismatch");
        } else {
          // Try int→float coercion if assigning to float
          if (s->lhs->type->kind == TypeKind::Float) {
            s->rhs = try_coerce_int_to_float(s->rhs);
          }
          check_assignable(s->lhs, s->rhs, s->loc);
        }
        if (returns) *returns = false;
        break;
      }
      case StmtKind::Expr: {
        check_expr(s->init);
        if (returns) *returns = false;
        break;
      }
      case StmtKind::If: {
        check_expr(s->cond);
        if (s->cond->type->kind != TypeKind::Bool)
          error_at(path, s->cond->loc, "E0401", "condition must be `bool`, found `" + s->cond->type->str() + "`");
        bool r1=false,r2=false;
        check_stmt(s->then_b, &r1);
        if (s->else_b) check_stmt(s->else_b, &r2);
        if (returns) *returns = s->else_b && r1 && r2;
        break;
      }
      case StmtKind::While: {
        check_expr(s->cond);
        if (s->cond->type->kind != TypeKind::Bool)
          error_at(path, s->cond->loc, "E0401", "condition must be `bool`, found `" + s->cond->type->str() + "`");
        loop_depth++; check_stmt(s->then_b, nullptr); loop_depth--;
        if (returns) *returns = false;
        break;
      }
      case StmtKind::For: {
        Scope inner; inner.parent = scope; scope = &inner;
        if (s->for_init) check_stmt(s->for_init, nullptr);
        if (s->for_cond) {
          check_expr(s->for_cond);
          if (s->for_cond->type->kind != TypeKind::Bool)
            error_at(path, s->for_cond->loc, "E0401", "condition must be `bool`, found `" + s->for_cond->type->str() + "`");
        }
        loop_depth++;
        check_stmt(s->then_b, nullptr);
        if (s->for_update) check_stmt(s->for_update, nullptr);
        loop_depth--;
        scope = inner.parent;
        if (returns) *returns = false;
        break;
      }
      case StmtKind::Break: case StmtKind::Continue: {
        int loops_in_task = task_depth > 0 ? (loop_depth - task_loop_mark.back()) : loop_depth;
        if (task_depth > 0 && loops_in_task <= 0) {
          if (task_loop_mark.back() > 0)
            error_at(path, s->loc, "E0804", "`break`/`continue` cannot leave a task");
          else
            error_at(path, s->loc, "E0510", "`break`/`continue` outside loop");
        } else if (loop_depth <= 0) {
          error_at(path, s->loc, "E0510", "`break`/`continue` outside loop");
        }
        if (returns) *returns = false;
        break;
      }
      case StmtKind::Return: {
        if (task_depth > 0) {
          error_at(path, s->loc, "E0803", "`return` is not allowed inside a task");
          if (s->ret) check_expr(s->ret);
        } else if (s->ret) {
          check_expr(s->ret);
          if (cur_ret->kind == TypeKind::Void)
            error_at(path, s->loc, "E0408", "type mismatch: expected `void`, found value");
          else if (!type_eq(s->ret->type, cur_ret))
            error_at(path, s->ret->loc, "E0408", "type mismatch: expected `" + cur_ret->str() + "`, found `" + s->ret->type->str() + "`");
        } else {
          if (cur_ret->kind != TypeKind::Void)
            error_at(path, s->loc, "E0408", "type mismatch: expected `" + cur_ret->str() + "`, found `void`");
        }
        if (returns) *returns = true;
        break;
      }
      case StmtKind::Parallel: {
        s->parallel_id = next_parallel_id++;
        for (auto& t : s->stmts) check_stmt(t, nullptr);
        if (returns) *returns = false;
        break;
      }
      case StmtKind::Task: {
        task_depth++;
        task_loop_mark.push_back(loop_depth);
        check_stmt(s->then_b, nullptr);
        // Capture outer names used in the task (not task-local, not mangled consts/fns).
        std::unordered_set<std::string> declared;
        std::function<void(StmtPtr)> decls = [&](StmtPtr st) {
          if (!st) return;
          if (st->kind == StmtKind::Let || st->kind == StmtKind::Const) declared.insert(st->name);
          if (st->kind == StmtKind::For && st->for_init) decls(st->for_init);
          if (st->then_b) decls(st->then_b);
          if (st->else_b) decls(st->else_b);
          if (st->for_update) decls(st->for_update);
          for (auto& x : st->stmts) decls(x);
        };
        decls(s->then_b);
        std::unordered_set<std::string> seen;
        std::function<void(ExprPtr)> uses = [&](ExprPtr e) {
          if (!e) return;
          if (e->kind == ExprKind::Ident) {
            if (e->mangled.empty() && !declared.count(e->name) && !seen.count(e->name)) {
              if (scope && scope->find(e->name)) {
                seen.insert(e->name);
                s->captures.push_back(e->name);
                s->capture_types.push_back(e->type);
              }
            }
          }
          if (e->kind == ExprKind::This) {
            s->capture_this = true;
            s->this_cap_type = e->type;
          }
          uses(e->lhs); uses(e->rhs);
          for (auto& a : e->args) uses(a);
          for (auto& fv : e->fields) uses(fv.second);
        };
        std::function<void(StmtPtr)> walk = [&](StmtPtr st) {
          if (!st) return;
          uses(st->init); uses(st->cond); uses(st->lhs); uses(st->rhs);
          uses(st->for_cond); uses(st->ret);
          walk(st->then_b); walk(st->else_b); walk(st->for_init); walk(st->for_update);
          for (auto& x : st->stmts) walk(x);
        };
        walk(s->then_b);
        task_loop_mark.pop_back();
        task_depth--;
        if (returns) *returns = false;
        break;
      }
    }
  }

  void check_function(FunctionDecl& f) {
    cur_fn = f.name; cur_ret = f.ret; cur_class = nullptr; cur_struct = nullptr; in_ctor = false;
    Scope sc; scope = &sc;
    for (auto& p : f.params) sc.declare(p.name, VarInfo{p.type, false, p.loc}, path);
    bool ret=false;
    check_stmt(f.body, &ret);
    if (f.ret->kind != TypeKind::Void && !ret) {
      SourceLoc el = f.body->end_loc.line ? f.body->end_loc : f.body->loc;
      error_at(path, el, "E0508", "missing return on some paths in `" + f.name + "`");
    }
    scope = nullptr;
  }

  void check_struct_method(StructDecl& s, MethodDecl& m) {
    cur_fn = m.name; cur_ret = m.ret; cur_class = nullptr; cur_struct = &s; in_ctor = false;
    Scope sc; scope = &sc;
    // M2: `this` for struct methods is available but refers to a by-reference binding
    // For now, skip body checking for synthetic methods (empty body)
    if (m.body->kind == StmtKind::Block && m.body->stmts.empty()) {
      // Built-in method, skip checking
      scope = nullptr;
      return;
    }
    for (auto& p : m.params) sc.declare(p.name, VarInfo{p.type, false, p.loc}, path);
    bool ret=false;
    check_stmt(m.body, &ret);
    if (m.ret->kind != TypeKind::Void && !ret) {
      SourceLoc el = m.body->end_loc.line ? m.body->end_loc : m.body->loc;
      error_at(path, el, "E0508", "missing return on some paths in `" + m.name + "`");
    }
    scope = nullptr;
  }
  
  void check_operator(OperatorDecl& op) {
    // M2: Validate operator overloads
    // Test 037: E0601 - Cannot overload operators for primitive types only
    // Test 038: E0604 - Binary operators must have exactly 2 parameters
    // Test 040: E0603 - Comparison operators must return bool
    
    // Check that at least one parameter is a user-defined struct (not primitive)
    bool has_user_struct = false;
    for (auto& p : op.params) {
      if (p.type->kind == TypeKind::Struct) {
        // After finalization, type->name is the c_sym, so use find_struct_any
        auto* sd = find_struct_any(p.type->name);
        if (sd) has_user_struct = true;
      }
    }
    
    if (!has_user_struct) {
      error_at(path, op.loc, "E0601", "operator overload must have at least one user-defined struct parameter");
      return;
    }
    
    // Check return type for comparison operators (must return bool)
    if (op.op == TokKind::EqEq || op.op == TokKind::Neq) {
      if (op.ret->kind != TypeKind::Bool) {
        error_at(path, op.loc, "E0603", "comparison operator must return bool");
        return;
      }
    }
    
    // Check arity (must have exactly 2 parameters)
    if (op.params.size() != 2) {
      error_at(path, op.loc, "E0604", "operator overload must have exactly 2 parameters");
      return;
    }
    
    // Generate C symbol
    std::string op_name;
    if (op.op == TokKind::Plus) op_name = "op_add";
    else if (op.op == TokKind::Minus) op_name = "op_sub";
    else if (op.op == TokKind::Star) op_name = "op_mul";
    else if (op.op == TokKind::Slash) op_name = "op_div";
    else if (op.op == TokKind::EqEq) op_name = "op_eq";
    else if (op.op == TokKind::Neq) op_name = "op_neq";
    else op_name = "op_unknown";
    
    op.c_sym = "fn_m" + std::to_string(cur_mod->id) + "_" + op_name;
    
    // Check body
    cur_fn = "operator"; cur_ret = op.ret; cur_class = nullptr; cur_struct = nullptr; in_ctor = false;
    Scope sc; scope = &sc;
    for (auto& p : op.params) {
      sc.declare(p.name, VarInfo{p.type, false, p.loc}, path);
    }
    bool ret = false;
    check_stmt(op.body, &ret);
    if (!ret && op.ret->kind != TypeKind::Void) {
      error_at(path, op.loc, "E0508", "missing return in operator");
    }
    scope = nullptr;
  }

  void check_method(ClassDecl& c, MethodDecl& m) {
    cur_fn = m.name; cur_ret = m.ret; cur_class = &c; cur_struct = nullptr; in_ctor = m.is_ctor;
    Scope sc; scope = &sc;
    // M3: Synthetic farmos:scene methods are empty stubs implemented in runtime/farm_scene.c.
    if (is_runtime_class_sym(c.c_sym) && m.body && m.body->kind == StmtKind::Block && m.body->stmts.empty()) {
      scope = nullptr;
      return;
    }
    for (auto& p : m.params) sc.declare(p.name, VarInfo{p.type, false, p.loc}, path);
    bool ret=false;
    check_stmt(m.body, &ret);
    if (m.is_ctor) {
      std::unordered_set<std::string> assigned;
      std::function<void(StmtPtr)> scan = [&](StmtPtr s) {
        if (!s) return;
        if (s->kind==StmtKind::Assign && s->lhs && s->lhs->kind==ExprKind::Field &&
            s->lhs->lhs && s->lhs->lhs->kind==ExprKind::This)
          assigned.insert(s->lhs->name);
        if (s->kind==StmtKind::Block) for (auto& x: s->stmts) scan(x);
        if (s->kind==StmtKind::If) { scan(s->then_b); scan(s->else_b); }
      };
      scan(m.body);
      for (auto& f : c.fields)
        if (!assigned.count(f.name))
          error_at(path, m.loc, "E0418", "constructor does not assign field `" + f.name + "`");
    } else if (m.ret->kind != TypeKind::Void && !ret) {
      SourceLoc el = m.body->end_loc.line ? m.body->end_loc : m.body->loc;
      error_at(path, el, "E0508", "missing return on some paths in `" + m.name + "`");
    }
    scope = nullptr;
  }

  void check_const(ConstDecl& c) {
    Scope sc; scope = &sc;
    check_expr(c.init);
    if (c.has_type_ann) {
      c.type = finalize_type(c.type, c.loc, false);
      if (!type_eq(c.init->type, c.type))
        error_at(path, c.init->loc, "E0408", "type mismatch");
    } else c.type = c.init->type;
    std::function<bool(ExprPtr)> has_class_new = [&](ExprPtr e)->bool {
      if (!e) return false;
      if (e->kind==ExprKind::New && find_class(e->type_name)) return true;
      if (has_class_new(e->lhs)||has_class_new(e->rhs)) return true;
      for (auto& a:e->args) if (has_class_new(a)) return true;
      for (auto& f:e->fields) if (has_class_new(f.second)) return true;
      return false;
    };
    if (has_class_new(c.init))
      error_at(path, c.loc, "E0407", "top-level const initializer is not compile-time constant");
    scope = nullptr;
  }

  void run() {
    for (auto& m : prog.modules) {
      cur_mod = &m;
      path = m.diag_path.empty() ? m.path : m.diag_path;
      for (auto& s : m.structs)
        for (auto& f : s.fields) f.type = finalize_type(f.type, f.loc, false);
      for (auto& c : m.classes) {
        for (auto& f : c.fields) f.type = finalize_type(f.type, f.loc, false);
        int ctors = 0;
        for (size_t i=0;i<c.methods.size();++i) {
          auto& md = c.methods[i];
          for (auto& p : md.params) p.type = finalize_type(p.type, p.loc, false);
          md.ret = finalize_type(md.ret, md.loc, true);
          if (md.is_ctor) { ctors++; c.ctor_index = (int)i; }
        }
        if (ctors != 1)
          error_at(path, c.loc, "E0414", "class `" + c.name + "` must have exactly one constructor");
      }
      // M2: Finalize struct method types
      for (auto& s : m.structs) {
        for (auto& md : s.methods) {
          for (auto& p : md.params) p.type = finalize_type(p.type, p.loc, false);
          md.ret = finalize_type(md.ret, md.loc, true);
        }
      }
      // M2: Finalize operator types (test 036)
      for (auto& op : m.operators) {
        for (auto& p : op.params) p.type = finalize_type(p.type, p.loc, false);
        op.ret = finalize_type(op.ret, op.loc, true);
      }
      for (auto& f : m.functions) {
        for (auto& p : f.params) p.type = finalize_type(p.type, p.loc, false);
        f.ret = finalize_type(f.ret, f.loc, true);
      }
    }

    Module* mainm = nullptr;
    FunctionDecl* mainfn = nullptr;
    for (auto& m : prog.modules) if (m.is_main) {
      mainm = &m;
      auto it = m.vis_functions.find("main");
      if (it != m.vis_functions.end()) mainfn = it->second;
    }
    if (!mainm || !mainfn) {
      error_at(prog.modules.empty() ? std::string("<input>") : prog.modules[0].path,
               SourceLoc{1,1}, "E0506", "missing or invalid `function main(): int` in main file");
    } else {
      if (mainfn->params.size()!=0 || mainfn->ret->kind != TypeKind::Int)
        error_at(mainm->path, mainfn->loc, "E0506", "missing or invalid `function main(): int` in main file");
    }
    for (auto& m : prog.modules) {
      for (auto& f : m.functions) {
        if (!m.is_main && f.name == "main")
          error_at(m.path, f.loc, "E0507", "`main` is only allowed in the main file");
      }
    }

    for (auto& m : prog.modules) {
      cur_mod = &m; path = m.diag_path.empty() ? m.path : m.diag_path;
      for (auto& c : m.consts) check_const(c);
    }
    for (auto& m : prog.modules) {
      cur_mod = &m; path = m.diag_path.empty() ? m.path : m.diag_path;
      // M2: Check operators
      for (auto& op : m.operators) check_operator(op);
      for (auto& f : m.functions) check_function(f);
      for (auto& c : m.classes)
        for (auto& md : c.methods) check_method(c, md);
      // M2: Check struct methods
      for (auto& s : m.structs)
        for (auto& md : s.methods) check_struct_method(s, md);
    }
  }
};

static bool stmt_has_parallel(StmtPtr s) {
  if (!s) return false;
  if (s->kind == StmtKind::Parallel) return true;
  if (stmt_has_parallel(s->then_b) || stmt_has_parallel(s->else_b) ||
      stmt_has_parallel(s->for_init) || stmt_has_parallel(s->for_update))
    return true;
  for (auto& x : s->stmts) if (stmt_has_parallel(x)) return true;
  return false;
}

bool program_uses_parallel(Program& prog) {
  for (auto& m : prog.modules) {
    if (m.path == "farmos:math" || m.path == "farmos:scene" || m.path == "farmos:physics") continue;
    for (auto& f : m.functions) if (stmt_has_parallel(f.body)) return true;
    for (auto& c : m.classes) for (auto& md : c.methods) if (stmt_has_parallel(md.body)) return true;
    for (auto& s : m.structs) for (auto& md : s.methods) if (stmt_has_parallel(md.body)) return true;
    for (auto& op : m.operators) if (stmt_has_parallel(op.body)) return true;
  }
  return false;
}

bool analyze_program(Program& prog) {
  Sema s(prog);
  s.run();
  if (!has_errors()) analyze_conflicts(prog);
  return !has_errors();
}

} // namespace farm

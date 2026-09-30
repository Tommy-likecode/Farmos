#include "emit_c.hpp"
#include <sstream>
#include <cctype>
#include <cstdio>
#include <functional>
#include <set>

namespace farm {

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

  // M2/M3: Check if struct is from farmos:math module
  bool is_math_struct(StructDecl* sd) {
    if (!sd) return false;
    static const std::set<std::string> math_structs = 
      {"Vector2", "Vector3", "Vector4", "Matrix3", "Matrix4", 
       "Quaternion", "Color", "Euler", "Ray", "Sphere", "Box3"};
    return math_structs.count(sd->name) > 0;
  }

  // M2/M3: Get runtime field name for math module structs (inconsistent f_ prefix usage)
  std::string runtime_field_name(const std::string& field_name, const std::string& struct_name = "") {
    // Vector2 uses x, y without prefix; most others use f_x, f_y
    if (struct_name == "Vector2" && (field_name == "x" || field_name == "y")) {
      return field_name;
    }
    
    // Fields that don't have f_ prefix in runtime (for all structs)
    static const std::set<std::string> no_prefix_fields = 
      {"r", "g", "b", "w", "elements", "origin", "direction", "min", "max", "order", "hit", "point", "distance", "center", "radius"};
    return no_prefix_fields.count(field_name) ? field_name : ("f_" + field_name);
  }

  // M3: Check if type is a scene class
  bool is_scene_class(const TypePtr& t) {
    if (t->kind != TypeKind::Class) return false;
    static const std::set<std::string> scene_classes = 
      {"farm_Scene", "farm_Object3D", "farm_PerspectiveCamera", "farm_Mesh",
       "farm_BoxGeometry", "farm_SphereGeometry", "farm_PlaneGeometry",
       "farm_MeshBasicMaterial", "farm_MeshStandardMaterial",
       "farm_AmbientLight", "farm_DirectionalLight", "farm_PointLight", "farm_Renderer"};
    return scene_classes.count(t->name) > 0;
  }

  // M3: Check if scene class inherits from Object3D
  bool is_object3d_subclass(const std::string& class_name) {
    static const std::set<std::string> subclasses = 
      {"farm_Scene", "farm_PerspectiveCamera", "farm_Mesh",
       "farm_AmbientLight", "farm_DirectionalLight", "farm_PointLight"};
    return subclasses.count(class_name) > 0;
  }

  std::string c_type(const TypePtr& t) {
    switch (t->kind) {
      case TypeKind::Int: return "int64_t";
      case TypeKind::Float: return "double";
      case TypeKind::Bool: return "int8_t";
      case TypeKind::String: return "FarmString";
      case TypeKind::Void: return "void";
      case TypeKind::Struct:
        // M2/M3: Math module structs use runtime typedefs (farm_Vector3, farm_Color, etc.)
        if (t->name.find("m1_") == 0) {
          return "farm_" + t->name.substr(3); // m1_Color -> farm_Color
        }
        return "struct Farm_" + t->name;
      case TypeKind::Class: return "struct " + t->name + "*";  // M3: c_sym already includes prefix
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
      case ExprKind::Ident: return !e->mangled.empty() ? e->mangled : ("v_" + e->name);
      case ExprKind::This: return "this";
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
          // Check if lhs is a direct field access to a fixed array (e.g., matrix.elements[i])
          // In that case, the field is already a C array, so we don't need .data
          if (e->lhs->kind == ExprKind::Field) {
            return "({ int64_t __i=(" + idx + "); farm_bounds_check(__i, (int64_t)" + std::to_string(e->lhs->type->fixed_len) + "); (" + arr + ")[__i]; })";
          } else {
            return "({ int64_t __i=(" + idx + "); farm_bounds_check(__i, (int64_t)" + std::to_string(e->lhs->type->fixed_len) + "); (" + arr + ").data[__i]; })";
          }
        } else {
          std::string et = c_type(e->lhs->type->elem);
          return "({ int64_t __i=(" + idx + "); *(" + et + "*)farm_dyn_index(&(" + arr + "), __i); })";
        }
      }
      case ExprKind::Field: {
        auto base = emit_expr(e->lhs);
        if (e->lhs->type->kind == TypeKind::Class) {
          std::string field_access = "(" + base + ")->f_" + e->name;
          
          // M3: Object3D rotation/quaternion fields need sync before read
          if (e->lhs->type->name.find("farm_Object3D") == 0 || 
              e->lhs->type->name.find("farm_Scene") == 0 ||
              e->lhs->type->name.find("farm_PerspectiveCamera") == 0 ||
              e->lhs->type->name.find("farm_Mesh") == 0 ||
              e->lhs->type->name.find("farm_AmbientLight") == 0 ||
              e->lhs->type->name.find("farm_DirectionalLight") == 0 ||
              e->lhs->type->name.find("farm_PointLight") == 0) {
            if (e->name == "rotation") {
              // Reading rotation: sync from quaternion if needed
              return "({ sync_rotation_from_quaternion(" + base + "); " + field_access + "; })";
            } else if (e->name == "quaternion") {
              // Reading quaternion: sync from rotation if needed  
              return "({ sync_quaternion_from_rotation(" + base + "); " + field_access + "; })";
            }
          }
          
          // M3: Mesh geometry and material fields are stored as void* in C runtime
          // but typed specifically in sema. Cast them when accessed.
          if (e->lhs->type->name == "farm_Mesh") {
            if (e->name == "material") {
              // Cast void* to the actual material type (c_type already returns pointer for classes)
              std::string mat_type = c_type(e->type);
              return "(" + mat_type + ")" + field_access;
            }
            if (e->name == "geometry") {
              // Cast void* to the actual geometry type (c_type already returns pointer for classes)
              std::string geom_type = c_type(e->type);
              return "(" + geom_type + ")" + field_access;
            }
          }
          
          return field_access;
        }
        // M2/M3: Math module structs have inconsistent field naming in runtime
        if (e->lhs->type->kind == TypeKind::Struct && e->lhs->type->name.find("m1_") == 0) {
          std::string struct_name = e->lhs->type->name.substr(3); // Remove "m1_" prefix
          return "(" + base + ")." + runtime_field_name(e->name, struct_name);
        }
        return "(" + base + ").f_" + e->name;
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
            // M3: Special handling for lookAt(x, y, z) -> lookAt_v(Vector3)
            if (e->mangled == "farm_PerspectiveCamera_lookAt" && e->args.size() == 3) {
              std::string x = emit_expr(e->args[0]);
              std::string y = emit_expr(e->args[1]);
              std::string z = emit_expr(e->args[2]);
              return "farm_PerspectiveCamera_lookAt_v(farm_Vector3_new(" + x + ", " + y + ", " + z + "))";
            }
            
            std::string call = e->mangled + "(";
            for (size_t i=0;i<e->args.size();++i) {
              if (i) call += ", ";
              std::string arg_val = emit_expr(e->args[i]);
              // M3: Auto-upcast Object3D subclasses for hierarchy methods
              if (e->args[i]->type->kind == TypeKind::Class &&
                  (e->mangled.find("add") != std::string::npos || 
                   e->mangled.find("remove") != std::string::npos ||
                   e->mangled.find("addAt") != std::string::npos)) {
                std::string class_name = e->args[i]->type->name;
                if (class_name == "farm_Scene" || class_name == "farm_PerspectiveCamera" || 
                    class_name == "farm_Mesh" || class_name == "farm_AmbientLight" || 
                    class_name == "farm_DirectionalLight" || class_name == "farm_PointLight") {
                  arg_val = "(farm_Object3D*)" + arg_val;
                }
              }
              call += arg_val;
            }
            call += ")";
            return call;
          }
          if (!e->mangled.empty()) {
            std::string call = e->mangled + "(";
            for (size_t i=0;i<e->args.size();++i) {
              if (i) call += ", ";
              std::string arg_val = emit_expr(e->args[i]);
              // M3: Auto-upcast Object3D subclasses for hierarchy methods
              if (e->args[i]->type->kind == TypeKind::Class &&
                  (e->mangled.find("add") != std::string::npos || 
                   e->mangled.find("remove") != std::string::npos ||
                   e->mangled.find("addAt") != std::string::npos)) {
                std::string class_name = e->args[i]->type->name;
                if (class_name == "farm_Scene" || class_name == "farm_PerspectiveCamera" || 
                    class_name == "farm_Mesh" || class_name == "farm_AmbientLight" || 
                    class_name == "farm_DirectionalLight" || class_name == "farm_PointLight") {
                  arg_val = "(farm_Object3D*)" + arg_val;
                }
              }
              call += arg_val;
            }
            call += ")";
            return call;
          }
        }
        if (e->lhs->kind==ExprKind::Field) {
          // Check if this is a math struct method (farmos:math)
          bool is_math_method = false;
          if (e->lhs->lhs->type->kind == TypeKind::Struct && e->lhs->lhs->type->name.find("m1_") == 0) {
            is_math_method = true;
          }
          
          
          // M3: Special handling for Color.setHex and Color.getHex (static-style functions)
          if (e->mangled == "farm_Color_setHex") {
            // setHex(hex) - no receiver argument, just the hex value
            std::string hex_val = emit_expr(e->args[0]);
            std::string call = "farm_Color_setHex(" + hex_val + ")";
            // If receiver is an lvalue, assign result back
            if (e->lhs->lhs->is_lvalue) {
              emit_assign(e->lhs->lhs, call);
              return "0";
            }
            return call;
          }
          
          if (e->mangled == "farm_Color_getHex") {
            // getHex() - takes receiver by value
            std::string recv = emit_expr(e->lhs->lhs);
            return "farm_Color_getHex(" + recv + ")";
          }
          
          // For struct methods, pass &receiver; for class methods, pass receiver (already a pointer)
          // Exception: math struct methods pass receiver by value and return a new value
          std::string recv;
          if (e->lhs->lhs->type->kind == TypeKind::Struct && !is_math_method) {
            // Regular struct method: need address of receiver
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
          } else if (is_math_method) {
            // Math struct method: pass receiver by value
            recv = emit_expr(e->lhs->lhs);
          } else {
            // Class method: receiver is already a pointer
            recv = emit_expr(e->lhs->lhs);
          }
          
          // M3: Special handling for lookAt on Object3D and subclasses
          if (e->mangled.find("_lookAt") != std::string::npos) {
            std::string class_name = e->lhs->lhs->type->name;
            if (class_name == "farm_Object3D" || class_name == "farm_Scene" || 
                class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                class_name == "farm_AmbientLight" || class_name == "farm_DirectionalLight" || 
                class_name == "farm_PointLight") {
              if (e->args.size() == 3) {
                // lookAt(x, y, z)
                std::string x = emit_expr(e->args[0]);
                std::string y = emit_expr(e->args[1]);
                std::string z = emit_expr(e->args[2]);
                return "farm_Object3D_lookAt_xyz((farm_Object3D*)" + recv + ", " + x + ", " + y + ", " + z + ")";
              } else if (e->args.size() == 1) {
                // lookAt(Vector3)
                std::string v = emit_expr(e->args[0]);
                return "farm_Object3D_lookAt_v((farm_Object3D*)" + recv + ", " + v + ")";
              }
            }
          }
          
          // M3: Special handling for Vector3.set(x, y, z) and Quaternion.set(x, y, z, w) - inline field assignment
          if (e->mangled.find("_set") != std::string::npos && 
              e->lhs->lhs->type->kind == TypeKind::Struct) {
            std::string struct_name = e->lhs->lhs->type->name;
            bool is_vector3_set = struct_name.find("Vector3") != std::string::npos && e->args.size() == 3;
            bool is_quat_set = struct_name.find("Quaternion") != std::string::npos && e->args.size() == 4;
            
            if (is_vector3_set || is_quat_set) {
              // Check if this is setting rotation or quaternion on an Object3D
              bool is_rotation_field = false;
              bool is_quaternion_field = false;
              std::string obj_ptr;
              
              if (e->lhs->lhs->kind == ExprKind::Field && e->lhs->lhs->lhs->type->kind == TypeKind::Class) {
                std::string class_name = e->lhs->lhs->lhs->type->name;
                bool is_object3d_subclass = (class_name == "farm_Object3D" || class_name == "farm_Scene" || 
                                             class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                                             class_name == "farm_AmbientLight" || class_name == "farm_DirectionalLight" || 
                                             class_name == "farm_PointLight");
                if (is_object3d_subclass && e->lhs->lhs->name == "rotation") {
                  is_rotation_field = true;
                  obj_ptr = emit_expr(e->lhs->lhs->lhs);
                } else if (is_object3d_subclass && e->lhs->lhs->name == "quaternion") {
                  is_quaternion_field = true;
                  obj_ptr = emit_expr(e->lhs->lhs->lhs);
                }
              }
              
              // Emit inline assignment
              std::string ptr;
              if (e->lhs->lhs->is_lvalue) {
                ptr = emit_lvalue_ptr(e->lhs->lhs);
              } else {
                std::string recv_val = emit_expr(e->lhs->lhs);
                std::string tmp = fresh("rcv");
                out << c_type(e->lhs->lhs->type) << " " << tmp << " = " << recv_val << ";\n";
                ptr = "&" + tmp;
              }
              
              if (is_vector3_set) {
                std::string x = emit_expr(e->args[0]);
                std::string y = emit_expr(e->args[1]);
                std::string z = emit_expr(e->args[2]);
                out << "(" << ptr << ")->f_x = " << x << ";\n";
                out << "(" << ptr << ")->f_y = " << y << ";\n";
                out << "(" << ptr << ")->f_z = " << z << ";\n";
              } else { // is_quat_set
                std::string x = emit_expr(e->args[0]);
                std::string y = emit_expr(e->args[1]);
                std::string z = emit_expr(e->args[2]);
                std::string w = emit_expr(e->args[3]);
                out << "(" << ptr << ")->f_x = " << x << ";\n";
                out << "(" << ptr << ")->f_y = " << y << ";\n";
                out << "(" << ptr << ")->f_z = " << z << ";\n";
                out << "(" << ptr << ")->w = " << w << ";\n";  // w has no f_ prefix
              }
              
              // M3: Set dirty flag if this is rotation/quaternion on Object3D
              if (is_rotation_field) {
                out << "(" << obj_ptr << ")->rotation_dirty = true;\n";
              } else if (is_quaternion_field) {
                out << "(" << obj_ptr << ")->quaternion_dirty = true;\n";
              }
              
              // Return dereferenced value (set returns the struct)
              return "*(" + ptr + ")";
            }
          }
          
          std::string call_name = e->mangled;
          // M1: User-defined class methods use double underscore: ClassName__methodName
          // but M3 scene classes in runtime use single underscore
          // Operators already have correct underscores, don't modify them
          // If mangled has single underscore and lhs->lhs is a non-scene Class (and not an operator), fix it
          std::string class_name = e->lhs->lhs->type->name;
          bool is_scene_class = (class_name == "farm_Scene" || class_name == "farm_Object3D" ||
                                 class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                                 class_name == "farm_BoxGeometry" || class_name == "farm_SphereGeometry" ||
                                 class_name == "farm_PlaneGeometry" || class_name == "farm_MeshBasicMaterial" ||
                                 class_name == "farm_MeshStandardMaterial" || class_name == "farm_AmbientLight" ||
                                 class_name == "farm_DirectionalLight" || class_name == "farm_PointLight" ||
                                 class_name == "farm_Renderer");
          bool is_operator = (call_name.find("__farm_op_") != std::string::npos);
          
          if (!is_scene_class && !is_operator && e->lhs->lhs->type->kind == TypeKind::Class && call_name.find("_") != std::string::npos) {
            // Replace single underscore with double underscore for user-defined class methods
            size_t pos = call_name.find("_");
            // Find the last single underscore before the method name
            size_t last_single = std::string::npos;
            for (size_t i = 0; i < call_name.length() - 1; i++) {
              if (call_name[i] == '_' && call_name[i+1] != '_') {
                last_single = i;
              }
            }
            if (last_single != std::string::npos && call_name[last_single+1] != '_') {
              // Insert another underscore to make it double
              call_name.insert(last_single+1, "_");
            }
          }
          
          std::string call = call_name + "(" + recv;
          for (auto& a : e->args) { 
            call += ", ";
            std::string arg_val = emit_expr(a);
            // M3: Auto-upcast Object3D subclasses to Object3D* for scene hierarchy methods
            if (a->type->kind == TypeKind::Class) {
              std::string class_name = a->type->name;
              // Check if this is an Object3D subclass and the method likely expects Object3D*
              if ((class_name == "farm_Scene" || class_name == "farm_PerspectiveCamera" || 
                   class_name == "farm_Mesh" || class_name == "farm_AmbientLight" || 
                   class_name == "farm_DirectionalLight" || class_name == "farm_PointLight") &&
                  (e->mangled.find("add") != std::string::npos || 
                   e->mangled.find("remove") != std::string::npos ||
                   e->mangled.find("addAt") != std::string::npos)) {
                arg_val = "(farm_Object3D*)" + arg_val;
              }
            }
            call += arg_val;
          }
          call += ")";
          
          // M3: For math methods on Object3D fields (position, scale), assign result back
          // But NOT for standalone math values (M2 tests) or chained calls
          bool should_assign_back = false;
          if (is_math_method && e->lhs->lhs->is_lvalue && 
              e->type->kind == TypeKind::Struct && 
              e->type->name == e->lhs->lhs->type->name) {
            // Check if receiver is a field of an Object3D subclass
            if (e->lhs->lhs->kind == ExprKind::Field && e->lhs->lhs->lhs->type->kind == TypeKind::Class) {
              std::string class_name = e->lhs->lhs->lhs->type->name;
              bool is_object3d_subclass = (class_name == "farm_Object3D" || class_name == "farm_Scene" || 
                                           class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                                           class_name == "farm_AmbientLight" || class_name == "farm_DirectionalLight" || 
                                           class_name == "farm_PointLight");
              std::string field_name = e->lhs->lhs->name;
              // Only assign back for Object3D fields like position, scale (not rotation/quaternion which have dirty flags)
              if (is_object3d_subclass && (field_name == "position" || field_name == "scale")) {
                should_assign_back = true;
              }
            }
          }
          
          if (should_assign_back) {
            emit_assign(e->lhs->lhs, call);
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
            std::string el = fresh("el");
            out << c_type(e->type->elem) << " " << el << " = " << emit_expr(a) << "; farm_dyn_push(&" << v << ", &" << el << ");\n";
          }
          return v;
        }
        std::string ty = c_type(e->type);
        std::string v = fresh("fa");
        out << ty << " " << v << ";\n";
        for (size_t i=0;i<e->args.size();++i)
          out << v << ".data[" << i << "] = " << emit_expr(e->args[i]) << ";\n";
        return v;
      }
      case ExprKind::StructLit: {
        std::string ty = c_type(e->type);
        std::string v = fresh("st");
        out << ty << " " << v << ";\n";
        for (auto& fv : e->fields)
          out << v << ".f_" << fv.first << " = " << emit_expr(fv.second) << ";\n";
        return v;
      }
      case ExprKind::New: {
        if (e->type->kind == TypeKind::Class) {
          std::string v = fresh("obj");
          
          // M3 scene classes: map to runtime constructor functions
          // Check if this is a farmos:scene class by name
          std::string class_name = e->type->name;
          bool is_scene_class = (class_name == "farm_Scene" || class_name == "farm_Object3D" ||
                                 class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                                 class_name == "farm_BoxGeometry" || class_name == "farm_SphereGeometry" ||
                                 class_name == "farm_PlaneGeometry" || class_name == "farm_MeshBasicMaterial" ||
                                 class_name == "farm_MeshStandardMaterial" || class_name == "farm_AmbientLight" ||
                                 class_name == "farm_DirectionalLight" || class_name == "farm_PointLight" ||
                                 class_name == "farm_Renderer");
          
          if (is_scene_class) {
            // M3: Pre-emit nested New expressions as temporaries to avoid invalid C syntax
            std::vector<std::string> arg_values;
            for (auto& arg : e->args) {
              if (arg->kind == ExprKind::New) {
                // Emit the nested new as a temporary variable first
                std::string temp_val = emit_expr(arg);
                arg_values.push_back(temp_val);
              } else {
                arg_values.push_back(emit_expr(arg));
              }
            }
            
            // Call runtime constructor function directly
            out << "struct " << e->mangled << "* " << v << " = " << e->mangled << "_new";
            
            // M3: Use the resolved constructor variant from sema
            if (!e->ctor_variant.empty()) {
              out << "_" << e->ctor_variant;
            }
            
            out << "(";
            for (size_t i = 0; i < arg_values.size(); ++i) {
              if (i > 0) out << ", ";
              out << arg_values[i];
            }
            out << ");\n";
            
            // M3: Runtime bug workaround - farm_Mesh_new and Light constructors don't initialize Object3D fields
            // Only initialize for Mesh and Lights - NOT Scene/Camera (see comment below)
            bool needs_object3d_init = (class_name == "farm_Mesh" || class_name == "farm_AmbientLight" ||
                                       class_name == "farm_DirectionalLight" || class_name == "farm_PointLight");
            if (needs_object3d_init) {
              out << v << "->f_visible = 1;\n"; // true
              out << v << "->matrixAutoUpdate = 1;\n"; // true
              out << v << "->parent = NULL;\n";
              out << v << "->children.items = NULL;\n";
              out << v << "->children.count = 0;\n";
              out << v << "->children.capacity = 0;\n";
            }
          } else {
            // Original class constructor logic
            out << "struct " << e->mangled << "* " << v << " = (struct " << e->mangled << "*)farm_arena_alloc(sizeof(struct " << e->mangled << "));\n";
            out << e->mangled << "__constructor(" << v;
            for (auto& a : e->args) out << ", " << emit_expr(a);
            out << ");\n";
          }
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
            out << "  for (int i = 0; i < 16; i++) " << v << ".elements[i] = 0.0;\n";
            out << "  " << v << ".elements[0] = 1.0;\n";   // [0,0]
            out << "  " << v << ".elements[5] = 1.0;\n";   // [1,1]
            out << "  " << v << ".elements[10] = 1.0;\n";  // [2,2]
            out << "  " << v << ".elements[15] = 1.0;\n";  // [3,3]
          } else if (sd->name == "Matrix3") {
            // Identity matrix: diagonal = 1, rest = 0
            out << "  for (int i = 0; i < 9; i++) " << v << ".elements[i] = 0.0;\n";
            out << "  " << v << ".elements[0] = 1.0;\n";   // [0,0]
            out << "  " << v << ".elements[4] = 1.0;\n";   // [1,1]
            out << "  " << v << ".elements[8] = 1.0;\n";   // [2,2]
          } else if (sd->name == "Quaternion") {
            // Identity quaternion: (0, 0, 0, 1)
            out << "  " << v << ".f_x = 0.0;\n";
            out << "  " << v << ".f_y = 0.0;\n";
            out << "  " << v << ".f_z = 0.0;\n";
            out << "  " << v << ".f_w = 1.0;\n";
          } else if (sd->name == "Box3") {
            // Empty box: min = +infinity, max = -infinity
            out << "  " << v << ".f_min.f_x = 1.0/0.0;\n";  // +inf
            out << "  " << v << ".f_min.f_y = 1.0/0.0;\n";
            out << "  " << v << ".f_min.f_z = 1.0/0.0;\n";
            out << "  " << v << ".f_max.f_x = -1.0/0.0;\n"; // -inf
            out << "  " << v << ".f_max.f_y = -1.0/0.0;\n";
            out << "  " << v << ".f_max.f_z = -1.0/0.0;\n";
          } else if (sd->name == "Euler") {
            // Default Euler: (0, 0, 0, "XYZ")
            out << "  " << v << ".f_x = 0.0;\n";
            out << "  " << v << ".f_y = 0.0;\n";
            out << "  " << v << ".f_z = 0.0;\n";
            out << "  " << v << ".f_order = (FarmString){.ptr=\"XYZ\", .len=3};\n";
          } else {
            // Default: zero all fields
            for (auto& f : sd->fields) {
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
                    out << nested_v << ".f_" << nf.name << " = " << zero_val << ";\n";
                  }
                }
                out << v << ".f_" << f.name << " = " << nested_v << ";\n";
              } else {
                std::string zero_val = "0";
                if (f.type->kind == TypeKind::Float) zero_val = "0.0";
                out << v << ".f_" << f.name << " = " << zero_val << ";\n";
              }
            }
          }
        } else if (sd->name == "Color" && e->args.size() == 1) {
          // Color(hex: int) constructor
          std::string hex_val = emit_expr(e->args[0]);
          if (is_math_struct(sd)) {
            out << v << ".r = ((" << hex_val << " >> 16) & 255) / 255.0;\n";
            out << v << ".g = ((" << hex_val << " >> 8) & 255) / 255.0;\n";
            out << v << ".b = (" << hex_val << " & 255) / 255.0;\n";
          } else {
            out << v << ".f_r = ((" << hex_val << " >> 16) & 255) / 255.0;\n";
            out << v << ".f_g = ((" << hex_val << " >> 8) & 255) / 255.0;\n";
            out << v << ".f_b = (" << hex_val << " & 255) / 255.0;\n";
          }
        } else if (sd->name == "Euler" && e->args.size() == 3) {
          // Euler(x, y, z) constructor with default order "XYZ"
          for (size_t i=0; i<3; ++i) {
            std::string arg_val = emit_expr(e->args[i]);
            out << v << ".f_" << sd->fields[i].name << " = " << arg_val << ";\n";
          }
          out << v << ".f_order = (FarmString){.ptr=\"XYZ\", .len=3};\n";
        } else {
          // Explicit constructor with all arguments
          for (size_t i=0;i<e->args.size();++i) {
            std::string arg_val = emit_expr(e->args[i]);
            std::string field_name = is_math_struct(sd) ? runtime_field_name(sd->fields[i].name, sd->name) : ("f_" + sd->fields[i].name);
            out << v << "." << field_name << " = " << arg_val << ";\n";
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
      return "&v_" + lv->name;
    }
    if (lv->kind == ExprKind::Field) {
      if (lv->lhs->type->kind == TypeKind::Class) {
        std::string base = emit_expr(lv->lhs);
        std::string bp = fresh("bp");
        out << c_type(lv->lhs->type) << " " << bp << " = " << base << ";\n";
        return "&(" + bp + "->f_" + lv->name + ")";
      }
      /* struct field: address of enclosing struct value, then field */
      std::string sp = emit_lvalue_ptr(lv->lhs);
      return "&((" + sp + ")->f_" + lv->name + ")";
    }
    if (lv->kind == ExprKind::Index) {
      std::string ip = fresh("ix");
      std::string idx = emit_expr(lv->rhs);
      out << "int64_t " << ip << " = " << idx << ";\n";
      if (lv->lhs->type->kind == TypeKind::FixedArray) {
        std::string ap = emit_lvalue_ptr(lv->lhs);
        out << "farm_bounds_check(" << ip << ", (int64_t)" << lv->lhs->type->fixed_len << ");\n";
        return "&((" + ap + ")->data[" + ip + "])";
      }
      if (lv->lhs->kind == ExprKind::Ident) {
        out << "farm_bounds_check(" << ip << ", v_" << lv->lhs->name << ".len);\n";
        return "((" + c_type(lv->type) + "*)v_" + lv->lhs->name + ".data) + " + ip;
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
    std::string ptr = emit_lvalue_ptr(lv);
    out << "*(" << ptr << ") = " << rval << ";\n";
    
    // M3: Track dirty flags for Euler/Quaternion sync on Object3D
    // When assigning to rotation.x/y/z or quaternion.x/y/z/w on Object3D subclasses,
    // mark the appropriate dirty flag
    if (lv->kind == ExprKind::Field && lv->lhs && lv->lhs->kind == ExprKind::Field) {
      // Check if this is obj.rotation.x or obj.quaternion.x pattern
      std::string parent_field = lv->lhs->name;
      std::string child_field = lv->name;
      
      // Check if the grandparent is an Object3D subclass
      if (lv->lhs->lhs && lv->lhs->lhs->type && lv->lhs->lhs->type->kind == TypeKind::Class) {
        std::string class_name = lv->lhs->lhs->type->name;
        bool is_object3d = (class_name == "farm_Object3D" || class_name == "farm_Scene" || 
                           class_name == "farm_PerspectiveCamera" || class_name == "farm_Mesh" ||
                           class_name == "farm_AmbientLight" || class_name == "farm_DirectionalLight" || 
                           class_name == "farm_PointLight");
        
        if (is_object3d) {
          std::string obj_expr = emit_expr(lv->lhs->lhs);
          if (parent_field == "rotation" && (child_field == "x" || child_field == "y" || child_field == "z" || child_field == "order")) {
            out << "(" << obj_expr << ")->rotation_dirty = 1;\n";
          } else if (parent_field == "quaternion" && (child_field == "x" || child_field == "y" || child_field == "z" || child_field == "w")) {
            out << "(" << obj_expr << ")->quaternion_dirty = 1;\n";
          }
        }
      }
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
        out << "for (; " << fc << "; ) {\n";
        emit_stmt(s->then_b);
        if (s->for_update) emit_stmt(s->for_update);
        out << "}\n}\n";
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
    }
  }

  std::string emit_all() {
    out << "/* Generated by farmc */\n";
    out << "#include <stdint.h>\n";
    out << "#include <stdio.h>\n";
    out << "#include <math.h>\n";
    out << "#include \"farm_rt.h\"\n";
    // M2: Check if any math types are used
    bool uses_math = false;
    for (auto& m : prog.modules) {
      if (m.path == "farmos:math") {
        uses_math = true;
        break;
      }
      for (auto& imp : m.imports) {
        if (imp.path == "farmos:math") {
          uses_math = true;
          break;
        }
      }
      if (uses_math) break;
    }
    if (uses_math) {
      out << "#include \"farm_math.h\"\n";
    }
    // M3: Check if any scene classes are used
    bool uses_scene = false;
    for (auto& m : prog.modules) {
      for (auto& imp : m.imports) {
        if (imp.path == "farmos:scene") {
          uses_scene = true;
          break;
        }
      }
      for (auto& c : m.classes) {
        // Check if this is a scene class by c_sym prefix
        if (c.c_sym.find("farm_Scene") == 0 ||
            c.c_sym.find("farm_Object3D") == 0 ||
            c.c_sym.find("farm_PerspectiveCamera") == 0 ||
            c.c_sym.find("farm_Mesh") == 0 ||
            c.c_sym.find("farm_BoxGeometry") == 0 ||
            c.c_sym.find("farm_SphereGeometry") == 0 ||
            c.c_sym.find("farm_PlaneGeometry") == 0 ||
            c.c_sym.find("farm_MeshBasicMaterial") == 0 ||
            c.c_sym.find("farm_MeshStandardMaterial") == 0 ||
            c.c_sym.find("farm_AmbientLight") == 0 ||
            c.c_sym.find("farm_DirectionalLight") == 0 ||
            c.c_sym.find("farm_PointLight") == 0 ||
            c.c_sym.find("farm_Renderer") == 0) {
          uses_scene = true;
          break;
        }
      }
      if (uses_scene) break;
    }
    if (uses_scene) {
      out << "#include \"farm_scene.h\"\n";
    }
    out << "/* farmc build: " << __DATE__ << " " << __TIME__ << " */\n";
    out << "\n";
    // Emit fixed array typedefs first (structs may reference them)
    emit_fixed_typedefs();
    // Emit all structs first (so classes can reference them)
    for (auto& m : prog.modules) {
      // M2/M3: Skip math module structs - they're typedefs in farm_math.h
      bool is_math_module = (m.path == "farmos:math");
      for (auto& s : m.structs) {
        if (is_math_module) continue;
        
        out << "struct Farm_" << s.c_sym << " {\n";
        for (auto& f : s.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    // Then emit all classes (skip scene classes - already in farm_scene.h)
    for (auto& m : prog.modules) {
      for (auto& c : m.classes) {
        // M3: Skip scene classes defined in runtime header
        if (c.c_sym.find("farm_Scene") == 0 || c.c_sym.find("farm_Object3D") == 0 ||
            c.c_sym.find("farm_PerspectiveCamera") == 0 || c.c_sym.find("farm_Mesh") == 0 ||
            c.c_sym.find("farm_BoxGeometry") == 0 || c.c_sym.find("farm_SphereGeometry") == 0 ||
            c.c_sym.find("farm_PlaneGeometry") == 0 || c.c_sym.find("farm_MeshBasicMaterial") == 0 ||
            c.c_sym.find("farm_MeshStandardMaterial") == 0 || c.c_sym.find("farm_AmbientLight") == 0 ||
            c.c_sym.find("farm_DirectionalLight") == 0 || c.c_sym.find("farm_PointLight") == 0 ||
            c.c_sym.find("farm_Renderer") == 0) {
          continue;
        }
        out << "struct " << c.c_sym << " {\n";
        for (auto& f : c.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    
    // M2: Forward declare struct print helpers  
    out << "/* M2 struct print helpers forward declarations */\n";
    for (auto& m : prog.modules) {
      bool is_math_module = (m.path == "farmos:math");
      for (auto& s : m.structs) {
        std::string type_name = is_math_module ? ("farm_" + s.name) : ("struct Farm_" + s.c_sym);
        out << "void farm_print_" << sanitize(s.name) << "(" << type_name << " v);\n";
        out << "void farm_print_" << sanitize(s.name) << "_ln(" << type_name << " v);\n";
      }
    }
    out << "/* End M2 forward declarations */\n";
    
    for (auto& m : prog.modules) {
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
        // M3: Skip forward declarations for scene classes - they're in farm_scene.h
        bool is_scene_class = (c.c_sym == "farm_Scene" || c.c_sym == "farm_Object3D" ||
                               c.c_sym == "farm_PerspectiveCamera" || c.c_sym == "farm_Mesh" ||
                               c.c_sym == "farm_BoxGeometry" || c.c_sym == "farm_SphereGeometry" ||
                               c.c_sym == "farm_PlaneGeometry" || c.c_sym == "farm_MeshBasicMaterial" ||
                               c.c_sym == "farm_MeshStandardMaterial" || c.c_sym == "farm_AmbientLight" ||
                               c.c_sym == "farm_DirectionalLight" || c.c_sym == "farm_PointLight" ||
                               c.c_sym == "farm_Renderer");
        if (is_scene_class) continue;
        
        for (auto& md : c.methods) {
          std::string name = md.is_ctor ? (c.c_sym + "__constructor") : (c.c_sym + "__" + md.name);
          out << (md.is_ctor ? "void" : c_type(md.ret)) << " " << name << "(struct " << c.c_sym << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ");\n";
        }
      }
      // M2: Forward declare struct methods
      bool is_math_module = (m.path == "farmos:math");
      if (is_math_module) {
        // Skip math module - methods are declared in farm_math.h
        continue;
      }
      
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
            ret_type = "struct Farm_" + s.c_sym + "*";
          }
          out << ret_type << " " << s.c_sym << "__" << md.name << "(struct Farm_" << s.c_sym << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ");\n";
        }
      }
    }
    for (auto& m : prog.modules)
      for (auto& c : m.consts)
        out << "static " << c_type(c.type) << " " << c.c_sym << ";\n";

    out << "static void farm_init_globals(void) {\n";
    for (auto& m : prog.modules) {
      for (auto& c : m.consts) {
        std::string v = emit_expr(c.init);
        out << "  " << c.c_sym << " = " << v << ";\n";
      }
    }
    out << "}\n";

    for (auto& m : prog.modules) {
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
        // M3: Skip implementations for scene classes - they're in farm_scene.c
        bool is_scene_class = (c.c_sym == "farm_Scene" || c.c_sym == "farm_Object3D" ||
                               c.c_sym == "farm_PerspectiveCamera" || c.c_sym == "farm_Mesh" ||
                               c.c_sym == "farm_BoxGeometry" || c.c_sym == "farm_SphereGeometry" ||
                               c.c_sym == "farm_PlaneGeometry" || c.c_sym == "farm_MeshBasicMaterial" ||
                               c.c_sym == "farm_MeshStandardMaterial" || c.c_sym == "farm_AmbientLight" ||
                               c.c_sym == "farm_DirectionalLight" || c.c_sym == "farm_PointLight" ||
                               c.c_sym == "farm_Renderer");
        if (is_scene_class) continue;
        
        for (auto& md : c.methods) {
          std::string name = md.is_ctor ? (c.c_sym + "__constructor") : (c.c_sym + "__" + md.name);
          out << (md.is_ctor ? "void" : c_type(md.ret)) << " " << name << "(struct " << c.c_sym << "* this";
          for (auto& p : md.params) out << ", " << c_type(p.type) << " v_" << p.name;
          out << ") ";
          emit_stmt(md.body);
          out << "\n";
        }
      }
      
      // M2: Implement struct methods
      bool is_math_module = (m.path == "farmos:math");
      // M2: Implement struct print helpers (must be before continue for math modules)
      for (auto& s : m.structs) {
        // Determine if this is a math module struct
        std::string type_name = is_math_module ? ("farm_" + s.name) : ("struct Farm_" + s.c_sym);
        
        // print version (no newline)
        out << "void farm_print_" << sanitize(s.name) << "(" << type_name << " v) {\n";
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
            
            // Determine field name with proper prefix
            std::string field_access = is_math_module ? ("v." + runtime_field_name(field.name, s.name)) : ("v.f_" + field.name);
            
            // Special case for Euler.order (char[4])
            if (s.name == "Euler" && field.name == "order") {
              out << "  printf(\"\\\"%s\\\"\", " << field_access << ");\n";
              out << "  fflush(stdout);\n";
            } else if (field.type->kind == TypeKind::FixedArray) {
              // Fixed char array - print as C string
              out << "  printf(\"\\\"%s\\\"\", " << field_access << ");\n";
              out << "  fflush(stdout);\n";
            } else if (field.type->kind == TypeKind::Float) {
              out << "  farm_print_float(" << field_access << ");\n";
            } else if (field.type->kind == TypeKind::String) {
              out << "  printf(\"\\\"\");\n";  // Opening quote
              out << "  fflush(stdout);\n";
              out << "  farm_print_string(" << field_access << ");\n";
              out << "  printf(\"\\\"\");\n";  // Closing quote
              out << "  fflush(stdout);\n";
            } else if (field.type->kind == TypeKind::Bool) {
              out << "  farm_print_bool(" << field_access << ");\n";
            } else if (field.type->kind == TypeKind::Struct) {
              // For struct fields, find the base type name without module prefix
              std::string field_type_name = field.type->name;
              size_t pos = field_type_name.find_last_of('_');
              if (pos != std::string::npos && field_type_name.substr(0, pos).find("m") == 0) {
                field_type_name = field_type_name.substr(pos + 1);
              }
              out << "  farm_print_" << field_type_name << "(" << field_access << ");\n";
            } else {
              out << "  farm_print_float(" << field_access << ");\n"; // Default
            }
          }
          out << "  printf(\")\");\n";
          out << "  fflush(stdout);\n"; // Flush closing paren before function returns
        }
        out << "}\n";
        
        // println version (with newline)
        out << "void farm_print_" << sanitize(s.name) << "_ln(" << type_name << " v) {\n";
        out << "  farm_print_" << sanitize(s.name) << "(v);\n";
        out << "  putchar('\\n');\n";
        out << "  fflush(stdout);\n"; // Flush newline before returning
        out << "}\n";
      }
      
      if (is_math_module) {
        // Skip math module struct methods - they're implemented in farm_math.c
        continue;
      }
      
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
            ret_type = "struct Farm_" + s.c_sym + "*";
            returns_this_ptr = true;
          }
          out << ret_type << " " << s.c_sym << "__" << md.name << "(struct Farm_" + s.c_sym << "* this";
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
              out << "  struct Farm_" << s.c_sym << " result;\n";
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
              out << "  return sqrt(this->f_x * this->f_x + this->f_y * this->f_y);\n";
            } else if (s.name == "Vector2" && md.name == "normalize") {
              out << "  double len = sqrt(this->f_x * this->f_x + this->f_y * this->f_y);\n";
              out << "  if (len > 0.0) { this->f_x /= len; this->f_y /= len; }\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "add") {
              out << "  this->f_x += v_v.f_x;\n";
              out << "  this->f_y += v_v.f_y;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "multiplyScalar") {
              out << "  this->f_x *= v_s;\n";
              out << "  this->f_y *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "applyMatrix3") {
              // Apply 3x3 matrix to (x, y, 1) homogeneous coordinate
              out << "  double* e = v_m.f_elements.data;\n";
              out << "  double x = e[0] * this->f_x + e[3] * this->f_y + e[6];\n";
              out << "  double y = e[1] * this->f_x + e[4] * this->f_y + e[7];\n";
              out << "  this->f_x = x;\n";
              out << "  this->f_y = y;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector2" && md.name == "__farm_op_add") {
              out << "  struct Farm_m1_Vector2 result;\n";
              out << "  result.f_x = this->f_x + v_other.f_x;\n";
              out << "  result.f_y = this->f_y + v_other.f_y;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector4" && md.name == "dot") {
              out << "  return this->f_x * v_v.f_x + this->f_y * v_v.f_y + this->f_z * v_v.f_z + this->f_w * v_v.f_w;\n";
            } else if (s.name == "Vector4" && md.name == "multiplyScalar") {
              out << "  this->f_x *= v_s;\n";
              out << "  this->f_y *= v_s;\n";
              out << "  this->f_z *= v_s;\n";
              out << "  this->f_w *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "determinant") {
              // 4x4 matrix determinant (column-major order)
              out << "  double* m = this->f_elements.data;\n";
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
              out << "  double* m = this->f_elements.data;\n";
              out << "  m[0] = 1.0; m[4] = 0.0; m[8]  = 0.0; m[12] = v_x;\n";
              out << "  m[1] = 0.0; m[5] = 1.0; m[9]  = 0.0; m[13] = v_y;\n";
              out << "  m[2] = 0.0; m[6] = 0.0; m[10] = 1.0; m[14] = v_z;\n";
              out << "  m[3] = 0.0; m[7] = 0.0; m[11] = 0.0; m[15] = 1.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeScale") {
              // Set to scale matrix (column-major)
              out << "  double* m = this->f_elements.data;\n";
              out << "  m[0] = v_x; m[4] = 0.0;  m[8]  = 0.0;  m[12] = 0.0;\n";
              out << "  m[1] = 0.0; m[5] = v_y;  m[9]  = 0.0;  m[13] = 0.0;\n";
              out << "  m[2] = 0.0; m[6] = 0.0;  m[10] = v_z;  m[14] = 0.0;\n";
              out << "  m[3] = 0.0; m[7] = 0.0;  m[11] = 0.0;  m[15] = 1.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "multiply") {
              // this = this * m (column-major)
              out << "  double* a = this->f_elements.data;\n";
              out << "  double* b = v_m.f_elements.data;\n";
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
              out << "  double* m = this->f_elements.data;\n";
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
              out << "  double* m = this->f_elements.data;\n";
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
              out << "  double* m = this->f_elements.data;\n";
              for (int i = 0; i < 16; i++) {
                out << "  m[" << i << "] = v_n" << i << ";\n";
              }
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeRotationY") {
              out << "  double c = cos(v_theta);\n";
              out << "  double s = sin(v_theta);\n";
              out << "  double* e = this->f_elements.data;\n";
              out << "  e[0] = c;  e[4] = 0; e[8] = s;  e[12] = 0;\n";
              out << "  e[1] = 0;  e[5] = 1; e[9] = 0;  e[13] = 0;\n";
              out << "  e[2] = -s; e[6] = 0; e[10] = c; e[14] = 0;\n";
              out << "  e[3] = 0;  e[7] = 0; e[11] = 0; e[15] = 1;\n";
              out << "  return this;\n";
            } else if (s.name == "Matrix4" && md.name == "makeRotationX") {
              out << "  double c = cos(v_theta);\n";
              out << "  double s = sin(v_theta);\n";
              out << "  double* e = this->f_elements.data;\n";
              out << "  e[0] = 1; e[4] = 0;  e[8] = 0;  e[12] = 0;\n";
              out << "  e[1] = 0; e[5] = c;  e[9] = -s; e[13] = 0;\n";
              out << "  e[2] = 0; e[6] = s;  e[10] = c; e[14] = 0;\n";
              out << "  e[3] = 0; e[7] = 0;  e[11] = 0; e[15] = 1;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "applyMatrix4") {
              // Apply 4x4 matrix to (x, y, z, 1) homogeneous coordinate
              out << "  double* e = v_m.f_elements.data;\n";
              out << "  double x = this->f_x, y = this->f_y, z = this->f_z;\n";
              out << "  double w = e[3]*x + e[7]*y + e[11]*z + e[15];\n";
              out << "  w = (w != 0.0) ? 1.0 / w : 1.0;\n";
              out << "  this->f_x = (e[0]*x + e[4]*y + e[8]*z  + e[12]) * w;\n";
              out << "  this->f_y = (e[1]*x + e[5]*y + e[9]*z  + e[13]) * w;\n";
              out << "  this->f_z = (e[2]*x + e[6]*y + e[10]*z + e[14]) * w;\n";
              out << "  return this;\n";
            } else if (s.name == "Vector3" && md.name == "applyQuaternion") {
              // Rotate vector by quaternion
              out << "  double qx = v_q.f_x, qy = v_q.f_y, qz = v_q.f_z, qw = v_q.f_w;\n";
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
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_x + v_other.f_x;\n";
              out << "  result.f_y = this->f_y + v_other.f_y;\n";
              out << "  result.f_z = this->f_z + v_other.f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_sub") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_x - v_other.f_x;\n";
              out << "  result.f_y = this->f_y - v_other.f_y;\n";
              out << "  result.f_z = this->f_z - v_other.f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_mul") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_x * v_scalar;\n";
              out << "  result.f_y = this->f_y * v_scalar;\n";
              out << "  result.f_z = this->f_z * v_scalar;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_div") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_x / v_scalar;\n";
              out << "  result.f_y = this->f_y / v_scalar;\n";
              out << "  result.f_z = this->f_z / v_scalar;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_eq") {
              out << "  return (this->f_x == v_other.f_x && this->f_y == v_other.f_y && this->f_z == v_other.f_z) ? 1 : 0;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_neq") {
              out << "  return (this->f_x != v_other.f_x || this->f_y != v_other.f_y || this->f_z != v_other.f_z) ? 1 : 0;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_neg") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = -this->f_x;\n";
              out << "  result.f_y = -this->f_y;\n";
              out << "  result.f_z = -this->f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Vector3" && md.name == "__farm_op_rmul") {
              // scalar * vector (reverse multiply)
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = v_scalar * this->f_x;\n";
              out << "  result.f_y = v_scalar * this->f_y;\n";
              out << "  result.f_z = v_scalar * this->f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Quaternion" && md.name == "multiply") {
              // this = this * q
              out << "  double qax = this->f_x, qay = this->f_y, qaz = this->f_z, qaw = this->f_w;\n";
              out << "  double qbx = v_q.f_x, qby = v_q.f_y, qbz = v_q.f_z, qbw = v_q.f_w;\n";
              out << "  this->f_x = qax*qbw + qaw*qbx + qay*qbz - qaz*qby;\n";
              out << "  this->f_y = qay*qbw + qaw*qby + qaz*qbx - qax*qbz;\n";
              out << "  this->f_z = qaz*qbw + qaw*qbz + qax*qby - qay*qbx;\n";
              out << "  this->f_w = qaw*qbw - qax*qbx - qay*qby - qaz*qbz;\n";
              out << "  return this;\n";
            } else if (s.name == "Quaternion" && md.name == "equals") {
              out << "  return (this->f_x == v_q.f_x && this->f_y == v_q.f_y && this->f_z == v_q.f_z && this->f_w == v_q.f_w) ? 1 : 0;\n";
            } else if (s.name == "Quaternion" && md.name == "setFromAxisAngle") {
              out << "  double half = v_angle * 0.5;\n";
              out << "  double s = sin(half);\n";
              out << "  this->f_x = v_axis.f_x * s;\n";
              out << "  this->f_y = v_axis.f_y * s;\n";
              out << "  this->f_z = v_axis.f_z * s;\n";
              out << "  this->f_w = cos(half);\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "setHex") {
              out << "  this->f_r = ((v_hex >> 16) & 255) / 255.0;\n";
              out << "  this->f_g = ((v_hex >> 8) & 255) / 255.0;\n";
              out << "  this->f_b = (v_hex & 255) / 255.0;\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "multiplyScalar") {
              out << "  this->f_r *= v_s;\n";
              out << "  this->f_g *= v_s;\n";
              out << "  this->f_b *= v_s;\n";
              out << "  return this;\n";
            } else if (s.name == "Color" && md.name == "getHex") {
              out << "  int r = (int)(this->f_r * 255.0);\n";
              out << "  int g = (int)(this->f_g * 255.0);\n";
              out << "  int b = (int)(this->f_b * 255.0);\n";
              out << "  return (r << 16) | (g << 8) | b;\n";
            } else if (s.name == "Ray" && md.name == "at") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_origin.f_x + this->f_direction.f_x * v_t;\n";
              out << "  result.f_y = this->f_origin.f_y + this->f_direction.f_y * v_t;\n";
              out << "  result.f_z = this->f_origin.f_z + this->f_direction.f_z * v_t;\n";
              out << "  return result;\n";
            } else if (s.name == "Ray" && md.name == "intersectSphere") {
              out << "  struct Farm_m1_RayHit result;\n";
              out << "  double dx = this->f_origin.f_x - v_s.f_center.f_x;\n";
              out << "  double dy = this->f_origin.f_y - v_s.f_center.f_y;\n";
              out << "  double dz = this->f_origin.f_z - v_s.f_center.f_z;\n";
              out << "  double a = this->f_direction.f_x*this->f_direction.f_x + this->f_direction.f_y*this->f_direction.f_y + this->f_direction.f_z*this->f_direction.f_z;\n";
              out << "  double b = 2.0 * (dx*this->f_direction.f_x + dy*this->f_direction.f_y + dz*this->f_direction.f_z);\n";
              out << "  double c = dx*dx + dy*dy + dz*dz - v_s.f_radius*v_s.f_radius;\n";
              out << "  double disc = b*b - 4.0*a*c;\n";
              out << "  if (disc < 0.0) { result.f_hit = 0; result.f_point.f_x = result.f_point.f_y = result.f_point.f_z = 0.0; result.f_distance = 0.0; return result; }\n";
              out << "  double t = (-b - sqrt(disc)) / (2.0*a);\n";
              out << "  if (t < 0.0) { result.f_hit = 0; result.f_point.f_x = result.f_point.f_y = result.f_point.f_z = 0.0; result.f_distance = 0.0; return result; }\n";
              out << "  result.f_hit = 1;\n";
              out << "  result.f_point.f_x = this->f_origin.f_x + this->f_direction.f_x * t;\n";
              out << "  result.f_point.f_y = this->f_origin.f_y + this->f_direction.f_y * t;\n";
              out << "  result.f_point.f_z = this->f_origin.f_z + this->f_direction.f_z * t;\n";
              out << "  result.f_distance = t;\n";
              out << "  return result;\n";
            } else if (s.name == "Sphere" && md.name == "containsPoint") {
              out << "  double dx = v_p.f_x - this->f_center.f_x;\n";
              out << "  double dy = v_p.f_y - this->f_center.f_y;\n";
              out << "  double dz = v_p.f_z - this->f_center.f_z;\n";
              out << "  return (dx*dx + dy*dy + dz*dz <= this->f_radius*this->f_radius) ? 1 : 0;\n";
            } else if (s.name == "Sphere" && md.name == "intersectsSphere") {
              out << "  double dx = this->f_center.f_x - v_s.f_center.f_x;\n";
              out << "  double dy = this->f_center.f_y - v_s.f_center.f_y;\n";
              out << "  double dz = this->f_center.f_z - v_s.f_center.f_z;\n";
              out << "  double dist = sqrt(dx*dx + dy*dy + dz*dz);\n";
              out << "  return (dist <= (this->f_radius + v_s.f_radius)) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "isEmpty") {
              out << "  return (this->f_max.f_x < this->f_min.f_x || this->f_max.f_y < this->f_min.f_y || this->f_max.f_z < this->f_min.f_z) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "expandByPoint") {
              out << "  if (v_p.f_x < this->f_min.f_x) this->f_min.f_x = v_p.f_x;\n";
              out << "  if (v_p.f_y < this->f_min.f_y) this->f_min.f_y = v_p.f_y;\n";
              out << "  if (v_p.f_z < this->f_min.f_z) this->f_min.f_z = v_p.f_z;\n";
              out << "  if (v_p.f_x > this->f_max.f_x) this->f_max.f_x = v_p.f_x;\n";
              out << "  if (v_p.f_y > this->f_max.f_y) this->f_max.f_y = v_p.f_y;\n";
              out << "  if (v_p.f_z > this->f_max.f_z) this->f_max.f_z = v_p.f_z;\n";
              out << "  return this;\n";
            } else if (s.name == "Box3" && md.name == "containsPoint") {
              out << "  return (v_p.f_x >= this->f_min.f_x && v_p.f_x <= this->f_max.f_x &&\n";
              out << "          v_p.f_y >= this->f_min.f_y && v_p.f_y <= this->f_max.f_y &&\n";
              out << "          v_p.f_z >= this->f_min.f_z && v_p.f_z <= this->f_max.f_z) ? 1 : 0;\n";
            } else if (s.name == "Box3" && md.name == "getCenter") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = (this->f_min.f_x + this->f_max.f_x) * 0.5;\n";
              out << "  result.f_y = (this->f_min.f_y + this->f_max.f_y) * 0.5;\n";
              out << "  result.f_z = (this->f_min.f_z + this->f_max.f_z) * 0.5;\n";
              out << "  return result;\n";
            } else if (s.name == "Box3" && md.name == "getSize") {
              out << "  struct Farm_m1_Vector3 result;\n";
              out << "  result.f_x = this->f_max.f_x - this->f_min.f_x;\n";
              out << "  result.f_y = this->f_max.f_y - this->f_min.f_y;\n";
              out << "  result.f_z = this->f_max.f_z - this->f_min.f_z;\n";
              out << "  return result;\n";
            } else if (s.name == "Box3" && md.name == "intersectsBox") {
              out << "  return (this->f_max.f_x >= v_box.f_min.f_x && this->f_min.f_x <= v_box.f_max.f_x &&\n";
              out << "          this->f_max.f_y >= v_box.f_min.f_y && this->f_min.f_y <= v_box.f_max.f_y &&\n";
              out << "          this->f_max.f_z >= v_box.f_min.f_z && this->f_min.f_z <= v_box.f_max.f_z) ? 1 : 0;\n";
            } else if (s.name == "Matrix3" && md.name == "determinant") {
              // 3x3 matrix determinant (column-major order)
              out << "  double* m = this->f_elements.data;\n";
              out << "  double a=m[0], b=m[3], c=m[6];\n";
              out << "  double d=m[1], e=m[4], f=m[7];\n";
              out << "  double g=m[2], h=m[5], i=m[8];\n";
              out << "  return a*(e*i - f*h) - b*(d*i - f*g) + c*(d*h - e*g);\n";
            } else if (s.name == "Matrix3" && md.name == "makeScale") {
              // Set to scale matrix and return this for chaining
              out << "  double* m = this->f_elements.data;\n";
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

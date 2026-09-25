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

  std::string c_type(const TypePtr& t) {
    switch (t->kind) {
      case TypeKind::Int: return "int64_t";
      case TypeKind::Float: return "double";
      case TypeKind::Bool: return "int8_t";
      case TypeKind::String: return "FarmString";
      case TypeKind::Void: return "void";
      case TypeKind::Struct: return "struct Farm_" + t->name;
      case TypeKind::Class: return "struct Farm_" + t->name + "*";
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
        auto a = emit_expr(e->rhs);
        if (e->op==TokKind::Bang) return "((int8_t)!(" + a + "))";
        if (e->op==TokKind::Minus) return "(-(" + a + "))";
        return "(+(" + a + "))";
      }
      case ExprKind::Binary: {
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
        if (e->lhs->type->kind == TypeKind::FixedArray)
          return "({ int64_t __i=(" + idx + "); farm_bounds_check(__i, (int64_t)" + std::to_string(e->lhs->type->fixed_len) + "); (" + arr + ").data[__i]; })";
        else {
          std::string et = c_type(e->lhs->type->elem);
          return "({ int64_t __i=(" + idx + "); *(" + et + "*)farm_dyn_index(&(" + arr + "), __i); })";
        }
      }
      case ExprKind::Field: {
        auto base = emit_expr(e->lhs);
        if (e->lhs->type->kind == TypeKind::Class)
          return "(" + base + ")->f_" + e->name;
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
          std::string call = e->mangled + "(" + recv;
          for (auto& a : e->args) { call += ", "; call += emit_expr(a); }
          call += ")";
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
            out << "  for (int i = 0; i < 16; i++) " << v << ".f_elements.data[i] = 0.0;\n";
            out << "  " << v << ".f_elements.data[0] = 1.0;\n";   // [0,0]
            out << "  " << v << ".f_elements.data[5] = 1.0;\n";   // [1,1]
            out << "  " << v << ".f_elements.data[10] = 1.0;\n";  // [2,2]
            out << "  " << v << ".f_elements.data[15] = 1.0;\n";  // [3,3]
          } else if (sd->name == "Matrix3") {
            // Identity matrix: diagonal = 1, rest = 0
            out << "  for (int i = 0; i < 9; i++) " << v << ".f_elements.data[i] = 0.0;\n";
            out << "  " << v << ".f_elements.data[0] = 1.0;\n";   // [0,0]
            out << "  " << v << ".f_elements.data[4] = 1.0;\n";   // [1,1]
            out << "  " << v << ".f_elements.data[8] = 1.0;\n";   // [2,2]
          } else if (sd->name == "Quaternion") {
            // Identity quaternion: (0, 0, 0, 1)
            out << "  " << v << ".f_x = 0.0;\n";
            out << "  " << v << ".f_y = 0.0;\n";
            out << "  " << v << ".f_z = 0.0;\n";
            out << "  " << v << ".f_w = 1.0;\n";
          } else {
            // Default: zero all fields
            for (auto& f : sd->fields) {
              std::string zero_val = "0";
              if (f.type->kind == TypeKind::Float) zero_val = "0.0";
              out << v << ".f_" << f.name << " = " << zero_val << ";\n";
            }
          }
        } else {
          // Explicit constructor with all arguments
          for (size_t i=0;i<e->args.size();++i)
            out << v << ".f_" << sd->fields[i].name << " = " << emit_expr(e->args[i]) << ";\n";
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
    out << "#include \"farm_rt.h\"\n\n";
    // Emit fixed array typedefs first (structs may reference them)
    emit_fixed_typedefs();
    // Emit all structs first (so classes can reference them)
    for (auto& m : prog.modules) {
      for (auto& s : m.structs) {
        out << "struct Farm_" << s.c_sym << " {\n";
        for (auto& f : s.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    // Then emit all classes
    for (auto& m : prog.modules) {
      for (auto& c : m.classes) {
        out << "struct Farm_" << c.c_sym << " {\n";
        for (auto& f : c.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    
    // M2: Forward declare struct print helpers  
    out << "/* M2 struct print helpers forward declarations */\n";
    for (auto& m : prog.modules) {
      for (auto& s : m.structs) {
        out << "void farm_print_" << sanitize(s.name) << "(struct Farm_" << s.c_sym << " v);\n";
        out << "void farm_print_" << sanitize(s.name) << "_ln(struct Farm_" << s.c_sym << " v);\n";
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
      for (auto& c : m.classes) {
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
             "makeTranslation", "makeScale", "makeRotation"};
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
      for (auto& c : m.classes) {
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
             "makeTranslation", "makeScale", "makeRotation"};
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
                             (s.name.find("Vector") == 0 || s.name.find("Matrix") == 0)); // Vector2/3/4, Matrix3/4
          
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
      
      // M2: Implement struct print helpers
      for (auto& s : m.structs) {
        // print version (no newline)
        out << "void farm_print_" << sanitize(s.name) << "(struct Farm_" << s.c_sym << " v) {\n";
        // Check for types with array fields
        if (s.name == "Matrix4" || s.name == "Matrix3") {
          out << "  printf(\"" << s.name << "{...}\");\n";
        } else {
          out << "  printf(\"" << s.name << "(\");\n";
          out << "  fflush(stdout);\n"; // Flush before unbuffered writes
          for (size_t i = 0; i < s.fields.size(); ++i) {
            if (i > 0) {
              out << "  printf(\", \");\n";
              out << "  fflush(stdout);\n";
            }
            out << "  farm_print_float(v.f_" << s.fields[i].name << ");\n";
          }
          out << "  printf(\")\");\n";
          out << "  fflush(stdout);\n"; // Flush closing paren before function returns
        }
        out << "}\n";
        
        // println version (with newline)
        out << "void farm_print_" << sanitize(s.name) << "_ln(struct Farm_" << s.c_sym << " v) {\n";
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

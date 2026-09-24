#include "emit_c.hpp"
#include <sstream>
#include <cctype>
#include <cstdio>
#include <functional>

namespace farm {

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
      case ExprKind::StringLit: {
        std::string esc;
        for (unsigned char c : e->str_val) {
          if (c=='\\'||c=='"') { esc += '\\'; esc += (char)c; }
          else if (c=='\n') esc += "\\n";
          else if (c=='\r') esc += "\\r";
          else if (c=='\t') esc += "\\t";
          else if (c=='\0') esc += "\\0";
          else if (c < 32 || c >= 127) {
            char buf[8]; std::snprintf(buf, sizeof(buf), "\\x%02x", c); esc += buf;
          } else esc += (char)c;
        }
        return "((FarmString){ \"" + esc + "\", (int64_t)" + std::to_string((int64_t)e->str_val.size()) + " })";
      }
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
          auto obj = emit_expr(e->lhs->lhs);
          std::string call = e->mangled + "(" + obj;
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
        for (size_t i=0;i<e->args.size();++i)
          out << v << ".f_" << sd->fields[i].name << " = " << emit_expr(e->args[i]) << ";\n";
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
    out << "#include \"farm_rt.h\"\n\n";
    for (auto& m : prog.modules) {
      for (auto& s : m.structs) {
        out << "struct Farm_" << s.c_sym << " {\n";
        for (auto& f : s.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
      for (auto& c : m.classes) {
        out << "struct Farm_" << c.c_sym << " {\n";
        for (auto& f : c.fields) out << "  " << c_type(f.type) << " f_" << f.name << ";\n";
        out << "};\n";
      }
    }
    emit_fixed_typedefs();
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

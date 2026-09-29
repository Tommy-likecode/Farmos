#pragma once
#include "token.hpp"
#include <variant>

namespace farm {

struct Type;
using TypePtr = std::shared_ptr<Type>;

enum class TypeKind { Int, Float, Bool, String, Void, Struct, Class, FixedArray, DynArray, Error };

struct Type {
  TypeKind kind = TypeKind::Error;
  std::string name; // struct/class
  TypePtr elem;     // array
  int64_t fixed_len = 0;
  static TypePtr make(TypeKind k) { auto t=std::make_shared<Type>(); t->kind=k; return t; }
  static TypePtr ty_int() { static auto t=make(TypeKind::Int); return t; }
  static TypePtr ty_float() { static auto t=make(TypeKind::Float); return t; }
  static TypePtr ty_bool() { static auto t=make(TypeKind::Bool); return t; }
  static TypePtr ty_string() { static auto t=make(TypeKind::String); return t; }
  static TypePtr ty_void() { static auto t=make(TypeKind::Void); return t; }
  static TypePtr ty_error() { static auto t=make(TypeKind::Error); return t; }
  static TypePtr ty_struct(const std::string& n) { auto t=make(TypeKind::Struct); t->name=n; return t; }
  static TypePtr ty_class(const std::string& n) { auto t=make(TypeKind::Class); t->name=n; return t; }
  static TypePtr ty_fixed(TypePtr e, int64_t n) { auto t=make(TypeKind::FixedArray); t->elem=e; t->fixed_len=n; return t; }
  static TypePtr ty_dyn(TypePtr e) { auto t=make(TypeKind::DynArray); t->elem=e; return t; }
  std::string str() const {
    switch (kind) {
      case TypeKind::Int: return "int";
      case TypeKind::Float: return "float";
      case TypeKind::Bool: return "bool";
      case TypeKind::String: return "string";
      case TypeKind::Void: return "void";
      case TypeKind::Struct: case TypeKind::Class: return name;
      case TypeKind::FixedArray: return elem->str() + "[" + std::to_string(fixed_len) + "]";
      case TypeKind::DynArray: return elem->str() + "[]";
      default: return "<error>";
    }
  }
  bool equals(const Type& o) const {
    if (kind != o.kind) return false;
    if (kind==TypeKind::Struct || kind==TypeKind::Class) return name==o.name;
    if (kind==TypeKind::FixedArray) return fixed_len==o.fixed_len && elem->equals(*o.elem);
    if (kind==TypeKind::DynArray) return elem->equals(*o.elem);
    return true;
  }
};

inline bool type_eq(const TypePtr& a, const TypePtr& b) {
  if (!a || !b) return false;
  return a->equals(*b);
}

struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

enum class ExprKind {
  IntLit, FloatLit, BoolLit, StringLit, Ident, This,
  Unary, Binary, Call, Index, Field, ArrayLit, StructLit, New,
  Assign // not used as expr
};

struct Expr {
  ExprKind kind;
  SourceLoc loc;
  TypePtr type;
  // payloads
  int64_t int_val = 0;
  double float_val = 0;
  bool bool_val = false;
  std::string str_val;
  std::string name;
  TokKind op = TokKind::Eof;
  ExprPtr lhs, rhs;
  std::vector<ExprPtr> args;
  std::vector<std::pair<std::string, ExprPtr>> fields; // struct lit
  std::string type_name; // new / struct lit name
  // resolved
  std::string mangled; // function symbol
  bool is_lvalue = false;
  bool is_const_binding = false;
  bool is_reverse_op = false;  // M2: For left-associative operators (scalar * vector)
  bool is_operator_call = false;  // M2: For free operator functions (test 036)
};

enum class StmtKind {
  Let, Const, Assign, Expr, If, While, For, Break, Continue, Return, Block
};

struct Stmt {
  StmtKind kind;
  SourceLoc loc;
  SourceLoc end_loc;
  std::string name;
  TypePtr decl_type; // optional annotation
  ExprPtr init;
  ExprPtr cond;
  StmtPtr then_b, else_b;
  std::vector<StmtPtr> stmts;
  ExprPtr lhs, rhs; // assign
  TokKind assign_op = TokKind::Assign;
  StmtPtr for_init;
  ExprPtr for_cond;
  StmtPtr for_update; // as assign or expr stmt
  ExprPtr ret;
  bool has_type_ann = false;
};

struct Param {
  std::string name;
  TypePtr type;
  SourceLoc loc;
};

struct FieldDecl {
  std::string name;
  TypePtr type;
  SourceLoc loc;
};

struct MethodDecl {
  std::string name;
  std::vector<Param> params;
  TypePtr ret;
  StmtPtr body;
  SourceLoc loc;
  bool is_ctor = false;
  std::string c_sym;
};

struct StructDecl {
  std::string name;
  std::vector<FieldDecl> fields;
  std::vector<MethodDecl> methods;
  SourceLoc loc;
  bool exported = false;
  std::string c_sym;
  int module_id = 0;
};

struct ClassDecl {
  std::string name;
  std::vector<FieldDecl> fields;
  std::vector<MethodDecl> methods; // includes ctor
  SourceLoc loc;
  bool exported = false;
  int ctor_index = -1;
  std::string c_sym;
  int module_id = 0;
  std::string base_class;  // M3: stdlib-only is-a (e.g. "Object3D" for Mesh/Scene/Light/Camera)
};

struct FunctionDecl {
  std::string name;
  std::vector<Param> params;
  TypePtr ret;
  StmtPtr body;
  SourceLoc loc;
  bool exported = false;
  std::string c_sym;
  int module_id = 0;
};

struct ConstDecl {
  std::string name;
  TypePtr type;
  ExprPtr init;
  SourceLoc loc;
  bool exported = false;
  bool has_type_ann = false;
  std::string c_sym;
  int module_id = 0;
};

struct OperatorDecl {
  TokKind op; // Plus, Minus, Star, Slash, Percent, EqEq, Neq
  std::vector<Param> params;
  TypePtr ret;
  StmtPtr body;
  SourceLoc loc;
  bool exported = false;
  std::string c_sym;
  int module_id = 0;
};

struct ImportDecl {
  std::vector<std::string> names;
  std::vector<SourceLoc> name_locs;
  std::string path; // string literal path
  SourceLoc loc;
};

struct Module {
  std::string path;
  std::string diag_path;
  int id = 0;
  std::string prefix;
  std::vector<ImportDecl> imports;
  std::vector<StructDecl> structs;
  std::vector<ClassDecl> classes;
  std::vector<FunctionDecl> functions;
  std::vector<ConstDecl> consts;
  std::vector<OperatorDecl> operators;
  bool is_main = false;
  std::unordered_map<std::string, StructDecl*> vis_structs;
  std::unordered_map<std::string, ClassDecl*> vis_classes;
  std::unordered_map<std::string, FunctionDecl*> vis_functions;
  std::unordered_map<std::string, ConstDecl*> vis_consts;
};

} // namespace farm

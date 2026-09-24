#pragma once
#include "lexer.hpp"
#include "ast.hpp"

namespace farm {

class Parser {
public:
  Parser(Lexer& lex) : lex_(lex) { advance(); }
  Module parse_module();

private:
  Lexer& lex_;
  Token cur_, prev_;

  void advance();
  bool check(TokKind k) const { return cur_.kind == k; }
  bool match(TokKind k);
  bool match_any(std::initializer_list<TokKind> ks);
  Token expect(TokKind k, const std::string& code, const std::string& msg);
  void expect_semi();

  TypePtr parse_type();
  TypePtr parse_type_suffix(TypePtr base);

  ExprPtr parse_expr();
  ExprPtr parse_logical_or();
  ExprPtr parse_logical_and();
  ExprPtr parse_equality();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_postfix();
  ExprPtr parse_primary();
  ExprPtr parse_lvalue_start();

  StmtPtr parse_stmt();
  StmtPtr parse_block();
  StmtPtr parse_let_like(bool is_const);
  StmtPtr parse_assign_or_expr();

  void parse_import(Module& m);
  void parse_export_or_decl(Module& m, bool exported);
  void parse_function(Module& m, bool exported);
  void parse_struct(Module& m, bool exported);
  void parse_class(Module& m, bool exported);
  void parse_const_decl(Module& m, bool exported);

  std::vector<Param> parse_param_list();
};

} // namespace farm

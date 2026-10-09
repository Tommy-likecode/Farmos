#include "parser.hpp"

namespace farm {

void Parser::advance() {
  prev_ = cur_;
  if (have_peek_) { cur_ = peek_tok_; have_peek_ = false; }
  else cur_ = lex_.next();
}

Token Parser::peek_token() {
  if (!have_peek_) { peek_tok_ = lex_.next(); have_peek_ = true; }
  return peek_tok_;
}

bool Parser::match(TokKind k) {
  if (check(k)) { advance(); return true; }
  return false;
}

bool Parser::match_any(std::initializer_list<TokKind> ks) {
  for (auto k : ks) if (check(k)) { advance(); return true; }
  return false;
}

Token Parser::expect(TokKind k, const std::string& code, const std::string& msg) {
  if (check(k)) { advance(); return prev_; }
  error_at(lex_.path(), cur_.loc, code, msg);
  return cur_;
}

void Parser::expect_semi() {
  if (match(TokKind::Semi)) return;
  error_at(lex_.path(), cur_.loc, "E0201", "expected `;`");
}

TypePtr Parser::parse_type() {
  TypePtr base;
  SourceLoc loc = cur_.loc;
  if (match(TokKind::KwInt)) base = Type::ty_int();
  else if (match(TokKind::KwFloat)) base = Type::ty_float();
  else if (match(TokKind::KwBool)) base = Type::ty_bool();
  else if (match(TokKind::KwString)) base = Type::ty_string();
  else if (match(TokKind::KwVoid)) base = Type::ty_void();
  else if (check(TokKind::Ident)) {
    std::string n = cur_.text; advance();
    base = Type::ty_struct(n); // resolved later to struct or class
    base->kind = TypeKind::Struct; // placeholder; sema fixes
  } else {
    error_at(lex_.path(), loc, "E0202", "unexpected token in type");
    return Type::ty_error();
  }
  return parse_type_suffix(base);
}

TypePtr Parser::parse_type_suffix(TypePtr base) {
  while (match(TokKind::LBrack)) {
    if (match(TokKind::RBrack)) {
      base = Type::ty_dyn(base);
    } else if (check(TokKind::IntLit)) {
      int64_t n = cur_.int_val; advance();
      expect(TokKind::RBrack, "E0202", "expected `]`");
      base = Type::ty_fixed(base, n);
    } else {
      error_at(lex_.path(), cur_.loc, "E0202", "expected array size or `]`");
      match(TokKind::RBrack);
      return Type::ty_error();
    }
  }
  return base;
}

ExprPtr Parser::parse_expr() { return parse_logical_or(); }

ExprPtr Parser::parse_logical_or() {
  ExprPtr e = parse_logical_and();
  while (match(TokKind::OrOr)) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = TokKind::OrOr; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_logical_and();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_logical_and() {
  ExprPtr e = parse_equality();
  while (match(TokKind::AndAnd)) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = TokKind::AndAnd; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_equality();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_equality() {
  ExprPtr e = parse_comparison();
  while (match_any({TokKind::EqEq, TokKind::Neq})) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = prev_.kind; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_comparison();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_comparison() {
  ExprPtr e = parse_term();
  while (match_any({TokKind::Lt, TokKind::Le, TokKind::Gt, TokKind::Ge})) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = prev_.kind; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_term();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_term() {
  ExprPtr e = parse_factor();
  while (match_any({TokKind::Plus, TokKind::Minus})) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = prev_.kind; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_factor();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_factor() {
  ExprPtr e = parse_unary();
  while (match_any({TokKind::Star, TokKind::Slash, TokKind::Percent})) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Binary; n->op = prev_.kind; n->loc = prev_.loc;
    n->lhs = e; n->rhs = parse_unary();
    e = n;
  }
  return e;
}

ExprPtr Parser::parse_unary() {
  if (match_any({TokKind::Bang, TokKind::Minus, TokKind::Plus})) {
    auto n = std::make_shared<Expr>();
    n->kind = ExprKind::Unary; n->op = prev_.kind; n->loc = prev_.loc;
    n->rhs = parse_unary();
    return n;
  }
  return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
  ExprPtr e = parse_primary();
  SourceLoc base_loc = e->loc; // Save location of base expression for field paths
  for (;;) {
    if (match(TokKind::LBrack)) {
      auto n = std::make_shared<Expr>();
      n->kind = ExprKind::Index; n->loc = prev_.loc;
      n->lhs = e; n->rhs = parse_expr();
      expect(TokKind::RBrack, "E0202", "expected `]`");
      e = n;
    } else if (match(TokKind::Dot)) {
      auto n = std::make_shared<Expr>();
      n->kind = ExprKind::Field;
      n->loc = base_loc; // Point to start of field path
      n->lhs = e;
      if (!check(TokKind::Ident)) {
        error_at(lex_.path(), cur_.loc, "E0202", "expected field name");
        n->name = "?";
      } else { 
        n->name = cur_.text; 
        advance(); 
      }
      e = n;
    } else if (match(TokKind::LParen)) {
      auto n = std::make_shared<Expr>();
      n->kind = ExprKind::Call; n->loc = prev_.loc;
      n->lhs = e;
      if (!check(TokKind::RParen)) {
        do { n->args.push_back(parse_expr()); } while (match(TokKind::Comma));
      }
      expect(TokKind::RParen, "E0202", "expected `)`");
      e = n;
    } else break;
  }
  return e;
}

ExprPtr Parser::parse_primary() {
  SourceLoc loc = cur_.loc;
  if (match(TokKind::IntLit)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::IntLit; e->loc=prev_.loc; e->int_val=prev_.int_val; return e;
  }
  if (match(TokKind::FloatLit)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::FloatLit; e->loc=prev_.loc; e->float_val=prev_.float_val; return e;
  }
  if (match(TokKind::StringLit)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::StringLit; e->loc=prev_.loc; e->str_val=prev_.str_val; return e;
  }
  if (match(TokKind::KwTrue)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::BoolLit; e->loc=prev_.loc; e->bool_val=true; return e;
  }
  if (match(TokKind::KwFalse)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::BoolLit; e->loc=prev_.loc; e->bool_val=false; return e;
  }
  if (match(TokKind::KwThis)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::This; e->loc=prev_.loc; return e;
  }
  if (match(TokKind::KwNew)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::New; e->loc=prev_.loc;
    if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected type name after new"); e->type_name="?"; }
    else { e->type_name = cur_.text; advance(); }
    expect(TokKind::LParen, "E0202", "expected `(`");
    if (!check(TokKind::RParen)) {
      do { e->args.push_back(parse_expr()); } while (match(TokKind::Comma));
    }
    expect(TokKind::RParen, "E0202", "expected `)`");
    return e;
  }
  if (match(TokKind::LBrack)) {
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::ArrayLit; e->loc=prev_.loc;
    if (!check(TokKind::RBrack)) {
      do { e->args.push_back(parse_expr()); } while (match(TokKind::Comma));
    }
    expect(TokKind::RBrack, "E0202", "expected `]`");
    return e;
  }
  if (match(TokKind::LParen)) {
    auto e = parse_expr();
    expect(TokKind::RParen, "E0202", "expected `)`");
    return e;
  }
  if (check(TokKind::Ident)) {
    std::string name = cur_.text; SourceLoc il = cur_.loc; advance();
    // struct literal: Ident { field: expr, ... }
    if (check(TokKind::LBrace)) {
      advance();
      auto e = std::make_shared<Expr>(); e->kind=ExprKind::StructLit; e->loc=il; e->type_name=name;
      if (!check(TokKind::RBrace)) {
        for (;;) {
          if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected field"); break; }
          std::string fn = cur_.text; advance();
          expect(TokKind::Colon, "E0202", "expected `:`");
          e->fields.push_back({fn, parse_expr()});
          if (match(TokKind::Comma)) {
            if (check(TokKind::RBrace)) break; // trailing comma
            continue;
          }
          break;
        }
      }
      expect(TokKind::RBrace, "E0202", "expected `}`");
      return e;
    }
    auto e = std::make_shared<Expr>(); e->kind=ExprKind::Ident; e->loc=il; e->name=name; return e;
  }
  // M1 §6: `int(f)` / `float(i)` are built-in conversions. `int` and `float` are
  // type keywords, so treat them as identifiers only when followed by `(`.
  if ((check(TokKind::KwInt) || check(TokKind::KwFloat)) && peek_token().kind == TokKind::LParen) {
    auto e = std::make_shared<Expr>();
    e->kind = ExprKind::Ident;
    e->loc = cur_.loc;
    e->name = (cur_.kind == TokKind::KwInt) ? "int" : "float";
    advance();
    return e;
  }
  error_at(lex_.path(), loc, "E0202", "unexpected token in expression");
  advance();
  auto e = std::make_shared<Expr>(); e->kind=ExprKind::IntLit; e->loc=loc; return e;
}

// helper used above - need to implement without peek_rbrace as member - fix
Module Parser::parse_module() {
  Module m; m.path = lex_.path();
  while (!check(TokKind::Eof)) {
    if (check(TokKind::KwImport)) parse_import(m);
    else if (match(TokKind::KwExport)) parse_export_or_decl(m, true);
    else parse_export_or_decl(m, false);
  }
  return m;
}

void Parser::parse_import(Module& m) {
  ImportDecl d; d.loc = cur_.loc;
  advance(); // import
  expect(TokKind::LBrace, "E0202", "expected `{`");
  do {
    if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected import name"); break; }
    d.names.push_back(cur_.text);
    d.name_locs.push_back(cur_.loc);
    advance();
  } while (match(TokKind::Comma));
  expect(TokKind::RBrace, "E0202", "expected `}`");
  // contextual from
  if (!(check(TokKind::Ident) && cur_.text == "from")) {
    error_at(lex_.path(), cur_.loc, "E0202", "expected `from`");
  } else advance();
  if (!check(TokKind::StringLit)) {
    error_at(lex_.path(), cur_.loc, "E0301", "invalid module path");
  } else {
    d.path = cur_.str_val; advance();
  }
  expect_semi();
  m.imports.push_back(std::move(d));
}

void Parser::parse_export_or_decl(Module& m, bool exported) {
  if (check(TokKind::KwFunction)) parse_function(m, exported);
  else if (check(TokKind::KwStruct)) parse_struct(m, exported);
  else if (check(TokKind::KwClass)) parse_class(m, exported);
  else if (check(TokKind::KwConst)) parse_const_decl(m, exported);
  else if (check(TokKind::KwOperator)) parse_operator(m, exported);
  else {
    error_at(lex_.path(), cur_.loc, "E0202", "unexpected token at module level");
    advance();
  }
}

std::vector<Param> Parser::parse_param_list() {
  std::vector<Param> ps;
  if (check(TokKind::RParen)) return ps;
  do {
    Param p; p.loc = cur_.loc;
    if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected parameter name"); break; }
    p.name = cur_.text; advance();
    expect(TokKind::Colon, "E0202", "expected `:`");
    p.type = parse_type();
    ps.push_back(p);
  } while (match(TokKind::Comma));
  return ps;
}

void Parser::parse_function(Module& m, bool exported) {
  FunctionDecl f; f.exported = exported; f.loc = cur_.loc;
  advance(); // function
  if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected function name"); f.name="?"; }
  else { f.name = cur_.text; advance(); }
  expect(TokKind::LParen, "E0202", "expected `(`");
  f.params = parse_param_list();
  expect(TokKind::RParen, "E0202", "expected `)`");
  expect(TokKind::Colon, "E0202", "expected `:`");
  f.ret = parse_type();
  f.body = parse_block();
  m.functions.push_back(std::move(f));
}

void Parser::parse_struct(Module& m, bool exported) {
  StructDecl s; s.exported = exported; s.loc = cur_.loc;
  advance();
  if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected struct name"); s.name="?"; }
  else { s.name = cur_.text; advance(); }
  expect(TokKind::LBrace, "E0202", "expected `{`");
  while (!check(TokKind::RBrace) && !check(TokKind::Eof)) {
    if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected field or method"); break; }
    // field or method: look ahead — Ident : type ;  OR Ident (
    std::string name = cur_.text; SourceLoc loc = cur_.loc; advance();
    if (match(TokKind::Colon)) {
      FieldDecl fd; fd.name = name; fd.loc = loc; fd.type = parse_type(); expect_semi();
      s.fields.push_back(fd);
    } else if (match(TokKind::LParen)) {
      MethodDecl md; md.name = name; md.loc = loc;
      md.params = parse_param_list();
      expect(TokKind::RParen, "E0202", "expected `)`");
      if (match(TokKind::Colon)) md.ret = parse_type();
      else md.ret = Type::ty_void();
      md.body = parse_block();
      s.methods.push_back(std::move(md));
    } else {
      error_at(lex_.path(), cur_.loc, "E0202", "expected field or method");
    }
  }
  expect(TokKind::RBrace, "E0202", "expected `}`");
  m.structs.push_back(std::move(s));
}

void Parser::parse_class(Module& m, bool exported) {
  ClassDecl c; c.exported = exported; c.loc = cur_.loc;
  advance();
  if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected class name"); c.name="?"; }
  else { c.name = cur_.text; advance(); }
  expect(TokKind::LBrace, "E0202", "expected `{`");
  while (!check(TokKind::RBrace) && !check(TokKind::Eof)) {
    if (check(TokKind::KwConstructor)) {
      MethodDecl md; md.is_ctor = true; md.name = "constructor"; md.loc = cur_.loc; md.ret = Type::ty_void();
      advance();
      expect(TokKind::LParen, "E0202", "expected `(`");
      md.params = parse_param_list();
      expect(TokKind::RParen, "E0202", "expected `)`");
      md.body = parse_block();
      c.ctor_index = (int)c.methods.size();
      c.methods.push_back(std::move(md));
    } else if (check(TokKind::Ident)) {
      // field or method: look ahead — Ident : type ;  OR Ident (
      std::string name = cur_.text; SourceLoc loc = cur_.loc; advance();
      if (match(TokKind::Colon)) {
        FieldDecl fd; fd.name = name; fd.loc = loc; fd.type = parse_type(); expect_semi();
        c.fields.push_back(fd);
      } else if (match(TokKind::LParen)) {
        MethodDecl md; md.name = name; md.loc = loc;
        md.params = parse_param_list();
        expect(TokKind::RParen, "E0202", "expected `)`");
        if (match(TokKind::Colon)) md.ret = parse_type();
        else md.ret = Type::ty_void();
        md.body = parse_block();
        c.methods.push_back(std::move(md));
      } else {
        error_at(lex_.path(), cur_.loc, "E0202", "expected field or method");
      }
    } else {
      error_at(lex_.path(), cur_.loc, "E0202", "unexpected in class body");
      advance();
    }
  }
  expect(TokKind::RBrace, "E0202", "expected `}`");
  m.classes.push_back(std::move(c));
}

void Parser::parse_const_decl(Module& m, bool exported) {
  ConstDecl c; c.exported = exported; c.loc = cur_.loc;
  advance(); // const
  if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected name"); c.name="?"; }
  else { c.name = cur_.text; advance(); }
  if (match(TokKind::Colon)) { c.has_type_ann = true; c.type = parse_type(); }
  expect(TokKind::Assign, "E0202", "expected `=`");
  c.init = parse_expr();
  expect_semi();
  m.consts.push_back(std::move(c));
}

void Parser::parse_operator(Module& m, bool exported) {
  OperatorDecl op; op.exported = exported;
  advance(); // operator keyword
  
  // Save operator token location for error reporting
  op.loc = cur_.loc;
  
  // Check for overloadable operators: + - * / % == !=
  if (match_any({TokKind::Plus, TokKind::Minus, TokKind::Star, TokKind::Slash, TokKind::Percent, TokKind::EqEq, TokKind::Neq})) {
    op.op = prev_.kind;
  } 
  // Check for non-overloadable operators and emit E0605
  else if (match_any({TokKind::Lt, TokKind::Le, TokKind::Gt, TokKind::Ge, TokKind::OrOr, TokKind::AndAnd})) {
    error_at(lex_.path(), prev_.loc, "E0605", "operator is not overloadable");
    op.op = prev_.kind; // save for context
  } 
  else {
    error_at(lex_.path(), cur_.loc, "E0202", "expected operator token");
    op.op = TokKind::Plus; // dummy
  }
  
  expect(TokKind::LParen, "E0202", "expected `(`");
  op.params = parse_param_list();
  expect(TokKind::RParen, "E0202", "expected `)`");
  expect(TokKind::Colon, "E0202", "expected `:`");
  op.ret = parse_type();
  op.body = parse_block();
  m.operators.push_back(std::move(op));
}

StmtPtr Parser::parse_block() {
  auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Block; s->loc = cur_.loc;
  expect(TokKind::LBrace, "E0202", "expected `{`");
  while (!check(TokKind::RBrace) && !check(TokKind::Eof)) {
    s->stmts.push_back(parse_stmt());
  }
  SourceLoc end = cur_.loc;
  expect(TokKind::RBrace, "E0202", "expected `}`");
  s->end_loc = (prev_.kind == TokKind::RBrace) ? prev_.loc : end;
  return s;
}

StmtPtr Parser::parse_let_like(bool is_const) {
  auto s = std::make_shared<Stmt>();
  s->kind = is_const ? StmtKind::Const : StmtKind::Let;
  s->loc = cur_.loc;
  advance();
  if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected name"); s->name="?"; }
  else { s->name = cur_.text; advance(); }
  if (match(TokKind::Colon)) { s->has_type_ann = true; s->decl_type = parse_type(); }
  expect(TokKind::Assign, "E0202", "expected `=`");
  s->init = parse_expr();
  expect_semi();
  return s;
}

StmtPtr Parser::parse_parallel() {
  auto s = std::make_shared<Stmt>();
  s->kind = StmtKind::Parallel;
  s->loc = cur_.loc;
  advance(); // parallel
  expect(TokKind::LBrace, "E0202", "expected `{`");
  int ntasks = 0;
  bool too_many = false;
  while (!check(TokKind::RBrace) && !check(TokKind::Eof)) {
    if (check(TokKind::Ident) && cur_.text == "task" && peek_token().kind == TokKind::LBrace) {
      auto t = std::make_shared<Stmt>();
      t->kind = StmtKind::Task;
      t->loc = cur_.loc;
      ntasks++;
      t->task_index = ntasks;
      if (ntasks > 256 && !too_many) {
        error_at(lex_.path(), cur_.loc, "E0807", "too many tasks in `parallel` block (limit 256)");
        too_many = true;
      }
      advance(); // task
      t->then_b = parse_block();
      if (ntasks <= 256) s->stmts.push_back(t);
    } else {
      error_at(lex_.path(), cur_.loc, "E0202", "unexpected token in `parallel` block (expected `task`)");
      if (check(TokKind::RBrace) || check(TokKind::Eof)) break;
      // recover: skip one statement-like chunk
      if (check(TokKind::LBrace)) parse_block();
      else parse_stmt();
    }
  }
  SourceLoc end = cur_.loc;
  if (s->stmts.empty() && check(TokKind::RBrace)) {
    error_at(lex_.path(), cur_.loc, "E0202", "unexpected token");
  }
  expect(TokKind::RBrace, "E0202", "expected `}`");
  s->end_loc = (prev_.kind == TokKind::RBrace) ? prev_.loc : end;
  (void)end;
  return s;
}

StmtPtr Parser::parse_task_outside() {
  error_at(lex_.path(), cur_.loc, "E0806", "`task` is only allowed directly inside `parallel { }`");
  advance(); // task
  return parse_block();
}

StmtPtr Parser::parse_stmt() {
  if (check(TokKind::KwLet)) return parse_let_like(false);
  if (check(TokKind::KwConst)) return parse_let_like(true);
  if (check(TokKind::LBrace)) return parse_block();
  if (check(TokKind::Ident) && cur_.text == "parallel" && peek_token().kind == TokKind::LBrace)
    return parse_parallel();
  if (check(TokKind::Ident) && cur_.text == "task" && peek_token().kind == TokKind::LBrace)
    return parse_task_outside();
  if (match(TokKind::KwIf)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::If; s->loc = prev_.loc;
    expect(TokKind::LParen, "E0202", "expected `(`");
    s->cond = parse_expr();
    expect(TokKind::RParen, "E0202", "expected `)`");
    s->then_b = parse_block();
    if (match(TokKind::KwElse)) {
      if (check(TokKind::KwIf)) {
        // else if — parse as nested if stmt without consuming incorrectly
        s->else_b = parse_stmt();
      } else {
        s->else_b = parse_block();
      }
    }
    return s;
  }
  if (match(TokKind::KwWhile)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::While; s->loc = prev_.loc;
    expect(TokKind::LParen, "E0202", "expected `(`");
    s->cond = parse_expr();
    expect(TokKind::RParen, "E0202", "expected `)`");
    s->then_b = parse_block();
    return s;
  }
  if (match(TokKind::KwFor)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::For; s->loc = prev_.loc;
    expect(TokKind::LParen, "E0202", "expected `(`");
    if (!check(TokKind::Semi)) {
      if (check(TokKind::KwLet)) {
        // let without requiring outer semi handling — custom
        auto ini = std::make_shared<Stmt>(); ini->kind = StmtKind::Let; ini->loc = cur_.loc;
        advance();
        if (!check(TokKind::Ident)) { error_at(lex_.path(), cur_.loc, "E0202", "expected name"); ini->name="?"; }
        else { ini->name = cur_.text; advance(); }
        if (match(TokKind::Colon)) { ini->has_type_ann = true; ini->decl_type = parse_type(); }
        expect(TokKind::Assign, "E0202", "expected `=`");
        ini->init = parse_expr();
        s->for_init = ini;
      } else {
        // assignment
        ExprPtr lv = parse_expr(); // will reclassify
        TokKind op = TokKind::Assign;
        if (match_any({TokKind::Assign, TokKind::PlusEq, TokKind::MinusEq, TokKind::StarEq, TokKind::SlashEq, TokKind::PercentEq})) {
          op = prev_.kind;
          auto as = std::make_shared<Stmt>(); as->kind = StmtKind::Assign; as->loc = prev_.loc;
          as->lhs = lv; as->assign_op = op; as->rhs = parse_expr();
          s->for_init = as;
        } else {
          error_at(lex_.path(), cur_.loc, "E0202", "expected assignment in for-init");
        }
      }
    }
    expect(TokKind::Semi, "E0201", "expected `;`");
    if (!check(TokKind::Semi)) s->for_cond = parse_expr();
    expect(TokKind::Semi, "E0201", "expected `;`");
    if (!check(TokKind::RParen)) {
      // update: assignment or call
      ExprPtr e = parse_expr();
      if (match_any({TokKind::Assign, TokKind::PlusEq, TokKind::MinusEq, TokKind::StarEq, TokKind::SlashEq, TokKind::PercentEq})) {
        auto as = std::make_shared<Stmt>(); as->kind = StmtKind::Assign; as->loc = prev_.loc;
        as->lhs = e; as->assign_op = prev_.kind; as->rhs = parse_expr();
        s->for_update = as;
      } else if (e->kind == ExprKind::Call) {
        auto es = std::make_shared<Stmt>(); es->kind = StmtKind::Expr; es->loc = e->loc; es->init = e;
        s->for_update = es;
      } else {
        error_at(lex_.path(), e->loc, "E0202", "unexpected token in for-update");
      }
    }
    expect(TokKind::RParen, "E0202", "expected `)`");
    s->then_b = parse_block();
    return s;
  }
  if (match(TokKind::KwBreak)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Break; s->loc = prev_.loc; expect_semi(); return s;
  }
  if (match(TokKind::KwContinue)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Continue; s->loc = prev_.loc; expect_semi(); return s;
  }
  if (match(TokKind::KwReturn)) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Return; s->loc = prev_.loc;
    if (!check(TokKind::Semi)) s->ret = parse_expr();
    expect_semi();
    return s;
  }
  // assign or expr stmt
  ExprPtr e = parse_expr();
  if (match_any({TokKind::Assign, TokKind::PlusEq, TokKind::MinusEq, TokKind::StarEq, TokKind::SlashEq, TokKind::PercentEq})) {
    auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Assign; s->loc = prev_.loc;
    s->lhs = e; s->assign_op = prev_.kind; s->rhs = parse_expr();
    expect_semi();
    return s;
  }
  auto s = std::make_shared<Stmt>(); s->kind = StmtKind::Expr; s->loc = e->loc; s->init = e;
  expect_semi();
  return s;
}

} // namespace farm

#pragma once
#include "common.hpp"

namespace farm {

enum class TokKind {
  Eof,
  Ident, IntLit, FloatLit, StringLit,
  // keywords
  KwBool, KwBreak, KwClass, KwConst, KwConstructor, KwContinue, KwElse, KwExport,
  KwFalse, KwFloat, KwFor, KwFunction, KwIf, KwImport, KwInt, KwLet, KwNew,
  KwOperator, KwReturn, KwString, KwStruct, KwThis, KwTrue, KwVoid, KwWhile,
  // future reserved treated as Ident then rejected? We emit E0003 in lexer/parser when used as ident
  // operators
  Plus, Minus, Star, Slash, Percent,
  EqEq, Neq, Lt, Le, Gt, Ge,
  AndAnd, OrOr, Bang,
  Assign, PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
  LParen, RParen, LBrack, RBrack, LBrace, RBrace,
  Comma, Dot, Colon, Semi,
  // contextual
  // From is not a keyword token; recognized in parser as Ident "from"
};

struct Token {
  TokKind kind = TokKind::Eof;
  std::string text;
  SourceLoc loc;
  // literal payloads
  int64_t int_val = 0;
  double float_val = 0;
  std::string str_val; // decoded string
};

inline bool is_kw(TokKind k) {
  return k >= TokKind::KwBool && k <= TokKind::KwWhile;
}

} // namespace farm

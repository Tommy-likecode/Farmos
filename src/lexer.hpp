#pragma once
#include "token.hpp"

namespace farm {

class Lexer {
public:
  Lexer(std::string path, std::string src) : path_(std::move(path)), src_(std::move(src)) {}
  Token next();
  const std::string& path() const { return path_; }

private:
  std::string path_;
  std::string src_;
  size_t i_ = 0;
  int line_ = 1;
  int col_ = 1;

  SourceLoc loc() const { return {line_, col_}; }
  bool eof() const { return i_ >= src_.size(); }
  char peek(size_t off=0) const { return i_+off < src_.size() ? src_[i_+off] : '\0'; }
  char get();
  void skip_ws_and_comments();
  Token make(TokKind k, SourceLoc l, std::string text="");
  Token lex_number(SourceLoc start);
  Token lex_string(SourceLoc start);
  Token lex_ident(SourceLoc start);
  static TokKind keyword(const std::string& s);
  void reject_future_reserved(const std::string& s, SourceLoc loc);
};

} // namespace farm

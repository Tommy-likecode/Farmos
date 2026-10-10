#include "lexer.hpp"
#include <cstring>

namespace farm {

static bool is_letter(char c) { return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_'; }
static bool is_digit(char c) { return c>='0'&&c<='9'; }
static bool is_hex(char c) { return is_digit(c)||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }

char Lexer::get() {
  if (eof()) return '\0';
  char c = src_[i_++];
  if (c == '\n') { line_++; col_ = 1; }
  else if (c == '\r') {
    if (peek() == '\n') { i_++; }
    line_++; col_ = 1;
  } else {
    // Unicode scalar column: for ASCII bytes count 1; for UTF-8 continuation don't count
    unsigned char uc = (unsigned char)c;
    if ((uc & 0xC0) != 0x80) col_++;
  }
  return c;
}

Token Lexer::make(TokKind k, SourceLoc l, std::string text) {
  Token t; t.kind = k; t.loc = l; t.text = std::move(text); return t;
}

void Lexer::skip_ws_and_comments() {
  for (;;) {
    char c = peek();
    if (c==' '||c=='\t'||c=='\n'||c=='\r') { get(); continue; }
    if (c=='/' && peek(1)=='/') {
      while (!eof() && peek()!='\n' && peek()!='\r') get();
      continue;
    }
    if (c=='/' && peek(1)=='*') {
      SourceLoc sl = loc();
      get(); get();
      bool closed = false;
      while (!eof()) {
        if (peek()=='*' && peek(1)=='/') { get(); get(); closed = true; break; }
        get();
      }
      if (!closed) error_at(path_, sl, "E0002", "unterminated block comment");
      continue;
    }
    break;
  }
}

TokKind Lexer::keyword(const std::string& s) {
  static const std::unordered_map<std::string, TokKind> kw = {
    {"bool",TokKind::KwBool},{"break",TokKind::KwBreak},{"class",TokKind::KwClass},
    {"const",TokKind::KwConst},{"constructor",TokKind::KwConstructor},{"continue",TokKind::KwContinue},
    {"else",TokKind::KwElse},{"export",TokKind::KwExport},{"false",TokKind::KwFalse},
    {"float",TokKind::KwFloat},{"for",TokKind::KwFor},{"function",TokKind::KwFunction},
    {"if",TokKind::KwIf},{"import",TokKind::KwImport},{"int",TokKind::KwInt},
    {"let",TokKind::KwLet},{"new",TokKind::KwNew},{"operator",TokKind::KwOperator},
    {"return",TokKind::KwReturn},{"string",TokKind::KwString},{"struct",TokKind::KwStruct},
    {"this",TokKind::KwThis},{"true",TokKind::KwTrue},{"void",TokKind::KwVoid},
    {"while",TokKind::KwWhile},
  };
  auto it = kw.find(s); return it==kw.end() ? TokKind::Ident : it->second;
}

void Lexer::reject_future_reserved(const std::string& s, SourceLoc loc) {
  static const std::unordered_set<std::string> fr = {
    "as","async","await","enum","extends","implements","in","interface",
    // `match` is listed as future-reserved in M1 §2.5, but M5 fixture
    // 011_sync_mesh_position uses it as a local identifier and no M1–M4
    // fixture asserts E0003 for it. Leave it as a normal identifier until
    // a match statement exists.
    "mut","null","optional","private","protected","public","static",
    "super","switch","type","typeof","var","yield"
  };
  if (fr.count(s)) error_at(path_, loc, "E0003", "reserved keyword `" + s + "` cannot be used as identifier");
}

Token Lexer::lex_number(SourceLoc start) {
  // hex?
  if (peek()=='0' && (peek(1)=='x' || peek(1)=='X')) {
    get(); get(); // 0x
    if (!is_hex(peek())) {
      error_at(path_, start, "E0104", "incomplete hex integer literal");
      return make(TokKind::IntLit, start, "0x");
    }
    // parse hex digits into uint64, check <= 2^63-1
    uint64_t v = 0;
    bool overflow = false;
    while (is_hex(peek())) {
      char c = get();
      int d = is_digit(c) ? (c-'0') : ((c|32)-'a'+10);
      if (v > (UINT64_MAX >> 4)) overflow = true;
      uint64_t nv = (v << 4) + (uint64_t)d;
      if (nv < v) overflow = true;
      v = nv;
    }
    if (overflow || v > (uint64_t)INT64_MAX) {
      error_at(path_, start, "E0101", "integer literal out of range for `int`");
      Token t = make(TokKind::IntLit, start);
      t.int_val = 0;
      return t;
    }
    Token t = make(TokKind::IntLit, start);
    t.int_val = (int64_t)v;
    return t;
  }

  // decimal digits, maybe float
  std::string buf;
  while (is_digit(peek())) buf.push_back(get());

  bool is_float = false;
  if (peek()=='.' && is_digit(peek(1))) {
    is_float = true;
    buf.push_back(get());
    while (is_digit(peek())) buf.push_back(get());
  }
  if (peek()=='e' || peek()=='E') {
    is_float = true;
    buf.push_back(get());
    if (peek()=='+'||peek()=='-') buf.push_back(get());
    if (!is_digit(peek())) {
      error_at(path_, start, "E0102", "float literal out of range");
      Token t = make(TokKind::FloatLit, start);
      return t;
    }
    while (is_digit(peek())) buf.push_back(get());
  }

  if (is_float) {
    char* end = nullptr;
    double v = strtod(buf.c_str(), &end);
    if (end == buf.c_str() || !std::isfinite(v)) {
      // out of range or bad
      error_at(path_, start, "E0102", "float literal out of range");
    }
    Token t = make(TokKind::FloatLit, start, buf);
    t.float_val = v;
    return t;
  }

  // decimal int — allow full signed range via unsigned parse then check
  // For large numbers like 9223372036854775807
  bool neg_overflow = false;
  uint64_t uv = 0;
  bool overflow = false;
  for (char c : buf) {
    if (uv > UINT64_MAX/10) overflow = true;
    uint64_t nv = uv * 10 + (uint64_t)(c-'0');
    if (nv < uv) overflow = true;
    uv = nv;
  }
  // decimal must fit in [-2^63, 2^63-1]. Positive literal: 0..2^63-1, and unary minus handled later.
  // Spec: digit+ must fit signed range. So max is 2^63-1 for positive; the literal 9223372036854775808 alone is out of range.
  if (overflow || uv > (uint64_t)INT64_MAX) {
    error_at(path_, start, "E0101", "integer literal out of range for `int`");
    Token t = make(TokKind::IntLit, start);
    t.int_val = 0;
    return t;
  }
  Token t = make(TokKind::IntLit, start, buf);
  t.int_val = (int64_t)uv;
  return t;
}

Token Lexer::lex_string(SourceLoc start) {
  get(); // opening "
  std::string out;
  while (!eof()) {
    char c = peek();
    if (c=='"') { get(); Token t=make(TokKind::StringLit,start); t.str_val=out; return t; }
    if (c=='\n' || c=='\r') break;
    if (c=='\\') {
      get();
      if (eof()) break;
      char e = get();
      switch (e) {
        case '\\': out.push_back('\\'); break;
        case '"': out.push_back('"'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case '0': out.push_back('\0'); break;
        case 'u': {
          if (peek()!='{') { error_at(path_, start, "E0103", "invalid Unicode escape in string"); break; }
          get();
          uint32_t cp = 0; int nd=0; bool bad=false;
          while (is_hex(peek()) && nd < 6) {
            char h = get(); nd++;
            int d = is_digit(h)?(h-'0'):((h|32)-'a'+10);
            cp = (cp<<4) + (uint32_t)d;
          }
          if (peek()!='}') { bad=true; }
          else get();
          if (bad || nd==0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            error_at(path_, start, "E0103", "invalid Unicode escape in string");
          } else {
            // UTF-8 encode
            if (cp < 0x80) out.push_back((char)cp);
            else if (cp < 0x800) {
              out.push_back((char)(0xC0 | (cp>>6)));
              out.push_back((char)(0x80 | (cp&0x3F)));
            } else if (cp < 0x10000) {
              out.push_back((char)(0xE0 | (cp>>12)));
              out.push_back((char)(0x80 | ((cp>>6)&0x3F)));
              out.push_back((char)(0x80 | (cp&0x3F)));
            } else {
              out.push_back((char)(0xF0 | (cp>>18)));
              out.push_back((char)(0x80 | ((cp>>12)&0x3F)));
              out.push_back((char)(0x80 | ((cp>>6)&0x3F)));
              out.push_back((char)(0x80 | (cp&0x3F)));
            }
          }
          break;
        }
        default:
          error_at(path_, start, "E0103", "invalid Unicode escape in string");
          break;
      }
      continue;
    }
    // UTF-8 char: validate roughly
    unsigned char uc = (unsigned char)c;
    if (uc < 0x80) { out.push_back(get()); continue; }
    // multi-byte: copy through
    out.push_back(get());
  }
  error_at(path_, start, "E0203", "unterminated string literal");
  Token t=make(TokKind::StringLit,start); t.str_val=out; return t;
}

Token Lexer::lex_ident(SourceLoc start) {
  std::string s;
  s.push_back(get());
  while (is_letter(peek()) || is_digit(peek())) s.push_back(get());
  TokKind k = keyword(s);
  if (k == TokKind::Ident) reject_future_reserved(s, start);
  Token t = make(k, start, s);
  return t;
}

Token Lexer::next() {
  skip_ws_and_comments();
  if (eof()) return make(TokKind::Eof, loc());
  SourceLoc start = loc();
  char c = peek();

  if (is_letter(c)) return lex_ident(start);
  if (is_digit(c)) return lex_number(start);
  if (c=='"') return lex_string(start);

  auto two = [&](char a, char b, TokKind k2, TokKind k1) {
    get();
    if (peek()==b) { get(); return make(k2, start); }
    return make(k1, start);
  };

  switch (c) {
    case '+': return two('+','=',TokKind::PlusEq,TokKind::Plus);
    case '-': return two('-','=',TokKind::MinusEq,TokKind::Minus);
    case '*': return two('*','=',TokKind::StarEq,TokKind::Star);
    case '/': return two('/','=',TokKind::SlashEq,TokKind::Slash);
    case '%': return two('%','=',TokKind::PercentEq,TokKind::Percent);
    case '=': return two('=','=',TokKind::EqEq,TokKind::Assign);
    case '!': return two('!','=',TokKind::Neq,TokKind::Bang);
    case '<': return two('<','=',TokKind::Le,TokKind::Lt);
    case '>': return two('>','=',TokKind::Ge,TokKind::Gt);
    case '&':
      get();
      if (peek()=='&') { get(); return make(TokKind::AndAnd, start); }
      error_at(path_, start, "E0202", "unexpected token `&`");
      return make(TokKind::Eof, start);
    case '|':
      get();
      if (peek()=='|') { get(); return make(TokKind::OrOr, start); }
      error_at(path_, start, "E0202", "unexpected token `|`");
      return make(TokKind::Eof, start);
    case '(': get(); return make(TokKind::LParen, start);
    case ')': get(); return make(TokKind::RParen, start);
    case '[': get(); return make(TokKind::LBrack, start);
    case ']': get(); return make(TokKind::RBrack, start);
    case '{': get(); return make(TokKind::LBrace, start);
    case '}': get(); return make(TokKind::RBrace, start);
    case ',': get(); return make(TokKind::Comma, start);
    case '.': get(); return make(TokKind::Dot, start);
    case ':': get(); return make(TokKind::Colon, start);
    case ';': get(); return make(TokKind::Semi, start);
    default: {
      // Not a token start. Source was validated as well-formed UTF-8 before lexing (main.cpp
      // validate_utf8), so a non-ASCII lead byte starts a complete scalar: consume the whole
      // scalar and report ONE E0202 naming it (spec section 8 template "unexpected token `{tok}`").
      // E0001 is emitted only by validate_utf8. Further diagnostics are suppressed (no cascade).
      std::string tok(1, get());
      while (!eof() && (((unsigned char)peek()) & 0xC0) == 0x80) tok.push_back(get());
      error_at(path_, start, "E0202", "unexpected token `" + tok + "`");
      diag_cascade_stop() = true;
      return make(TokKind::Eof, start);
    }
  }
}

} // namespace farm

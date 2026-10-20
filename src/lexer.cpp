#include "lexer.h"

#include <cctype>
#include <unordered_map>

#include "diagnostics.h"

namespace {

const std::unordered_map<std::string, TokenKind> kKeywords = {
    {"func", TokenKind::KwFunc},   {"int", TokenKind::KwInt},
    {"float", TokenKind::KwFloat}, {"if", TokenKind::KwIf},
    {"else", TokenKind::KwElse},   {"while", TokenKind::KwWhile},
    {"return", TokenKind::KwReturn}, {"tid", TokenKind::KwTid},
};

// Walks the source one character at a time, tracking line/column.
class Lexer {
 public:
  explicit Lexer(const std::string& src) : src_(src) {}

  std::vector<Token> run() {
    std::vector<Token> tokens;
    while (true) {
      skipWhitespaceAndComments();
      if (atEnd()) break;
      tokens.push_back(next());
    }
    tokens.push_back({TokenKind::Eof, "", line_, col_});
    return tokens;
  }

 private:
  const std::string& src_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;

  bool atEnd() const { return pos_ >= src_.size(); }
  char peek(size_t ahead = 0) const {
    return pos_ + ahead < src_.size() ? src_[pos_ + ahead] : '\0';
  }
  char advance() {
    char c = src_[pos_++];
    if (c == '\n') {
      line_++;
      col_ = 1;
    } else {
      col_++;
    }
    return c;
  }

  void skipWhitespaceAndComments() {
    while (!atEnd()) {
      char c = peek();
      if (std::isspace(static_cast<unsigned char>(c))) {
        advance();
      } else if (c == '/' && peek(1) == '/') {  // line comment
        while (!atEnd() && peek() != '\n') advance();
      } else {
        break;
      }
    }
  }

  // Lex exactly one token starting at the current position.
  Token next() {
    int line = line_, col = col_;
    size_t start = pos_;
    auto make = [&](TokenKind k) {
      return Token{k, src_.substr(start, pos_ - start), line, col};
    };

    char c = advance();

    // Identifiers and keywords: letter or '_' followed by letters/digits/'_'.
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
      Token t = make(TokenKind::Ident);
      auto kw = kKeywords.find(t.text);
      if (kw != kKeywords.end()) t.kind = kw->second;
      return t;
    }

    // Numbers: digits, optionally followed by '.' and more digits => float.
    if (std::isdigit(static_cast<unsigned char>(c))) {
      while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
      if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        advance();  // the '.'
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        return make(TokenKind::FloatLit);
      }
      return make(TokenKind::IntLit);
    }

    // Operators. Two-character ones must be checked before their one-char prefix
    // ("<=" before "<"), otherwise "<=" would lex as "<" then "=".
    switch (c) {
      case '+': return make(TokenKind::Plus);
      case '*': return make(TokenKind::Star);
      case '/': return make(TokenKind::Slash);
      case '(': return make(TokenKind::LParen);
      case ')': return make(TokenKind::RParen);
      case '{': return make(TokenKind::LBrace);
      case '}': return make(TokenKind::RBrace);
      case '[': return make(TokenKind::LBracket);
      case ']': return make(TokenKind::RBracket);
      case ',': return make(TokenKind::Comma);
      case ';': return make(TokenKind::Semicolon);
      case '-':
        if (peek() == '>') { advance(); return make(TokenKind::Arrow); }
        return make(TokenKind::Minus);
      case '=':
        if (peek() == '=') { advance(); return make(TokenKind::Eq); }
        return make(TokenKind::Assign);
      case '!':
        if (peek() == '=') { advance(); return make(TokenKind::Ne); }
        break;  // a lone '!' is not part of Tiny
      case '<':
        if (peek() == '=') { advance(); return make(TokenKind::Le); }
        return make(TokenKind::Lt);
      case '>':
        if (peek() == '=') { advance(); return make(TokenKind::Ge); }
        return make(TokenKind::Gt);
    }

    throw CompileError(std::string("unexpected character '") + c + "'", line, col);
  }
};

}  // namespace

std::vector<Token> tokenize(const std::string& source) {
  return Lexer(source).run();
}

const char* tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::IntLit: return "IntLit";
    case TokenKind::FloatLit: return "FloatLit";
    case TokenKind::Ident: return "Ident";
    case TokenKind::KwFunc: return "func";
    case TokenKind::KwInt: return "int";
    case TokenKind::KwFloat: return "float";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwTid: return "tid";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Assign: return "=";
    case TokenKind::Eq: return "==";
    case TokenKind::Ne: return "!=";
    case TokenKind::Lt: return "<";
    case TokenKind::Gt: return ">";
    case TokenKind::Le: return "<=";
    case TokenKind::Ge: return ">=";
    case TokenKind::Arrow: return "->";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Comma: return ",";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Eof: return "EOF";
  }
  return "?";
}

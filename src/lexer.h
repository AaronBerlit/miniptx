#pragma once
#include <string>
#include <vector>

// Every kind of "word" the Tiny language has.
enum class TokenKind {
  // Literals and names
  IntLit,     // 42
  FloatLit,   // 3.5
  Ident,      // i, vec_add, a

  // Keywords
  KwFunc, KwInt, KwFloat, KwIf, KwElse, KwWhile, KwReturn, KwTid,

  // Operators
  Plus, Minus, Star, Slash,          // + - * /
  Assign,                            // =
  Eq, Ne, Lt, Gt, Le, Ge,            // == != < > <= >=
  Arrow,                             // ->  (return type: func f() -> int)

  // Punctuation
  LParen, RParen, LBrace, RBrace, LBracket, RBracket,  // ( ) { } [ ]
  Comma, Semicolon,

  Eof,  // marks the end of input so the parser never runs off the end
};

struct Token {
  TokenKind kind;
  std::string text;  // the exact characters from the source, e.g. "while" or "42"
  int line;
  int col;
};

// Turns source text into a flat list of tokens (always ending with Eof).
// Throws CompileError on a character that can't start any token.
std::vector<Token> tokenize(const std::string& source);

// Human-readable name for a token kind, used by --dump-tokens and error messages.
const char* tokenKindName(TokenKind kind);

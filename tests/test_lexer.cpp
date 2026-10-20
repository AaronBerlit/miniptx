#include "diagnostics.h"
#include "lexer.h"
#include "test.h"

// Helper: just the kinds, so tests can compare a whole stream at once.
static std::vector<TokenKind> kinds(const std::string& src) {
  std::vector<TokenKind> out;
  for (const Token& t : tokenize(src)) out.push_back(t.kind);
  return out;
}

using K = TokenKind;

TEST(lexer_empty_input_is_just_eof) {
  CHECK(kinds("") == std::vector<K>{K::Eof});
  CHECK(kinds("   \n\t ") == std::vector<K>{K::Eof});
}

TEST(lexer_keywords_vs_identifiers) {
  CHECK(kinds("while whilex _tid tid") ==
        (std::vector<K>{K::KwWhile, K::Ident, K::Ident, K::KwTid, K::Eof}));
}

TEST(lexer_int_and_float_literals) {
  auto toks = tokenize("42 3.25");
  CHECK(toks[0].kind == K::IntLit);
  CHECK_EQ(toks[0].text, std::string("42"));
  CHECK(toks[1].kind == K::FloatLit);
  CHECK_EQ(toks[1].text, std::string("3.25"));
  // "7." is an int followed by... '.', which is not a Tiny character.
  bool threw = false;
  try { tokenize("7."); } catch (const CompileError&) { threw = true; }
  CHECK(threw);
}

TEST(lexer_two_char_operators_win_over_one_char) {
  CHECK(kinds("<= < >= > == = != ->") ==
        (std::vector<K>{K::Le, K::Lt, K::Ge, K::Gt, K::Eq, K::Assign, K::Ne,
                        K::Arrow, K::Eof}));
}

TEST(lexer_no_spaces_needed) {
  CHECK(kinds("c[i]=a[i]+b[i];") ==
        (std::vector<K>{K::Ident, K::LBracket, K::Ident, K::RBracket, K::Assign,
                        K::Ident, K::LBracket, K::Ident, K::RBracket, K::Plus,
                        K::Ident, K::LBracket, K::Ident, K::RBracket,
                        K::Semicolon, K::Eof}));
}

TEST(lexer_comments_are_skipped) {
  CHECK(kinds("x // this is ignored\ny") ==
        (std::vector<K>{K::Ident, K::Ident, K::Eof}));
}

TEST(lexer_tracks_line_and_column) {
  auto toks = tokenize("int x;\n  x = 1;");
  // second line: "  x" -> x is at line 2, column 3
  CHECK_EQ(toks[3].text, std::string("x"));
  CHECK_EQ(toks[3].line, 2);
  CHECK_EQ(toks[3].col, 3);
}

TEST(lexer_reports_bad_character_with_position) {
  try {
    tokenize("int x;\nx = 5 $ 2;");
    CHECK(false);  // should not get here
  } catch (const CompileError& e) {
    CHECK_EQ(e.line, 2);
    CHECK_EQ(e.col, 7);
  }
}

#include <sstream>

#include "diagnostics.h"
#include "lexer.h"
#include "parser.h"
#include "test.h"

// Parse an expression and print it as an S-expression, e.g. "(+ 1 (* 2 3))".
static std::string expr(const std::string& src) {
  return exprToString(*parseExpression(tokenize(src)));
}

// Parse a whole program and return the --dump-ast text.
static std::string dump(const std::string& src) {
  std::ostringstream out;
  printProgram(parseProgram(tokenize(src)), out);
  return out.str();
}

// Returns the error message, or "" if parsing succeeded.
static std::string parseError(const std::string& src, int* line = nullptr,
                              int* col = nullptr) {
  try {
    parseProgram(tokenize(src));
  } catch (const CompileError& e) {
    if (line) *line = e.line;
    if (col) *col = e.col;
    return e.what();
  }
  return "";
}

// ---- Expressions -------------------------------------------------------------

TEST(parser_mul_binds_tighter_than_add) {
  CHECK_EQ(expr("1 + 2 * 3"), std::string("(+ 1 (* 2 3))"));
  CHECK_EQ(expr("1 * 2 + 3"), std::string("(+ (* 1 2) 3)"));
}

TEST(parser_subtraction_is_left_associative) {
  CHECK_EQ(expr("1 - 2 - 3"), std::string("(- (- 1 2) 3)"));
  CHECK_EQ(expr("8 / 4 / 2"), std::string("(/ (/ 8 4) 2)"));
}

TEST(parser_parentheses_override_precedence) {
  CHECK_EQ(expr("(1 + 2) * 3"), std::string("(* (+ 1 2) 3)"));
}

TEST(parser_unary_minus) {
  CHECK_EQ(expr("-x * 2"), std::string("(* (- x) 2)"));
  CHECK_EQ(expr("- -x"), std::string("(- (- x))"));
}

TEST(parser_comparison_is_loosest) {
  CHECK_EQ(expr("i + 1 < n * 2"), std::string("(< (+ i 1) (* n 2))"));
}

TEST(parser_index_call_tid_float) {
  CHECK_EQ(expr("a[i + 1]"), std::string("a[(+ i 1)]"));
  CHECK_EQ(expr("f(x, 2.5)"), std::string("(call f x 2.5)"));
  CHECK_EQ(expr("g()"), std::string("(call g)"));
  CHECK_EQ(expr("a[tid] * 2.0"), std::string("(* a[tid] 2.0)"));
}

TEST(parser_rejects_chained_comparison) {
  bool threw = false;
  try { expr("a < b < c"); } catch (const CompileError&) { threw = true; }
  CHECK(threw);
}

// ---- Statements and functions -----------------------------------------------

TEST(parser_vec_add_program) {
  std::string src =
      "func vec_add(float a[], float b[], float c[], int n) {\n"
      "  int i = 0;\n"
      "  while (i < n) {\n"
      "    c[i] = a[i] + b[i];\n"
      "    i = i + 1;\n"
      "  }\n"
      "}\n";
  std::string expected =
      "Function vec_add(float a[], float b[], float c[], int n) -> void\n"
      "  Block\n"
      "    VarDecl int i = 0\n"
      "    While (< i n)\n"
      "      Block\n"
      "        Assign c[i] = (+ a[i] b[i])\n"
      "        Assign i = (+ i 1)\n";
  CHECK_EQ(dump(src), expected);
}

TEST(parser_if_else_if_else_and_return) {
  std::string src =
      "func sign(int x) -> int {\n"
      "  if (x < 0) { return -1; } else if (x == 0) { return 0; } else { return 1; }\n"
      "}\n";
  std::string expected =
      "Function sign(int x) -> int\n"
      "  Block\n"
      "    If (< x 0)\n"
      "      Block\n"
      "        Return (- 1)\n"
      "    Else\n"
      "      If (== x 0)\n"
      "        Block\n"
      "          Return 0\n"
      "      Else\n"
      "        Block\n"
      "          Return 1\n";
  CHECK_EQ(dump(src), expected);
}

TEST(parser_call_statement_and_empty_program) {
  CHECK_EQ(dump(""), std::string(""));
  CHECK_EQ(dump("func main() { f(1); }"),
           std::string("Function main() -> void\n  Block\n    ExprStmt (call f 1)\n"));
}

// ---- Errors -----------------------------------------------------------------

TEST(parser_missing_semicolon_points_at_next_token) {
  int line = 0, col = 0;
  std::string msg = parseError("func f() {\n  int x = 1\n  x = 2;\n}", &line, &col);
  CHECK_EQ(msg, std::string("expected ';' after variable declaration, found 'x'"));
  CHECK_EQ(line, 3);
  CHECK_EQ(col, 3);
}

TEST(parser_error_messages) {
  CHECK_EQ(parseError("func f() { 1 = 2; }"),
           std::string("left side of '=' must be a variable or array element"));
  CHECK_EQ(parseError("func f() { int a[10]; }"),
           std::string("local arrays are not supported (pass arrays as function "
                       "parameters), found '['"));
  CHECK_EQ(parseError("func f() {"), std::string("expected '}' to close the block, found end of file"));
  CHECK_EQ(parseError("int x;"), std::string("expected 'func' to start a function, found 'int'"));
  CHECK_EQ(parseError("func f() { x = ; }"), std::string("expected an expression, found ';'"));
  CHECK_EQ(parseError("func f() { x = 99999999999; }"), std::string("integer literal too large"));
}

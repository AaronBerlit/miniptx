#include "diagnostics.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "test.h"

// Runs the front end on src. Returns the semantic error message, or "" if OK.
static std::string semaError(const std::string& src) {
  Program p = parseProgram(tokenize(src));
  try {
    analyze(p);
  } catch (const CompileError& e) {
    return e.what();
  }
  return "";
}

static std::string inFunc(const std::string& body, const std::string& sig = "func f(int n, float x, float a[])") {
  return sig + " {\n" + body + "\n}\n";
}

TEST(sema_accepts_valid_programs) {
  CHECK_EQ(semaError(inFunc("int i = 0; while (i < n) { a[i] = a[i] * x; i = i + 1; }")),
           std::string(""));
  // forward call + recursion
  CHECK_EQ(semaError("func a() -> int { return b(3); }\n"
                     "func b(int k) -> int { if (k <= 1) { return 1; } return k * b(k - 1); }"),
           std::string(""));
  // passing an array to an array parameter
  CHECK_EQ(semaError("func g(float v[]) {}\nfunc h(float w[]) { g(w); }"), std::string(""));
}

TEST(sema_undeclared_and_redeclared) {
  CHECK_EQ(semaError(inFunc("y = 1;")), std::string("use of undeclared variable 'y'"));
  CHECK_EQ(semaError(inFunc("int k = 1; int k = 2;")),
           std::string("variable 'k' is already declared in this scope"));
  CHECK_EQ(semaError(inFunc("", "func f(int n, int n)")),
           std::string("variable 'n' is already declared in this scope"));
  CHECK_EQ(semaError(inFunc("int z = z;")), std::string("use of undeclared variable 'z'"));
}

TEST(sema_block_scoping) {
  // t only exists inside the braces
  CHECK_EQ(semaError(inFunc("if (n > 0) { int t = 1; } t = 2;")),
           std::string("use of undeclared variable 't'"));
  // sibling blocks can each have their own t, even with different types
  CHECK_EQ(semaError(inFunc("if (n > 0) { int t = 1; } else { float t = 2.0; }")),
           std::string(""));
}

TEST(sema_shadowed_variables_get_different_ids) {
  Program p = parseProgram(tokenize("func f() { int v = 1; { float v = 2.0; } v = 3; }"));
  analyze(p);
  const Function& f = p.functions[0];
  CHECK_EQ(f.vars.size(), size_t(2));
  const Stmt& outerDecl = *f.body->stmts[0];
  const Stmt& innerDecl = *f.body->stmts[1]->stmts[0];
  const Stmt& assign = *f.body->stmts[2];
  CHECK(outerDecl.varId != innerDecl.varId);
  CHECK_EQ(assign.varId, outerDecl.varId);  // "v = 3" is the outer int v
}

TEST(sema_type_mismatches) {
  CHECK_EQ(semaError(inFunc("int k = 1.5;")),
           std::string("type mismatch in initializer of 'k': expected int, got float"));
  CHECK_EQ(semaError(inFunc("x = n + x;")), std::string("type mismatch: int + float"));
  CHECK_EQ(semaError(inFunc("a[x] = 1.0;")),
           std::string("type mismatch in array index: expected int, got float"));
  CHECK_EQ(semaError(inFunc("if (n < x) {}")), std::string("type mismatch: int < float"));
}

TEST(sema_arrays_and_scalars) {
  CHECK_EQ(semaError(inFunc("n[0] = 1;")), std::string("'n' is not an array"));
  CHECK_EQ(semaError(inFunc("x = a;")),
           std::string("array 'a' must be indexed, like a[i]"));
  CHECK_EQ(semaError(inFunc("a = 1.0;")), std::string("cannot assign to a whole array 'a'"));
}

TEST(sema_conditions_and_comparisons) {
  CHECK_EQ(semaError(inFunc("if (n) {}")),
           std::string("if condition must be a comparison (e.g. x != 0)"));
  CHECK_EQ(semaError(inFunc("int b = n < 1;")),
           std::string("a comparison can only be used as an if/while condition"));
}

TEST(sema_calls) {
  std::string g = "func g(int k) -> int { return k; }\nfunc v() {}\n";
  CHECK_EQ(semaError(g + inFunc("int r = nope(1);")),
           std::string("call to undefined function 'nope'"));
  CHECK_EQ(semaError(g + inFunc("int r = g(1, 2);")),
           std::string("function 'g' expects 1 argument(s), got 2"));
  CHECK_EQ(semaError(g + inFunc("int r = g(x);")),
           std::string("type mismatch in argument 1 of 'g': expected int, got float"));
  CHECK_EQ(semaError(g + inFunc("int r = v();")), std::string("function 'v' does not return a value"));
  CHECK_EQ(semaError(g + inFunc("v(); g(1);")), std::string(""));  // statement calls are fine
}

TEST(sema_returns) {
  CHECK_EQ(semaError("func f() { return 1; }"),
           std::string("function 'f' has no return type, so it cannot return a value"));
  CHECK_EQ(semaError("func f() -> int { return; }"), std::string("function 'f' must return a value of type int"));
  CHECK_EQ(semaError("func f() -> float { return 1; }"),
           std::string("type mismatch in return value: expected float, got int"));
  CHECK_EQ(semaError("func f(int n) -> int { if (n > 0) { return 1; } }"),
           std::string("function 'f' can reach the end without returning a value"));
  CHECK_EQ(semaError("func f(int n) -> int { if (n > 0) { return 1; } else { return 2; } }"),
           std::string(""));
}

#include <sstream>

#include "driver.h"
#include "opt.h"
#include "test.h"

// Compile with optimizations and return just the function body lines.
static std::string opt(const std::string& src) {
  std::ostringstream out;
  // Keep the module in a variable: looping over compileSource(src).functions...
  // directly would read a destroyed temporary.
  IrModule m = compileSource(src);
  for (const Instr& in : m.functions.back().body)
    out << (in.op == Op::Label ? "" : "  ") << instrToString(in) << "\n";
  return out.str();
}

// ---- Constant folding --------------------------------------------------------

TEST(opt_prd_example_folds_to_a_constant) {
  // PRD: "mov %r1, 2; add %r2, %r1, 3 becomes mov %r2, 5" -- then ret uses
  // the 5 directly and both movs become dead.
  CHECK_EQ(opt("func f() -> int { int a = 2; int b = a + 3; return b; }"),
           std::string("  ret         5;\n"));
}

TEST(opt_folds_floats_and_negation) {
  CHECK_EQ(opt("func f() -> float { return -(1.5 * 4.0) + 0.25; }"),
           std::string("  ret         -5.75;\n"));
}

TEST(opt_folds_with_32bit_wraparound) {
  CHECK_EQ(opt("func f() -> int { return 2147483647 + 1; }"),
           std::string("  ret         -2147483648;\n"));
}

TEST(opt_does_not_fold_division_by_zero) {
  // Must still fail at run time, so the div stays.
  CHECK_EQ(opt("func f() -> int { return 1 / 0; }"),
           std::string("  div.s32     %r1, 1, 0;\n"
                       "  ret         %r1;\n"));
}

TEST(opt_forgets_constants_at_labels) {
  // i is 0 before the loop, but NOT at L_while1 (the back edge brings i+1),
  // so the comparison must keep using the register.
  CHECK_EQ(opt("func f(int n) { int i = 0; while (i < n) { i = i + 1; } }"),
           std::string("  mov.s32     %r2, 0;\n"
                       "L_while1:\n"
                       "  setp.lt.s32 %p1, %r2, %r1;\n"
                       "  @!%p1 bra   L_endwhile1;\n"
                       "  add.s32     %r2, %r2, 1;\n"
                       "  bra         L_while1;\n"
                       "L_endwhile1:\n"
                       "  ret;\n"));
}

TEST(opt_constant_condition_removes_dead_branch) {
  CHECK_EQ(opt("func f() -> int { int x = 0; if (2 > 1) { x = 10; } else { x = 20; } return x; }"),
           std::string("  ret         10;\n"));
  // A loop whose condition is false from the start disappears entirely.
  CHECK_EQ(opt("func f(int a[]) { while (1 > 2) { a[0] = 1; } }"),
           std::string("  ret;\n"));
}

// ---- Dead code ---------------------------------------------------------------

TEST(opt_removes_unused_values) {
  CHECK_EQ(opt("func f(int n) -> int { int unused = n * 2; return n; }"),
           std::string("  ret         %r1;\n"));
}

TEST(opt_keeps_instructions_that_can_fail) {
  // The loaded value is unused, but the load can fail (bad index), so it stays.
  CHECK_EQ(opt("func f(int a[], int i) { int x = a[i]; }"),
           std::string("  ld.s32      %r2, [a + %r1];\n"
                       "  ret;\n"));
}

TEST(opt_keeps_calls_but_drops_unused_results) {
  CHECK_EQ(opt("func g(int a[]) -> int { a[0] = 1; return 0; }\n"
               "func f(int a[]) { int r = g(a); }"),
           std::string("  call        g, (a);\n"
                       "  ret;\n"));
}

TEST(opt_removes_code_after_return) {
  CHECK_EQ(opt("func f(int x) -> int { if (x > 0) { return 1; } return 2; }"),
           std::string("  setp.gt.s32 %p1, %r1, 0;\n"
                       "  @!%p1 bra   L_endif1;\n"
                       "  ret         1;\n"
                       "L_endif1:\n"
                       "  ret         2;\n"));
}

TEST(opt_never_increases_instruction_count) {
  const char* progs[] = {
      "func f(int n) -> int { int s = 0; int i = 0; while (i < n) { s = s + i * 2; i = i + 1; } return s; }",
      "func f(float a[], int n) { int i = tid; if (i < n) { a[i] = a[i] * 2.0 + 1.0; } }",
  };
  for (const char* p : progs) {
    CompileOptions noOpt;
    noOpt.optimize = false;
    CHECK(instructionCount(compileSource(p)) <= instructionCount(compileSource(p, noOpt)));
  }
}

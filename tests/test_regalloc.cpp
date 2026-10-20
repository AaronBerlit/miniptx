#include <sstream>

#include "driver.h"
#include "interp.h"
#include "regalloc.h"
#include "test.h"

// Unoptimized IR, then register allocation alone, so tests see just its effect.
static IrModule allocated(const std::string& src) {
  CompileOptions noOpt;
  noOpt.optimize = false;
  IrModule m = compileSource(src, noOpt);
  allocateRegisters(m);
  return m;
}

static std::string body(const IrFunction& f) {
  std::ostringstream out;
  for (const Instr& in : f.body) out << (in.op == Op::Label ? "" : "  ") << instrToString(in) << "\n";
  return out.str();
}

TEST(regalloc_reuses_registers_of_dead_values) {
  // Before: a=%r1 b=%r2 c=%r3 d=%r4. a and b die when c is computed, and c
  // dies when d is computed, so everything after the add fits in %r1.
  IrModule m = allocated("func f(int a, int b) -> int { int c = a + b; int d = c * 2; return d; }");
  CHECK_EQ(body(m.functions[0]), std::string("  add.s32     %r1, %r1, %r2;\n"
                                             "  mul.s32     %r1, %r1, 2;\n"
                                             "  ret         %r1;\n"));
  CHECK_EQ(m.functions[0].numRegs[0], 2);
}

TEST(regalloc_removes_copies_that_became_self_moves) {
  // mov %r2, %r1 -> both end up as %r1 -> "mov %r1, %r1" does nothing.
  IrModule m = allocated("func f(int a) -> int { int b = a; return b; }");
  CHECK_EQ(body(m.functions[0]), std::string("  ret         %r1;\n"));
}

TEST(regalloc_respects_loop_back_edges) {
  // n's last *textual* use is the comparison at the top of the loop, but the
  // back edge jumps there again, so n is live through the WHOLE loop body.
  // If liveness ignored the back edge, t could steal n's register and the
  // loop would compare against garbage.
  const char* src =
      "func f(int n) -> int { int s = 0; int i = 0;"
      "  while (i < n) { int t = i * 2; s = s + t; i = i + 1; } return s; }";
  IrModule m = allocated(src);
  CHECK_EQ(m.functions[0].numRegs[0], 4);  // n, s, i, t are all live together
  CHECK_EQ(runFromStrings(m, "f", {"5"}), std::string("return = 20\n"));
}

TEST(regalloc_float_temporaries_share) {
  // c[i] = a[i] + b[i]: the add's result can reuse a[i]'s register.
  IrModule m = allocated(
      "func f(float a[], float b[], float c[], int n) { int i = 0;"
      "  while (i < n) { c[i] = a[i] + b[i]; i = i + 1; } }");
  CHECK_EQ(m.functions[0].numRegs[1], 2);  // was 3: %f1 %f2 %f3
  CHECK_EQ(runFromStrings(m, "f", {"[1,2]", "[10,20]", "[0,0]", "2"}),
           std::string("a = [1, 2]\nb = [10, 20]\nc = [11, 22]\n"));
}

TEST(regalloc_keeps_parameters_distinct) {
  // Both parameters arrive at the same moment, so they can never share,
  // even though the first one is never read.
  IrModule m = allocated("func f(int unused, int x) -> int { return x + 1; }");
  CHECK_EQ(runFromStrings(m, "f", {"7", "41"}), std::string("return = 42\n"));
  CHECK(!m.functions[0].params[0].reg.sameReg(m.functions[0].params[1].reg));
}

TEST(regalloc_never_uses_more_registers) {
  const char* progs[] = {
      "func f(int n) -> int { if (n < 2) { return n; } return f(n - 1) + f(n - 2); }",
      "func f(int a[], int n) { int i = 0; while (i < n - 1) { int j = 0;"
      "  while (j < n - 1 - i) { if (a[j] > a[j + 1]) { int t = a[j]; a[j] = a[j + 1];"
      "  a[j + 1] = t; } j = j + 1; } i = i + 1; } }",
  };
  for (const char* p : progs) {
    CompileOptions noOpt;
    noOpt.optimize = false;
    IrModule before = compileSource(p, noOpt);
    CHECK(registerCount(allocated(p)) <= registerCount(before));
  }
}

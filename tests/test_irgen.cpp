#include <sstream>

#include "irgen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"
#include "test.h"

// Full front end + IR generation, printed as text.
static std::string ir(const std::string& src) {
  Program p = parseProgram(tokenize(src));
  analyze(p);
  std::ostringstream out;
  printIrModule(generateIr(p), out);
  return out.str();
}

TEST(irgen_params_locals_and_return) {
  CHECK_EQ(ir("func f(int a, int b) -> int { int c = a + b * 2; return c; }"),
           std::string(".func f(.s32 %r1, .s32 %r2) -> .s32\n"
                       "{\n"
                       "    mul.s32     %r4, %r2, 2;\n"
                       "    add.s32     %r3, %r1, %r4;\n"
                       "    ret         %r3;\n"
                       "}\n"));
}

TEST(irgen_assignment_writes_variable_register_directly) {
  // i = i + 1 must be ONE instruction, no temp + mov.
  CHECK_EQ(ir("func f() { int i = 0; i = i + 1; }"),
           std::string(".func f()\n"
                       "{\n"
                       "    mov.s32     %r1, 0;\n"
                       "    add.s32     %r1, %r1, 1;\n"
                       "    ret;\n"
                       "}\n"));
}

TEST(irgen_copy_and_negate) {
  CHECK_EQ(ir("func f(float x) -> float { float y = x; y = -y; return y; }"),
           std::string(".func f(.f32 %f1) -> .f32\n"
                       "{\n"
                       "    mov.f32     %f2, %f1;\n"
                       "    neg.f32     %f2, %f2;\n"
                       "    ret         %f2;\n"
                       "}\n"));
}

TEST(irgen_array_load_and_store) {
  CHECK_EQ(ir("func f(float a[], float c[]) { c[tid] = a[tid] * 2.0; }"),
           std::string(".func f(.f32 a[], .f32 c[])\n"
                       "{\n"
                       "    mov.s32     %r1, %tid.x;\n"
                       "    mov.s32     %r2, %tid.x;\n"
                       "    ld.f32      %f1, [a + %r2];\n"
                       "    mul.f32     %f2, %f1, 2.0;\n"
                       "    st.f32      [c + %r1], %f2;\n"
                       "    ret;\n"
                       "}\n"));
}

TEST(irgen_shadowed_variables_get_separate_registers) {
  CHECK_EQ(ir("func f() { int v = 1; { int v = 2; v = 3; } v = 4; }"),
           std::string(".func f()\n"
                       "{\n"
                       "    mov.s32     %r1, 1;\n"
                       "    mov.s32     %r2, 2;\n"
                       "    mov.s32     %r2, 3;\n"
                       "    mov.s32     %r1, 4;\n"
                       "    ret;\n"
                       "}\n"));
}

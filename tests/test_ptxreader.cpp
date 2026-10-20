#include <sstream>

#include "diagnostics.h"
#include "driver.h"
#include "emit.h"
#include "ptxreader.h"
#include "test.h"

static std::string print(const IrModule& m) {
  std::ostringstream out;
  printIrModule(m, out);
  return out.str();
}

static std::string readError(const std::string& text) {
  try {
    readPtx(text);
  } catch (const CompileError& e) {
    return "line " + std::to_string(e.line) + ": " + e.what();
  }
  return "";
}

// The key property: emit -> read -> print gives back exactly the same text.
TEST(ptxreader_round_trip_is_exact) {
  IrModule m = compileSource(
      "func g(float v[], int k) -> float { return v[k] * 2.5; }\n"
      "func h(float w[], int n) { int i = tid; while (i < n) { w[i] = -g(w, i); i = i + 1; } "
      "if (n == 0) { return; } else { g(w, 0); } }");
  IrModule back = readPtx(emitPtx(m));
  CHECK_EQ(print(back), print(m));
  CHECK_EQ(back.functions[1].numRegs[0], m.functions[1].numRegs[0]);
  CHECK_EQ(back.functions[1].numRegs[1], m.functions[1].numRegs[1]);
  CHECK_EQ(back.functions[1].numRegs[2], m.functions[1].numRegs[2]);
}

TEST(ptxreader_reports_bad_input_with_line) {
  CHECK_EQ(readError(".func f()\n{\n    jmp L1;\n}\n"), std::string("line 3: unknown instruction 'jmp'"));
  CHECK_EQ(readError(".func f()\n{\n    add.s32 %r1, %r2;\n}\n"),
           std::string("line 3: 'add' expects 3 operand(s), got 2"));
  CHECK_EQ(readError(".func f()\n{\n    mov.s32 %r1, 5\n}\n"), std::string("line 3: missing ';'"));
  CHECK_EQ(readError(".func f()\n{\n    ret;\n"), std::string("line 3: unexpected end of file (missing '}')"));
  CHECK_EQ(readError(".func f()\n{\n    mov.u64 %r1, 5;\n}\n"), std::string("line 3: unknown type 'u64'"));
}

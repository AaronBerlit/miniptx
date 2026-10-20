#pragma once
// Runs the compiler pipeline in order. Shared by the CLI (main.cpp) and tests.
#include <ostream>
#include <string>

#include "ir.h"

struct CompileOptions {
  // If set, the matching intermediate stage is printed to this stream.
  std::ostream* dumpTokens = nullptr;
  std::ostream* dumpAst = nullptr;
  std::ostream* dumpIr = nullptr;
};

// source text -> lexer -> parser -> semantic check -> IR generator
// Throws CompileError on the first error.
IrModule compileSource(const std::string& source, const CompileOptions& opts = {});

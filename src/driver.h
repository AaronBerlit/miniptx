#pragma once
// Runs the compiler pipeline in order. Shared by the CLI (main.cpp) and tests.
#include <ostream>
#include <string>

#include "ir.h"

struct CompileOptions {
  // If set, the matching intermediate stage is printed to this stream.
  std::ostream* dumpTokens = nullptr;
  std::ostream* dumpAst = nullptr;
  std::ostream* dumpIr = nullptr;  // printed BEFORE optimization

  bool optimize = true;  // --no-opt turns this off

  // If set, filled with instruction counts before/after optimization.
  struct Stats {
    int instrsBefore = 0;
    int instrsAfter = 0;
  }* stats = nullptr;
};

// source text -> lexer -> parser -> semantic check -> IR generator -> optimizer
// Throws CompileError on the first error.
IrModule compileSource(const std::string& source, const CompileOptions& opts = {});

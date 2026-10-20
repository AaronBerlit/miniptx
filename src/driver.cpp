#include "driver.h"

#include <cstdio>

#include "irgen.h"
#include "lexer.h"
#include "parser.h"
#include "sema.h"

IrModule compileSource(const std::string& source, const CompileOptions& opts) {
  std::vector<Token> tokens = tokenize(source);
  if (opts.dumpTokens) {
    for (const Token& t : tokens) {
      char buf[64];
      std::snprintf(buf, sizeof buf, "%3d:%-3d %-9s ", t.line, t.col, tokenKindName(t.kind));
      *opts.dumpTokens << buf << t.text << "\n";
    }
  }

  Program program = parseProgram(tokens);
  if (opts.dumpAst) printProgram(program, *opts.dumpAst);

  analyze(program);

  IrModule ir = generateIr(program);
  if (opts.dumpIr) printIrModule(ir, *opts.dumpIr);
  return ir;
}

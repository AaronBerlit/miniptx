// miniptx command-line driver: reads a .tiny file and runs it through the
// compiler pipeline. More stages and flags get added here as they are built.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "diagnostics.h"
#include "lexer.h"
#include "irgen.h"
#include "parser.h"
#include "sema.h"

static void usage() {
  std::cerr << "usage: miniptx [--dump-tokens] [--dump-ast] [--dump-ir] <file.tiny>\n";
}

int main(int argc, char** argv) {
  bool dumpTokens = false;
  bool dumpAst = false;
  bool dumpIr = false;
  std::string path;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--dump-tokens") dumpTokens = true;
    else if (arg == "--dump-ast") dumpAst = true;
    else if (arg == "--dump-ir") dumpIr = true;
    else if (!arg.empty() && arg[0] == '-') { usage(); return 2; }
    else if (!path.empty()) { usage(); return 2; }  // only one input file
    else path = arg;
  }
  if (path.empty()) { usage(); return 2; }

  std::ifstream in(path);
  if (!in) {
    std::cerr << "miniptx: cannot open '" << path << "'\n";
    return 1;
  }
  std::stringstream buf;
  buf << in.rdbuf();
  std::string source = buf.str();

  try {
    auto tokens = tokenize(source);
    if (dumpTokens) {
      for (const Token& t : tokens) {
        std::printf("%3d:%-3d %-9s %s\n", t.line, t.col, tokenKindName(t.kind),
                    t.text.c_str());
      }
    }
    Program program = parseProgram(tokens);
    if (dumpAst) printProgram(program, std::cout);
    analyze(program);
    IrModule ir = generateIr(program);
    if (dumpIr) printIrModule(ir, std::cout);
  } catch (const CompileError& e) {
    std::cerr << path << ":" << e.line << ":" << e.col << ": error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

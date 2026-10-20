// miniptx command-line driver.
//
//   miniptx prog.tiny                       print the PTX-like output
//   miniptx prog.tiny -o prog.ptx           write it to a file
//   miniptx prog.tiny --run f 5 [1,2,3]     compile, then run f in the interpreter
//   miniptx prog.ptx --run f 5              run an already-compiled file
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "diagnostics.h"
#include "driver.h"
#include "emit.h"
#include "interp.h"
#include "ptxreader.h"

static void usage() {
  std::cerr <<
      "usage: miniptx [options] <file.tiny | file.ptx> [--run <func> [args...]]\n"
      "options:\n"
      "  --dump-tokens, --dump-ast, --dump-ir   print an intermediate stage\n"
      "  -o <file>                              write the PTX-like output to <file>\n"
      "  --threads <N>                          with --run: launch N threads (tid = 0..N-1)\n"
      "  --run <func> [args...]                 run <func>; args look like 5, 2.5, [1,2,3]\n";
}

static bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

int main(int argc, char** argv) {
  CompileOptions opts;
  std::string path, outPath, runFn;
  std::vector<std::string> runArgs;
  bool run = false;
  int threads = 1;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--dump-tokens") opts.dumpTokens = &std::cout;
    else if (arg == "--dump-ast") opts.dumpAst = &std::cout;
    else if (arg == "--dump-ir") opts.dumpIr = &std::cout;
    else if (arg == "-o" && i + 1 < argc) outPath = argv[++i];
    else if (arg == "--threads" && i + 1 < argc) {
      threads = std::atoi(argv[++i]);
      if (threads < 1) { std::cerr << "miniptx: --threads must be at least 1\n"; return 2; }
    } else if (arg == "--run" && i + 1 < argc) {
      run = true;
      runFn = argv[++i];
      // Everything after the function name belongs to the program, even "-5".
      for (i++; i < argc; i++) runArgs.push_back(argv[i]);
    } else if (!arg.empty() && arg[0] == '-') { usage(); return 2; }
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

  try {
    std::string ptx;
    if (endsWith(path, ".ptx")) {
      ptx = buf.str();  // already compiled: skip straight to running it
    } else {
      ptx = emitPtx(compileSource(buf.str(), opts));
    }

    if (!outPath.empty()) {
      std::ofstream out(outPath);
      if (!out) { std::cerr << "miniptx: cannot write '" << outPath << "'\n"; return 1; }
      out << ptx;
    }

    if (run) {
      // Run the *text* we just produced, not the in-memory IR: this proves the
      // emitted file contains everything needed to execute the program.
      IrModule module = readPtx(ptx);
      std::cout << runFromStrings(module, runFn, runArgs, threads);
    } else if (outPath.empty() && !opts.dumpTokens && !opts.dumpAst && !opts.dumpIr) {
      std::cout << ptx;  // no other output requested: print the result
    }
  } catch (const CompileError& e) {
    std::cerr << path << ":" << e.line << ":" << e.col << ": error: " << e.what() << "\n";
    return 1;
  } catch (const RuntimeError& e) {
    std::cerr << "runtime error: " << e.what() << "\n";
    return 1;
  }
  return 0;
}

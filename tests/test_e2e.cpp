// End-to-end tests: every .tiny file in examples/ and tests/e2e/ is a test.
// Directives inside the file say what should happen:
//
//   // RUN: [--threads N] func args...    compile, emit, read back, run
//   // EXPECT: line                        expected output of the RUN above
//   // ERROR: message                      compiling must fail with this message
//
// Each file is registered as its own test (e2e_<name>) when the program starts.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "diagnostics.h"
#include "driver.h"
#include "emit.h"
#include "interp.h"
#include "ptxreader.h"
#include "test.h"

namespace fs = std::filesystem;

namespace {

struct RunCase {
  std::vector<std::string> words;  // [--threads N] func args...
  std::string expected;
};

std::string readFile(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Text after "// KEY:" on this line, or nullopt-like empty bool.
bool directive(const std::string& line, const std::string& key, std::string& out) {
  std::string tag = "// " + key + ":";
  size_t at = line.find(tag);
  if (at == std::string::npos) return false;
  out = line.substr(at + tag.size());
  if (!out.empty() && out[0] == ' ') out.erase(0, 1);
  return true;
}

void fail(const std::string& msg) {
  std::cerr << "  " << msg << "\n";
  currentFailures()++;
}

void runE2eFile(const fs::path& path) {
  std::string source = readFile(path);
  std::vector<RunCase> cases;
  std::string expectedError;
  bool hasError = false;

  std::istringstream lines(source);
  std::string line, text;
  while (std::getline(lines, line)) {
    if (directive(line, "RUN", text)) {
      RunCase c;
      std::istringstream ws(text);
      for (std::string w; ws >> w;) c.words.push_back(w);
      cases.push_back(c);
    } else if (directive(line, "EXPECT", text)) {
      if (cases.empty()) return fail("EXPECT before any RUN");
      cases.back().expected += text + "\n";
    } else if (directive(line, "ERROR", text)) {
      hasError = true;
      expectedError = text;
    }
  }
  if (cases.empty() && !hasError) return fail("file has no // RUN: or // ERROR: directives");

  if (hasError) {
    try {
      compileSource(source);
      fail("expected compile error '" + expectedError + "' but compilation succeeded");
    } catch (const CompileError& e) {
      if (e.what() != expectedError)
        fail("expected error: " + expectedError + "\n  actual error:   " + e.what());
    }
    return;
  }

  // Compile -> PTX text -> read back. Running the re-read module checks the
  // emitter and reader as well as the code generator. Every RUN is checked
  // with AND without optimization: an optimization must never change results.
  CompileOptions noOpt;
  noOpt.optimize = false;
  struct Variant {
    const char* label;
    IrModule module;
  };
  std::vector<Variant> variants = {
      {"optimized", readPtx(emitPtx(compileSource(source)))},
      {"unoptimized", readPtx(emitPtx(compileSource(source, noOpt)))},
  };

  for (const Variant& v : variants)
  for (const RunCase& c : cases) {
    const IrModule& module = v.module;
    std::vector<std::string> w = c.words;
    int threads = 1;
    if (w.size() >= 2 && w[0] == "--threads") {
      threads = std::stoi(w[1]);
      w.erase(w.begin(), w.begin() + 2);
    }
    if (w.empty()) return fail("RUN line has no function name");
    std::string fn = w[0];
    std::vector<std::string> args(w.begin() + 1, w.end());

    std::string got;
    try {
      got = runFromStrings(module, fn, args, threads);
    } catch (const RuntimeError& e) {
      got = std::string("runtime error: ") + e.what() + "\n";
    }
    if (got != c.expected) {
      std::string runLine;
      for (const std::string& s : c.words) runLine += s + " ";
      fail(std::string("[") + v.label + "] RUN: " + runLine + "\n  --- expected ---\n" + c.expected + "  --- got ---\n" + got);
    }
  }
}

// Runs before main(): find the files and register one test per file.
const int registered = [] {
  std::vector<fs::path> files;
  for (const char* dir : {MINIPTX_EXAMPLES_DIR, MINIPTX_E2E_DIR}) {
    if (!fs::exists(dir)) continue;
    for (const auto& entry : fs::directory_iterator(dir))
      if (entry.path().extension() == ".tiny") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files)
    testRegistry().push_back({"e2e_" + f.stem().string(), [f] { runE2eFile(f); }});
  return 0;
}();

}  // namespace

#pragma once
#include <stdexcept>
#include <string>

// Every stage of the compiler reports problems by throwing a CompileError.
// It carries a source position so main() can print "file:line:col: error: ...".
struct CompileError : std::runtime_error {
  int line;
  int col;
  CompileError(const std::string& msg, int line, int col)
      : std::runtime_error(msg), line(line), col(col) {}
};

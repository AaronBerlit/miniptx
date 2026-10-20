#pragma once
// A small virtual machine that executes IR, standing in for a GPU.
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "ir.h"

// Something went wrong while the program was running (bad index, divide by
// zero, ...). Different from CompileError, which means the program is invalid.
struct RuntimeError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// An array argument: the interpreter's stand-in for GPU global memory.
struct ArrayValue {
  IrType type;
  std::vector<int32_t> ints;  // used when type == S32
  std::vector<float> floats;  // used when type == F32
  size_t size() const { return type == IrType::S32 ? ints.size() : floats.size(); }
};

// One argument or return value: a scalar, or a pointer to an array.
struct Value {
  int32_t i = 0;
  float f = 0;
  ArrayValue* array = nullptr;
};

class Interpreter {
 public:
  explicit Interpreter(const IrModule& module);

  // Runs one function as thread `tid`. Arrays are modified in place.
  Value call(const std::string& name, const std::vector<Value>& args, int tid);

  long long steps() const { return steps_; }  // instructions executed so far

 private:
  struct FnInfo {
    const IrFunction* fn;
    std::map<std::string, size_t> labels;  // label name -> instruction index
  };
  std::map<std::string, FnInfo> functions_;
  long long steps_ = 0;

  Value exec(const FnInfo& info, const std::vector<Value>& args, int tid, int depth);
};

// Convenience used by the CLI and the end-to-end tests: parses arguments from
// text ("5", "2.5", "[1,2,3]"), runs `fn` once per thread, and returns the
// output: the return value (if any) and the final contents of each array.
std::string runFromStrings(const IrModule& module, const std::string& fn,
                           const std::vector<std::string>& args, int threads = 1);

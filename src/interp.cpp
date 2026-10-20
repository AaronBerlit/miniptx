#include "interp.h"

#include <climits>
#include <memory>
#include <sstream>

namespace {

constexpr long long kMaxSteps = 10'000'000;  // catches infinite loops
constexpr int kMaxDepth = 1000;              // catches runaway recursion

// 32-bit wraparound, like real hardware. (Signed overflow in C++ is undefined
// behaviour, so do the math in 64 bits and truncate.)
int32_t wrap(long long v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

// One function activation: its own registers and its view of array memory.
struct Frame {
  std::vector<int32_t> r;  // %r registers, index = register number
  std::vector<float> f;    // %f registers
  std::vector<char> p;     // %p registers (predicates)
  std::map<std::string, ArrayValue*> arrays;
  int tid;

  template <typename T>
  static T& slot(std::vector<T>& v, const Operand& o) {
    if (o.reg < 0 || static_cast<size_t>(o.reg) >= v.size())
      throw RuntimeError("register " + operandToString(o) + " out of range");
    return v[o.reg];
  }

  int32_t readInt(const Operand& o) {
    switch (o.kind) {
      case Operand::Kind::Reg:
        if (o.cls == RegClass::Int) return slot(r, o);
        break;
      case Operand::Kind::ImmInt: return static_cast<int32_t>(o.imm);
      case Operand::Kind::Tid: return tid;
      default: break;
    }
    throw RuntimeError("expected an int operand, got " + operandToString(o));
  }

  float readFloat(const Operand& o) {
    switch (o.kind) {
      case Operand::Kind::Reg:
        if (o.cls == RegClass::Float) return slot(f, o);
        break;
      case Operand::Kind::ImmFloat: return static_cast<float>(o.fimm);
      case Operand::Kind::ImmInt: return static_cast<float>(o.imm);
      default: break;
    }
    throw RuntimeError("expected a float operand, got " + operandToString(o));
  }

  bool readPred(const Operand& o) {
    if (o.isReg() && o.cls == RegClass::Pred) return slot(p, o);
    throw RuntimeError("expected a predicate, got " + operandToString(o));
  }

  // Read a value as type t (s32 or f32) into a Value.
  Value read(const Operand& o, IrType t) {
    Value v;
    if (t == IrType::S32) v.i = readInt(o);
    else v.f = readFloat(o);
    return v;
  }

  // Write to a register; its class (%r or %f) decides which field is used.
  void write(const Operand& dst, const Value& v) {
    if (!dst.isReg()) throw RuntimeError("destination is not a register");
    switch (dst.cls) {
      case RegClass::Int: slot(r, dst) = v.i; break;
      case RegClass::Float: slot(f, dst) = v.f; break;
      case RegClass::Pred: throw RuntimeError("cannot write a value to a predicate");
    }
  }

  ArrayValue& array(const Operand& o) {
    auto it = arrays.find(o.name);
    if (o.kind != Operand::Kind::Array || it == arrays.end())
      throw RuntimeError("unknown array '" + o.name + "'");
    return *it->second;
  }

  // Bounds-checked element access: [a + idx]
  size_t index(ArrayValue& a, const Operand& arrOp, const Operand& idxOp) {
    int32_t i = readInt(idxOp);
    if (i < 0 || static_cast<size_t>(i) >= a.size())
      throw RuntimeError("index " + std::to_string(i) + " is out of bounds for array '" +
                         arrOp.name + "' (size " + std::to_string(a.size()) + ")");
    return static_cast<size_t>(i);
  }
};

}  // namespace

Interpreter::Interpreter(const IrModule& module) {
  for (const IrFunction& f : module.functions) {
    FnInfo info{&f, {}};
    for (size_t i = 0; i < f.body.size(); i++)
      if (f.body[i].op == Op::Label) info.labels[f.body[i].label] = i;
    functions_[f.name] = info;
  }
}

Value Interpreter::call(const std::string& name, const std::vector<Value>& args, int tid) {
  auto it = functions_.find(name);
  if (it == functions_.end()) throw RuntimeError("no function named '" + name + "'");
  return exec(it->second, args, tid, 0);
}

Value Interpreter::exec(const FnInfo& info, const std::vector<Value>& args, int tid, int depth) {
  const IrFunction& fn = *info.fn;
  if (depth > kMaxDepth)
    throw RuntimeError("call stack overflow in '" + fn.name + "' (runaway recursion?)");
  if (args.size() != fn.params.size())
    throw RuntimeError("wrong number of arguments for '" + fn.name + "'");

  // Fresh registers for this call; every register starts at 0.
  Frame fr;
  fr.r.assign(fn.numRegs[0] + 1, 0);
  fr.f.assign(fn.numRegs[1] + 1, 0.0f);
  fr.p.assign(fn.numRegs[2] + 1, 0);
  fr.tid = tid;
  for (size_t i = 0; i < fn.params.size(); i++) {
    const IrParam& p = fn.params[i];
    if (p.isArray) fr.arrays[p.name] = args[i].array;
    else fr.write(p.reg, args[i]);
  }

  // The fetch-execute loop. pc ("program counter") = index of the next instruction.
  size_t pc = 0;
  while (pc < fn.body.size()) {
    const Instr& in = fn.body[pc++];
    if (in.op == Op::Label) continue;  // labels are markers, not work
    if (++steps_ > kMaxSteps)
      throw RuntimeError("step limit exceeded (" + std::to_string(kMaxSteps) +
                         " instructions): infinite loop?");

    bool isInt = in.type == IrType::S32;
    switch (in.op) {
      case Op::Mov:
        fr.write(in.dst, fr.read(in.srcs[0], in.type));
        break;

      case Op::Neg: {
        Value v;
        if (isInt) v.i = wrap(-static_cast<long long>(fr.readInt(in.srcs[0])));
        else v.f = -fr.readFloat(in.srcs[0]);
        fr.write(in.dst, v);
        break;
      }

      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Div: {
        Value v;
        if (isInt) {
          long long a = fr.readInt(in.srcs[0]), b = fr.readInt(in.srcs[1]);
          switch (in.op) {
            case Op::Add: v.i = wrap(a + b); break;
            case Op::Sub: v.i = wrap(a - b); break;
            case Op::Mul: v.i = wrap(a * b); break;
            default:
              if (b == 0) throw RuntimeError("integer division by zero");
              v.i = wrap(a / b);  // INT_MIN / -1 wraps back to INT_MIN
          }
        } else {
          float a = fr.readFloat(in.srcs[0]), b = fr.readFloat(in.srcs[1]);
          switch (in.op) {
            case Op::Add: v.f = a + b; break;
            case Op::Sub: v.f = a - b; break;
            case Op::Mul: v.f = a * b; break;
            default: v.f = a / b;  // float division by zero gives inf, like hardware
          }
        }
        fr.write(in.dst, v);
        break;
      }

      case Op::Ld: {
        ArrayValue& a = fr.array(in.srcs[0]);
        size_t i = fr.index(a, in.srcs[0], in.srcs[1]);
        Value v;
        if (a.type == IrType::S32) v.i = a.ints[i];
        else v.f = a.floats[i];
        fr.write(in.dst, v);
        break;
      }

      case Op::St: {
        ArrayValue& a = fr.array(in.srcs[0]);
        size_t i = fr.index(a, in.srcs[0], in.srcs[1]);
        if (a.type == IrType::S32) a.ints[i] = fr.readInt(in.srcs[2]);
        else a.floats[i] = fr.readFloat(in.srcs[2]);
        break;
      }

      case Op::Setp: {
        // Compare as the instruction's type, store true/false in a predicate.
        double a = isInt ? fr.readInt(in.srcs[0]) : fr.readFloat(in.srcs[0]);
        double b = isInt ? fr.readInt(in.srcs[1]) : fr.readFloat(in.srcs[1]);
        bool res = false;
        switch (in.cmp) {
          case Cmp::Lt: res = a < b; break;
          case Cmp::Gt: res = a > b; break;
          case Cmp::Le: res = a <= b; break;
          case Cmp::Ge: res = a >= b; break;
          case Cmp::Eq: res = a == b; break;
          case Cmp::Ne: res = a != b; break;
        }
        if (!in.dst.isReg() || in.dst.cls != RegClass::Pred)
          throw RuntimeError("setp must write a predicate register");
        Frame::slot(fr.p, in.dst) = res;
        break;
      }

      case Op::Bra: {
        if (in.guard.isReg()) {
          bool g = fr.readPred(in.guard);
          if (in.guardNegated) g = !g;
          if (!g) break;  // guard false: fall through to the next instruction
        }
        auto target = info.labels.find(in.label);
        if (target == info.labels.end()) throw RuntimeError("unknown label '" + in.label + "'");
        pc = target->second;
        break;
      }

      case Op::Call: {
        auto callee = functions_.find(in.label);
        if (callee == functions_.end()) throw RuntimeError("call to unknown function '" + in.label + "'");
        const IrFunction& cf = *callee->second.fn;
        if (in.srcs.size() != cf.params.size())
          throw RuntimeError("wrong number of arguments in call to '" + cf.name + "'");
        std::vector<Value> callArgs;
        for (size_t i = 0; i < in.srcs.size(); i++) {
          if (cf.params[i].isArray) {
            Value v;
            v.array = &fr.array(in.srcs[i]);  // same memory, not a copy
            callArgs.push_back(v);
          } else {
            callArgs.push_back(fr.read(in.srcs[i], cf.params[i].type));
          }
        }
        Value result = exec(callee->second, callArgs, tid, depth + 1);
        if (in.dst.kind != Operand::Kind::None) fr.write(in.dst, result);
        break;
      }

      case Op::Ret:
        if (!fn.returnsValue) return Value{};
        if (in.srcs.empty()) throw RuntimeError("'" + fn.name + "' returned without a value");
        return fr.read(in.srcs[0], fn.returnType);

      case Op::Label:
        break;
    }
  }
  if (fn.returnsValue) throw RuntimeError("'" + fn.name + "' ended without returning a value");
  return Value{};
}

// ---- Text arguments and results ------------------------------------------------

namespace {

std::string fmtFloat(float f) {
  std::ostringstream os;
  os << f;
  return os.str();
}

int32_t parseIntArg(const std::string& s, const std::string& what) {
  try {
    size_t used = 0;
    long long v = std::stoll(s, &used);
    if (used == s.size() && v >= INT_MIN && v <= INT_MAX) return static_cast<int32_t>(v);
  } catch (const std::exception&) {
  }
  throw RuntimeError(what + " should be an int, got '" + s + "'");
}

float parseFloatArg(const std::string& s, const std::string& what) {
  try {
    size_t used = 0;
    float v = std::stof(s, &used);
    if (used == s.size()) return v;
  } catch (const std::exception&) {
  }
  throw RuntimeError(what + " should be a float, got '" + s + "'");
}

}  // namespace

std::string runFromStrings(const IrModule& module, const std::string& fnName,
                           const std::vector<std::string>& args, int threads) {
  const IrFunction* fn = nullptr;
  for (const IrFunction& f : module.functions)
    if (f.name == fnName) fn = &f;
  if (!fn) throw RuntimeError("no function named '" + fnName + "'");
  if (args.size() != fn->params.size())
    throw RuntimeError("'" + fnName + "' expects " + std::to_string(fn->params.size()) +
                       " argument(s), got " + std::to_string(args.size()));

  std::vector<std::unique_ptr<ArrayValue>> arrays;  // owns the memory
  std::vector<Value> values;
  for (size_t i = 0; i < args.size(); i++) {
    const IrParam& p = fn->params[i];
    const std::string& s = args[i];
    // Scalars are just registers in the .ptx text, so only arrays keep a name.
    std::string what = "argument " + std::to_string(i + 1) + (p.isArray ? " ('" + p.name + "')" : "");
    Value v;
    if (p.isArray) {
      if (s.size() < 2 || s.front() != '[' || s.back() != ']')
        throw RuntimeError(what + " should be an array like [1,2,3], got '" + s + "'");
      auto a = std::make_unique<ArrayValue>();
      a->type = p.type;
      std::stringstream inner(s.substr(1, s.size() - 2));
      std::string item;
      while (std::getline(inner, item, ',')) {
        size_t b = item.find_first_not_of(' '), e = item.find_last_not_of(' ');
        if (b == std::string::npos) continue;  // "[]" or a trailing comma
        item = item.substr(b, e - b + 1);
        if (p.type == IrType::S32) a->ints.push_back(parseIntArg(item, what));
        else a->floats.push_back(parseFloatArg(item, what));
      }
      v.array = a.get();
      arrays.push_back(std::move(a));
    } else if (p.type == IrType::S32) {
      v.i = parseIntArg(s, what);
    } else {
      v.f = parseFloatArg(s, what);
    }
    values.push_back(v);
  }

  Interpreter interp(module);
  std::ostringstream out;
  // GPU-style launch: the same function runs once per thread, each with its
  // own tid. (A real GPU runs them in parallel; we run them one after another.)
  for (int t = 0; t < threads; t++) {
    Value r = interp.call(fnName, values, t);
    if (fn->returnsValue) {
      if (threads > 1) out << "tid " << t << ": ";
      out << "return = " << (fn->returnType == IrType::S32 ? std::to_string(r.i) : fmtFloat(r.f)) << "\n";
    }
  }
  for (size_t i = 0; i < fn->params.size(); i++) {
    const IrParam& p = fn->params[i];
    if (!p.isArray) continue;
    const ArrayValue& a = *values[i].array;
    out << p.name << " = [";
    for (size_t k = 0; k < a.size(); k++) {
      if (k) out << ", ";
      out << (a.type == IrType::S32 ? std::to_string(a.ints[k]) : fmtFloat(a.floats[k]));
    }
    out << "]\n";
  }
  return out.str();
}

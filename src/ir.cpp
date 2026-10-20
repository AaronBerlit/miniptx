#include "ir.h"

#include <iomanip>
#include <sstream>

static const char* typeSuffix(IrType t) { return t == IrType::S32 ? "s32" : "f32"; }

static const char* cmpName(Cmp c) {
  switch (c) {
    case Cmp::Lt: return "lt";
    case Cmp::Gt: return "gt";
    case Cmp::Le: return "le";
    case Cmp::Ge: return "ge";
    case Cmp::Eq: return "eq";
    case Cmp::Ne: return "ne";
  }
  return "?";
}

static const char* opName(Op op) {
  switch (op) {
    case Op::Mov: return "mov";
    case Op::Add: return "add";
    case Op::Sub: return "sub";
    case Op::Mul: return "mul";
    case Op::Div: return "div";
    case Op::Neg: return "neg";
    case Op::Ld: return "ld";
    case Op::St: return "st";
    case Op::Setp: return "setp";
    case Op::Bra: return "bra";
    case Op::Label: return "label";
    case Op::Call: return "call";
    case Op::Ret: return "ret";
  }
  return "?";
}

std::string operandToString(const Operand& o) {
  switch (o.kind) {
    case Operand::Kind::None: return "_";
    case Operand::Kind::Reg: {
      const char* prefix = o.cls == RegClass::Int ? "%r" : o.cls == RegClass::Float ? "%f" : "%p";
      return prefix + std::to_string(o.reg);
    }
    case Operand::Kind::ImmInt: return std::to_string(o.imm);
    case Operand::Kind::ImmFloat: {
      std::ostringstream os;
      os << std::setprecision(9) << o.fimm;
      std::string s = os.str();
      if (s.find_first_of(".einf") == std::string::npos) s += ".0";
      return s;
    }
    case Operand::Kind::Tid: return "%tid.x";
    case Operand::Kind::Array: return o.name;
  }
  return "?";
}

// Pads the opcode column so operands line up, like hand-written PTX.
static std::string padOp(const std::string& op) {
  return op.size() < 12 ? op + std::string(12 - op.size(), ' ') : op + " ";
}

std::string instrToString(const Instr& in) {
  const std::string t = typeSuffix(in.type);
  auto src = [&](size_t i) { return operandToString(in.srcs[i]); };
  switch (in.op) {
    case Op::Label:
      return in.label + ":";
    case Op::Mov:
    case Op::Neg:
      return padOp(std::string(opName(in.op)) + "." + t) + operandToString(in.dst) + ", " + src(0) + ";";
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
      return padOp(std::string(opName(in.op)) + "." + t) + operandToString(in.dst) + ", " +
             src(0) + ", " + src(1) + ";";
    case Op::Ld:
      return padOp("ld." + t) + operandToString(in.dst) + ", [" + src(0) + " + " + src(1) + "];";
    case Op::St:
      return padOp("st." + t) + "[" + src(0) + " + " + src(1) + "], " + src(2) + ";";
    case Op::Setp:
      return padOp(std::string("setp.") + cmpName(in.cmp) + "." + t) + operandToString(in.dst) +
             ", " + src(0) + ", " + src(1) + ";";
    case Op::Bra: {
      std::string g;
      if (in.guard.isReg()) g = std::string("@") + (in.guardNegated ? "!" : "") + operandToString(in.guard) + " ";
      return g + padOp("bra") + in.label + ";";
    }
    case Op::Call: {
      // PTX shape: call (ret), name, (arg1, arg2);
      std::string s = padOp("call");
      if (in.dst.kind != Operand::Kind::None) s += "(" + operandToString(in.dst) + "), ";
      s += in.label + ", (";
      for (size_t i = 0; i < in.srcs.size(); i++) s += (i ? ", " : "") + src(i);
      return s + ");";
    }
    case Op::Ret:
      return in.srcs.empty() ? "ret;" : padOp("ret") + src(0) + ";";
  }
  return "?";
}

void printIrFunction(const IrFunction& f, std::ostream& out) {
  out << ".func " << f.name << "(";
  for (size_t i = 0; i < f.params.size(); i++) {
    const IrParam& p = f.params[i];
    if (i) out << ", ";
    out << "." << typeSuffix(p.type) << " "
        << (p.isArray ? p.name + "[]" : operandToString(p.reg));
  }
  out << ")";
  if (f.returnsValue) out << " -> ." << typeSuffix(f.returnType);
  out << "\n{\n";
  for (const Instr& in : f.body) {
    // Labels sit at the left margin; instructions are indented.
    out << (in.op == Op::Label ? "" : "    ") << instrToString(in) << "\n";
  }
  out << "}\n";
}

void printIrModule(const IrModule& m, std::ostream& out) {
  for (size_t i = 0; i < m.functions.size(); i++) {
    if (i) out << "\n";
    printIrFunction(m.functions[i], out);
  }
}

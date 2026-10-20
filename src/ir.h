#pragma once
// The IR: a flat list of PTX-like instructions per function, using an unlimited
// supply of virtual registers. The optimizer, register allocator, emitter and
// interpreter all work on this structure.
//
// Registers come in three classes, like PTX:
//   %r1, %r2 ...  32-bit ints        (.s32)
//   %f1, %f2 ...  32-bit floats      (.f32)
//   %p1, %p2 ...  predicates (true/false), written by setp, read by bra
#include <ostream>
#include <string>
#include <vector>

enum class RegClass { Int, Float, Pred };

struct Operand {
  enum class Kind {
    None,      // unused slot
    Reg,       // %r3 / %f2 / %p1
    ImmInt,    // 42
    ImmFloat,  // 2.5
    Tid,       // %tid.x  (the built-in thread id)
    Array,     // name of an array parameter, used as a memory base: [a + %r1]
  };
  Kind kind = Kind::None;
  RegClass cls = RegClass::Int;  // Reg only
  int reg = 0;                   // Reg only: register number (1-based)
  long long imm = 0;             // ImmInt
  double fimm = 0;               // ImmFloat
  std::string name;              // Array

  static Operand makeReg(RegClass c, int n) {
    Operand o; o.kind = Kind::Reg; o.cls = c; o.reg = n; return o;
  }
  static Operand makeInt(long long v) { Operand o; o.kind = Kind::ImmInt; o.imm = v; return o; }
  static Operand makeFloat(double v) { Operand o; o.kind = Kind::ImmFloat; o.fimm = v; return o; }
  static Operand makeTid() { Operand o; o.kind = Kind::Tid; return o; }
  static Operand makeArray(const std::string& n) {
    Operand o; o.kind = Kind::Array; o.name = n; return o;
  }

  bool isReg() const { return kind == Kind::Reg; }
  bool isImm() const { return kind == Kind::ImmInt || kind == Kind::ImmFloat; }
  bool sameReg(const Operand& o) const {
    return isReg() && o.isReg() && cls == o.cls && reg == o.reg;
  }
};

enum class Op {
  Mov,    // dst = a
  Add, Sub, Mul, Div,  // dst = a op b
  Neg,    // dst = -a
  Ld,     // dst = array[index]           srcs: {array, index}
  St,     // array[index] = value         srcs: {array, index, value}  (no dst)
  Setp,   // pred = a <cmp> b             (cmp field says which comparison)
  Bra,    // jump to label; if guard is set, only when guard (or !guard) is true
  Label,  // a jump target, not a real instruction
  Call,   // [dst =] callee(args...)      srcs: args
  Ret,    // return [srcs[0]]
};

enum class Cmp { Lt, Gt, Le, Ge, Eq, Ne };

// The type suffix on an instruction: add.s32 vs add.f32.
enum class IrType { S32, F32 };

struct Instr {
  Op op;
  IrType type = IrType::S32;
  Cmp cmp = Cmp::Eq;          // Setp only
  Operand dst;                // Kind::None if the instruction writes nothing
  std::vector<Operand> srcs;
  std::string label;          // Label: its name. Bra: target. Call: callee name.
  Operand guard;              // Bra only: predicate register, or None = always jump
  bool guardNegated = false;  // Bra only: "@!%p1 bra" jumps when %p1 is FALSE
};

struct IrParam {
  std::string name;  // source name (used for arrays: [a + %r1])
  IrType type;
  bool isArray;
  Operand reg;       // scalars: the register holding it on entry. Arrays: None.
};

struct IrFunction {
  std::string name;
  std::vector<IrParam> params;
  bool returnsValue = false;
  IrType returnType = IrType::S32;
  std::vector<Instr> body;
  int numRegs[3] = {0, 0, 0};  // how many %r / %f / %p registers are used
};

struct IrModule {
  std::vector<IrFunction> functions;
};

// PTX-style text, e.g. "add.s32 %r1, %r1, 1;". Used by --dump-ir and the tests.
std::string operandToString(const Operand& o);
std::string instrToString(const Instr& in);
void printIrFunction(const IrFunction& f, std::ostream& out);
void printIrModule(const IrModule& m, std::ostream& out);

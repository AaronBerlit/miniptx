#include "irgen.h"

#include "diagnostics.h"

namespace {

RegClass regClassOf(BaseType t) { return t == BaseType::Float ? RegClass::Float : RegClass::Int; }
IrType irTypeOf(BaseType t) { return t == BaseType::Float ? IrType::F32 : IrType::S32; }

class FunctionGen {
 public:
  FunctionGen(const Function& fn, IrFunction& out) : fn_(fn), out_(out) {}

  void run() {
    out_.name = fn_.name;
    out_.returnsValue = fn_.returnType != BaseType::Void;
    out_.returnType = irTypeOf(fn_.returnType);
    varReg_.resize(fn_.vars.size());

    // Each scalar parameter arrives in its own register. Array parameters
    // have no register: they are named memory, addressed as [a + index].
    for (const Param& p : fn_.params) {
      IrParam ip{p.name, irTypeOf(p.type), p.isArray, Operand()};
      if (!p.isArray) {
        ip.reg = newReg(regClassOf(p.type));
        varReg_[p.varId] = ip.reg;
      }
      out_.params.push_back(ip);
    }

    genBlock(*fn_.body);

    // A void function may just fall off the end; give it an explicit ret.
    if (out_.body.empty() || out_.body.back().op != Op::Ret) out_.body.push_back(ret());
  }

 private:
  const Function& fn_;
  IrFunction& out_;
  // Every scalar variable (by varId) lives in ONE register for its whole life.
  // Assigning to the variable overwrites that register.
  std::vector<Operand> varReg_;

  Operand newReg(RegClass c) {
    int& count = out_.numRegs[static_cast<int>(c)];
    return Operand::makeReg(c, ++count);
  }

  void emit(Instr in) { out_.body.push_back(std::move(in)); }

  static Instr ret() { Instr in; in.op = Op::Ret; return in; }

  [[noreturn]] static void notYet(const char* what, int line, int col) {
    throw CompileError(std::string(what) + " is not implemented yet (step 4b)", line, col);
  }

  // ---- Statements -----------------------------------------------------------

  void genBlock(const Stmt& block) {
    for (const StmtPtr& s : block.stmts) genStmt(*s);
  }

  void genStmt(const Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::VarDecl: {
        Operand reg = newReg(regClassOf(s.declType));
        varReg_[s.varId] = reg;
        if (s.value) genExpr(*s.value, &reg);  // compute straight into the variable
        break;
      }
      case Stmt::Kind::Assign: {
        const VarInfo& v = fn_.vars[s.varId];
        if (s.index) {
          Operand idx = genExpr(*s.index);
          Operand val = genExpr(*s.value);
          Instr st;
          st.op = Op::St;
          st.type = irTypeOf(v.type);
          st.srcs = {Operand::makeArray(v.name), idx, val};
          emit(st);
        } else {
          genExpr(*s.value, &varReg_[s.varId]);
        }
        break;
      }
      case Stmt::Kind::Return: {
        Instr r = ret();
        if (s.value) r.srcs.push_back(genExpr(*s.value));
        emit(r);
        break;
      }
      case Stmt::Kind::Block:
        genBlock(s);
        break;
      case Stmt::Kind::If: notYet("if", s.line, s.col);
      case Stmt::Kind::While: notYet("while", s.line, s.col);
      case Stmt::Kind::ExprStmt: notYet("call", s.line, s.col);
    }
  }

  // ---- Expressions ----------------------------------------------------------

  // Generates code for e and returns the operand holding its value.
  //
  // `target` is an optional hint: "please put the result in this register".
  // Without it, `i = i + 1` would become   add %r2, %r1, 1;  mov %r1, %r2;
  // With it, the add writes %r1 directly:  add %r1, %r1, 1;
  // This is safe because the target is only written by the LAST instruction,
  // after every operand has already been read.
  Operand genExpr(const Expr& e, const Operand* target = nullptr) {
    RegClass cls = regClassOf(e.type);
    IrType ty = irTypeOf(e.type);
    auto dstReg = [&] { return target ? *target : newReg(cls); };

    switch (e.kind) {
      // Constants and variables need no instruction at all: the operand IS the
      // value. We only emit a mov if the caller asked for a specific register.
      case Expr::Kind::IntLit: return place(Operand::makeInt(e.intValue), target, ty);
      case Expr::Kind::FloatLit: return place(Operand::makeFloat(e.floatValue), target, ty);
      case Expr::Kind::Var: return place(varReg_[e.varId], target, ty);

      case Expr::Kind::Tid: {
        // Like PTX, %tid.x can only be read by a mov, never used directly in math.
        Operand dst = dstReg();
        emitMov(dst, Operand::makeTid(), ty);
        return dst;
      }

      case Expr::Kind::Index: {
        Operand idx = genExpr(*e.lhs);
        Operand dst = dstReg();
        Instr ld;
        ld.op = Op::Ld;
        ld.type = ty;
        ld.dst = dst;
        ld.srcs = {Operand::makeArray(fn_.vars[e.varId].name), idx};
        emit(ld);
        return dst;
      }

      case Expr::Kind::Unary: {
        Operand a = genExpr(*e.lhs);
        Operand dst = dstReg();
        Instr neg;
        neg.op = Op::Neg;
        neg.type = ty;
        neg.dst = dst;
        neg.srcs = {a};
        emit(neg);
        return dst;
      }

      case Expr::Kind::Binary: {
        Operand a = genExpr(*e.lhs);
        Operand b = genExpr(*e.rhs);
        Operand dst = dstReg();
        Instr in;
        in.type = ty;
        in.dst = dst;
        in.srcs = {a, b};
        switch (e.op) {
          case TokenKind::Plus: in.op = Op::Add; break;
          case TokenKind::Minus: in.op = Op::Sub; break;
          case TokenKind::Star: in.op = Op::Mul; break;
          case TokenKind::Slash: in.op = Op::Div; break;
          default: notYet("comparison", e.line, e.col);  // handled with if/while in 4b
        }
        emit(in);
        return dst;
      }

      case Expr::Kind::Call: notYet("call", e.line, e.col);
    }
    return Operand();
  }

  // Puts an already-available value where the caller wants it.
  Operand place(const Operand& value, const Operand* target, IrType ty) {
    if (!target) return value;
    if (!value.sameReg(*target)) emitMov(*target, value, ty);
    return *target;
  }

  void emitMov(const Operand& dst, const Operand& src, IrType ty) {
    Instr mov;
    mov.op = Op::Mov;
    mov.type = ty;
    mov.dst = dst;
    mov.srcs = {src};
    emit(mov);
  }
};

}  // namespace

IrModule generateIr(const Program& program) {
  IrModule m;
  for (const Function& f : program.functions) {
    m.functions.emplace_back();
    FunctionGen(f, m.functions.back()).run();
  }
  return m;
}

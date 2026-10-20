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
  int labelCount_ = 0;  // makes label names unique within the function

  Operand newReg(RegClass c) {
    int& count = out_.numRegs[static_cast<int>(c)];
    return Operand::makeReg(c, ++count);
  }

  void emit(Instr in) { out_.body.push_back(std::move(in)); }

  static Instr ret() { Instr in; in.op = Op::Ret; return in; }

  // ---- Statements -----------------------------------------------------------

  void genBlock(const Stmt& block) {
    for (const StmtPtr& s : block.stmts) genStmt(*s);
  }

  void genStmt(const Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::VarDecl: {
        Operand reg = newReg(regClassOf(s.declType));
        varReg_[s.varId] = reg;
        if (s.value) {
          genExpr(*s.value, &reg);  // compute straight into the variable
        } else {
          // "int x;" means x = 0. This must be explicit: after register
          // allocation, x's register may still hold another variable's old value.
          emitMov(reg, s.declType == BaseType::Float ? Operand::makeFloat(0) : Operand::makeInt(0),
                  irTypeOf(s.declType));
        }
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
      case Stmt::Kind::If: {
        int n = ++labelCount_;
        std::string endLabel = "L_endif" + std::to_string(n);
        if (!s.elseBody) {
          //     setp / @!p bra L_endif     <- skip the body when cond is false
          //     ...then...
          // L_endif:
          genBranchIfFalse(*s.cond, endLabel);
          genStmt(*s.body);
        } else {
          //     setp / @!p bra L_else
          //     ...then...
          //     bra L_endif                 <- don't fall into the else part
          // L_else:
          //     ...else...
          // L_endif:
          std::string elseLabel = "L_else" + std::to_string(n);
          genBranchIfFalse(*s.cond, elseLabel);
          genStmt(*s.body);
          emitBra(endLabel);
          emitLabel(elseLabel);
          genStmt(*s.elseBody);
        }
        emitLabel(endLabel);
        break;
      }
      case Stmt::Kind::While: {
        // L_while:
        //     setp / @!p bra L_endwhile    <- test at the top; leave when false
        //     ...body...
        //     bra L_while                  <- loop back and test again
        // L_endwhile:
        int n = ++labelCount_;
        std::string top = "L_while" + std::to_string(n);
        std::string end = "L_endwhile" + std::to_string(n);
        emitLabel(top);
        genBranchIfFalse(*s.cond, end);
        genStmt(*s.body);
        emitBra(top);
        emitLabel(end);
        break;
      }
      case Stmt::Kind::ExprStmt:
        genExpr(*s.value);  // a call whose result (if any) is ignored
        break;
    }
  }

  // A condition is always a comparison (sema guarantees it). It becomes:
  //   setp.<cmp>.<type> %pN, a, b;    // %pN = (a <cmp> b)
  //   @!%pN bra label;                // jump away when the condition is FALSE
  // Jumping on *false* lets the "true" code follow straight after, in source order.
  void genBranchIfFalse(const Expr& cond, const std::string& label) {
    Operand a = genExpr(*cond.lhs);
    Operand b = genExpr(*cond.rhs);
    Instr setp;
    setp.op = Op::Setp;
    setp.type = irTypeOf(cond.lhs->type);
    setp.dst = newReg(RegClass::Pred);
    setp.srcs = {a, b};
    switch (cond.op) {
      case TokenKind::Lt: setp.cmp = Cmp::Lt; break;
      case TokenKind::Gt: setp.cmp = Cmp::Gt; break;
      case TokenKind::Le: setp.cmp = Cmp::Le; break;
      case TokenKind::Ge: setp.cmp = Cmp::Ge; break;
      case TokenKind::Eq: setp.cmp = Cmp::Eq; break;
      case TokenKind::Ne: setp.cmp = Cmp::Ne; break;
      default: throw CompileError("internal error: condition is not a comparison", cond.line, cond.col);
    }
    emit(setp);

    Instr bra;
    bra.op = Op::Bra;
    bra.label = label;
    bra.guard = setp.dst;
    bra.guardNegated = true;
    emit(bra);
  }

  void emitBra(const std::string& label) {
    Instr bra;
    bra.op = Op::Bra;
    bra.label = label;
    emit(bra);
  }

  void emitLabel(const std::string& label) {
    Instr l;
    l.op = Op::Label;
    l.label = label;
    emit(l);
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
          default:  // comparisons only appear as conditions (see genBranchIfFalse)
            throw CompileError("internal error: comparison used as a value", e.line, e.col);
        }
        emit(in);
        return dst;
      }

      case Expr::Kind::Call: {
        // call (%r5), factorial, (%r1);
        Instr call;
        call.op = Op::Call;
        call.label = e.name;
        call.type = ty;
        for (const ExprPtr& arg : e.args) {
          bool isArray = arg->kind == Expr::Kind::Var && fn_.vars[arg->varId].isArray;
          // Arrays are passed by name: the callee reads and writes the same memory.
          call.srcs.push_back(isArray ? Operand::makeArray(fn_.vars[arg->varId].name)
                                      : genExpr(*arg));
        }
        if (e.type != BaseType::Void) call.dst = dstReg();
        emit(call);
        return call.dst;
      }
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

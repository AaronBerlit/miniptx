#include "opt.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace {

using RegKey = std::pair<int, int>;  // (register class, number)
RegKey keyOf(const Operand& o) { return {static_cast<int>(o.cls), o.reg}; }

int32_t wrap(long long v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }

// Must compute exactly what the interpreter would at run time, including 32-bit
// wraparound and single-precision float rounding, or folding changes answers.
std::optional<Operand> evalArith(Op op, IrType t, const Operand& x, const Operand& y) {
  if (t == IrType::S32) {
    long long a = x.imm, b = y.imm;
    switch (op) {
      case Op::Add: return Operand::makeInt(wrap(a + b));
      case Op::Sub: return Operand::makeInt(wrap(a - b));
      case Op::Mul: return Operand::makeInt(wrap(a * b));
      case Op::Div:
        if (b == 0) return std::nullopt;  // leave it: must fail at run time
        return Operand::makeInt(wrap(a / b));
      default: return std::nullopt;
    }
  }
  auto asFloat = [](const Operand& o) {
    return static_cast<float>(o.kind == Operand::Kind::ImmFloat ? o.fimm : o.imm);
  };
  float a = asFloat(x), b = asFloat(y), r;
  switch (op) {
    case Op::Add: r = a + b; break;
    case Op::Sub: r = a - b; break;
    case Op::Mul: r = a * b; break;
    case Op::Div: r = a / b; break;
    default: return std::nullopt;
  }
  if (!std::isfinite(r)) return std::nullopt;  // keep inf/nan as run-time results
  return Operand::makeFloat(r);
}

bool evalCmp(Cmp c, IrType t, const Operand& x, const Operand& y) {
  auto val = [t](const Operand& o) -> double {
    if (t == IrType::S32) return static_cast<int32_t>(o.imm);
    return static_cast<float>(o.kind == Operand::Kind::ImmFloat ? o.fimm : o.imm);
  };
  double a = val(x), b = val(y);
  switch (c) {
    case Cmp::Lt: return a < b;
    case Cmp::Gt: return a > b;
    case Cmp::Le: return a <= b;
    case Cmp::Ge: return a >= b;
    case Cmp::Eq: return a == b;
    case Cmp::Ne: return a != b;
  }
  return false;
}

Instr makeMov(const Operand& dst, const Operand& src, IrType t) {
  Instr m;
  m.op = Op::Mov;
  m.type = t;
  m.dst = dst;
  m.srcs = {src};
  return m;
}

}  // namespace

int instructionCount(const IrFunction& f) {
  int n = 0;
  for (const Instr& in : f.body) n += in.op != Op::Label;
  return n;
}

int instructionCount(const IrModule& m) {
  int n = 0;
  for (const IrFunction& f : m.functions) n += instructionCount(f);
  return n;
}

// ---- Constant folding ----------------------------------------------------------

bool foldConstants(IrFunction& f) {
  bool changed = false;
  // What we know at the current point: register -> the constant it holds.
  std::map<RegKey, Operand> known;
  std::map<int, bool> knownPred;  // predicate register -> true/false

  // Replace a register operand by its constant value, if we know it.
  auto propagate = [&](Operand& o) {
    if (!o.isReg() || o.cls == RegClass::Pred) return;
    auto it = known.find(keyOf(o));
    if (it == known.end()) return;
    o = it->second;
    changed = true;
  };
  // dst is about to be overwritten: whatever we knew about it is stale.
  auto forget = [&](const Operand& d) {
    if (!d.isReg()) return;
    if (d.cls == RegClass::Pred) knownPred.erase(d.reg);
    else known.erase(keyOf(d));
  };

  std::vector<Instr> out;
  for (Instr in : f.body) {
    switch (in.op) {
      case Op::Label:
        // Control can jump here from elsewhere, where registers may hold
        // different values. Only knowledge from straight-line code is safe.
        known.clear();
        knownPred.clear();
        break;

      case Op::Mov:
        propagate(in.srcs[0]);
        forget(in.dst);
        if (in.srcs[0].isImm()) known[keyOf(in.dst)] = in.srcs[0];
        break;

      case Op::Add:
      case Op::Sub:
      case Op::Mul:
      case Op::Div:
        propagate(in.srcs[0]);
        propagate(in.srcs[1]);
        forget(in.dst);
        if (in.srcs[0].isImm() && in.srcs[1].isImm()) {
          if (auto r = evalArith(in.op, in.type, in.srcs[0], in.srcs[1])) {
            in = makeMov(in.dst, *r, in.type);  // add %r2, 2, 3  ->  mov %r2, 5
            known[keyOf(in.dst)] = *r;
            changed = true;
          }
        }
        break;

      case Op::Neg:
        propagate(in.srcs[0]);
        forget(in.dst);
        if (in.srcs[0].isImm()) {
          const Operand& s = in.srcs[0];
          Operand r = in.type == IrType::S32 ? Operand::makeInt(wrap(-s.imm))
                                             : Operand::makeFloat(-static_cast<float>(
                                                   s.kind == Operand::Kind::ImmFloat ? s.fimm : s.imm));
          in = makeMov(in.dst, r, in.type);
          known[keyOf(in.dst)] = r;
          changed = true;
        }
        break;

      case Op::Ld:
        propagate(in.srcs[1]);  // the index
        forget(in.dst);
        break;

      case Op::St:
        propagate(in.srcs[1]);
        propagate(in.srcs[2]);
        break;

      case Op::Setp:
        propagate(in.srcs[0]);
        propagate(in.srcs[1]);
        forget(in.dst);
        if (in.srcs[0].isImm() && in.srcs[1].isImm())
          knownPred[in.dst.reg] = evalCmp(in.cmp, in.type, in.srcs[0], in.srcs[1]);
        break;

      case Op::Bra:
        if (in.guard.isReg()) {
          auto it = knownPred.find(in.guard.reg);
          if (it != knownPred.end()) {
            changed = true;
            bool taken = it->second != in.guardNegated;
            if (!taken) continue;  // never jumps: delete the branch
            in.guard = Operand();  // always jumps: make it unconditional
            in.guardNegated = false;
          }
        }
        break;

      case Op::Call:
        for (Operand& a : in.srcs) propagate(a);
        forget(in.dst);
        break;

      case Op::Ret:
        if (!in.srcs.empty()) propagate(in.srcs[0]);
        break;
    }
    out.push_back(in);
  }
  f.body = std::move(out);
  return changed;
}

// ---- Unreachable code ----------------------------------------------------------

bool removeUnreachable(IrFunction& f) {
  bool changed = false;
  std::vector<Instr>& body = f.body;

  // 1. Reachability: start at the first instruction and follow every edge.
  std::map<std::string, size_t> labelAt;
  for (size_t i = 0; i < body.size(); i++)
    if (body[i].op == Op::Label) labelAt[body[i].label] = i;

  std::vector<bool> reached(body.size(), false);
  std::vector<size_t> work;
  if (!body.empty()) work.push_back(0);
  while (!work.empty()) {
    size_t i = work.back();
    work.pop_back();
    if (i >= body.size() || reached[i]) continue;
    reached[i] = true;
    const Instr& in = body[i];
    if (in.op == Op::Ret) continue;                        // no successor
    if (in.op == Op::Bra) {
      auto t = labelAt.find(in.label);
      if (t != labelAt.end()) work.push_back(t->second);  // the jump
      if (!in.guard.isReg()) continue;                     // unconditional: no fall-through
    }
    work.push_back(i + 1);                                 // fall through
  }

  std::vector<Instr> out;
  for (size_t i = 0; i < body.size(); i++) {
    if (reached[i]) out.push_back(body[i]);
    else changed = true;
  }

  // 2. "bra L;" where L is the very next thing is a jump to where we'd go anyway.
  std::vector<Instr> out2;
  for (size_t i = 0; i < out.size(); i++) {
    if (out[i].op == Op::Bra) {
      bool toNext = false;
      for (size_t j = i + 1; j < out.size() && out[j].op == Op::Label; j++)
        if (out[j].label == out[i].label) toNext = true;
      if (toNext) {
        changed = true;
        continue;
      }
    }
    out2.push_back(out[i]);
  }

  // 3. Labels that no branch targets anymore are just clutter.
  std::set<std::string> targets;
  for (const Instr& in : out2)
    if (in.op == Op::Bra) targets.insert(in.label);
  body.clear();
  for (const Instr& in : out2) {
    if (in.op == Op::Label && !targets.count(in.label)) {
      changed = true;
      continue;
    }
    body.push_back(in);
  }
  return changed;
}

// ---- Dead values ---------------------------------------------------------------

bool removeDeadValues(IrFunction& f) {
  bool changedAny = false;
  bool changed = true;
  while (changed) {  // removing one dead value can make its inputs dead too
    changed = false;
    std::set<RegKey> used;
    for (const Instr& in : f.body) {
      for (const Operand& o : in.srcs)
        if (o.isReg()) used.insert(keyOf(o));
      if (in.guard.isReg()) used.insert(keyOf(in.guard));
    }

    std::vector<Instr> out;
    for (Instr& in : f.body) {
      if (in.dst.isReg() && !used.count(keyOf(in.dst))) {
        // Only delete instructions whose ONLY effect is writing dst. Integer
        // div and ld can also fail at run time (divide by zero, bad index),
        // so they stay, keeping optimized and unoptimized behaviour identical.
        bool pureOnly = in.op == Op::Mov || in.op == Op::Add || in.op == Op::Sub ||
                        in.op == Op::Mul || in.op == Op::Neg || in.op == Op::Setp ||
                        (in.op == Op::Div && in.type == IrType::F32);
        if (pureOnly) {
          changed = true;
          continue;
        }
        if (in.op == Op::Call) {  // the call has effects; just drop the unused result
          in.dst = Operand();
          changed = true;
        }
      }
      out.push_back(in);
    }
    f.body = std::move(out);
    changedAny |= changed;
  }
  return changedAny;
}

void optimize(IrModule& m) {
  for (IrFunction& f : m.functions) {
    // Run the passes until none of them finds anything more to do.
    for (int round = 0; round < 20; round++) {
      bool changed = foldConstants(f);
      changed |= removeUnreachable(f);
      changed |= removeDeadValues(f);
      if (!changed) break;
    }
  }
}

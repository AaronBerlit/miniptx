#include "regalloc.h"

#include <algorithm>
#include <climits>
#include <map>
#include <set>

namespace {

// Is this operand a register of class c?
bool isRegOf(const Operand& o, RegClass c) { return o.isReg() && o.cls == c; }

// Registers of class c that instruction `in` reads.
std::vector<int> usesOf(const Instr& in, RegClass c) {
  std::vector<int> u;
  for (const Operand& o : in.srcs)
    if (isRegOf(o, c)) u.push_back(o.reg);
  if (isRegOf(in.guard, c)) u.push_back(in.guard.reg);
  return u;
}

// ---- Step 1: liveness -----------------------------------------------------------
//
// A register is LIVE at a point if its current value will be read later.
// For each instruction k (working backwards, because "will be read later"
// depends on what comes after):
//
//   liveOut[k] = union of liveIn[s] for every successor s of k
//   liveIn[k]  = uses(k)  +  (liveOut[k] - def(k))
//
// Loops make one backward pass insufficient: the loop's last instruction has
// the loop's FIRST instruction as a successor, which we haven't finished yet.
// So we repeat until nothing changes (another fixpoint).
struct Liveness {
  std::vector<std::vector<char>> in, out;  // [instruction][register] -> live?
};

Liveness computeLiveness(const IrFunction& f, RegClass c, int numRegs) {
  size_t n = f.body.size();
  std::vector<std::vector<size_t>> succ = successors(f);
  Liveness L;
  L.in.assign(n, std::vector<char>(numRegs + 1, 0));
  L.out.assign(n, std::vector<char>(numRegs + 1, 0));

  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t k = n; k-- > 0;) {
      const Instr& in = f.body[k];
      std::vector<char> out(numRegs + 1, 0);
      for (size_t s : succ[k])
        for (int r = 1; r <= numRegs; r++) out[r] |= L.in[s][r];

      std::vector<char> live = out;
      if (isRegOf(in.dst, c)) live[in.dst.reg] = 0;  // overwritten here: old value is dead
      for (int r : usesOf(in, c)) live[r] = 1;       // read here: must be alive

      if (live != L.in[k] || out != L.out[k]) {
        L.in[k] = std::move(live);
        L.out[k] = std::move(out);
        changed = true;
      }
    }
  }
  return L;
}

// ---- Step 2: live intervals ----------------------------------------------------
//
// Give each instruction k two positions: 2k = "while reading inputs",
// 2k+1 = "after writing the output". A register's interval is the span from
// the first to the last position where it is alive or written.
//
// Why two positions? In  add %r3, %r1, %r2  where this is %r1's last use,
// %r1 is alive at 2k (being read) and %r3 starts at 2k+1. The intervals don't
// overlap, so %r3 may reuse %r1's register -- reading happens before writing.
struct Interval {
  int vreg;
  int start = INT_MAX;
  int end = INT_MIN;
  void cover(int pos) {
    start = std::min(start, pos);
    end = std::max(end, pos);
  }
};

std::vector<Interval> buildIntervals(const IrFunction& f, RegClass c, int numRegs) {
  Liveness L = computeLiveness(f, c, numRegs);
  std::vector<Interval> iv(numRegs + 1);
  for (int r = 1; r <= numRegs; r++) iv[r].vreg = r;

  // Parameters are written by the caller, before instruction 0.
  for (const IrParam& p : f.params)
    if (isRegOf(p.reg, c)) iv[p.reg.reg].cover(-1);

  for (size_t k = 0; k < f.body.size(); k++) {
    int pos = static_cast<int>(2 * k);
    for (int r = 1; r <= numRegs; r++) {
      if (L.in[k][r]) iv[r].cover(pos);
      if (L.out[k][r]) iv[r].cover(pos + 1);
    }
    const Instr& in = f.body[k];
    if (isRegOf(in.dst, c)) iv[in.dst.reg].cover(pos + 1);
  }

  std::vector<Interval> used;
  for (int r = 1; r <= numRegs; r++)
    if (iv[r].start != INT_MAX) used.push_back(iv[r]);  // skip registers that never appear
  return used;
}

// ---- Step 3: linear scan -------------------------------------------------------
//
// Visit intervals in order of start position. Before placing one, free the
// registers of every interval that has already ended. Then take the lowest
// free register number (or a brand-new one if none is free).
std::map<int, int> linearScan(std::vector<Interval> intervals) {
  std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
    return a.start != b.start ? a.start < b.start : a.vreg < b.vreg;
  });

  std::map<int, int> assign;           // virtual -> physical
  std::vector<Interval> active;        // intervals currently holding a register
  std::set<int> freeRegs;              // physical registers available for reuse
  int nextNew = 1;

  for (const Interval& cur : intervals) {
    // Expire: anything that ended before cur starts gives its register back.
    for (auto it = active.begin(); it != active.end();) {
      if (it->end < cur.start) {
        freeRegs.insert(assign[it->vreg]);
        it = active.erase(it);
      } else {
        ++it;
      }
    }
    int phys;
    if (!freeRegs.empty()) {
      phys = *freeRegs.begin();
      freeRegs.erase(freeRegs.begin());
    } else {
      phys = nextNew++;
    }
    assign[cur.vreg] = phys;
    active.push_back(cur);
  }
  return assign;
}

void allocateClass(IrFunction& f, RegClass c) {
  int numRegs = f.numRegs[static_cast<int>(c)];
  if (numRegs == 0) return;
  std::map<int, int> assign = linearScan(buildIntervals(f, c, numRegs));

  auto rename = [&](Operand& o) {
    if (isRegOf(o, c)) o.reg = assign.at(o.reg);
  };
  for (IrParam& p : f.params) rename(p.reg);
  for (Instr& in : f.body) {
    rename(in.dst);
    rename(in.guard);
    for (Operand& o : in.srcs) rename(o);
  }
  int used = 0;
  for (const auto& kv : assign) used = std::max(used, kv.second);
  f.numRegs[static_cast<int>(c)] = used;
}

}  // namespace

void allocateRegisters(IrFunction& f) {
  allocateClass(f, RegClass::Int);
  allocateClass(f, RegClass::Float);
  allocateClass(f, RegClass::Pred);

  // A copy whose source and destination ended up in the same register
  // ("mov %r1, %r1") does nothing; drop it. A free bonus of sharing registers.
  std::vector<Instr> out;
  for (const Instr& in : f.body)
    if (!(in.op == Op::Mov && in.dst.sameReg(in.srcs[0]))) out.push_back(in);
  f.body = std::move(out);
}

void allocateRegisters(IrModule& m) {
  for (IrFunction& f : m.functions) allocateRegisters(f);
}

int registerCount(const IrFunction& f) {
  std::set<std::pair<int, int>> seen;
  auto see = [&](const Operand& o) {
    if (o.isReg()) seen.insert({static_cast<int>(o.cls), o.reg});
  };
  for (const IrParam& p : f.params) see(p.reg);
  for (const Instr& in : f.body) {
    see(in.dst);
    see(in.guard);
    for (const Operand& o : in.srcs) see(o);
  }
  return static_cast<int>(seen.size());
}

int registerCount(const IrModule& m) {
  int n = 0;
  for (const IrFunction& f : m.functions) n += registerCount(f);
  return n;
}

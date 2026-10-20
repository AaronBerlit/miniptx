// A small line-based assembler-style reader. Each line is one of:
//   .func name(params) [-> .type]     function header
//   {   }                             body start / end
//   L_name:                           label
//   [@[!]%pN] op.mods operands;       instruction
#include "ptxreader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>

#include "diagnostics.h"

namespace {

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r");
  return s.substr(b, e - b + 1);
}

bool startsWith(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool isIdent(const std::string& s) {
  if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
  return std::all_of(s.begin(), s.end(),
                     [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
}

class Reader {
 public:
  IrModule run(const std::string& text) {
    std::istringstream in(text);
    std::string raw;
    while (std::getline(in, raw)) {
      line_++;
      size_t comment = raw.find("//");
      std::string s = trim(comment == std::string::npos ? raw : raw.substr(0, comment));
      if (s.empty()) continue;

      if (startsWith(s, ".func")) {
        if (inBody_) fail("missing '}' before next .func");
        module_.functions.emplace_back();
        header(trim(s.substr(5)), module_.functions.back());
        expectBrace_ = true;
      } else if (s == "{") {
        if (!expectBrace_) fail("unexpected '{'");
        expectBrace_ = false;
        inBody_ = true;
      } else if (s == "}") {
        if (!inBody_) fail("unexpected '}'");
        inBody_ = false;
      } else if (!inBody_) {
        fail("expected '.func' or '{'");
      } else if (s.back() == ':') {
        Instr l;
        l.op = Op::Label;
        l.label = s.substr(0, s.size() - 1);
        if (!isIdent(l.label)) fail("bad label name '" + l.label + "'");
        current().body.push_back(l);
      } else {
        current().body.push_back(instruction(s));
      }
    }
    if (inBody_ || expectBrace_) fail("unexpected end of file (missing '}')");
    for (IrFunction& f : module_.functions) countRegisters(f);
    return module_;
  }

 private:
  IrModule module_;
  int line_ = 0;
  bool inBody_ = false;
  bool expectBrace_ = false;

  [[noreturn]] void fail(const std::string& msg) const { throw CompileError(msg, line_, 1); }
  IrFunction& current() { return module_.functions.back(); }

  IrType parseType(const std::string& t) {
    if (t == "s32" || t == ".s32") return IrType::S32;
    if (t == "f32" || t == ".f32") return IrType::F32;
    fail("unknown type '" + t + "'");
  }

  // .func name(.s32 %r1, .f32 a[]) -> .s32
  void header(const std::string& s, IrFunction& f) {
    size_t open = s.find('('), close = s.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open)
      fail("malformed .func header");
    f.name = trim(s.substr(0, open));
    if (!isIdent(f.name)) fail("bad function name '" + f.name + "'");

    for (const std::string& p : splitTopLevel(s.substr(open + 1, close - open - 1))) {
      size_t sp = p.find(' ');
      if (sp == std::string::npos) fail("malformed parameter '" + p + "'");
      IrParam ip;
      ip.type = parseType(p.substr(0, sp));
      std::string rest = trim(p.substr(sp + 1));
      ip.isArray = rest.size() > 2 && rest.compare(rest.size() - 2, 2, "[]") == 0;
      if (ip.isArray) {
        ip.name = rest.substr(0, rest.size() - 2);
      } else {
        ip.reg = operand(rest);
        if (!ip.reg.isReg()) fail("scalar parameter must be a register");
        ip.name = rest;
      }
      f.params.push_back(ip);
    }

    std::string tail = trim(s.substr(close + 1));
    if (!tail.empty()) {
      if (!startsWith(tail, "->")) fail("expected '->' after parameters");
      f.returnsValue = true;
      f.returnType = parseType(trim(tail.substr(2)));
    }
  }

  // Splits "a, [b + c], (d, e)" on commas that are not inside [] or ().
  std::vector<std::string> splitTopLevel(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
      if (c == '[' || c == '(') depth++;
      if (c == ']' || c == ')') depth--;
      if (c == ',' && depth == 0) {
        out.push_back(trim(cur));
        cur.clear();
      } else {
        cur += c;
      }
    }
    if (!trim(cur).empty() || !out.empty()) out.push_back(trim(cur));
    return out;
  }

  Operand operand(const std::string& s) {
    if (s == "%tid.x") return Operand::makeTid();
    if (s.size() >= 3 && s[0] == '%' && std::string("rfp").find(s[1]) != std::string::npos &&
        std::all_of(s.begin() + 2, s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); })) {
      RegClass c = s[1] == 'r' ? RegClass::Int : s[1] == 'f' ? RegClass::Float : RegClass::Pred;
      int n = std::stoi(s.substr(2));
      if (n < 1) fail("register numbers start at 1");
      return Operand::makeReg(c, n);
    }
    if (!s.empty() && (std::isdigit(static_cast<unsigned char>(s[0])) || s[0] == '-')) {
      try {
        size_t used = 0;
        if (s.find_first_of(".eEn") != std::string::npos) {  // 2.5, 1e+10, inf, nan
          double v = std::stod(s, &used);
          if (used == s.size()) return Operand::makeFloat(v);
        } else {
          long long v = std::stoll(s, &used);
          if (used == s.size() && v >= INT32_MIN && v <= INT32_MAX) return Operand::makeInt(v);
        }
      } catch (const std::exception&) {
      }
      fail("bad number '" + s + "'");
    }
    if (isIdent(s)) return Operand::makeArray(s);
    fail("bad operand '" + s + "'");
  }

  // "[a + %r1]" -> array operand + index operand
  std::pair<Operand, Operand> memory(const std::string& s) {
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') fail("expected [array + index], got '" + s + "'");
    std::string inner = s.substr(1, s.size() - 2);
    size_t plus = inner.find('+');
    if (plus == std::string::npos) fail("expected [array + index], got '" + s + "'");
    Operand arr = operand(trim(inner.substr(0, plus)));
    if (arr.kind != Operand::Kind::Array) fail("memory base must be an array name");
    return {arr, operand(trim(inner.substr(plus + 1)))};
  }

  Instr instruction(std::string s) {
    if (s.back() != ';') fail("missing ';'");
    s = trim(s.substr(0, s.size() - 1));
    Instr in;

    // Optional guard: @%p1 or @!%p1
    if (s[0] == '@') {
      size_t sp = s.find(' ');
      if (sp == std::string::npos) fail("guard without instruction");
      std::string g = s.substr(1, sp - 1);
      in.guardNegated = !g.empty() && g[0] == '!';
      in.guard = operand(in.guardNegated ? g.substr(1) : g);
      if (!in.guard.isReg() || in.guard.cls != RegClass::Pred) fail("guard must be a predicate register");
      s = trim(s.substr(sp));
    }

    size_t sp = s.find_first_of(" \t");
    std::string opcode = s.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : trim(s.substr(sp));
    std::vector<std::string> mods;  // "setp.lt.s32" -> {"setp", "lt", "s32"}
    {
      std::stringstream ss(opcode);
      std::string part;
      while (std::getline(ss, part, '.')) mods.push_back(part);
    }
    const std::string& base = mods[0];
    std::vector<std::string> ops = splitTopLevel(rest);
    auto need = [&](size_t nMods, size_t nOps) {
      if (mods.size() != nMods) fail("malformed opcode '" + opcode + "'");
      if (ops.size() != nOps)
        fail("'" + base + "' expects " + std::to_string(nOps) + " operand(s), got " + std::to_string(ops.size()));
    };
    if (in.guard.isReg() && base != "bra") fail("only 'bra' may have a guard");

    if (base == "mov" || base == "neg") {
      need(2, 2);
      in.op = base == "mov" ? Op::Mov : Op::Neg;
      in.type = parseType(mods[1]);
      in.dst = operand(ops[0]);
      in.srcs = {operand(ops[1])};
    } else if (base == "add" || base == "sub" || base == "mul" || base == "div") {
      need(2, 3);
      in.op = base == "add" ? Op::Add : base == "sub" ? Op::Sub : base == "mul" ? Op::Mul : Op::Div;
      in.type = parseType(mods[1]);
      in.dst = operand(ops[0]);
      in.srcs = {operand(ops[1]), operand(ops[2])};
    } else if (base == "ld") {
      need(2, 2);
      in.op = Op::Ld;
      in.type = parseType(mods[1]);
      in.dst = operand(ops[0]);
      auto [arr, idx] = memory(ops[1]);
      in.srcs = {arr, idx};
    } else if (base == "st") {
      need(2, 2);
      in.op = Op::St;
      in.type = parseType(mods[1]);
      auto [arr, idx] = memory(ops[0]);
      in.srcs = {arr, idx, operand(ops[1])};
    } else if (base == "setp") {
      need(3, 3);
      in.op = Op::Setp;
      const std::string& c = mods[1];
      if (c == "lt") in.cmp = Cmp::Lt;
      else if (c == "gt") in.cmp = Cmp::Gt;
      else if (c == "le") in.cmp = Cmp::Le;
      else if (c == "ge") in.cmp = Cmp::Ge;
      else if (c == "eq") in.cmp = Cmp::Eq;
      else if (c == "ne") in.cmp = Cmp::Ne;
      else fail("unknown comparison '" + c + "'");
      in.type = parseType(mods[2]);
      in.dst = operand(ops[0]);
      in.srcs = {operand(ops[1]), operand(ops[2])};
    } else if (base == "bra") {
      need(1, 1);
      in.op = Op::Bra;
      in.label = ops[0];
      if (!isIdent(in.label)) fail("bad label '" + in.label + "'");
    } else if (base == "call") {
      // call (%r1), name, (args);   or   call name, (args);
      if (mods.size() != 1) fail("malformed opcode '" + opcode + "'");
      in.op = Op::Call;
      size_t i = 0;
      if (ops.size() == 3) {
        if (ops[0].size() < 2 || ops[0].front() != '(' || ops[0].back() != ')') fail("bad call result");
        in.dst = operand(trim(ops[0].substr(1, ops[0].size() - 2)));
        if (in.dst.isReg() && in.dst.cls == RegClass::Float) in.type = IrType::F32;
        i = 1;
      } else if (ops.size() != 2) {
        fail("malformed call");
      }
      in.label = ops[i];
      const std::string& args = ops[i + 1];
      if (args.size() < 2 || args.front() != '(' || args.back() != ')') fail("call arguments must be in ( )");
      for (const std::string& a : splitTopLevel(args.substr(1, args.size() - 2))) in.srcs.push_back(operand(a));
    } else if (base == "ret") {
      if (mods.size() != 1 || ops.size() > 1) fail("malformed ret");
      in.op = Op::Ret;
      if (!ops.empty()) in.srcs = {operand(ops[0])};
    } else {
      fail("unknown instruction '" + base + "'");
    }
    return in;
  }

  // numRegs isn't written in the text, so recover it: the highest register
  // number seen in each class.
  static void countRegisters(IrFunction& f) {
    auto see = [&](const Operand& o) {
      if (!o.isReg()) return;
      int& n = f.numRegs[static_cast<int>(o.cls)];
      n = std::max(n, o.reg);
    };
    for (const IrParam& p : f.params) see(p.reg);
    for (const Instr& in : f.body) {
      see(in.dst);
      see(in.guard);
      for (const Operand& o : in.srcs) see(o);
    }
  }
};

}  // namespace

IrModule readPtx(const std::string& text) { return Reader().run(text); }

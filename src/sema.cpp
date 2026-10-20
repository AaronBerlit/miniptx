#include "sema.h"

#include <unordered_map>

#include "diagnostics.h"

namespace {

[[noreturn]] void fail(const std::string& msg, int line, int col) {
  throw CompileError(msg, line, col);
}

std::string quoted(const std::string& s) { return "'" + s + "'"; }

class Analyzer {
 public:
  explicit Analyzer(Program& p) : program_(p) {}

  void run() {
    // Pass 1: collect every function signature first, so a function can call
    // one defined later in the file (or itself, recursively).
    for (Function& f : program_.functions) {
      if (functions_.count(f.name))
        fail("function " + quoted(f.name) + " is already defined", f.line, f.col);
      functions_[f.name] = &f;
    }
    // Pass 2: check each function body.
    for (Function& f : program_.functions) checkFunction(f);
  }

 private:
  Program& program_;
  std::unordered_map<std::string, Function*> functions_;

  // State for the function currently being checked.
  Function* fn_ = nullptr;
  // Scopes: one map (name -> varId) per open block. Lookup searches innermost
  // first, so an inner declaration can shadow an outer one.
  std::vector<std::unordered_map<std::string, int>> scopes_;

  // ---- Scopes ---------------------------------------------------------------

  int declare(const std::string& name, BaseType type, bool isArray, bool isParam,
              int line, int col) {
    auto& scope = scopes_.back();
    if (scope.count(name))
      fail("variable " + quoted(name) + " is already declared in this scope", line, col);
    int id = static_cast<int>(fn_->vars.size());
    fn_->vars.push_back({name, type, isArray, isParam});
    scope[name] = id;
    return id;
  }

  int lookup(const std::string& name, int line, int col) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(name);
      if (found != it->end()) return found->second;
    }
    fail("use of undeclared variable " + quoted(name), line, col);
  }

  const VarInfo& var(int id) const { return fn_->vars[id]; }

  // ---- Functions and statements --------------------------------------------

  void checkFunction(Function& f) {
    fn_ = &f;
    scopes_.clear();
    scopes_.emplace_back();  // parameters live in the outermost scope
    for (Param& p : f.params)
      p.varId = declare(p.name, p.type, p.isArray, true, p.line, p.col);

    checkBlockContents(*f.body);

    if (f.returnType != BaseType::Void && !alwaysReturns(*f.body))
      fail("function " + quoted(f.name) + " can reach the end without returning a value",
           f.line, f.col);
  }

  // A block opens a new scope; its variables vanish at the closing brace.
  void checkBlock(Stmt& block) {
    scopes_.emplace_back();
    checkBlockContents(block);
    scopes_.pop_back();
  }
  void checkBlockContents(Stmt& block) {
    for (StmtPtr& s : block.stmts) checkStmt(*s);
  }

  void checkStmt(Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::VarDecl:
        // Check the initializer BEFORE declaring, so "int x = x;" is an error.
        if (s.value) expectType(*s.value, s.declType, "initializer of " + quoted(s.name));
        s.varId = declare(s.name, s.declType, false, false, s.line, s.col);
        break;

      case Stmt::Kind::Assign: {
        s.varId = lookup(s.name, s.line, s.col);
        const VarInfo& v = var(s.varId);
        if (s.index) {
          if (!v.isArray) fail(quoted(s.name) + " is not an array", s.line, s.col);
          expectType(*s.index, BaseType::Int, "array index");
        } else if (v.isArray) {
          fail("cannot assign to a whole array " + quoted(s.name), s.line, s.col);
        }
        expectType(*s.value, v.type, "assignment to " + quoted(s.name));
        break;
      }

      case Stmt::Kind::If:
        checkCondition(*s.cond, "if");
        checkBlock(*s.body);
        if (s.elseBody) {
          if (s.elseBody->kind == Stmt::Kind::Block) checkBlock(*s.elseBody);
          else checkStmt(*s.elseBody);  // else-if
        }
        break;

      case Stmt::Kind::While:
        checkCondition(*s.cond, "while");
        checkBlock(*s.body);
        break;

      case Stmt::Kind::Return:
        if (fn_->returnType == BaseType::Void) {
          if (s.value)
            fail("function " + quoted(fn_->name) + " has no return type, so it cannot "
                 "return a value", s.line, s.col);
        } else {
          if (!s.value)
            fail("function " + quoted(fn_->name) + " must return a value of type " +
                 typeName(fn_->returnType), s.line, s.col);
          expectType(*s.value, fn_->returnType, "return value");
        }
        break;

      case Stmt::Kind::Block:
        checkBlock(s);
        break;

      case Stmt::Kind::ExprStmt:
        checkExpr(*s.value, /*allowVoid=*/true);
        break;
    }
  }

  void checkCondition(Expr& cond, const char* what) {
    checkExpr(cond, false, /*allowBool=*/true);
    if (cond.type != BaseType::Bool)
      fail(std::string(what) + " condition must be a comparison (e.g. x != 0)",
           cond.line, cond.col);
  }

  // Conservative "does every path end in a return?" check.
  // A while loop never counts: the checker can't know the loop runs at all.
  static bool alwaysReturns(const Stmt& s) {
    switch (s.kind) {
      case Stmt::Kind::Return:
        return true;
      case Stmt::Kind::Block:
        for (const StmtPtr& c : s.stmts)
          if (alwaysReturns(*c)) return true;
        return false;
      case Stmt::Kind::If:
        return s.elseBody && alwaysReturns(*s.body) && alwaysReturns(*s.elseBody);
      default:
        return false;
    }
  }

  // ---- Expressions ----------------------------------------------------------

  void expectType(Expr& e, BaseType want, const std::string& context) {
    checkExpr(e);
    if (e.type != want)
      fail("type mismatch in " + context + ": expected " + typeName(want) + ", got " +
           typeName(e.type), e.line, e.col);
  }

  // Computes e.type (and varId where relevant). Comparisons (Bool) are only
  // legal where allowBool is set; void calls only where allowVoid is set.
  void checkExpr(Expr& e, bool allowVoid = false, bool allowBool = false) {
    switch (e.kind) {
      case Expr::Kind::IntLit:
      case Expr::Kind::Tid:
        e.type = BaseType::Int;
        break;

      case Expr::Kind::FloatLit:
        e.type = BaseType::Float;
        break;

      case Expr::Kind::Var: {
        e.varId = lookup(e.name, e.line, e.col);
        const VarInfo& v = var(e.varId);
        if (v.isArray)
          fail("array " + quoted(e.name) + " must be indexed, like " + e.name + "[i]",
               e.line, e.col);
        e.type = v.type;
        break;
      }

      case Expr::Kind::Index: {
        e.varId = lookup(e.name, e.line, e.col);
        const VarInfo& v = var(e.varId);
        if (!v.isArray) fail(quoted(e.name) + " is not an array", e.line, e.col);
        expectType(*e.lhs, BaseType::Int, "array index");
        e.type = v.type;
        break;
      }

      case Expr::Kind::Call:
        checkCall(e);
        if (e.type == BaseType::Void && !allowVoid)
          fail("function " + quoted(e.name) + " does not return a value", e.line, e.col);
        break;

      case Expr::Kind::Unary:
        checkExpr(*e.lhs);
        e.type = e.lhs->type;
        break;

      case Expr::Kind::Binary: {
        checkExpr(*e.lhs);
        checkExpr(*e.rhs);
        // No implicit int<->float conversion: both sides must match exactly.
        if (e.lhs->type != e.rhs->type)
          fail(std::string("type mismatch: ") + typeName(e.lhs->type) + " " +
               tokenKindName(e.op) + " " + typeName(e.rhs->type), e.line, e.col);
        bool isCompare = e.op != TokenKind::Plus && e.op != TokenKind::Minus &&
                         e.op != TokenKind::Star && e.op != TokenKind::Slash;
        if (isCompare) {
          if (!allowBool)
            fail("a comparison can only be used as an if/while condition", e.line, e.col);
          e.type = BaseType::Bool;
        } else {
          e.type = e.lhs->type;
        }
        break;
      }
    }
  }

  void checkCall(Expr& e) {
    auto it = functions_.find(e.name);
    if (it == functions_.end())
      fail("call to undefined function " + quoted(e.name), e.line, e.col);
    const Function& callee = *it->second;

    if (e.args.size() != callee.params.size())
      fail("function " + quoted(e.name) + " expects " +
           std::to_string(callee.params.size()) + " argument(s), got " +
           std::to_string(e.args.size()), e.line, e.col);

    for (size_t i = 0; i < e.args.size(); i++) {
      Expr& arg = *e.args[i];
      const Param& p = callee.params[i];
      std::string ctx = "argument " + std::to_string(i + 1) + " of " + quoted(e.name);
      if (p.isArray) {
        // An array parameter needs a bare array name: f(a), not f(a[0]).
        if (arg.kind != Expr::Kind::Var)
          fail(ctx + " must be an array", arg.line, arg.col);
        arg.varId = lookup(arg.name, arg.line, arg.col);
        const VarInfo& v = var(arg.varId);
        if (!v.isArray || v.type != p.type)
          fail(ctx + " must be a " + typeName(p.type) + " array", arg.line, arg.col);
        arg.type = v.type;
      } else {
        expectType(arg, p.type, ctx);
      }
    }
    e.type = callee.returnType;
  }
};

}  // namespace

void analyze(Program& program) { Analyzer(program).run(); }

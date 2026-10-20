#pragma once
// The Abstract Syntax Tree: what the parser produces and every later stage reads.
//
// Design choice: one struct per category (Expr, Stmt) with a `kind` tag, rather
// than a class per node type. Later stages just `switch (e.kind)`. Each struct
// has a few fields, and each kind only uses some of them (listed below).
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "lexer.h"

// Bool is the type of a comparison. It can only be used as an if/while
// condition, which maps directly onto PTX: setp writes a predicate, bra reads it.
enum class BaseType { Int, Float, Void, Bool };
const char* typeName(BaseType t);

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct Expr {
  enum class Kind {
    IntLit,    // intValue
    FloatLit,  // floatValue
    Tid,       // (no fields) the built-in thread id
    Var,       // name
    Index,     // name[lhs]
    Call,      // name(args...)
    Unary,     // op lhs        (op is always Minus)
    Binary,    // lhs op rhs
  };
  Kind kind;
  int line = 0, col = 0;

  long long intValue = 0;
  double floatValue = 0;
  std::string name;
  TokenKind op = TokenKind::Eof;
  ExprPtr lhs, rhs;
  std::vector<ExprPtr> args;

  // Filled in by semantic analysis (step 3); the parser leaves these empty.
  BaseType type = BaseType::Void;
  int varId = -1;  // Var, Index: which declaration this name refers to
};

struct Stmt {
  enum class Kind {
    VarDecl,   // declType name [= value]
    Assign,    // name = value   or   name[index] = value
    If,        // if (cond) body [else elseBody]   (elseBody is a Block or an If)
    While,     // while (cond) body
    Return,    // return [value]
    Block,     // { stmts... }
    ExprStmt,  // value;   (in practice, a call)
  };
  Kind kind;
  int line = 0, col = 0;

  BaseType declType = BaseType::Void;
  std::string name;
  ExprPtr index;  // Assign to array element only
  ExprPtr value;  // VarDecl init, Assign rhs, Return value, ExprStmt
  ExprPtr cond;   // If, While
  StmtPtr body;   // If "then" block, While body
  StmtPtr elseBody;
  std::vector<StmtPtr> stmts;  // Block

  int varId = -1;  // VarDecl, Assign: filled in by semantic analysis
};

struct Param {
  BaseType type;
  std::string name;
  bool isArray;
  int line, col;
  int varId = -1;  // filled in by semantic analysis
};

// One entry per declared variable (parameter or local) in a function.
// Semantic analysis builds this table; varId is the index into it.
struct VarInfo {
  std::string name;
  BaseType type;
  bool isArray;
  bool isParam;
};

struct Function {
  std::string name;
  std::vector<Param> params;
  BaseType returnType = BaseType::Void;
  StmtPtr body;  // always a Block
  int line = 0, col = 0;
  std::vector<VarInfo> vars;  // filled in by semantic analysis
};

struct Program {
  std::vector<Function> functions;
};

// Debug printing (used by --dump-ast and by the tests).
// Expressions print as S-expressions: a + b * c  ->  (+ a (* b c))
std::string exprToString(const Expr& e);
void printProgram(const Program& p, std::ostream& out);

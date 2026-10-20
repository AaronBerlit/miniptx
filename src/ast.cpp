#include "ast.h"

#include <iomanip>
#include <sstream>

const char* typeName(BaseType t) {
  switch (t) {
    case BaseType::Int: return "int";
    case BaseType::Float: return "float";
    case BaseType::Void: return "void";
    case BaseType::Bool: return "bool";
  }
  return "?";
}

// Always show a float with a '.', so 2.0 prints as "2.0" and not "2" (an int).
static std::string floatToString(double v) {
  std::ostringstream os;
  os << std::setprecision(9) << v;
  std::string s = os.str();
  if (s.find_first_of(".einf") == std::string::npos) s += ".0";
  return s;
}

std::string exprToString(const Expr& e) {
  switch (e.kind) {
    case Expr::Kind::IntLit: return std::to_string(e.intValue);
    case Expr::Kind::FloatLit: return floatToString(e.floatValue);
    case Expr::Kind::Tid: return "tid";
    case Expr::Kind::Var: return e.name;
    case Expr::Kind::Index: return e.name + "[" + exprToString(*e.lhs) + "]";
    case Expr::Kind::Call: {
      std::string s = "(call " + e.name;
      for (const ExprPtr& a : e.args) s += " " + exprToString(*a);
      return s + ")";
    }
    case Expr::Kind::Unary:
      return std::string("(") + tokenKindName(e.op) + " " + exprToString(*e.lhs) + ")";
    case Expr::Kind::Binary:
      return std::string("(") + tokenKindName(e.op) + " " + exprToString(*e.lhs) + " " +
             exprToString(*e.rhs) + ")";
  }
  return "?";
}

static void printStmt(const Stmt& s, std::ostream& out, int depth) {
  std::string pad(depth * 2, ' ');
  switch (s.kind) {
    case Stmt::Kind::VarDecl:
      out << pad << "VarDecl " << typeName(s.declType) << " " << s.name;
      if (s.value) out << " = " << exprToString(*s.value);
      out << "\n";
      break;
    case Stmt::Kind::Assign:
      out << pad << "Assign " << s.name;
      if (s.index) out << "[" << exprToString(*s.index) << "]";
      out << " = " << exprToString(*s.value) << "\n";
      break;
    case Stmt::Kind::If:
      out << pad << "If " << exprToString(*s.cond) << "\n";
      printStmt(*s.body, out, depth + 1);
      if (s.elseBody) {
        out << pad << "Else\n";
        printStmt(*s.elseBody, out, depth + 1);
      }
      break;
    case Stmt::Kind::While:
      out << pad << "While " << exprToString(*s.cond) << "\n";
      printStmt(*s.body, out, depth + 1);
      break;
    case Stmt::Kind::Return:
      out << pad << "Return";
      if (s.value) out << " " << exprToString(*s.value);
      out << "\n";
      break;
    case Stmt::Kind::Block:
      out << pad << "Block\n";
      for (const StmtPtr& c : s.stmts) printStmt(*c, out, depth + 1);
      break;
    case Stmt::Kind::ExprStmt:
      out << pad << "ExprStmt " << exprToString(*s.value) << "\n";
      break;
  }
}

void printProgram(const Program& p, std::ostream& out) {
  for (const Function& f : p.functions) {
    out << "Function " << f.name << "(";
    for (size_t i = 0; i < f.params.size(); i++) {
      const Param& prm = f.params[i];
      if (i) out << ", ";
      out << typeName(prm.type) << " " << prm.name << (prm.isArray ? "[]" : "");
    }
    out << ") -> " << typeName(f.returnType) << "\n";
    printStmt(*f.body, out, 1);
  }
}

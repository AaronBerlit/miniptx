// Recursive-descent parser: one method per grammar rule in docs/grammar.md.
#include "parser.h"

#include <climits>
#include <string>

#include "diagnostics.h"

namespace {

class Parser {
 public:
  explicit Parser(const std::vector<Token>& toks) : toks_(toks) {}

  // program = { function } EOF
  Program program() {
    Program p;
    while (!check(TokenKind::Eof)) p.functions.push_back(function());
    return p;
  }

  // Entry point for parseExpression(): an expr followed by nothing else.
  ExprPtr standaloneExpr() {
    ExprPtr e = expr();
    expect(TokenKind::Eof, "end of expression");
    return e;
  }

 private:
  const std::vector<Token>& toks_;
  size_t pos_ = 0;

  // ---- Token helpers --------------------------------------------------------
  const Token& peek(size_t ahead = 0) const {
    size_t i = pos_ + ahead;
    return i < toks_.size() ? toks_[i] : toks_.back();  // back() is always Eof
  }
  bool check(TokenKind k) const { return peek().kind == k; }
  const Token& advance() {
    const Token& t = peek();
    if (t.kind != TokenKind::Eof) pos_++;
    return t;
  }
  // If the next token is k, consume it and return true.
  bool match(TokenKind k) {
    if (!check(k)) return false;
    advance();
    return true;
  }
  // The next token *must* be k; otherwise it's a syntax error.
  const Token& expect(TokenKind k, const std::string& context) {
    if (check(k)) return advance();
    error(std::string("expected '") + tokenKindName(k) + "' " + context);
  }

  [[noreturn]] void error(const std::string& msg) const {
    const Token& t = peek();
    std::string found = t.kind == TokenKind::Eof ? "end of file" : "'" + t.text + "'";
    throw CompileError(msg + ", found " + found, t.line, t.col);
  }

  static ExprPtr newExpr(Expr::Kind k, const Token& at) {
    auto e = std::make_unique<Expr>();
    e->kind = k;
    e->line = at.line;
    e->col = at.col;
    return e;
  }
  static StmtPtr newStmt(Stmt::Kind k, const Token& at) {
    auto s = std::make_unique<Stmt>();
    s->kind = k;
    s->line = at.line;
    s->col = at.col;
    return s;
  }

  // ---- Declarations ---------------------------------------------------------

  // type = "int" | "float"
  bool atType() const { return check(TokenKind::KwInt) || check(TokenKind::KwFloat); }
  BaseType type() {
    if (match(TokenKind::KwInt)) return BaseType::Int;
    if (match(TokenKind::KwFloat)) return BaseType::Float;
    error("expected a type ('int' or 'float')");
  }

  // function = "func" IDENT "(" [ param { "," param } ] ")" [ "->" type ] block
  Function function() {
    Function f;
    const Token& kw = expect(TokenKind::KwFunc, "to start a function");
    f.line = kw.line;
    f.col = kw.col;
    f.name = expect(TokenKind::Ident, "after 'func' (function name)").text;
    expect(TokenKind::LParen, "after function name");
    if (!check(TokenKind::RParen)) {
      do f.params.push_back(param());
      while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "after parameters");
    if (match(TokenKind::Arrow)) f.returnType = type();
    f.body = block();
    return f;
  }

  // param = type IDENT [ "[" "]" ]
  Param param() {
    const Token& start = peek();
    Param p;
    p.type = type();
    p.name = expect(TokenKind::Ident, "for parameter name").text;
    p.isArray = false;
    if (match(TokenKind::LBracket)) {
      expect(TokenKind::RBracket, "after '[' in array parameter");
      p.isArray = true;
    }
    p.line = start.line;
    p.col = start.col;
    return p;
  }

  // ---- Statements -----------------------------------------------------------

  // block = "{" { statement } "}"
  StmtPtr block() {
    StmtPtr b = newStmt(Stmt::Kind::Block, expect(TokenKind::LBrace, "to start a block"));
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) b->stmts.push_back(statement());
    expect(TokenKind::RBrace, "to close the block");
    return b;
  }

  StmtPtr statement() {
    if (atType()) return varDecl();
    if (check(TokenKind::KwIf)) return ifStmt();
    if (check(TokenKind::KwWhile)) return whileStmt();
    if (check(TokenKind::KwReturn)) return returnStmt();
    if (check(TokenKind::LBrace)) return block();
    return assignOrExprStmt();
  }

  // varDecl = type IDENT [ "=" expr ] ";"
  StmtPtr varDecl() {
    StmtPtr s = newStmt(Stmt::Kind::VarDecl, peek());
    s->declType = type();
    s->name = expect(TokenKind::Ident, "for variable name").text;
    if (check(TokenKind::LBracket))
      error("local arrays are not supported (pass arrays as function parameters)");
    if (match(TokenKind::Assign)) s->value = expr();
    expect(TokenKind::Semicolon, "after variable declaration");
    return s;
  }

  // "if" "(" expr ")" block [ "else" ( ifStmt | block ) ]
  StmtPtr ifStmt() {
    StmtPtr s = newStmt(Stmt::Kind::If, advance());
    expect(TokenKind::LParen, "after 'if'");
    s->cond = expr();
    expect(TokenKind::RParen, "after if condition");
    s->body = block();
    if (match(TokenKind::KwElse)) {
      // "else if" is just an else whose body is another if statement.
      s->elseBody = check(TokenKind::KwIf) ? ifStmt() : block();
    }
    return s;
  }

  // "while" "(" expr ")" block
  StmtPtr whileStmt() {
    StmtPtr s = newStmt(Stmt::Kind::While, advance());
    expect(TokenKind::LParen, "after 'while'");
    s->cond = expr();
    expect(TokenKind::RParen, "after while condition");
    s->body = block();
    return s;
  }

  // "return" [ expr ] ";"
  StmtPtr returnStmt() {
    StmtPtr s = newStmt(Stmt::Kind::Return, advance());
    if (!check(TokenKind::Semicolon)) s->value = expr();
    expect(TokenKind::Semicolon, "after return");
    return s;
  }

  // Assignment and expression statements both start with an expression, so we
  // can't tell them apart from the first token. Trick: parse an expression,
  // and if '=' follows, check that what we parsed is something assignable
  // (a variable or an array element) and turn it into an Assign.
  StmtPtr assignOrExprStmt() {
    const Token& start = peek();
    ExprPtr e = expr();
    if (check(TokenKind::Assign)) {
      if (e->kind != Expr::Kind::Var && e->kind != Expr::Kind::Index)
        throw CompileError("left side of '=' must be a variable or array element",
                           start.line, start.col);
      advance();  // '='
      StmtPtr s = newStmt(Stmt::Kind::Assign, start);
      s->name = e->name;
      if (e->kind == Expr::Kind::Index) s->index = std::move(e->lhs);
      s->value = expr();
      expect(TokenKind::Semicolon, "after assignment");
      return s;
    }
    StmtPtr s = newStmt(Stmt::Kind::ExprStmt, start);
    s->value = std::move(e);
    expect(TokenKind::Semicolon, "after expression");
    return s;
  }

  // ---- Expressions (loosest precedence first) -------------------------------

  static bool isCompareOp(TokenKind k) {
    return k == TokenKind::Lt || k == TokenKind::Gt || k == TokenKind::Le ||
           k == TokenKind::Ge || k == TokenKind::Eq || k == TokenKind::Ne;
  }

  static ExprPtr binary(const Token& opTok, ExprPtr l, ExprPtr r) {
    ExprPtr e = newExpr(Expr::Kind::Binary, opTok);
    e->op = opTok.kind;
    e->lhs = std::move(l);
    e->rhs = std::move(r);
    return e;
  }

  // expr = additive [ compareOp additive ]
  // At most ONE comparison: that's what makes "a < b < c" an error.
  ExprPtr expr() {
    ExprPtr left = additive();
    if (isCompareOp(peek().kind)) {
      const Token& op = advance();
      left = binary(op, std::move(left), additive());
      if (isCompareOp(peek().kind)) error("comparisons cannot be chained");
    }
    return left;
  }

  // additive = term { ("+" | "-") term }
  // The loop (not recursion) is what makes it LEFT-associative:
  // 1 - 2 - 3 builds (1 - 2) first, then uses that as the left side of "- 3".
  ExprPtr additive() {
    ExprPtr left = term();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
      const Token& op = advance();
      left = binary(op, std::move(left), term());
    }
    return left;
  }

  // term = unary { ("*" | "/") unary }
  ExprPtr term() {
    ExprPtr left = unary();
    while (check(TokenKind::Star) || check(TokenKind::Slash)) {
      const Token& op = advance();
      left = binary(op, std::move(left), unary());
    }
    return left;
  }

  // unary = "-" unary | primary
  ExprPtr unary() {
    if (check(TokenKind::Minus)) {
      const Token& op = advance();
      ExprPtr e = newExpr(Expr::Kind::Unary, op);
      e->op = op.kind;
      e->lhs = unary();
      return e;
    }
    return primary();
  }

  ExprPtr primary() {
    const Token& t = peek();
    switch (t.kind) {
      case TokenKind::IntLit: {
        advance();
        ExprPtr e = newExpr(Expr::Kind::IntLit, t);
        // Tiny ints are 32-bit (PTX .s32). stoll can't overflow here because
        // the lexer only gives us digits, but a long digit string could.
        if (t.text.size() > 10 || std::stoll(t.text) > INT_MAX)
          throw CompileError("integer literal too large", t.line, t.col);
        e->intValue = std::stoll(t.text);
        return e;
      }
      case TokenKind::FloatLit: {
        advance();
        ExprPtr e = newExpr(Expr::Kind::FloatLit, t);
        e->floatValue = std::stod(t.text);
        return e;
      }
      case TokenKind::KwTid:
        advance();
        return newExpr(Expr::Kind::Tid, t);
      case TokenKind::LParen: {
        advance();
        ExprPtr e = expr();
        expect(TokenKind::RParen, "to close '('");
        return e;  // no node for parentheses: the tree shape already says it all
      }
      case TokenKind::Ident: {
        advance();
        if (match(TokenKind::LBracket)) {  // a[i]
          ExprPtr e = newExpr(Expr::Kind::Index, t);
          e->name = t.text;
          e->lhs = expr();
          expect(TokenKind::RBracket, "after array index");
          return e;
        }
        if (match(TokenKind::LParen)) {  // f(x, y)
          ExprPtr e = newExpr(Expr::Kind::Call, t);
          e->name = t.text;
          if (!check(TokenKind::RParen)) {
            do e->args.push_back(expr());
            while (match(TokenKind::Comma));
          }
          expect(TokenKind::RParen, "after call arguments");
          return e;
        }
        ExprPtr e = newExpr(Expr::Kind::Var, t);
        e->name = t.text;
        return e;
      }
      default:
        error("expected an expression");
    }
  }
};

}  // namespace

Program parseProgram(const std::vector<Token>& tokens) { return Parser(tokens).program(); }

ExprPtr parseExpression(const std::vector<Token>& tokens) {
  return Parser(tokens).standaloneExpr();
}

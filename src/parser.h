#pragma once
#include <vector>

#include "ast.h"
#include "lexer.h"

// Builds an AST from tokens following docs/grammar.md.
// Throws CompileError (with position) on the first syntax error.
Program parseProgram(const std::vector<Token>& tokens);

// Parses a single expression (must use up all tokens). Handy for tests.
ExprPtr parseExpression(const std::vector<Token>& tokens);

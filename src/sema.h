#pragma once
#include "ast.h"

// Semantic analysis: checks the program *means* something valid, and annotates
// the AST for code generation:
//   - every Expr gets its `type`
//   - every variable use/declaration gets a `varId` (index into Function::vars)
// Throws CompileError on the first problem found.
void analyze(Program& program);

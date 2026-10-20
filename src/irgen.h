#pragma once
#include "ast.h"
#include "ir.h"

// Lowers a checked AST (after analyze()) into PTX-like IR with virtual registers.
IrModule generateIr(const Program& program);

#pragma once
#include <string>

#include "ir.h"

// Parses .ptx-style text (as written by emitPtx) back into IR, so the
// interpreter can run a compiled file without the original source.
// Throws CompileError with the line number on malformed input.
IrModule readPtx(const std::string& text);

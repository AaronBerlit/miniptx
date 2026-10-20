#pragma once
#include <string>

#include "ir.h"

// The final compiler stage: turns IR into the text of a .ptx-style file.
std::string emitPtx(const IrModule& module);

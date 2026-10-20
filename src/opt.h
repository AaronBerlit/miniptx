#pragma once
#include "ir.h"

// Each pass returns true if it changed anything. optimize() repeats them until
// nothing changes, because one pass often creates work for another: folding
// makes values unused, which dead-code removal then deletes.

// Constant folding + propagation: computes constant expressions at compile time.
//   mov %r1, 2;  add %r2, %r1, 3;    ->    mov %r1, 2;  mov %r2, 5;
// Also resolves branches whose condition is a known constant.
bool foldConstants(IrFunction& f);

// Unreachable code: deletes instructions no path can reach (e.g. after a ret),
// jumps to the very next instruction, and labels nothing jumps to.
bool removeUnreachable(IrFunction& f);

// Dead values: deletes instructions whose result register is never read.
bool removeDeadValues(IrFunction& f);

void optimize(IrModule& m);

// Instructions in the function, not counting labels (they're not executed).
int instructionCount(const IrFunction& f);
int instructionCount(const IrModule& m);

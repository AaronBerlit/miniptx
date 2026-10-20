#pragma once
#include "ir.h"

// Linear-scan register allocation: renumbers the virtual registers so that
// registers whose values are never alive at the same time share one number.
// Done separately for each class (%r, %f, %p), since they're separate files.
void allocateRegisters(IrFunction& f);
void allocateRegisters(IrModule& m);

// Number of distinct registers actually used (all classes), for --stats.
int registerCount(const IrFunction& f);
int registerCount(const IrModule& m);

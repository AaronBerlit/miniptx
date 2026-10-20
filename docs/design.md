# Design Notes

The main decisions in MiniPTX, why each was made, and what it costs.

## Front end

**Hand-written recursive-descent parser.** Each grammar rule in
[grammar.md](grammar.md) is one method. Precedence comes from the structure of
the grammar: each level calls the next-tighter one. Left associativity comes
from loops rather than recursion. This approach is easy to read, gives precise
error messages, and is the technique production compilers like Clang and GCC
use.

**Error positions point at the token that broke the rule.** A missing `;` is
reported at the *next* token, because that's where the parser finds out. GCC
and Clang do the same.

**One `Expr` and one `Stmt` struct with a `kind` tag**, instead of a class
hierarchy with virtual visitors. Later stages are a `switch (e.kind)`. It's
less type-safe, but much shorter and easier to follow for a language this size.

**Semantic analysis resolves names to `varId`s.** Scopes are a stack of
name→id maps. Every use of a variable is stamped with the ID of the declaration
it refers to, so shadowed variables (two different `v`s) become different IDs.
After this, no later stage deals with names or scopes.

**Strict typing: no implicit int/float conversion.** PTX instructions carry
their type (`add.s32` vs `add.f32`), so the code generator needs exact types
anyway. Allowing mixed types would need inserted `cvt` instructions. That's
possible, but it's extra machinery for little teaching value.

**Comparisons have type `Bool` and may only appear as conditions.** This
mirrors PTX exactly: `setp` writes a predicate register, and only a guarded
instruction (`@%p bra`) reads one. The restriction means the compiler never has
to turn a predicate back into an integer, which would need `selp`.

**The "missing return" check is conservative.** A `while` loop never counts as
"always returns", even `while (1 > 0) { return 1; }`. Rejecting a few valid
programs is better than accepting an invalid one.

## IR

**PTX-shaped, flat, with unlimited virtual registers.** The IR *is* the output
format. Printing it gives the `.ptx` text. That way the optimizer and register
allocator work on exactly what gets emitted.

**Not SSA. Each variable owns one register for its whole life**, and
assignment overwrites it. SSA (one assignment per register, with φ-nodes at
merge points) makes many optimizations simpler, but it needs dominance analysis
to build and a φ-removal step afterwards. The PRD excludes it, and the
non-SSA form matches hand-written PTX more closely. The cost: constant
propagation must forget what it knows at every label (see below).

**Target hints in the code generator.** `genExpr(e, &target)` lets the *last*
instruction of an expression write straight into the destination variable, so
`i = i + 1` becomes `add %r1, %r1, 1` instead of an add into a temporary plus a
`mov`. This is safe because only the final instruction writes the target, after
every operand has been read.

**Conditions branch on *false*.** `@!%p bra L_end` skips the body, so the body
follows in source order. `if`/`else` needs an explicit `bra L_endif` after the
"then" part. Forgetting that jump is the classic bug here, and a test checks it.

**Uninitialized variables are explicitly zeroed.** `int x;` generates
`mov %r, 0`. Before register allocation existed, this happened to work because
the interpreter zeroes every register. After allocation, registers are shared,
so an implicit zero would read another variable's leftover value.

## Optimizer

**Block-local constant propagation.** Constants are tracked forward through
straight-line code and forgotten at labels, because a label can be reached from
somewhere else, such as a loop back-edge. Knowing constants *across* labels
would need a dataflow analysis or SSA.

**Folding must match run-time semantics exactly**: 32-bit wraparound, and
float math in single precision (`9.0 / 5.0` folds to `1.79999995`, the real
`f32` value). Division by zero and results that would be `inf`/`nan` are not
folded, so run-time behaviour is unchanged.

**Dead code removal keeps instructions that can fail.** `ld` (bad index) and
integer `div` (divide by zero) are kept even when their result is unused.
Deleting them would make a crashing program appear to work. Calls are kept
because they may write to arrays. Only an unused *result* of a call is dropped.

**The passes run until nothing changes**, because each one creates work for
the others: folding makes values dead, and a folded branch makes code
unreachable.

**Every end-to-end test runs both optimized and unoptimized.** This is the
main safety net. An optimization that changes any answer fails the suite.

## Register allocation

**Liveness by iterative backward dataflow** over a per-instruction control-flow
graph. A single backward pass is not enough with loops: the back-edge makes a
value used at the loop header live through the whole loop body. The regression
test for this is `regalloc_respects_loop_back_edges`.

**Two positions per instruction** (read at 2k, write at 2k+1). The result of
an instruction can then reuse the register of an operand that is being read for
the last time, giving the `add %r1, %r1, %r2` pattern.

**Linear scan with no spilling.** Like real PTX, there is no register limit at
this level (`ptxas` applies the hardware limit later). With unlimited
registers, allocating intervals greedily by start position is optimal. Each
register class (`%r`, `%f`, `%p`) is allocated separately. Parameters are
treated as defined just before the first instruction, so they never share with
each other.

## Back end and interpreter

**The interpreter runs the emitted text.** `--run` goes through
`emit → ptxreader → interpret`, so anything missing from the `.ptx` file breaks
the tests. A round-trip test checks that emit → read → print is byte-for-byte
identical.

**The interpreter is defensive.** It bounds-checks every `ld`/`st`, reports
integer division by zero, wraps 32-bit arithmetic (done in 64 bits and then
truncated, because signed overflow is undefined in C++), and has a step limit
and a recursion-depth limit so broken programs fail cleanly instead of hanging.

**Threads run one after another.** `--threads N` runs the function N times
with `tid = 0..N-1`. That gives the same results as parallel execution for the
example kernels, because each thread writes a different element. It does not
model races or warps.

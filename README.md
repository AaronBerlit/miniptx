# miniptx

[![CI](https://github.com/AaronBerlit/miniptx/actions/workflows/ci.yml/badge.svg)](https://github.com/AaronBerlit/miniptx/actions/workflows/ci.yml)

A small compiler for a toy C-like language ("Tiny") that outputs PTX-style
assembly, plus an interpreter so the output can actually be run and checked.

I wanted to understand how a compiler works end to end, and specifically what
PTX is: the virtual ISA that CUDA compiles to before `ptxas` turns it into real
GPU machine code. So the output here has the same flavor as PTX (typed
instructions, unlimited virtual registers, predicates, guarded branches), but
it's not valid PTX. See [the differences](#differences-from-real-ptx) at the end.

![demo](docs/demo.gif)

## Example

`examples/vec_add.tiny`:

```c
func vec_add(float a[], float b[], float c[], int n) {
    int i = 0;
    while (i < n) {
        c[i] = a[i] + b[i];
        i = i + 1;
    }
}
```

`./build/miniptx examples/vec_add.tiny` (I added the comments):

```
.func vec_add(.f32 a[], .f32 b[], .f32 c[], .s32 %r1)
{
    mov.s32     %r2, 0;
L_while1:
    setp.lt.s32 %p1, %r2, %r1;      // %p1 = i < n
    @!%p1 bra   L_endwhile1;
    ld.f32      %f1, [a + %r2];
    ld.f32      %f2, [b + %r2];
    add.f32     %f1, %f1, %f2;      // reuses %f1 after register allocation
    st.f32      [c + %r2], %f1;
    add.s32     %r2, %r2, 1;
    bra         L_while1;
L_endwhile1:
    ret;
}
```

## Building

Needs CMake 3.16+ and gcc or clang with C++17. No other dependencies.

```bash
cmake -S . -B build
cmake --build build
./build/unit_tests
```

There's also an ASan/UBSan build: add `-DMINIPTX_ASAN=ON` and use a separate
build dir.

## Running it

```bash
# print the output (or write it with -o out.ptx)
./build/miniptx examples/vec_add.tiny

# compile and run a function; arrays are written like [1,2,3]
./build/miniptx examples/vec_add.tiny --run vec_add [1,2,3] [10,20,30] [0,0,0] 3

# "launch" a kernel on N threads (tid goes 0..N-1, run one after another)
./build/miniptx examples/saxpy.tiny --threads 5 --run saxpy 2.0 [1,2,3,4] [10,10,10,10] 4

# run a .ptx file that was already compiled
./build/miniptx out.ptx --run vec_add [1,2] [3,4] [0,0] 2
```

Useful flags for poking at the internals: `--dump-tokens`, `--dump-ast`,
`--dump-ir` (before optimization), `--no-opt`, and `--stats`.

## How it's put together

```
lexer -> parser -> sema -> irgen -> optimizer -> register allocator -> emitter
                                                                          |
                                         interpreter <- ptx reader <------+
```

Each stage is its own file in `src/`. One choice I'm happy with: `--run`
doesn't execute the compiler's in-memory IR. It writes the `.ptx` text, parses
it back in, and runs that. So if the emitter ever drops something, the tests
break.

The language is small on purpose: `int`, `float`, arrays (only as function
parameters), `+ - * /`, comparisons, `if`/`else`, `while`, functions, and a
`tid` builtin. Grammar is in [docs/grammar.md](docs/grammar.md). Two rules that
come straight from PTX:

- int and float never mix implicitly, since `add.s32` and `add.f32` are
  different instructions
- a comparison can only be used as an `if`/`while` condition. It becomes `setp`
  plus a guarded `bra`, so a predicate never needs converting back to a number

The reasoning behind the bigger decisions (no SSA, how the optimizer stays
correct, etc.) is in [docs/design.md](docs/design.md).

## Optimizations

- **Constant folding.** Propagates constants within straight-line code and
  folds them, using the same 32-bit wraparound and float rounding as run time.
  It doesn't fold `x / 0` (that should still fail when run), and it resolves
  branches whose condition is known.
- **Dead code removal.** Unreachable code, jumps to the next instruction,
  unused labels, and instructions whose result is never read. `ld` and integer
  `div` stay even when unused, because they can fail at run time.
- **Register allocation.** Linear scan over live intervals. Liveness is
  computed iteratively, which matters for loops: a value used at the top of a
  loop is live through the whole body because of the back edge. This was the
  trickiest part to get right, and there's a test specifically for it.

Numbers from `--stats`:

| example | instructions | registers |
|---|---|---|
| sign | 11 → 7 | 4 → 2 |
| celsius | 9 → 8 | 7 → 4 |
| fib_recursive | 9 → 9 | 7 → 3 |
| is_prime | 16 → 16 | 9 → 4 |
| bubble_sort | 26 → 26 | 17 → 10 |
| saxpy | 9 → 9 | 8 → 5 |

Constant folding barely moves the instruction count on most of these. Normal
loop code just doesn't have many constant expressions, and irgen already writes
results straight into the destination register, so there aren't many extra
`mov`s to clean up. It helps when constants are written for readability, like
`9.0 / 5.0` in `celsius` (which folds to `1.79999995`, the actual f32 value).
What's left in something like `bubble_sort` (recomputing `n - 1` on every
iteration) would need loop-invariant code motion, which I haven't done.

Register allocation helps a lot more, and on a GPU that's the number that
matters for occupancy anyway.

## Tests

81 tests, using a tiny test harness in `tests/test.h` (no GoogleTest).

There are unit tests for each stage, but most of the confidence comes from the
end-to-end tests. Every `.tiny` file in `examples/` and `tests/e2e/` has its
expected output in comments:

```
// RUN: factorial 5
// EXPECT: return = 120
```

The test runner compiles each file, emits it, reads it back, runs it, and
compares. It does this **twice**, with and without optimizations, so an
optimization that changes a result fails the build. There are also tests for
compile errors and for runtime errors (out of bounds, divide by zero, infinite
loops, runaway recursion).

CI builds with gcc and clang on Linux and clang on macOS with `-Werror`, and
runs everything under ASan/UBSan too.

## Differences from real PTX

- memory is indexed by element (`[a + %r1]`), not by byte offset with 64-bit
  pointers
- arrays are named parameters, instead of pointers loaded with `ld.param` +
  `cvta`
- `ret %r1` returns a value directly; real PTX goes through `.param` space
- float immediates are written in decimal, not as hex bit patterns like
  `0f40200000`
- one `%tid.x`, and threads run one after another (no blocks, warps, or
  parallelism)

## Things I'd like to add

- a register limit with spilling (`--max-regs N`), which is closer to what
  `ptxas` actually deals with
- CSE and loop-invariant code motion
- byte addressing and `.param` space
- comparing against `nvcc -ptx` output for the same kernels

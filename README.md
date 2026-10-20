# MiniPTX

[![CI](https://github.com/AaronBerlit/miniptx/actions/workflows/ci.yml/badge.svg)](https://github.com/AaronBerlit/miniptx/actions/workflows/ci.yml)

A small optimizing compiler, written in C++17, for a tiny C-like language. It
targets a **PTX-like virtual instruction set** and includes an interpreter that
runs the output.

[PTX](https://docs.nvidia.com/cuda/parallel-thread-execution/) is NVIDIA's
portable GPU instruction set. CUDA compiles to PTX, and the driver's assembler
(`ptxas`) translates it into real machine code for a specific GPU. MiniPTX
builds the half of that story that comes first: a full compiler pipeline whose
output has the same shape as PTX. That means typed instructions, unlimited
virtual registers, predicates, and guarded branches.

![Demo: compiling a SAXPY kernel to PTX-like code and running it](docs/demo.gif)

**Input** (`examples/vec_add.tiny`):

```c
func vec_add(float a[], float b[], float c[], int n) {
    int i = 0;
    while (i < n) {
        c[i] = a[i] + b[i];
        i = i + 1;
    }
}
```

**Output** (`miniptx examples/vec_add.tiny`, with comments added):

```
.func vec_add(.f32 a[], .f32 b[], .f32 c[], .s32 %r1)
{
    mov.s32     %r2, 0;
L_while1:
    setp.lt.s32 %p1, %r2, %r1;      // %p1 = (i < n)
    @!%p1 bra   L_endwhile1;        // leave the loop when false
    ld.f32      %f1, [a + %r2];
    ld.f32      %f2, [b + %r2];
    add.f32     %f1, %f1, %f2;      // result reuses %f1 (register allocation)
    st.f32      [c + %r2], %f1;
    add.s32     %r2, %r2, 1;
    bra         L_while1;
L_endwhile1:
    ret;
}
```

## Pipeline

```
 source.tiny
     │
     ▼
 ┌─────────┐ tokens ┌────────┐  AST  ┌──────┐ typed AST ┌──────────────┐
 │  Lexer  ├───────►│ Parser ├──────►│ Sema ├──────────►│ IR generator │
 └─────────┘        └────────┘       └──────┘           └──────┬───────┘
  lexer.cpp         parser.cpp       sema.cpp            irgen.cpp│ virtual registers
                                                                 ▼
 ┌─────────────┐  text  ┌─────────┐       ┌────────────────┐  ┌──────────────────────┐
 │ Interpreter │◄───────┤ Emitter │◄──────┤ Register alloc │◄─┤ Optimizer            │
 └─────────────┘ (.ptx) └─────────┘       │ (linear scan)  │  │ const fold, dead code│
  interp.cpp +          emit.cpp          └────────────────┘  └──────────────────────┘
  ptxreader.cpp                            regalloc.cpp        opt.cpp
```

Each stage lives in its own file and can be inspected with a flag
(`--dump-tokens`, `--dump-ast`, `--dump-ir`). The interpreter runs the
**emitted text**, not the compiler's in-memory data. That proves the `.ptx`
file is complete on its own.

## Build

Requires CMake ≥ 3.16 and a C++17 compiler (gcc or clang). There are no other
dependencies.

```bash
cmake -S . -B build
cmake --build build
./build/unit_tests
```

AddressSanitizer + UBSan build:

```bash
cmake -S . -B build-asan -DMINIPTX_ASAN=ON && cmake --build build-asan && ./build-asan/unit_tests
```

## Usage

```bash
# Compile: print the PTX-like output, or write it to a file
./build/miniptx examples/vec_add.tiny
./build/miniptx examples/vec_add.tiny -o vec_add.ptx

# Compile and run a function. Args: ints, floats, arrays like [1,2,3]
./build/miniptx examples/vec_add.tiny --run vec_add [1,2,3] [10,20,30] [0,0,0] 3
#   a = [1, 2, 3]
#   b = [10, 20, 30]
#   c = [11, 22, 33]

# GPU-style launch: run the kernel once per thread, tid = 0..N-1
./build/miniptx examples/saxpy.tiny --threads 5 --run saxpy 2.0 [1,2,3,4] [10,10,10,10] 4
#   x = [1, 2, 3, 4]
#   y = [12, 14, 16, 18]

# Run an already-compiled file
./build/miniptx vec_add.ptx --run vec_add [1,2] [3,4] [0,0] 2

# Inspect stages and optimization effects
./build/miniptx --dump-ast examples/factorial.tiny
./build/miniptx --no-opt examples/sign.tiny
./build/miniptx --stats examples/is_prime.tiny
#   instructions: 16 -> 16 (0% fewer)
#   registers:    9 -> 4 (55% fewer)
```

## The Tiny language

`int` and `float`, arrays (as function parameters), `+ - * /`, comparisons,
`if`/`else`, `while`, functions with recursion, and a built-in thread id `tid`.
The full grammar is in [docs/grammar.md](docs/grammar.md). Some deliberate
choices:

- **No implicit int↔float conversion.** Mixing them is a type error. PTX has
  separate `.s32` and `.f32` instructions, so the compiler must always know
  which one to use.
- **Comparisons can only be `if`/`while` conditions.** A comparison compiles to
  `setp` (which writes a predicate) followed by `@%p bra`. Restricting where a
  comparison can appear means a predicate never has to be turned back into a
  number.

## Optimizations

| Pass | What it does |
|---|---|
| **Constant folding** | Propagates known constants forward and computes constant expressions at compile time, with the same 32-bit wraparound and single-precision rounding as run time. It never folds division by zero, which must still fail at run time. Branches on a known condition are resolved. |
| **Dead code removal** | Deletes code that can't be reached (by walking the control-flow graph), jumps to the next instruction, unused labels, and instructions whose result is never read. `ld` and integer `div` are kept even when their result is unused, because they can fail. |
| **Register allocation** | Linear scan over live intervals from iterative liveness analysis. A result can reuse a register that is being read for the last time. Copies that end up as `mov %r1, %r1` are removed. |

Results on the example programs (`--stats`):

| Example | Instructions | Registers |
|---|---|---|
| sign | 11 → 7 (−36%) | 4 → 2 (−50%) |
| celsius | 9 → 8 (−11%) | 7 → 4 (−42%) |
| fib_recursive | 9 → 9 | 7 → 3 (−57%) |
| is_prime | 16 → 16 | 9 → 4 (−55%) |
| bubble_sort | 26 → 26 | 17 → 10 (−41%) |
| saxpy | 9 → 9 | 8 → 5 (−37%) |
| factorial, fibonacci, sum | unchanged | unchanged |

**Why constant folding rarely changes instruction counts here.** Hand-written
loops contain few constant expressions, and the IR generator already avoids
most redundant copies (it computes a result directly into the destination
variable's register). Folding pays off when code uses constants for
readability, as in `celsius`, where `9.0 / 5.0` becomes `1.79999995`: the exact
`f32` value, not 1.8. The waste that remains, such as `bubble_sort`
recomputing `n - 1` on every iteration, would need loop-invariant code motion
or common-subexpression elimination. Those are outside this project's scope.

**Register allocation is the bigger win.** On a GPU, fewer registers per
thread means more threads can be resident at once (occupancy).

## Testing

**81 tests**, run by a small built-in framework (`./build/unit_tests`):

- **Unit tests** for each stage: lexer, parser (precedence, associativity,
  error positions), semantic checks, IR generation, PTX reader, each
  optimization pass, and register allocation.
- **End-to-end tests:** every file in [examples/](examples) and
  [tests/e2e/](tests/e2e) is a test. Each one is compiled, emitted as text,
  read back, run, and compared with its expected output:

  ```
  // RUN: factorial 5
  // EXPECT: return = 120
  ```

  Every `RUN` is checked **both optimized and unoptimized**, so an
  optimization can never silently change a result.
- **Error tests:** compile errors (with line and column), plus run-time errors
  (out-of-bounds index, division by zero, infinite loops, runaway recursion).
- The whole suite also runs under **AddressSanitizer + UBSan**, and the build
  produces no warnings with `-Wall -Wextra -Wpedantic`.

## How this differs from real PTX

| MiniPTX | Real PTX |
|---|---|
| Memory is addressed by element index: `[a + %r1]` | Addresses are in bytes: `[%rd1 + 4]`, with 64-bit pointers |
| Arrays are named parameters | Pointers are loaded with `ld.param` and converted with `cvta` |
| `ret %r1` returns a value | Return values go through `.param` space with `st.param` |
| Float immediates are decimal: `2.5` | Float immediates are hex bit patterns: `0f40200000` |
| One `%tid.x`, threads run one after another | 3-D thread/block/grid IDs; warps run in parallel |

## Project layout

```
src/        lexer, parser, ast, sema, ir, irgen, opt, regalloc, emit, ptxreader, interp, driver, main
tests/      unit tests (test_*.cpp) and tests/e2e/*.tiny
examples/   14 sample programs (also end-to-end tests)
docs/       grammar.md, design.md
```

[docs/design.md](docs/design.md) explains the main design decisions and their
trade-offs.

## Future work

- Spilling under a register limit (`--max-regs N`), the way `ptxas` handles a register budget
- Common subexpression elimination and loop-invariant code motion
- Byte addressing and `.param` space, to get closer to real PTX
- Comparing output with real `nvcc -ptx` output for equivalent CUDA kernels

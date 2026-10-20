# PRD: MiniPTX — A Small Compiler That Targets a PTX-like Instruction Set

**Author:** (your name)
**Status:** Draft v1
**Language:** C++17
**Repo:** (add GitHub link)

---

## 1. Overview

MiniPTX is a small compiler written in C++. It reads programs written in a tiny C-like language ("Tiny") and produces code for a made-up virtual instruction set that looks like NVIDIA's PTX. A small interpreter then runs that output so we can check it works.

The compiler also includes a few simple optimizations.

## 2. Why this project exists

- **Learning goal:** Understand how a compiler works from start to finish (front end, optimizer, back end).
- **Career goal:** Show a PTX Compiler team that I understand what PTX is: a portable virtual instruction set that is translated to real GPU code later.
- **Portfolio goal:** Have a clean GitHub project with a README, tests, and clear design docs.

## 3. Goals and Non-Goals

### Goals
1. Compile a small C-like language into PTX-like instructions.
2. Run the output in a simple interpreter and get correct results.
3. Add three optimizations: constant folding, dead code removal, register allocation.
4. Have good tests and good documentation.

### Non-Goals (to keep scope small)
- No real GPU code generation or real PTX validity.
- No full C support (no pointers, structs, preprocessor, etc.).
- No advanced optimizations (loop unrolling, SSA, inlining).
- No linker or debugger in v1 (possible future work).

## 4. Target Users

- **Me:** to learn and to show in interviews.
- **Recruiters / interviewers:** they will skim the README and maybe run an example.
- **Other students:** may read the code to learn how a compiler is built.

## 5. The Tiny Language (input)

Keep it small so it can be finished.

**Features:**
- Types: `int` and `float`
- Variables and assignment
- Arithmetic: `+ - * /`
- Comparisons: `< > <= >= == !=`
- Control flow: `if / else`, `while`
- Arrays of `int` / `float` (for load and store instructions)
- Functions with parameters and a return value
- Special built-in `tid` (a fake "thread id") to give a GPU-like feel

**Example:**
```
func vec_add(int n) {
    int i = 0;
    while (i < n) {
        c[i] = a[i] + b[i];
        i = i + 1;
    }
}
```

## 6. The Output Instruction Set (PTX-like)

Virtual registers and simple instructions, similar in spirit to PTX.

| Instruction | Meaning |
|---|---|
| `mov` | copy a value into a register |
| `add`, `sub`, `mul`, `div` | arithmetic |
| `ld`, `st` | load from / store to memory |
| `setp` | compare and set a predicate (true/false) |
| `bra` | jump (can be conditional using a predicate) |
| `ret` | return from function |

Registers: `%r1, %r2...` for ints, `%f1, %f2...` for floats, `%p1...` for predicates.

**Example output:**
```
mov.s32   %r1, 0;
L_loop:
setp.lt.s32 %p1, %r1, %r2;
@!%p1 bra L_end;
ld.f32    %f1, [a + %r1];
ld.f32    %f2, [b + %r1];
add.f32   %f3, %f1, %f2;
st.f32    [c + %r1], %f3;
add.s32   %r1, %r1, 1;
bra       L_loop;
L_end:
ret;
```

## 7. System Design

```
Source code
   |
   v
[Lexer]      -> tokens
   |
[Parser]     -> AST (syntax tree)
   |
[Semantic check] -> types checked, errors reported
   |
[IR generator]   -> list of instructions with unlimited virtual registers
   |
[Optimizer]      -> constant folding, dead code removal
   |
[Register allocator] -> fewer registers used
   |
[Emitter]    -> PTX-like text file
   |
[Interpreter] -> runs the output, prints results
```

**Suggested folder layout:**
```
miniptx/
  src/        lexer, parser, ast, sema, irgen, opt, regalloc, emit, interp
  tests/      input programs + expected output
  examples/   sample programs
  docs/       design notes
  CMakeLists.txt
  README.md
  PRD.md
```

## 8. Functional Requirements

| ID | Requirement | Priority |
|---|---|---|
| F1 | Lexer turns source into tokens, reports bad characters with line numbers | Must |
| F2 | Parser builds an AST and reports syntax errors clearly | Must |
| F3 | Semantic check catches undeclared variables and type mismatches | Must |
| F4 | IR generator outputs PTX-like instructions for all language features | Must |
| F5 | Emitter writes a readable `.ptx`-style text file | Must |
| F6 | Interpreter runs the output and produces correct results | Must |
| F7 | Constant folding (`2 + 3` becomes `5`) | Must |
| F8 | Dead code removal (unused values and unreachable code) | Must |
| F9 | Register allocation (linear scan, reuse registers) | Should |
| F10 | `--dump-ast`, `--dump-ir`, and `--no-opt` command line flags | Should |
| F11 | Show before/after optimization stats (instruction count, register count) | Could |

## 9. Optimizations (details)

1. **Constant folding:** Compute constant expressions at compile time.
   `mov %r1, 2; add %r2, %r1, 3` becomes `mov %r2, 5`.
2. **Dead code removal:** Delete instructions whose result is never used, and code after a `ret` or jump that can never run.
3. **Register allocation (linear scan):** Work out how long each virtual register is alive. Reuse a register once its old value is no longer needed.

## 10. Non-Functional Requirements

- **Language/tools:** C++17, CMake build, no big external libraries (standard library only; GoogleTest optional).
- **Platform:** Linux and macOS (should build with gcc or clang).
- **Quality:** No compiler warnings with `-Wall -Wextra`. Check for memory bugs with AddressSanitizer.
- **Readability:** Each stage in its own file, short comments on tricky parts.

## 11. Testing Plan

- **Unit tests:** lexer tokens, parser output, each optimization pass.
- **End-to-end tests:** compile a program, run it in the interpreter, compare with expected output.
- **Optimization tests:** check that optimized and unoptimized versions give the same answer.
- **Error tests:** bad programs should give a clear error, not crash.
- **CI:** GitHub Actions to build and run tests on every push.

## 12. Success Metrics

- 10+ example programs compile and run correctly (e.g. vector add, sum, factorial, fibonacci, bubble sort).
- 30+ automated tests pass.
- Optimizations reduce instruction count on at least a few examples (show numbers in README).
- README has a build guide, an example, and a diagram.
- Repo has steady commits over several weeks.

## 13. Milestones

[uncertain] These time estimates are a guess. Adjust to your deadline.

| Week | Milestone |
|---|---|
| 1 | Set up repo, CMake, lexer, and tests |
| 2 | Parser and AST, `--dump-ast` |
| 3 | Semantic checks, IR generator for expressions and variables |
| 4 | Control flow (`if`, `while`), arrays, load/store, emitter |
| 5 | Interpreter, end-to-end tests, example programs |
| 6 | Constant folding and dead code removal |
| 7 | Register allocation, stats output |
| 8 | Polish: README, docs, CI, cleanup |

## 14. Risks and How to Handle Them

| Risk | Plan |
|---|---|
| Scope grows too big | Stick to the non-goals. Finish a working version first, then add more. |
| Parser gets stuck on tricky grammar | Keep the grammar small and write it down before coding. |
| Register allocation is hard | Do it last. The project is still good without it. |
| Running out of time | Priorities: F1 to F8 first. F9 to F11 are bonus. |

## 15. Future Work (after v1)

- Assembler and linker for object-like files
- Basic debug info (line numbers mapped to instructions)
- A tiny debugger using `ptrace`
- More optimizations (common subexpression elimination)
- Compare output with real PTX from `nvcc -ptx`

## 16. Learning Resources

- NVIDIA PTX ISA documentation (public)
- "Crafting Interpreters" by Robert Nystrom (free online)
- "Engineering a Compiler" by Cooper and Torczon
- LLVM Kaleidoscope tutorial (for ideas on structure)

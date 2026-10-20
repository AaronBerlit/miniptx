# Tiny Language Grammar

Notation: `{ x }` = zero or more `x`, `[ x ]` = optional `x`, `|` = or,
quoted text = an exact token. UPPERCASE = a token kind from the lexer.

```
program     = { function } EOF

function    = "func" IDENT "(" [ param { "," param } ] ")" [ "->" type ] block
param       = type IDENT [ "[" "]" ]          // "[]" makes it an array parameter
type        = "int" | "float"

block       = "{" { statement } "}"
statement   = varDecl
            | "if" "(" expr ")" block [ "else" ( ifStmt | block ) ]
            | "while" "(" expr ")" block
            | "return" [ expr ] ";"
            | block
            | IDENT [ "[" expr "]" ] "=" expr ";"     // assignment
            | expr ";"                                // e.g. a call: f(x);
varDecl     = type IDENT [ "=" expr ] ";"

expr        = additive [ compareOp additive ]
compareOp   = "<" | ">" | "<=" | ">=" | "==" | "!="
additive    = term { ( "+" | "-" ) term }
term        = unary { ( "*" | "/" ) unary }
unary       = "-" unary | primary
primary     = INTLIT | FLOATLIT | "tid"
            | IDENT                                   // variable
            | IDENT "[" expr "]"                      // array element
            | IDENT "(" [ expr { "," expr } ] ")"     // function call
            | "(" expr ")"
```

## Operator precedence (loosest to tightest)

| Level | Operators | Associativity |
|---|---|---|
| comparison | `< > <= >= == !=` | none (`a < b < c` is a syntax error) |
| additive | `+ -` | left (`a - b - c` = `(a - b) - c`) |
| term | `* /` | left |
| unary | `-` | right (`- - x` = `-(-x)`) |

Each precedence level is one grammar rule, and a rule only calls the rule for
the *next tighter* level. That's how the grammar itself encodes precedence:
in `a + b * c`, `additive` sees `a`, then `+`, then asks `term` for the right
side. `term` grabs all of `b * c` before returning.

## Deliberate limitations (keep scope small)

- Arrays exist only as function parameters (`float a[]`), never as locals.
  That matches PTX kernels, which receive arrays as pointer parameters.
- No `&&`, `||`, or `!`. Nest `if`s instead.
- No implicit int/float conversion. Mixing them is a type error (checked in
  semantic analysis, not here).
- `if`/`while` bodies always need braces. This avoids the "dangling else"
  ambiguity entirely.

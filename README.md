# LLFPL

**Low-Level Floating-Point Language** -- a deterministic expression language and
interpreter for numerical and signal work, written in C11.

LLFPL evaluates directly from mapped source text into a cache-line-aligned
register bank. It builds no syntax tree, allocates nothing while a program runs,
and never substitutes a value for a mathematically defined IEEE 754 result.

```
Require(stdlib.LLFPL)

Slab(PacketRing, 512)
Identity(SaturationLevel, 100)

Map(Condition, value, ClampToRange(value, Negate(SaturationLevel), SaturationLevel))

Commit(write_offset(PacketRing, 0, 250))
Commit(Condition(read_offset(PacketRing, 0)))
```

```
$ bin/llfpl program.LLFPL
250
100
```

---

## Contents

| Section | Summary |
| :------ | :------ |
| [1. Design rationale](#1-design-rationale) | The decisions the implementation rests on, and what each one rejects |
| [2. Building](#2-building) | Toolchain requirements, build configurations, and every make target |
| [3. Using the interpreter](#3-using-the-interpreter) | Command line options, output streams, exit status |
| [4. Language overview](#4-language-overview) | Directives, built-in forms, primitives, and Turing completeness |
| [5. Standard library](#5-standard-library) | Constants and templates shipped in `lib/llfpl` |
| [6. Project layout](#6-project-layout) | Directory structure and the rule that governs dependencies |
| [7. Testing](#7-testing) | The golden-file suite, what it covers, how to run it |
| [8. Embedding](#8-embedding) | Driving the runtime from C without the command line tool |
| [9. Documentation](#9-documentation) | Language specification and architecture notes |
| [10. Licence](#10-licence) | MIT |

---

## 1. Design rationale

**No allocation while a program runs.** All storage is reserved when a session
is created. Evaluating an expression, invoking a template, iterating a loop or
writing to an arena allocates nothing. An allocator on the evaluation path
contributes latency that varies with the state of the heap, and that variance is
not removable by tuning.

**No intermediate representation.** The evaluator reads atoms from a scanner and
produces values. A template body is a saved position in the source, not a tree,
so declaring one and invoking one both cost nothing but the read.

**Alignment is measured, not assumed.** The cache line size is probed at
start-up -- 64 bytes on x86-64, 128 on Apple silicon -- and every buffer is
aligned to the answer. A symbol record is exactly 64 bytes, checked by a static
assertion, so a lookup touches exactly one cache line.

**IEEE 754 without exception.** Division by zero yields a signed infinity. Zero
divided by zero yields a NaN. Nothing is silently replaced with a plausible
substitute, and no compiler flag that changes a computed result is used.

**Selection without branching.** `Branch` combines its arms arithmetically,
`selector * consequent + (1 - selector) * alternative`, with a selector
restricted to exactly `1.0` or `0.0`. The selection is exact, emits no
conditional jump, and cannot be mispredicted.

**Errors are reported, never absorbed.** An unresolved name, an out-of-range
arena offset, a fractional byte offset, a rebound identity and a name that
shadows a reserved verb are all diagnosed with a file, line and column. Nothing
silently evaluates to zero.

---

## 2. Building

Requires a C11 compiler and a POSIX host. Developed and tested with Clang on
macOS (AArch64); the sources use only C11 and POSIX.1-2008 interfaces, with the
Darwin, Linux and x86 probes selected at compile time.

```
make                 # release build into bin/llfpl
make test            # build and run the test suite
make debug           # assertions, frame pointers, no optimisation
make sanitize test   # rebuild under the sanitizers and run the suite
make format          # rewrite all sources with clang-format
make format-check    # fail if any source is not already formatted
make compile-commands # emit compile_commands.json for clangd
make install         # install under PREFIX, default /usr/local
make clean
```

`make install` places the binary in `$PREFIX/bin` and the standard library in
`$PREFIX/share/llfpl/lib`. The interpreter locates its library relative to its
own path, so an installation can be moved without reconfiguration.

---

## 3. Using the interpreter

```
llfpl [options] <source.LLFPL>
```

| Option | Effect |
| --- | --- |
| `-I`, `--module-path DIR` | Search `DIR` for required modules; repeatable, searched before the built-in locations |
| `-v`, `--verbose` | Report module loads, declarations and per-commit timings |
| `-s`, `--summary` | Print a summary of the run when it finishes |
| `--no-register-sync` | Do not publish the register bank to architectural registers |
| `--hardware` | Print the detected host profile and exit |
| `--language` | Print the language reference summary and exit |
| `-h`, `--help` | Print usage and exit |
| `-V`, `--version` | Print the version and exit |

Results go to standard output, one line per `Commit`. Diagnostics go to standard
error. The exit status is `0` when no error was reported, `1` when the program
ran and reported one, and `2` when the command line was malformed.

```
$ bin/llfpl --verbose --summary examples/newton_square_root.LLFPL
llfpl: trace: aarch64 host: 128 byte cache line (probed), 16 virtual registers
llfpl: trace: loading module .../examples/newton_square_root.LLFPL (2222 bytes)
llfpl: trace: loading module .../lib/llfpl/stdlib.LLFPL (4827 bytes)
llfpl: trace: bound identity PI
llfpl: trace: declared template Square with 1 parameter(s)
...
llfpl: trace: commit 2: 1.414213562373095 (cost 648, elapsed 19708 ns)
llfpl: 6 commit(s), reduction cost 684, 25333 ns evaluating, 2 module(s), 0 error(s)
```

---

## 4. Language overview

Five directives may appear at the top level.

```
Identity(Name, expression)          -- bind an immutable global
Slab(Name, byte_capacity)           -- reserve an aligned data arena
Map(Name, parameters..., body)      -- declare a reusable expression
Require(module/path.LLFPL)          -- load another module, exactly once
Commit(expression)                  -- evaluate and report a value
```

Four forms are built into expressions.

```
Branch(selector, consequent, alternative)      -- arithmetic selection
Loop(iteration_count, TemplateName)            -- bounded repetition
write_offset(ArenaName, byte_offset, value)    -- store, yielding the value
read_offset(ArenaName, byte_offset)            -- load
```

Eight primitives do the arithmetic.

```
plus  minus  multiply  divide  modulo  greater  less  equal
```

Every value is an IEEE 754 double. Truth is `1.0` and `0.0`, which is what makes
`Branch` exact. Comments run from `--` to the end of a line. Literals accept a
sign, a fractional part and an exponent: `-2.5e-2` is one literal.

`Loop`, an arena for unbounded state, and `Branch` for selection together make
the language Turing complete:

```
Slab(Counter, 64)
Map(Step, iteration, write_offset(Counter, 0, plus(read_offset(Counter, 0), 1)))

Commit(Loop(1000000, Step))
Commit(read_offset(Counter, 0))
```

The full specification is in [`docs/language-reference.md`](docs/language-reference.md).

---

## 5. Standard library

`lib/llfpl/stdlib.LLFPL` is found by name from anywhere:

```
Require(stdlib.LLFPL)
```

It provides mathematical constants correct to the last representable bit
(`PI`, `TAU`, `E`, `SQRT_TWO`, `EPSILON`, `LARGEST_FINITE`), sign and magnitude
helpers (`AbsoluteValue`, `Negate`, `SignOf`), selection (`Minimum`, `Maximum`,
`ClampToRange`), powers (`Square`, `Cube`, `Reciprocal`), interpolation and
angle conversion (`Interpolate`, `ToRadians`, `ToDegrees`), comparison
(`NearlyEqual`, `IsZero`, `IsNegative`, `IsPositive`), rounding (`Truncate`,
`FractionalPart`) and a kilobyte scratch arena.

Nothing in it is privileged; it is written in the same eight primitives and four
forms that a program has, and reading it is the shortest way to learn what the
language expresses.

---

## 6. Project layout

```
include/llfpl/       Public headers, one directory per layer
  core/              Limits, statuses, diagnostics, identifiers, paths, clock
  memory/            Aligned allocation, source mappings, data arenas
  hardware/          Host topology probe, virtual register file
  frontend/          Lexical scanner, template declarations
  runtime/           Symbol table, primitives, built-ins, evaluator
  interpreter/       Module loader, directives, session
  cli/               Command line options
src/                 Implementations, mirroring include/llfpl
lib/llfpl/           Standard library written in LLFPL
tests/               Golden-file suite and its runner
examples/            Worked programs
docs/                Language reference and architecture notes
```

Layers depend only on those above them. Where a lower layer needs a decision
only a higher one can make, the decision is injected rather than imported: the
frontend must reject a template named after a built-in, so it takes the
reserved-name rule as a function pointer instead of including the runtime.

---

## 7. Testing

```
make test
```

The suite runs every program in `tests/programs` and compares it against
`tests/expected`: standard output exactly, exit status exactly, and standard
error by required substrings, since a diagnostic carries host-specific paths.

It covers the primitives, numeric literals and round-trip printing, IEEE 754
edge cases, identities, templates, branchless selection, arena access, a
million-iteration loop, module resolution and cycles, the standard library,
expression nesting deeper than the physical register bank, and twelve
diagnostics.

`make sanitize test` runs the same suite under the address and
undefined-behaviour sanitizers with recovery disabled. Configuration selectors
combine with any other goal in a single invocation; switching between release,
debug and sanitized builds discards the stale object tree automatically.

---

## 8. Embedding

The runtime is a library with a command line tool on top. Embedding it takes
three calls:

```c
#include "llfpl/llfpl.h"

LlfplInterpreterOptions options;
LlfplInterpreterSession *session = NULL;

llfpl_interpreter_options_initialise(&options);
options.result_stream = destination;

if (llfpl_status_is_ok(llfpl_interpreter_session_create(&options, &session))) {
    llfpl_interpreter_session_execute_source_file(session, "program.LLFPL");
    llfpl_interpreter_session_destroy(session);
}
```

Results, diagnostics, verbosity, the module search path and register
synchronisation are all set through the options structure. Nothing in the
library writes to a stream it was not given.

---

## 9. Documentation

| Document | Contents |
| :------- | :------- |
| [`docs/language-reference.md`](docs/language-reference.md) | Complete specification: lexical structure, directives, built-in forms, primitives, name resolution, limits, grammar |
| [`docs/architecture.md`](docs/architecture.md) | Implementation design layer by layer, with the rejected alternative named for each decision |

Three things in the repository are authoritative in their own right and are kept
that way deliberately, so none of them can drift out of step with the prose:

| Source of truth | Answers |
| :-------------- | :------ |
| `llfpl --language` | The directives, built-ins and primitives actually compiled in, printed from the same dispatch tables the evaluator uses |
| [`include/llfpl/core/configuration_limits.h`](include/llfpl/core/configuration_limits.h) | Every static capacity, with the static assertions that enforce the layouts depending on them |
| [`lib/llfpl/stdlib.LLFPL`](lib/llfpl/stdlib.LLFPL) | What the language can express, written in the language itself |

---

## 10. Licence

MIT. See [`LICENSE`](LICENSE).

Copyright (c) 2026 Rahul Dange.

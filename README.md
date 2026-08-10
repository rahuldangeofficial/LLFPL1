# LLFPL

![Language](https://img.shields.io/badge/language-C11-00599C?style=flat-square)
![Platform](https://img.shields.io/badge/platform-POSIX.1--2008-4b5563?style=flat-square)
![Dependencies](https://img.shields.io/badge/dependencies-none-4b5563?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-1f6feb?style=flat-square)

Low-Level Floating-Point Language: a deterministic expression language and its
interpreter, written in C11 for numerical and signal processing work.

A program is a sequence of declarations. The interpreter memory-maps the source,
evaluates expressions directly from the token stream into a cache-line-aligned
register bank, and performs no heap allocation once a session has started.
Arithmetic is IEEE 754 binary64 throughout.

## Contents

1. [Overview](#1-overview)
2. [Requirements](#2-requirements)
3. [Building](#3-building)
4. [Usage](#4-usage)
5. [Language](#5-language)
6. [Standard library](#6-standard-library)
7. [Repository layout](#7-repository-layout)
8. [Testing](#8-testing)
9. [Embedding](#9-embedding)
10. [Documentation](#10-documentation)
11. [Licence](#11-licence)

---

## 1. Overview

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

### Implementation characteristics

| Aspect | Behaviour |
| :----- | :-------- |
| Allocation | All storage is reserved when a session is created. Expression evaluation, template invocation, loop iteration and arena access allocate nothing. |
| Alignment | The cache line size is probed at start-up (64 bytes on x86-64, 128 on Apple silicon) and used for every buffer. A symbol record is 64 bytes, enforced by static assertion, so a lookup touches one cache line. |
| Parsing | No syntax tree. A template body is recorded as a position in the mapped source and re-scanned on invocation. |
| Arithmetic | IEEE 754 binary64. Division by zero yields a signed infinity, zero over zero yields NaN. The build applies no flag that alters a computed result. |
| Selection | `Branch` computes `selector * consequent + (1 - selector) * alternative` with a selector of exactly `1.0` or `0.0`, emitting no conditional jump. |
| Output | Results print as the shortest decimal string that reparses to the same binary64 value. |
| Diagnostics | Failures report file, line and column. Evaluation stops at the first error. |
| Capacities | Every static limit is declared in one header, with static assertions covering the layouts that depend on it. |

---

## 2. Requirements

| Component | Requirement |
| :-------- | :---------- |
| Compiler | C11, with `_Static_assert` and inline assembly support |
| Platform | POSIX.1-2008 (`mmap`, `posix_memalign`, `clock_gettime`, `realpath`) |
| Link | `libm` |
| Optional | `clang-format` for the `format` targets |

Architecture-specific probes for Darwin, Linux and x86 are selected at compile
time; other targets fall back to documented defaults.

Verified on macOS 15 / AArch64 with Apple Clang 21.

---

## 3. Building

```
make
```

The binary is written to `bin/llfpl` and object files to `build/`.

| Target | Effect |
| :----- | :----- |
| `make` | Release build: `-O3`, link-time optimisation |
| `make test` | Build and run the test suite |
| `make debug` | `-O0 -g3`, frame pointers retained |
| `make sanitize test` | Rebuild under AddressSanitizer and UndefinedBehaviorSanitizer, then run the suite |
| `make format` | Rewrite all sources with `clang-format` |
| `make format-check` | Exit non-zero if any source is unformatted |
| `make compile-commands` | Write `compile_commands.json` for clangd and clang-tidy |
| `make install` | Install under `PREFIX` (default `/usr/local`) |
| `make uninstall` | Remove an installation from `PREFIX` |
| `make clean` | Delete `build/` and `bin/` |

Configuration selectors combine with other goals in one invocation. Switching
between release, debug and sanitized builds discards the stale object tree
automatically; an explicit `clean` is not required.

`make install` places the binary in `$PREFIX/bin` and the standard library in
`$PREFIX/share/llfpl/lib`. The interpreter resolves its library relative to its
own path, so an installation can be relocated without reconfiguration.

---

## 4. Usage

```
llfpl [options] <source.LLFPL>
```

| Option | Effect |
| :----- | :----- |
| `-I`, `--module-path DIR` | Search `DIR` for required modules. Repeatable; searched ahead of the default locations. |
| `-v`, `--verbose` | Report module loads, declarations and per-commit timings |
| `-s`, `--summary` | Print a summary of the run on completion |
| `--no-register-sync` | Skip publishing the register bank to architectural registers |
| `--hardware` | Print the detected host profile and exit |
| `--language` | Print the language reference summary and exit |
| `-h`, `--help` | Print usage and exit |
| `-V`, `--version` | Print the version and exit |

Results are written to standard output, one line per `Commit`. Diagnostics are
written to standard error.

| Exit status | Meaning |
| :---------- | :------ |
| `0` | The program ran and reported no error |
| `1` | The program ran and reported at least one error |
| `2` | The command line was malformed, or the runtime could not start |

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
1
1.414213562373095
...
```

A diagnostic carries the source location and states both the expectation and
what was found:

```
$ bin/llfpl program.LLFPL
llfpl: /path/to/program.LLFPL:7:16: error: expected ',' in a primitive application but found numeric literal
```

---

## 5. Language

Every value is an IEEE 754 double. Truth is represented by `1.0` and `0.0`.
Comments run from `--` to the end of the line. Numeric literals accept a sign, a
fractional part and an exponent, so `-2.5e-2` is a single literal.

### Directives

Only these may appear at the top level.

| Directive | Purpose |
| :-------- | :------ |
| `Identity(Name, expression)` | Bind an immutable global |
| `Slab(Name, byte_capacity)` | Reserve a cache-line-aligned data arena |
| `Map(Name, parameters..., body)` | Declare a reusable parameterised expression |
| `Require(module/path.LLFPL)` | Load another module, at most once per session |
| `Commit(expression)` | Evaluate an expression and report its value |

### Built-in expression forms

| Form | Purpose |
| :--- | :------ |
| `Branch(selector, consequent, alternative)` | Arithmetic selection; both arms are evaluated |
| `Loop(iteration_count, TemplateName)` | Bounded repetition, passing the iteration index |
| `write_offset(ArenaName, byte_offset, value)` | Store into an arena, yielding the stored value |
| `read_offset(ArenaName, byte_offset)` | Load from an arena |

### Primitives

| Primitive | Result |
| :-------- | :----- |
| `plus(a, b)` | Sum |
| `minus(a, b)` | Difference |
| `multiply(a, b)` | Product |
| `divide(a, b)` | Quotient under IEEE 754 |
| `modulo(a, b)` | Remainder, taking the sign of the dividend |
| `greater(a, b)` | `1.0` if a is greater than b, else `0.0` |
| `less(a, b)` | `1.0` if a is less than b, else `0.0` |
| `equal(a, b)` | `1.0` if a equals b, else `0.0` |

### Turing completeness

`Loop` supplies unbounded iteration, an arena supplies unbounded state, and
`Branch` supplies selection.

```
Slab(Counter, 64)
Map(Step, iteration, write_offset(Counter, 0, plus(read_offset(Counter, 0), 1)))

Commit(Loop(1000000, Step))
Commit(read_offset(Counter, 0))
```

### Limits

| Quantity | Limit |
| :------- | ----: |
| Identifier length | 47 characters |
| Identities per session | 1024 |
| Templates per session | 256 |
| Parameters per template | 8 |
| Arenas per session | 64 |
| Arena capacity | 1 GiB |
| Modules per session | 128 |
| Expression nesting depth | 512 |

Reaching a limit produces a diagnostic naming it. No limit is applied by silent
truncation.

The complete specification is
[`docs/language-reference.md`](docs/language-reference.md).

---

## 6. Standard library

```
Require(stdlib.LLFPL)
```

`lib/llfpl/stdlib.LLFPL` is located relative to the interpreter binary and needs
no path.

| Group | Members |
| :---- | :------ |
| Constants | `ZERO` `ONE` `TWO` `TEN` `PI` `TAU` `E` `SQRT_TWO` `LN_TWO` `HALF_PI` `DEGREES_PER_RADIAN` |
| Format limits | `EPSILON` `LARGEST_FINITE` `SMALLEST_NORMAL` |
| Sign and magnitude | `Negate` `AbsoluteValue` `SignOf` |
| Selection | `Minimum` `Maximum` `ClampToRange` |
| Powers | `Square` `Cube` `Reciprocal` |
| Interpolation | `Interpolate` `ToRadians` `ToDegrees` |
| Comparison | `IsNegative` `IsPositive` `IsZero` `NearlyEqual` |
| Rounding | `Truncate` `FractionalPart` |
| Storage | `ScratchArena` (1 KiB) with `StoreScratch` and `LoadScratch` |

The library is written in the same eight primitives and four built-in forms
available to any program.

---

## 7. Repository layout

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
lib/llfpl/           Standard library, written in LLFPL
tests/               Golden-file suite and its runner
examples/            Worked programs
docs/                Language reference and architecture notes
```

Each layer depends only on the ones listed above it. Where a lower layer
requires a decision belonging to a higher one, the decision is passed in: the
frontend rejects a template named after a built-in by applying a predicate
supplied by the caller, rather than including the runtime headers.

---

## 8. Testing

```
make test
```

Each program in `tests/programs` is executed and compared against
`tests/expected`: standard output and exit status exactly, standard error by
required substrings, since diagnostics carry host-specific paths.

Coverage: the primitives, numeric literals and round-trip printing, IEEE 754
edge cases, identity binding, template invocation, branchless selection, arena
access, a one-million-iteration loop, module resolution and `Require` cycles,
the standard library, expression nesting deeper than the physical register bank,
and twelve diagnostic paths.

```
make sanitize test
```

runs the same suite under AddressSanitizer and UndefinedBehaviorSanitizer with
error recovery disabled.

---

## 9. Embedding

The command line tool is a thin layer over a library. A session is created,
used, and destroyed:

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

Result and diagnostic streams, verbosity, the module search path and register
synchronisation are configured through `LlfplInterpreterOptions`. The library
writes to no stream it was not given.

`include/llfpl/llfpl.h` exposes the full interface. Individual headers are
self-contained and may be included on their own.

---

## 10. Documentation

| Document | Contents |
| :------- | :------- |
| [`docs/language-reference.md`](docs/language-reference.md) | Lexical structure, directives, built-in forms, primitives, name resolution, limits, grammar |
| [`docs/architecture.md`](docs/architecture.md) | Implementation design layer by layer, with the alternative rejected at each decision |

`llfpl --language` prints the directives, built-ins and primitives compiled into
the binary, from the same dispatch tables the evaluator uses.

---

## 11. Licence

MIT. See [`LICENSE`](LICENSE).

Copyright (c) 2026 Rahul Dange.

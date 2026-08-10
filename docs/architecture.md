# LLFPL Architecture

Version 1.0.0

This document explains how the interpreter is built and, more usefully, why each
part is built the way it is. Every decision recorded here was made against a
specific alternative, and the alternative is named.

---

## 1. Layering

Seven layers, each depending only on the ones above it in this list.

| Layer         | Directory                | Responsibility                                    |
| ------------- | ------------------------ | ------------------------------------------------- |
| `core`        | `*/core`                 | Limits, statuses, diagnostics, identifiers, paths, clock, numeric conversion |
| `memory`      | `*/memory`               | Aligned allocation, source mappings, data arenas   |
| `hardware`    | `*/hardware`             | Host topology probe, virtual register file         |
| `frontend`    | `*/frontend`             | Lexical scanner, template declarations             |
| `runtime`     | `*/runtime`              | Symbol table, primitives, built-ins, evaluator      |
| `interpreter` | `*/interpreter`          | Module loader, directives, session composition      |
| `cli`         | `*/cli`                  | Option parsing, entry point                         |

The dependency direction is enforced by inspection of the include graph and is
never inverted. Where a lower layer needs a decision that only a higher one can
make, the decision is injected rather than imported: the frontend must reject a
template named after a built-in, but it takes the reserved-name rule as a
function pointer instead of including the runtime's headers.

Every header is self-contained and includes exactly what it uses.
`include/llfpl/llfpl.h` exists for embedders and is used by nothing inside the
project.

---

## 2. Memory model

### 2.1 Nothing is allocated while a program runs

All storage is reserved during session construction. After that, evaluating an
expression, invoking a template, iterating a loop or accessing an arena performs
no allocation at all.

This is the single decision the rest of the design follows from. An allocator on
the evaluation path contributes latency that varies with the state of the heap,
and that variance is not removable by tuning; removing the allocator removes it
outright.

The complete footprint is declared in one file,
`include/llfpl/core/configuration_limits.h`, so the memory a session will use is
auditable by reading a page of constants.

### 2.2 Alignment is probed, not assumed

The cache line size is discovered at start-up: `sysctlbyname` on Darwin,
`sysconf` on Linux, `CPUID` leaf 1 on x86 as a fallback, and 64 bytes if none of
them answer. Every long-lived buffer is aligned to the probed value.

Assuming 64 bytes would be wrong on Apple silicon, which uses 128, and the cost
of being wrong is that a structure sized to occupy one line occupies two.

The operating system is asked before the processor is, because the kernel
already accounts for virtualisation, heterogeneous core clusters and firmware
overrides, none of which the raw processor report reflects.

### 2.3 Source text is mapped, never copied

Modules are mapped read-only. Atoms point into the mapping, and a template body
is a saved scanner position rather than a copy of the text.

The mapping is bounded by an explicit end pointer, never by a terminating zero
byte. A file whose length is an exact multiple of the page size has no zero byte
after its final character, so a terminator-driven lexer reads past the end of
the mapping on exactly those files, and on no others -- which is precisely the
kind of defect that survives testing.

Mappings are owned by the session and outlive every template taken from them.

---

## 3. Data structures

### 3.1 Symbol table

Coalesced chaining over one cache-line-aligned slab, split into a primary zone
of 880 slots that hashes address directly and a cellar of 144 slots that absorbs
collisions.

Each record is exactly 64 bytes, verified by a static assertion, so a probe
touches exactly one cache line. Separate chaining with heap nodes would touch
two: one for the bucket, one for the node.

Collisions link into the cellar rather than walking forward through the primary
zone, so one name's collisions can never displace another name's home slot --
the clustering failure that degrades linear probing. Robin Hood addressing would
also avoid it, but pays for its low variance with cascading element moves on
every insertion.

The cellar cursor only rises. Records are never removed, so a slot below the
cursor can never become free again, and restarting the search would re-examine
slots already known to be taken.

### 3.2 Identifier storage

Every name lives in a 48-byte inline buffer, zero-padded past its end.

Inline storage keeps a name in the same cache line as the record that owns it.
Zero padding means two equal names have byte-identical buffers, and it also
makes the length check on a lookup a single byte load: if the stored buffer has
a terminator exactly where the candidate span ends, the two names are the same
length, and only then is a comparison worth starting.

Refusal, never truncation. A truncated name would collide with a different name
that shares its prefix.

### 3.3 Template index

An open-addressed index of 512 `uint16_t` slots over at most 256 templates,
keeping occupancy at or below one half.

Linear probing is right at this occupancy because a probe sequence walks
consecutive two-byte slots, thirty-two to a cache line, so a collision costs an
increment rather than a miss.

### 3.4 Activation frames

A frame holds argument values and a borrowed pointer to the template being
invoked. Parameter names stay in the template, where they were already stored.

Copying eight 48-byte names into every frame would make each call a
third of a kilobyte of memory traffic for information that never changes between
calls. The frame as built is 80 bytes and lives on the evaluator's own stack.

---

## 4. Evaluation

### 4.1 No intermediate representation

The evaluator reads atoms from a scanner and produces values. There is no tree,
no bytecode, and no lowering pass.

The cost of running an expression is therefore the cost of scanning it plus the
cost of the arithmetic. A tree would add an allocation per node, a traversal
whose memory access pattern is decided by the allocator, and a second
representation of a program that already exists in a perfectly good one.

The price of this choice is honest and worth stating: a template body is
re-scanned on every invocation. Scanning is a linear pass over text that is
already resident and hot, which is why this is a good trade for the expression
sizes the language is built for, and it is the reason a template body is
re-read rather than re-parsed into anything.

### 4.2 Register allocation and spilling

Each activation is given the register its result belongs in and the first
register it may use for sub-expressions. Live registers are therefore bounded by
nesting depth rather than by expression size.

A logical index beyond the physical bank wraps onto a real register, and the
activation saves that register's occupant on a spill stack for its duration. No
live value is ever aliased away, and expression depth is bounded by the
recursion limit rather than by the register count.

### 4.3 Failure handling

The first diagnostic stops evaluation. Every activation checks for a recorded
error on entry and returns immediately, so recursive descent unwinds without
each level having to thread a status through its return value.

Continuing after a failure would produce a cascade of diagnostics describing
consequences rather than causes.

A depth limit of 512 activations guards the stack. Each activation uses well
under 256 bytes, which keeps a full-depth evaluation an order of magnitude
inside the smallest default thread stack.

### 4.4 Dispatch tables

Built-in forms and top-level directives are each a table of descriptors: a name,
a handler and a description. Adding either is adding a row and a function.

This is the open-closed principle applied to the part of a language runtime that
actually grows. The evaluator does not enumerate the built-ins, and the module
executor does not enumerate the directives, so neither has to change when the
language does. The same tables drive the output of `llfpl --language`, which
means the help text cannot drift out of date.

Primitives are resolved differently, by a switch on the verb's length followed
by a fixed-width comparison. Every primitive verb is eight characters or fewer,
so the comparison lowers to a single machine-word compare and the length switch
lowers to a jump table. A dispatch table would add an indirect call to an
operation whose body is one instruction.

---

## 5. Numerics

### 5.1 IEEE 754 without exception

No operation substitutes a value for a mathematically defined result. Division
by zero yields a signed infinity, zero divided by zero yields a NaN, and
comparisons with NaN are false.

The alternative -- guarding division and returning zero -- converts a detectable
singularity into a plausible-looking wrong answer that propagates silently. A
language whose purpose is arithmetic cannot make that trade.

The build uses no flag that changes a computed result. `-ffast-math` and its
components are not used. The only relaxation is `-fno-math-errno`, which affects
error reporting rather than any value.

### 5.2 Shortest round-trip printing

A result is printed as the shortest decimal string that reads back as the same
binary64 value: 15 significant digits are tried, then 16, then 17, and the first
that reparses exactly is used.

A fixed precision either truncates a result or pads it with digits that are not
in the value. Seventeen digits always round-trip, so the search terminates.

### 5.3 Cost model

The cycle counter charges a nominal issue-to-use latency per operation: 3 for an
add, 4 for a multiply, 14 for a division, 1 for a comparison. `Branch` is
charged as the two multiplies, subtract and add that its identity comprises.

This is a deterministic, architecture-independent measure of the work an
expression demands, reproducible across runs and machines. It is deliberately
not a cycle-accurate simulation of any processor. Wall-clock time, reported
separately under `--verbose`, is the measurement.

---

## 6. Register synchronisation

`Commit` publishes the low four virtual registers into architectural
floating-point registers using inline assembly, then clears the dirty bitmap.

What this guarantees is exact: the compiler may not reorder, merge or elide the
transfer, so at the following instruction the architectural registers hold the
computed values and every pending store to the register bank has been committed
to memory.

What it does not guarantee is that those registers survive the next call, since
the platform calling convention treats them as scratch. The purpose is a
deterministic, non-elidable commit point that a timing bracket cannot be
optimised across -- not a durable binding between virtual and physical
registers. It can be disabled with `--no-register-sync`.

---

## 7. Module loading

Module identity is the canonical path, obtained with `realpath`, which
normalises the path and proves the file exists in one call.

Two specifications reaching the same file are therefore one module. A diamond of
`Require` declarations loads the shared dependency once, and a cycle terminates
because a module is recorded as loaded before its body runs.

The standard library is found relative to the interpreter binary, not relative
to the working directory and not through an environment variable. An installed
`llfpl` therefore finds its library from any directory, and a relocated
installation keeps working, which a compiled-in absolute path would not.

---

## 8. Diagnostics

Every message goes through one reporter. That single choke point is what allows
results and diagnostics to occupy different streams, verbose output to be
switched off without touching a call site, and the exit status to be decided by
an authoritative error count rather than inferred.

Results go to standard output, one line per `Commit`. Diagnostics go to standard
error. A diagnostic carries the module path, line and column of the atom it
concerns, and names both what was expected and what was found.

Exit status is 0 when no error was reported, 1 when the program ran and reported
one, and 2 when the command line was malformed or the runtime could not start.

---

## 9. Build

`-O3` with link-time optimisation, so the inline hot-path accessors declared in
the hardware and identifier headers fold into the evaluator across translation
units.

The warning set is deliberately severe -- including `-Wconversion`,
`-Wsign-conversion`, `-Wcast-qual`, `-Wshadow` and `-Wdouble-promotion` -- and
the tree builds clean under all of it. `make sanitize test`
rebuilds under the address and undefined-behaviour sanitizers with recovery
disabled, so the suite fails on the first violation rather than continuing.

The configuration in force is recorded in the object tree and compared at parse
time. Switching between release, debug and sanitized builds discards the stale
objects outright rather than trusting modification times, whose one-second
granularity on some filesystems is wide enough for a fast build to slip through.

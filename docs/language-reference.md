# LLFPL Language Reference

Version 1.0.0

LLFPL is a small language. It has five top-level directives, four built-in
expression forms, eight primitive operations and one value type. Everything else
a program does is composition. This document is the complete specification of
what the interpreter accepts.

---

## 1. Lexical structure

### 1.1 Source files

A source file is a sequence of bytes with the extension `.LLFPL`. The extension
is enforced by both the command line and the `Require` directive. Source text is
read through a read-only memory mapping and is never copied, so a file must
remain unchanged for the duration of a run.

### 1.2 Comments

A comment begins with two hyphens and continues to the end of the line.

```
-- This is a comment.
Commit(plus(1, 2))  -- So is this.
```

There is no block comment form. A double hyphen is recognised before a numeric
sign is, so `-- 5` is a comment and not a negation.

### 1.3 Identifiers

An identifier begins with a letter or an underscore and continues with letters,
digits and underscores. Identifiers are case sensitive and may be at most 47
characters long. A longer identifier is rejected with a diagnostic; it is never
truncated, because truncation would silently merge two distinct names.

### 1.4 Numeric literals

```
literal := '-'? ( digit+ ( '.' digit* )? | '.' digit+ ) ( [eE] [+-]? digit+ )?
```

All of `42`, `-42`, `3.5`, `-0.125`, `.75`, `1.5e3` and `-2.5e-2` are literals.
Conversion to binary64 is correctly rounded.

An incomplete exponent is not consumed. In `1e`, the `1` is a literal and the
`e` is an identifier, so the resulting diagnostic points at the real problem
instead of reporting a malformed number.

### 1.5 Punctuation

The only punctuation is `(`, `)` and `,`. Whitespace is insignificant except as
a separator, and line breaks carry no meaning beyond ending a comment.

---

## 2. Values

Every value is an IEEE 754 binary64 double. There is no integer type, no boolean
type and no string type.

Truth is represented by the two exactly representable values `1.0` and `0.0`.
Every relational primitive produces one of exactly these two, which is what
makes the arithmetic selection performed by `Branch` exact rather than
approximate.

Infinities and NaNs are ordinary values. They arise where IEEE 754 says they
should and propagate normally.

---

## 3. Top-level directives

A module is a sequence of directives. Only directives may appear at the top
level; a bare expression there is a syntax error.

### 3.1 Identity

```
Identity(Name, expression)
```

Binds a name to the value of an expression, once and for all. The expression may
refer to anything already declared, so an identity can be derived rather than
restated:

```
Identity(SampleWidth, 8)
Identity(RingSlots, 64)
Identity(RingBytes, multiply(RingSlots, SampleWidth))
```

Binding is immutable. A second `Identity` for the same name is an error, not a
redefinition, so the meaning of a name is always readable from its single point
of declaration.

### 3.2 Slab

```
Slab(Name, byte_capacity)
```

Reserves a named data arena of the given size, aligned to the host cache line
and zero-filled. The capacity is an expression and must evaluate to a whole
number of bytes between 1 and 1073741824.

An arena is reserved once and lives until the session ends. There is no
deallocation, and none is needed: nothing about an arena's lifetime depends on
program flow.

### 3.3 Map

```
Map(Name, parameter1, parameter2, ..., body)
```

Declares a reusable parameterised expression with up to eight parameters. The
body is the final argument.

Parameters are distinguished from the body by lookahead rather than by counting,
so a body that is a bare parameter reference parses correctly:

```
Map(Echo, value, value)
```

Declaring a template allocates nothing. What is recorded is the position in the
source at which the body begins, so invoking a template re-reads the original
text rather than walking a tree built from it.

A template name may not be reused, and neither a template name nor a parameter
name may be a name the language reserves.

### 3.4 Require

```
Require(module/path.LLFPL)
```

Loads another module. The path is taken verbatim from the source between the
parentheses, so it needs no quoting and may contain separators and dots.

Resolution is tried in this order:

1. the path itself, if absolute;
2. relative to the directory of the module containing the `Require`;
3. each directory given with `-I`, in order;
4. the directories discovered from the interpreter's own location, which is
   where the bundled standard library is found;
5. relative to the current working directory.

A module is loaded at most once per session, identified by its canonical path.
Requiring the same module through two different relative paths loads it once,
and a cycle of `Require` declarations terminates rather than recursing.

### 3.5 Commit

```
Commit(expression)
```

Evaluates an expression, publishes the register bank, and writes the result to
standard output as the shortest decimal string that reads back as the same
binary64 value.

`Commit` is the only directive that produces output. Everything else a program
prints, it prints by committing it.

---

## 4. Built-in expression forms

These four forms control whether and how often their arguments are evaluated,
which is why they are built in rather than being primitives.

### 4.1 Branch

```
Branch(selector, consequent, alternative)
```

Selects between two values arithmetically:

```
result = selector * consequent + (1 - selector) * alternative
```

With a selector of exactly `1.0` or `0.0` the identity is exact, and it compiles
to arithmetic with no conditional jump, so its cost does not depend on which way
the condition went.

Both arms are always evaluated. This is a language semantic, not an
implementation detail: `Branch` selects between two values, and does not choose
which of two effects to perform. An arm with a side effect performs it either
way.

A selector that is neither `1.0` nor `0.0` interpolates between the arms, which
follows from the identity rather than being a special case.

### 4.2 Loop

```
Loop(iteration_count, TemplateName)
```

Invokes a template the given number of times, passing the zero-based iteration
index as its first parameter when it declares one. The value of the form is the
value of the final iteration, or `0` when the count is zero.

The activation frame is built once and its argument mutated per iteration, so a
loop of any length allocates nothing.

Together with arena storage for unbounded state and `Branch` for selection, this
is what makes LLFPL Turing complete.

### 4.3 write_offset

```
write_offset(ArenaName, byte_offset, value)
```

Stores a value into a reserved arena and yields the value stored, so a store
composes as an expression. The arena name is a name, not an expression.

### 4.4 read_offset

```
read_offset(ArenaName, byte_offset)
```

Loads a value from a reserved arena.

For both accessors the offset must be a finite, non-negative whole number, and
the eight bytes at that offset must lie entirely inside the arena. A fractional
or out-of-range offset is a diagnosed error, never a truncated or wrapped
access.

---

## 5. Primitive operations

| Form              | Result                                              |
| ----------------- | --------------------------------------------------- |
| `plus(a, b)`      | sum                                                  |
| `minus(a, b)`     | difference                                           |
| `multiply(a, b)`  | product                                              |
| `divide(a, b)`    | quotient under IEEE 754                              |
| `modulo(a, b)`    | remainder, taking the sign of the dividend           |
| `greater(a, b)`   | `1.0` when a is greater than b, otherwise `0.0`      |
| `less(a, b)`      | `1.0` when a is less than b, otherwise `0.0`         |
| `equal(a, b)`     | `1.0` when a equals b, otherwise `0.0`               |

Division by zero yields a signed infinity, and zero divided by zero yields a
NaN. No guard substitutes another value, because substituting one would turn a
detectable singularity into a plausible-looking wrong answer.

Every comparison involving a NaN is false, including a NaN compared with itself.

---

## 6. Name resolution

An identifier in expression position is resolved in this order:

1. built-in form: `Branch`, `Loop`, `write_offset`, `read_offset`
2. primitive operation
3. parameter of the enclosing template activation
4. template
5. identity

The order is total and unambiguous. Steps one and two name reserved verbs that
no declaration may shadow, so the only real scoping rule left is that a
parameter takes precedence over a global of the same name.

An identifier that matches nothing is an error. It never silently evaluates to
zero.

---

## 7. Reserved names

The four built-in forms, the eight primitives and the five directives are
reserved. None may be used as the name of an identity, a template, a parameter
or an arena.

---

## 8. Limits

| Quantity                        | Limit         |
| ------------------------------- | ------------- |
| Identifier length               | 47 characters |
| Identities per session          | 1024          |
| Templates per session           | 256           |
| Parameters per template         | 8             |
| Arenas per session              | 64            |
| Arena capacity                  | 1 GiB         |
| Modules per session             | 128           |
| Expression nesting depth        | 512           |

Every limit is a compile-time constant declared in
`include/llfpl/core/configuration_limits.h`, and reaching one produces a
diagnostic naming the limit. No limit is enforced by silent truncation.

---

## 9. Grammar

```
module      := directive*

directive   := 'Identity' '(' identifier ',' expression ')'
             | 'Slab'     '(' identifier ',' expression ')'
             | 'Map'      '(' identifier ( ',' identifier )* ',' expression ')'
             | 'Require'  '(' raw-path ')'
             | 'Commit'   '(' expression ')'

expression  := literal
             | identifier
             | identifier '(' argument-list? ')'

argument-list := expression ( ',' expression )*
```

An `identifier` in expression position with no argument list is a parameter or
an identity; with one, it is a built-in, a primitive or a template. `raw-path`
is the verbatim source text between the parentheses, with surrounding spaces and
tabs removed.

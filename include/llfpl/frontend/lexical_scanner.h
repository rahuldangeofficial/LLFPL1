/*
 * lexical_scanner.h -- The zero-copy lexer.
 *
 * The scanner produces atoms that point directly into a mapped source file. No
 * token text is ever copied, so lexing an entire module allocates nothing at
 * all, and a template body can be recorded as nothing more than a copy of the
 * scanner's cursor state.
 *
 * Two properties are load-bearing:
 *
 *   1. The scanner is bounded by an explicit end pointer, never by a null byte.
 *      A file whose length is an exact multiple of the page size has no zero
 *      byte after its last character, so a terminator-driven lexer would read
 *      past the end of the mapping on exactly those inputs.
 *
 *   2. The scanner is a plain value with no owned resources. Copying one is a
 *      complete, valid snapshot of a position in the source, which is what
 *      makes template invocation and lookahead free.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_FRONTEND_LEXICAL_SCANNER_H
#define LLFPL_FRONTEND_LEXICAL_SCANNER_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/diagnostic_reporter.h"

/* ---- Atoms --------------------------------------------------------------- */

typedef enum {
    LLFPL_ATOM_IDENTIFIER,         /* A name: directive, template, parameter, verb. */
    LLFPL_ATOM_NUMERIC_LITERAL,    /* A signed decimal literal, exponent optional.  */
    LLFPL_ATOM_OPEN_PARENTHESIS,   /* '('                                           */
    LLFPL_ATOM_CLOSE_PARENTHESIS,  /* ')'                                           */
    LLFPL_ATOM_ARGUMENT_SEPARATOR, /* ','                                           */
    LLFPL_ATOM_END_OF_SOURCE,      /* The bounded end of the mapping was reached.   */
    LLFPL_ATOM_UNRECOGNISED        /* A byte that begins no valid atom.             */
} LlfplAtomKind;

typedef struct {
    const char *text_begin; /* Into the mapping; not null-terminated. */
    size_t text_length;     /* Zero only for end-of-source.           */
    LlfplAtomKind kind;
    uint32_t line_number;   /* One-based. */
    uint32_t column_number; /* One-based, counted in bytes. */
} LlfplAtom;

/* ---- Scanner ------------------------------------------------------------- */

typedef struct {
    const char *source_begin;       /* First byte of the module.               */
    const char *source_end;         /* One past the last byte; the hard bound. */
    const char *cursor;             /* Next byte to examine.                   */
    const char *current_line_begin; /* Anchor for the column calculation.      */
    const char *module_path;        /* Borrowed; used only for diagnostics.    */
    uint32_t line_number;           /* One-based line of the cursor.           */
} LlfplLexicalScanner;

/*
 * Binds a scanner to a bounded span of source text. module_path is retained by
 * reference for diagnostics and must outlive the scanner, which is guaranteed
 * because it points into the mapping registry that owns the text itself.
 */
void llfpl_lexical_scanner_initialise(LlfplLexicalScanner *scanner,
                                      const char *source_begin,
                                      size_t source_length,
                                      const char *module_path);

/* Consumes and returns the next atom, advancing the scanner. */
LlfplAtom llfpl_lexical_scanner_next(LlfplLexicalScanner *scanner);

/*
 * Returns the next atom without advancing. Implemented by scanning a copy of
 * the scanner, which costs nothing because a scanner is a plain value.
 */
LlfplAtom llfpl_lexical_scanner_peek(const LlfplLexicalScanner *scanner);

/*
 * Returns the atom after the next one without advancing. The template parser
 * needs exactly two atoms of lookahead to tell a trailing parameter name from
 * the start of the body expression.
 */
LlfplAtom llfpl_lexical_scanner_peek_second(const LlfplLexicalScanner *scanner);

/* Records the scanner's position; the copy is an independent, valid scanner. */
static LLFPL_ALWAYS_INLINE void
llfpl_lexical_scanner_snapshot(const LlfplLexicalScanner *source_scanner,
                               LlfplLexicalScanner *destination_scanner) {
    *destination_scanner = *source_scanner;
}

/* ---- Reporting helpers --------------------------------------------------- */

/* Source location of an atom, ready to hand to the diagnostic reporter. */
static LLFPL_ALWAYS_INLINE LlfplSourceLocation
llfpl_atom_source_location(const LlfplLexicalScanner *scanner, LlfplAtom atom) {
    return llfpl_source_location_make((*scanner).module_path, atom.line_number, atom.column_number);
}

/* Stable lower-case name of an atom kind, for use in diagnostic messages. */
const char *llfpl_atom_kind_describe(LlfplAtomKind kind) LLFPL_CONST_FUNCTION;

#endif /* LLFPL_FRONTEND_LEXICAL_SCANNER_H */

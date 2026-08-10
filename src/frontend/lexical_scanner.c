/*
 * lexical_scanner.c -- Bounded, allocation-free tokenisation.
 *
 * The whitespace and comment skipper is a single iterative loop rather than a
 * recursive one. Recursion there is the classic way a lexer acquires a stack
 * overflow on a file that opens with a long block of comment lines, and the
 * iterative form makes the scanner's stack use constant by construction.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/frontend/lexical_scanner.h"

/* ---- Character classification -------------------------------------------- */

/*
 * Classification is done with explicit range tests instead of the <ctype.h>
 * predicates. Those consult the active locale, so their answers can change with
 * the environment; an LLFPL identifier must mean the same thing on every host,
 * and the range tests also avoid a table lookup per character.
 */

static LLFPL_ALWAYS_INLINE int character_is_decimal_digit(char inspected_character) {
    return inspected_character >= '0' && inspected_character <= '9';
}

static LLFPL_ALWAYS_INLINE int character_is_identifier_head(char inspected_character) {
    return (inspected_character >= 'a' && inspected_character <= 'z') ||
           (inspected_character >= 'A' && inspected_character <= 'Z') || inspected_character == '_';
}

static LLFPL_ALWAYS_INLINE int character_is_identifier_body(char inspected_character) {
    return character_is_identifier_head(inspected_character) ||
           character_is_decimal_digit(inspected_character);
}

static LLFPL_ALWAYS_INLINE int character_is_horizontal_or_vertical_space(char inspected_character) {
    return inspected_character == ' ' || inspected_character == '\t' ||
           inspected_character == '\n' || inspected_character == '\r' ||
           inspected_character == '\v' || inspected_character == '\f';
}

/* ---- Cursor primitives --------------------------------------------------- */

static LLFPL_ALWAYS_INLINE int cursor_has_bytes_remaining(const LlfplLexicalScanner *scanner,
                                                          size_t required_byte_count) {
    return (size_t)((*scanner).source_end - (*scanner).cursor) >= required_byte_count;
}

/* Character at a forward offset, or a null byte when that offset is past the end. */
static LLFPL_ALWAYS_INLINE char cursor_character_at(const LlfplLexicalScanner *scanner,
                                                    size_t forward_offset) {
    if (!cursor_has_bytes_remaining(scanner, forward_offset + 1u)) {
        return '\0';
    }

    return *((*scanner).cursor + forward_offset);
}

/* Advances one byte, maintaining the line and column anchors. */
static LLFPL_ALWAYS_INLINE void cursor_advance_one_byte(LlfplLexicalScanner *scanner) {
    if (*(*scanner).cursor == '\n') {
        (*scanner).line_number++;
        (*scanner).current_line_begin = (*scanner).cursor + 1;
    }

    (*scanner).cursor++;
}

static LLFPL_ALWAYS_INLINE uint32_t cursor_column_number(const LlfplLexicalScanner *scanner) {
    return (uint32_t)((*scanner).cursor - (*scanner).current_line_begin) + 1u;
}

/* ---- Insignificant text -------------------------------------------------- */

/*
 * Consumes whitespace and line comments until the cursor rests on the first
 * byte of an atom, or on the end of the source.
 */
static void skip_insignificant_text(LlfplLexicalScanner *scanner) {
    while ((*scanner).cursor < (*scanner).source_end) {
        const char inspected_character = *(*scanner).cursor;

        if (character_is_horizontal_or_vertical_space(inspected_character)) {
            cursor_advance_one_byte(scanner);
            continue;
        }

        /* A line comment opens with two hyphens and runs to the line break. */
        if (inspected_character == '-' && cursor_character_at(scanner, 1u) == '-') {
            while ((*scanner).cursor < (*scanner).source_end && *(*scanner).cursor != '\n') {
                cursor_advance_one_byte(scanner);
            }
            continue;
        }

        return;
    }
}

/* ---- Atom construction --------------------------------------------------- */

/*
 * Emits an atom of the given kind covering the next atom_length bytes and
 * advances the cursor past them. Atoms never span a line break, so the cursor
 * can be advanced by a plain addition without the line bookkeeping.
 */
static LlfplAtom emit_atom(LlfplLexicalScanner *scanner, LlfplAtomKind kind, size_t atom_length) {
    LlfplAtom atom;

    atom.text_begin = (*scanner).cursor;
    atom.text_length = atom_length;
    atom.kind = kind;
    atom.line_number = (*scanner).line_number;
    atom.column_number = cursor_column_number(scanner);

    (*scanner).cursor += atom_length;

    return atom;
}

/*
 * Measures a numeric literal starting at the cursor.
 *
 *   literal := '-'? ( digit+ ( '.' digit* )? | '.' digit+ ) ( [eE] [+-]? digit+ )?
 *
 * A leading hyphen is unambiguous: two hyphens already opened a comment before
 * control reached here, so a hyphen at this point can only introduce a sign.
 * Returns zero when the cursor does not begin a literal at all.
 */
static size_t measure_numeric_literal(const LlfplLexicalScanner *scanner) {
    size_t measured_length = 0u;
    size_t digits_before_exponent = 0u;

    if (cursor_character_at(scanner, measured_length) == '-') {
        measured_length++;
    }

    while (character_is_decimal_digit(cursor_character_at(scanner, measured_length))) {
        measured_length++;
        digits_before_exponent++;
    }

    if (cursor_character_at(scanner, measured_length) == '.') {
        measured_length++;

        while (character_is_decimal_digit(cursor_character_at(scanner, measured_length))) {
            measured_length++;
            digits_before_exponent++;
        }
    }

    /* A sign or a lone decimal point with no digits is not a number. */
    if (digits_before_exponent == 0u) {
        return 0u;
    }

    /*
     * An exponent is accepted only when it is complete. An incomplete one, as in
     * "1e", is left unconsumed so that the 'e' lexes as the identifier it is and
     * the parser reports the problem where the reader can see it.
     */
    {
        const char exponent_marker = cursor_character_at(scanner, measured_length);

        if (exponent_marker == 'e' || exponent_marker == 'E') {
            size_t exponent_length = measured_length + 1u;
            const char exponent_sign = cursor_character_at(scanner, exponent_length);
            size_t exponent_digit_count = 0u;

            if (exponent_sign == '+' || exponent_sign == '-') {
                exponent_length++;
            }

            while (character_is_decimal_digit(cursor_character_at(scanner, exponent_length))) {
                exponent_length++;
                exponent_digit_count++;
            }

            if (exponent_digit_count > 0u) {
                measured_length = exponent_length;
            }
        }
    }

    return measured_length;
}

/* Measures an identifier starting at the cursor. */
static size_t measure_identifier(const LlfplLexicalScanner *scanner) {
    size_t measured_length = 0u;

    while (character_is_identifier_body(cursor_character_at(scanner, measured_length))) {
        measured_length++;
    }

    return measured_length;
}

/* ---- Public interface ---------------------------------------------------- */

void llfpl_lexical_scanner_initialise(LlfplLexicalScanner *scanner,
                                      const char *source_begin,
                                      size_t source_length,
                                      const char *module_path) {
    if (scanner == NULL) {
        return;
    }

    (*scanner).source_begin = source_begin;
    (*scanner).source_end = source_begin + source_length;
    (*scanner).cursor = source_begin;
    (*scanner).current_line_begin = source_begin;
    (*scanner).module_path = module_path;
    (*scanner).line_number = 1u;
}

LlfplAtom llfpl_lexical_scanner_next(LlfplLexicalScanner *scanner) {
    char leading_character = '\0';
    size_t measured_length = 0u;

    skip_insignificant_text(scanner);

    if ((*scanner).cursor >= (*scanner).source_end) {
        return emit_atom(scanner, LLFPL_ATOM_END_OF_SOURCE, 0u);
    }

    leading_character = *(*scanner).cursor;

    if (character_is_identifier_head(leading_character)) {
        return emit_atom(scanner, LLFPL_ATOM_IDENTIFIER, measure_identifier(scanner));
    }

    if (character_is_decimal_digit(leading_character) || leading_character == '-' ||
        leading_character == '.') {
        measured_length = measure_numeric_literal(scanner);

        if (measured_length > 0u) {
            return emit_atom(scanner, LLFPL_ATOM_NUMERIC_LITERAL, measured_length);
        }
    }

    switch (leading_character) {
        case '(':
            return emit_atom(scanner, LLFPL_ATOM_OPEN_PARENTHESIS, 1u);
        case ')':
            return emit_atom(scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, 1u);
        case ',':
            return emit_atom(scanner, LLFPL_ATOM_ARGUMENT_SEPARATOR, 1u);
        default:
            return emit_atom(scanner, LLFPL_ATOM_UNRECOGNISED, 1u);
    }
}

LlfplAtom llfpl_lexical_scanner_peek(const LlfplLexicalScanner *scanner) {
    LlfplLexicalScanner lookahead_scanner = *scanner;

    return llfpl_lexical_scanner_next(&lookahead_scanner);
}

LlfplAtom llfpl_lexical_scanner_peek_second(const LlfplLexicalScanner *scanner) {
    LlfplLexicalScanner lookahead_scanner = *scanner;

    (void)llfpl_lexical_scanner_next(&lookahead_scanner);

    return llfpl_lexical_scanner_next(&lookahead_scanner);
}

const char *llfpl_atom_kind_describe(LlfplAtomKind kind) {
    switch (kind) {
        case LLFPL_ATOM_IDENTIFIER:
            return "identifier";
        case LLFPL_ATOM_NUMERIC_LITERAL:
            return "numeric literal";
        case LLFPL_ATOM_OPEN_PARENTHESIS:
            return "'('";
        case LLFPL_ATOM_CLOSE_PARENTHESIS:
            return "')'";
        case LLFPL_ATOM_ARGUMENT_SEPARATOR:
            return "','";
        case LLFPL_ATOM_END_OF_SOURCE:
            return "end of source";
        case LLFPL_ATOM_UNRECOGNISED:
            return "unrecognised character";
    }

    return "unknown atom";
}

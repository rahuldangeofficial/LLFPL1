/*
 * numeric_conversion.h -- Exact text-to-double and double-to-text conversion.
 *
 * LLFPL is a numeric language, so the fidelity of these two functions is a
 * language-level guarantee rather than a formatting convenience:
 *
 *   - Parsing uses the C library's correctly rounded strtod, applied to a
 *     bounded copy of the literal so that no read ever leaves the source
 *     mapping.
 *   - Printing emits the shortest decimal string that reads back as the exact
 *     same binary64 value. A result is therefore never silently truncated, and
 *     never padded with the noise digits that a fixed precision produces.
 *
 * The process is left in the "C" locale for its whole lifetime, so the decimal
 * separator is always a period regardless of the host environment.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_NUMERIC_CONVERSION_H
#define LLFPL_CORE_NUMERIC_CONVERSION_H

#include <stddef.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/status_code.h"

/*
 * Converts the first literal_length bytes of literal_text into a double.
 *
 * Returns LLFPL_STATUS_OK only when the entire span was consumed by a single
 * well-formed numeric literal. Any trailing character, an empty span, or a span
 * wider than LLFPL_NUMERIC_LITERAL_BUFFER_CAPACITY yields
 * LLFPL_STATUS_SYNTAX_ERROR and leaves the output untouched.
 */
LlfplStatus llfpl_numeric_parse_double(const char *literal_text,
                                       size_t literal_length,
                                       double *parsed_value_out) LLFPL_WARN_UNUSED_RESULT;

/*
 * Writes the shortest round-tripping decimal rendering of value into
 * destination_buffer and returns the number of characters written, excluding
 * the terminating null byte. A capacity of at least 32 bytes is always
 * sufficient. Returns zero and writes an empty string when the buffer is too
 * small to hold the rendering.
 */
size_t llfpl_numeric_format_double(double value, char *destination_buffer, size_t buffer_capacity);

#endif /* LLFPL_CORE_NUMERIC_CONVERSION_H */

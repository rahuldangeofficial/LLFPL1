/*
 * numeric_conversion.c -- Correctly rounded parsing and shortest round-trip
 *                         printing of binary64 values.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/core/numeric_conversion.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "llfpl/core/configuration_limits.h"

/*
 * The three precisions tried, in order, when printing. Seventeen significant
 * digits are sufficient to round-trip every finite binary64 value, so the last
 * candidate always succeeds; the earlier ones simply produce a shorter string
 * when one exists that still reads back exactly.
 */
static const int shortest_round_trip_precisions[] = {15, 16, 17};

LlfplStatus llfpl_numeric_parse_double(const char *literal_text,
                                       size_t literal_length,
                                       double *parsed_value_out) {
    char terminated_literal[LLFPL_NUMERIC_LITERAL_BUFFER_CAPACITY];
    char *first_unconsumed_character = NULL;
    double parsed_value = 0.0;

    if (literal_text == NULL || parsed_value_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (literal_length == 0u || literal_length >= sizeof(terminated_literal)) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    /*
     * The source text lives in a read-only file mapping that is bounded by its
     * length rather than by a null byte, so the literal is copied out before
     * strtod -- which insists on a terminated string -- is allowed to see it.
     */
    memcpy(terminated_literal, literal_text, literal_length);
    *(terminated_literal + literal_length) = '\0';

    parsed_value = strtod(terminated_literal, &first_unconsumed_character);

    /* A partially consumed literal is a lexer defect, not a valid number. */
    if (first_unconsumed_character != terminated_literal + literal_length) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    *parsed_value_out = parsed_value;
    return LLFPL_STATUS_OK;
}

size_t llfpl_numeric_format_double(double value, char *destination_buffer, size_t buffer_capacity) {
    size_t candidate_index = 0u;

    if (destination_buffer == NULL || buffer_capacity == 0u) {
        return 0u;
    }

    *destination_buffer = '\0';

    for (candidate_index = 0u; candidate_index < LLFPL_ARRAY_LENGTH(shortest_round_trip_precisions);
         candidate_index++) {
        const int precision = *(shortest_round_trip_precisions + candidate_index);
        double reparsed_value = 0.0;
        char *first_unconsumed_character = NULL;
        int written_length =
            snprintf(destination_buffer, buffer_capacity, "%.*g", precision, value);

        if (written_length < 0 || (size_t)written_length >= buffer_capacity) {
            *destination_buffer = '\0';
            return 0u;
        }

        reparsed_value = strtod(destination_buffer, &first_unconsumed_character);

        /*
         * Bit-identical comparison, not an epsilon test: the contract is that
         * the printed text reads back as the very same value. Comparing the
         * doubles directly is exactly right here, and it also rejects the
         * candidate for a NaN input, which correctly falls through to the
         * widest precision and prints the platform's canonical spelling.
         */
        if (reparsed_value == value) {
            return (size_t)written_length;
        }
    }

    /*
     * Reached only for values that are not equal to themselves -- that is, for
     * NaN. The widest rendering already sits in the buffer and is the right
     * answer; report its length.
     */
    return strlen(destination_buffer);
}

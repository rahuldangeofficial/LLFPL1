/*
 * primitive_reduction.c -- Verb resolution and IEEE 754 evaluation.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/runtime/primitive_reduction.h"

#include <math.h>
#include <string.h>

/* ---- Verb resolution ------------------------------------------------------ */

/*
 * Compares a span against a literal of known length. The length has already
 * been established by the caller's switch, so the comparison is a fixed-width
 * memcmp that the compiler lowers to one or two word compares.
 */
#define LLFPL_SPAN_MATCHES(span_text, literal) \
    (memcmp((span_text), (literal), sizeof(literal) - 1u) == 0)

LlfplPrimitiveOperation llfpl_primitive_operation_from_span(const char *verb_text,
                                                            size_t verb_length) {
    if (verb_text == NULL) {
        return LLFPL_PRIMITIVE_NONE;
    }

    switch (verb_length) {
        case 4u:
            if (LLFPL_SPAN_MATCHES(verb_text, "plus")) {
                return LLFPL_PRIMITIVE_ADD;
            }
            if (LLFPL_SPAN_MATCHES(verb_text, "less")) {
                return LLFPL_PRIMITIVE_LESS_THAN;
            }
            break;

        case 5u:
            if (LLFPL_SPAN_MATCHES(verb_text, "minus")) {
                return LLFPL_PRIMITIVE_SUBTRACT;
            }
            if (LLFPL_SPAN_MATCHES(verb_text, "equal")) {
                return LLFPL_PRIMITIVE_EQUAL_TO;
            }
            break;

        case 6u:
            if (LLFPL_SPAN_MATCHES(verb_text, "divide")) {
                return LLFPL_PRIMITIVE_DIVIDE;
            }
            if (LLFPL_SPAN_MATCHES(verb_text, "modulo")) {
                return LLFPL_PRIMITIVE_MODULO;
            }
            break;

        case 7u:
            if (LLFPL_SPAN_MATCHES(verb_text, "greater")) {
                return LLFPL_PRIMITIVE_GREATER_THAN;
            }
            break;

        case 8u:
            if (LLFPL_SPAN_MATCHES(verb_text, "multiply")) {
                return LLFPL_PRIMITIVE_MULTIPLY;
            }
            break;

        default:
            break;
    }

    return LLFPL_PRIMITIVE_NONE;
}

const char *llfpl_primitive_operation_name(LlfplPrimitiveOperation operation) {
    switch (operation) {
        case LLFPL_PRIMITIVE_ADD:
            return "plus";
        case LLFPL_PRIMITIVE_SUBTRACT:
            return "minus";
        case LLFPL_PRIMITIVE_MULTIPLY:
            return "multiply";
        case LLFPL_PRIMITIVE_DIVIDE:
            return "divide";
        case LLFPL_PRIMITIVE_MODULO:
            return "modulo";
        case LLFPL_PRIMITIVE_GREATER_THAN:
            return "greater";
        case LLFPL_PRIMITIVE_LESS_THAN:
            return "less";
        case LLFPL_PRIMITIVE_EQUAL_TO:
            return "equal";
        case LLFPL_PRIMITIVE_NONE:
            break;
    }

    return "none";
}

/* ---- Evaluation ----------------------------------------------------------- */

double llfpl_primitive_apply(LlfplPrimitiveOperation operation,
                             double left_operand,
                             double right_operand) {
    switch (operation) {
        case LLFPL_PRIMITIVE_ADD:
            return left_operand + right_operand;

        case LLFPL_PRIMITIVE_SUBTRACT:
            return left_operand - right_operand;

        case LLFPL_PRIMITIVE_MULTIPLY:
            return left_operand * right_operand;

        case LLFPL_PRIMITIVE_DIVIDE:
            /*
             * No zero guard. Floating-point division by zero raises no signal;
             * it produces a signed infinity, or a NaN for the indeterminate
             * form, and that value carries the fact of the singularity forward
             * where a substituted zero would conceal it.
             */
            return left_operand / right_operand;

        case LLFPL_PRIMITIVE_MODULO:
            /*
             * fmod, not remainder: fmod truncates toward zero and so agrees
             * with the sign convention of the dividend, which is what the
             * modulo of a language with C-family arithmetic is expected to do.
             */
            return fmod(left_operand, right_operand);

        /*
         * Relational results are exactly 1.0 or 0.0. Any comparison involving a
         * NaN is false by IEEE 754, including equality of a NaN with itself, so
         * these yield 0.0 in that case -- which is the correct answer, not an
         * oversight.
         */
        case LLFPL_PRIMITIVE_GREATER_THAN:
            return (left_operand > right_operand) ? 1.0 : 0.0;

        case LLFPL_PRIMITIVE_LESS_THAN:
            return (left_operand < right_operand) ? 1.0 : 0.0;

        case LLFPL_PRIMITIVE_EQUAL_TO:
            return (left_operand == right_operand) ? 1.0 : 0.0;

        case LLFPL_PRIMITIVE_NONE:
            break;
    }

    return 0.0;
}

/* ---- Cost model ----------------------------------------------------------- */

uint64_t llfpl_primitive_nominal_latency(LlfplPrimitiveOperation operation) {
    switch (operation) {
        case LLFPL_PRIMITIVE_ADD:
        case LLFPL_PRIMITIVE_SUBTRACT:
            return 3u;

        case LLFPL_PRIMITIVE_MULTIPLY:
            return 4u;

        case LLFPL_PRIMITIVE_DIVIDE:
            return 14u;

        case LLFPL_PRIMITIVE_MODULO:
            return 20u;

        case LLFPL_PRIMITIVE_GREATER_THAN:
        case LLFPL_PRIMITIVE_LESS_THAN:
        case LLFPL_PRIMITIVE_EQUAL_TO:
            return 1u;

        case LLFPL_PRIMITIVE_NONE:
            break;
    }

    return 0u;
}

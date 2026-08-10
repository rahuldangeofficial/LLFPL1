/*
 * expression_evaluator.h -- Recursive-descent evaluation straight from source
 *                           text into registers.
 *
 * There is no intermediate representation. The evaluator reads atoms from a
 * scanner and produces values, so the cost of running an expression is the cost
 * of scanning it plus the cost of the arithmetic. Nothing is allocated, nothing
 * is walked twice, and no node structure is built only to be discarded.
 *
 * Register allocation is explicit and travels with the call. Each activation
 * receives the register its result belongs in and the first register it may use
 * for sub-expressions, which bounds the live register count at the nesting
 * depth rather than at the size of the expression.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_EXPRESSION_EVALUATOR_H
#define LLFPL_RUNTIME_EXPRESSION_EVALUATOR_H

#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/frontend/lexical_scanner.h"
#include "llfpl/runtime/activation_frame.h"
#include "llfpl/runtime/execution_context.h"

/*
 * Where an activation's result goes, and where it may work.
 *
 * Both indices are logical. An index at or beyond the physical register count
 * wraps onto a real register, and the evaluator saves that register's occupant
 * on the spill stack for the duration of the activation, so wrapping never
 * loses a live value.
 */
typedef struct {
    uint16_t destination_register_index;
    uint16_t first_scratch_register_index;
} LlfplRegisterAllocation;

static LLFPL_ALWAYS_INLINE LlfplRegisterAllocation llfpl_register_allocation_make(
    uint16_t destination_register_index, uint16_t first_scratch_register_index) {
    LlfplRegisterAllocation allocation;

    allocation.destination_register_index = destination_register_index;
    allocation.first_scratch_register_index = first_scratch_register_index;

    return allocation;
}

/*
 * Evaluates exactly one expression from the scanner and returns its value.
 *
 * On failure a diagnostic is reported and zero is returned; the caller detects
 * this with llfpl_execution_context_has_failed rather than by inspecting the
 * value, because zero is also a perfectly ordinary result.
 */
double llfpl_expression_evaluator_evaluate(LlfplExecutionContext *context,
                                           LlfplLexicalScanner *scanner,
                                           const LlfplActivationFrame *frame,
                                           LlfplRegisterAllocation allocation);

/*
 * Consumes an atom of the expected kind, reporting a located diagnostic that
 * names the construct being parsed when the expectation is not met. Returns
 * non-zero on success. Shared with the built-in handlers so that every
 * expectation in the language produces the same shape of message.
 */
int llfpl_expression_evaluator_expect_atom(LlfplExecutionContext *context,
                                           LlfplLexicalScanner *scanner,
                                           LlfplAtomKind expected_kind,
                                           const char *construct_description);

#endif /* LLFPL_RUNTIME_EXPRESSION_EVALUATOR_H */

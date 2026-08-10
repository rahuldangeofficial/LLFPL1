/*
 * primitive_reduction.h -- The irreducible arithmetic and relational
 *                          operations of the language.
 *
 * These eight operations are the only computation LLFPL performs directly;
 * everything else in a program is composition. They are kept in their own
 * module, free of any dependency on the register file, the symbol table or the
 * evaluator, so that the definition of what the language computes stays
 * separate from the machinery that decides when to compute it.
 *
 * Semantics are IEEE 754 binary64, without exception. Division by zero yields a
 * signed infinity and zero divided by zero yields a quiet NaN, because those
 * are the correct results in the extended reals and they are what every other
 * serious numerical system produces. Substituting zero, as a naive guard would,
 * turns a detectable singularity into a plausible-looking wrong answer that
 * propagates silently through the rest of the computation.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_PRIMITIVE_REDUCTION_H
#define LLFPL_RUNTIME_PRIMITIVE_REDUCTION_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"

typedef enum {
    LLFPL_PRIMITIVE_ADD,
    LLFPL_PRIMITIVE_SUBTRACT,
    LLFPL_PRIMITIVE_MULTIPLY,
    LLFPL_PRIMITIVE_DIVIDE,
    LLFPL_PRIMITIVE_MODULO,
    LLFPL_PRIMITIVE_GREATER_THAN,
    LLFPL_PRIMITIVE_LESS_THAN,
    LLFPL_PRIMITIVE_EQUAL_TO,

    /* Not an operation: the verb named no primitive. */
    LLFPL_PRIMITIVE_NONE
} LlfplPrimitiveOperation;

/*
 * Resolves a verb spelling to its operation, or LLFPL_PRIMITIVE_NONE.
 *
 * The lookup dispatches on the verb's length first and compares fixed-width
 * spans second. Every primitive verb is eight characters or fewer, so each
 * comparison lowers to a single machine-word compare, and the length switch
 * lowers to a jump table -- no chain of string comparisons, and no hash.
 */
LlfplPrimitiveOperation llfpl_primitive_operation_from_span(const char *verb_text,
                                                            size_t verb_length) LLFPL_PURE_FUNCTION;

/* Canonical spelling of an operation, for diagnostics. Never null. */
const char *llfpl_primitive_operation_name(LlfplPrimitiveOperation operation) LLFPL_CONST_FUNCTION;

/*
 * Applies an operation to two operands under IEEE 754 semantics.
 *
 * Relational operations produce exactly 1.0 or 0.0, never any other value. That
 * is what makes the branchless selection identity used by Branch exact rather
 * than approximate:
 *
 *     result = condition * consequent + (1 - condition) * alternative
 *
 * With a condition restricted to the two integral values 1.0 and 0.0, both
 * products are exactly representable and the sum is exact, so the identity
 * reproduces a conditional selection bit for bit while emitting no branch.
 */
double llfpl_primitive_apply(LlfplPrimitiveOperation operation,
                             double left_operand,
                             double right_operand) LLFPL_CONST_FUNCTION;

/*
 * Nominal issue-to-use latency charged to the cycle counter for an operation.
 *
 * These weights are a deterministic, architecture-independent cost model that
 * reflects the relative expense of the operations on contemporary superscalar
 * cores: a division costs several times a multiply, a comparison costs least.
 * The counter is therefore a reproducible measure of the work an expression
 * demands, and is deliberately not a cycle-accurate simulation of any specific
 * processor. Wall-clock timing, reported separately, is the measurement.
 */
uint64_t llfpl_primitive_nominal_latency(LlfplPrimitiveOperation operation) LLFPL_CONST_FUNCTION;

#endif /* LLFPL_RUNTIME_PRIMITIVE_REDUCTION_H */

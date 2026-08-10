/*
 * execution_context.h -- The set of collaborators an evaluation needs, passed
 *                        as one object.
 *
 * Threading five subsystem pointers plus two counters through every recursive
 * call was the shape this evaluator would otherwise have. Bundling them removes
 * that: a function that needs the runtime takes one parameter, adding a
 * subsystem changes no signature anywhere, and the recursion depth is stored
 * where the guard that reads it can be relied upon.
 *
 * The context borrows every subsystem it names. It owns nothing, constructs
 * nothing and destroys nothing; the interpreter session owns them all and
 * outlives every context built over them.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_EXECUTION_CONTEXT_H
#define LLFPL_RUNTIME_EXECUTION_CONTEXT_H

#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/diagnostic_reporter.h"
#include "llfpl/frontend/template_definition.h"
#include "llfpl/hardware/vector_register_file.h"
#include "llfpl/memory/memory_arena.h"
#include "llfpl/runtime/symbol_table.h"

typedef struct {
    LlfplSymbolTable *symbol_table;           /* Immutable identity bindings. */
    LlfplTemplateSegment *template_segment;   /* Map declarations.            */
    LlfplMemoryArenaRegistry *arena_registry; /* Slab reservations.           */
    LlfplVectorRegisterFile *register_file;   /* Virtual register bank.       */
    LlfplDiagnosticReporter *diagnostic_reporter;

    uint32_t current_evaluation_depth; /* Rises on entry, falls on exit. */
    uint32_t maximum_evaluation_depth; /* Guard against runaway recursion. */
} LlfplExecutionContext;

/*
 * Once an evaluation has failed, the remainder of the expression is abandoned
 * rather than evaluated against invalid state. Recursive descent has no other
 * way to unwind, and continuing would turn one honest diagnostic into a cascade
 * of misleading ones.
 */
static LLFPL_ALWAYS_INLINE int
llfpl_execution_context_has_failed(const LlfplExecutionContext *context) {
    return llfpl_diagnostic_reporter_has_errors((*context).diagnostic_reporter);
}

#endif /* LLFPL_RUNTIME_EXECUTION_CONTEXT_H */

/*
 * builtin_operation.c -- The special forms of LLFPL and their dispatch table.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/runtime/builtin_operation.h"

#include <math.h>
#include <string.h>

#include "llfpl/core/identifier.h"
#include "llfpl/runtime/primitive_reduction.h"

/* ---- Shared handler utilities -------------------------------------------- */

/* Consumes an atom of the expected kind on behalf of a named special form. */
static int expect_atom(const LlfplBuiltinInvocation *invocation,
                       LlfplAtomKind expected_kind,
                       const char *construct_description) {
    return llfpl_expression_evaluator_expect_atom(
        (*invocation).context, (*invocation).scanner, expected_kind, construct_description);
}

/* Evaluates one argument into the given pair of logical registers. */
static double evaluate_argument(const LlfplBuiltinInvocation *invocation,
                                uint16_t destination_register_index,
                                uint16_t first_scratch_register_index) {
    return llfpl_expression_evaluator_evaluate(
        (*invocation).context,
        (*invocation).scanner,
        (*invocation).frame,
        llfpl_register_allocation_make(destination_register_index, first_scratch_register_index));
}

/* Publishes a special form's result into its destination register. */
static double publish_result(const LlfplBuiltinInvocation *invocation, double result_value) {
    const uint16_t register_count =
        llfpl_vector_register_file_count((*(*invocation).context).register_file);

    if (register_count > 0u) {
        llfpl_vector_register_file_write(
            (*(*invocation).context).register_file,
            (uint16_t)((*invocation).allocation.destination_register_index % register_count),
            result_value);
    }

    return result_value;
}

/*
 * Reports a plain message located at the special form's own call site. The
 * message is passed through a "%s" conversion rather than used as a format, so
 * a handler can never accidentally turn text into a format specification.
 */
static void report_error(const LlfplBuiltinInvocation *invocation, const char *message) {
    llfpl_diagnostic_report_error(
        (*(*invocation).context).diagnostic_reporter, (*invocation).call_site, "%s", message);
}

/*
 * Converts an evaluated expression into a byte offset.
 *
 * An offset is a memory address, so the conversion is strict: the value must be
 * finite, non-negative, integral, and within the 32-bit offset range. Accepting
 * a fractional or out-of-range value by truncating it would turn a program bug
 * into a silently wrong memory access.
 */
static int convert_to_byte_offset(const LlfplBuiltinInvocation *invocation,
                                  double offset_value,
                                  uint32_t *byte_offset_out) {
    if (!isfinite(offset_value) || offset_value < 0.0 || offset_value > (double)UINT32_MAX ||
        floor(offset_value) != offset_value) {
        report_error(invocation, "a byte offset must be a non-negative whole number");
        return 0;
    }

    *byte_offset_out = (uint32_t)offset_value;
    return 1;
}

/* Reads an arena name atom and resolves it, reporting an undeclared arena. */
static LlfplMemoryArena *consume_arena_operand(const LlfplBuiltinInvocation *invocation) {
    const LlfplAtom arena_name_atom = llfpl_lexical_scanner_next((*invocation).scanner);
    LlfplMemoryArena *arena = NULL;

    if (arena_name_atom.kind != LLFPL_ATOM_IDENTIFIER) {
        llfpl_diagnostic_report_error(
            (*(*invocation).context).diagnostic_reporter,
            llfpl_atom_source_location((*invocation).scanner, arena_name_atom),
            "expected an arena name but found %s",
            llfpl_atom_kind_describe(arena_name_atom.kind));
        return NULL;
    }

    {
        LlfplMemoryArenaRegistry *registry = (*(*invocation).context).arena_registry;
        uint32_t arena_index = 0u;

        for (arena_index = 0u; arena_index < (*registry).arena_count; arena_index++) {
            LlfplMemoryArena *candidate_arena = (*registry).arenas + arena_index;

            if (llfpl_identifier_equals_span((*candidate_arena).name,
                                             arena_name_atom.text_begin,
                                             arena_name_atom.text_length)) {
                arena = candidate_arena;
                break;
            }
        }
    }

    if (arena == NULL) {
        llfpl_diagnostic_report_error(
            (*(*invocation).context).diagnostic_reporter,
            llfpl_atom_source_location((*invocation).scanner, arena_name_atom),
            "'%.*s' names no reserved arena",
            (int)arena_name_atom.text_length,
            arena_name_atom.text_begin);
    }

    return arena;
}

/* ---- Branch --------------------------------------------------------------- */

/*
 * Branch(selector, consequent, alternative)
 *
 * Both arms are evaluated, then combined arithmetically:
 *
 *     result = selector * consequent + (1 - selector) * alternative
 *
 * with the selector restricted to the exactly representable values 1.0 and 0.0
 * that every relational primitive produces. The selection is therefore exact,
 * and it compiles to arithmetic with no conditional jump, so its cost does not
 * depend on which way the condition went and no branch predictor can mispredict
 * it. On unpredictable data that is decisively faster than a real branch.
 *
 * That both arms are evaluated is a language semantic, not an implementation
 * detail. Branch selects between two values; it does not choose which of two
 * effects to perform.
 */
static double builtin_branch(const LlfplBuiltinInvocation *invocation) {
    const uint16_t first_scratch = (*invocation).allocation.first_scratch_register_index;
    double selector_value = 0.0;
    double consequent_value = 0.0;
    double alternative_value = 0.0;
    double selected_value = 0.0;

    if (!expect_atom(invocation, LLFPL_ATOM_OPEN_PARENTHESIS, "Branch")) {
        return 0.0;
    }

    selector_value = evaluate_argument(invocation, first_scratch, (uint16_t)(first_scratch + 1u));

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "Branch")) {
        return 0.0;
    }

    consequent_value = evaluate_argument(
        invocation, (uint16_t)(first_scratch + 1u), (uint16_t)(first_scratch + 2u));

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "Branch")) {
        return 0.0;
    }

    alternative_value = evaluate_argument(
        invocation, (uint16_t)(first_scratch + 2u), (uint16_t)(first_scratch + 3u));

    if (!expect_atom(invocation, LLFPL_ATOM_CLOSE_PARENTHESIS, "Branch")) {
        return 0.0;
    }

    selected_value =
        (selector_value * consequent_value) + ((1.0 - selector_value) * alternative_value);

    /* Charged as its constituent reductions: two multiplies, a subtract, an add. */
    llfpl_vector_register_file_charge_cycles(
        (*(*invocation).context).register_file,
        (2u * llfpl_primitive_nominal_latency(LLFPL_PRIMITIVE_MULTIPLY)) +
            llfpl_primitive_nominal_latency(LLFPL_PRIMITIVE_SUBTRACT) +
            llfpl_primitive_nominal_latency(LLFPL_PRIMITIVE_ADD));

    return publish_result(invocation, selected_value);
}

/* ---- Loop ----------------------------------------------------------------- */

/*
 * Loop(iteration_count, TemplateName)
 *
 * Invokes a template the given number of times, passing the zero-based
 * iteration index as its first parameter when it declares one. This is the
 * construct that makes LLFPL Turing complete: combined with arena storage for
 * unbounded state and Branch for selection, it supplies the unbounded iteration
 * a general model of computation requires.
 *
 * The activation frame is built once and its argument mutated per iteration, so
 * a loop of any length performs no allocation whatsoever.
 */
static double builtin_loop(const LlfplBuiltinInvocation *invocation) {
    const uint16_t first_scratch = (*invocation).allocation.first_scratch_register_index;
    const LlfplTemplateDefinition *invoked_template = NULL;
    LlfplActivationFrame iteration_frame;
    LlfplAtom template_name_atom;
    double iteration_count_value = 0.0;
    double last_iteration_value = 0.0;
    uint64_t iteration_limit = 0u;
    uint64_t iteration_index = 0u;

    if (!expect_atom(invocation, LLFPL_ATOM_OPEN_PARENTHESIS, "Loop")) {
        return 0.0;
    }

    iteration_count_value =
        evaluate_argument(invocation, first_scratch, (uint16_t)(first_scratch + 1u));

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "Loop")) {
        return 0.0;
    }

    template_name_atom = llfpl_lexical_scanner_next((*invocation).scanner);

    if (template_name_atom.kind != LLFPL_ATOM_IDENTIFIER) {
        llfpl_diagnostic_report_error(
            (*(*invocation).context).diagnostic_reporter,
            llfpl_atom_source_location((*invocation).scanner, template_name_atom),
            "Loop expects a template name but found %s",
            llfpl_atom_kind_describe(template_name_atom.kind));
        return 0.0;
    }

    if (!expect_atom(invocation, LLFPL_ATOM_CLOSE_PARENTHESIS, "Loop")) {
        return 0.0;
    }

    invoked_template = llfpl_template_segment_lookup_span((*(*invocation).context).template_segment,
                                                          template_name_atom.text_begin,
                                                          template_name_atom.text_length);
    if (invoked_template == NULL) {
        llfpl_diagnostic_report_error(
            (*(*invocation).context).diagnostic_reporter,
            llfpl_atom_source_location((*invocation).scanner, template_name_atom),
            "'%.*s' names no template",
            (int)template_name_atom.text_length,
            template_name_atom.text_begin);
        return 0.0;
    }

    if (!isfinite(iteration_count_value) || iteration_count_value < 0.0 ||
        iteration_count_value >= 9223372036854775808.0) {
        report_error(invocation, "Loop requires a finite, non-negative iteration count");
        return 0.0;
    }

    iteration_limit = (uint64_t)iteration_count_value;

    llfpl_activation_frame_initialise(&iteration_frame, invoked_template);
    iteration_frame.argument_count = ((*invoked_template).parameter_count > 0u) ? 1u : 0u;

    for (iteration_index = 0u; iteration_index < iteration_limit; iteration_index++) {
        LlfplLexicalScanner body_scanner;

        /* Stop at the first failure instead of repeating it a million times. */
        if (llfpl_execution_context_has_failed((*invocation).context)) {
            return 0.0;
        }

        *(iteration_frame.argument_values + 0) = (double)iteration_index;

        llfpl_lexical_scanner_snapshot(&(*invoked_template).body_snapshot, &body_scanner);

        last_iteration_value = llfpl_expression_evaluator_evaluate(
            (*invocation).context, &body_scanner, &iteration_frame, (*invocation).allocation);
    }

    return publish_result(invocation, last_iteration_value);
}

/* ---- Arena access ---------------------------------------------------------- */

/*
 * write_offset(ArenaName, byte_offset, value)
 *
 * Stores a value into a reserved arena and yields the value stored, so that a
 * store composes as an expression.
 */
static double builtin_write_offset(const LlfplBuiltinInvocation *invocation) {
    const uint16_t first_scratch = (*invocation).allocation.first_scratch_register_index;
    LlfplMemoryArena *arena = NULL;
    double offset_value = 0.0;
    double stored_value = 0.0;
    uint32_t byte_offset = 0u;

    if (!expect_atom(invocation, LLFPL_ATOM_OPEN_PARENTHESIS, "write_offset")) {
        return 0.0;
    }

    arena = consume_arena_operand(invocation);
    if (arena == NULL) {
        return 0.0;
    }

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "write_offset")) {
        return 0.0;
    }

    offset_value = evaluate_argument(invocation, first_scratch, (uint16_t)(first_scratch + 1u));

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "write_offset")) {
        return 0.0;
    }

    stored_value = evaluate_argument(
        invocation, (uint16_t)(first_scratch + 1u), (uint16_t)(first_scratch + 2u));

    if (!expect_atom(invocation, LLFPL_ATOM_CLOSE_PARENTHESIS, "write_offset")) {
        return 0.0;
    }

    if (!convert_to_byte_offset(invocation, offset_value, &byte_offset)) {
        return 0.0;
    }

    if (!llfpl_status_is_ok(llfpl_memory_arena_store_double(arena, byte_offset, stored_value))) {
        report_error(invocation, "write_offset writes past the end of the arena");
        return 0.0;
    }

    return publish_result(invocation, stored_value);
}

/* read_offset(ArenaName, byte_offset) -- yields the value stored at the offset. */
static double builtin_read_offset(const LlfplBuiltinInvocation *invocation) {
    const uint16_t first_scratch = (*invocation).allocation.first_scratch_register_index;
    LlfplMemoryArena *arena = NULL;
    double offset_value = 0.0;
    double loaded_value = 0.0;
    uint32_t byte_offset = 0u;

    if (!expect_atom(invocation, LLFPL_ATOM_OPEN_PARENTHESIS, "read_offset")) {
        return 0.0;
    }

    arena = consume_arena_operand(invocation);
    if (arena == NULL) {
        return 0.0;
    }

    if (!expect_atom(invocation, LLFPL_ATOM_ARGUMENT_SEPARATOR, "read_offset")) {
        return 0.0;
    }

    offset_value = evaluate_argument(invocation, first_scratch, (uint16_t)(first_scratch + 1u));

    if (!expect_atom(invocation, LLFPL_ATOM_CLOSE_PARENTHESIS, "read_offset")) {
        return 0.0;
    }

    if (!convert_to_byte_offset(invocation, offset_value, &byte_offset)) {
        return 0.0;
    }

    if (!llfpl_status_is_ok(llfpl_memory_arena_load_double(arena, byte_offset, &loaded_value))) {
        report_error(invocation, "read_offset reads past the end of the arena");
        return 0.0;
    }

    return publish_result(invocation, loaded_value);
}

/* ---- Dispatch table -------------------------------------------------------- */

/*
 * The table is the language's list of special forms. Its order is the order the
 * command line help prints, and nothing else depends on it, so entries may be
 * added anywhere.
 */
static const LlfplBuiltinDescriptor builtin_descriptors[] = {
    {"Branch",
     6u,
     builtin_branch,
     "Branch(selector, consequent, alternative)",
     "Branchless selection; both arms are evaluated"},

    {"Loop",
     4u,
     builtin_loop,
     "Loop(iteration_count, TemplateName)",
     "Bounded repetition; passes the iteration index"},

    {"write_offset",
     12u,
     builtin_write_offset,
     "write_offset(ArenaName, byte_offset, value)",
     "Stores a value into an arena and yields it"},

    {"read_offset",
     11u,
     builtin_read_offset,
     "read_offset(ArenaName, byte_offset)",
     "Loads a value from an arena"},
};

const LlfplBuiltinDescriptor *llfpl_builtin_lookup_span(const char *verb_text, size_t verb_length) {
    size_t descriptor_index = 0u;

    if (verb_text == NULL) {
        return NULL;
    }

    for (descriptor_index = 0u; descriptor_index < LLFPL_ARRAY_LENGTH(builtin_descriptors);
         descriptor_index++) {
        const LlfplBuiltinDescriptor *descriptor = builtin_descriptors + descriptor_index;

        /*
         * The length test comes first and rejects almost every candidate in a
         * single integer comparison, so the string comparison runs only for a
         * verb that could actually match.
         */
        if ((*descriptor).verb_name_length == verb_length &&
            memcmp((*descriptor).verb_name, verb_text, verb_length) == 0) {
            return descriptor;
        }
    }

    return NULL;
}

size_t llfpl_builtin_count(void) {
    return LLFPL_ARRAY_LENGTH(builtin_descriptors);
}

const LlfplBuiltinDescriptor *llfpl_builtin_descriptor_at(size_t descriptor_index) {
    if (descriptor_index >= LLFPL_ARRAY_LENGTH(builtin_descriptors)) {
        return NULL;
    }

    return builtin_descriptors + descriptor_index;
}

int llfpl_verb_is_reserved(const char *verb_text, size_t verb_length) {
    if (llfpl_builtin_lookup_span(verb_text, verb_length) != NULL) {
        return 1;
    }

    return llfpl_primitive_operation_from_span(verb_text, verb_length) != LLFPL_PRIMITIVE_NONE;
}

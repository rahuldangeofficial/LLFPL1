/*
 * expression_evaluator.c -- Recursive descent from source text to values.
 *
 * Name resolution order, applied to every identifier in expression position:
 *
 *     1. built-in special form   Branch, Loop, read_offset, write_offset
 *     2. primitive operation     plus, minus, multiply, ...
 *     3. template parameter      a name bound by the enclosing activation
 *     4. template                a Map declaration
 *     5. identity                a global Identity binding
 *
 * The order is fixed and total, and it is unambiguous by construction: steps
 * one and two name reserved verbs that no declaration is permitted to shadow,
 * so the only genuine scoping decision left is that a parameter takes
 * precedence over a global of the same name -- the rule every language with
 * lexical scope already uses.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/runtime/expression_evaluator.h"

#include "llfpl/core/numeric_conversion.h"
#include "llfpl/runtime/builtin_operation.h"
#include "llfpl/runtime/primitive_reduction.h"
#include "llfpl/runtime/symbol_table.h"

/* ---- Register mapping ---------------------------------------------------- */

/*
 * Maps a logical register index onto a physical one.
 *
 * The modulo is the only sanctioned way to exceed the physical register count.
 * It is safe because an activation whose logical index wraps also spills the
 * physical register it lands on, so no live value is ever overwritten.
 */
static uint16_t physical_register_for(const LlfplExecutionContext *context,
                                      uint16_t logical_register_index) {
    const uint16_t register_count = llfpl_vector_register_file_count((*context).register_file);

    if (register_count == 0u) {
        return 0u;
    }

    return (uint16_t)(logical_register_index % register_count);
}

static int logical_register_exceeds_bank(const LlfplExecutionContext *context,
                                         uint16_t logical_register_index) {
    return logical_register_index >= llfpl_vector_register_file_count((*context).register_file);
}

/* ---- Shared parsing helpers ---------------------------------------------- */

int llfpl_expression_evaluator_expect_atom(LlfplExecutionContext *context,
                                           LlfplLexicalScanner *scanner,
                                           LlfplAtomKind expected_kind,
                                           const char *construct_description) {
    LlfplAtom atom;

    /*
     * An expectation raised while the evaluation is already unwinding describes
     * a consequence, not a cause. Reporting it would bury the one diagnostic
     * that identifies the actual defect underneath the parse failures that
     * follow from abandoning the expression halfway through. The scanner is
     * left untouched as well, since its position no longer means anything.
     */
    if (llfpl_execution_context_has_failed(context)) {
        return 0;
    }

    atom = llfpl_lexical_scanner_next(scanner);

    if (atom.kind == expected_kind) {
        return 1;
    }

    llfpl_diagnostic_report_error((*context).diagnostic_reporter,
                                  llfpl_atom_source_location(scanner, atom),
                                  "expected %s in %s but found %s",
                                  llfpl_atom_kind_describe(expected_kind),
                                  construct_description,
                                  llfpl_atom_kind_describe(atom.kind));
    return 0;
}

/* ---- Expression forms ---------------------------------------------------- */

/* A numeric literal: the only atom that carries its value in its own text. */
static double evaluate_numeric_literal(LlfplExecutionContext *context,
                                       LlfplLexicalScanner *scanner,
                                       LlfplAtom atom,
                                       uint16_t physical_destination) {
    double literal_value = 0.0;

    if (!llfpl_status_is_ok(
            llfpl_numeric_parse_double(atom.text_begin, atom.text_length, &literal_value))) {
        llfpl_diagnostic_report_error((*context).diagnostic_reporter,
                                      llfpl_atom_source_location(scanner, atom),
                                      "malformed numeric literal");
        return 0.0;
    }

    llfpl_vector_register_file_write((*context).register_file, physical_destination, literal_value);
    return literal_value;
}

/*
 * A primitive application: verb '(' left ',' right ')'.
 *
 * The two operands are given adjacent scratch registers so that both remain
 * live and individually inspectable while the reduction is charged, which is
 * what makes the register bank a faithful picture of the computation rather
 * than a scratchpad that only happens to hold the last value written.
 */
static double evaluate_primitive_application(LlfplExecutionContext *context,
                                             LlfplLexicalScanner *scanner,
                                             const LlfplActivationFrame *frame,
                                             LlfplRegisterAllocation allocation,
                                             LlfplPrimitiveOperation operation,
                                             uint16_t physical_destination) {
    const uint16_t first_scratch = allocation.first_scratch_register_index;
    double left_operand = 0.0;
    double right_operand = 0.0;
    double reduced_value = 0.0;

    if (!llfpl_expression_evaluator_expect_atom(
            context, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "a primitive application")) {
        return 0.0;
    }

    left_operand = llfpl_expression_evaluator_evaluate(
        context,
        scanner,
        frame,
        llfpl_register_allocation_make(first_scratch, (uint16_t)(first_scratch + 1u)));

    if (!llfpl_expression_evaluator_expect_atom(
            context, scanner, LLFPL_ATOM_ARGUMENT_SEPARATOR, "a primitive application")) {
        return 0.0;
    }

    right_operand = llfpl_expression_evaluator_evaluate(
        context,
        scanner,
        frame,
        llfpl_register_allocation_make((uint16_t)(first_scratch + 1u),
                                       (uint16_t)(first_scratch + 2u)));

    if (!llfpl_expression_evaluator_expect_atom(
            context, scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, "a primitive application")) {
        return 0.0;
    }

    reduced_value = llfpl_primitive_apply(operation, left_operand, right_operand);

    llfpl_vector_register_file_write((*context).register_file, physical_destination, reduced_value);
    llfpl_vector_register_file_charge_cycles((*context).register_file,
                                             llfpl_primitive_nominal_latency(operation));

    return reduced_value;
}

/*
 * A template invocation: name '(' argument ( ',' argument )* ')'.
 *
 * Arguments are evaluated in the caller's frame, then bound into a fresh frame
 * on this activation's stack, and the body is evaluated from the snapshot the
 * declaration recorded. Call-by-value with no allocation anywhere in the path.
 */
static double evaluate_template_invocation(LlfplExecutionContext *context,
                                           LlfplLexicalScanner *scanner,
                                           const LlfplActivationFrame *caller_frame,
                                           LlfplRegisterAllocation allocation,
                                           const LlfplTemplateDefinition *invoked_template) {
    const uint16_t first_scratch = allocation.first_scratch_register_index;
    LlfplActivationFrame callee_frame;
    LlfplLexicalScanner body_scanner;
    uint8_t argument_index = 0u;

    llfpl_activation_frame_initialise(&callee_frame, invoked_template);

    if (!llfpl_expression_evaluator_expect_atom(
            context, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "a template invocation")) {
        return 0.0;
    }

    for (argument_index = 0u; argument_index < (*invoked_template).parameter_count;
         argument_index++) {
        if (argument_index > 0u &&
            !llfpl_expression_evaluator_expect_atom(
                context, scanner, LLFPL_ATOM_ARGUMENT_SEPARATOR, "a template invocation")) {
            return 0.0;
        }

        *(callee_frame.argument_values + argument_index) = llfpl_expression_evaluator_evaluate(
            context,
            scanner,
            caller_frame,
            llfpl_register_allocation_make(first_scratch, (uint16_t)(first_scratch + 1u)));
    }

    callee_frame.argument_count = (*invoked_template).parameter_count;

    if (!llfpl_expression_evaluator_expect_atom(
            context, scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, "a template invocation")) {
        return 0.0;
    }

    llfpl_lexical_scanner_snapshot(&(*invoked_template).body_snapshot, &body_scanner);

    return llfpl_expression_evaluator_evaluate(context, &body_scanner, &callee_frame, allocation);
}

/* Resolves an identifier that names no verb: parameter, template, or identity. */
static double evaluate_named_value(LlfplExecutionContext *context,
                                   LlfplLexicalScanner *scanner,
                                   const LlfplActivationFrame *frame,
                                   LlfplRegisterAllocation allocation,
                                   LlfplAtom atom,
                                   uint16_t physical_destination) {
    const LlfplTemplateDefinition *invoked_template = NULL;
    LlfplValue resolved_identity;
    double resolved_value = 0.0;

    /* A parameter of the enclosing activation shadows anything global. */
    if (llfpl_activation_frame_resolve_span(
            frame, atom.text_begin, atom.text_length, &resolved_value)) {
        llfpl_vector_register_file_write(
            (*context).register_file, physical_destination, resolved_value);
        return resolved_value;
    }

    invoked_template = llfpl_template_segment_lookup_span(
        (*context).template_segment, atom.text_begin, atom.text_length);
    if (invoked_template != NULL) {
        return evaluate_template_invocation(context, scanner, frame, allocation, invoked_template);
    }

    if (llfpl_status_is_ok(llfpl_symbol_table_resolve_span(
            (*context).symbol_table, atom.text_begin, atom.text_length, &resolved_identity))) {
        resolved_value = (resolved_identity.kind == LLFPL_VALUE_DOUBLE)
                             ? resolved_identity.payload.as_double
                             : 0.0;

        llfpl_vector_register_file_write(
            (*context).register_file, physical_destination, resolved_value);
        return resolved_value;
    }

    llfpl_diagnostic_report_error((*context).diagnostic_reporter,
                                  llfpl_atom_source_location(scanner, atom),
                                  "'%.*s' names no parameter, template or identity",
                                  (int)atom.text_length,
                                  atom.text_begin);
    return 0.0;
}

/* ---- Dispatch ------------------------------------------------------------ */

/*
 * Evaluates one expression, assuming the depth guard and the spill discipline
 * have already been applied by the public entry point below.
 */
static double evaluate_one_expression(LlfplExecutionContext *context,
                                      LlfplLexicalScanner *scanner,
                                      const LlfplActivationFrame *frame,
                                      LlfplRegisterAllocation allocation,
                                      uint16_t physical_destination) {
    const LlfplAtom atom = llfpl_lexical_scanner_next(scanner);
    const LlfplBuiltinDescriptor *builtin_descriptor = NULL;
    LlfplPrimitiveOperation primitive_operation = LLFPL_PRIMITIVE_NONE;

    if (atom.kind == LLFPL_ATOM_NUMERIC_LITERAL) {
        return evaluate_numeric_literal(context, scanner, atom, physical_destination);
    }

    if (atom.kind != LLFPL_ATOM_IDENTIFIER) {
        llfpl_diagnostic_report_error((*context).diagnostic_reporter,
                                      llfpl_atom_source_location(scanner, atom),
                                      "expected an expression but found %s",
                                      llfpl_atom_kind_describe(atom.kind));
        return 0.0;
    }

    builtin_descriptor = llfpl_builtin_lookup_span(atom.text_begin, atom.text_length);
    if (builtin_descriptor != NULL) {
        LlfplBuiltinInvocation invocation;

        invocation.context = context;
        invocation.scanner = scanner;
        invocation.frame = frame;
        invocation.allocation = allocation;
        invocation.call_site = llfpl_atom_source_location(scanner, atom);

        return (*builtin_descriptor).handler(&invocation);
    }

    primitive_operation = llfpl_primitive_operation_from_span(atom.text_begin, atom.text_length);
    if (primitive_operation != LLFPL_PRIMITIVE_NONE) {
        return evaluate_primitive_application(
            context, scanner, frame, allocation, primitive_operation, physical_destination);
    }

    return evaluate_named_value(context, scanner, frame, allocation, atom, physical_destination);
}

double llfpl_expression_evaluator_evaluate(LlfplExecutionContext *context,
                                           LlfplLexicalScanner *scanner,
                                           const LlfplActivationFrame *frame,
                                           LlfplRegisterAllocation allocation) {
    uint16_t physical_destination = 0u;
    int destination_was_spilled = 0;
    double evaluated_value = 0.0;

    if (context == NULL || scanner == NULL) {
        return 0.0;
    }

    /* Abandon the rest of the expression once anything has gone wrong. */
    if (llfpl_execution_context_has_failed(context)) {
        return 0.0;
    }

    if ((*context).current_evaluation_depth >= (*context).maximum_evaluation_depth) {
        llfpl_diagnostic_report_error(
            (*context).diagnostic_reporter,
            llfpl_source_location_make((*scanner).module_path, (*scanner).line_number, 0u),
            "expression nesting exceeds the depth limit of %u",
            (unsigned)(*context).maximum_evaluation_depth);
        return 0.0;
    }

    physical_destination = physical_register_for(context, allocation.destination_register_index);

    /*
     * Preserve whatever the physical register already holds if this activation
     * only reached it by wrapping. The occupant belongs to an outer, still-live
     * activation, and reusing it without saving would be the silent aliasing
     * this design exists to avoid.
     */
    if (logical_register_exceeds_bank(context, allocation.destination_register_index)) {
        destination_was_spilled =
            llfpl_vector_register_file_push_spill((*context).register_file, physical_destination);

        if (!destination_was_spilled) {
            llfpl_diagnostic_report_error(
                (*context).diagnostic_reporter,
                llfpl_source_location_make((*scanner).module_path, (*scanner).line_number, 0u),
                "register spill stack exhausted");
            return 0.0;
        }
    }

    (*context).current_evaluation_depth++;
    evaluated_value =
        evaluate_one_expression(context, scanner, frame, allocation, physical_destination);
    (*context).current_evaluation_depth--;

    if (destination_was_spilled) {
        llfpl_vector_register_file_pop_spill((*context).register_file, physical_destination);
    }

    return evaluated_value;
}

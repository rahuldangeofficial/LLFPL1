/*
 * template_definition.c -- Parsing of Map declarations and indexed lookup of
 *                          the resulting templates.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/frontend/template_definition.h"

#include <string.h>

#include "llfpl/core/identifier.h"

/* ---- Name index ---------------------------------------------------------- */

/*
 * The index is open-addressed with linear probing. Linear probing is the right
 * collision strategy at this occupancy because a probe sequence walks
 * consecutive uint16_t slots -- thirty-two of them share a cache line -- so a
 * collision costs an increment rather than a cache miss.
 */

static uint32_t index_slot_for_hash(uint32_t name_hash) {
    return name_hash & (LLFPL_TEMPLATE_INDEX_SLOT_COUNT - 1u);
}

static uint32_t index_next_slot(uint32_t slot) {
    return (slot + 1u) & (LLFPL_TEMPLATE_INDEX_SLOT_COUNT - 1u);
}

static void
index_insert(LlfplTemplateSegment *segment, uint32_t name_hash, uint16_t definition_index) {
    uint32_t slot = index_slot_for_hash(name_hash);

    while (*((*segment).name_index + slot) != LLFPL_TEMPLATE_INDEX_EMPTY_SLOT) {
        slot = index_next_slot(slot);
    }

    *((*segment).name_index + slot) = definition_index;
}

/*
 * Shared lookup body. The name is compared as an unterminated span so that a
 * lookup driven by an atom needs no scratch buffer and no copy.
 */
static const LlfplTemplateDefinition *lookup_by_span(const LlfplTemplateSegment *segment,
                                                     const char *template_name,
                                                     size_t template_name_length) {
    uint32_t slot = index_slot_for_hash(llfpl_identifier_hash(template_name, template_name_length));
    uint32_t probe_count = 0u;

    for (probe_count = 0u; probe_count < LLFPL_TEMPLATE_INDEX_SLOT_COUNT; probe_count++) {
        const uint16_t definition_index = *((*segment).name_index + slot);
        const LlfplTemplateDefinition *definition = NULL;

        /* An empty slot terminates the probe sequence: the name is absent. */
        if (definition_index == LLFPL_TEMPLATE_INDEX_EMPTY_SLOT) {
            return NULL;
        }

        definition = (*segment).definitions + definition_index;

        if (llfpl_identifier_equals_span((*definition).name, template_name, template_name_length)) {
            return definition;
        }

        slot = index_next_slot(slot);
    }

    return NULL;
}

/* ---- Declaration parsing ------------------------------------------------- */

/*
 * Consumes exactly one complete expression.
 *
 * An expression is either a single atom, or an identifier followed by a
 * parenthesised argument list, in which case the whole list is consumed by
 * tracking parenthesis depth. This is what lets the parser find where a
 * template body ends without building any representation of it.
 */
static LlfplStatus skip_one_expression(LlfplLexicalScanner *scanner,
                                       LlfplDiagnosticReporter *reporter) {
    const LlfplAtom leading_atom = llfpl_lexical_scanner_next(scanner);
    uint32_t open_parenthesis_depth = 0u;

    if (leading_atom.kind == LLFPL_ATOM_END_OF_SOURCE ||
        leading_atom.kind == LLFPL_ATOM_UNRECOGNISED) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_atom_source_location(scanner, leading_atom),
                                      "expected an expression but found %s",
                                      llfpl_atom_kind_describe(leading_atom.kind));
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    /* A literal, or a bare name used as a value, is already complete. */
    if (leading_atom.kind != LLFPL_ATOM_IDENTIFIER ||
        llfpl_lexical_scanner_peek(scanner).kind != LLFPL_ATOM_OPEN_PARENTHESIS) {
        return LLFPL_STATUS_OK;
    }

    do {
        const LlfplAtom atom = llfpl_lexical_scanner_next(scanner);

        if (atom.kind == LLFPL_ATOM_OPEN_PARENTHESIS) {
            open_parenthesis_depth++;
        } else if (atom.kind == LLFPL_ATOM_CLOSE_PARENTHESIS) {
            open_parenthesis_depth--;
        } else if (atom.kind == LLFPL_ATOM_END_OF_SOURCE) {
            llfpl_diagnostic_report_error(
                reporter, llfpl_atom_source_location(scanner, atom), "unterminated argument list");
            return LLFPL_STATUS_SYNTAX_ERROR;
        }
    } while (open_parenthesis_depth > 0u);

    return LLFPL_STATUS_OK;
}

/* Consumes an atom of the expected kind, reporting a precise message if absent. */
static LlfplStatus expect_atom_kind(LlfplLexicalScanner *scanner,
                                    LlfplDiagnosticReporter *reporter,
                                    LlfplAtomKind expected_kind) {
    const LlfplAtom atom = llfpl_lexical_scanner_next(scanner);

    if (atom.kind != expected_kind) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_atom_source_location(scanner, atom),
                                      "expected %s in Map declaration but found %s",
                                      llfpl_atom_kind_describe(expected_kind),
                                      llfpl_atom_kind_describe(atom.kind));
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    return LLFPL_STATUS_OK;
}

/* Copies an atom's text into an identifier buffer, reporting any refusal. */
static LlfplStatus assign_identifier_from_atom(char *destination_buffer,
                                               LlfplAtom atom,
                                               LlfplLexicalScanner *scanner,
                                               LlfplDiagnosticReporter *reporter,
                                               const char *role_description) {
    const LlfplStatus status =
        llfpl_identifier_assign(destination_buffer, atom.text_begin, atom.text_length);

    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_atom_source_location(scanner, atom),
                                      "%s must be at most %d characters long",
                                      role_description,
                                      LLFPL_MAXIMUM_IDENTIFIER_LENGTH);
    }

    return status;
}

/* ---- Public interface ---------------------------------------------------- */

void llfpl_template_segment_initialise(LlfplTemplateSegment *segment) {
    uint32_t slot = 0u;

    if (segment == NULL) {
        return;
    }

    (*segment).definition_count = 0u;

    for (slot = 0u; slot < LLFPL_TEMPLATE_INDEX_SLOT_COUNT; slot++) {
        *((*segment).name_index + slot) = LLFPL_TEMPLATE_INDEX_EMPTY_SLOT;
    }
}

/*
 * Rejects a declaration that would shadow a name the evaluator already
 * resolves. Applied to the template name and to every parameter, because either
 * would make the resolution order observable in a way a reader should never
 * have to reason about.
 */
static LlfplStatus reject_reserved_name(LlfplReservedNamePredicate name_is_reserved,
                                        LlfplAtom atom,
                                        LlfplLexicalScanner *scanner,
                                        LlfplDiagnosticReporter *reporter,
                                        const char *role_description) {
    if (name_is_reserved == NULL || !name_is_reserved(atom.text_begin, atom.text_length)) {
        return LLFPL_STATUS_OK;
    }

    llfpl_diagnostic_report_error(reporter,
                                  llfpl_atom_source_location(scanner, atom),
                                  "'%.*s' is reserved by the language and cannot be used as %s",
                                  (int)atom.text_length,
                                  atom.text_begin,
                                  role_description);
    return LLFPL_STATUS_SEMANTIC_ERROR;
}

LlfplStatus llfpl_template_segment_define(LlfplTemplateSegment *segment,
                                          LlfplLexicalScanner *scanner,
                                          LlfplDiagnosticReporter *reporter,
                                          LlfplReservedNamePredicate name_is_reserved) {
    LlfplTemplateDefinition *definition = NULL;
    LlfplAtom name_atom;
    LlfplStatus status = LLFPL_STATUS_OK;

    if (segment == NULL || scanner == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if ((*segment).definition_count >= LLFPL_TEMPLATE_SEGMENT_CAPACITY) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_source_location_unknown(),
                                      "template segment is full at %u definitions",
                                      (unsigned)LLFPL_TEMPLATE_SEGMENT_CAPACITY);
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    definition = (*segment).definitions + (*segment).definition_count;
    memset(definition, 0, sizeof(*definition));

    status = expect_atom_kind(scanner, reporter, LLFPL_ATOM_OPEN_PARENTHESIS);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    name_atom = llfpl_lexical_scanner_next(scanner);
    if (name_atom.kind != LLFPL_ATOM_IDENTIFIER) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "expected a template name but found %s",
                                      llfpl_atom_kind_describe(name_atom.kind));
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    status =
        reject_reserved_name(name_is_reserved, name_atom, scanner, reporter, "a template name");
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    status = assign_identifier_from_atom(
        (*definition).name, name_atom, scanner, reporter, "a template name");
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    if (llfpl_template_segment_lookup(segment, (*definition).name) != NULL) {
        llfpl_diagnostic_report_error(reporter,
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "template '%s' is already defined",
                                      (*definition).name);
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    status = expect_atom_kind(scanner, reporter, LLFPL_ATOM_ARGUMENT_SEPARATOR);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    /*
     * Parameters and the body are separated by lookahead, not by counting. An
     * identifier that is followed by a separator is another parameter; anything
     * else begins the body, which is always the final argument.
     */
    while (1) {
        const LlfplAtom candidate_atom = llfpl_lexical_scanner_peek(scanner);
        const LlfplAtom following_atom = llfpl_lexical_scanner_peek_second(scanner);

        if (candidate_atom.kind != LLFPL_ATOM_IDENTIFIER ||
            following_atom.kind != LLFPL_ATOM_ARGUMENT_SEPARATOR) {
            break;
        }

        if ((*definition).parameter_count >= LLFPL_TEMPLATE_PARAMETER_CAPACITY) {
            llfpl_diagnostic_report_error(reporter,
                                          llfpl_atom_source_location(scanner, candidate_atom),
                                          "template '%s' declares more than %u parameters",
                                          (*definition).name,
                                          (unsigned)LLFPL_TEMPLATE_PARAMETER_CAPACITY);
            return LLFPL_STATUS_CAPACITY_EXCEEDED;
        }

        status = reject_reserved_name(
            name_is_reserved, candidate_atom, scanner, reporter, "a parameter name");
        if (!llfpl_status_is_ok(status)) {
            return status;
        }

        status = assign_identifier_from_atom(
            *((*definition).parameter_names + (*definition).parameter_count),
            candidate_atom,
            scanner,
            reporter,
            "a parameter name");
        if (!llfpl_status_is_ok(status)) {
            return status;
        }

        (*definition).parameter_count++;

        (void)llfpl_lexical_scanner_next(scanner); /* the parameter name    */
        (void)llfpl_lexical_scanner_next(scanner); /* the separator after it */
    }

    /* Record the coordinate of the body before consuming a single atom of it. */
    llfpl_lexical_scanner_snapshot(scanner, &(*definition).body_snapshot);

    status = skip_one_expression(scanner, reporter);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    status = expect_atom_kind(scanner, reporter, LLFPL_ATOM_CLOSE_PARENTHESIS);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    index_insert(segment,
                 llfpl_identifier_hash((*definition).name, strlen((*definition).name)),
                 (uint16_t)(*segment).definition_count);
    (*segment).definition_count++;

    llfpl_diagnostic_report_trace(reporter,
                                  "declared template %s with %u parameter(s)",
                                  (*definition).name,
                                  (unsigned)(*definition).parameter_count);

    return LLFPL_STATUS_OK;
}

const LlfplTemplateDefinition *llfpl_template_segment_lookup(const LlfplTemplateSegment *segment,
                                                             const char *template_name) {
    if (segment == NULL || template_name == NULL) {
        return NULL;
    }

    return llfpl_template_segment_lookup_span(segment, template_name, strlen(template_name));
}

const LlfplTemplateDefinition *llfpl_template_segment_lookup_span(
    const LlfplTemplateSegment *segment, const char *template_name, size_t template_name_length) {
    if (segment == NULL || template_name == NULL) {
        return NULL;
    }

    /* A name that cannot be stored cannot have been stored. */
    if (!llfpl_identifier_length_is_valid(template_name_length)) {
        return NULL;
    }

    return lookup_by_span(segment, template_name, template_name_length);
}

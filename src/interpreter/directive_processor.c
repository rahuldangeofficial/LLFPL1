/*
 * directive_processor.c -- The five top-level directives of LLFPL.
 *
 *     Identity(Name, expression)          bind an immutable global
 *     Slab(Name, byte_capacity)           reserve an aligned data arena
 *     Map(Name, parameters..., body)      declare a reusable expression
 *     Require(module/path.LLFPL)          load another module once
 *     Commit(expression)                  evaluate and report a result
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/interpreter/directive_processor.h"

#include <math.h>
#include <string.h>

#include "llfpl/core/filesystem_path.h"
#include "llfpl/core/identifier.h"
#include "llfpl/core/monotonic_clock.h"
#include "llfpl/core/numeric_conversion.h"
#include "llfpl/frontend/lexical_scanner.h"
#include "llfpl/runtime/builtin_operation.h"
#include "llfpl/runtime/expression_evaluator.h"

/* Registers used for a top-level evaluation: result in zero, scratch from one. */
#define LLFPL_TOP_LEVEL_RESULT_REGISTER        0u
#define LLFPL_TOP_LEVEL_FIRST_SCRATCH_REGISTER 1u

/* ---- Directive table types ------------------------------------------------ */

typedef LlfplStatus (*LlfplDirectiveHandler)(LlfplModuleExecutionEnvironment *environment,
                                             LlfplLexicalScanner *scanner,
                                             LlfplAtom keyword_atom);

typedef struct {
    const char *keyword;
    size_t keyword_length;
    LlfplDirectiveHandler handler;
    const char *signature_summary;
    const char *purpose_summary;
} LlfplDirectiveDescriptor;

/* Forward declaration; the table is defined after the handlers it names. */
static const LlfplDirectiveDescriptor *lookup_directive(const char *keyword_text,
                                                        size_t keyword_length);

/* ---- Shared helpers -------------------------------------------------------- */

static LlfplDiagnosticReporter *reporter_of(const LlfplModuleExecutionEnvironment *environment) {
    return (*(*environment).execution_context).diagnostic_reporter;
}

static int expect_atom(LlfplModuleExecutionEnvironment *environment,
                       LlfplLexicalScanner *scanner,
                       LlfplAtomKind expected_kind,
                       const char *construct_description) {
    return llfpl_expression_evaluator_expect_atom(
        (*environment).execution_context, scanner, expected_kind, construct_description);
}

/* Evaluates one top-level expression into the result register. */
static double evaluate_top_level_expression(LlfplModuleExecutionEnvironment *environment,
                                            LlfplLexicalScanner *scanner) {
    return llfpl_expression_evaluator_evaluate(
        (*environment).execution_context,
        scanner,
        NULL,
        llfpl_register_allocation_make(LLFPL_TOP_LEVEL_RESULT_REGISTER,
                                       LLFPL_TOP_LEVEL_FIRST_SCRATCH_REGISTER));
}

/*
 * Reads the identifier a declaration is naming, rejecting a name that the
 * language reserves or that is too long to store.
 */
static LlfplStatus consume_declared_name(LlfplModuleExecutionEnvironment *environment,
                                         LlfplLexicalScanner *scanner,
                                         const char *directive_keyword,
                                         LlfplAtom *name_atom_out) {
    const LlfplAtom name_atom = llfpl_lexical_scanner_next(scanner);

    *name_atom_out = name_atom;

    if (name_atom.kind != LLFPL_ATOM_IDENTIFIER) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "%s expects a name but found %s",
                                      directive_keyword,
                                      llfpl_atom_kind_describe(name_atom.kind));
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    if (llfpl_verb_is_reserved(name_atom.text_begin, name_atom.text_length) ||
        llfpl_directive_name_is_reserved(name_atom.text_begin, name_atom.text_length)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "'%.*s' is reserved by the language",
                                      (int)name_atom.text_length,
                                      name_atom.text_begin);
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    if (!llfpl_identifier_length_is_valid(name_atom.text_length)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "a name must be between 1 and %d characters long",
                                      LLFPL_MAXIMUM_IDENTIFIER_LENGTH);
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    return LLFPL_STATUS_OK;
}

/* ---- Identity -------------------------------------------------------------- */

/*
 * Identity(Name, expression)
 *
 * The value is a full expression, not merely a literal, so an identity may be
 * derived from the ones already bound. Binding is immutable: a second Identity
 * for the same name is an error rather than a redefinition, which is what makes
 * a name's meaning readable from its single point of declaration.
 */
static LlfplStatus directive_identity(LlfplModuleExecutionEnvironment *environment,
                                      LlfplLexicalScanner *scanner,
                                      LlfplAtom keyword_atom) {
    LlfplAtom name_atom;
    LlfplStatus status = LLFPL_STATUS_OK;
    double bound_value = 0.0;

    LLFPL_UNUSED_PARAMETER(keyword_atom);

    if (!expect_atom(environment, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "Identity")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    status = consume_declared_name(environment, scanner, "Identity", &name_atom);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    if (!expect_atom(environment, scanner, LLFPL_ATOM_ARGUMENT_SEPARATOR, "Identity")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    bound_value = evaluate_top_level_expression(environment, scanner);

    if (!expect_atom(environment, scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, "Identity")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    status = llfpl_symbol_table_bind_span((*(*environment).execution_context).symbol_table,
                                          name_atom.text_begin,
                                          name_atom.text_length,
                                          llfpl_value_from_double(bound_value));

    if (status == LLFPL_STATUS_SEMANTIC_ERROR) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "identity '%.*s' is already bound and cannot be rebound",
                                      (int)name_atom.text_length,
                                      name_atom.text_begin);
        return status;
    }

    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "cannot bind identity: %s",
                                      llfpl_status_describe(status));
        return status;
    }

    llfpl_diagnostic_report_trace(reporter_of(environment),
                                  "bound identity %.*s",
                                  (int)name_atom.text_length,
                                  name_atom.text_begin);
    return LLFPL_STATUS_OK;
}

/* ---- Slab ------------------------------------------------------------------ */

/*
 * Slab(Name, byte_capacity)
 *
 * Reserves a cache-line-aligned arena. The capacity is an expression so that it
 * can be written in terms of identities -- Slab(Ring, multiply(PageSize, 4)) --
 * rather than as a magic number repeated wherever the size matters.
 */
static LlfplStatus directive_slab(LlfplModuleExecutionEnvironment *environment,
                                  LlfplLexicalScanner *scanner,
                                  LlfplAtom keyword_atom) {
    LlfplAtom name_atom;
    LlfplStatus status = LLFPL_STATUS_OK;
    double capacity_value = 0.0;

    LLFPL_UNUSED_PARAMETER(keyword_atom);

    if (!expect_atom(environment, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "Slab")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    status = consume_declared_name(environment, scanner, "Slab", &name_atom);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    if (!expect_atom(environment, scanner, LLFPL_ATOM_ARGUMENT_SEPARATOR, "Slab")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    capacity_value = evaluate_top_level_expression(environment, scanner);

    if (!expect_atom(environment, scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, "Slab")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    if (!isfinite(capacity_value) || capacity_value < 1.0 ||
        capacity_value > (double)LLFPL_MEMORY_ARENA_MAXIMUM_SIZE_IN_BYTES ||
        floor(capacity_value) != capacity_value) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "Slab capacity must be a whole number of bytes "
                                      "between 1 and %u",
                                      (unsigned)LLFPL_MEMORY_ARENA_MAXIMUM_SIZE_IN_BYTES);
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    {
        char arena_name[LLFPL_IDENTIFIER_BUFFER_CAPACITY];

        status = llfpl_identifier_assign(arena_name, name_atom.text_begin, name_atom.text_length);
        if (llfpl_status_is_ok(status)) {
            status = llfpl_memory_arena_registry_reserve(
                (*(*environment).execution_context).arena_registry,
                arena_name,
                (uint32_t)capacity_value);
        }
    }

    if (status == LLFPL_STATUS_SEMANTIC_ERROR) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "arena '%.*s' is already reserved",
                                      (int)name_atom.text_length,
                                      name_atom.text_begin);
        return status;
    }

    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, name_atom),
                                      "cannot reserve arena: %s",
                                      llfpl_status_describe(status));
        return status;
    }

    llfpl_diagnostic_report_trace(reporter_of(environment),
                                  "reserved %u byte arena %.*s",
                                  (unsigned)capacity_value,
                                  (int)name_atom.text_length,
                                  name_atom.text_begin);
    return LLFPL_STATUS_OK;
}

/* ---- Map -------------------------------------------------------------------- */

static LlfplStatus directive_map(LlfplModuleExecutionEnvironment *environment,
                                 LlfplLexicalScanner *scanner,
                                 LlfplAtom keyword_atom) {
    LLFPL_UNUSED_PARAMETER(keyword_atom);

    /*
     * The reserved-name rule is injected rather than imported: the frontend
     * applies it without ever learning what the runtime's vocabulary contains.
     */
    /* The declaration reports its own trace, since only it knows the name. */
    return llfpl_template_segment_define((*(*environment).execution_context).template_segment,
                                         scanner,
                                         reporter_of(environment),
                                         llfpl_verb_is_reserved);
}

/* ---- Require ----------------------------------------------------------------- */

/*
 * Reads the module specification as the verbatim source text between the
 * parentheses.
 *
 * A path is not a sequence of atoms -- separators and dots are not operators
 * here -- so reconstructing one by concatenating tokens would silently mangle
 * any path the lexer happens to split differently. Taking the raw span keeps
 * the specification exactly as it was written.
 */
static LlfplStatus consume_module_specification(LlfplModuleExecutionEnvironment *environment,
                                                LlfplLexicalScanner *scanner,
                                                char *specification_out,
                                                size_t buffer_capacity) {
    const char *specification_begin = NULL;
    const char *specification_end = NULL;
    size_t specification_length = 0u;

    if (!expect_atom(environment, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "Require")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    specification_begin = (*scanner).cursor;

    while (1) {
        const LlfplAtom atom = llfpl_lexical_scanner_next(scanner);

        if (atom.kind == LLFPL_ATOM_CLOSE_PARENTHESIS) {
            specification_end = atom.text_begin;
            break;
        }

        if (atom.kind == LLFPL_ATOM_END_OF_SOURCE) {
            llfpl_diagnostic_report_error(reporter_of(environment),
                                          llfpl_atom_source_location(scanner, atom),
                                          "unterminated Require directive");
            return LLFPL_STATUS_SYNTAX_ERROR;
        }
    }

    /* Trim the whitespace a writer may have put around the path. */
    while (specification_begin < specification_end &&
           (*specification_begin == ' ' || *specification_begin == '\t')) {
        specification_begin++;
    }
    while (specification_end > specification_begin &&
           (*(specification_end - 1) == ' ' || *(specification_end - 1) == '\t')) {
        specification_end--;
    }

    specification_length = (size_t)(specification_end - specification_begin);

    if (specification_length == 0u) {
        llfpl_diagnostic_report_error(
            reporter_of(environment),
            llfpl_source_location_make((*scanner).module_path, (*scanner).line_number, 0u),
            "Require expects a module path");
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    if (specification_length + 1u > buffer_capacity) {
        llfpl_diagnostic_report_error(
            reporter_of(environment),
            llfpl_source_location_make((*scanner).module_path, (*scanner).line_number, 0u),
            "module path is longer than %u characters",
            (unsigned)(buffer_capacity - 1u));
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(specification_out, specification_begin, specification_length);
    *(specification_out + specification_length) = '\0';

    return LLFPL_STATUS_OK;
}

static LlfplStatus directive_require(LlfplModuleExecutionEnvironment *environment,
                                     LlfplLexicalScanner *scanner,
                                     LlfplAtom keyword_atom) {
    char module_specification[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    char canonical_path[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    LlfplStatus status = LLFPL_STATUS_OK;

    status = consume_module_specification(
        environment, scanner, module_specification, sizeof(module_specification));
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    if (!llfpl_path_has_extension(module_specification, LLFPL_SOURCE_FILE_EXTENSION)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, keyword_atom),
                                      "required module '%s' must end in %s",
                                      module_specification,
                                      LLFPL_SOURCE_FILE_EXTENSION);
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    status = llfpl_module_loader_resolve((*environment).module_loader,
                                         (*scanner).module_path,
                                         module_specification,
                                         canonical_path,
                                         sizeof(canonical_path));
    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_atom_source_location(scanner, keyword_atom),
                                      "cannot locate required module '%s'",
                                      module_specification);
        return status;
    }

    /* Already loaded, whether through another path or through a cycle. */
    if (llfpl_module_loader_is_loaded((*environment).module_loader, canonical_path)) {
        llfpl_diagnostic_report_trace(
            reporter_of(environment), "module already loaded: %s", canonical_path);
        return LLFPL_STATUS_OK;
    }

    return llfpl_directive_processor_execute_module(environment, canonical_path);
}

/* ---- Commit ------------------------------------------------------------------ */

/*
 * Commit(expression)
 *
 * Evaluates an expression, publishes the register bank, and writes the result
 * to the result stream. The timing brackets only the evaluation and the
 * publication, and the publication is what makes the measurement honest: it is
 * a barrier the compiler may not move work across, so nothing computed inside
 * the bracket can drift outside it.
 */
static LlfplStatus directive_commit(LlfplModuleExecutionEnvironment *environment,
                                    LlfplLexicalScanner *scanner,
                                    LlfplAtom keyword_atom) {
    char formatted_result[LLFPL_NUMERIC_LITERAL_BUFFER_CAPACITY];
    uint64_t started_at_nanoseconds = 0u;
    uint64_t elapsed_nanoseconds = 0u;
    double committed_value = 0.0;

    LLFPL_UNUSED_PARAMETER(keyword_atom);

    if (!expect_atom(environment, scanner, LLFPL_ATOM_OPEN_PARENTHESIS, "Commit")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    started_at_nanoseconds = llfpl_monotonic_clock_now_in_nanoseconds();

    committed_value = evaluate_top_level_expression(environment, scanner);

    if ((*environment).hardware_synchronisation_is_enabled) {
        llfpl_vector_register_file_synchronise_to_silicon(
            (*(*environment).execution_context).register_file);
    }

    elapsed_nanoseconds = llfpl_monotonic_clock_now_in_nanoseconds() - started_at_nanoseconds;

    if (!expect_atom(environment, scanner, LLFPL_ATOM_CLOSE_PARENTHESIS, "Commit")) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    if (llfpl_execution_context_has_failed((*environment).execution_context)) {
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    (*environment).committed_expression_count++;
    (*environment).total_elapsed_nanoseconds += elapsed_nanoseconds;

    (void)llfpl_numeric_format_double(committed_value, formatted_result, sizeof(formatted_result));

    if ((*environment).result_stream != NULL) {
        (void)fprintf((*environment).result_stream, "%s\n", formatted_result);
    }

    llfpl_diagnostic_report_trace(reporter_of(environment),
                                  "commit %u: %s (cost %llu, elapsed %llu ns)",
                                  (unsigned)(*environment).committed_expression_count,
                                  formatted_result,
                                  (unsigned long long)llfpl_vector_register_file_retired_cycles(
                                      (*(*environment).execution_context).register_file),
                                  (unsigned long long)elapsed_nanoseconds);

    return LLFPL_STATUS_OK;
}

/* ---- Directive table ----------------------------------------------------------- */

static const LlfplDirectiveDescriptor directive_descriptors[] = {
    {"Identity",
     8u,
     directive_identity,
     "Identity(Name, expression)",
     "Binds an immutable global value"},

    {"Slab",
     4u,
     directive_slab,
     "Slab(Name, byte_capacity)",
     "Reserves a cache-line-aligned data arena"},

    {"Map",
     3u,
     directive_map,
     "Map(Name, parameters..., body)",
     "Declares a reusable parameterised expression"},

    {"Require",
     7u,
     directive_require,
     "Require(module/path.LLFPL)",
     "Loads another module exactly once"},

    {"Commit",
     6u,
     directive_commit,
     "Commit(expression)",
     "Evaluates an expression and reports its value"},
};

static const LlfplDirectiveDescriptor *lookup_directive(const char *keyword_text,
                                                        size_t keyword_length) {
    size_t descriptor_index = 0u;

    for (descriptor_index = 0u; descriptor_index < LLFPL_ARRAY_LENGTH(directive_descriptors);
         descriptor_index++) {
        const LlfplDirectiveDescriptor *descriptor = directive_descriptors + descriptor_index;

        if ((*descriptor).keyword_length == keyword_length &&
            memcmp((*descriptor).keyword, keyword_text, keyword_length) == 0) {
            return descriptor;
        }
    }

    return NULL;
}

size_t llfpl_directive_count(void) {
    return LLFPL_ARRAY_LENGTH(directive_descriptors);
}

const char *llfpl_directive_signature_at(size_t directive_index) {
    if (directive_index >= LLFPL_ARRAY_LENGTH(directive_descriptors)) {
        return NULL;
    }

    return (*(directive_descriptors + directive_index)).signature_summary;
}

const char *llfpl_directive_purpose_at(size_t directive_index) {
    if (directive_index >= LLFPL_ARRAY_LENGTH(directive_descriptors)) {
        return NULL;
    }

    return (*(directive_descriptors + directive_index)).purpose_summary;
}

int llfpl_directive_name_is_reserved(const char *name_text, size_t name_length) {
    return lookup_directive(name_text, name_length) != NULL;
}

/* ---- Module execution ------------------------------------------------------------ */

LlfplStatus llfpl_directive_processor_execute_module(LlfplModuleExecutionEnvironment *environment,
                                                     const char *canonical_module_path) {
    const LlfplSourceFileMapping *mapping = NULL;
    LlfplLexicalScanner scanner;
    LlfplStatus status = LLFPL_STATUS_OK;

    if (environment == NULL || canonical_module_path == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Marked before execution, not after. A module that requires itself, either
     * directly or around a longer cycle, must find itself already recorded when
     * the cycle closes, or the recursion would not terminate.
     */
    status = llfpl_module_loader_mark_loaded((*environment).module_loader, canonical_module_path);
    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_source_location_unknown(),
                                      "cannot track module '%s': %s",
                                      canonical_module_path,
                                      llfpl_status_describe(status));
        return status;
    }

    status = llfpl_source_file_mapping_registry_open(
        (*environment).mapping_registry, canonical_module_path, &mapping);
    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(reporter_of(environment),
                                      llfpl_source_location_unknown(),
                                      "cannot read module '%s': %s",
                                      canonical_module_path,
                                      llfpl_status_describe(status));
        return status;
    }

    llfpl_diagnostic_report_trace(reporter_of(environment),
                                  "loading module %s (%u bytes)",
                                  (*mapping).canonical_path,
                                  (unsigned)(*mapping).text_length_in_bytes);

    /*
     * The scanner borrows the mapping's own copy of the path. The mapping
     * outlives every template snapshot taken from it, so a diagnostic raised
     * long after this module finished still names the right file.
     */
    llfpl_lexical_scanner_initialise(&scanner,
                                     (*mapping).text_begin,
                                     (*mapping).text_length_in_bytes,
                                     (*mapping).canonical_path);

    while (1) {
        const LlfplAtom keyword_atom = llfpl_lexical_scanner_next(&scanner);
        const LlfplDirectiveDescriptor *descriptor = NULL;

        if (keyword_atom.kind == LLFPL_ATOM_END_OF_SOURCE) {
            break;
        }

        if (keyword_atom.kind != LLFPL_ATOM_IDENTIFIER) {
            llfpl_diagnostic_report_error(reporter_of(environment),
                                          llfpl_atom_source_location(&scanner, keyword_atom),
                                          "expected a directive but found %s",
                                          llfpl_atom_kind_describe(keyword_atom.kind));
            return LLFPL_STATUS_SYNTAX_ERROR;
        }

        descriptor = lookup_directive(keyword_atom.text_begin, keyword_atom.text_length);

        if (descriptor == NULL) {
            llfpl_diagnostic_report_error(reporter_of(environment),
                                          llfpl_atom_source_location(&scanner, keyword_atom),
                                          "'%.*s' is not a directive; "
                                          "only declarations may appear at the top level",
                                          (int)keyword_atom.text_length,
                                          keyword_atom.text_begin);
            return LLFPL_STATUS_SYNTAX_ERROR;
        }

        status = (*descriptor).handler(environment, &scanner, keyword_atom);

        /* Stop at the first failure; later diagnostics would only be noise. */
        if (!llfpl_status_is_ok(status)) {
            return status;
        }
    }

    return LLFPL_STATUS_OK;
}

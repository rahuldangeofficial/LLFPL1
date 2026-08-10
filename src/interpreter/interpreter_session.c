/*
 * interpreter_session.c -- Construction, teardown and driving of a session.
 *
 * Subsystems are brought up in dependency order and torn down in the exact
 * reverse, and a partially constructed session releases only what it managed to
 * build. That is what makes a failure during start-up leak nothing, which
 * matters here because the failure in question is an allocation refusal.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/interpreter/interpreter_session.h"

#include <stdlib.h>
#include <string.h>

#include "llfpl/core/diagnostic_reporter.h"
#include "llfpl/core/filesystem_path.h"
#include "llfpl/core/monotonic_clock.h"
#include "llfpl/frontend/template_definition.h"
#include "llfpl/hardware/vector_register_file.h"
#include "llfpl/interpreter/directive_processor.h"
#include "llfpl/interpreter/module_loader.h"
#include "llfpl/memory/memory_arena.h"
#include "llfpl/memory/source_file_mapping.h"
#include "llfpl/runtime/execution_context.h"
#include "llfpl/runtime/symbol_table.h"

/* ---- Session state --------------------------------------------------------- */

/*
 * Held by value rather than by pointer wherever the subsystem has no allocation
 * of its own. One allocation for the session means one cache-resident block for
 * everything the interpreter touches between evaluations.
 */
struct LlfplInterpreterSession {
    LlfplHardwareTopology hardware_topology;

    LlfplDiagnosticReporter diagnostic_reporter;
    LlfplSymbolTable symbol_table;
    LlfplTemplateSegment template_segment;
    LlfplMemoryArenaRegistry arena_registry;
    LlfplVectorRegisterFile register_file;
    LlfplSourceFileMappingRegistry mapping_registry;
    LlfplModuleLoader module_loader;

    LlfplExecutionContext execution_context;
    LlfplModuleExecutionEnvironment execution_environment;

    int summary_is_enabled;
};

/* ---- Options ---------------------------------------------------------------- */

void llfpl_interpreter_options_initialise(LlfplInterpreterOptions *options) {
    if (options == NULL) {
        return;
    }

    memset(options, 0, sizeof(*options));

    (*options).result_stream = stdout;
    (*options).diagnostic_stream = stderr;
    (*options).verbose_output_is_enabled = 0;
    (*options).summary_is_enabled = 0;
    (*options).hardware_synchronisation_is_enabled = 1;
    (*options).invocation_argument = NULL;
    (*options).additional_module_directory_count = 0u;
}

/* ---- Construction ------------------------------------------------------------ */

/*
 * Brings up the subsystems that own memory. Returns at the first refusal with
 * everything constructed so far already released, so the caller never has to
 * reason about a half-built session.
 */
static LlfplStatus construct_owned_subsystems(LlfplInterpreterSession *session) {
    const uint16_t alignment_in_bytes = (*session).hardware_topology.cache_line_size_in_bytes;
    LlfplStatus status = LLFPL_STATUS_OK;

    status = llfpl_vector_register_file_initialise(&(*session).register_file,
                                                   (*session).hardware_topology);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    status = llfpl_symbol_table_initialise(&(*session).symbol_table, alignment_in_bytes);
    if (!llfpl_status_is_ok(status)) {
        llfpl_vector_register_file_release(&(*session).register_file);
        return status;
    }

    status = llfpl_memory_arena_registry_initialise(&(*session).arena_registry, alignment_in_bytes);
    if (!llfpl_status_is_ok(status)) {
        llfpl_symbol_table_release(&(*session).symbol_table);
        llfpl_vector_register_file_release(&(*session).register_file);
        return status;
    }

    return LLFPL_STATUS_OK;
}

static void wire_execution_context(LlfplInterpreterSession *session,
                                   const LlfplInterpreterOptions *options) {
    (*session).execution_context.symbol_table = &(*session).symbol_table;
    (*session).execution_context.template_segment = &(*session).template_segment;
    (*session).execution_context.arena_registry = &(*session).arena_registry;
    (*session).execution_context.register_file = &(*session).register_file;
    (*session).execution_context.diagnostic_reporter = &(*session).diagnostic_reporter;
    (*session).execution_context.current_evaluation_depth = 0u;
    (*session).execution_context.maximum_evaluation_depth = LLFPL_MAXIMUM_EVALUATION_DEPTH;

    (*session).execution_environment.execution_context = &(*session).execution_context;
    (*session).execution_environment.module_loader = &(*session).module_loader;
    (*session).execution_environment.mapping_registry = &(*session).mapping_registry;
    (*session).execution_environment.result_stream = (*options).result_stream;
    (*session).execution_environment.hardware_synchronisation_is_enabled =
        (*options).hardware_synchronisation_is_enabled;
    (*session).execution_environment.committed_expression_count = 0u;
    (*session).execution_environment.total_elapsed_nanoseconds = 0u;
}

static void register_module_directories(LlfplInterpreterSession *session,
                                        const LlfplInterpreterOptions *options) {
    uint32_t directory_index = 0u;

    /* Explicit directories first: an option must be able to override a default. */
    for (directory_index = 0u; directory_index < (*options).additional_module_directory_count;
         directory_index++) {
        const char *directory_path = *((*options).additional_module_directories + directory_index);

        if (directory_path == NULL) {
            continue;
        }

        if (!llfpl_status_is_ok(llfpl_module_loader_add_search_directory(&(*session).module_loader,
                                                                         directory_path))) {
            llfpl_diagnostic_report_warning(&(*session).diagnostic_reporter,
                                            llfpl_source_location_unknown(),
                                            "ignoring module directory '%s'",
                                            directory_path);
        }
    }

    llfpl_module_loader_add_default_directories(&(*session).module_loader,
                                                (*options).invocation_argument);
}

LlfplStatus llfpl_interpreter_session_create(const LlfplInterpreterOptions *options,
                                             LlfplInterpreterSession **session_out) {
    LlfplInterpreterSession *session = NULL;
    LlfplStatus status = LLFPL_STATUS_OK;

    if (options == NULL || session_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    *session_out = NULL;

    session = calloc(1u, sizeof(*session));
    if (session == NULL) {
        return LLFPL_STATUS_OUT_OF_MEMORY;
    }

    (*session).hardware_topology = llfpl_hardware_topology_probe();
    (*session).summary_is_enabled = (*options).summary_is_enabled;

    llfpl_diagnostic_reporter_initialise(&(*session).diagnostic_reporter,
                                         (*options).diagnostic_stream,
                                         (*options).verbose_output_is_enabled);

    status = construct_owned_subsystems(session);
    if (!llfpl_status_is_ok(status)) {
        free(session);
        return status;
    }

    llfpl_template_segment_initialise(&(*session).template_segment);
    llfpl_source_file_mapping_registry_initialise(&(*session).mapping_registry);
    llfpl_module_loader_initialise(&(*session).module_loader);

    wire_execution_context(session, options);
    register_module_directories(session, options);

    llfpl_diagnostic_report_trace(
        &(*session).diagnostic_reporter,
        "%s host: %u byte cache line (%s), %u virtual registers",
        (*session).hardware_topology.architecture_name,
        (unsigned)(*session).hardware_topology.cache_line_size_in_bytes,
        (*session).hardware_topology.cache_line_size_was_probed ? "probed" : "assumed",
        (unsigned)(*session).hardware_topology.virtual_register_count);

    *session_out = session;
    return LLFPL_STATUS_OK;
}

void llfpl_interpreter_session_destroy(LlfplInterpreterSession *session) {
    if (session == NULL) {
        return;
    }

    /*
     * Reverse construction order. The mappings go last among the borrowed
     * resources because template snapshots point into them, and releasing the
     * text before the segment that references it would leave dangling snapshots
     * behind for the remainder of the teardown.
     */
    llfpl_memory_arena_registry_release(&(*session).arena_registry);
    llfpl_symbol_table_release(&(*session).symbol_table);
    llfpl_vector_register_file_release(&(*session).register_file);
    llfpl_source_file_mapping_registry_release(&(*session).mapping_registry);

    free(session);
}

/* ---- Execution ----------------------------------------------------------------- */

LlfplStatus llfpl_interpreter_session_execute_source_file(LlfplInterpreterSession *session,
                                                          const char *source_path) {
    char canonical_path[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    LlfplStatus status = LLFPL_STATUS_OK;

    if (session == NULL || source_path == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (!llfpl_path_has_extension(source_path, LLFPL_SOURCE_FILE_EXTENSION)) {
        llfpl_diagnostic_report_error(&(*session).diagnostic_reporter,
                                      llfpl_source_location_unknown(),
                                      "'%s' is not an LLFPL source file; "
                                      "the required extension is %s",
                                      source_path,
                                      LLFPL_SOURCE_FILE_EXTENSION);
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    status = llfpl_module_loader_resolve(
        &(*session).module_loader, NULL, source_path, canonical_path, sizeof(canonical_path));
    if (!llfpl_status_is_ok(status)) {
        llfpl_diagnostic_report_error(&(*session).diagnostic_reporter,
                                      llfpl_source_location_unknown(),
                                      "cannot open '%s'",
                                      source_path);
        return status;
    }

    return llfpl_directive_processor_execute_module(&(*session).execution_environment,
                                                    canonical_path);
}

/* ---- Reporting -------------------------------------------------------------------- */

uint32_t llfpl_interpreter_session_error_count(const LlfplInterpreterSession *session) {
    if (session == NULL) {
        return 0u;
    }

    return (*session).diagnostic_reporter.reported_error_count;
}

LlfplHardwareTopology
llfpl_interpreter_session_hardware_topology(const LlfplInterpreterSession *session) {
    return (*session).hardware_topology;
}

void llfpl_interpreter_session_write_summary(const LlfplInterpreterSession *session,
                                             FILE *destination_stream) {
    const LlfplModuleExecutionEnvironment *environment = NULL;

    if (session == NULL || destination_stream == NULL) {
        return;
    }

    environment = &(*session).execution_environment;

    (void)fprintf(
        destination_stream,
        "llfpl: %u commit(s), reduction cost %llu, %llu ns evaluating, "
        "%u module(s), %u error(s)\n",
        (unsigned)(*environment).committed_expression_count,
        (unsigned long long)llfpl_vector_register_file_retired_cycles(&(*session).register_file),
        (unsigned long long)(*environment).total_elapsed_nanoseconds,
        (unsigned)(*session).module_loader.loaded_module_count,
        (unsigned)(*session).diagnostic_reporter.reported_error_count);
}

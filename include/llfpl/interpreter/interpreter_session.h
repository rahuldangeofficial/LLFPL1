/*
 * interpreter_session.h -- The composition root of the LLFPL runtime.
 *
 * A session owns every subsystem, wires them together once, and exposes a
 * three-call interface: create, execute, destroy. Nothing above this header
 * needs to know that a symbol table, an arena registry or a register file
 * exists, and nothing below it needs to know how the process was invoked.
 *
 * The handle is opaque. A session is created and destroyed exactly once per
 * process in the command line tool, so the indirection costs nothing measurable
 * and buys the freedom to change the composition without recompiling callers.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_INTERPRETER_INTERPRETER_SESSION_H
#define LLFPL_INTERPRETER_INTERPRETER_SESSION_H

#include <stdint.h>
#include <stdio.h>

#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"
#include "llfpl/hardware/hardware_topology.h"

typedef struct {
    /* Where evaluated Commit results are written. Defaults to stdout. */
    FILE *result_stream;

    /* Where diagnostics and trace output are written. Defaults to stderr. */
    FILE *diagnostic_stream;

    /* Emit trace output describing declarations, module loads and timings. */
    int verbose_output_is_enabled;

    /* Print a summary of the run to the diagnostic stream when it finishes. */
    int summary_is_enabled;

    /* Publish the register bank to architectural registers at each Commit. */
    int hardware_synchronisation_is_enabled;

    /* argv[0], used to locate the standard library relative to the binary. */
    const char *invocation_argument;

    /* Directories searched before the automatically discovered ones. */
    const char *additional_module_directories[LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES];
    uint32_t additional_module_directory_count;
} LlfplInterpreterOptions;

typedef struct LlfplInterpreterSession LlfplInterpreterSession;

/* Fills options with the documented defaults. Always call this first, so that
 * an option added later cannot leave an existing caller with a garbage field. */
void llfpl_interpreter_options_initialise(LlfplInterpreterOptions *options);

LlfplStatus
llfpl_interpreter_session_create(const LlfplInterpreterOptions *options,
                                 LlfplInterpreterSession **session_out) LLFPL_WARN_UNUSED_RESULT;

void llfpl_interpreter_session_destroy(LlfplInterpreterSession *session);

/*
 * Loads and executes a source file, together with everything it requires.
 *
 * The path is resolved and canonicalised the same way a required module is, so
 * the entry point and its dependencies obey one set of rules.
 */
LlfplStatus llfpl_interpreter_session_execute_source_file(LlfplInterpreterSession *session,
                                                          const char *source_path);

/* Number of errors reported so far. Zero is the only successful outcome. */
uint32_t llfpl_interpreter_session_error_count(const LlfplInterpreterSession *session);

/* The hardware profile this session was built for. */
LlfplHardwareTopology
llfpl_interpreter_session_hardware_topology(const LlfplInterpreterSession *session);

/* Writes the end-of-run summary: commits, cost, and elapsed evaluation time. */
void llfpl_interpreter_session_write_summary(const LlfplInterpreterSession *session,
                                             FILE *destination_stream);

#endif /* LLFPL_INTERPRETER_INTERPRETER_SESSION_H */

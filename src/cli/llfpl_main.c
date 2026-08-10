/*
 * llfpl_main.c -- Entry point of the LLFPL command line interpreter.
 *
 * main does four things and delegates everything else: parse the command line,
 * build a session from it, run the requested source, and translate the outcome
 * into an exit status. There is no language logic here at all, which is what
 * allows the same runtime to be driven by a different front end without any of
 * it being rewritten.
 *
 * Exit status:
 *     0  the program ran and reported no error
 *     1  the program was loaded but an error was reported
 *     2  the command line was malformed, or the session could not be built
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include <stdio.h>
#include <stdlib.h>

#include "llfpl/cli/command_line_options.h"
#include "llfpl/core/monotonic_clock.h"
#include "llfpl/core/status_code.h"
#include "llfpl/hardware/hardware_topology.h"
#include "llfpl/interpreter/interpreter_session.h"

#ifndef LLFPL_VERSION_STRING
#define LLFPL_VERSION_STRING "0.0.0-development"
#endif

#define LLFPL_EXIT_SUCCESS       0
#define LLFPL_EXIT_PROGRAM_ERROR 1
#define LLFPL_EXIT_USAGE_ERROR   2

/* ---- Informational commands ------------------------------------------------ */

static void write_version(FILE *destination_stream) {
    (void)fprintf(destination_stream, "LLFPL %s\n", LLFPL_VERSION_STRING);
}

static void write_hardware_profile(FILE *destination_stream) {
    const LlfplHardwareTopology topology = llfpl_hardware_topology_probe();

    (void)fprintf(destination_stream,
                  "architecture          %s\n"
                  "cache line            %u bytes (%s)\n"
                  "virtual registers     %u\n"
                  "monotonic clock       %llu ns resolution\n",
                  topology.architecture_name,
                  (unsigned)topology.cache_line_size_in_bytes,
                  topology.cache_line_size_was_probed ? "probed" : "assumed",
                  (unsigned)topology.virtual_register_count,
                  (unsigned long long)llfpl_monotonic_clock_resolution_in_nanoseconds());
}

/* ---- Session construction from parsed options -------------------------------- */

static void apply_options(const LlfplCommandLineOptions *command_line,
                          const char *invocation_argument,
                          LlfplInterpreterOptions *session_options) {
    uint32_t directory_index = 0u;

    llfpl_interpreter_options_initialise(session_options);

    (*session_options).verbose_output_is_enabled = (*command_line).verbose_output_is_enabled;
    (*session_options).summary_is_enabled = (*command_line).summary_is_enabled;
    (*session_options).hardware_synchronisation_is_enabled =
        (*command_line).hardware_synchronisation_is_enabled;
    (*session_options).invocation_argument = invocation_argument;

    for (directory_index = 0u; directory_index < (*command_line).module_directory_count;
         directory_index++) {
        *((*session_options).additional_module_directories + directory_index) =
            *((*command_line).module_directories + directory_index);
    }

    (*session_options).additional_module_directory_count = (*command_line).module_directory_count;
}

static int execute_source(const LlfplCommandLineOptions *command_line,
                          const char *invocation_argument) {
    LlfplInterpreterOptions session_options;
    LlfplInterpreterSession *session = NULL;
    LlfplStatus status = LLFPL_STATUS_OK;
    int exit_status = LLFPL_EXIT_SUCCESS;

    apply_options(command_line, invocation_argument, &session_options);

    status = llfpl_interpreter_session_create(&session_options, &session);
    if (!llfpl_status_is_ok(status)) {
        (void)fprintf(
            stderr, "llfpl: error: cannot start the runtime: %s\n", llfpl_status_describe(status));
        return LLFPL_EXIT_USAGE_ERROR;
    }

    (void)llfpl_interpreter_session_execute_source_file(session, (*command_line).source_path);

    /*
     * The error count, not the returned status, decides the outcome. A module
     * can report a diagnostic and still unwind cleanly, and it is the presence
     * of a diagnostic that means the program did not do what it said.
     */
    if (llfpl_interpreter_session_error_count(session) > 0u) {
        exit_status = LLFPL_EXIT_PROGRAM_ERROR;
    }

    if ((*command_line).summary_is_enabled) {
        llfpl_interpreter_session_write_summary(session, stderr);
    }

    llfpl_interpreter_session_destroy(session);

    return exit_status;
}

/* ---- Entry point --------------------------------------------------------------- */

int main(int argument_count, char **argument_values) {
    LlfplCommandLineOptions command_line;
    const char *invocation_argument = (argument_count > 0) ? *(argument_values + 0) : "llfpl";

    if (!llfpl_status_is_ok(
            llfpl_command_line_parse(argument_count, argument_values, stderr, &command_line))) {
        llfpl_command_line_write_usage(stderr, invocation_argument);
        return LLFPL_EXIT_USAGE_ERROR;
    }

    switch (command_line.command) {
        case LLFPL_COMMAND_SHOW_HELP:
            llfpl_command_line_write_usage(stdout, invocation_argument);
            return LLFPL_EXIT_SUCCESS;

        case LLFPL_COMMAND_SHOW_VERSION:
            write_version(stdout);
            return LLFPL_EXIT_SUCCESS;

        case LLFPL_COMMAND_SHOW_HARDWARE:
            write_hardware_profile(stdout);
            return LLFPL_EXIT_SUCCESS;

        case LLFPL_COMMAND_SHOW_LANGUAGE:
            llfpl_command_line_write_language_reference(stdout);
            return LLFPL_EXIT_SUCCESS;

        case LLFPL_COMMAND_EXECUTE_SOURCE:
            break;
    }

    return execute_source(&command_line, invocation_argument);
}

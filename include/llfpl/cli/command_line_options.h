/*
 * command_line_options.h -- Parsing of the llfpl command line.
 *
 * Argument parsing is kept out of main so that it can be reasoned about, and
 * changed, without touching the program's control flow. The parser decides only
 * what the user asked for; it starts nothing and prints nothing except when the
 * request itself was malformed.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CLI_COMMAND_LINE_OPTIONS_H
#define LLFPL_CLI_COMMAND_LINE_OPTIONS_H

#include <stdint.h>
#include <stdio.h>

#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

/* What the parsed command line asks the program to do. */
typedef enum {
    LLFPL_COMMAND_EXECUTE_SOURCE, /* Run a source file.                     */
    LLFPL_COMMAND_SHOW_HELP,      /* Print usage and exit successfully.     */
    LLFPL_COMMAND_SHOW_VERSION,   /* Print the version and exit.            */
    LLFPL_COMMAND_SHOW_HARDWARE,  /* Print the detected host profile.       */
    LLFPL_COMMAND_SHOW_LANGUAGE   /* Print the language reference summary.  */
} LlfplCommandKind;

typedef struct {
    LlfplCommandKind command;

    const char *source_path; /* Valid only for LLFPL_COMMAND_EXECUTE_SOURCE. */

    int verbose_output_is_enabled;
    int summary_is_enabled;
    int hardware_synchronisation_is_enabled;

    const char *module_directories[LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES];
    uint32_t module_directory_count;
} LlfplCommandLineOptions;

/*
 * Parses argv. On a malformed command line a diagnostic naming the offending
 * argument is written to error_stream and a non-OK status is returned; the
 * options are then unspecified and must not be used.
 */
LlfplStatus llfpl_command_line_parse(int argument_count,
                                     char *const *argument_values,
                                     FILE *error_stream,
                                     LlfplCommandLineOptions *options_out);

/* Writes the usage text, including the directive and built-in tables. */
void llfpl_command_line_write_usage(FILE *destination_stream, const char *program_name);

/* Writes the language reference summary: directives, built-ins, primitives. */
void llfpl_command_line_write_language_reference(FILE *destination_stream);

#endif /* LLFPL_CLI_COMMAND_LINE_OPTIONS_H */

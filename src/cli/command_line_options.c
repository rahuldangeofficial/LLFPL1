/*
 * command_line_options.c -- Argument parsing and the help texts.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/cli/command_line_options.h"

#include <string.h>

#include "llfpl/interpreter/directive_processor.h"
#include "llfpl/runtime/builtin_operation.h"
#include "llfpl/runtime/primitive_reduction.h"

#ifndef LLFPL_VERSION_STRING
#define LLFPL_VERSION_STRING "0.0.0-development"
#endif

/* ---- Internal helpers ------------------------------------------------------ */

static int argument_matches(const char *argument, const char *short_form, const char *long_form) {
    if (short_form != NULL && strcmp(argument, short_form) == 0) {
        return 1;
    }

    return long_form != NULL && strcmp(argument, long_form) == 0;
}

/*
 * Extracts the value of an option that carries one, accepting all three of the
 * conventional spellings: "-Ivalue", "--name=value" and a following argument.
 * Advances the index past whatever form was used.
 */
static const char *extract_option_value(const char *argument,
                                        const char *short_form,
                                        const char *long_form,
                                        int argument_count,
                                        char *const *argument_values,
                                        int *argument_index) {
    const size_t short_form_length = (short_form != NULL) ? strlen(short_form) : 0u;
    const size_t long_form_length = (long_form != NULL) ? strlen(long_form) : 0u;

    if (short_form_length > 0u && strncmp(argument, short_form, short_form_length) == 0 &&
        *(argument + short_form_length) != '\0') {
        return argument + short_form_length;
    }

    if (long_form_length > 0u && strncmp(argument, long_form, long_form_length) == 0 &&
        *(argument + long_form_length) == '=') {
        return argument + long_form_length + 1u;
    }

    if (argument_matches(argument, short_form, long_form)) {
        if (*argument_index + 1 >= argument_count) {
            return NULL;
        }

        (*argument_index)++;
        return *(argument_values + *argument_index);
    }

    return NULL;
}

static int argument_introduces_option(const char *argument) {
    return *argument == '-' && *(argument + 1) != '\0';
}

/* ---- Parsing ---------------------------------------------------------------- */

LlfplStatus llfpl_command_line_parse(int argument_count,
                                     char *const *argument_values,
                                     FILE *error_stream,
                                     LlfplCommandLineOptions *options_out) {
    int argument_index = 1;
    int options_have_ended = 0;

    if (argument_values == NULL || options_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (error_stream == NULL) {
        error_stream = stderr;
    }

    memset(options_out, 0, sizeof(*options_out));
    (*options_out).command = LLFPL_COMMAND_EXECUTE_SOURCE;
    (*options_out).hardware_synchronisation_is_enabled = 1;

    for (argument_index = 1; argument_index < argument_count; argument_index++) {
        const char *argument = *(argument_values + argument_index);

        if (!options_have_ended && strcmp(argument, "--") == 0) {
            options_have_ended = 1;
            continue;
        }

        if (!options_have_ended && argument_introduces_option(argument)) {
            const char *option_value = NULL;

            if (argument_matches(argument, "-h", "--help")) {
                (*options_out).command = LLFPL_COMMAND_SHOW_HELP;
                return LLFPL_STATUS_OK;
            }

            if (argument_matches(argument, "-V", "--version")) {
                (*options_out).command = LLFPL_COMMAND_SHOW_VERSION;
                return LLFPL_STATUS_OK;
            }

            if (argument_matches(argument, NULL, "--hardware")) {
                (*options_out).command = LLFPL_COMMAND_SHOW_HARDWARE;
                return LLFPL_STATUS_OK;
            }

            if (argument_matches(argument, NULL, "--language")) {
                (*options_out).command = LLFPL_COMMAND_SHOW_LANGUAGE;
                return LLFPL_STATUS_OK;
            }

            if (argument_matches(argument, "-v", "--verbose")) {
                (*options_out).verbose_output_is_enabled = 1;
                continue;
            }

            if (argument_matches(argument, "-s", "--summary")) {
                (*options_out).summary_is_enabled = 1;
                continue;
            }

            if (argument_matches(argument, NULL, "--no-register-sync")) {
                (*options_out).hardware_synchronisation_is_enabled = 0;
                continue;
            }

            option_value = extract_option_value(
                argument, "-I", "--module-path", argument_count, argument_values, &argument_index);
            if (option_value != NULL) {
                if (*option_value == '\0') {
                    (void)fprintf(error_stream,
                                  "llfpl: error: '%s' requires a non-empty directory\n",
                                  argument);
                    return LLFPL_STATUS_INVALID_ARGUMENT;
                }

                if ((*options_out).module_directory_count >=
                    LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES) {
                    (void)fprintf(error_stream,
                                  "llfpl: error: at most %u module directories may be given\n",
                                  (unsigned)LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES);
                    return LLFPL_STATUS_CAPACITY_EXCEEDED;
                }

                *((*options_out).module_directories + (*options_out).module_directory_count) =
                    option_value;
                (*options_out).module_directory_count++;
                continue;
            }

            if (argument_matches(argument, "-I", "--module-path")) {
                (void)fprintf(error_stream, "llfpl: error: '%s' requires a directory\n", argument);
                return LLFPL_STATUS_INVALID_ARGUMENT;
            }

            (void)fprintf(error_stream, "llfpl: error: unrecognised option '%s'\n", argument);
            return LLFPL_STATUS_INVALID_ARGUMENT;
        }

        if ((*options_out).source_path != NULL) {
            (void)fprintf(
                error_stream, "llfpl: error: more than one source file given ('%s')\n", argument);
            return LLFPL_STATUS_INVALID_ARGUMENT;
        }

        (*options_out).source_path = argument;
    }

    if ((*options_out).source_path == NULL) {
        (void)fprintf(error_stream, "llfpl: error: no source file given\n");
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    return LLFPL_STATUS_OK;
}

/* ---- Help texts --------------------------------------------------------------- */

void llfpl_command_line_write_usage(FILE *destination_stream, const char *program_name) {
    if (destination_stream == NULL) {
        return;
    }

    (void)fprintf(destination_stream,
                  "LLFPL %s -- Low-Level Floating-Point Language\n"
                  "\n"
                  "Usage: %s [options] <source%s>\n"
                  "\n"
                  "Options:\n"
                  "  -I, --module-path DIR   Search DIR for required modules; repeatable, and\n"
                  "                          searched before the directories discovered from\n"
                  "                          the location of this executable.\n"
                  "  -v, --verbose           Report module loads, declarations and per-commit\n"
                  "                          timings on the diagnostic stream.\n"
                  "  -s, --summary           Print a summary of the run when it finishes.\n"
                  "      --no-register-sync  Do not publish the register bank to architectural\n"
                  "                          registers at each Commit.\n"
                  "      --hardware          Print the detected host profile and exit.\n"
                  "      --language          Print the language reference summary and exit.\n"
                  "  -h, --help              Print this text and exit.\n"
                  "  -V, --version           Print the version and exit.\n"
                  "\n"
                  "Results are written to standard output, one value per Commit.\n"
                  "Diagnostics are written to standard error. The exit status is zero only\n"
                  "when no error was reported.\n",
                  LLFPL_VERSION_STRING,
                  (program_name != NULL) ? program_name : "llfpl",
                  LLFPL_SOURCE_FILE_EXTENSION);
}

void llfpl_command_line_write_language_reference(FILE *destination_stream) {
    static const char *const primitive_verbs[] = {
        "plus(a, b)      sum",
        "minus(a, b)     difference",
        "multiply(a, b)  product",
        "divide(a, b)    quotient, IEEE 754 semantics",
        "modulo(a, b)    remainder with the sign of the dividend",
        "greater(a, b)   1.0 when a is greater than b, otherwise 0.0",
        "less(a, b)      1.0 when a is less than b, otherwise 0.0",
        "equal(a, b)     1.0 when a equals b, otherwise 0.0",
    };

    size_t entry_index = 0u;

    if (destination_stream == NULL) {
        return;
    }

    (void)fprintf(destination_stream, "Top-level directives:\n");
    for (entry_index = 0u; entry_index < llfpl_directive_count(); entry_index++) {
        (void)fprintf(destination_stream,
                      "  %-34s %s\n",
                      llfpl_directive_signature_at(entry_index),
                      llfpl_directive_purpose_at(entry_index));
    }

    (void)fprintf(destination_stream, "\nBuilt-in expression forms:\n");
    for (entry_index = 0u; entry_index < llfpl_builtin_count(); entry_index++) {
        const LlfplBuiltinDescriptor *descriptor = llfpl_builtin_descriptor_at(entry_index);

        (void)fprintf(destination_stream,
                      "  %-44s %s\n",
                      (*descriptor).signature_summary,
                      (*descriptor).purpose_summary);
    }

    (void)fprintf(destination_stream, "\nPrimitive operations:\n");
    for (entry_index = 0u; entry_index < LLFPL_ARRAY_LENGTH(primitive_verbs); entry_index++) {
        (void)fprintf(destination_stream, "  %s\n", *(primitive_verbs + entry_index));
    }

    (void)fprintf(destination_stream,
                  "\nComments run from a double hyphen to the end of the line.\n"
                  "Numeric literals accept a sign, a fractional part and an exponent.\n"
                  "Identifiers are at most %d characters long.\n",
                  LLFPL_MAXIMUM_IDENTIFIER_LENGTH);
}

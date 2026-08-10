/*
 * diagnostic_reporter.c -- Message formatting and accounting.
 *
 * All three reporting entry points share one renderer so that the location
 * prefix, the severity label and the trailing newline are produced in exactly
 * one place. Adding a severity therefore cannot introduce a formatting
 * inconsistency.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/core/diagnostic_reporter.h"

#include <stdarg.h>

/* ---- Internal rendering ------------------------------------------------- */

/*
 * Writes "<path>:<line>:<column>: " when the location carries enough
 * information to be useful, and nothing at all otherwise.
 */
static void write_location_prefix(FILE *stream, LlfplSourceLocation location) {
    if (location.module_path == NULL) {
        return;
    }

    if (location.line_number == 0u) {
        (void)fprintf(stream, "%s: ", location.module_path);
        return;
    }

    if (location.column_number == 0u) {
        (void)fprintf(stream, "%s:%u: ", location.module_path, location.line_number);
        return;
    }

    (void)fprintf(
        stream, "%s:%u:%u: ", location.module_path, location.line_number, location.column_number);
}

/*
 * The format string reaching vfprintf is necessarily not a literal here -- that
 * is the whole purpose of a shared renderer. Format checking is not lost: every
 * caller is declared with the printf attribute, so each call site is verified
 * against its own literal at the point where the arguments are actually
 * supplied. The warning is suppressed only across this one statement.
 */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

static void render_message(FILE *stream,
                           const char *severity_label,
                           LlfplSourceLocation location,
                           const char *message_format,
                           va_list message_arguments) {
    (void)fputs("llfpl: ", stream);
    write_location_prefix(stream, location);
    (void)fprintf(stream, "%s: ", severity_label);
    (void)vfprintf(stream, message_format, message_arguments);
    (void)fputc('\n', stream);
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/*
 * Resolves the stream a reporter should write to. A reporter that was never
 * initialised, or was initialised with a null stream, still reports to stderr:
 * losing a diagnostic is worse than reporting it to an unexpected destination.
 */
static FILE *resolve_stream(const LlfplDiagnosticReporter *reporter) {
    if (reporter == NULL || (*reporter).diagnostic_stream == NULL) {
        return stderr;
    }

    return (*reporter).diagnostic_stream;
}

/* ---- Public interface --------------------------------------------------- */

void llfpl_diagnostic_reporter_initialise(LlfplDiagnosticReporter *reporter,
                                          FILE *diagnostic_stream,
                                          int verbose_output_is_enabled) {
    if (reporter == NULL) {
        return;
    }

    (*reporter).diagnostic_stream = (diagnostic_stream != NULL) ? diagnostic_stream : stderr;
    (*reporter).reported_error_count = 0u;
    (*reporter).reported_warning_count = 0u;
    (*reporter).verbose_output_is_enabled = verbose_output_is_enabled;
}

void llfpl_diagnostic_report_error(LlfplDiagnosticReporter *reporter,
                                   LlfplSourceLocation location,
                                   const char *message_format,
                                   ...) {
    va_list message_arguments;

    va_start(message_arguments, message_format);
    render_message(resolve_stream(reporter), "error", location, message_format, message_arguments);
    va_end(message_arguments);

    if (reporter != NULL) {
        (*reporter).reported_error_count++;
    }
}

void llfpl_diagnostic_report_warning(LlfplDiagnosticReporter *reporter,
                                     LlfplSourceLocation location,
                                     const char *message_format,
                                     ...) {
    va_list message_arguments;

    va_start(message_arguments, message_format);
    render_message(
        resolve_stream(reporter), "warning", location, message_format, message_arguments);
    va_end(message_arguments);

    if (reporter != NULL) {
        (*reporter).reported_warning_count++;
    }
}

void llfpl_diagnostic_report_trace(LlfplDiagnosticReporter *reporter,
                                   const char *message_format,
                                   ...) {
    va_list message_arguments;

    if (reporter == NULL || !(*reporter).verbose_output_is_enabled) {
        return;
    }

    va_start(message_arguments, message_format);
    render_message(resolve_stream(reporter),
                   "trace",
                   llfpl_source_location_unknown(),
                   message_format,
                   message_arguments);
    va_end(message_arguments);
}

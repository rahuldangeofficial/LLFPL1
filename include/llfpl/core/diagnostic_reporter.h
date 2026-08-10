/*
 * diagnostic_reporter.h -- Centralised reporting of errors, warnings and
 *                          verbose trace output.
 *
 * Every subsystem funnels its messages through one reporter instance instead of
 * calling printf directly. That single choke point is what lets the command
 * line interface redirect diagnostics away from the result stream, silence
 * trace output, and decide the process exit code from an authoritative error
 * count rather than from a guess.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_DIAGNOSTIC_REPORTER_H
#define LLFPL_CORE_DIAGNOSTIC_REPORTER_H

#include <stdint.h>
#include <stdio.h>

#include "llfpl/core/compiler_attributes.h"

/*
 * Where a diagnostic happened. A location with a null module path or a zero
 * line number prints as an unlocated message, which is the correct rendering
 * for problems discovered before any source text was read.
 */
typedef struct {
    const char *module_path; /* Borrowed; must outlive the reporting call. */
    uint32_t line_number;    /* One-based; zero means unknown.             */
    uint32_t column_number;  /* One-based; zero means unknown.             */
} LlfplSourceLocation;

typedef struct {
    FILE *diagnostic_stream;         /* Destination for every message.        */
    uint32_t reported_error_count;   /* Monotonic; drives the exit status.    */
    uint32_t reported_warning_count; /* Monotonic; informational only.        */
    int verbose_output_is_enabled;   /* Gates llfpl_diagnostic_report_trace.  */
} LlfplDiagnosticReporter;

/* Builds an unlocated source location. */
static LLFPL_ALWAYS_INLINE LlfplSourceLocation llfpl_source_location_unknown(void) {
    LlfplSourceLocation location;

    location.module_path = NULL;
    location.line_number = 0u;
    location.column_number = 0u;

    return location;
}

/* Builds a source location that points at a specific line and column. */
static LLFPL_ALWAYS_INLINE LlfplSourceLocation llfpl_source_location_make(const char *module_path,
                                                                          uint32_t line_number,
                                                                          uint32_t column_number) {
    LlfplSourceLocation location;

    location.module_path = module_path;
    location.line_number = line_number;
    location.column_number = column_number;

    return location;
}

/*
 * Prepares a reporter. Passing a null stream selects stderr, which is the
 * correct default for a command line tool: diagnostics must never contaminate
 * the result stream that a caller is parsing.
 */
void llfpl_diagnostic_reporter_initialise(LlfplDiagnosticReporter *reporter,
                                          FILE *diagnostic_stream,
                                          int verbose_output_is_enabled);

void llfpl_diagnostic_report_error(LlfplDiagnosticReporter *reporter,
                                   LlfplSourceLocation location,
                                   const char *message_format,
                                   ...) LLFPL_PRINTF_LIKE(3, 4);

void llfpl_diagnostic_report_warning(LlfplDiagnosticReporter *reporter,
                                     LlfplSourceLocation location,
                                     const char *message_format,
                                     ...) LLFPL_PRINTF_LIKE(3, 4);

/*
 * Emits a message only when verbose output is enabled. Trace messages describe
 * what the runtime is doing (modules loaded, identities bound, arenas
 * reserved); they are never part of a program's observable result.
 */
void llfpl_diagnostic_report_trace(LlfplDiagnosticReporter *reporter,
                                   const char *message_format,
                                   ...) LLFPL_PRINTF_LIKE(2, 3);

static LLFPL_ALWAYS_INLINE int
llfpl_diagnostic_reporter_has_errors(const LlfplDiagnosticReporter *reporter) {
    return reporter != NULL && (*reporter).reported_error_count > 0u;
}

#endif /* LLFPL_CORE_DIAGNOSTIC_REPORTER_H */

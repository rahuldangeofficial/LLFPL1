/*
 * status_code.h -- The result vocabulary shared by every LLFPL subsystem.
 *
 * Operations that can fail return an LlfplStatus rather than a bare integer or
 * a sentinel pointer, so a caller never has to guess what a failure meant. The
 * enumeration is deliberately small: each constant maps to a distinct recovery
 * strategy, which is the only reason for a status to exist at all.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_STATUS_CODE_H
#define LLFPL_CORE_STATUS_CODE_H

#include "llfpl/core/compiler_attributes.h"

typedef enum {
    /* The operation completed and any output parameter is valid. */
    LLFPL_STATUS_OK = 0,

    /* A precondition on the arguments was violated; a programming error. */
    LLFPL_STATUS_INVALID_ARGUMENT,

    /* The host refused a required allocation. */
    LLFPL_STATUS_OUT_OF_MEMORY,

    /* A file could not be opened, inspected, or mapped. */
    LLFPL_STATUS_IO_FAILURE,

    /* A fixed capacity declared in configuration_limits.h was reached. */
    LLFPL_STATUS_CAPACITY_EXCEEDED,

    /* The requested name does not exist in the searched scope. */
    LLFPL_STATUS_NOT_FOUND,

    /* The source text does not conform to the LLFPL grammar. */
    LLFPL_STATUS_SYNTAX_ERROR,

    /* The source text parses but asks for something meaningless. */
    LLFPL_STATUS_SEMANTIC_ERROR,

    /* Recursive descent exceeded LLFPL_MAXIMUM_EVALUATION_DEPTH. */
    LLFPL_STATUS_RECURSION_LIMIT_EXCEEDED
} LlfplStatus;

/*
 * Returns a stable, lower-case, human-readable description of a status. The
 * returned string has static storage duration and is never null, so it is
 * always safe to pass straight into a format string.
 */
const char *llfpl_status_describe(LlfplStatus status) LLFPL_CONST_FUNCTION;

/* Convenience predicate that reads better than an equality test at call sites. */
static LLFPL_ALWAYS_INLINE int llfpl_status_is_ok(LlfplStatus status) {
    return status == LLFPL_STATUS_OK;
}

#endif /* LLFPL_CORE_STATUS_CODE_H */

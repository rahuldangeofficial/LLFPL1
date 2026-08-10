/*
 * status_code.c -- Descriptions for the shared status vocabulary.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/core/status_code.h"

const char *llfpl_status_describe(LlfplStatus status) {
    switch (status) {
        case LLFPL_STATUS_OK:
            return "success";
        case LLFPL_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case LLFPL_STATUS_OUT_OF_MEMORY:
            return "out of memory";
        case LLFPL_STATUS_IO_FAILURE:
            return "input or output failure";
        case LLFPL_STATUS_CAPACITY_EXCEEDED:
            return "static capacity exceeded";
        case LLFPL_STATUS_NOT_FOUND:
            return "not found";
        case LLFPL_STATUS_SYNTAX_ERROR:
            return "syntax error";
        case LLFPL_STATUS_SEMANTIC_ERROR:
            return "semantic error";
        case LLFPL_STATUS_RECURSION_LIMIT_EXCEEDED:
            return "evaluation depth limit exceeded";
    }

    /*
     * Unreachable for any value produced by this codebase. Kept so that the
     * function is total even when a status crosses an ABI boundary.
     */
    return "unrecognised status";
}

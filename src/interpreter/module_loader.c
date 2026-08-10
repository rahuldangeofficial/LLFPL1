/*
 * module_loader.c -- Module path resolution and load tracking.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/interpreter/module_loader.h"

#include <string.h>

#include "llfpl/core/filesystem_path.h"

/* ---- Internal helpers ---------------------------------------------------- */

/*
 * Attempts one candidate. Canonicalisation both normalises the path and proves
 * the file exists, so a successful call is a complete answer and a failed one
 * costs nothing more than a stat.
 */
static int
try_candidate(const char *candidate_path, char *canonical_path_out, size_t buffer_capacity) {
    return llfpl_status_is_ok(
        llfpl_path_canonicalise(candidate_path, canonical_path_out, buffer_capacity));
}

/* Joins a directory with the specification and tries the result. */
static int try_within_directory(const char *directory_path,
                                const char *module_specification,
                                char *canonical_path_out,
                                size_t buffer_capacity) {
    char candidate_path[LLFPL_MODULE_PATH_BUFFER_CAPACITY];

    if (!llfpl_status_is_ok(llfpl_path_join(
            directory_path, module_specification, candidate_path, sizeof(candidate_path)))) {
        return 0;
    }

    return try_candidate(candidate_path, canonical_path_out, buffer_capacity);
}

/* ---- Lifecycle ----------------------------------------------------------- */

void llfpl_module_loader_initialise(LlfplModuleLoader *loader) {
    if (loader == NULL) {
        return;
    }

    (*loader).search_directory_count = 0u;
    (*loader).loaded_module_count = 0u;
}

/* ---- Search directories --------------------------------------------------- */

LlfplStatus llfpl_module_loader_add_search_directory(LlfplModuleLoader *loader,
                                                     const char *directory_path) {
    size_t directory_length = 0u;

    if (loader == NULL || directory_path == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if ((*loader).search_directory_count >= LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    directory_length = strlen(directory_path);
    if (directory_length + 1u > LLFPL_MODULE_PATH_BUFFER_CAPACITY) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(*((*loader).search_directories + (*loader).search_directory_count),
           directory_path,
           directory_length + 1u);
    (*loader).search_directory_count++;

    return LLFPL_STATUS_OK;
}

void llfpl_module_loader_add_default_directories(LlfplModuleLoader *loader,
                                                 const char *invocation_argument) {
    /* Relative to the executable, in the order a build tree then an install. */
    static const char *const relative_library_directories[] = {
        "../lib/llfpl",
        "../share/llfpl/lib",
    };

    char executable_path[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    char executable_directory[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    size_t relative_index = 0u;

    if (loader == NULL) {
        return;
    }

    if (!llfpl_status_is_ok(llfpl_path_of_running_executable(
            invocation_argument, executable_path, sizeof(executable_path)))) {
        return;
    }

    if (!llfpl_status_is_ok(llfpl_path_extract_directory(
            executable_path, executable_directory, sizeof(executable_directory)))) {
        return;
    }

    for (relative_index = 0u; relative_index < LLFPL_ARRAY_LENGTH(relative_library_directories);
         relative_index++) {
        char candidate_directory[LLFPL_MODULE_PATH_BUFFER_CAPACITY];
        char canonical_directory[LLFPL_MODULE_PATH_BUFFER_CAPACITY];

        if (!llfpl_status_is_ok(llfpl_path_join(executable_directory,
                                                *(relative_library_directories + relative_index),
                                                candidate_directory,
                                                sizeof(candidate_directory)))) {
            continue;
        }

        /* Register only directories that exist, so resolution never wastes a
         * stat on a layout this installation does not use. */
        if (!llfpl_status_is_ok(llfpl_path_canonicalise(
                candidate_directory, canonical_directory, sizeof(canonical_directory)))) {
            continue;
        }

        (void)llfpl_module_loader_add_search_directory(loader, canonical_directory);
    }
}

/* ---- Resolution ----------------------------------------------------------- */

LlfplStatus llfpl_module_loader_resolve(const LlfplModuleLoader *loader,
                                        const char *requesting_module_path,
                                        const char *module_specification,
                                        char *canonical_path_out,
                                        size_t buffer_capacity) {
    uint32_t search_index = 0u;

    if (loader == NULL || module_specification == NULL || canonical_path_out == NULL ||
        buffer_capacity == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (llfpl_path_is_absolute(module_specification)) {
        return try_candidate(module_specification, canonical_path_out, buffer_capacity)
                   ? LLFPL_STATUS_OK
                   : LLFPL_STATUS_NOT_FOUND;
    }

    if (requesting_module_path != NULL) {
        char requesting_directory[LLFPL_MODULE_PATH_BUFFER_CAPACITY];

        if (llfpl_status_is_ok(llfpl_path_extract_directory(
                requesting_module_path, requesting_directory, sizeof(requesting_directory))) &&
            try_within_directory(
                requesting_directory, module_specification, canonical_path_out, buffer_capacity)) {
            return LLFPL_STATUS_OK;
        }
    }

    for (search_index = 0u; search_index < (*loader).search_directory_count; search_index++) {
        if (try_within_directory(*((*loader).search_directories + search_index),
                                 module_specification,
                                 canonical_path_out,
                                 buffer_capacity)) {
            return LLFPL_STATUS_OK;
        }
    }

    /* Last resort: interpret the specification against the working directory. */
    if (try_candidate(module_specification, canonical_path_out, buffer_capacity)) {
        return LLFPL_STATUS_OK;
    }

    return LLFPL_STATUS_NOT_FOUND;
}

/* ---- Load tracking --------------------------------------------------------- */

int llfpl_module_loader_is_loaded(const LlfplModuleLoader *loader, const char *canonical_path) {
    uint32_t module_index = 0u;

    if (loader == NULL || canonical_path == NULL) {
        return 0;
    }

    for (module_index = 0u; module_index < (*loader).loaded_module_count; module_index++) {
        if (strcmp(*((*loader).loaded_module_paths + module_index), canonical_path) == 0) {
            return 1;
        }
    }

    return 0;
}

LlfplStatus llfpl_module_loader_mark_loaded(LlfplModuleLoader *loader, const char *canonical_path) {
    size_t path_length = 0u;

    if (loader == NULL || canonical_path == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if ((*loader).loaded_module_count >= LLFPL_MAXIMUM_LOADED_MODULES) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    path_length = strlen(canonical_path);
    if (path_length + 1u > LLFPL_MODULE_PATH_BUFFER_CAPACITY) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(*((*loader).loaded_module_paths + (*loader).loaded_module_count),
           canonical_path,
           path_length + 1u);
    (*loader).loaded_module_count++;

    return LLFPL_STATUS_OK;
}

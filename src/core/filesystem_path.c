/*
 * filesystem_path.c -- Bounded path composition and canonicalisation.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/core/filesystem_path.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include "llfpl/core/configuration_limits.h"

/* ---- Internal helpers --------------------------------------------------- */

/*
 * Copies source_text into destination_buffer, refusing rather than truncating.
 * Every path-producing function in this file funnels its final write through
 * here, which is what makes "no silent truncation" a property of the module
 * instead of a habit that each function has to remember.
 */
static LlfplStatus
copy_without_truncation(const char *source_text, char *destination_buffer, size_t buffer_capacity) {
    const size_t source_length = strlen(source_text);

    if (source_length + 1u > buffer_capacity) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(destination_buffer, source_text, source_length + 1u);
    return LLFPL_STATUS_OK;
}

/* ---- Public interface --------------------------------------------------- */

int llfpl_path_is_absolute(const char *path) {
    return path != NULL && *path == LLFPL_PATH_SEPARATOR_CHARACTER;
}

LlfplStatus
llfpl_path_extract_directory(const char *path, char *destination_buffer, size_t buffer_capacity) {
    const char *last_separator = NULL;
    size_t directory_length = 0u;

    if (path == NULL || destination_buffer == NULL || buffer_capacity == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    last_separator = strrchr(path, LLFPL_PATH_SEPARATOR_CHARACTER);

    if (last_separator == NULL) {
        return copy_without_truncation(".", destination_buffer, buffer_capacity);
    }

    /* A path of the form "/name" has the filesystem root as its directory. */
    if (last_separator == path) {
        return copy_without_truncation("/", destination_buffer, buffer_capacity);
    }

    directory_length = (size_t)(last_separator - path);

    if (directory_length + 1u > buffer_capacity) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(destination_buffer, path, directory_length);
    *(destination_buffer + directory_length) = '\0';

    return LLFPL_STATUS_OK;
}

LlfplStatus llfpl_path_join(const char *directory_path,
                            const char *relative_path,
                            char *destination_buffer,
                            size_t buffer_capacity) {
    size_t directory_length = 0u;
    size_t relative_length = 0u;
    size_t separator_length = 0u;
    size_t total_length = 0u;

    if (directory_path == NULL || relative_path == NULL || destination_buffer == NULL ||
        buffer_capacity == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    /* An absolute leaf ignores the directory entirely, as the shell would. */
    if (llfpl_path_is_absolute(relative_path)) {
        return copy_without_truncation(relative_path, destination_buffer, buffer_capacity);
    }

    if (*directory_path == '\0' || strcmp(directory_path, ".") == 0) {
        return copy_without_truncation(relative_path, destination_buffer, buffer_capacity);
    }

    directory_length = strlen(directory_path);
    relative_length = strlen(relative_path);

    /* Do not double the separator when the directory already ends with one. */
    separator_length =
        (*(directory_path + directory_length - 1u) == LLFPL_PATH_SEPARATOR_CHARACTER) ? 0u : 1u;

    total_length = directory_length + separator_length + relative_length;

    if (total_length + 1u > buffer_capacity) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memcpy(destination_buffer, directory_path, directory_length);

    if (separator_length == 1u) {
        *(destination_buffer + directory_length) = LLFPL_PATH_SEPARATOR_CHARACTER;
    }

    memcpy(
        destination_buffer + directory_length + separator_length, relative_path, relative_length);
    *(destination_buffer + total_length) = '\0';

    return LLFPL_STATUS_OK;
}

LlfplStatus
llfpl_path_canonicalise(const char *path, char *destination_buffer, size_t buffer_capacity) {
    char resolved_path[PATH_MAX];

    if (path == NULL || destination_buffer == NULL || buffer_capacity == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    /*
     * realpath is given an explicit PATH_MAX buffer rather than a null pointer.
     * The null form allocates, and this function is called on every module
     * resolution attempt, including the ones that fail.
     */
    if (realpath(path, resolved_path) == NULL) {
        return LLFPL_STATUS_NOT_FOUND;
    }

    return copy_without_truncation(resolved_path, destination_buffer, buffer_capacity);
}

LlfplStatus llfpl_path_of_running_executable(const char *invocation_argument,
                                             char *destination_buffer,
                                             size_t buffer_capacity) {
    if (destination_buffer == NULL || buffer_capacity == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

#if defined(__APPLE__)
    {
        char reported_path[PATH_MAX];
        uint32_t reported_capacity = (uint32_t)sizeof(reported_path);

        if (_NSGetExecutablePath(reported_path, &reported_capacity) == 0) {
            return llfpl_path_canonicalise(reported_path, destination_buffer, buffer_capacity);
        }
    }
#elif defined(__linux__)
    {
        char reported_path[PATH_MAX];
        const ssize_t reported_length =
            readlink("/proc/self/exe", reported_path, sizeof(reported_path) - 1u);

        if (reported_length > 0) {
            *(reported_path + reported_length) = '\0';
            return copy_without_truncation(reported_path, destination_buffer, buffer_capacity);
        }
    }
#endif

    /*
     * Last resort: the invocation argument. It is only usable when it contains
     * a separator, because a bare command name was resolved through PATH and
     * says nothing about where the binary actually lives.
     */
    if (invocation_argument != NULL &&
        strchr(invocation_argument, LLFPL_PATH_SEPARATOR_CHARACTER) != NULL) {
        return llfpl_path_canonicalise(invocation_argument, destination_buffer, buffer_capacity);
    }

    return LLFPL_STATUS_NOT_FOUND;
}

int llfpl_path_has_extension(const char *path, const char *extension) {
    size_t path_length = 0u;
    size_t extension_length = 0u;

    if (path == NULL || extension == NULL) {
        return 0;
    }

    path_length = strlen(path);
    extension_length = strlen(extension);

    if (path_length < extension_length) {
        return 0;
    }

    return strcmp(path + path_length - extension_length, extension) == 0;
}

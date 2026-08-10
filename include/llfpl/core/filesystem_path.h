/*
 * filesystem_path.h -- Bounded path manipulation for the module loader.
 *
 * Every function writes into a caller-supplied buffer and reports truncation as
 * a failure instead of silently producing a shorter path. Silent truncation is
 * how a loader ends up opening the wrong file, so it is treated here as an
 * error condition on equal footing with a missing file.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_FILESYSTEM_PATH_H
#define LLFPL_CORE_FILESYSTEM_PATH_H

#include <stddef.h>

#include "llfpl/core/status_code.h"

/* Directory separator used when composing paths on this host. */
#define LLFPL_PATH_SEPARATOR_CHARACTER '/'

/* True when the path is rooted and therefore needs no search directory. */
int llfpl_path_is_absolute(const char *path);

/*
 * Copies the directory portion of path into destination_buffer, without a
 * trailing separator. A path with no separator yields ".", so the result is
 * always a directory that can be joined against.
 */
LlfplStatus
llfpl_path_extract_directory(const char *path, char *destination_buffer, size_t buffer_capacity);

/*
 * Joins directory_path and relative_path with exactly one separator. An empty
 * or "." directory yields relative_path unchanged, which keeps composed paths
 * readable in diagnostics.
 */
LlfplStatus llfpl_path_join(const char *directory_path,
                            const char *relative_path,
                            char *destination_buffer,
                            size_t buffer_capacity);

/*
 * Resolves symbolic links and relative segments to produce the canonical
 * absolute path. Fails when the path does not exist, which makes this the
 * loader's existence test as well as its identity test: two specifications that
 * canonicalise to the same string denote the same module.
 */
LlfplStatus
llfpl_path_canonicalise(const char *path, char *destination_buffer, size_t buffer_capacity);

/*
 * Absolute path of the currently running executable, used to locate the bundled
 * standard library relative to the installed binary. Falls back to the
 * invocation argument when the host offers no direct query.
 */
LlfplStatus llfpl_path_of_running_executable(const char *invocation_argument,
                                             char *destination_buffer,
                                             size_t buffer_capacity);

/* Case-sensitive suffix test used to enforce the .LLFPL source extension. */
int llfpl_path_has_extension(const char *path, const char *extension);

#endif /* LLFPL_CORE_FILESYSTEM_PATH_H */

/*
 * source_file_mapping.h -- Read-only memory maps of LLFPL source modules.
 *
 * The frontend never copies source text. Atoms, and the template bodies built
 * from them, are pointers straight into a mapped file, which is what makes
 * template invocation free of allocation. The consequence is a lifetime rule
 * the registry below exists to enforce: every mapping must stay resident until
 * the interpreter session that parsed it is torn down.
 *
 * Mapped text is treated as a bounded byte range, never as a null-terminated
 * string. A file whose length is an exact multiple of the page size has no
 * trailing zero byte to find, so any scanner that relied on one would read past
 * the end of the mapping on precisely those files.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_MEMORY_SOURCE_FILE_MAPPING_H
#define LLFPL_MEMORY_SOURCE_FILE_MAPPING_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

typedef struct {
    const char *text_begin;                                 /* First mapped byte.        */
    size_t text_length_in_bytes;                            /* Exact extent of the map.  */
    int file_descriptor;                                    /* Held open for the map.    */
    char canonical_path[LLFPL_MODULE_PATH_BUFFER_CAPACITY]; /* Identity for reports. */
} LlfplSourceFileMapping;

/*
 * A session-lifetime collection of mappings. Holding the mappings here, rather
 * than in the parser that created them, is what keeps template bodies valid
 * long after the module that defined them finished executing.
 */
typedef struct {
    LlfplSourceFileMapping mappings[LLFPL_MAXIMUM_LOADED_MODULES];
    uint32_t mapping_count;
} LlfplSourceFileMappingRegistry;

void llfpl_source_file_mapping_registry_initialise(LlfplSourceFileMappingRegistry *registry);

/*
 * Maps the file at canonical_path and retains it in the registry. On success
 * the mapping pointer written to mapping_out remains valid until the registry
 * is released. An empty file is rejected: it can define nothing, and accepting
 * it would only push the failure to a less informative place.
 */
LlfplStatus llfpl_source_file_mapping_registry_open(LlfplSourceFileMappingRegistry *registry,
                                                    const char *canonical_path,
                                                    const LlfplSourceFileMapping **mapping_out)
    LLFPL_WARN_UNUSED_RESULT;

/* Unmaps every retained mapping and closes every descriptor. */
void llfpl_source_file_mapping_registry_release(LlfplSourceFileMappingRegistry *registry);

#endif /* LLFPL_MEMORY_SOURCE_FILE_MAPPING_H */

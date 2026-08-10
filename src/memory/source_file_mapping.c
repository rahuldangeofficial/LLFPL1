/*
 * source_file_mapping.c -- Read-only mmap of source modules with session-scoped
 *                          lifetime management.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/memory/source_file_mapping.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void llfpl_source_file_mapping_registry_initialise(LlfplSourceFileMappingRegistry *registry) {
    if (registry == NULL) {
        return;
    }

    (*registry).mapping_count = 0u;
}

LlfplStatus llfpl_source_file_mapping_registry_open(LlfplSourceFileMappingRegistry *registry,
                                                    const char *canonical_path,
                                                    const LlfplSourceFileMapping **mapping_out) {
    LlfplSourceFileMapping *mapping = NULL;
    struct stat file_status;
    const void *mapped_address = NULL;
    size_t path_length = 0u;
    int file_descriptor = -1;

    if (registry == NULL || canonical_path == NULL || mapping_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    *mapping_out = NULL;

    if ((*registry).mapping_count >= LLFPL_MAXIMUM_LOADED_MODULES) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    path_length = strlen(canonical_path);
    if (path_length + 1u > LLFPL_MODULE_PATH_BUFFER_CAPACITY) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    file_descriptor = open(canonical_path, O_RDONLY | O_CLOEXEC);
    if (file_descriptor < 0) {
        return LLFPL_STATUS_IO_FAILURE;
    }

    if (fstat(file_descriptor, &file_status) != 0) {
        (void)close(file_descriptor);
        return LLFPL_STATUS_IO_FAILURE;
    }

    /* Only a regular file has a stable extent that can be mapped safely. */
    if (!S_ISREG(file_status.st_mode) || file_status.st_size <= 0) {
        (void)close(file_descriptor);
        return LLFPL_STATUS_IO_FAILURE;
    }

    mapped_address =
        mmap(NULL, (size_t)file_status.st_size, PROT_READ, MAP_PRIVATE, file_descriptor, 0);

    if (mapped_address == MAP_FAILED) {
        (void)close(file_descriptor);
        return LLFPL_STATUS_IO_FAILURE;
    }

    mapping = (*registry).mappings + (*registry).mapping_count;

    (*mapping).text_begin = (const char *)mapped_address;
    (*mapping).text_length_in_bytes = (size_t)file_status.st_size;
    (*mapping).file_descriptor = file_descriptor;
    memcpy((*mapping).canonical_path, canonical_path, path_length + 1u);

    (*registry).mapping_count++;

    *mapping_out = mapping;
    return LLFPL_STATUS_OK;
}

void llfpl_source_file_mapping_registry_release(LlfplSourceFileMappingRegistry *registry) {
    uint32_t mapping_index = 0u;

    if (registry == NULL) {
        return;
    }

    for (mapping_index = 0u; mapping_index < (*registry).mapping_count; mapping_index++) {
        LlfplSourceFileMapping *mapping = (*registry).mappings + mapping_index;

        if ((*mapping).text_begin != NULL) {
            /* The const qualifier describes our use, not the kernel's ownership. */
            (void)munmap((void *)(uintptr_t)(*mapping).text_begin, (*mapping).text_length_in_bytes);
            (*mapping).text_begin = NULL;
        }

        if ((*mapping).file_descriptor >= 0) {
            (void)close((*mapping).file_descriptor);
            (*mapping).file_descriptor = -1;
        }
    }

    (*registry).mapping_count = 0u;
}

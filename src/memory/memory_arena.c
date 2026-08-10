/*
 * memory_arena.c -- Reservation and bounds-checked access for named arenas.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/memory/memory_arena.h"

#include <string.h>

#include "llfpl/core/identifier.h"
#include "llfpl/memory/aligned_allocation.h"

/* Width of the only value type an arena stores. */
#define LLFPL_ARENA_VALUE_WIDTH_IN_BYTES ((uint32_t)sizeof(double))

/* ---- Internal helpers --------------------------------------------------- */

/*
 * Overflow-proof bounds test. Written as a subtraction from the capacity rather
 * than an addition to the offset, because the additive form wraps for offsets
 * within eight bytes of the 32-bit ceiling and would then admit an out-of-range
 * access as if it were in range.
 */
static int offset_admits_a_value(const LlfplMemoryArena *arena, uint32_t byte_offset) {
    if ((*arena).capacity_in_bytes < LLFPL_ARENA_VALUE_WIDTH_IN_BYTES) {
        return 0;
    }

    return byte_offset <= (*arena).capacity_in_bytes - LLFPL_ARENA_VALUE_WIDTH_IN_BYTES;
}

/* ---- Lifecycle ---------------------------------------------------------- */

LlfplStatus llfpl_memory_arena_registry_initialise(LlfplMemoryArenaRegistry *registry,
                                                   uint16_t alignment_in_bytes) {
    if (registry == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (!llfpl_alignment_is_valid((size_t)alignment_in_bytes)) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    memset((*registry).arenas, 0, sizeof((*registry).arenas));
    (*registry).arena_count = 0u;
    (*registry).alignment_in_bytes = alignment_in_bytes;

    return LLFPL_STATUS_OK;
}

void llfpl_memory_arena_registry_release(LlfplMemoryArenaRegistry *registry) {
    uint32_t arena_index = 0u;

    if (registry == NULL) {
        return;
    }

    for (arena_index = 0u; arena_index < (*registry).arena_count; arena_index++) {
        LlfplMemoryArena *arena = (*registry).arenas + arena_index;

        llfpl_aligned_release((*arena).base_address);
        (*arena).base_address = NULL;
        (*arena).capacity_in_bytes = 0u;
    }

    (*registry).arena_count = 0u;
}

/* ---- Reservation and lookup --------------------------------------------- */

LlfplStatus llfpl_memory_arena_registry_reserve(LlfplMemoryArenaRegistry *registry,
                                                const char *arena_name,
                                                uint32_t capacity_in_bytes) {
    LlfplMemoryArena *arena = NULL;
    void *reserved_storage = NULL;
    LlfplStatus status = LLFPL_STATUS_OK;

    if (registry == NULL || arena_name == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (capacity_in_bytes == 0u || capacity_in_bytes > LLFPL_MEMORY_ARENA_MAXIMUM_SIZE_IN_BYTES) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if ((*registry).arena_count >= LLFPL_MEMORY_ARENA_REGISTRY_CAPACITY) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    if (llfpl_memory_arena_registry_lookup(registry, arena_name) != NULL) {
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    arena = (*registry).arenas + (*registry).arena_count;

    status = llfpl_identifier_assign_terminated((*arena).name, arena_name);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    status = llfpl_aligned_allocate(
        (size_t)(*registry).alignment_in_bytes, (size_t)capacity_in_bytes, &reserved_storage);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    (*arena).base_address = (uint8_t *)reserved_storage;
    (*arena).capacity_in_bytes = capacity_in_bytes;

    (*registry).arena_count++;

    return LLFPL_STATUS_OK;
}

LlfplMemoryArena *llfpl_memory_arena_registry_lookup(LlfplMemoryArenaRegistry *registry,
                                                     const char *arena_name) {
    char search_key[LLFPL_IDENTIFIER_BUFFER_CAPACITY];
    uint32_t arena_index = 0u;

    if (registry == NULL || arena_name == NULL) {
        return NULL;
    }

    /*
     * Normalising the search term into a padded buffer once turns the scan into
     * a sequence of fixed-width comparisons, rather than a strcmp per arena.
     * A name too long to be an arena name cannot match anything.
     */
    if (!llfpl_status_is_ok(llfpl_identifier_assign_terminated(search_key, arena_name))) {
        return NULL;
    }

    for (arena_index = 0u; arena_index < (*registry).arena_count; arena_index++) {
        LlfplMemoryArena *arena = (*registry).arenas + arena_index;

        if (llfpl_identifier_equals((*arena).name, search_key)) {
            return arena;
        }
    }

    return NULL;
}

/* ---- Element access ------------------------------------------------------ */

LlfplStatus
llfpl_memory_arena_store_double(LlfplMemoryArena *arena, uint32_t byte_offset, double value) {
    if (arena == NULL || (*arena).base_address == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (!offset_admits_a_value(arena, byte_offset)) {
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    memcpy((*arena).base_address + byte_offset, &value, sizeof(value));

    return LLFPL_STATUS_OK;
}

LlfplStatus llfpl_memory_arena_load_double(const LlfplMemoryArena *arena,
                                           uint32_t byte_offset,
                                           double *value_out) {
    if (arena == NULL || (*arena).base_address == NULL || value_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (!offset_admits_a_value(arena, byte_offset)) {
        return LLFPL_STATUS_SEMANTIC_ERROR;
    }

    memcpy(value_out, (*arena).base_address + byte_offset, sizeof(*value_out));

    return LLFPL_STATUS_OK;
}

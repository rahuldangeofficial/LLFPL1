/*
 * memory_arena.h -- Named, cache-line-aligned data buffers.
 *
 * An arena is the only mutable data store an LLFPL program can address. It is
 * a flat span of bytes, reserved once by a Slab declaration, aligned to the
 * host cache line, and addressed by byte offset. There is no allocator, no free
 * list and no reclamation: an arena lives from its declaration until the
 * session ends. That is what removes allocator latency, and its variance, from
 * the measured execution path entirely.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_MEMORY_MEMORY_ARENA_H
#define LLFPL_MEMORY_MEMORY_ARENA_H

#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

typedef struct {
    char name[LLFPL_IDENTIFIER_BUFFER_CAPACITY]; /* Inline, so a lookup hits one line. */
    uint8_t *base_address;                       /* Aligned to the host cache line.    */
    uint32_t capacity_in_bytes;                  /* Exact reserved extent.             */
} LlfplMemoryArena;

typedef struct {
    LlfplMemoryArena arenas[LLFPL_MEMORY_ARENA_REGISTRY_CAPACITY];
    uint32_t arena_count;
    uint16_t alignment_in_bytes; /* Probed cache line size for this host. */
} LlfplMemoryArenaRegistry;

/*
 * Prepares an empty registry. alignment_in_bytes comes from the hardware probe
 * and must be a valid alignment; an invalid value is rejected rather than
 * corrected, so a broken probe surfaces at start-up instead of degrading every
 * subsequent reservation.
 */
LlfplStatus llfpl_memory_arena_registry_initialise(LlfplMemoryArenaRegistry *registry,
                                                   uint16_t alignment_in_bytes);

/* Releases every reserved arena and empties the registry. */
void llfpl_memory_arena_registry_release(LlfplMemoryArenaRegistry *registry);

/*
 * Reserves a named arena of capacity_in_bytes. Re-declaring an existing name is
 * rejected with LLFPL_STATUS_SEMANTIC_ERROR rather than silently reserving a
 * second arena that the first name would shadow.
 */
LlfplStatus llfpl_memory_arena_registry_reserve(LlfplMemoryArenaRegistry *registry,
                                                const char *arena_name,
                                                uint32_t capacity_in_bytes);

/* Returns the named arena, or null when no such arena has been reserved. */
LlfplMemoryArena *llfpl_memory_arena_registry_lookup(LlfplMemoryArenaRegistry *registry,
                                                     const char *arena_name);

/*
 * Stores a double at a byte offset. The offset is bounds-checked in a form that
 * cannot overflow: subtracting the value width from the capacity is safe
 * because a reservation is never smaller than that width, whereas the more
 * obvious "offset + width > capacity" wraps for offsets near the 32-bit limit.
 *
 * The transfer uses memcpy so that an offset which is not a multiple of eight
 * remains well defined. On every supported target this compiles to the same
 * single store instruction a direct assignment would emit.
 */
LlfplStatus
llfpl_memory_arena_store_double(LlfplMemoryArena *arena, uint32_t byte_offset, double value);

/* Loads a double from a byte offset, with the same bounds discipline. */
LlfplStatus llfpl_memory_arena_load_double(const LlfplMemoryArena *arena,
                                           uint32_t byte_offset,
                                           double *value_out);

#endif /* LLFPL_MEMORY_MEMORY_ARENA_H */

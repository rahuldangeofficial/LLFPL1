/*
 * aligned_allocation.c -- posix_memalign wrapper with validation and zeroing.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/memory/aligned_allocation.h"

#include <stdlib.h>
#include <string.h>

int llfpl_alignment_is_valid(size_t alignment_in_bytes) {
    if (alignment_in_bytes < sizeof(void *)) {
        return 0;
    }

    /* A power of two clears its own low bits when one is subtracted. */
    return (alignment_in_bytes & (alignment_in_bytes - 1u)) == 0u;
}

LlfplStatus
llfpl_aligned_allocate(size_t alignment_in_bytes, size_t size_in_bytes, void **allocation_out) {
    size_t rounded_size_in_bytes = 0u;
    void *allocation = NULL;

    if (allocation_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    *allocation_out = NULL;

    if (!llfpl_alignment_is_valid(alignment_in_bytes) || size_in_bytes == 0u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Round the request up to a whole number of alignment units. posix_memalign
     * requires this on some implementations, and it guarantees that the tail of
     * the allocation never shares a cache line with the next object.
     */
    rounded_size_in_bytes = (size_in_bytes + alignment_in_bytes - 1u) & ~(alignment_in_bytes - 1u);

    /* Reject a request whose rounding wrapped the address space. */
    if (rounded_size_in_bytes < size_in_bytes) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (posix_memalign(&allocation, alignment_in_bytes, rounded_size_in_bytes) != 0) {
        return LLFPL_STATUS_OUT_OF_MEMORY;
    }

    memset(allocation, 0, rounded_size_in_bytes);

    *allocation_out = allocation;
    return LLFPL_STATUS_OK;
}

void llfpl_aligned_release(void *allocation) {
    /* free accepts a null pointer, so no guard is required. */
    free(allocation);
}

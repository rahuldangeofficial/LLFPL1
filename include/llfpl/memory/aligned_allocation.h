/*
 * aligned_allocation.h -- Boundary-guaranteed heap allocation.
 *
 * Every long-lived buffer in the runtime -- the register file, the symbol slab,
 * each memory arena -- is placed on a cache line boundary. Alignment is not
 * cosmetic here: an unaligned buffer makes a structure that was sized to fit
 * one line occupy two, doubling the number of lines a hot loop touches.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_MEMORY_ALIGNED_ALLOCATION_H
#define LLFPL_MEMORY_ALIGNED_ALLOCATION_H

#include <stddef.h>

#include "llfpl/core/status_code.h"

/*
 * Allocates size_in_bytes of zero-initialised storage whose address is a
 * multiple of alignment_in_bytes.
 *
 * alignment_in_bytes must be a power of two and at least sizeof(void *); any
 * other value is rejected as an invalid argument rather than being quietly
 * rounded, because a rounded alignment is a silent performance regression.
 * The size is rounded up to a whole multiple of the alignment so that the
 * allocation occupies complete cache lines and cannot share its final line
 * with an unrelated object.
 */
LlfplStatus llfpl_aligned_allocate(size_t alignment_in_bytes,
                                   size_t size_in_bytes,
                                   void **allocation_out) LLFPL_WARN_UNUSED_RESULT;

/* Releases storage obtained from llfpl_aligned_allocate. Null is a no-op. */
void llfpl_aligned_release(void *allocation);

/* Reports whether a value is a valid alignment argument. */
int llfpl_alignment_is_valid(size_t alignment_in_bytes);

#endif /* LLFPL_MEMORY_ALIGNED_ALLOCATION_H */

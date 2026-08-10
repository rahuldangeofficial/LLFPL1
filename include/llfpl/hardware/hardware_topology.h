/*
 * hardware_topology.h -- Run-time discovery of the properties of the host that
 *                        the runtime's memory layout depends on.
 *
 * Two facts are needed before a session can be built: how wide a cache line is,
 * which fixes the alignment of every allocation, and how many virtual registers
 * the evaluator may hand out. Both are discovered rather than assumed, because
 * the correct answer differs across the machines LLFPL is expected to run on --
 * 64 bytes on x86-64, 128 bytes on Apple silicon -- and hard-coding either one
 * gives up the property the design exists to provide.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_HARDWARE_HARDWARE_TOPOLOGY_H
#define LLFPL_HARDWARE_HARDWARE_TOPOLOGY_H

#include <stdint.h>

/*
 * Number of virtual registers the evaluator addresses.
 *
 * Sixteen is not arbitrary. The dirty-register bitmap is a uint16_t, so sixteen
 * is the largest count that a single word can track, and it is also the size of
 * the architectural floating-point register file on x86-64 (xmm0 through xmm15)
 * and half of it on AArch64. Keeping the virtual count at or below the physical
 * count is what allows a synchronisation to be a straight transfer rather than
 * a spill-and-reload.
 */
#define LLFPL_VIRTUAL_REGISTER_COUNT            16u

/* Alignment used when the host declines to report its cache geometry. */
#define LLFPL_FALLBACK_CACHE_LINE_SIZE_IN_BYTES 64u

typedef struct {
    uint16_t virtual_register_count;   /* Registers the evaluator may allocate. */
    uint16_t cache_line_size_in_bytes; /* Alignment for every runtime buffer.   */
    const char *architecture_name;     /* Static string; never null.            */
    int cache_line_size_was_probed;    /* Zero when the fallback was applied.   */
} LlfplHardwareTopology;

/*
 * Probes the host and returns a topology that is always internally consistent:
 * the alignment is a power of two of at least sizeof(void *), and the register
 * count never exceeds the width of the dirty-register bitmap. A failed probe
 * degrades to the documented fallback rather than reporting an error, because a
 * conservative alignment is always safe -- it costs memory, never correctness.
 */
LlfplHardwareTopology llfpl_hardware_topology_probe(void);

#endif /* LLFPL_HARDWARE_HARDWARE_TOPOLOGY_H */

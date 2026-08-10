/*
 * vector_register_file.c -- Register bank reservation and the architectural
 *                           synchronisation sequence.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/hardware/vector_register_file.h"

#include <string.h>

#include "llfpl/memory/aligned_allocation.h"

/*
 * How many virtual registers are published to architectural registers by a
 * synchronisation. Four is the number that both supported instruction sets can
 * accept without disturbing a register the calling convention needs preserved
 * (xmm0 through xmm3 on x86-64, d0 through d3 on AArch64).
 */
#define LLFPL_SYNCHRONISED_REGISTER_COUNT 4u

/* Bitmap of the registers that a synchronisation is able to publish. */
#define LLFPL_SYNCHRONISED_REGISTER_MASK  ((uint16_t)0x000Fu)

/* ---- Lifecycle ----------------------------------------------------------- */

LlfplStatus llfpl_vector_register_file_initialise(LlfplVectorRegisterFile *file,
                                                  LlfplHardwareTopology topology) {
    void *register_storage = NULL;
    void *spill_storage = NULL;
    LlfplStatus status = LLFPL_STATUS_OK;

    if (file == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    memset(file, 0, sizeof(*file));

    /*
     * The dirty bitmap dedicates one bit to each register, so a bank wider than
     * the bitmap could not be tracked. Rejecting the topology here is what makes
     * the bitmap complete by construction everywhere else.
     */
    if (topology.virtual_register_count == 0u || topology.virtual_register_count > 16u) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    status = llfpl_aligned_allocate((size_t)topology.cache_line_size_in_bytes,
                                    sizeof(double) * (size_t)topology.virtual_register_count,
                                    &register_storage);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    status = llfpl_aligned_allocate((size_t)topology.cache_line_size_in_bytes,
                                    sizeof(double) * (size_t)LLFPL_REGISTER_SPILL_STACK_CAPACITY,
                                    &spill_storage);
    if (!llfpl_status_is_ok(status)) {
        llfpl_aligned_release(register_storage);
        return status;
    }

    (*file).register_values = (double *)register_storage;
    (*file).spill_stack_values = (double *)spill_storage;
    (*file).register_count = topology.virtual_register_count;
    (*file).dirty_register_mask = 0u;
    (*file).retired_cycle_count = 0u;
    (*file).spill_stack_depth = 0u;

    return LLFPL_STATUS_OK;
}

void llfpl_vector_register_file_release(LlfplVectorRegisterFile *file) {
    if (file == NULL) {
        return;
    }

    llfpl_aligned_release((*file).register_values);
    llfpl_aligned_release((*file).spill_stack_values);

    memset(file, 0, sizeof(*file));
}

/* ---- Architectural synchronisation --------------------------------------- */

/*
 * Reads the low registers into a small contiguous staging block. Staging keeps
 * the inline assembly's memory operands on the stack at known offsets, which is
 * the addressing form both instruction sets can encode directly, and it lets a
 * bank narrower than the synchronisation width contribute zeros instead of
 * reading out of bounds.
 */
static void stage_low_registers(const LlfplVectorRegisterFile *file, double *staging_block) {
    uint16_t register_index = 0u;

    for (register_index = 0u; register_index < LLFPL_SYNCHRONISED_REGISTER_COUNT;
         register_index++) {
        *(staging_block + register_index) = (register_index < (*file).register_count)
                                                ? *((*file).register_values + register_index)
                                                : 0.0;
    }
}

void llfpl_vector_register_file_synchronise_to_silicon(LlfplVectorRegisterFile *file) {
    double staging_block[LLFPL_SYNCHRONISED_REGISTER_COUNT];

    if (file == NULL) {
        return;
    }

    /* Nothing has moved since the last synchronisation; there is nothing to publish. */
    if (((*file).dirty_register_mask & LLFPL_SYNCHRONISED_REGISTER_MASK) == 0u) {
        (*file).dirty_register_mask = 0u;
        return;
    }

    stage_low_registers(file, staging_block);

#if defined(__x86_64__)
    __asm__ __volatile__("movsd %0, %%xmm0\n\t"
                         "movsd %1, %%xmm1\n\t"
                         "movsd %2, %%xmm2\n\t"
                         "movsd %3, %%xmm3\n\t"
                         :
                         : "m"(*(staging_block + 0)),
                           "m"(*(staging_block + 1)),
                           "m"(*(staging_block + 2)),
                           "m"(*(staging_block + 3))
                         : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
#elif defined(__aarch64__) || defined(__arm64__)
    __asm__ __volatile__("ldr d0, %0\n\t"
                         "ldr d1, %1\n\t"
                         "ldr d2, %2\n\t"
                         "ldr d3, %3\n\t"
                         :
                         : "m"(*(staging_block + 0)),
                           "m"(*(staging_block + 1)),
                           "m"(*(staging_block + 2)),
                           "m"(*(staging_block + 3))
                         : "d0", "d1", "d2", "d3", "memory");
#else
    /*
     * No architectural sequence is available for this target. The barrier below
     * still provides the guarantee the callers actually depend on: every pending
     * store to the register bank is committed before execution continues, so a
     * measurement taken after this point cannot exclude work done before it.
     */
    __asm__ __volatile__("" : : "r"(staging_block) : "memory");
#endif

    (*file).dirty_register_mask = 0u;
}

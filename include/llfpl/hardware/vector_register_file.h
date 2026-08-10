/*
 * vector_register_file.h -- The virtual register bank that holds every
 *                           intermediate value an LLFPL program computes.
 *
 * The file is a cache-line-aligned array of doubles plus the bookkeeping the
 * evaluator needs: a dirty bitmap identifying which registers have been written
 * since the last synchronisation, a retired-cycle counter, and a spill stack.
 *
 * On the spill stack. The evaluator allocates one register per live
 * intermediate, so an expression nested more deeply than the file is wide would
 * otherwise have to reuse a register that still holds a live value. Rather than
 * silently aliasing, the occupant is pushed onto the spill stack before reuse
 * and restored afterwards. Register contents therefore remain correct at every
 * point of the evaluation, at any nesting depth, and expression depth is bounded
 * only by LLFPL_MAXIMUM_EVALUATION_DEPTH rather than by the register count.
 *
 * The structure is declared here, rather than hidden behind an opaque handle,
 * so that the accessors on the hot path can be inlined. They are the only
 * supported way to mutate it: writing a field directly bypasses the dirty
 * bitmap and the bounds discipline, and will desynchronise the file.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_HARDWARE_VECTOR_REGISTER_FILE_H
#define LLFPL_HARDWARE_VECTOR_REGISTER_FILE_H

#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"
#include "llfpl/hardware/hardware_topology.h"

typedef struct {
    double *LLFPL_RESTRICT register_values;    /* Cache-line-aligned bank.        */
    double *LLFPL_RESTRICT spill_stack_values; /* Saved occupants, deepest last.  */
    uint64_t retired_cycle_count;              /* Reductions charged so far.      */
    uint32_t spill_stack_depth;                /* Live entries on the spill stack. */
    uint16_t register_count;                   /* Valid indices: 0 .. count - 1.  */
    uint16_t dirty_register_mask;              /* Bit i set when register i moved. */
} LlfplVectorRegisterFile;

/*
 * Reserves the register bank and the spill stack using the probed cache line
 * size. Fails if the topology asks for more registers than the dirty bitmap can
 * track, which keeps the bitmap and the bank provably in step.
 */
LlfplStatus
llfpl_vector_register_file_initialise(LlfplVectorRegisterFile *file,
                                      LlfplHardwareTopology topology) LLFPL_WARN_UNUSED_RESULT;

void llfpl_vector_register_file_release(LlfplVectorRegisterFile *file);

/*
 * Publishes the low registers into the host's architectural floating-point
 * registers and clears the dirty bitmap.
 *
 * What this guarantees is precise and worth stating exactly: the compiler may
 * not reorder, merge or elide the transfer, so at the instruction following the
 * call the architectural registers hold the values the program computed, and
 * every pending store to the register bank has been committed to memory. What
 * it does not guarantee is that those registers survive the next function call,
 * because the platform calling convention treats them as scratch. The purpose
 * is a deterministic, non-elidable commit point for measurement, not a durable
 * binding between virtual and physical registers.
 */
void llfpl_vector_register_file_synchronise_to_silicon(LlfplVectorRegisterFile *file);

/* ---- Hot-path accessors -------------------------------------------------- */

static LLFPL_ALWAYS_INLINE uint16_t
llfpl_vector_register_file_count(const LlfplVectorRegisterFile *file) {
    return (*file).register_count;
}

static LLFPL_ALWAYS_INLINE void llfpl_vector_register_file_write(LlfplVectorRegisterFile *file,
                                                                 uint16_t register_index,
                                                                 double value) {
    if (LLFPL_EXPECT_FALSE(register_index >= (*file).register_count)) {
        return;
    }

    *((*file).register_values + register_index) = value;
    (*file).dirty_register_mask |= (uint16_t)(1u << register_index);
}

static LLFPL_ALWAYS_INLINE double
llfpl_vector_register_file_read(const LlfplVectorRegisterFile *file, uint16_t register_index) {
    if (LLFPL_EXPECT_FALSE(register_index >= (*file).register_count)) {
        return 0.0;
    }

    return *((*file).register_values + register_index);
}

static LLFPL_ALWAYS_INLINE void
llfpl_vector_register_file_charge_cycles(LlfplVectorRegisterFile *file, uint64_t cycle_count) {
    (*file).retired_cycle_count += cycle_count;
}

static LLFPL_ALWAYS_INLINE uint64_t
llfpl_vector_register_file_retired_cycles(const LlfplVectorRegisterFile *file) {
    return (*file).retired_cycle_count;
}

static LLFPL_ALWAYS_INLINE uint16_t
llfpl_vector_register_file_dirty_mask(const LlfplVectorRegisterFile *file) {
    return (*file).dirty_register_mask;
}

/* ---- Spill stack --------------------------------------------------------- */

/*
 * Saves the current occupant of a register so that the register may be reused.
 * Returns zero when the spill stack is full, in which case the caller must not
 * reuse the register; the evaluator's depth limit is set so that this cannot
 * happen in practice, and the check exists to make that a proven property
 * rather than an assumed one.
 */
static LLFPL_ALWAYS_INLINE int llfpl_vector_register_file_push_spill(LlfplVectorRegisterFile *file,
                                                                     uint16_t register_index) {
    if (LLFPL_EXPECT_FALSE((*file).spill_stack_depth >= LLFPL_REGISTER_SPILL_STACK_CAPACITY)) {
        return 0;
    }

    *((*file).spill_stack_values + (*file).spill_stack_depth) =
        llfpl_vector_register_file_read(file, register_index);
    (*file).spill_stack_depth++;

    return 1;
}

/* Restores the most recently saved occupant into a register. */
static LLFPL_ALWAYS_INLINE void llfpl_vector_register_file_pop_spill(LlfplVectorRegisterFile *file,
                                                                     uint16_t register_index) {
    if (LLFPL_EXPECT_FALSE((*file).spill_stack_depth == 0u)) {
        return;
    }

    (*file).spill_stack_depth--;
    llfpl_vector_register_file_write(
        file, register_index, *((*file).spill_stack_values + (*file).spill_stack_depth));
}

#endif /* LLFPL_HARDWARE_VECTOR_REGISTER_FILE_H */

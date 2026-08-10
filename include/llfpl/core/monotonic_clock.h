/*
 * monotonic_clock.h -- Nanosecond-resolution elapsed-time source.
 *
 * The clock is monotonic: it never steps backwards and is unaffected by system
 * time adjustments, which makes it the only defensible basis for the per-commit
 * timings that LLFPL reports. The implementation selects the highest resolution
 * source the host offers, because a microsecond-granular clock cannot resolve
 * the individual reductions this runtime is built to measure.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_MONOTONIC_CLOCK_H
#define LLFPL_CORE_MONOTONIC_CLOCK_H

#include <stdint.h>

/*
 * Returns nanoseconds elapsed since an unspecified fixed origin. Only the
 * difference between two readings is meaningful. Returns zero if the host
 * exposes no monotonic clock at all, in which case every measured interval
 * collapses to zero rather than becoming nonsense.
 */
uint64_t llfpl_monotonic_clock_now_in_nanoseconds(void);

/*
 * Resolution of the underlying clock in nanoseconds, as reported by the host.
 * Useful for deciding whether a measured interval is signal or quantisation.
 */
uint64_t llfpl_monotonic_clock_resolution_in_nanoseconds(void);

#endif /* LLFPL_CORE_MONOTONIC_CLOCK_H */

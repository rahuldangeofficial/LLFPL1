/*
 * monotonic_clock.c -- Host-specific selection of the finest monotonic clock.
 *
 * Clock selection matters more than it looks. On Darwin, CLOCK_MONOTONIC is
 * quantised to a microsecond, which is coarser than an entire LLFPL expression
 * evaluation and would report most commits as taking the same time.
 * CLOCK_UPTIME_RAW is the unadjusted mach timebase and resolves single
 * nanoseconds, so it is preferred wherever it exists.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/core/monotonic_clock.h"

#include <time.h>

/* ---- Clock identifier selection ----------------------------------------- */

#if defined(CLOCK_UPTIME_RAW)
/* Darwin: unadjusted, nanosecond-resolution, immune to time-of-day slew. */
#define LLFPL_SELECTED_CLOCK_IDENTIFIER CLOCK_UPTIME_RAW
#elif defined(CLOCK_MONOTONIC_RAW)
/* Linux: monotonic and free of network-time-protocol frequency corrections. */
#define LLFPL_SELECTED_CLOCK_IDENTIFIER CLOCK_MONOTONIC_RAW
#elif defined(CLOCK_MONOTONIC)
#define LLFPL_SELECTED_CLOCK_IDENTIFIER CLOCK_MONOTONIC
#endif

#define LLFPL_NANOSECONDS_PER_SECOND UINT64_C(1000000000)

uint64_t llfpl_monotonic_clock_now_in_nanoseconds(void) {
#if defined(LLFPL_SELECTED_CLOCK_IDENTIFIER)
    struct timespec sampled_instant;

    if (clock_gettime(LLFPL_SELECTED_CLOCK_IDENTIFIER, &sampled_instant) != 0) {
        return 0u;
    }

    return ((uint64_t)sampled_instant.tv_sec * LLFPL_NANOSECONDS_PER_SECOND) +
           (uint64_t)sampled_instant.tv_nsec;
#else
    return 0u;
#endif
}

uint64_t llfpl_monotonic_clock_resolution_in_nanoseconds(void) {
#if defined(LLFPL_SELECTED_CLOCK_IDENTIFIER)
    struct timespec reported_resolution;

    if (clock_getres(LLFPL_SELECTED_CLOCK_IDENTIFIER, &reported_resolution) != 0) {
        return 0u;
    }

    return ((uint64_t)reported_resolution.tv_sec * LLFPL_NANOSECONDS_PER_SECOND) +
           (uint64_t)reported_resolution.tv_nsec;
#else
    return 0u;
#endif
}

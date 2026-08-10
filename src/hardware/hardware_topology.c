/*
 * hardware_topology.c -- Cache geometry discovery.
 *
 * Probe order is deliberate. Where the operating system publishes the cache
 * line size, that answer is preferred over reading it out of the processor,
 * because the kernel already accounts for virtualisation, heterogeneous core
 * clusters and firmware overrides -- all cases in which the raw processor
 * report is not the size the running program will actually observe.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/hardware/hardware_topology.h"

#include <stddef.h>

#include "llfpl/core/compiler_attributes.h"

#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

/* ---- Architecture identification ---------------------------------------- */

#if defined(__aarch64__) || defined(__arm64__)
#define LLFPL_ARCHITECTURE_NAME "aarch64"
#elif defined(__x86_64__)
#define LLFPL_ARCHITECTURE_NAME "x86_64"
#elif defined(__i386__)
#define LLFPL_ARCHITECTURE_NAME "x86"
#else
#define LLFPL_ARCHITECTURE_NAME "generic"
#endif

/* ---- Validation --------------------------------------------------------- */

/*
 * A reported cache line size is accepted only if the runtime can actually use
 * it as an allocation boundary: a power of two, wide enough for a pointer, and
 * representable in the 16-bit field that carries it.
 */
static int reported_line_size_is_usable(unsigned long reported_size) {
    if (reported_size < sizeof(void *) || reported_size > 65535ul) {
        return 0;
    }

    return (reported_size & (reported_size - 1ul)) == 0ul;
}

/* ---- Cache line probes --------------------------------------------------- */

/*
 * Returns the host cache line size, or zero when no source could supply a
 * usable answer. Each branch is a complete probe for one platform; the caller
 * applies the fallback so that the failure policy lives in exactly one place.
 */
static uint16_t probe_cache_line_size_in_bytes(void) {
#if defined(__APPLE__)
    {
        size_t reported_size = 0u;
        size_t reported_size_width = sizeof(reported_size);

        if (sysctlbyname("hw.cachelinesize", &reported_size, &reported_size_width, NULL, 0) == 0 &&
            reported_line_size_is_usable((unsigned long)reported_size)) {
            return (uint16_t)reported_size;
        }
    }
#elif defined(__linux__)
    {
        const long reported_size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);

        if (reported_size > 0 && reported_line_size_is_usable((unsigned long)reported_size)) {
            return (uint16_t)reported_size;
        }
    }
#endif

#if defined(__x86_64__) || defined(__i386__)
    {
        unsigned int feature_eax = 0u;
        unsigned int feature_ebx = 0u;
        unsigned int feature_ecx = 0u;
        unsigned int feature_edx = 0u;

        /*
         * Leaf 1, EBX bits 15:8 hold the CLFLUSH line size in eight-byte units,
         * which is the cache line size on every processor that reports it.
         */
        if (__get_cpuid(1u, &feature_eax, &feature_ebx, &feature_ecx, &feature_edx)) {
            const unsigned long reported_size = (unsigned long)((feature_ebx >> 8) & 0xFFu) * 8ul;

            if (reported_line_size_is_usable(reported_size)) {
                return (uint16_t)reported_size;
            }
        }
    }
#endif

    return 0u;
}

/* ---- Public interface ---------------------------------------------------- */

LlfplHardwareTopology llfpl_hardware_topology_probe(void) {
    LlfplHardwareTopology topology;
    const uint16_t probed_line_size = probe_cache_line_size_in_bytes();

    topology.virtual_register_count = (uint16_t)LLFPL_VIRTUAL_REGISTER_COUNT;
    topology.architecture_name = LLFPL_ARCHITECTURE_NAME;
    topology.cache_line_size_was_probed = (probed_line_size != 0u);
    topology.cache_line_size_in_bytes = (probed_line_size != 0u)
                                            ? probed_line_size
                                            : (uint16_t)LLFPL_FALLBACK_CACHE_LINE_SIZE_IN_BYTES;

    return topology;
}

/* ---- Layout invariants ---------------------------------------------------- */

_Static_assert(LLFPL_VIRTUAL_REGISTER_COUNT <= 16u,
               "the dirty-register bitmap is sixteen bits wide");

_Static_assert((LLFPL_FALLBACK_CACHE_LINE_SIZE_IN_BYTES &
                (LLFPL_FALLBACK_CACHE_LINE_SIZE_IN_BYTES - 1u)) == 0u,
               "the fallback alignment must be a power of two");

_Static_assert(LLFPL_FALLBACK_CACHE_LINE_SIZE_IN_BYTES >= sizeof(void *),
               "the fallback alignment must satisfy the platform allocator minimum");

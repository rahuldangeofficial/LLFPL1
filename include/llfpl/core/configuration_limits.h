/*
 * configuration_limits.h -- The single place where every static capacity of
 *                           the LLFPL runtime is declared.
 *
 * LLFPL performs no allocation while a program is running: all storage is
 * reserved once, during session construction, from the limits below. Keeping
 * the limits together makes the entire memory footprint of an LLFPL process
 * auditable by reading one file, and keeps the trade-off between capacity and
 * cache residency an explicit engineering decision rather than an accident
 * scattered across a dozen translation units.
 *
 * Static assertions at the bottom of this header prove the invariants that the
 * data-structure layouts depend on, so a careless edit here fails the build
 * instead of corrupting memory at run time.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_CONFIGURATION_LIMITS_H
#define LLFPL_CORE_CONFIGURATION_LIMITS_H

#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"

/* ---- Identifiers -------------------------------------------------------- */

/*
 * Every name in an LLFPL program -- an identity, a template, a parameter, a
 * memory arena -- is stored in a fixed inline buffer of this size. Inline
 * storage keeps a name in the same cache line as the record that owns it, so a
 * symbol probe touches exactly one line instead of chasing a pointer into an
 * unrelated page.
 */
#define LLFPL_IDENTIFIER_BUFFER_CAPACITY 48
#define LLFPL_MAXIMUM_IDENTIFIER_LENGTH  (LLFPL_IDENTIFIER_BUFFER_CAPACITY - 1)

/* ---- Symbol table ------------------------------------------------------- */

/*
 * The identity symbol table is a coalesced-chaining hash table. The address
 * space is split into a primary zone, which hash values map onto directly, and
 * a cellar that absorbs collisions. A cellar of roughly fourteen percent of the
 * table keeps the expected probe count below 1.2 at full occupancy, which is
 * measurably better than open addressing without introducing pointer chasing.
 */
#define LLFPL_SYMBOL_TABLE_TOTAL_SLOTS   1024u
#define LLFPL_SYMBOL_TABLE_PRIMARY_SLOTS 880u
#define LLFPL_SYMBOL_TABLE_CELLAR_SLOTS \
    (LLFPL_SYMBOL_TABLE_TOTAL_SLOTS - LLFPL_SYMBOL_TABLE_PRIMARY_SLOTS)

/* Sentinel stored in the chain link of a record that terminates its chain. */
#define LLFPL_SYMBOL_CHAIN_TERMINATOR            ((uint16_t)0xFFFFu)

/* ---- Templates ---------------------------------------------------------- */

#define LLFPL_TEMPLATE_SEGMENT_CAPACITY          256u
#define LLFPL_TEMPLATE_PARAMETER_CAPACITY        8u

/* ---- Memory arenas ------------------------------------------------------ */

#define LLFPL_MEMORY_ARENA_REGISTRY_CAPACITY     64u

/* An arena is addressed with a 32-bit byte offset, which bounds its extent. */
#define LLFPL_MEMORY_ARENA_MAXIMUM_SIZE_IN_BYTES ((uint32_t)0x40000000u) /* 1 GiB */

/* ---- Evaluation --------------------------------------------------------- */

/*
 * Guard against unbounded recursive descent. Each evaluator activation costs
 * well under 256 bytes of stack, so 512 levels stay far inside the smallest
 * default thread stack (512 KiB) with an order of magnitude to spare.
 */
#define LLFPL_MAXIMUM_EVALUATION_DEPTH           512u

/* One spill slot per evaluation level is the worst case the evaluator can reach. */
#define LLFPL_REGISTER_SPILL_STACK_CAPACITY      (LLFPL_MAXIMUM_EVALUATION_DEPTH + 1u)

/* Widest numeric literal the scanner will accept, including sign and exponent. */
#define LLFPL_NUMERIC_LITERAL_BUFFER_CAPACITY    64u

/* ---- Modules ------------------------------------------------------------ */

#define LLFPL_MAXIMUM_LOADED_MODULES             128u
#define LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES  16u
#define LLFPL_MODULE_PATH_BUFFER_CAPACITY        1024u

/* The one source extension the toolchain accepts. */
#define LLFPL_SOURCE_FILE_EXTENSION              ".LLFPL"

/* ---- Invariants --------------------------------------------------------- */

_Static_assert(LLFPL_IDENTIFIER_BUFFER_CAPACITY % 8u == 0u,
               "identifier buffers are compared eight bytes at a time");

_Static_assert(LLFPL_SYMBOL_TABLE_PRIMARY_SLOTS < LLFPL_SYMBOL_TABLE_TOTAL_SLOTS,
               "the cellar must contain at least one slot");

_Static_assert(LLFPL_SYMBOL_TABLE_TOTAL_SLOTS < LLFPL_SYMBOL_CHAIN_TERMINATOR,
               "every slot index must be distinguishable from the chain terminator");

_Static_assert(LLFPL_TEMPLATE_PARAMETER_CAPACITY <= 255u,
               "parameter counts are stored in a single byte");

_Static_assert(LLFPL_MEMORY_ARENA_MAXIMUM_SIZE_IN_BYTES <= (uint32_t)0x7FFFFFFFu,
               "arena bounds arithmetic must not overflow a 32-bit offset");

#endif /* LLFPL_CORE_CONFIGURATION_LIMITS_H */

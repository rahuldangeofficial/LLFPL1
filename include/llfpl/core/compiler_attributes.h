/*
 * compiler_attributes.h -- Portable spellings for the compiler and hardware
 *                          hints that the LLFPL runtime relies upon.
 *
 * Every attribute defined here degrades to a no-op on toolchains that do not
 * understand it, so the translation units below this header never need a
 * single conditional-compilation block of their own.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_COMPILER_ATTRIBUTES_H
#define LLFPL_CORE_COMPILER_ATTRIBUTES_H

#include <stddef.h>

/* ---- Cache geometry ---------------------------------------------------- */

/*
 * Compile-time cache line assumption. The runtime probes the real cache line
 * size at start-up (see hardware_topology.h) and uses the probed value for
 * every dynamic allocation. This constant exists only for structure layout,
 * which must be decided before the program runs. Sixty-four bytes is the
 * widest value that is also a divisor of every cache line in production use
 * (64 bytes on x86-64 and 128 bytes on Apple silicon), so a structure aligned
 * to it never straddles a line on either family.
 */
#define LLFPL_CACHE_LINE_SIZE_IN_BYTES 64

/* Places the annotated object on its own cache line boundary. */
#define LLFPL_CACHE_LINE_ALIGNED       _Alignas(LLFPL_CACHE_LINE_SIZE_IN_BYTES)

/* ---- Branch prediction hints ------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define LLFPL_EXPECT_TRUE(condition)  __builtin_expect(!!(condition), 1)
#define LLFPL_EXPECT_FALSE(condition) __builtin_expect(!!(condition), 0)
#else
#define LLFPL_EXPECT_TRUE(condition)  (condition)
#define LLFPL_EXPECT_FALSE(condition) (condition)
#endif

/* ---- Inlining and aliasing --------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)
#define LLFPL_ALWAYS_INLINE      inline __attribute__((always_inline))
#define LLFPL_NEVER_INLINE       __attribute__((noinline))
#define LLFPL_PURE_FUNCTION      __attribute__((pure))
#define LLFPL_CONST_FUNCTION     __attribute__((const))
#define LLFPL_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define LLFPL_PRINTF_LIKE(format_index, first_argument_index) \
    __attribute__((format(printf, format_index, first_argument_index)))
#else
#define LLFPL_ALWAYS_INLINE inline
#define LLFPL_NEVER_INLINE
#define LLFPL_PURE_FUNCTION
#define LLFPL_CONST_FUNCTION
#define LLFPL_WARN_UNUSED_RESULT
#define LLFPL_PRINTF_LIKE(format_index, first_argument_index)
#endif

/* Spelled out so that the pointer-arithmetic house style stays unambiguous. */
#define LLFPL_RESTRICT                    restrict

/* ---- Miscellaneous ----------------------------------------------------- */

/* Silences a deliberately unused parameter without weakening the warning set. */
#define LLFPL_UNUSED_PARAMETER(parameter) ((void)(parameter))

/* Element count of an array whose type is visible at the point of use. */
#define LLFPL_ARRAY_LENGTH(array)         (sizeof(array) / sizeof(*(array)))

#endif /* LLFPL_CORE_COMPILER_ATTRIBUTES_H */

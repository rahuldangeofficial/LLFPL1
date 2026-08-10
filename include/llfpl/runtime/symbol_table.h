/*
 * symbol_table.h -- Storage for the immutable identities a program declares.
 *
 * The table is a coalesced-chaining hash table over one contiguous, cache-line
 * aligned slab. Three properties follow from that choice, and each of them is
 * the reason a more familiar alternative was rejected:
 *
 *   - Each record occupies exactly one cache line, so a probe touches exactly
 *     one line. Separate-chaining with heap nodes would touch two: one for the
 *     bucket and one for the node.
 *
 *   - Collisions are resolved by linking into a reserved cellar rather than by
 *     walking forward through the primary zone. Open addressing degrades under
 *     clustering, and Robin Hood addressing pays for its low variance with
 *     cascading element moves on every insertion.
 *
 *   - The slab is reserved once and never grows, so a binding cannot trigger an
 *     allocation, and no pointer into the table is ever invalidated.
 *
 * Identities are immutable by language rule. Re-binding an existing name is
 * reported as a semantic error rather than silently replacing the value.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_SYMBOL_TABLE_H
#define LLFPL_RUNTIME_SYMBOL_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

/* ---- Values -------------------------------------------------------------- */

typedef enum {
    LLFPL_VALUE_UNDEFINED = 0,
    LLFPL_VALUE_DOUBLE = 1,
    LLFPL_VALUE_SIGNED_INTEGER = 2,
    LLFPL_VALUE_UNSIGNED_INTEGER = 3,
    LLFPL_VALUE_POINTER = 4,
    LLFPL_VALUE_BOOLEAN = 5
} LlfplValueKind;

typedef union {
    double as_double;
    int64_t as_signed_integer;
    uint64_t as_unsigned_integer;
    void *as_pointer;
    uint8_t as_boolean;
} LlfplValuePayload;

typedef struct {
    LlfplValuePayload payload;
    LlfplValueKind kind;
} LlfplValue;

/* ---- Records ------------------------------------------------------------- */

/*
 * One record, sized to one cache line exactly. The static assertion below is
 * not decoration: it is what keeps the single-line probe guarantee true after
 * any future edit to this structure or to the identifier capacity.
 */
typedef struct {
    LlfplValuePayload payload;                   /*  8 bytes */
    char name[LLFPL_IDENTIFIER_BUFFER_CAPACITY]; /* 48 bytes */
    uint16_t next_in_chain;                      /*  2 bytes */
    uint8_t is_occupied;                         /*  1 byte  */
    uint8_t kind;                                /*  1 byte  */
    uint8_t reserved_padding[4];                 /*  4 bytes */
} LlfplSymbolRecord;

_Static_assert(sizeof(LlfplSymbolRecord) == LLFPL_CACHE_LINE_SIZE_IN_BYTES,
               "a symbol record must occupy exactly one cache line");

typedef struct {
    LlfplSymbolRecord *records; /* Aligned slab of TOTAL_SLOTS records.   */
    uint32_t occupied_record_count;
    uint32_t next_cellar_candidate; /* Rising cursor into the cellar zone.   */
} LlfplSymbolTable;

/* ---- Lifecycle ----------------------------------------------------------- */

LlfplStatus llfpl_symbol_table_initialise(LlfplSymbolTable *table,
                                          uint16_t alignment_in_bytes) LLFPL_WARN_UNUSED_RESULT;

void llfpl_symbol_table_release(LlfplSymbolTable *table);

/* ---- Binding and resolution ---------------------------------------------- */

/*
 * Binds a name to a value.
 *
 * Returns LLFPL_STATUS_SEMANTIC_ERROR when the name is already bound, which is
 * the language's immutability rule surfacing as a diagnosable condition rather
 * than as a silent overwrite.
 */
LlfplStatus llfpl_symbol_table_bind(LlfplSymbolTable *table, const char *name, LlfplValue value);

/* Bounded-span form for names that point into a source mapping. */
LlfplStatus llfpl_symbol_table_bind_span(LlfplSymbolTable *table,
                                         const char *name,
                                         size_t name_length,
                                         LlfplValue value);

/*
 * Resolves a name. On success the value is written to value_out; on failure
 * LLFPL_STATUS_NOT_FOUND is returned and value_out is left untouched, so a
 * caller can distinguish an absent name from one bound to zero.
 */
LlfplStatus
llfpl_symbol_table_resolve(const LlfplSymbolTable *table, const char *name, LlfplValue *value_out);

/* Bounded-span form for names that point into a source mapping. */
LlfplStatus llfpl_symbol_table_resolve_span(const LlfplSymbolTable *table,
                                            const char *name,
                                            size_t name_length,
                                            LlfplValue *value_out);

/* ---- Value construction helpers ------------------------------------------ */

static LLFPL_ALWAYS_INLINE LlfplValue llfpl_value_from_double(double numeric_value) {
    LlfplValue value;

    value.payload.as_double = numeric_value;
    value.kind = LLFPL_VALUE_DOUBLE;

    return value;
}

#endif /* LLFPL_RUNTIME_SYMBOL_TABLE_H */

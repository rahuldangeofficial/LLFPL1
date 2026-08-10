/*
 * symbol_table.c -- Coalesced-chaining hash table over a cache-line-aligned slab.
 *
 * Address space layout:
 *
 *     [ primary zone: slots 0 .. 879 ][ cellar: slots 880 .. 1023 ]
 *
 * A hash maps only into the primary zone. When the slot it selects is taken by
 * a different name, the new record is placed in the next free cellar slot and
 * linked onto the end of the existing chain. Chains therefore never invade the
 * primary zone, so one name's collisions can never displace another name's home
 * slot -- the failure mode that makes plain linear probing degrade.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#include "llfpl/runtime/symbol_table.h"

#include <string.h>

#include "llfpl/core/identifier.h"
#include "llfpl/memory/aligned_allocation.h"

/* ---- Internal helpers ---------------------------------------------------- */

static uint32_t home_slot_for_name(const char *name, size_t name_length) {
    return llfpl_identifier_hash(name, name_length) % LLFPL_SYMBOL_TABLE_PRIMARY_SLOTS;
}

static LlfplSymbolRecord *record_at(const LlfplSymbolTable *table, uint32_t slot) {
    return (*table).records + slot;
}

/*
 * Claims the next unoccupied cellar slot.
 *
 * The cursor only ever rises. Because records are never removed, a slot below
 * the cursor can never become free again, so restarting the search from the
 * bottom would re-examine slots that are known to be taken. This keeps the
 * total cellar search cost across an entire program linear in the cellar size.
 */
static LlfplStatus claim_cellar_slot(LlfplSymbolTable *table, uint32_t *claimed_slot_out) {
    while ((*table).next_cellar_candidate < LLFPL_SYMBOL_TABLE_TOTAL_SLOTS) {
        const uint32_t candidate_slot = (*table).next_cellar_candidate;

        (*table).next_cellar_candidate++;

        if (!(*record_at(table, candidate_slot)).is_occupied) {
            *claimed_slot_out = candidate_slot;
            return LLFPL_STATUS_OK;
        }
    }

    return LLFPL_STATUS_CAPACITY_EXCEEDED;
}

static void occupy_record(LlfplSymbolRecord *record, const char *padded_name, LlfplValue value) {
    (*record).payload = value.payload;
    (*record).kind = (uint8_t)value.kind;
    (*record).is_occupied = 1u;
    memcpy((*record).name, padded_name, (size_t)LLFPL_IDENTIFIER_BUFFER_CAPACITY);
}

/* ---- Lifecycle ----------------------------------------------------------- */

LlfplStatus llfpl_symbol_table_initialise(LlfplSymbolTable *table, uint16_t alignment_in_bytes) {
    void *record_storage = NULL;
    LlfplStatus status = LLFPL_STATUS_OK;
    uint32_t slot = 0u;

    if (table == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    memset(table, 0, sizeof(*table));

    status =
        llfpl_aligned_allocate((size_t)alignment_in_bytes,
                               sizeof(LlfplSymbolRecord) * (size_t)LLFPL_SYMBOL_TABLE_TOTAL_SLOTS,
                               &record_storage);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    (*table).records = (LlfplSymbolRecord *)record_storage;
    (*table).occupied_record_count = 0u;
    (*table).next_cellar_candidate = LLFPL_SYMBOL_TABLE_PRIMARY_SLOTS;

    /*
     * The slab arrives zeroed, which already marks every record unoccupied. The
     * chain links still need the terminator, since zero is a valid slot index.
     */
    for (slot = 0u; slot < LLFPL_SYMBOL_TABLE_TOTAL_SLOTS; slot++) {
        (*record_at(table, slot)).next_in_chain = LLFPL_SYMBOL_CHAIN_TERMINATOR;
    }

    return LLFPL_STATUS_OK;
}

void llfpl_symbol_table_release(LlfplSymbolTable *table) {
    if (table == NULL) {
        return;
    }

    llfpl_aligned_release((*table).records);
    memset(table, 0, sizeof(*table));
}

/* ---- Binding ------------------------------------------------------------- */

LlfplStatus llfpl_symbol_table_bind_span(LlfplSymbolTable *table,
                                         const char *name,
                                         size_t name_length,
                                         LlfplValue value) {
    char padded_name[LLFPL_IDENTIFIER_BUFFER_CAPACITY];
    LlfplStatus status = LLFPL_STATUS_OK;
    uint32_t chain_slot = 0u;
    uint32_t claimed_slot = 0u;

    if (table == NULL || (*table).records == NULL || name == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    status = llfpl_identifier_assign(padded_name, name, name_length);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    if ((*table).occupied_record_count >= LLFPL_SYMBOL_TABLE_TOTAL_SLOTS) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    chain_slot = home_slot_for_name(name, name_length);

    /* Fast path: the home slot is free, so the chain is one record long. */
    if (!(*record_at(table, chain_slot)).is_occupied) {
        occupy_record(record_at(table, chain_slot), padded_name, value);
        (*table).occupied_record_count++;
        return LLFPL_STATUS_OK;
    }

    /* Walk to the end of the chain, refusing a rebind of an existing name. */
    while (1) {
        LlfplSymbolRecord *record = record_at(table, chain_slot);

        if (llfpl_identifier_equals((*record).name, padded_name)) {
            return LLFPL_STATUS_SEMANTIC_ERROR;
        }

        if ((*record).next_in_chain == LLFPL_SYMBOL_CHAIN_TERMINATOR) {
            break;
        }

        chain_slot = (*record).next_in_chain;
    }

    status = claim_cellar_slot(table, &claimed_slot);
    if (!llfpl_status_is_ok(status)) {
        return status;
    }

    occupy_record(record_at(table, claimed_slot), padded_name, value);
    (*record_at(table, chain_slot)).next_in_chain = (uint16_t)claimed_slot;
    (*table).occupied_record_count++;

    return LLFPL_STATUS_OK;
}

LlfplStatus llfpl_symbol_table_bind(LlfplSymbolTable *table, const char *name, LlfplValue value) {
    if (name == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    return llfpl_symbol_table_bind_span(table, name, strlen(name), value);
}

/* ---- Resolution ---------------------------------------------------------- */

LlfplStatus llfpl_symbol_table_resolve_span(const LlfplSymbolTable *table,
                                            const char *name,
                                            size_t name_length,
                                            LlfplValue *value_out) {
    uint32_t chain_slot = 0u;

    if (table == NULL || (*table).records == NULL || name == NULL || value_out == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    /* A name that could never have been stored cannot be found. */
    if (!llfpl_identifier_length_is_valid(name_length)) {
        return LLFPL_STATUS_NOT_FOUND;
    }

    chain_slot = home_slot_for_name(name, name_length);

    while (chain_slot != LLFPL_SYMBOL_CHAIN_TERMINATOR) {
        const LlfplSymbolRecord *record = record_at(table, chain_slot);

        if (!(*record).is_occupied) {
            break;
        }

        if (llfpl_identifier_equals_span((*record).name, name, name_length)) {
            (*value_out).payload = (*record).payload;
            (*value_out).kind = (LlfplValueKind)(*record).kind;
            return LLFPL_STATUS_OK;
        }

        chain_slot = (*record).next_in_chain;
    }

    return LLFPL_STATUS_NOT_FOUND;
}

LlfplStatus
llfpl_symbol_table_resolve(const LlfplSymbolTable *table, const char *name, LlfplValue *value_out) {
    if (name == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    return llfpl_symbol_table_resolve_span(table, name, strlen(name), value_out);
}

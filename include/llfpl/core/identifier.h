/*
 * identifier.h -- Fixed-capacity name storage shared by every named record.
 *
 * Identities, templates, template parameters and memory arenas all store their
 * name in the same inline buffer shape. Centralising the copy, compare and hash
 * operations here means the length rule is stated once, the hash function is
 * the same everywhere, and no subsystem can invent a subtly different notion of
 * what makes two names equal.
 *
 * Names are truncated by no operation in this header. A name that does not fit
 * is refused, and the caller reports it; silent truncation would make two
 * distinct identifiers collide into one binding.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_CORE_IDENTIFIER_H
#define LLFPL_CORE_IDENTIFIER_H

#include <stdint.h>
#include <string.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

/*
 * Copies a bounded source span into an identifier buffer of exactly
 * LLFPL_IDENTIFIER_BUFFER_CAPACITY bytes. The unused tail is zeroed so that two
 * equal names always have byte-identical buffers, which is what allows the
 * comparison below to read whole words without inspecting a length.
 */
static LLFPL_ALWAYS_INLINE LlfplStatus llfpl_identifier_assign(char *destination_buffer,
                                                               const char *source_text,
                                                               size_t source_length) {
    if (destination_buffer == NULL || source_text == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    if (source_length == 0u) {
        return LLFPL_STATUS_SYNTAX_ERROR;
    }

    if (source_length > (size_t)LLFPL_MAXIMUM_IDENTIFIER_LENGTH) {
        return LLFPL_STATUS_CAPACITY_EXCEEDED;
    }

    memset(destination_buffer, 0, (size_t)LLFPL_IDENTIFIER_BUFFER_CAPACITY);
    memcpy(destination_buffer, source_text, source_length);

    return LLFPL_STATUS_OK;
}

/* Copies a null-terminated name, applying the same refusal-over-truncation rule. */
static LLFPL_ALWAYS_INLINE LlfplStatus llfpl_identifier_assign_terminated(char *destination_buffer,
                                                                          const char *source_text) {
    if (source_text == NULL) {
        return LLFPL_STATUS_INVALID_ARGUMENT;
    }

    return llfpl_identifier_assign(destination_buffer, source_text, strlen(source_text));
}

/*
 * Equality over two identifier buffers.
 *
 * Because every buffer is zero-filled past its name, the whole fixed extent can
 * be compared at once. That turns a name comparison into a handful of aligned
 * word compares with no length load, no early-exit branch per character, and no
 * dependence on where the terminator sits.
 */
static LLFPL_ALWAYS_INLINE int llfpl_identifier_equals(const char *left_buffer,
                                                       const char *right_buffer) {
    return memcmp(left_buffer, right_buffer, (size_t)LLFPL_IDENTIFIER_BUFFER_CAPACITY) == 0;
}

/* Reports whether a span could be stored as an identifier at all. */
static LLFPL_ALWAYS_INLINE int llfpl_identifier_length_is_valid(size_t length) {
    return length > 0u && length <= (size_t)LLFPL_MAXIMUM_IDENTIFIER_LENGTH;
}

/*
 * Equality between a stored identifier buffer and an unterminated span, such as
 * the text of an atom pointing into a source mapping.
 *
 * This is the form used on the evaluation path. Padding the span into a
 * scratch buffer first would cost a full-width clear on every single lookup;
 * checking the stored buffer's terminator instead reduces the test to one byte
 * load and a comparison over exactly the span's own length.
 */
static LLFPL_ALWAYS_INLINE int
llfpl_identifier_equals_span(const char *stored_buffer, const char *text, size_t length) {
    if (!llfpl_identifier_length_is_valid(length)) {
        return 0;
    }

    /* The stored buffer is zero-padded, so this proves the stored name is the
     * same length as the span before a single character is compared. */
    if (*(stored_buffer + length) != '\0') {
        return 0;
    }

    return memcmp(stored_buffer, text, length) == 0;
}

/*
 * FNV-1a over a bounded span. Chosen for its very short dependency chain: one
 * exclusive-or and one multiply per byte, which retires in a couple of cycles
 * for the short names LLFPL programs use, and disperses single-character
 * differences across the whole word.
 */
static LLFPL_ALWAYS_INLINE uint32_t llfpl_identifier_hash(const char *text, size_t length) {
    const uint32_t offset_basis = 2166136261u;
    const uint32_t prime = 16777619u;
    uint32_t accumulated_hash = offset_basis;
    size_t character_index = 0u;

    for (character_index = 0u; character_index < length; character_index++) {
        accumulated_hash ^= (uint32_t)(uint8_t)*(text + character_index);
        accumulated_hash *= prime;
    }

    return accumulated_hash;
}

/* Hash of a null-terminated name, consistent with the bounded form above. */
static LLFPL_ALWAYS_INLINE uint32_t llfpl_identifier_hash_terminated(const char *text) {
    return llfpl_identifier_hash(text, strlen(text));
}

#endif /* LLFPL_CORE_IDENTIFIER_H */

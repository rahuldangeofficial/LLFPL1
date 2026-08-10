/*
 * template_definition.h -- Named, parameterised expressions and the segment
 *                          that stores them.
 *
 * A Map declaration is not compiled into a tree. What is recorded is the
 * scanner position at which the body expression begins -- a coordinate into the
 * mapped source file -- together with the parameter names. Invoking the
 * template restores that coordinate into a scanner on the caller's stack and
 * evaluates from it.
 *
 * The consequences are the point of the design: defining a template allocates
 * nothing, invoking one allocates nothing, and the body text is never copied,
 * parsed twice into different forms, or held in a structure whose traversal
 * would chase pointers across unrelated cache lines.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_FRONTEND_TEMPLATE_DEFINITION_H
#define LLFPL_FRONTEND_TEMPLATE_DEFINITION_H

#include <stdint.h>

#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/diagnostic_reporter.h"
#include "llfpl/core/status_code.h"
#include "llfpl/frontend/lexical_scanner.h"

/* Size of the open-addressed name index. A power of two twice the segment
 * capacity keeps the load factor at or below one half, which bounds the
 * expected probe count for a lookup below 1.5. */
#define LLFPL_TEMPLATE_INDEX_SLOT_COUNT 512u

/* Sentinel for an unoccupied index slot. */
#define LLFPL_TEMPLATE_INDEX_EMPTY_SLOT ((uint16_t)0xFFFFu)

typedef struct {
    char name[LLFPL_IDENTIFIER_BUFFER_CAPACITY];
    char parameter_names[LLFPL_TEMPLATE_PARAMETER_CAPACITY][LLFPL_IDENTIFIER_BUFFER_CAPACITY];
    LlfplLexicalScanner body_snapshot; /* Scanner positioned at the body expression. */
    uint8_t parameter_count;
} LlfplTemplateDefinition;

typedef struct {
    LlfplTemplateDefinition definitions[LLFPL_TEMPLATE_SEGMENT_CAPACITY];
    uint16_t name_index[LLFPL_TEMPLATE_INDEX_SLOT_COUNT];
    uint32_t definition_count;
} LlfplTemplateSegment;

_Static_assert(LLFPL_TEMPLATE_INDEX_SLOT_COUNT >= 2u * LLFPL_TEMPLATE_SEGMENT_CAPACITY,
               "the template name index must stay at or below half occupancy");

_Static_assert((LLFPL_TEMPLATE_INDEX_SLOT_COUNT & (LLFPL_TEMPLATE_INDEX_SLOT_COUNT - 1u)) == 0u,
               "index slot count must be a power of two so probing can mask instead of divide");

_Static_assert(LLFPL_TEMPLATE_SEGMENT_CAPACITY < LLFPL_TEMPLATE_INDEX_EMPTY_SLOT,
               "every definition index must be distinguishable from the empty-slot sentinel");

/*
 * Predicate that reports whether a name is reserved by the language.
 *
 * The frontend must reject a declaration that shadows a built-in or a
 * primitive, but it has no business knowing what those are -- that is the
 * runtime's vocabulary, and a direct dependency on it would invert the layering
 * of this codebase. The caller supplies the rule instead, and passing null
 * means no name is reserved.
 */
typedef int (*LlfplReservedNamePredicate)(const char *name_text, size_t name_length);

void llfpl_template_segment_initialise(LlfplTemplateSegment *segment);

/*
 * Parses a Map declaration. The scanner must be positioned immediately after
 * the Map keyword; on success it is left immediately after the declaration's
 * closing parenthesis, so the caller can continue reading the module without
 * knowing anything about the declaration's internal shape.
 *
 * The grammar is
 *
 *     Map '(' name ( ',' parameter )* ',' body ')'
 *
 * with the body being the final argument. Parameters are distinguished from the
 * body by lookahead rather than by position, so a body that is a bare parameter
 * reference -- Map(Echo, value, value) -- parses correctly.
 */
LlfplStatus llfpl_template_segment_define(LlfplTemplateSegment *segment,
                                          LlfplLexicalScanner *scanner,
                                          LlfplDiagnosticReporter *reporter,
                                          LlfplReservedNamePredicate name_is_reserved);

/* Returns the named template, or null when no such template is defined. */
const LlfplTemplateDefinition *llfpl_template_segment_lookup(const LlfplTemplateSegment *segment,
                                                             const char *template_name);

/*
 * Bounded-span form of the lookup, used on the evaluation path where the name
 * is an atom pointing into the source mapping and copying it out first would be
 * wasted work.
 */
const LlfplTemplateDefinition *llfpl_template_segment_lookup_span(
    const LlfplTemplateSegment *segment, const char *template_name, size_t template_name_length);

#endif /* LLFPL_FRONTEND_TEMPLATE_DEFINITION_H */

/*
 * activation_frame.h -- The binding of a template's parameters for one call.
 *
 * A frame holds argument values and a borrowed pointer to the template being
 * executed; the parameter names stay in the template, where they were already
 * stored. Copying the names into every frame would make each call an eighth of
 * a kilobyte of memory traffic for information that never changes between
 * calls, and would put a frame well past the point where it comfortably lives
 * in the caller's stack cache.
 *
 * A frame is a value on the evaluator's stack. Calling a template costs one
 * structure initialisation and nothing else: no allocation, no free list, no
 * reference count, and no reclamation at return.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_ACTIVATION_FRAME_H
#define LLFPL_RUNTIME_ACTIVATION_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/identifier.h"
#include "llfpl/frontend/template_definition.h"

typedef struct {
    const LlfplTemplateDefinition *invoked_template; /* Borrowed; owns the names. */
    double argument_values[LLFPL_TEMPLATE_PARAMETER_CAPACITY];
    uint8_t argument_count;
} LlfplActivationFrame;

/* Prepares an empty frame bound to a template. A null template yields a frame
 * with no parameters, which is the correct shape for top-level evaluation. */
static LLFPL_ALWAYS_INLINE void
llfpl_activation_frame_initialise(LlfplActivationFrame *frame,
                                  const LlfplTemplateDefinition *invoked_template) {
    (*frame).invoked_template = invoked_template;
    (*frame).argument_count = 0u;
}

/*
 * Resolves a parameter name against the frame.
 *
 * The scan is linear over at most LLFPL_TEMPLATE_PARAMETER_CAPACITY entries,
 * which beats any indexed structure at this size: the names are contiguous, the
 * first mismatched byte ends each comparison, and the whole search is resolved
 * from one or two cache lines with no hash to compute.
 */
static LLFPL_ALWAYS_INLINE int
llfpl_activation_frame_resolve_span(const LlfplActivationFrame *frame,
                                    const char *parameter_name,
                                    size_t parameter_name_length,
                                    double *value_out) {
    uint8_t parameter_index = 0u;

    if (frame == NULL || (*frame).invoked_template == NULL) {
        return 0;
    }

    for (parameter_index = 0u; parameter_index < (*frame).argument_count; parameter_index++) {
        const char *stored_name = *((*(*frame).invoked_template).parameter_names + parameter_index);

        if (llfpl_identifier_equals_span(stored_name, parameter_name, parameter_name_length)) {
            *value_out = *((*frame).argument_values + parameter_index);
            return 1;
        }
    }

    return 0;
}

#endif /* LLFPL_RUNTIME_ACTIVATION_FRAME_H */

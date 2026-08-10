/*
 * builtin_operation.h -- The special forms, and the table that dispatches them.
 *
 * A built-in differs from a primitive in that it controls whether and how often
 * its arguments are evaluated: Branch evaluates a selector before its branches,
 * Loop evaluates a template body repeatedly, and the arena accessors take an
 * arena name that is not an expression at all. None of that can be expressed as
 * a function of two already-computed values, which is exactly why these are a
 * separate mechanism rather than more entries in the primitive table.
 *
 * Dispatch is table-driven. Adding a special form means adding a handler and a
 * row; the evaluator is not touched, and neither is any existing handler. That
 * is the open-closed principle applied where it actually pays for itself --
 * this is the part of a language runtime that grows.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_RUNTIME_BUILTIN_OPERATION_H
#define LLFPL_RUNTIME_BUILTIN_OPERATION_H

#include <stddef.h>

#include "llfpl/core/diagnostic_reporter.h"
#include "llfpl/frontend/lexical_scanner.h"
#include "llfpl/runtime/activation_frame.h"
#include "llfpl/runtime/execution_context.h"
#include "llfpl/runtime/expression_evaluator.h"

/*
 * Everything a handler needs. The scanner is positioned immediately after the
 * verb, so the handler owns the parse of its own argument list from the opening
 * parenthesis onward -- which is what lets each special form have whatever
 * shape it needs.
 */
typedef struct {
    LlfplExecutionContext *context;
    LlfplLexicalScanner *scanner;
    const LlfplActivationFrame *frame;
    LlfplRegisterAllocation allocation;
    LlfplSourceLocation call_site;
} LlfplBuiltinInvocation;

typedef double (*LlfplBuiltinHandler)(const LlfplBuiltinInvocation *invocation);

typedef struct {
    const char *verb_name;
    size_t verb_name_length;
    LlfplBuiltinHandler handler;
    const char *signature_summary; /* Shown by the command line help. */
    const char *purpose_summary;
} LlfplBuiltinDescriptor;

/* Resolves a verb span to its descriptor, or null when the verb is not built in. */
const LlfplBuiltinDescriptor *llfpl_builtin_lookup_span(const char *verb_text, size_t verb_length);

/* Enumeration support, used by the command line help and the documentation. */
size_t llfpl_builtin_count(void);
const LlfplBuiltinDescriptor *llfpl_builtin_descriptor_at(size_t descriptor_index);

/*
 * Reports whether a verb is reserved by the language -- that is, whether it
 * names a built-in or a primitive.
 *
 * Declarations consult this so that a program cannot bind an identity, a
 * template or a parameter to a name the evaluator already resolves. Rejecting
 * the declaration is the only way to keep name resolution unambiguous without
 * imposing a shadowing rule that a reader would have to memorise.
 */
int llfpl_verb_is_reserved(const char *verb_text, size_t verb_length);

#endif /* LLFPL_RUNTIME_BUILTIN_OPERATION_H */

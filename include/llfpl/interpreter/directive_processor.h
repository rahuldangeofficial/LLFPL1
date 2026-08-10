/*
 * directive_processor.h -- Execution of a module's top-level declarations.
 *
 * A module is a sequence of directives. Directives are the only construct that
 * may appear at the top level, and expressions are the only construct that may
 * appear inside one; keeping the two grammars disjoint is what makes a module
 * readable as a declaration of intent rather than as a script.
 *
 * Dispatch is table-driven, for the same reason the built-in table is: a new
 * directive is a new row and a new handler, and no existing code changes.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_INTERPRETER_DIRECTIVE_PROCESSOR_H
#define LLFPL_INTERPRETER_DIRECTIVE_PROCESSOR_H

#include <stdint.h>
#include <stdio.h>

#include "llfpl/core/status_code.h"
#include "llfpl/interpreter/module_loader.h"
#include "llfpl/memory/source_file_mapping.h"
#include "llfpl/runtime/execution_context.h"

/*
 * Everything a directive needs beyond the execution context: where to send
 * results, how to find a required module, and where mapped source is retained.
 * Grouping them here keeps the interpreter session free to change how it stores
 * them without touching a single handler.
 */
typedef struct {
    LlfplExecutionContext *execution_context;
    LlfplModuleLoader *module_loader;
    LlfplSourceFileMappingRegistry *mapping_registry;

    FILE *result_stream;
    int hardware_synchronisation_is_enabled;

    uint32_t committed_expression_count;
    uint64_t total_elapsed_nanoseconds;
} LlfplModuleExecutionEnvironment;

/*
 * Loads and executes a module identified by its canonical path.
 *
 * The module is marked as loaded before its body runs, so a Require cycle
 * terminates at the point it closes back on itself rather than recursing.
 */
LlfplStatus llfpl_directive_processor_execute_module(LlfplModuleExecutionEnvironment *environment,
                                                     const char *canonical_module_path);

/* Enumeration support for the command line help. */
size_t llfpl_directive_count(void);
const char *llfpl_directive_signature_at(size_t directive_index);
const char *llfpl_directive_purpose_at(size_t directive_index);

/* Reports whether a name is one of the top-level directive keywords. */
int llfpl_directive_name_is_reserved(const char *name_text, size_t name_length);

#endif /* LLFPL_INTERPRETER_DIRECTIVE_PROCESSOR_H */

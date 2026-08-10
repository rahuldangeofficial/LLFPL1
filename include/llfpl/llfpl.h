/*
 * llfpl.h -- Umbrella header for embedding the LLFPL runtime.
 *
 * Include this to obtain the whole public interface in one line. Nothing in the
 * library requires it: every header below is self-contained and includes
 * exactly what it needs, so a translation unit that touches only one subsystem
 * should include that subsystem's header directly and pay for nothing else.
 *
 * Embedding an interpreter takes three calls:
 *
 *     LlfplInterpreterOptions options;
 *     LlfplInterpreterSession *session = NULL;
 *
 *     llfpl_interpreter_options_initialise(&options);
 *     llfpl_interpreter_session_create(&options, &session);
 *     llfpl_interpreter_session_execute_source_file(session, "program.LLFPL");
 *     llfpl_interpreter_session_destroy(session);
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_LLFPL_H
#define LLFPL_LLFPL_H

/* Foundation */
#include "llfpl/core/compiler_attributes.h"
#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/diagnostic_reporter.h"
#include "llfpl/core/filesystem_path.h"
#include "llfpl/core/identifier.h"
#include "llfpl/core/monotonic_clock.h"
#include "llfpl/core/numeric_conversion.h"
#include "llfpl/core/status_code.h"

/* Storage */
#include "llfpl/memory/aligned_allocation.h"
#include "llfpl/memory/memory_arena.h"
#include "llfpl/memory/source_file_mapping.h"

/* Host */
#include "llfpl/hardware/hardware_topology.h"
#include "llfpl/hardware/vector_register_file.h"

/* Frontend */
#include "llfpl/frontend/lexical_scanner.h"
#include "llfpl/frontend/template_definition.h"

/* Runtime */
#include "llfpl/runtime/activation_frame.h"
#include "llfpl/runtime/builtin_operation.h"
#include "llfpl/runtime/execution_context.h"
#include "llfpl/runtime/expression_evaluator.h"
#include "llfpl/runtime/primitive_reduction.h"
#include "llfpl/runtime/symbol_table.h"

/* Interpreter */
#include "llfpl/interpreter/directive_processor.h"
#include "llfpl/interpreter/interpreter_session.h"
#include "llfpl/interpreter/module_loader.h"

#endif /* LLFPL_LLFPL_H */

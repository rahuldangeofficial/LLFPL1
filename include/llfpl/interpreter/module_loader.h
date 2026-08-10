/*
 * module_loader.h -- Resolution and identity tracking for required modules.
 *
 * The loader answers two questions and nothing else: where does this module
 * specification point, and has that module already been loaded. Keeping it
 * ignorant of what a module contains is what allows the same loader to serve
 * the command line, the Require directive and any future consumer.
 *
 * Module identity is the canonical path, so two specifications that reach the
 * same file through different relative routes are recognised as one module.
 * That is what makes a diamond of Require declarations load the shared
 * dependency once, and what makes a cycle terminate instead of recursing until
 * the stack is gone.
 *
 * Author: Rahul Dange
 * Year:   2026
 */

#ifndef LLFPL_INTERPRETER_MODULE_LOADER_H
#define LLFPL_INTERPRETER_MODULE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "llfpl/core/configuration_limits.h"
#include "llfpl/core/status_code.h"

typedef struct {
    char search_directories[LLFPL_MAXIMUM_MODULE_SEARCH_DIRECTORIES]
                           [LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    uint32_t search_directory_count;

    char loaded_module_paths[LLFPL_MAXIMUM_LOADED_MODULES][LLFPL_MODULE_PATH_BUFFER_CAPACITY];
    uint32_t loaded_module_count;
} LlfplModuleLoader;

void llfpl_module_loader_initialise(LlfplModuleLoader *loader);

/*
 * Appends a directory to the search list. Directories are searched in the order
 * they were added, so an explicitly supplied directory always takes precedence
 * over the ones discovered automatically.
 */
LlfplStatus llfpl_module_loader_add_search_directory(LlfplModuleLoader *loader,
                                                     const char *directory_path);

/*
 * Adds the directories where a standard library is expected to live, derived
 * from the location of the running executable:
 *
 *     <executable directory>/../lib/llfpl          a build tree
 *     <executable directory>/../share/llfpl/lib    an installed tree
 *
 * Deriving them from the binary rather than from the working directory is what
 * lets an installed llfpl find its standard library no matter where it is
 * invoked from, without an environment variable and without a compiled-in
 * absolute path that a relocation would invalidate.
 */
void llfpl_module_loader_add_default_directories(LlfplModuleLoader *loader,
                                                 const char *invocation_argument);

/*
 * Resolves a module specification to a canonical path.
 *
 * Search order:
 *     1. the specification itself, if absolute
 *     2. relative to the directory of the module that requested it
 *     3. each registered search directory, in order
 *     4. relative to the current working directory
 *
 * Resolving relative to the requesting module first is what makes a group of
 * modules relocatable as a unit: they refer to each other by their own layout,
 * not by where the process happens to have been started.
 */
LlfplStatus llfpl_module_loader_resolve(const LlfplModuleLoader *loader,
                                        const char *requesting_module_path,
                                        const char *module_specification,
                                        char *canonical_path_out,
                                        size_t buffer_capacity);

/* Reports whether a canonical path has already been loaded. */
int llfpl_module_loader_is_loaded(const LlfplModuleLoader *loader, const char *canonical_path);

/* Records a canonical path as loaded, so a later Require of it is a no-op. */
LlfplStatus llfpl_module_loader_mark_loaded(LlfplModuleLoader *loader, const char *canonical_path);

#endif /* LLFPL_INTERPRETER_MODULE_LOADER_H */

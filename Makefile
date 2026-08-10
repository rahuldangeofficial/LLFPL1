# =============================================================================
# LLFPL -- Low-Level Floating-Point Language
#
# Build targets
#   all        Build the interpreter into bin/ (release configuration).
#   debug      Rebuild with assertions, frame pointers and no optimisation.
#   sanitize   Rebuild under the address and undefined-behaviour sanitizers.
#   test       Build and run the golden-file test suite.
#   format     Rewrite every source file with clang-format.
#   format-check
#              Fail if any source file is not already formatted.
#   compile-commands
#              Emit compile_commands.json for clangd and clang-tidy.
#   install    Install the binary and the standard library under PREFIX.
#   uninstall  Remove an installation from PREFIX.
#   clean      Remove all build output.
#
# Author: Rahul Dange
# Year:   2026
# =============================================================================

PROJECT_NAME    := llfpl
PROJECT_VERSION := 1.0.0

# ---- Toolchain --------------------------------------------------------------

CC        ?= cc
CLANG_FORMAT ?= clang-format
INSTALL   ?= install
PREFIX    ?= /usr/local

# ---- Layout -----------------------------------------------------------------

SOURCE_DIR   := src
INCLUDE_DIR  := include
LIBRARY_DIR  := lib
BUILD_DIR    := build
BINARY_DIR   := bin
TEST_DIR     := tests

BINARY       := $(BINARY_DIR)/$(PROJECT_NAME)

SOURCES      := $(shell find $(SOURCE_DIR) -name '*.c' | sort)
OBJECTS      := $(patsubst $(SOURCE_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPENDENCIES := $(OBJECTS:.o=.d)

HEADERS      := $(shell find $(INCLUDE_DIR) -name '*.h' | sort)
FORMATTABLE  := $(SOURCES) $(HEADERS)

# ---- Compilation flags -------------------------------------------------------

# _POSIX_C_SOURCE selects posix_memalign, clock_gettime and realpath.
# _DARWIN_C_SOURCE restores the Darwin-only declarations that the strict POSIX
# level would otherwise hide, which is where sysctlbyname and CLOCK_UPTIME_RAW
# live on macOS.
FEATURE_FLAGS := -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE

WARNING_FLAGS := \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wshadow \
	-Wcast-qual \
	-Wcast-align \
	-Wconversion \
	-Wsign-conversion \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wpointer-arith \
	-Wwrite-strings \
	-Wredundant-decls \
	-Wundef \
	-Wdouble-promotion \
	-Wformat=2 \
	-Wvla

# -O3 with link-time optimisation lets the hot-path accessors, which are inline
# in their headers, fold into the evaluator across translation units.
# -fno-math-errno is the only floating-point relaxation applied: it stops the
# compiler treating fmod as a function that can set errno. No flag that changes
# a computed result is used, because IEEE 754 semantics are a language promise
# and -ffast-math would break it.
RELEASE_FLAGS := -O3 -flto -fno-math-errno -DNDEBUG
DEBUG_FLAGS   := -O0 -g3 -fno-omit-frame-pointer
SANITIZE_FLAGS := -O1 -g3 -fno-omit-frame-pointer \
	-fsanitize=address,undefined -fno-sanitize-recover=all

# The configuration is chosen from the goals of this single invocation. Nothing
# recurses into a second make, so there is exactly one parse, one set of flags,
# and no window in which a sub-make disagrees with its parent about which
# configuration the object tree currently holds.
ifneq (,$(filter sanitize,$(MAKECMDGOALS)))
CONFIGURATION_FLAGS := $(SANITIZE_FLAGS)
else ifneq (,$(filter debug,$(MAKECMDGOALS)))
CONFIGURATION_FLAGS := $(DEBUG_FLAGS)
else
CONFIGURATION_FLAGS ?= $(RELEASE_FLAGS)
endif

CPPFLAGS := -I$(INCLUDE_DIR) $(FEATURE_FLAGS) \
	-DLLFPL_VERSION_STRING=\"$(PROJECT_VERSION)\"
CFLAGS   := -std=c11 $(WARNING_FLAGS) $(CONFIGURATION_FLAGS) -MMD -MP
LDFLAGS  := $(CONFIGURATION_FLAGS)
LDLIBS   := -lm

# ---- Configuration change detection ---------------------------------------------
#
# Objects built under one configuration must never be linked under another: a
# sanitized object linked without the sanitizer runtime fails at link time, and
# a debug object linked into a release binary would silently ship unoptimised
# code.
#
# The active configuration is recorded in a stamp file and compared at parse
# time. A change discards the object tree outright rather than relying on the
# stamp being newer than the objects, because file modification times are only
# granular to the second on some filesystems and a fast enough edit-build cycle
# would slip inside that window.

CONFIGURATION_STAMP := $(BUILD_DIR)/.configuration

$(shell mkdir -p $(BUILD_DIR); \
	recorded_configuration=$$(cat $(CONFIGURATION_STAMP) 2>/dev/null); \
	current_configuration='$(CC)|$(CONFIGURATION_FLAGS)'; \
	if [ "$$recorded_configuration" != "$$current_configuration" ]; then \
		find $(BUILD_DIR) \( -name '*.o' -o -name '*.d' \) -delete 2>/dev/null; \
		printf '%s' "$$current_configuration" > $(CONFIGURATION_STAMP); \
	fi)

# ---- Primary targets ----------------------------------------------------------

.PHONY: all
all: $(BINARY)

$(BINARY): $(OBJECTS) | $(BINARY_DIR)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BINARY_DIR):
	@mkdir -p $@

# Configuration selectors. Combine them with any other goal in one invocation,
# for example "make sanitize test", so that the suite runs against the binary
# the same command just produced.
.PHONY: debug
debug: $(BINARY)

.PHONY: sanitize
sanitize: $(BINARY)

# ---- Testing --------------------------------------------------------------------

.PHONY: test
test: $(BINARY)
	@$(TEST_DIR)/run_test_suite.sh $(BINARY)

# ---- Formatting ------------------------------------------------------------------

.PHONY: format
format:
	@$(CLANG_FORMAT) -i $(FORMATTABLE)
	@echo "formatted $(words $(FORMATTABLE)) files"

.PHONY: format-check
format-check:
	@$(CLANG_FORMAT) --dry-run --Werror $(FORMATTABLE)
	@echo "all $(words $(FORMATTABLE)) files are correctly formatted"

# ---- Tooling support ---------------------------------------------------------------

.PHONY: compile-commands
compile-commands:
	@printf '[\n' > compile_commands.json
	@first=1; for source in $(SOURCES); do \
		if [ $$first -eq 0 ]; then printf ',\n' >> compile_commands.json; fi; \
		first=0; \
		command_line=$$(printf '%s %s %s -c %s -o %s' \
			'$(CC)' '$(CPPFLAGS)' '$(CFLAGS)' "$$source" \
			"$(BUILD_DIR)/$${source#$(SOURCE_DIR)/}.o" \
			| sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'); \
		printf '  {\n    "directory": "%s",\n    "command": "%s",\n    "file": "%s"\n  }' \
			"$(CURDIR)" "$$command_line" "$$source" \
			>> compile_commands.json; \
	done
	@printf '\n]\n' >> compile_commands.json
	@echo "wrote compile_commands.json"

# ---- Installation ---------------------------------------------------------------------

.PHONY: install
install: $(BINARY)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/share/$(PROJECT_NAME)/lib
	$(INSTALL) -m 0755 $(BINARY) $(DESTDIR)$(PREFIX)/bin/$(PROJECT_NAME)
	$(INSTALL) -m 0644 $(LIBRARY_DIR)/$(PROJECT_NAME)/*.LLFPL \
		$(DESTDIR)$(PREFIX)/share/$(PROJECT_NAME)/lib
	@echo "installed $(PROJECT_NAME) $(PROJECT_VERSION) into $(DESTDIR)$(PREFIX)"

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(PROJECT_NAME)
	rm -rf $(DESTDIR)$(PREFIX)/share/$(PROJECT_NAME)
	@echo "removed $(PROJECT_NAME) from $(DESTDIR)$(PREFIX)"

# ---- Housekeeping -----------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(BINARY_DIR)

-include $(DEPENDENCIES)

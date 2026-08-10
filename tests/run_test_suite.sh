#!/bin/sh
#
# run_test_suite.sh -- Golden-file test runner for the LLFPL interpreter.
#
# Each program in tests/programs is executed and compared against the
# expectations recorded in tests/expected:
#
#     <name>.out      exact standard output (required)
#     <name>.status   expected exit status  (optional, defaults to 0)
#     <name>.err      lines that must each appear somewhere in standard error
#                     (optional; matched as substrings so that the absolute
#                     module paths in a diagnostic do not have to be predicted)
#
# Standard output is compared exactly because it is the program's result and
# nothing about it is allowed to drift. Standard error is compared loosely
# because it carries paths and timings that legitimately differ between hosts.
#
# Usage: tests/run_test_suite.sh [path-to-llfpl]
#
# Author: Rahul Dange
# Year:   2026

set -u

suite_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_directory=$(dirname -- "$suite_directory")
interpreter=${1:-$project_directory/bin/llfpl}

program_directory="$suite_directory/programs"
expectation_directory="$suite_directory/expected"

if [ ! -x "$interpreter" ]; then
    printf 'test suite: no interpreter at %s; run make first\n' "$interpreter" >&2
    exit 2
fi

passed_count=0
failed_count=0
failed_names=''

for program_path in "$program_directory"/*.LLFPL; do
    test_name=$(basename -- "$program_path" .LLFPL)

    expected_output_path="$expectation_directory/$test_name.out"
    expected_status_path="$expectation_directory/$test_name.status"
    expected_error_path="$expectation_directory/$test_name.err"

    if [ ! -f "$expected_output_path" ]; then
        printf 'FAIL %-28s no expectation file %s\n' "$test_name" "$expected_output_path"
        failed_count=$((failed_count + 1))
        failed_names="$failed_names $test_name"
        continue
    fi

    actual_output_path=$(mktemp)
    actual_error_path=$(mktemp)

    "$interpreter" "$program_path" >"$actual_output_path" 2>"$actual_error_path"
    actual_status=$?

    expected_status=0
    if [ -f "$expected_status_path" ]; then
        expected_status=$(cat -- "$expected_status_path")
    fi

    failure_reason=''

    if [ "$actual_status" -ne "$expected_status" ]; then
        failure_reason="exit status $actual_status, expected $expected_status"
    elif ! diff -u -- "$expected_output_path" "$actual_output_path" >/dev/null 2>&1; then
        failure_reason='standard output differs'
    elif [ -f "$expected_error_path" ]; then
        while IFS= read -r required_fragment; do
            [ -z "$required_fragment" ] && continue

            if ! grep -qF -- "$required_fragment" "$actual_error_path"; then
                failure_reason="standard error is missing: $required_fragment"
                break
            fi
        done < "$expected_error_path"
    fi

    if [ -z "$failure_reason" ]; then
        printf 'PASS %s\n' "$test_name"
        passed_count=$((passed_count + 1))
    else
        printf 'FAIL %-28s %s\n' "$test_name" "$failure_reason"
        diff -u -- "$expected_output_path" "$actual_output_path" | sed 's/^/       /'
        sed 's/^/       stderr: /' "$actual_error_path"
        failed_count=$((failed_count + 1))
        failed_names="$failed_names $test_name"
    fi

    rm -f -- "$actual_output_path" "$actual_error_path"
done

printf '\n%d passed, %d failed\n' "$passed_count" "$failed_count"

if [ "$failed_count" -ne 0 ]; then
    printf 'failing:%s\n' "$failed_names"
    exit 1
fi

exit 0

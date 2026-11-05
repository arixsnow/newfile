#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# hangs.sh - a test that hangs or never runs fails and leaves nothing running

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

TESTS_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

gone() {
    t_pid=$(cat "$1" 2>/dev/null)
    if [ -z "$t_pid" ]; then
        return 1
    fi
    t_tries=0
    while [ "$t_tries" -lt 100 ]; do
        t_state=$(sed -n 's/^State:[[:space:]]*\(.\).*/\1/p' \
            "/proc/$t_pid/status" 2>/dev/null)
        if [ -z "$t_state" ] || [ "$t_state" = Z ]; then
            return 0
        fi
        sleep 0.1
        t_tries=$((t_tries + 1))
    done
    return 1
}

check_gone() {
    if gone "$2"; then
        pass "$1"
    else
        fail "$1" "process $(cat "$2" 2>/dev/null) is still running"
    fi
}

check_cases() {
    run env TESTS= CHECK_CASES="$CASE/cases" JOBS=1 TEST_TIMEOUT=1 \
        TEST_GRACE=1 sh "$TESTS_DIR/check.sh"
}

broken_tool() {
    mkdir bin
    printf '#!/bin/sh\nexit 125\n' >"bin/$1"
    chmod +x "bin/$1"
}

begin hanging
mkdir cases
cat >cases/hang.sh <<EOF
echo "TAP version 14"
echo "ok 1 - before the hang"
sleep 98765 &
echo \$! >"$CASE/child"
wait
EOF
check_cases
check_rc "a test file that hangs fails the run" 1
check_out "the report says it timed out" "timed out after 1 seconds"
check_out "and names the last check" "last check: before the hang"
check_gone "and what it started is stopped" child

begin stubborn
mkdir cases
cat >cases/stubborn.sh <<EOF
echo "TAP version 14"
trap '' TERM
sleep 98765 &
echo \$! >"$CASE/child"
wait
sleep 98765
EOF
check_cases
check_rc "a test file that ignores SIGTERM fails the run" 1
check_out "and is reported as timed out" "timed out after 1 seconds"
check_gone "and what it started is killed" child

begin leftover
mkdir cases
cat >cases/leftover.sh <<EOF
echo "TAP version 14"
sleep 98765 &
echo \$! >"$CASE/child"
echo "ok 1 - finished"
echo "1..1"
exit 0
EOF
check_cases
check_rc "a test file that finishes passes" 0
check_gone "and what it left running is stopped" child

if need_exec; then
    begin crash
    printf '#!/bin/sh\nexit 2\n' >crash
    chmod +x crash
    run env TESTS=signals NEWFILE="$CASE/crash" sh "$TESTS_DIR/check.sh"
    check_rc "a newfile that dies at once fails the signal tests" 1
    check_eq "without running into the time limit" "" \
        "$(grep -F 'timed out' "$T_OUT")"

    begin never_ran
    mkdir cases
    cat >cases/never.sh <<EOF
echo "TAP version 14"
echo "ok 1 - ran"
echo "1..1"
EOF
    broken_tool xargs
    run env PATH="$CASE/bin:$PATH" TESTS= CHECK_CASES="$CASE/cases" JOBS=1 \
        sh "$TESTS_DIR/check.sh"
    check_rc "a test file that never ran fails the run" 1
    check_out "and is named in the report" "did not run to the end"

    begin no_scratch
    broken_tool mktemp
    run env PATH="$CASE/bin:$PATH" sh "$TESTS_DIR/check.sh"
    check_rc "with no scratch directory the run stops" 2
else
    skip_why "a newfile that dies at once fails the signal tests"
    skip_why "without running into the time limit"
    skip_why "a test file that never ran fails the run"
    skip_why "and is named in the report"
    skip_why "with no scratch directory the run stops"
fi

if need_strace && need_exec; then
    begin faults_unmade
    broken_tool xargs
    run env PATH="$CASE/bin:$PATH" FAULTS_SCENARIOS=plain \
        sh "$TESTS_DIR/faults.sh"
    check_rc "fault runs that were never made fail the stage" 1
    check_out "and the report says so" "not ok 3 - every fault run was made"

    begin faults_no_scratch
    broken_tool mktemp
    run env PATH="$CASE/bin:$PATH" sh "$TESTS_DIR/faults.sh"
    check_rc "with no scratch directory the fault stage stops" 99

    begin faults
    printf '#!/bin/sh\nexec sleep 98765\n' >hung
    chmod +x hung
    run env NEWFILE="$CASE/hung" FAULTS_SCENARIOS=plain FAULTS_TIMEOUT=1 \
        sh "$TESTS_DIR/faults.sh"
    check_rc "a newfile that hangs fails the fault runs" 1
    check_out "the fault report says it hung" "the clean run hung"
    check_out "and that it stopped there" "stopped at the first hang"

    begin faults_stubborn
    cat >stubborn <<EOF
#!/bin/sh
trap '' TERM
sleep 98765 &
echo \$! >"$CASE/child"
wait
sleep 98765
EOF
    chmod +x stubborn
    run env NEWFILE="$CASE/stubborn" FAULTS_SCENARIOS=plain FAULTS_TIMEOUT=1 \
        FAULTS_GRACE=1 sh "$TESTS_DIR/faults.sh"
    check_rc "a newfile that ignores SIGTERM fails the fault runs" 1
    check_gone "and what it started is killed after the fault runs" child
else
    skip_why "fault runs that were never made fail the stage"
    skip_why "and the report says so"
    skip_why "with no scratch directory the fault stage stops"
    skip_why "a newfile that hangs fails the fault runs"
    skip_why "the fault report says it hung"
    skip_why "and that it stopped there"
    skip_why "a newfile that ignores SIGTERM fails the fault runs"
    skip_why "and what it started is killed after the fault runs"
fi

tap_done

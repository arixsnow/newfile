# shellcheck shell=sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# lib.sh - what every file in tests/cases sources

set -u
unset COLUMNS VERSION_CONTROL

NEWFILE="${NEWFILE:-./build/newfile}"
case "$NEWFILE" in
    /*) ;;
    *) NEWFILE="$PWD/$NEWFILE" ;;
esac
GAPCHECK="${GAPCHECK:-$(dirname "$NEWFILE")/gapcheck}"
case "$GAPCHECK" in
    /*) ;;
    *) GAPCHECK="$PWD/$GAPCHECK" ;;
esac

T_FILE=${0##*/}
T_FILE=${T_FILE%.sh}
T_N=0
T_FAILED=0
T_WHY=
T_CMD=
RC=0

echo "TAP version 14"

if ! T_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/newfile-$T_FILE.XXXXXX"); then
    echo "Bail out! cannot make a scratch directory"
    exit 99
fi
T_OUT=$T_ROOT/.out
T_ERR=$T_ROOT/.err
: >"$T_OUT"
: >"$T_ERR"
CASE=$T_ROOT

# shellcheck disable=SC2329
t_cleanup() {
    cd / || return
    if [ -n "${T_UMOUNT:-}" ]; then
        umount "$T_UMOUNT"
    fi
    if [ "${KEEP:-0}" = 1 ]; then
        echo "# kept $T_ROOT"
        return
    fi
    chmod -R u+rwx "$T_ROOT" 2>/dev/null
    rm -rf "$T_ROOT"
}
trap t_cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if [ "${VERBOSE:-0}" = 1 ]; then
    set -x
fi

begin() {
    CASE=$T_ROOT/$1
    mkdir "$CASE" || bail "cannot make $CASE"
    cd "$CASE" || bail "cannot enter $CASE"
    T_CMD=
}

t_desc() {
    printf '%s' "$1" | sed 's/#/\\#/g'
}

pass() {
    T_N=$((T_N + 1))
    printf 'ok %d - %s\n' "$T_N" "$(t_desc "$1")"
}

fail() {
    T_N=$((T_N + 1))
    T_FAILED=$((T_FAILED + 1))
    printf 'not ok %d - %s\n' "$T_N" "$(t_desc "$1")"
    shift
    for t_line in "$@"; do
        printf '#   %s\n' "$t_line"
    done
    if [ -n "$T_CMD" ]; then
        printf '#   last command: %s (exit %s)\n' "$T_CMD" "$RC"
        sed 's/^/#   stderr: /' "$T_ERR"
    fi
}

skip() {
    T_N=$((T_N + 1))
    printf 'ok %d - %s # SKIP %s\n' "$T_N" "$(t_desc "$1")" "$2"
}

skip_why() {
    skip "$1" "$T_WHY"
}

skip_all() {
    if [ "$T_N" -ne 0 ]; then
        bail "skip_all after $T_N checks"
    fi
    echo "1..0 # SKIP $1"
    exit 77
}

bail() {
    echo "Bail out! $1"
    exit 99
}

tap_done() {
    echo "1..$T_N"
    if [ "$T_FAILED" -ne 0 ]; then
        exit 1
    fi
    exit 0
}

run() {
    T_CMD="$*"
    "$@" >"$T_OUT" 2>"$T_ERR"
    RC=$?
}

nf() {
    run "$NEWFILE" "$@"
}

nf_pipe() {
    t_producer=$1
    shift
    T_CMD="$t_producer | newfile $*"
    sh -c "$t_producer" | "$NEWFILE" "$@" >"$T_OUT" 2>"$T_ERR"
    RC=$?
}

check_eq() {
    if [ "$2" = "$3" ]; then
        pass "$1"
    else
        fail "$1" "expected '$2'" "got '$3'"
    fi
}

check_rc() {
    check_eq "$1" "$2" "$RC"
}

check_exit() {
    t_desc_=$1
    t_want=$2
    shift 2
    run "$@"
    check_rc "$t_desc_" "$t_want"
}

check_file() {
    if [ -f "$2" ]; then
        pass "$1"
    else
        fail "$1" "'$2' is not a regular file"
    fi
}

check_absent() {
    if [ -e "$2" ] || [ -L "$2" ]; then
        fail "$1" "'$2' exists"
    else
        pass "$1"
    fi
}

check_symlink() {
    if [ -L "$2" ]; then
        pass "$1"
    else
        fail "$1" "'$2' is not a symbolic link"
    fi
}

check_dir() {
    if [ -d "$2" ] && [ ! -L "$2" ]; then
        pass "$1"
    else
        fail "$1" "'$2' is not a directory"
    fi
}

check_mode() {
    check_eq "$1" "$2" "$(stat -c '%a' "$3" 2>&1)"
}

check_owner() {
    check_eq "$1" "$2" "$(stat -c '%u' "$3" 2>&1)"
}

check_group() {
    check_eq "$1" "$2" "$(stat -c '%g' "$3" 2>&1)"
}

check_size() {
    check_eq "$1" "$2" "$(stat -c '%s' "$3" 2>&1)"
}

check_content() {
    check_eq "$1" "$2" "$(cat "$3" 2>&1)"
}

check_same() {
    if cmp -s "$2" "$3"; then
        pass "$1"
    else
        fail "$1" "'$2' and '$3' differ"
    fi
}

check_err() {
    if grep -qF -- "$2" "$T_ERR"; then
        pass "$1"
    else
        fail "$1" "stderr lacks '$2'"
    fi
}

check_out() {
    if grep -qF -- "$2" "$T_OUT"; then
        pass "$1"
    else
        fail "$1" "stdout lacks '$2'" "stdout: $(cat "$T_OUT")"
    fi
}

check_no_temps() {
    t_left=$(find "${2:-$CASE}" -name '.newfile.*' 2>/dev/null)
    if [ -z "$t_left" ]; then
        pass "$1"
    else
        fail "$1" "left: $t_left"
    fi
}

blocks_kib() {
    du -k "$1" | cut -f1
}

need_root() {
    if [ "$(id -u)" = 0 ]; then
        return 0
    fi
    T_WHY="needs root"
    return 1
}

need_nonroot() {
    if [ "$(id -u)" != 0 ]; then
        return 0
    fi
    T_WHY="root is not denied by permissions"
    return 1
}

need_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
        return 0
    fi
    T_WHY="$1 is not installed"
    return 1
}

need_strace() {
    need_cmd strace || return 1
    if strace -f -qq -o /dev/null true 2>/dev/null; then
        return 0
    fi
    T_WHY="strace cannot trace here"
    return 1
}

need_fifo() {
    if mkfifo "$T_ROOT/.fifo" 2>/dev/null; then
        rm -f "$T_ROOT/.fifo"
        return 0
    fi
    T_WHY="no FIFOs on this filesystem"
    return 1
}

need_exec() {
    printf '#!/bin/sh\nexit 0\n' >"$T_ROOT/.exec"
    chmod +x "$T_ROOT/.exec"
    if "$T_ROOT/.exec" 2>/dev/null; then
        rm -f "$T_ROOT/.exec"
        return 0
    fi
    rm -f "$T_ROOT/.exec"
    T_WHY="programs cannot run from this filesystem"
    return 1
}

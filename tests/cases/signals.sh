#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# signals.sh - a file being filled, signals during create, inherited SIG_IGN

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

if ! need_fifo; then
    for d in "a file being filled is not visible" \
             "and appears complete once filled" \
             "a signal during create leaves no file" \
             "a signal during create leaves no temporary file" \
             "SIGINT ignored by the caller is not overridden" \
             "SIGTERM still kills it" \
             "and the unfinished file is removed"; do
        skip_why "$d"
    done
    tap_done
fi

stop_writer() {
    kill "$writer" 2>/dev/null
    wait "$writer" 2>/dev/null
}

begin filling
mkfifo nv_fifo
{ printf 'part'; sleep 2; } >nv_fifo &
writer=$!
"$NEWFILE" --template=nv_fifo nv_out 2>"$T_ERR" &
pid=$!
sleep 1
check_absent "a file being filled is not visible" nv_out
wait "$pid"
stop_writer
check_content "and appears complete once filled" part nv_out

begin killed
mkfifo uc_fifo
{ printf 'part'; sleep 3; } >uc_fifo &
writer=$!
"$NEWFILE" --template=uc_fifo uc_sig >/dev/null 2>"$T_ERR" &
pid=$!
sleep 1
kill -TERM "$pid" 2>/dev/null
wait "$pid"
stop_writer
check_absent "a signal during create leaves no file" uc_sig
check_no_temps "a signal during create leaves no temporary file"

begin ignored
mkfifo ig_fifo
{ sleep 4; } >ig_fifo &
writer=$!
sh -c "trap '' INT; exec '$NEWFILE' --template=ig_fifo ig_a" \
    >/dev/null 2>"$T_ERR" &
pid=$!
sleep 1
kill -INT "$pid" 2>/dev/null
sleep 1
check_eq "SIGINT ignored by the caller is not overridden" alive \
    "$(kill -0 "$pid" 2>/dev/null && echo alive || echo dead)"
kill -TERM "$pid" 2>/dev/null
wait "$pid"
check_eq "SIGTERM still kills it" dead \
    "$(kill -0 "$pid" 2>/dev/null && echo alive || echo dead)"
stop_writer
check_absent "and the unfinished file is removed" ig_a

tap_done

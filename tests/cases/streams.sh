#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# streams.sh - -v, and standard output closed or full

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin verbose
nf -v -m 0644 verbfile
check_out "-v prints created" created
: >vord_taken
nf -v vord_new vord_taken
check_eq "-v reports the success before the later failure" 1 \
    "$(sed -n '1p' "$T_OUT" | grep -c created)"

begin closed
: >cs_taken
"$NEWFILE" -v cs_a cs_taken cs_c >&- 2>"$T_ERR"
check_size "a file made with stdout closed stays empty" 0 cs_a
check_size "so does one made after a failure" 0 cs_c
check_exit "-v with stdout closed exits non-zero" 1 \
    sh -c "'$NEWFILE' -v cs_v >&-"
check_file "with stdout closed the file is still created" cs_v

begin full
if [ -c /dev/full ]; then
    check_exit "--help to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' --help >/dev/full"
    check_exit "--version to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' --version >/dev/full"
    printf 'x' >full_v
    check_exit "-v to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' -f -v full_v >/dev/full"
    check_size "with stdout full the file is still created" 0 full_v
else
    for d in "--help to a full device exits non-zero" \
             "--version to a full device exits non-zero" \
             "-v to a full device exits non-zero" \
             "with stdout full the file is still created"; do
        skip "$d" "no /dev/full"
    done
fi

tap_done

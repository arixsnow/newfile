#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# help.sh - --help, --version and the usage synopsis

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin help
check_exit "--help exits 0" 0 "$NEWFILE" --help

HELP=$("$NEWFILE" --help 2>&1)
check_eq "no --help line is wider than 80 columns" "0" \
    "$(echo "$HELP" | awk '{ if (length > 80) n++ } END { print n + 0 }')"

missing=
for o in help version mode absolute parents owner reference size force \
         template backup sparse verbose; do
    echo "$HELP" | grep -q -- "--$o" || missing="$missing --$o"
done
for o in m A p o s f b v; do
    echo "$HELP" | grep -q -- "  -$o" || missing="$missing -$o"
done
check_eq "every option is described in --help" "" "$missing"
check_eq "--help lists every option in the table once" "14" \
    "$(echo "$HELP" | grep -cE '^  -|^      --')"

check_eq "COLUMNS narrows the help output" "0" \
    "$(COLUMNS=50 "$NEWFILE" --help 2>&1 |
        awk '{ if (length > 50 && $2 != "") n++ } END { print n + 0 }')"
for bad in abc 5 99999 " 60" "+60" "-60" "0x50"; do
    check_eq "COLUMNS='$bad' falls back to the default width" "0" \
        "$(COLUMNS="$bad" "$NEWFILE" --help 2>&1 |
            awk '{ if (length > 80) n++ } END { print n + 0 }')"
done

begin synopsis
SYN=$("$NEWFILE" --nosuch-option 2>&1 | sed -n '2,3p' | tr -d '\n')
check_eq "the synopsis option cluster is sorted" "sorted" \
    "$(echo "$SYN" | sed -n 's/.*\[-\([A-Za-z]*\)\].*/\1/p' |
        grep -o . | sort -c 2>/dev/null && echo sorted || echo unsorted)"
OUT=$("$NEWFILE" --nosuch-option 2>&1)
NAME=$(echo "$OUT" | sed -n '1s/:.*//p')
check_eq "the synopsis continuation aligns under the program name" \
    "$((7 + ${#NAME} + 1))" \
    "$(echo "$OUT" | sed -n '3p' | awk '{ print match($0, /[^ ]/) - 1 }')"

begin version
check_exit "--version exits 0" 0 "$NEWFILE" --version
VER=$("$NEWFILE" --version 2>&1)
check_eq "--version names the license by its SPDX identifier" "1" \
    "$(echo "$VER" | grep -c 'License: BSD-3-Clause\.')"
check_eq "--version points at no file it cannot guarantee" "0" \
    "$(echo "$VER" | grep -ci 'see the\|LICENSE file\|see the source')"
check_eq "no --version line is wider than 80 columns" "0" \
    "$(echo "$VER" | awk '{ if (length > 80) n++ } END { print n + 0 }')"

tap_done

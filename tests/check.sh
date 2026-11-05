#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# check.sh - runs tests/cases and sums up their TAP

set -u

SELF=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
CASES=${CHECK_CASES:-${SELF%/*}/cases}

if [ "${1:-}" = one ]; then
    env --default-signal=INT,QUIT timeout -k "$TEST_GRACE" "$TEST_TIMEOUT" \
        sh "$CASES/$2.sh" >"$LOGS/$2.tap" 2>"$LOGS/$2.err" &
    runner=$!
    trap 'kill "$runner" 2>/dev/null' HUP INT TERM
    wait "$runner"
    rc=$?
    while kill -0 "$runner" 2>/dev/null; do
        wait "$runner"
        rc=$?
    done
    kill -s KILL -- "-$runner" 2>/dev/null
    { echo "$rc" >"$LOGS/$2.rc"; } 2>/dev/null
    exit 0
fi

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
if [ ! -x "$NEWFILE" ]; then
    echo "check.sh: no newfile at $NEWFILE" >&2
    exit 2
fi

if [ -n "${TESTS:-}" ]; then
    NAMES=$TESTS
    for n in $NAMES; do
        if [ ! -f "$CASES/$n.sh" ]; then
            echo "check.sh: no tests/cases/$n.sh" >&2
            exit 2
        fi
    done
else
    NAMES=$(cd "$CASES" && for f in *.sh; do echo "${f%.sh}"; done)
fi

if ! LOGS=$(mktemp -d); then
    echo "check.sh: cannot make a scratch directory" >&2
    exit 2
fi
TEST_TIMEOUT=${TEST_TIMEOUT:-600}
TEST_GRACE=${TEST_GRACE:-30}
export LOGS NEWFILE GAPCHECK TEST_TIMEOUT TEST_GRACE
trap 'rm -rf "$LOGS"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN)}"

# shellcheck disable=SC2086
printf '%s\n' $NAMES | xargs -n 1 -P "$JOBS" sh "$SELF" one

: >"$LOGS/details"
: >"$LOGS/skips"
: >"$LOGS/status"

summarize() {
    awk -v rc="$(cat "$LOGS/$1.rc")" -v name="$1" -v limit="$TEST_TIMEOUT" \
        -v det="$LOGS/details" -v skips="$LOGS/skips" '
        NR == 1 {
            if ($0 != "TAP version 14") err = "no TAP version line"
            next
        }
        /^ok / {
            n++
            infail = 0
            last = $0
            sub(/^ok [0-9]+ - /, "", last)
            sub(/ # SKIP .*/, "", last)
            if ($0 ~ / # SKIP /) {
                s++
                why = $0
                sub(/.* # SKIP /, "", why)
                d = $0
                sub(/^ok [0-9]+ - /, "", d)
                sub(/ # SKIP .*/, "", d)
                print why "\t" name ": " d >> skips
            } else {
                p++
            }
            next
        }
        /^not ok / {
            n++
            f++
            infail = 1
            last = $0
            sub(/^not ok [0-9]+ - /, "", last)
            print name ": " $0 >> det
            next
        }
        /^#/ {
            if (infail) print "    " $0 >> det
            next
        }
        /^1\.\.0 # SKIP / {
            all = $0
            sub(/^1\.\.0 # SKIP /, "", all)
            next
        }
        /^1\.\.[0-9]+$/ {
            plan = substr($0, 4) + 0
            planned = 1
            next
        }
        /^Bail out!/ {
            err = $0
            next
        }
        { infail = 0 }
        END {
            if (all != "" && rc == 77 && err == "") {
                print all "\t" name ": the whole file" >> skips
                printf "%s\tskip\t0\t0\t1\t%s\n", name, all
                exit
            }
            if (rc == 124 || rc == 137) {
                err = "timed out after " limit " seconds"
                if (last != "") err = err ", last check: " last
            }
            if (err == "" && !planned) err = "no plan"
            if (err == "" && plan != n) err = "plan says " plan ", ran " n
            if (err == "" && rc == 0 && f) err = "exit 0 with failures"
            if (err == "" && rc == 1 && !f) err = "exit 1 with no failure"
            if (err == "" && rc != 0 && rc != 1) err = "exit " rc
            st = (err != "") ? "error" : (f ? "FAIL" : "ok")
            printf "%s\t%s\t%d\t%d\t%d\t%s\n", name, st, p, f, s, err
        }' "$LOGS/$1.tap"
}

for n in $NAMES; do
    if [ -f "$LOGS/$n.rc" ] && [ -f "$LOGS/$n.tap" ]; then
        summarize "$n" >>"$LOGS/status"
    else
        printf '%s\terror\t0\t0\t0\tdid not run to the end\n' "$n" \
            >>"$LOGS/status"
    fi
done

awk -F '\t' '{
    if ($2 == "skip") line = "skipped: " $6
    else {
        line = $3 " passed"
        if ($4) line = line ", " $4 " failed"
        if ($5) line = line ", " $5 " skipped"
        if ($6 != "") line = line " (" $6 ")"
    }
    printf "%-10s %-6s %s\n", $1, $2, line
}' "$LOGS/status"

if grep -q "$(printf '\t')\(FAIL\|error\)$(printf '\t')" "$LOGS/status"; then
    echo
    echo "--- failures"
    cat "$LOGS/details"
    awk -F '\t' '$2 == "error" { print $1 }' "$LOGS/status" |
        while read -r n; do
            echo "$n: $(awk -F '\t' -v n="$n" '$1 == n { print $6 }' \
                "$LOGS/status")"
            sed -n '1,20s/^/    stderr: /p' "$LOGS/$n.err" 2>/dev/null
        done
fi

if [ -s "$LOGS/skips" ]; then
    echo
    echo "--- skipped"
    sort "$LOGS/skips" | awk -F '\t' '
        $1 != last {
            if (last != "") print "  " count[last] " for " last ": " what
            last = $1
            what = ""
        }
        { count[$1]++; what = what (what == "" ? "" : "; ") $2 }
        END { if (last != "") print "  " count[last] " for " last ": " what }'
fi

# shellcheck disable=SC2086
awk -F '\t' -v want="$(printf '%s\n' $NAMES | wc -l)" '{
        p += $3
        f += $4
        s += $5
        if ($2 == "error" && $4 == 0) f++
    }
    END {
        if (NR != want) f++
        printf "\n=== %d passed, %d failed, %d skipped ===\n", p, f, s
        exit (f > 0)
    }' "$LOGS/status"

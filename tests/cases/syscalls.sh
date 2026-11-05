#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# syscalls.sh - modes, races and failures seen and made with strace

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

ASAN_OPTIONS="${ASAN_OPTIONS:+$ASAN_OPTIONS:}detect_leaks=0"
export ASAN_OPTIONS

PROBE="one probe serves every file in a directory"
RACE="a target replaced during -f -b exits 1
the backup holds the file that was backed up
the file that replaced it is kept under the spare name
the target is the new file
a directory that cannot be flushed exits 1
the spare in that directory is kept
a spare in a directory that flushed is not
the kept spare holds the old file
the other directory holds no spare"
HOLDER="a failed rename over the placeholder exits 1
a placeholder that cannot be removed is reported
the temporary file is still removed"
LISTING="a directory that cannot be listed fails that backup
the failed backup is reported
its target is left as it was
the next operand lists the directory again"

skip_each() {
    while read -r d; do
        skip "$d" "$2"
    done <<EOF
$1
EOF
}

probe() {
    t_log=$1
    shift
    T_CMD="strace -f -qq -o $t_log $*"
    strace -f -qq -o "$t_log" "$@" 2>"$T_ERR"
    RC=$?
    if [ "$RC" -ne 0 ]; then
        bail "a probe failed with status $RC: $T_CMD: $(head -n 1 "$T_ERR")"
    fi
}

if ! need_strace; then
    skip_each "only an empty probe is created with the requested mode
$PROBE
$RACE
$HOLDER
$LISTING" "$T_WHY"
    tap_done
fi

begin probe
printf 'seed\n' >tpl
mkdir tm
probe tm.log -e trace=openat,unlink,unlinkat,rename,renameat,renameat2 \
    "$NEWFILE" -m 0666 --template=tpl "$CASE/tm/a" "$CASE/tm/b"
TM=$(awk '
    match($0, /"[^"]*\/\.newfile\.[^"]*"/) {
        name = substr($0, RSTART, RLENGTH)
        if ($0 ~ /openat\(/) {
            mode = $0
            sub(/.*, /, "", mode)
            sub(/\).*/, "", mode)
            opened[name] = mode
        } else if ($0 ~ /unlink/) {
            gone[name] = 1
        } else {
            named[name] = 1
        }
    }
    END {
        for (n in opened) {
            if (opened[n] == "0600") {
                temps++
                continue
            }
            probes++
            if (!(n in gone) || (n in named)) bad++
        }
        print temps + 0, probes + 0, bad + 0
    }' tm.log)
check_eq "only an empty probe is created with the requested mode" 0 \
    "${TM##* }"
if [ "${TM%% *}" -gt 0 ]; then
    probes=${TM#* }
    check_eq "$PROBE" 1 "${probes% *}"
else
    skip "$PROBE" "files here are made unnamed"
fi

begin race
printf 'old' >exch
probe exch.log -e trace=renameat2 \
    "$NEWFILE" -f --backup=simple -s 3 "$CASE/exch"
EXN=$(awk '/renameat2\(/ { k++ }
           /RENAME_EXCHANGE\) = 0/ { print k; exit }' exch.log)
if [ -n "$EXN" ]; then
    printf 'A-original' >race
    (i=0
     while [ ! -e race~ ] && [ "$i" -lt 200 ]; do
         sleep 0.05
         i=$((i + 1))
     done
     printf 'B-concurrent' >race.new
     mv race.new race) &
    racer=$!
    run strace -f -qq -o /dev/null -e trace=renameat2 \
        -e inject=renameat2:delay_enter=3000000:when="$EXN" \
        "$NEWFILE" -f --backup=simple -s 3 "$CASE/race"
    wait "$racer"
    spare=$(sed -n \
        "s/^.*kept '\([^']*\)': it now holds a different file$/\1/p" "$T_ERR")
    check_rc "a target replaced during -f -b exits 1" 1
    check_content "the backup holds the file that was backed up" A-original \
        race~
    check_content "the file that replaced it is kept under the spare name" \
        B-concurrent "${spare:-/nonexistent}"
    check_size "the target is the new file" 3 race

    mkdir d1 d2
    printf 'old1' >d1/a
    printf 'old2' >d2/b
    run strace -f -qq -o /dev/null -P "$CASE/d1" -e trace=fsync,fdatasync \
        -e inject=fsync,fdatasync:error=EIO \
        "$NEWFILE" -f --backup=simple -s 1 "$CASE/d1/a" "$CASE/d2/b"
    check_rc "a directory that cannot be flushed exits 1" 1
    check_eq "the spare in that directory is kept" 1 \
        "$(grep -cF "the old '$CASE/d1/a'" "$T_ERR")"
    check_eq "a spare in a directory that flushed is not" 0 \
        "$(grep -cF "the old '$CASE/d2/b'" "$T_ERR")"
    check_eq "the kept spare holds the old file" old1 \
        "$(cat d1/.newfile.* 2>&1)"
    check_no_temps "the other directory holds no spare" d2
else
    skip_each "$RACE" "no exchanging rename here"
fi

begin placeholder
printf 'seed\n' >tpl
probe ph0.log -e trace=openat,rename,renameat,renameat2,unlink,unlinkat \
    "$NEWFILE" -A -m 0644 --template=tpl "$CASE/ph0"
PH=$(awk -v t="\"$CASE/ph0\"" '
    NR == 1 { main = $1 }
    $1 != main { next }
    {
        rest = $0
        sub(/^[0-9]+ +/, "", rest)
        name = substr(rest, 1, index(rest, "(") - 1)
        n[name]++
    }
    name == "openat" && index($0, t) && /O_CREAT\|O_EXCL/ {
        held = 1
        next
    }
    held && name ~ /^rename/ && index($0, t) {
        print name, n[name], n["unlink"] + 1, n["unlinkat"] + 1
        exit
    }' ph0.log)
if [ -n "$PH" ]; then
    read -r PHC PHN PHU PHUAT <<EOF
$PH
EOF
    run strace -f -qq -o /dev/null -e trace="$PHC,unlink,unlinkat" \
        -e inject="$PHC:error=EIO:when=$PHN" \
        -e inject=unlink:error=EROFS:when="$PHU" \
        -e inject=unlinkat:error=EROFS:when="$PHUAT" \
        "$NEWFILE" -A -m 0644 --template=tpl "$CASE/ph"
    check_rc "a failed rename over the placeholder exits 1" 1
    check_err "a placeholder that cannot be removed is reported" \
        "cannot remove '$CASE/ph': Read-only file system"
    check_no_temps "the temporary file is still removed"
else
    skip_each "$HOLDER" "names here need no placeholder"
fi

begin listing
for d in nl nlc; do
    mkdir "$d"
    printf 'old' >"$d/a"
    printf 'old' >"$d/b"
    printf 'x' >"$d/a.~3~"
    printf 'y' >"$d/b.~5~"
done
probe nlc.log -P "$CASE/nlc" -e trace=openat \
    "$NEWFILE" -f --backup=numbered -s 1 "$CASE/nlc/a" "$CASE/nlc/b"
NLN=$(awk '/openat\(/ { k++ } /O_DIRECTORY/ { print k; exit }' nlc.log)
if [ -z "$NLN" ]; then
    bail "the probe saw no directory being listed"
fi
run strace -f -qq -o /dev/null -P "$CASE/nl" -e trace=openat \
    -e inject=openat:error=EMFILE:when="$NLN" \
    "$NEWFILE" -f --backup=numbered -s 1 "$CASE/nl/a" "$CASE/nl/b"
check_rc "a directory that cannot be listed fails that backup" 1
check_err "the failed backup is reported" "cannot back up '$CASE/nl/a'"
check_content "its target is left as it was" old nl/a
check_content "the next operand lists the directory again" old nl/b.~6~

tap_done

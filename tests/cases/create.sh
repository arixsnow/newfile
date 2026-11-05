#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# create.sh - operands, paths, parents, batches and creates that fail

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin operands
"$NEWFILE" taken 2>"$T_ERR"
check_exit "fails on existing file" 1 "$NEWFILE" taken
nf -A -m 0644 multi1 multi2 multi3
for f in multi1 multi2 multi3; do
    check_file "$f exists" "$f"
done
nf -A -m 0644 multi1 newfail
check_rc "partial failure exits 1" 1
check_file "new file created despite partial failure" newfail

begin paths
nf -p -A -m 0644 ps/./skip/file
check_file "dot component ignored" ps/skip/file
nf -p -A -m 0644 ps/dslash//file
check_file "double slash ignored" ps/dslash/file
mkdir -p ps/real
ln -s real ps/sym
nf ps/sym/../vialink
check_file ".. through a symlink follows the link" ps/vialink
touch ps/sym/../viatouch
check_file ".. placement agrees with touch" ps/viatouch

LONG_PART=$(printf '%0100d' 0 | tr 0 a)
nf -p -A -m 0644 "ps/long/$LONG_PART/$LONG_PART/$LONG_PART/file"
check_file "long dynamic path succeeds" \
    "ps/long/$LONG_PART/$LONG_PART/$LONG_PART/file"
DEEP_PATH=$CASE/ps/deep
i=0
while [ "$i" -lt 18 ]; do
    DEEP_PATH="$DEEP_PATH/$LONG_PART"
    i=$((i + 1))
done
nf -p -A -m 0644 "$DEEP_PATH/file"
check_file "deep dynamic path succeeds" "$DEEP_PATH/file"
OVERLONG=$CASE/ps/overlong
mkdir -p "$OVERLONG"
i=0
while [ "$i" -lt 2200 ]; do
    OVERLONG="$OVERLONG/."
    i=$((i + 1))
done
check_exit "path beyond the system limit rejected" 1 \
    "$NEWFILE" -p "$OVERLONG/file"
check_exit "empty path exits 1" 1 "$NEWFILE" ""

begin parents
nf -p -A -m 0755 a/b/c/nested.txt
check_file "-p creates nested file" a/b/c/nested.txt
check_mode "-p nested perms" 755 a/b/c/nested.txt
mkdir -p pe/existing
nf -p pe/existing/f
check_file "-p with existing parent succeeds" pe/existing/f
touch pe/blk
check_exit "-p when parent path is a file exits 1" 1 "$NEWFILE" -p pe/blk/f

begin combined
nf -A -m 0755 co_ref
nf -p -A --reference=co_ref co/a/b/f
check_file "-p --reference creates nested file" co/a/b/f
check_mode "-p --reference copies mode" 755 co/a/b/f
echo old >co_fsm
nf -f -s 2K co_fsm
check_size "-f -s overwrites existing with new size" 2048 co_fsm
echo combo >co_tpl_src
nf --template=co_tpl_src -p co/c/d/f
check_content "--template -p creates nested file with content" combo co/c/d/f

begin batches
set --
i=1
while [ "$i" -le 200 ]; do
    set -- "$@" "f$i"
    i=$((i + 1))
done
nf -f -s 8 "$@"
set --
check_eq "-f across 200 operands creates all of them" 200 \
    "$(find . -name 'f*' -type f | wc -l | tr -d ' ')"
check_size "-f across 200 operands sizes them" 8 f137
nf twice twice
check_rc "an operand given twice fails the second time" 1
check_file "and the first one is created" twice
check_exit "-f with an operand given twice exits 0" 0 \
    "$NEWFILE" -f twice twice
printf 'orig' >bk2
nf -f --backup=numbered bk2 bk2
check_content "the first backup holds the original" orig bk2.~1~
check_file "the second backup holds the first replacement" bk2.~2~
check_no_temps "a batch leaves no temporary file"

begin unfinished
check_exit "create with an unreadable template exits 1" 1 \
    "$NEWFILE" --template=no_such_template uc_tpl
check_absent "a failed create leaves no file behind" uc_tpl
run sh -c "ulimit -f 100 && '$NEWFILE' -s 1M uc_a uc_b uc_c 2>&1; true"
OUT=$(cat "$T_OUT")
run sh -c "ulimit -f 100 && '$NEWFILE' -s 1M uc_d"
check_rc "a size limit is a per-file error, not a kill" 1
check_eq "a size limit names the problem" yes \
    "$(echo "$OUT" | grep -q "File too large" && echo yes || echo no)"
check_eq "every operand is attempted after the first fails" 3 \
    "$(echo "$OUT" | grep -c "File too large")"
for f in uc_a uc_b uc_c uc_d; do
    check_absent "$f removed after its create failed" "$f"
done

tap_done

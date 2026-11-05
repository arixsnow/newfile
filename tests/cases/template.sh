#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# template.sh - --template from files, devices, pipes and standard input

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

check_sparse() {
    if [ "$HOLES" = no ]; then
        skip "$1" "the filesystem does not report its gaps"
        return
    fi
    check_eq "$1" yes \
        "$([ "$(blocks_kib "$2")" -lt 512 ] && echo yes || echo no)"
}

begin src
SRC=$CASE
echo "hello from template" >tpl_src
"$NEWFILE" -s 1M --sparse tpl_tail_data
dd if=/dev/urandom of=tpl_tail_data bs=4096 seek=255 count=1 conv=notrunc \
    2>/dev/null
"$NEWFILE" -s 1M --sparse tpl_tail_gap
dd if=/dev/urandom of=tpl_tail_gap bs=4096 seek=0 count=1 conv=notrunc \
    2>/dev/null
dd if=/dev/urandom of=tpl_dense bs=4096 count=256 2>/dev/null
"$NEWFILE" -s 1M --sparse tpl_all_gap

begin basic
nf --template="$SRC/tpl_src" -A -m 0600 tpl_out
check_content "template content copied" "hello from template" tpl_out
check_mode "template mode applied" 600 tpl_out
check_exit "template nonexistent exits 1" 1 \
    "$NEWFILE" --template=no_such_file tpl_bad2
dd if=/dev/urandom of=pipe_src bs=1024 count=512 2>/dev/null
nf_pipe "dd if=pipe_src bs=65536 2>/dev/null" --template=- pipe_out
check_same "template from a pipe copies every byte" pipe_src pipe_out

begin shape
"$GAPCHECK" "$CASE"
gap=$?
case $gap in
    0) HOLES=yes ;;
    1) HOLES=no ;;
    *) HOLES=no
       check_eq "the gap probe runs" "0 or 1" "$gap" ;;
esac
nf --template="$SRC/tpl_tail_data" sh_sparse
check_size "sparse template keeps its size" 1048576 sh_sparse
check_same "sparse template copies every byte" "$SRC/tpl_tail_data" sh_sparse
check_sparse "sparse template stays sparse" sh_sparse
nf --template="$SRC/tpl_all_gap" sh_all_gap
check_size "template of nothing but gap keeps its size" 1048576 sh_all_gap
check_sparse "template of nothing but gap stays sparse" sh_all_gap
nf --template="$SRC/tpl_dense" sh_dense
check_eq "dense template stays dense" yes \
    "$([ "$(blocks_kib sh_dense)" -ge 1024 ] && echo yes || echo no)"
nf --template="$SRC/tpl_tail_data" -s 512K sh_trunc
check_size "--size below the template truncates" 524288 sh_trunc
nf --template="$SRC/tpl_tail_data" -s 2M sh_grow_data
check_size "--size above a template ending in data" 2097152 sh_grow_data
check_eq "template ending in data grows dense" yes \
    "$([ "$(blocks_kib sh_grow_data)" -ge 1024 ] && echo yes || echo no)"
nf --template="$SRC/tpl_tail_gap" -s 2M sh_grow_gap
check_size "--size above a template ending in a gap" 2097152 sh_grow_gap
check_sparse "template ending in a gap grows sparse" sh_grow_gap
nf --template="$SRC/tpl_tail_data" -s 2M --sparse sh_grow_forced
check_sparse "--sparse overrides a template ending in data" sh_grow_forced

"$NEWFILE" --template=- sh_in_one <"$SRC/tpl_tail_data" 2>"$T_ERR"
"$NEWFILE" --template=- sh_in_a sh_in_b sh_in_c <"$SRC/tpl_tail_data" \
    2>"$T_ERR"
check_size "stdin template keeps its size for one file" 1048576 sh_in_one
check_size "stdin template keeps its size for several" 1048576 sh_in_c
if cmp -s "$SRC/tpl_tail_data" sh_in_a && cmp -s "$SRC/tpl_tail_data" sh_in_c
then
    pass "stdin template copies every byte to several files"
else
    fail "stdin template copies every byte to several files" \
        "sh_in_a or sh_in_c differs from the template"
fi
check_sparse "stdin template stays sparse for several files" sh_in_c
check_eq "stdin template layout ignores the operand count" \
    "$(blocks_kib sh_in_one)" "$(blocks_kib sh_in_c)"
"$NEWFILE" --template=- -s 2M sh_in_grow_a sh_in_grow_b \
    <"$SRC/tpl_tail_gap" 2>"$T_ERR"
check_size "stdin template with --size above it" 2097152 sh_in_grow_b
check_sparse "stdin template ending in a gap grows sparse" sh_in_grow_b

begin edges
touch tpl_empty
nf --template=tpl_empty tpl_empty_out
check_size "empty template creates empty file" 0 tpl_empty_out
dd if=/dev/zero bs=256 count=1 of=tpl_bin_src 2>/dev/null
nf --template=tpl_bin_src tpl_binary
check_size "binary template copied intact" 256 tpl_binary
check_exit "a directory as template exits 1" 1 \
    "$NEWFILE" --template="$CASE" tpl_dir_out
check_absent "a directory as template creates nothing" tpl_dir_out
nf --template=/dev/null tpl_devnull
check_size "template from a character device is empty" 0 tpl_devnull

SHORT_SRC=/sys/devices/system/cpu/online
if [ -r "$SHORT_SRC" ]; then
    nf --template="$SHORT_SRC" tpl_short
    check_eq "template from an overstated size copies real content" \
        "$(cat "$SHORT_SRC")" "$(cat tpl_short)"
    check_size "template from an overstated size has the real length" \
        "$(wc -c <"$SHORT_SRC" | tr -d ' ')" tpl_short
else
    skip "template from an overstated size copies real content" \
        "$SHORT_SRC is not readable"
    skip "template from an overstated size has the real length" \
        "$SHORT_SRC is not readable"
fi

ZERO_SRC=/proc/version
if [ -r "$ZERO_SRC" ] && [ "$(stat -c '%s' "$ZERO_SRC")" = 0 ]; then
    nf --template="$ZERO_SRC" tpl_zerosize
    check_eq "template from a size of zero copies real content" \
        "$(cat "$ZERO_SRC")" "$(cat tpl_zerosize)"
else
    skip "template from a size of zero copies real content" \
        "$ZERO_SRC is not a readable file of size 0"
fi

if [ -c /dev/urandom ]; then
    dd if=/dev/urandom of=tpl_mb_src bs=1048576 count=4 2>/dev/null
    check_size "multi-megabyte template source is 4 MiB" 4194304 tpl_mb_src
    nf --template=tpl_mb_src tpl_mb_out
    check_size "multi-megabyte template has the source size" \
        "$(stat -c '%s' tpl_mb_src)" tpl_mb_out
    check_same "multi-megabyte template is byte-identical" \
        tpl_mb_src tpl_mb_out
else
    for d in "multi-megabyte template source is 4 MiB" \
             "multi-megabyte template has the source size" \
             "multi-megabyte template is byte-identical"; do
        skip "$d" "no /dev/urandom"
    done
fi

begin stdin
nf_pipe "echo 'hello stdin'" --template=- -A -m 0644 tpl_stdin
check_content "--template=- copies stdin content" "hello stdin" tpl_stdin
check_mode "--template=- applies mode" 644 tpl_stdin
nf_pipe "printf 'shared\n'" --template=- tpl_si_a tpl_si_b tpl_si_c
check_content "--template=- first of many gets stdin" shared tpl_si_a
check_content "--template=- second of many gets stdin" shared tpl_si_b
check_content "--template=- third of many gets stdin" shared tpl_si_c
nf_pipe ":" --template=- tpl_si_e1 tpl_si_e2
check_size "--template=- empty stdin first file empty" 0 tpl_si_e1
check_size "--template=- empty stdin second file empty" 0 tpl_si_e2

: >big_src
i=0
while [ "$i" -lt 16 ]; do
    printf '%08192d' 0 >>big_src
    i=$((i + 1))
done
check_size "oversized template source is 128 KiB" 131072 big_src
"$NEWFILE" --template=- tpl_big_a tpl_big_b <big_src 2>"$T_ERR"
check_size "oversized stdin first file has full size" 131072 tpl_big_a
check_size "oversized stdin second file has full size" 131072 tpl_big_b
if cmp -s big_src tpl_big_a && cmp -s big_src tpl_big_b; then
    pass "oversized stdin content is identical in both"
else
    fail "oversized stdin content is identical in both" \
        "tpl_big_a or tpl_big_b differs from big_src"
fi

: >pipe_big
i=0
while [ "$i" -lt 48 ]; do
    printf '%08192d' 0 >>pipe_big
    i=$((i + 1))
done
check_size "piped template source is 384 KiB" 393216 pipe_big
nf_pipe "dd if=pipe_big bs=65536 2>/dev/null" --template=- tpl_pipe_a \
    tpl_pipe_b
if cmp -s pipe_big tpl_pipe_a && cmp -s pipe_big tpl_pipe_b; then
    pass "piped template reaches every file"
else
    fail "piped template reaches every file" \
        "tpl_pipe_a or tpl_pipe_b differs from pipe_big"
fi

printf 'apple\nbanana\ncherry\n' >tpl_read_src
{ read -r _
  "$NEWFILE" --template=- tpl_read_out 2>"$T_ERR"
} <tpl_read_src
check_eq "--template=- resumes where standard input was left" \
    "banana cherry" "$(tr '\n' ' ' <tpl_read_out | sed 's/ $//')"
check_eq "--template=- leaves standard input after what it copied" 0 \
    "$({ "$NEWFILE" --template=- tpl_pos_one 2>"$T_ERR"
         wc -c | tr -d ' '; } <"$SRC/tpl_tail_gap")"
check_eq "--template=- leaves standard input after it for several files" 0 \
    "$({ "$NEWFILE" --template=- tpl_pos_a tpl_pos_b 2>"$T_ERR"
         wc -c | tr -d ' '; } <"$SRC/tpl_tail_gap")"

tap_done

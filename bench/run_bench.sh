#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# Benchmarks for newfile.  Run from the top of the source tree.
#
#   NEWFILE=build/newfile sh bench/run_bench.sh
#   NEWFILE_OLD=/tmp/newfile-old NEWFILE_NEW=build/newfile \
#       sh bench/run_bench.sh
#
# BENCHDIR selects the filesystem under test and is worth setting: the
# default lands wherever mktemp puts things, which on many systems is
# memory rather than a disk.  The filesystem measured is printed below.
#
# REPS, SIZE_MB and MANY_N override the defaults.  The page cache stays
# warm throughout, so these measure syscall and copy cost rather than
# the device.

NEWFILE="${NEWFILE:-./build/newfile}"
REPS="${REPS:-5}"
SIZE_MB="${SIZE_MB:-256}"
MANY_N="${MANY_N:-500}"

if [ "$(date +%N)" = "%N" ]; then
    echo "bench: date(1) here has no nanosecond field" >&2
    exit 1
fi

WORK=$(mktemp -d "${BENCHDIR:-${TMPDIR:-/tmp}}/newfile-bench.XXXXXX") || exit 1
# shellcheck disable=SC2329
cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

abspath() {
    case "$1" in
        /*) echo "$1" ;;
        *) echo "$PWD/$1" ;;
    esac
}

# Each repetition is timed alone, behind a sync.  Without one, dirty
# pages left by the previous binary land on the next and the first one
# measured looks several times faster than the same binary measured
# second.  The median then discards a rep that caught a background
# writeback anyway.
time_case() {
    label="$1"
    bin="$2"
    fn="$3"
    reset="${4:-:}"
    : > "$WORK/times"
    i=0
    while [ "$i" -lt "$REPS" ]; do
        "$reset"
        sync
        sleep 0.3
        start=$(date +%s%N)
        "$fn" "$bin"
        sync
        end=$(date +%s%N)
        echo $(( (end - start) / 1000000 )) >> "$WORK/times"
        i=$((i + 1))
    done
    printf '  %-36s %7s ms\n' "$label" \
        "$(sort -n "$WORK/times" |
           awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')"
}

# What the last repetition left behind.  Time alone hides the change
# that matters most on a sparse source.
report_space() {
    printf '  %-36s %7s KiB on disk\n' "$1" "$(du -k "$2" | cut -f1)"
}

reset_plain() {
    rm -rf "$WORK/plain"
    mkdir -p "$WORK/plain"
}

reset_tpl() {
    rm -rf "$WORK/tplmany"
    mkdir -p "$WORK/tplmany"
}

case_pipe() {
    dd if="$WORK/src_dense" bs=65536 2>/dev/null |
        "$1" -f --template=- "$WORK/out" 2>/dev/null
}

case_tpl_dense() {
    "$1" -f --template="$WORK/src_dense" "$WORK/out" 2>/dev/null
}

case_tpl_sparse() {
    "$1" -f --template="$WORK/src_sparse" "$WORK/out" 2>/dev/null
}

case_fill() {
    "$1" -f -s "${SIZE_MB}M" "$WORK/out" 2>/dev/null
}

case_fill_sparse() {
    "$1" -f -s "${SIZE_MB}M" --sparse "$WORK/out" 2>/dev/null
}

case_many() {
    xargs "$1" -f -s 8 < "$WORK/manylist" 2>/dev/null
}

case_plain() {
    xargs "$1" -s 8 < "$WORK/plainlist" 2>/dev/null
}

case_tpl_many() {
    xargs "$1" --template="$WORK/src_small" < "$WORK/tpllist" 2>/dev/null
}

# Fixtures.  The sparse source has the same apparent size as the dense
# one with two blocks of data in it.
dd if=/dev/urandom of="$WORK/src_dense" bs=1048576 count="$SIZE_MB" \
    2>/dev/null
dd if=/dev/urandom of="$WORK/src_sparse" bs=4096 count=1 2>/dev/null
dd if=/dev/urandom of="$WORK/src_sparse" bs=4096 count=1 \
    seek=$((SIZE_MB * 256 - 1)) conv=notrunc 2>/dev/null
dd if=/dev/urandom of="$WORK/src_small" bs=4096 count=1 2>/dev/null

make_list() {
    mkdir -p "$1"
    i=1
    while [ "$i" -le "$MANY_N" ]; do
        echo "$1/f$i"
        i=$((i + 1))
    done > "$2"
}

make_list "$WORK/many" "$WORK/manylist"
make_list "$WORK/plain" "$WORK/plainlist"
make_list "$WORK/tplmany" "$WORK/tpllist"

fstype=$(df -PT "$WORK" 2>/dev/null | awk 'NR == 2 {print $2}')
echo "reps=$REPS size=${SIZE_MB}M operands=$MANY_N"
echo "dir=$WORK fs=${fstype:-unknown}"

for slot in OLD NEW PLAIN; do
    case "$slot" in
        OLD) bin="$NEWFILE_OLD" ;;
        NEW) bin="$NEWFILE_NEW" ;;
        PLAIN)
            if [ -n "$NEWFILE_OLD" ] || [ -n "$NEWFILE_NEW" ]; then
                continue
            fi
            bin="$NEWFILE"
            ;;
    esac

    [ -n "$bin" ] || continue
    bin=$(abspath "$bin")
    if [ ! -x "$bin" ]; then
        echo "bench: $bin is not executable" >&2
        exit 1
    fi

    echo
    echo "$slot: $bin"
    time_case "template from a pipe" "$bin" case_pipe
    time_case "template, dense source" "$bin" case_tpl_dense
    time_case "template, sparse source" "$bin" case_tpl_sparse
    report_space "template, sparse source" "$WORK/out"
    time_case "template, 4K source, $MANY_N operands" "$bin" \
        case_tpl_many reset_tpl
    time_case "-s ${SIZE_MB}M" "$bin" case_fill
    time_case "-s ${SIZE_MB}M --sparse" "$bin" case_fill_sparse
    report_space "-s ${SIZE_MB}M --sparse" "$WORK/out"
    time_case "-f across $MANY_N operands" "$bin" case_many
    time_case "$MANY_N operands, no -f" "$bin" case_plain reset_plain
done

if ! command -v strace >/dev/null 2>&1; then
    exit 0
fi

target="${NEWFILE_NEW:-$NEWFILE}"
target=$(abspath "$target")

# strace wraps newfile alone, so the writer on the far side of the pipe
# is not counted with it.
echo
echo "syscall counts, one run each"

echo "  template from a pipe"
dd if="$WORK/src_dense" bs=65536 2>/dev/null |
    strace -c -o "$WORK/trace" \
        -e trace=read,write,copy_file_range \
        "$target" -f --template=- "$WORK/out" >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

echo "  template, 4K source, $MANY_N operands"
reset_tpl
xargs strace -c -o "$WORK/trace" \
    -e trace=openat,close,lseek,fstat,ftruncate,copy_file_range \
    "$target" --template="$WORK/src_small" < "$WORK/tpllist" \
    >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

echo "  -f across $MANY_N operands"
xargs strace -c -o "$WORK/trace" -e trace=fsync,openat,rename \
    "$target" -f -s 8 < "$WORK/manylist" >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

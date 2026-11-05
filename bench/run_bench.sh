#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# run_bench.sh - benchmarks, run from the top of the tree

set -u

NEWFILE="${NEWFILE:-./build/newfile}"
NEWFILE_OLD="${NEWFILE_OLD:-}"
NEWFILE_NEW="${NEWFILE_NEW:-}"
BENCH_SRCDIR="${BENCH_SRCDIR:-}"
REPS="${REPS:-5}"
SIZE_MB="${SIZE_MB:-256}"
MANY_N="${MANY_N:-500}"

if [ "$(date +%N)" = "%N" ]; then
    echo "bench: date(1) here has no nanosecond field" >&2
    exit 1
fi

WORK=$(mktemp -d "${BENCHDIR:-${TMPDIR:-/tmp}}/newfile-bench.XXXXXX") || exit 1
SRC="$WORK"
if [ -n "$BENCH_SRCDIR" ]; then
    SRC=$(mktemp -d "$BENCH_SRCDIR/newfile-src.XXXXXX") || exit 1
fi
# shellcheck disable=SC2329
cleanup() {
    rm -rf "$WORK"
    [ "$SRC" = "$WORK" ] || rm -rf "$SRC"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

abspath() {
    case "$1" in
        /*) echo "$1" ;;
        *) echo "$PWD/$1" ;;
    esac
}

time_case() {
    label="$1"
    bin="$2"
    fn="$3"
    reset="${4:-:}"
    : >"$WORK/times"
    i=0
    while [ "$i" -lt "$REPS" ]; do
        "$reset"
        sync
        sleep 0.3
        start=$(date +%s%N)
        if ! "$fn" "$bin"; then
            echo "bench: $label failed with $bin" >&2
            exit 1
        fi
        sync
        end=$(date +%s%N)
        echo $(( (end - start) / 1000000 )) >>"$WORK/times"
        i=$((i + 1))
    done
    printf '  %-36s %7s ms\n' "$label" \
        "$(sort -n "$WORK/times" |
           awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}')"
}

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
    dd if="$SRC/src_dense" bs=65536 2>/dev/null |
        "$1" -f --template=- "$WORK/out"
}

case_pipe_four() {
    dd if="$SRC/src_dense" bs=65536 2>/dev/null |
        "$1" -f --template=- "$WORK/out" "$WORK/out2" "$WORK/out3" \
            "$WORK/out4"
}

case_tpl_dense() {
    "$1" -f --template="$SRC/src_dense" "$WORK/out"
}

case_tpl_sparse() {
    "$1" -f --template="$SRC/src_sparse" "$WORK/out"
}

case_fill() {
    "$1" -f -s "${SIZE_MB}M" "$WORK/out"
}

case_fill_sparse() {
    "$1" -f -s "${SIZE_MB}M" --sparse "$WORK/out"
}

case_many() {
    xargs "$1" -f -s 8 <"$WORK/manylist"
}

case_plain() {
    xargs "$1" -s 8 <"$WORK/plainlist"
}

case_tpl_many() {
    xargs "$1" --template="$SRC/src_small" <"$WORK/tpllist"
}

make_source() {
    if ! dd if=/dev/urandom "$@" 2>/dev/null; then
        echo "bench: cannot write the source files in $SRC" >&2
        exit 1
    fi
}

make_source of="$SRC/src_dense" bs=1048576 count="$SIZE_MB"
make_source of="$SRC/src_sparse" bs=4096 count=1
make_source of="$SRC/src_sparse" bs=4096 count=1 \
    seek=$((SIZE_MB * 256 - 1)) conv=notrunc
make_source of="$SRC/src_small" bs=4096 count=1

make_list() {
    mkdir -p "$1"
    i=1
    while [ "$i" -le "$MANY_N" ]; do
        echo "$1/f$i"
        i=$((i + 1))
    done >"$2"
}

make_list "$WORK/many" "$WORK/manylist"
make_list "$WORK/plain" "$WORK/plainlist"
make_list "$WORK/tplmany" "$WORK/tpllist"

fstype=$(df -PT "$WORK" 2>/dev/null | awk 'NR == 2 {print $2}')
srcfs=$(df -PT "$SRC" 2>/dev/null | awk 'NR == 2 {print $2}')
echo "reps=$REPS size=${SIZE_MB}M operands=$MANY_N"
echo "dir=$WORK fs=${fstype:-unknown}"
echo "sources=$SRC fs=${srcfs:-unknown}"

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
    time_case "template from a pipe, 4 operands" "$bin" case_pipe_four
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

echo
echo "syscall counts, one run each"

echo "  template from a pipe, 4 operands"
dd if="$SRC/src_dense" bs=65536 2>/dev/null |
    strace -f -c -o "$WORK/trace" \
        -e trace=read,write,splice,copy_file_range,sendfile \
        "$target" -f --template=- "$WORK/out" "$WORK/out2" "$WORK/out3" \
        "$WORK/out4" >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

echo "  template, 4K source, $MANY_N operands"
reset_tpl
xargs strace -f -c -o "$WORK/trace" \
    -e trace=openat,close,linkat,fsync,copy_file_range,sendfile \
    "$target" --template="$SRC/src_small" <"$WORK/tpllist" \
    >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

echo "  -f across $MANY_N operands"
xargs strace -f -c -o "$WORK/trace" -e trace=fsync,openat,linkat,rename \
    "$target" -f -s 8 <"$WORK/manylist" >/dev/null 2>&1
sed -n '3,12p' "$WORK/trace" | sed 's/^/    /'

#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# size.sh - -s, its suffixes and limits, and --sparse

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin size
nf -s 4K sized
check_size "-s 4K" 4096 sized
check_eq "-s 4K allocates real blocks" yes \
    "$([ "$(blocks_kib sized)" -ge 4 ] && echo yes || echo no)"
nf -s 1M sized1m
check_size "-s 1M" 1048576 sized1m
nf -s 8M sized8m
check_size "-s 8M" 8388608 sized8m
check_eq "-s 8M allocates real blocks" yes \
    "$([ "$(blocks_kib sized8m)" -ge 8192 ] && echo yes || echo no)"
dd if=/dev/zero of=zero8m bs=1048576 count=8 2>/dev/null
check_same "-s 8M reads back as NUL" sized8m zero8m

begin edges
nf -s 0 size0
check_size "-s 0 creates zero-length file" 0 size0
nf -s 1G --sparse size1g
check_size "-s 1G --sparse creates 1 GiB file" 1073741824 size1g
nf -s 4k size4k
check_size "-s 4k lowercase accepted" 4096 size4k
check_exit "-s 10X invalid suffix exits 1" 1 "$NEWFILE" -s 10X bad1
check_exit "-s abc non-numeric exits 1" 1 "$NEWFILE" -s abc bad2
check_exit "-s too large exits 1" 1 "$NEWFILE" -s 9223372036854775808 bad3
check_exit "-s garbage exits 1" 1 "$NEWFILE" -s garbage bad4

begin sparse
nf -s 1M --sparse sized_sparse
check_size "-s --sparse nominal size" 1048576 sized_sparse
check_eq "-s --sparse uses fewer blocks than apparent size" yes \
    "$([ "$(blocks_kib sized_sparse)" -lt 512 ] && echo yes || echo no)"
nf --sparse sparse_warn
check_err "--sparse without --size prints warning" warning

tap_done

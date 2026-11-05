#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# owner.sh - -o with the caller's own user and groups, and bad owners

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin own
nf -o "$(id -u)" own_uid
check_owner "-o uid sets owner to current user" "$(id -u)" own_uid
nf -o "$(id -un):$(id -gn)" own_name
check_owner "-o user:group sets owner" "$(id -u)" own_name
nf -o ":$(id -gn)" own_group
check_group "-o :group sets group" "$(id -g)" own_group
nf -o ":$(id -g)" own_gidnum
check_group "-o :gid accepts a numeric group" "$(id -g)" own_gidnum
nf -o "$(id -un)" own_nocolon
check_owner "-o 'user' without a colon still works" "$(id -u)" own_nocolon

begin mode
nf -A -m 4755 -o "$(id -u)" own_suid
check_mode "-o with -m 4755 keeps setuid bit" 4755 own_suid
nf -A -m 0640 -o "$(id -un):$(id -gn)" own_mode
check_mode "-o user:group with -m 0640 applies mode" 640 own_mode
nf_pipe "printf 'seed\n'" --template=- -o "$(id -u)" own_tpl
check_content "-o with --template=- keeps content" seed own_tpl

begin bad
check_exit "-o invalid_user exits 1" 1 \
    "$NEWFILE" -o no_such_user_xyz_9999 own_bad
check_exit "-o ':' exits 1" 1 "$NEWFILE" -o : own_empty
check_exit "-o 'user:' exits 1" 1 "$NEWFILE" -o "$(id -un):" own_trailing
check_absent "-o 'user:' creates nothing" own_trailing
check_exit "-o negative numeric id exits 1" 1 "$NEWFILE" -o -1 own_negative
check_exit "-o overflowing numeric id exits 1" 1 \
    "$NEWFILE" -o 999999999999999999999999999999 own_huge
check_exit "-o 4294967295 exits 1" 1 "$NEWFILE" -o 4294967295 own_allones
check_exit "-o :4294967295 exits 1" 1 \
    "$NEWFILE" -o :4294967295 own_allones_g

tap_done

#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# options.sh - option parsing, its errors and operand validation

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin errors
check_exit "missing operand exits 1" 1 "$NEWFILE" -m 0644
check_exit "invalid mode exits 1" 1 "$NEWFILE" -m xyz bad
check_exit "mutually exclusive -m/--reference exits 1" 1 \
    "$NEWFILE" -m 0644 --reference=/dev/null bad
check_exit "invalid backup type exits 1" 1 "$NEWFILE" -f --backup=garbage bad

begin missing
nf -s
check_rc "-s without argument exits 1" 1
check_err "-s without argument names the problem" "requires an argument"
if grep -q '^usage:' "$T_ERR"; then
    pass "option error prints the usage synopsis"
else
    fail "option error prints the usage synopsis" "no line starts with usage:"
fi
nf --mode
check_rc "--mode without argument exits 1" 1
check_err "--mode without argument names the problem" "requires an argument"
nf
check_rc "no arguments exits 1" 1
check_err "no arguments reports missing operand" "missing operand"

begin strict
check_exit "-s +5 rejected" 1 "$NEWFILE" -s +5 sz_plus
check_exit "-s ' 5' rejected" 1 "$NEWFILE" -s ' 5' sz_ws
check_exit "-s -5 rejected" 1 "$NEWFILE" -s -5 sz_neg
check_exit "trailing comma in mode rejected" 1 "$NEWFILE" -m 'u=rw,' m_trail
check_exit "empty mode rejected" 1 "$NEWFILE" -m '' m_empty
check_exit "abbreviated long option rejected" 1 "$NEWFILE" --tem=/dev/null x
nf -z unknown
check_rc "unknown short option exits 1" 1
check_err "unknown short option names the problem" "unknown option"
nf --bogus unknown2
check_rc "unknown long option exits 1" 1
check_err "unknown long option names the problem" "unknown option"
nf --verbose=1 noarg
check_rc "argument to a no-argument long option exits 1" 1
check_err "no-argument long option names the problem" \
    "does not take an argument"

begin order
nf op_a -A -m 0600 op_b
check_mode "option after an operand still applies" 600 op_a
check_mode "operand before an option still created" 600 op_b
nf -A -m0600 op_attached
check_mode "attached short argument -m0600" 600 op_attached
nf --mode=0640 -A op_eq
check_mode "long option in --name=value form" 640 op_eq
nf -pfv op_bundle/x
check_file "bundled short options -pfv" op_bundle/x
mkdir dashdir
(cd dashdir && nf -- -dashed)
check_file "-- ends option parsing" dashdir/-dashed
mkdir mix
(cd mix && nf one -A -- -m two)
if [ -f mix/one ] && [ -f mix/-m ] && [ -f mix/two ]; then
    pass "operands after -- stay operands once options move"
else
    fail "operands after -- stay operands once options move" \
        "mix holds: $(find mix -mindepth 1 -printf '%f ')"
fi
nf op_e -m 0600 op_f -A -v op_g
check_eq "options between operands apply to every operand" "600 600 600" \
    "$(stat -c '%a' op_e op_f op_g 2>&1 | tr '\n' ' ' | sed 's/ $//')"

begin operands
nf eo_one "" eo_two
check_rc "empty operand exits 1" 1
check_file "empty operand does not stop later operands" eo_two
check_exit "trailing slash operand exits 1" 1 "$NEWFILE" ts_dir/
check_absent "trailing slash creates nothing" ts_dir

tap_done

#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# backup.sh - --backup: simple and numbered names, controls and refusals

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin basic
echo original >bk_file
nf -f -b bk_file
check_file "-f -b creates simple backup" bk_file~
check_content "backup has original content" original bk_file~
echo numbered1 >bk_num
nf -f --backup=numbered bk_num
check_file "numbered backup .~1~ created" bk_num.~1~
echo numbered2 >bk_num
nf -f --backup=numbered bk_num
check_file "numbered backup .~2~ created" bk_num.~2~
nf -f nobk_file
check_absent "-f without -b creates no backup" nobk_file~
check_exit "-b without -f exits 0" 0 "$NEWFILE" -b bk_warn_new
check_absent "-b without -f creates no backup file" bk_warn_new~
check_exit "--backup=garbage without -f exits 0" 0 \
    "$NEWFILE" --backup=garbage bk_nf
echo old >vb_file
nf -f -b -v vb_file
check_out "-f -b -v reports the backup" backup

begin repeated
printf 'v1' >rb_file
check_exit "-f -b first run" 0 "$NEWFILE" -f -b rb_file
printf 'v2' >rb_file
check_exit "-f -b second run" 0 "$NEWFILE" -f -b rb_file
check_content "the second backup holds what the second run replaced" v2 \
    rb_file~
check_size "the second run really replaced the file" 0 rb_file
for ctl in simple existing numbered; do
    printf 'v1' >"rb_$ctl"
    "$NEWFILE" -f --backup=$ctl "rb_$ctl" 2>"$T_ERR"
    printf 'v2' >"rb_$ctl"
    check_exit "--backup=$ctl twice" 0 "$NEWFILE" -f --backup=$ctl "rb_$ctl"
done
check_file "numbered still makes a second generation" rb_numbered.~2~

begin refused
mkdir rb_dir~
printf 'keep' >rb_dir
check_exit "-f -b with a directory in the way exits 1" 1 \
    "$NEWFILE" -f -b rb_dir
check_content "a directory in the way leaves the target untouched" keep rb_dir
check_no_temps "and no temporary file is left"
mkdir ln
name_max=$(getconf NAME_MAX ln)
LONG_NAME=$(printf "%0$((name_max - 3))d" 0 | tr 0 a)
printf 'keep' >"ln/$LONG_NAME"
check_content "a name 3 bytes short of NAME_MAX can be made" keep \
    "ln/$LONG_NAME"
check_exit "-f -b whose backup name is too long exits 1" 1 \
    "$NEWFILE" -f --backup=numbered -s 4 "ln/$LONG_NAME"
check_content "a backup name too long leaves the target untouched" keep \
    "ln/$LONG_NAME"
check_eq "and only the target is left" 1 \
    "$(find ln -type f | wc -l | tr -d ' ')"
mkdir bk_dir
check_exit "-f --backup on a directory exits 1" 1 \
    "$NEWFILE" -f --backup=simple bk_dir
check_dir "directory not renamed by backup" bk_dir
echo hello >bk_ltarget
ln -s bk_ltarget bk_link
check_exit "-f --backup on a symlink exits 1" 1 \
    "$NEWFILE" -f --backup=simple bk_link
check_symlink "symlink not renamed by backup" bk_link
check_content "symlink target untouched" hello bk_ltarget

begin numbering
printf 'v1' >sl_num
ln -s /nonexistent-newfile-target sl_num.~1~
nf -f --backup=numbered sl_num
check_symlink "a dangling symlink at .~1~ survives" sl_num.~1~
check_file "the backup moves to .~2~ instead" sl_num.~2~
printf 'v1' >sl_ex
ln -s /nonexistent-newfile-target sl_ex.~1~
nf -f --backup=existing sl_ex
check_file "existing picks numbered when .~1~ is a dangling symlink" sl_ex.~2~
printf 'v1' >sl_ok
printf 'other' >sl_target
ln -s sl_target sl_ok.~1~
nf -f --backup=numbered sl_ok
check_symlink "a resolvable symlink at .~1~ also survives" sl_ok.~1~
printf 'v1' >nb_file
"$NEWFILE" -f --backup=numbered nb_file 2>"$T_ERR"
"$NEWFILE" -f --backup=numbered nb_file 2>"$T_ERR"
rm -f nb_file.~1~
nf -f --backup=numbered nb_file
check_absent "a deleted backup number is not reused" nb_file.~1~
check_file "the next backup follows the highest" nb_file.~3~
printf 'v1' >eb_file
printf 'old' >eb_file.~2~
nf -f --backup=existing eb_file
check_file "existing counts any numbered backup, not only .~1~" eb_file.~3~
printf 'v1' >big_file
: >big_file.~18446744073709551615~
nf -f --backup=numbered big_file
check_file "a backup number past 2^64 still counts up" \
    big_file.~18446744073709551616~
printf 'v1' >hl_bk
ln hl_bk hl_bk~
nf -f --backup=simple hl_bk
check_content "a backup already linked to the file is kept" v1 hl_bk~
check_no_temps "and no temporary link is left behind"

begin unlistable
if need_nonroot; then
    mkdir so
    printf 'v1' >so/f
    printf 'v0' >so/f.~1~
    chmod 0311 so
    nf -f --backup=existing so/f
    chmod 0755 so
    check_file "existing finds .~1~ in a directory it cannot list" so/f.~2~
else
    skip_why "existing finds .~1~ in a directory it cannot list"
fi

begin controls
echo never_content >bk_never
nf -f --backup=never bk_never
check_file "--backup=never creates simple backup" bk_never~
echo none_content >bk_none
nf -f --backup=none bk_none
check_absent "--backup=none creates no backup" bk_none~
echo off_content >bk_off
nf -f --backup=off bk_off
check_absent "--backup=off creates no backup" bk_off~
echo t_content >bk_t
nf -f --backup=t bk_t
check_file "--backup=t creates numbered backup" bk_t.~1~
echo existing1 >bk_existing
nf -f --backup=existing bk_existing
check_file "--backup=existing (no .~1~) falls back to simple" bk_existing~
echo existing2 >bk_existing2
echo dummy >bk_existing2.~1~
nf -f --backup=existing bk_existing2
check_file "--backup=existing (with .~1~) uses numbered" bk_existing2.~2~
echo nil_content >bk_nil
nf -f --backup=nil bk_nil
check_file "--backup=nil falls back to simple" bk_nil~
echo vc_content >bk_vc
VERSION_CONTROL=numbered run "$NEWFILE" -f -b bk_vc
check_file "VERSION_CONTROL=numbered uses numbered backup" bk_vc.~1~
echo vc_off_content >bk_vc_off
VERSION_CONTROL=off run "$NEWFILE" -f -b bk_vc_off
check_absent "VERSION_CONTROL=off creates no backup" bk_vc_off~

tap_done

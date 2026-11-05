#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# force.sh - -f: atomic replacement and what it leaves when it fails

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin force
echo "old content" >forcefile
nf -f forcefile
check_size "-f replaces existing with an empty file" 0 forcefile
nf -f -A -m 0600 forcenew
check_mode "-f creates new with mode" 600 forcenew
printf 'old' >ov_mode
chmod 0666 ov_mode
nf -f -A -m 0600 ov_mode
check_mode "-f -m on existing applies mode without -o" 600 ov_mode
printf 'old' >ov_def
chmod 0600 ov_def
nf -f -A ov_def
check_mode "-f without -m normalizes to default a=rw" 666 ov_def
printf 'oldcontent' >ov_trunc
nf -f ov_trunc
check_size "-f with no content leaves an empty file" 0 ov_trunc

begin atomic
printf 'before' >at_ino
old_ino=$(stat -c '%i' at_ino)
nf -f -A -m 0644 at_ino
check_eq "-f gives the target a new inode" new \
    "$([ "$old_ino" = "$(stat -c '%i' at_ino)" ] && echo same || echo new)"
check_no_temps "-f leaves no temporary file"
mkdir bare
printf 'old' >bare/f
(cd bare && "$NEWFILE" -f -A -m 0644 f 2>"$T_ERR")
check_mode "-f replaces a bare operand name" 644 bare/f
check_no_temps "-f on a bare name leaves no temporary file"
printf 'precious' >at_keep
check_exit "-f with an unreadable template exits 1" 1 \
    "$NEWFILE" -f --template=no_such_template at_keep
check_content "a failed -f leaves the target intact" precious at_keep
check_no_temps "a failed -f leaves no temporary file"
printf 'v1' >at_bk
bk_ino=$(stat -c '%i' at_bk)
nf -f -b at_bk
check_eq "backup keeps the original inode" "$bk_ino" "$(stat -c '%i' at_bk~)"
check_content "backup keeps the original content" v1 at_bk~
printf 'linked' >at_hl
ln at_hl at_hl2
nf_pipe "printf fresh" -f --template=- at_hl
check_content "the replaced path has the new content" fresh at_hl
check_content "another hard link keeps the old content" linked at_hl2
printf 'target' >at_tgt
ln -s at_tgt at_lnk
nf -f -A -m 0644 at_lnk
check_eq "-f replaces the symlink, not its target" no \
    "$([ -L at_lnk ] && echo yes || echo no)"
check_content "-f leaves the old symlink target alone" target at_tgt

begin dir
mkdir at_dir
check_exit "-f on a directory exits 1" 1 "$NEWFILE" -f at_dir
check_dir "-f leaves the directory in place" at_dir
check_no_temps "-f on a directory leaves no temporary file"

begin write_failure
"$NEWFILE" -s 2M wf_src 2>"$T_ERR"
printf 'PRECIOUS' >wf_keep
run sh -c "ulimit -f 100 && '$NEWFILE' -f --template=wf_src wf_keep; true"
check_content "a failed write leaves the target byte-identical" PRECIOUS \
    wf_keep
check_no_temps "a failed write leaves no temporary file"

begin write_only
if need_nonroot; then
    mkdir wx
    printf 'old' >wx/f
    chmod 0300 wx
    check_exit "-f into a write-only directory exits 0" 0 \
        "$NEWFILE" -f -A -m 0644 wx/f
    check_exit "-f -b into a write-only directory exits 0" 0 \
        "$NEWFILE" -f -b wx/f
    chmod 0755 wx
    check_mode "-f into a write-only directory still applied the mode" 644 \
        wx/f
    check_file "-f -b into a write-only directory made the backup" wx/f~
else
    for d in "-f into a write-only directory exits 0" \
             "-f -b into a write-only directory exits 0" \
             "-f into a write-only directory still applied the mode" \
             "-f -b into a write-only directory made the backup"; do
        skip_why "$d"
    done
fi

begin tmpdir
mkdir td_tmp
printf 'old' >td_file
TMPDIR="$CASE/td_tmp" run "$NEWFILE" -f -A -m 0644 td_file
check_rc "-f succeeds with TMPDIR set elsewhere" 0
check_mode "-f applied the mode" 644 td_file
check_eq "-f left nothing in TMPDIR" 0 \
    "$(find td_tmp -type f | wc -l | tr -d ' ')"
xdev=
for alt in /dev/shm /var/tmp; do
    if [ -d "$alt" ] && [ -w "$alt" ] \
       && [ "$(stat -c '%d' "$alt")" != "$(stat -c '%d' .)" ]; then
        xdev=$alt
        break
    fi
done
if [ -n "$xdev" ]; then
    printf 'old' >td_xdev
    TMPDIR="$xdev" run "$NEWFILE" -f -A -m 0600 td_xdev
    check_mode "-f works with TMPDIR on another filesystem" 600 td_xdev
else
    skip "-f works with TMPDIR on another filesystem" \
        "no writable /dev/shm or /var/tmp on another filesystem"
fi

tap_done

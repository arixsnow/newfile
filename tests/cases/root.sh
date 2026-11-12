#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# root.sh - setgid directories, other owners, and callers without CAP_FOWNER

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

OTHER_UID=61000
DIR_GID=61001
CALLER_GID=61002

need_root || skip_all "$T_WHY"
need_cmd setpriv || skip_all "$T_WHY"

chmod 0755 "$T_ROOT"
printf 'seed\n' >"$T_ROOT/tpl"
chmod 0644 "$T_ROOT/tpl"

# shellcheck disable=SC2329
as_other() {
    setpriv --reuid="$OTHER_UID" --regid="$CALLER_GID" \
        --groups="$CALLER_GID" -- "$@"
}

# shellcheck disable=SC2329
without_fowner() {
    setpriv --inh-caps=-all,+chown,+dac_override \
        --bounding-set=-all,+chown,+dac_override -- "$@"
}

begin setgid
chown "0:$DIR_GID" .
chmod 2777 .
umask 002
run as_other "$NEWFILE" -o ":$CALLER_GID" -m 2775 f
check_mode "-o :group keeps the setgid bit the kernel stripped" 2775 f
check_group "and the file has that group" "$CALLER_GID" f

begin other
umask 022
nf -o "$OTHER_UID" -m 4755 --template="$T_ROOT/tpl" f
check_rc "-o another user exits 0" 0
check_eq "-o another user gives the file to that user" "$OTHER_UID 4755" \
    "$(stat -c '%u %a' f 2>&1)"
check_no_temps "-o another user leaves no temporary file"

begin no_fowner
run without_fowner "$NEWFILE" -o "$OTHER_UID" -m 4755 \
    --template="$T_ROOT/tpl" f
if [ "$RC" = 0 ] && [ "$(stat -c '%u %a' f)" = "$OTHER_UID 4755" ]; then
    result=coherent
elif [ "$RC" = 1 ] && [ ! -e f ] \
     && grep -q "cannot set permissions of 'f'" "$T_ERR"; then
    result=coherent
else
    result="exit $RC"
fi
check_eq "without CAP_FOWNER the file is made whole or not at all" coherent \
    "$result"
check_no_temps "without CAP_FOWNER no temporary file is left"

begin fuse
FUSE="without CAP_FOWNER a FUSE server's file is made
the FUSE server's file belongs to that user
the FUSE mount holds no temporary file"
FUSE_DIR=$CASE/pt
FUSE_PID=
FUSE_WHY=

fuse_all() {
    while read -r d; do
        "$1" "$d" "$2"
    done <<EOF
$FUSE
EOF
}

# passthrough_ll tells files apart by inode number alone, so it serves a
# tmpfs of its own. Over bindfs on ext4, a removed file's number can come
# back while the server still holds the old file.
fuse_up() {
    if ! { mkdir "$FUSE_DIR" &&
            mount -t tmpfs -o size=1m tmpfs "$FUSE_DIR"; } 2>"$T_ERR"; then
        FUSE_WHY="no tmpfs for passthrough_ll: $(cat "$T_ERR")"
        return 1
    fi
    T_UNDO=fuse_down
    mkdir "$FUSE_DIR/src" "$FUSE_DIR/mnt"
    passthrough_ll -f -o source="$FUSE_DIR/src" "$FUSE_DIR/mnt" \
        >"$CASE/server.err" 2>&1 &
    FUSE_PID=$!
    i=0
    while ! findmnt -n --mountpoint "$FUSE_DIR/mnt" >/dev/null; do
        if ! kill -0 "$FUSE_PID" 2>/dev/null; then
            FUSE_WHY="passthrough_ll did not mount: $(cat "$CASE/server.err")"
            return 1
        fi
        if [ "$i" -ge 100 ]; then
            FUSE_WHY="passthrough_ll did not mount within 10 seconds"
            return 1
        fi
        sleep 0.1
        i=$((i + 1))
    done
}

fuse_down() {
    if [ -n "$FUSE_PID" ]; then
        umount "$FUSE_DIR/mnt" 2>/dev/null || kill "$FUSE_PID" 2>/dev/null
        # The tmpfs stays busy until the server has exited.
        wait "$FUSE_PID"
        FUSE_PID=
    fi
    umount "$FUSE_DIR"
    T_UNDO=
}

if ! need_cmd passthrough_ll; then
    fuse_all skip "$T_WHY"
elif [ "$(cat /proc/sys/fs/protected_hardlinks)" != 1 ]; then
    fuse_all skip "fs.protected_hardlinks is off"
elif ! fuse_up; then
    fuse_all fail "$FUSE_WHY"
else
    run without_fowner "$NEWFILE" -o "$OTHER_UID" -m 4755 \
        --template="$T_ROOT/tpl" "$FUSE_DIR/mnt/f"
    check_rc "without CAP_FOWNER a FUSE server's file is made" 0
    check_eq "the FUSE server's file belongs to that user" \
        "$OTHER_UID 4755" "$(stat -c '%u %a' pt/mnt/f 2>&1)"
    check_no_temps "the FUSE mount holds no temporary file" pt/mnt
    fuse_down
fi

tap_done

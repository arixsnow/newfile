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
fuse_skip() {
    while read -r d; do
        skip "$d" "$1"
    done <<EOF
$FUSE
EOF
}
if ! need_cmd passthrough_ll; then
    fuse_skip "$T_WHY"
elif [ "$(cat /proc/sys/fs/protected_hardlinks)" != 1 ]; then
    fuse_skip "fs.protected_hardlinks is off"
else
    mkdir back mnt
    if passthrough_ll -o source="$CASE/back" "$CASE/mnt" 2>"$T_ERR"; then
        T_UMOUNT=$CASE/mnt
        run without_fowner "$NEWFILE" -o "$OTHER_UID" -m 4755 \
            --template="$T_ROOT/tpl" "$CASE/mnt/f"
        check_rc "without CAP_FOWNER a FUSE server's file is made" 0
        check_eq "the FUSE server's file belongs to that user" \
            "$OTHER_UID 4755" "$(stat -c '%u %a' mnt/f 2>&1)"
        check_no_temps "the FUSE mount holds no temporary file" mnt
        umount "$CASE/mnt"
        T_UMOUNT=
    else
        while read -r d; do
            fail "$d" "passthrough_ll did not mount: $(cat "$T_ERR")"
        done <<EOF
$FUSE
EOF
    fi
fi

tap_done

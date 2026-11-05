#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# mode.sh - octal, symbolic and exact modes, --reference and default ACLs

# shellcheck source=tests/lib.sh
. "$(dirname -- "$0")/../lib.sh"

begin basic
nf default
check_file "default file exists" default

begin octal
for m in 755 644 600; do
    nf -A -m "0$m" "octal$m"
    check_mode "-m 0$m" "$m" "octal$m"
done
nf -A -m 0777 abs777
check_mode "-A -m 0777 bypasses umask" 777 abs777

begin symbolic
nf -A -m u=rwx,g=rx,o=r sym754
check_mode "u=rwx,g=rx,o=r" 754 sym754
nf -A -m a=rw sym666
check_mode "a=rw" 666 sym666
nf -A -m u=rwx sym700
check_mode "u=rwx" 700 sym700
nf -A -m +x symx
check_mode "+x" 111 symx
nf -A -m a=rwx,o-w sym_minus
check_mode "a=rwx,o-w" 775 sym_minus
nf -A -m a=rw,u+x sym_plus
check_mode "a=rw,u+x" 766 sym_plus
nf -A -m u=rwx,g=rx,u+s sym_suid
check_mode "u+s sets the set-user-ID bit" 4750 sym_suid
nf -A -m a=rwx,+t sym_sticky
check_mode "+t sets the sticky bit" 1777 sym_sticky
nf -A -m a+r mode_ar
check_mode "-m a+r sets all-read" 444 mode_ar
nf -A -m "u=rw,g=r,o=r" mode_644sym
check_mode "-m u=rw,g=r,o=r gives 644" 644 mode_644sym

begin invalid
check_exit "-m 0888 invalid octal exits 1" 1 "$NEWFILE" -m 0888 bad1
check_exit "-m u=q invalid perm char exits 1" 1 "$NEWFILE" -m u=q bad2
check_exit "-m x=rwx invalid who-char exits 1" 1 "$NEWFILE" -m x=rwx bad3
check_exit "-m garbage exits 1" 1 "$NEWFILE" -m garbage bad4

begin reference
nf -A -m 0755 ref
nf --reference=ref -A refcopy
check_mode "--reference copies mode" 755 refcopy
check_exit "--reference nonexistent exits 1" 1 \
    "$NEWFILE" --reference=no_such_ref ref_out

begin scope
(umask 022 && "$NEWFILE" -p -A -m 0600 ascope/a/b/f 2>"$T_ERR")
check_mode "-p -A parent dir subject to umask" 755 ascope/a
check_mode "-p -A nested parent subject to umask" 755 ascope/a/b
check_mode "-p -A file mode exact" 600 ascope/a/b/f

begin setuid
printf 'hello from template\n' >tpl
nf -A -m 4755 --template=tpl suid_tpl
check_mode "-m 4755 with a template keeps setuid" 4755 suid_tpl
nf -A -m 4755 -s 4K suid_size
check_mode "-m 4755 with -s keeps setuid" 4755 suid_size

begin acl
printf 'hello from template\n' >"$T_ROOT/acl.tpl"
if need_cmd setfacl && setfacl -d -m u::rw,g::rw,o::r . 2>/dev/null; then
    printf 'old' >force
    (umask 077
     : >shell
     "$NEWFILE" plain
     "$NEWFILE" -f force
     "$NEWFILE" -o "$(id -u)" owner
     "$NEWFILE" --template="$T_ROOT/acl.tpl" tpl) 2>"$T_ERR"
    shell=$(stat -c '%a' shell)
    if [ "$shell" != 664 ]; then
        skip "a default ACL wins over the umask" \
            "the filesystem ignores default ACLs"
    fi
    for f in plain force owner tpl; do
        check_mode "$f gets the mode the shell's create does" "$shell" "$f"
    done
else
    if command -v setfacl >/dev/null 2>&1; then
        T_WHY="no default ACLs on this filesystem"
    fi
    for f in plain force owner tpl; do
        skip_why "$f gets the mode the shell's create does"
    done
fi

tap_done

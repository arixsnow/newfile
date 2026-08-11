#!/bin/sh
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# Test suite for newfile.  Run from the top of the source tree, with
# 'make check' or directly as 'sh tests/run_tests.sh'.
NEWFILE="${NEWFILE:-./newfile}"

# Absolute: tests run the binary from another directory.
case "$NEWFILE" in
    /*) ;;
    *) NEWFILE="$PWD/$NEWFILE" ;;
esac

PASS=0
FAIL=0
TESTDIR=$(mktemp -d)

# shellcheck disable=SC2329
cleanup() {
    rm -rf "$TESTDIR"
}
trap cleanup EXIT

assert_eq() {
    desc="$1"
    expected="$2"
    actual="$3"
    if [ "$expected" = "$actual" ]; then
        PASS=$((PASS + 1))
        echo "  PASS: $desc"
    else
        FAIL=$((FAIL + 1))
        echo "  FAIL: $desc (expected '$expected', got '$actual')"
    fi
}

assert_exit() {
    desc="$1"
    expected="$2"
    shift 2
    "$@" >/dev/null 2>&1
    actual=$?
    assert_eq "$desc" "$expected" "$actual"
}

# stat format flags differ between implementations.
if stat -c '%s' . >/dev/null 2>&1; then
    get_perms() { stat -c '%a' "$1" 2>/dev/null; }
    get_size() { stat -c '%s' "$1" 2>/dev/null; }
    get_owner() { stat -c '%u' "$1" 2>/dev/null; }
    get_group() { stat -c '%g' "$1" 2>/dev/null; }
    get_inode() { stat -c '%i' "$1" 2>/dev/null; }
    get_dev() { stat -c '%d' "$1" 2>/dev/null; }
else
    get_perms() { stat -f '%Lp' "$1" 2>/dev/null; }
    get_size() { stat -f '%z' "$1" 2>/dev/null; }
    get_owner() { stat -f '%u' "$1" 2>/dev/null; }
    get_group() { stat -f '%g' "$1" 2>/dev/null; }
    get_inode() { stat -f '%i' "$1" 2>/dev/null; }
    get_dev() { stat -f '%d' "$1" 2>/dev/null; }
fi

# Temporary files newfile has failed to clean up.
count_temps() { find "$TESTDIR" -name '.newfile.*' | wc -l | tr -d ' '; }

echo "=== newfile test suite ==="

# --help and --version
echo "--- help/version ---"
assert_exit "--help exits 0" 0 "$NEWFILE" --help

# The help body is generated from the option table, so these check the
# renderer and the table rather than a fixed layout.
HELP=$("$NEWFILE" --help 2>&1)

result=$(echo "$HELP" | awk '{ if (length > 80) n++ } END { print n + 0 }')
assert_eq "no --help line is wider than 80 columns" "0" "$result"

missing=
for o in help version mode absolute parents owner reference size force \
         template backup sparse verbose; do
    echo "$HELP" | grep -q -- "--$o" || missing="$missing --$o"
done
for o in m A p o s f b v; do
    echo "$HELP" | grep -q -- "  -$o" || missing="$missing -$o"
done
assert_eq "every option is described in --help" "" "$missing"

result=$(echo "$HELP" | grep -cE '^  -|^      --')
assert_eq "--help lists every option in the table once" "14" "$result"

# A word with nowhere left to go still overruns, so only lines that
# could have been broken are checked.
result=$(COLUMNS=50 "$NEWFILE" --help 2>&1 \
    | awk '{ if (length > 50 && $2 != "") n++ } END { print n + 0 }')
assert_eq "COLUMNS narrows the help output" "0" "$result"

# A sign or leading space is not a width, matching how -s is parsed.
for bad in abc 5 99999 " 60" "+60" "-60" "0x50"; do
    result=$(COLUMNS="$bad" "$NEWFILE" --help 2>&1 \
        | awk '{ if (length > 80) n++ } END { print n + 0 }')
    assert_eq "COLUMNS='$bad' falls back to the default width" "0" "$result"
done

# The synopsis groups are sorted by the generator, not by table order.
SYN=$("$NEWFILE" --nosuch-option 2>&1 | sed -n '2,3p' | tr -d '\n')
result=$(echo "$SYN" | sed -n 's/.*\[-\([A-Za-z]*\)\].*/\1/p' \
    | grep -o . | sort -c 2>/dev/null && echo sorted || echo unsorted)
assert_eq "the synopsis option cluster is sorted" "sorted" "$result"

# The continuation hangs under the name the program was invoked as.
OUT=$("$NEWFILE" --nosuch-option 2>&1)
NAME=$(echo "$OUT" | sed -n '1s/:.*//p')
result=$(echo "$OUT" | sed -n '3p' | awk '{ print match($0, /[^ ]/) - 1 }')
assert_eq "the synopsis continuation aligns under the program name" \
    "$((7 + ${#NAME} + 1))" "$result"
assert_exit "--version exits 0" 0 "$NEWFILE" --version

VER=$("$NEWFILE" --version 2>&1)
result=$(echo "$VER" | grep -c 'License: BSD-3-Clause\.')
assert_eq "--version names the license by its SPDX identifier" "1" "$result"

# Nothing may point at a file the program does not ship.
result=$(echo "$VER" | grep -ci 'see the\|LICENSE file\|see the source')
assert_eq "--version points at no file it cannot guarantee" "0" "$result"

result=$(echo "$VER" | awk '{ if (length > 80) n++ } END { print n + 0 }')
assert_eq "no --version line is wider than 80 columns" "0" "$result"

# Missing operand
echo "--- error handling ---"
assert_exit "missing operand exits 1" 1 "$NEWFILE" -m 0644
assert_exit "invalid mode exits 1" 1 "$NEWFILE" -m xyz "$TESTDIR/bad"
assert_exit "mutually exclusive -m/--reference exits 1" 1 \
    "$NEWFILE" -m 0644 --reference=/dev/null "$TESTDIR/bad"
assert_exit "invalid backup type exits 1" 1 \
    "$NEWFILE" -f --backup=garbage "$TESTDIR/bad"

# Basic creation with default mode
echo "--- basic creation ---"
"$NEWFILE" "$TESTDIR/default" 2>/dev/null
if [ -f "$TESTDIR/default" ]; then result="yes"; else result="no"; fi
assert_eq "default file exists" "yes" "$result"

# Octal mode
echo "--- octal mode ---"
"$NEWFILE" -A -m 0755 "$TESTDIR/octal755" 2>/dev/null
assert_eq "-m 0755" "755" "$(get_perms "$TESTDIR/octal755")"

"$NEWFILE" -A -m 0644 "$TESTDIR/octal644" 2>/dev/null
assert_eq "-m 0644" "644" "$(get_perms "$TESTDIR/octal644")"

"$NEWFILE" -A -m 0600 "$TESTDIR/octal600" 2>/dev/null
assert_eq "-m 0600" "600" "$(get_perms "$TESTDIR/octal600")"

# Symbolic mode
echo "--- symbolic mode ---"
"$NEWFILE" -A -m u=rwx,g=rx,o=r "$TESTDIR/sym754" 2>/dev/null
assert_eq "u=rwx,g=rx,o=r" "754" "$(get_perms "$TESTDIR/sym754")"

"$NEWFILE" -A -m a=rw "$TESTDIR/sym666" 2>/dev/null
assert_eq "a=rw" "666" "$(get_perms "$TESTDIR/sym666")"

"$NEWFILE" -A -m u=rwx "$TESTDIR/sym700" 2>/dev/null
assert_eq "u=rwx" "700" "$(get_perms "$TESTDIR/sym700")"

"$NEWFILE" -A -m +x "$TESTDIR/symx" 2>/dev/null
assert_eq "+x" "111" "$(get_perms "$TESTDIR/symx")"

"$NEWFILE" -A -m a=rwx,o-w "$TESTDIR/sym_minus" 2>/dev/null
assert_eq "a=rwx,o-w" "775" "$(get_perms "$TESTDIR/sym_minus")"

"$NEWFILE" -A -m a=rw,u+x "$TESTDIR/sym_plus" 2>/dev/null
assert_eq "a=rw,u+x" "766" "$(get_perms "$TESTDIR/sym_plus")"

"$NEWFILE" -A -m u=rwx,g=rx,u+s "$TESTDIR/sym_suid" 2>/dev/null
assert_eq "u+s sets the set-user-ID bit" "4750" \
    "$(get_perms "$TESTDIR/sym_suid")"

"$NEWFILE" -A -m a=rwx,+t "$TESTDIR/sym_sticky" 2>/dev/null
assert_eq "+t sets the sticky bit" "1777" \
    "$(get_perms "$TESTDIR/sym_sticky")"

# Absolute mode (bypass umask)
echo "--- absolute mode ---"
"$NEWFILE" -A -m 0777 "$TESTDIR/abs777" 2>/dev/null
assert_eq "-A -m 0777 bypasses umask" "777" "$(get_perms "$TESTDIR/abs777")"

# Fail on existing file (O_EXCL)
echo "--- O_EXCL safety ---"
assert_exit "fails on existing file" 1 "$NEWFILE" "$TESTDIR/default"

# Force overwrite
echo "--- force mode ---"
echo "old content" > "$TESTDIR/forcefile"
"$NEWFILE" -f "$TESTDIR/forcefile" 2>/dev/null
assert_eq "-f replaces existing with an empty file" "0" \
    "$(get_size "$TESTDIR/forcefile")"
rm -f "$TESTDIR/forcenew"
"$NEWFILE" -f -A -m 0600 "$TESTDIR/forcenew" 2>/dev/null
assert_eq "-f creates new with mode" "600" "$(get_perms "$TESTDIR/forcenew")"

# Parents
echo "--- parents ---"
"$NEWFILE" -p -A -m 0755 "$TESTDIR/a/b/c/nested.txt" 2>/dev/null
if [ -f "$TESTDIR/a/b/c/nested.txt" ]; then result="yes"; else result="no"; fi
assert_eq "-p creates nested file" "yes" "$result"
assert_eq "-p nested perms" "755" "$(get_perms "$TESTDIR/a/b/c/nested.txt")"

# Size
echo "--- size ---"
"$NEWFILE" -s 4K "$TESTDIR/sized" 2>/dev/null
assert_eq "-s 4K" "4096" "$(get_size "$TESTDIR/sized")"
blocks=$(du -k "$TESTDIR/sized" | cut -f1)
if [ "$blocks" -ge 4 ]; then result="yes"; else result="no"; fi
assert_eq "-s 4K allocates real blocks" "yes" "$result"

"$NEWFILE" -s 1M "$TESTDIR/sized1m" 2>/dev/null
assert_eq "-s 1M" "1048576" "$(get_size "$TESTDIR/sized1m")"

# Large enough that reserving the space and writing it out would differ
# if the blocks were not really being claimed.
"$NEWFILE" -s 8M "$TESTDIR/sized8m" 2>/dev/null
assert_eq "-s 8M" "8388608" "$(get_size "$TESTDIR/sized8m")"
blocks=$(du -k "$TESTDIR/sized8m" | cut -f1)
if [ "$blocks" -ge 8192 ]; then result="yes"; else result="no"; fi
assert_eq "-s 8M allocates real blocks" "yes" "$result"
dd if=/dev/zero of="$TESTDIR/zero8m" bs=1048576 count=8 2>/dev/null
if cmp -s "$TESTDIR/sized8m" "$TESTDIR/zero8m"; then
    result="yes"
else
    result="no"
fi
assert_eq "-s 8M reads back as NUL" "yes" "$result"

# Reference
echo "--- reference ---"
"$NEWFILE" --reference="$TESTDIR/octal755" -A "$TESTDIR/refcopy" 2>/dev/null
assert_eq "--reference copies mode" "755" "$(get_perms "$TESTDIR/refcopy")"

# Verbose
echo "--- verbose ---"
OUT=$("$NEWFILE" -v -m 0644 "$TESTDIR/verbfile" 2>&1)
if echo "$OUT" | grep -q "created"; then result="yes"; else result="no"; fi
assert_eq "-v prints created" "yes" "$result"

# Reporting a success and a failure in one run.  Command substitution
# gives the program a pipe, where the two streams buffer differently, so
# the order only holds if the reports are flushed as they are made.
: > "$TESTDIR/vord_taken"
OUT=$("$NEWFILE" -v "$TESTDIR/vord_new" "$TESTDIR/vord_taken" 2>&1)
result=$(echo "$OUT" | sed -n '1p' | grep -c "created")
assert_eq "-v reports the success before the later failure" "1" "$result"

# Multiple files
echo "--- multiple files ---"
"$NEWFILE" -A -m 0644 "$TESTDIR/multi1" "$TESTDIR/multi2" "$TESTDIR/multi3" 2>/dev/null
for f in multi1 multi2 multi3; do
    if [ -f "$TESTDIR/$f" ]; then result="yes"; else result="no"; fi
    assert_eq "$f exists" "yes" "$result"
done

# Partial failure (one file fails, others succeed)
echo "--- partial failure ---"
"$NEWFILE" -A -m 0644 "$TESTDIR/multi1" "$TESTDIR/newfail" 2>/dev/null
RC=$?
assert_eq "partial failure exits 1" "1" "$RC"
if [ -f "$TESTDIR/newfail" ]; then result="yes"; else result="no"; fi
assert_eq "new file created despite partial failure" "yes" "$result"

# Path handling.  The path goes to the kernel as typed, so ".." follows
# a symbolic link instead of being cancelled against the name in front
# of it.
echo "--- path handling ---"
"$NEWFILE" -p -A -m 0644 "$TESTDIR/ps/./skip/file" 2>/dev/null
if [ -f "$TESTDIR/ps/skip/file" ]; then result="yes"; else result="no"; fi
assert_eq "dot component ignored" "yes" "$result"

"$NEWFILE" -p -A -m 0644 "$TESTDIR/ps/dslash//file" 2>/dev/null
if [ -f "$TESTDIR/ps/dslash/file" ]; then result="yes"; else result="no"; fi
assert_eq "double slash ignored" "yes" "$result"

mkdir -p "$TESTDIR/ps/real"
ln -s real "$TESTDIR/ps/sym"
"$NEWFILE" "$TESTDIR/ps/sym/../vialink" 2>/dev/null
if [ -f "$TESTDIR/ps/vialink" ]; then result="yes"; else result="no"; fi
assert_eq ".. through a symlink follows the link" "yes" "$result"

touch "$TESTDIR/ps/sym/../viatouch"
if [ -f "$TESTDIR/ps/viatouch" ]; then result="yes"; else result="no"; fi
assert_eq ".. placement agrees with touch" "yes" "$result"

LONG_PART=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
"$NEWFILE" -p -A -m 0644 "$TESTDIR/ps/long/$LONG_PART/$LONG_PART/$LONG_PART/file" 2>/dev/null
if [ -f "$TESTDIR/ps/long/$LONG_PART/$LONG_PART/$LONG_PART/file" ]; then result="yes"; else result="no"; fi
assert_eq "long dynamic path succeeds" "yes" "$result"

DEEP_PATH="$TESTDIR/ps/deep"
i=0
while [ "$i" -lt 18 ]; do
    DEEP_PATH="$DEEP_PATH/$LONG_PART"
    i=$((i + 1))
done
"$NEWFILE" -p -A -m 0644 "$DEEP_PATH/file" 2>/dev/null
if [ -f "$DEEP_PATH/file" ]; then result="yes"; else result="no"; fi
assert_eq "deep dynamic path succeeds" "yes" "$result"

# A path that would only fit after collapsing redundant components is
# the kernel's to reject.
REDUNDANT_LONG_PATH="$TESTDIR/ps/overlong"
i=0
while [ "$i" -lt 2200 ]; do
    REDUNDANT_LONG_PATH="$REDUNDANT_LONG_PATH/."
    i=$((i + 1))
done
REDUNDANT_LONG_PATH="$REDUNDANT_LONG_PATH/file"
mkdir -p "$TESTDIR/ps/overlong"
assert_exit "path beyond the system limit rejected" 1 \
    "$NEWFILE" -p "$REDUNDANT_LONG_PATH"

assert_exit "empty path exits 1" 1 "$NEWFILE" ""

# Template
echo "--- template ---"
echo "hello from template" > "$TESTDIR/tpl_src"
"$NEWFILE" --template="$TESTDIR/tpl_src" -A -m 0600 "$TESTDIR/tpl_out" 2>/dev/null
assert_eq "template content copied" "hello from template" "$(cat "$TESTDIR/tpl_out")"
assert_eq "template mode applied" "600" "$(get_perms "$TESTDIR/tpl_out")"
assert_exit "template + size mutually exclusive" 1 \
    "$NEWFILE" --template="$TESTDIR/tpl_src" -s 1K "$TESTDIR/tpl_bad"
assert_exit "template nonexistent exits 1" 1 \
    "$NEWFILE" --template="$TESTDIR/no_such_file" "$TESTDIR/tpl_bad2"

# Backup
echo "--- backup ---"
echo "original" > "$TESTDIR/bk_file"
"$NEWFILE" -f -b "$TESTDIR/bk_file" 2>/dev/null
if [ -f "$TESTDIR/bk_file~" ]; then result="yes"; else result="no"; fi
assert_eq "-f -b creates simple backup" "yes" "$result"
assert_eq "backup has original content" "original" "$(cat "$TESTDIR/bk_file~")"

echo "numbered1" > "$TESTDIR/bk_num"
"$NEWFILE" -f --backup=numbered "$TESTDIR/bk_num" 2>/dev/null
if [ -f "$TESTDIR/bk_num.~1~" ]; then result="yes"; else result="no"; fi
assert_eq "numbered backup .~1~ created" "yes" "$result"

echo "numbered2" > "$TESTDIR/bk_num"
"$NEWFILE" -f --backup=numbered "$TESTDIR/bk_num" 2>/dev/null
if [ -f "$TESTDIR/bk_num.~2~" ]; then result="yes"; else result="no"; fi
assert_eq "numbered backup .~2~ created" "yes" "$result"

rm -f "$TESTDIR/nobk_file"
"$NEWFILE" -f "$TESTDIR/nobk_file" 2>/dev/null
if [ -f "$TESTDIR/nobk_file~" ]; then result="yes"; else result="no"; fi
assert_eq "-f without -b creates no backup" "no" "$result"

# Backing up twice.  The backup name is already taken on the second run,
# and link refuses that where rename would have replaced it.
echo "--- repeated backups ---"
printf 'v1' > "$TESTDIR/rb_file"
assert_exit "-f -b first run" 0 "$NEWFILE" -f -b "$TESTDIR/rb_file"
printf 'v2' > "$TESTDIR/rb_file"
assert_exit "-f -b second run" 0 "$NEWFILE" -f -b "$TESTDIR/rb_file"
assert_eq "the second backup holds what the second run replaced" "v2" \
    "$(cat "$TESTDIR/rb_file~")"
assert_eq "the second run really replaced the file" "0" \
    "$(get_size "$TESTDIR/rb_file")"

for ctl in simple existing numbered; do
    rm -f "$TESTDIR/rb_$ctl" "$TESTDIR/rb_$ctl~" "$TESTDIR/rb_$ctl".~*~
    printf 'v1' > "$TESTDIR/rb_$ctl"
    "$NEWFILE" -f --backup=$ctl "$TESTDIR/rb_$ctl" 2>/dev/null
    printf 'v2' > "$TESTDIR/rb_$ctl"
    assert_exit "--backup=$ctl twice" 0 \
        "$NEWFILE" -f --backup=$ctl "$TESTDIR/rb_$ctl"
done
assert_eq "numbered still makes a second generation" "yes" \
    "$(test -f "$TESTDIR/rb_numbered.~2~" && echo yes || echo no)"

# A directory occupying the backup name is still refused
mkdir "$TESTDIR/rb_dir~"
printf 'keep' > "$TESTDIR/rb_dir"
assert_exit "-f -b with a directory in the way exits 1" 1 \
    "$NEWFILE" -f -b "$TESTDIR/rb_dir"
assert_eq "and the target is untouched" "keep" "$(cat "$TESTDIR/rb_dir")"

# A dangling symlink is invisible to stat.  The numbering probe uses
# lstat.  Without it the symlink would be picked as a free name and then
# destroyed when the backup replaces it.
echo "--- symlink occupying a backup name ---"
printf 'v1' > "$TESTDIR/sl_num"
ln -s /nonexistent-newfile-target "$TESTDIR/sl_num.~1~"
"$NEWFILE" -f --backup=numbered "$TESTDIR/sl_num" 2>/dev/null
if [ -L "$TESTDIR/sl_num.~1~" ]; then result="yes"; else result="no"; fi
assert_eq "a dangling symlink at .~1~ survives" "yes" "$result"
if [ -f "$TESTDIR/sl_num.~2~" ]; then result="yes"; else result="no"; fi
assert_eq "the backup moves to .~2~ instead" "yes" "$result"

printf 'v1' > "$TESTDIR/sl_ex"
ln -s /nonexistent-newfile-target "$TESTDIR/sl_ex.~1~"
"$NEWFILE" -f --backup=existing "$TESTDIR/sl_ex" 2>/dev/null
if [ -f "$TESTDIR/sl_ex.~2~" ]; then result="yes"; else result="no"; fi
assert_eq "existing picks numbered when .~1~ is a dangling symlink" \
    "yes" "$result"

# A resolvable symlink was already skipped correctly by stat
printf 'v1' > "$TESTDIR/sl_ok"
printf 'other' > "$TESTDIR/sl_target"
ln -s sl_target "$TESTDIR/sl_ok.~1~"
"$NEWFILE" -f --backup=numbered "$TESTDIR/sl_ok" 2>/dev/null
if [ -L "$TESTDIR/sl_ok.~1~" ]; then result="yes"; else result="no"; fi
assert_eq "a resolvable symlink at .~1~ also survives" "yes" "$result"

# Backup control aliases
echo "--- backup control aliases ---"
echo "never_content" > "$TESTDIR/bk_never"
"$NEWFILE" -f --backup=never "$TESTDIR/bk_never" 2>/dev/null
if [ -f "$TESTDIR/bk_never~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=never creates simple backup" "yes" "$result"

echo "none_content" > "$TESTDIR/bk_none"
"$NEWFILE" -f --backup=none "$TESTDIR/bk_none" 2>/dev/null
if [ -f "$TESTDIR/bk_none~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=none creates no backup" "no" "$result"

echo "off_content" > "$TESTDIR/bk_off"
"$NEWFILE" -f --backup=off "$TESTDIR/bk_off" 2>/dev/null
if [ -f "$TESTDIR/bk_off~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=off creates no backup" "no" "$result"

echo "t_content" > "$TESTDIR/bk_t"
"$NEWFILE" -f --backup=t "$TESTDIR/bk_t" 2>/dev/null
if [ -f "$TESTDIR/bk_t.~1~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=t creates numbered backup" "yes" "$result"

echo "existing1" > "$TESTDIR/bk_existing"
"$NEWFILE" -f --backup=existing "$TESTDIR/bk_existing" 2>/dev/null
if [ -f "$TESTDIR/bk_existing~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=existing (no .~1~) falls back to simple" "yes" "$result"

echo "existing2" > "$TESTDIR/bk_existing2"
echo "dummy" > "$TESTDIR/bk_existing2.~1~"
"$NEWFILE" -f --backup=existing "$TESTDIR/bk_existing2" 2>/dev/null
if [ -f "$TESTDIR/bk_existing2.~2~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=existing (with .~1~) uses numbered" "yes" "$result"

echo "nil_content" > "$TESTDIR/bk_nil"
"$NEWFILE" -f --backup=nil "$TESTDIR/bk_nil" 2>/dev/null
if [ -f "$TESTDIR/bk_nil~" ]; then result="yes"; else result="no"; fi
assert_eq "--backup=nil falls back to simple" "yes" "$result"

echo "vc_content" > "$TESTDIR/bk_vc"
VERSION_CONTROL=numbered "$NEWFILE" -f -b "$TESTDIR/bk_vc" 2>/dev/null
if [ -f "$TESTDIR/bk_vc.~1~" ]; then result="yes"; else result="no"; fi
assert_eq "VERSION_CONTROL=numbered uses numbered backup" "yes" "$result"

echo "vc_off_content" > "$TESTDIR/bk_vc_off"
VERSION_CONTROL=off "$NEWFILE" -f -b "$TESTDIR/bk_vc_off" 2>/dev/null
if [ -f "$TESTDIR/bk_vc_off~" ]; then result="yes"; else result="no"; fi
assert_eq "VERSION_CONTROL=off creates no backup" "no" "$result"

# Reference errors
echo "--- reference errors ---"
assert_exit "--reference nonexistent exits 1" 1 \
    "$NEWFILE" --reference="$TESTDIR/no_such_ref" "$TESTDIR/ref_out"

# Size edge cases
echo "--- size edge cases ---"
"$NEWFILE" -s 0 "$TESTDIR/size0" 2>/dev/null
assert_eq "-s 0 creates zero-length file" "0" "$(get_size "$TESTDIR/size0")"

"$NEWFILE" -s 1G --sparse "$TESTDIR/size1g" 2>/dev/null
assert_eq "-s 1G --sparse creates 1 GiB file" "1073741824" "$(get_size "$TESTDIR/size1g")"

"$NEWFILE" -s 4k "$TESTDIR/size4k" 2>/dev/null
assert_eq "-s 4k lowercase accepted" "4096" "$(get_size "$TESTDIR/size4k")"

assert_exit "-s 10X invalid suffix exits 1" 1 "$NEWFILE" -s 10X "$TESTDIR/size_bad1"
assert_exit "-s abc non-numeric exits 1" 1 "$NEWFILE" -s abc "$TESTDIR/size_bad2"
assert_exit "-s too large exits 1" 1 \
    "$NEWFILE" -s 9223372036854775808 "$TESTDIR/size_too_large"

# Sparse file via --sparse
echo "--- sparse ---"
"$NEWFILE" -s 1M --sparse "$TESTDIR/sized_sparse" 2>/dev/null
assert_eq "-s --sparse nominal size" "1048576" "$(get_size "$TESTDIR/sized_sparse")"
blocks=$(du -k "$TESTDIR/sized_sparse" | cut -f1)
if [ "$blocks" -lt 512 ]; then result="yes"; else result="no"; fi
assert_eq "-s --sparse uses fewer blocks than apparent size" "yes" "$result"

OUT=$("$NEWFILE" --sparse "$TESTDIR/sparse_warn" 2>&1)
if echo "$OUT" | grep -q "warning"; then result="yes"; else result="no"; fi
assert_eq "--sparse without --size prints warning" "yes" "$result"

# Mode edge cases
echo "--- mode edge cases ---"
"$NEWFILE" -A -m a+r "$TESTDIR/mode_ar" 2>/dev/null
assert_eq "-m a+r sets all-read" "444" "$(get_perms "$TESTDIR/mode_ar")"

"$NEWFILE" -A -m "u=rw,g=r,o=r" "$TESTDIR/mode_644sym" 2>/dev/null
assert_eq "-m u=rw,g=r,o=r gives 644" "644" "$(get_perms "$TESTDIR/mode_644sym")"

assert_exit "-m 0888 invalid octal exits 1" 1 "$NEWFILE" -m 0888 "$TESTDIR/mode_bad1"
assert_exit "-m u=q invalid perm char exits 1" 1 "$NEWFILE" -m u=q "$TESTDIR/mode_bad2"
assert_exit "-m x=rwx invalid who-char exits 1" 1 "$NEWFILE" -m x=rwx "$TESTDIR/mode_bad3"

# Backup without force
echo "--- backup without force ---"
assert_exit "-b without -f exits 0" 0 "$NEWFILE" -b "$TESTDIR/bk_warn_new"
if [ -f "$TESTDIR/bk_warn_new~" ]; then result="yes"; else result="no"; fi
assert_eq "-b without -f creates no backup file" "no" "$result"

# Template edge cases
echo "--- template edge cases ---"
touch "$TESTDIR/tpl_empty"
"$NEWFILE" --template="$TESTDIR/tpl_empty" "$TESTDIR/tpl_empty_out" 2>/dev/null
assert_eq "empty template creates empty file" "0" "$(get_size "$TESTDIR/tpl_empty_out")"

dd if=/dev/zero bs=256 count=1 > "$TESTDIR/tpl_bin_src" 2>/dev/null
"$NEWFILE" --template="$TESTDIR/tpl_bin_src" "$TESTDIR/tpl_binary" 2>/dev/null
assert_eq "binary template copied intact" "256" "$(get_size "$TESTDIR/tpl_binary")"

# Not a regular file.  The copy falls back to reading it.
"$NEWFILE" --template=/dev/null "$TESTDIR/tpl_devnull" 2>/dev/null
assert_eq "template from a character device is empty" "0" \
    "$(get_size "$TESTDIR/tpl_devnull")"

# A regular file whose reported size stands in for the real one, which
# only the fallback can copy correctly.  cmp is no use against these:
# it compares the reported sizes and stops.
SHORT_SRC=/sys/devices/system/cpu/online
if [ -r "$SHORT_SRC" ]; then
    "$NEWFILE" --template="$SHORT_SRC" "$TESTDIR/tpl_short" 2>/dev/null
    assert_eq "template from an overstated size copies real content" \
        "$(cat "$SHORT_SRC")" "$(cat "$TESTDIR/tpl_short")"
    assert_eq "template from an overstated size has the real length" \
        "$(wc -c < "$SHORT_SRC" | tr -d ' ')" \
        "$(get_size "$TESTDIR/tpl_short")"
fi

# The opposite lie: a size of zero on a file that does have content.
ZERO_SRC=/proc/version
if [ -r "$ZERO_SRC" ] && [ "$(get_size "$ZERO_SRC")" = "0" ]; then
    "$NEWFILE" --template="$ZERO_SRC" "$TESTDIR/tpl_zerosize" 2>/dev/null
    assert_eq "template from a size of zero copies real content" \
        "$(cat "$ZERO_SRC")" "$(cat "$TESTDIR/tpl_zerosize")"
fi

# Several megabytes of unpredictable data, large enough to exercise the
# whole-file path a shorter template would not reach.
if [ -c /dev/urandom ]; then
    dd if=/dev/urandom of="$TESTDIR/tpl_mb_src" bs=1048576 count=4 \
        2>/dev/null
    MB_SIZE=$(get_size "$TESTDIR/tpl_mb_src")
    assert_eq "multi-megabyte template source is 4 MiB" "4194304" "$MB_SIZE"
    "$NEWFILE" --template="$TESTDIR/tpl_mb_src" "$TESTDIR/tpl_mb_out" \
        2>/dev/null
    assert_eq "multi-megabyte template has the source size" "$MB_SIZE" \
        "$(get_size "$TESTDIR/tpl_mb_out")"
    if cmp -s "$TESTDIR/tpl_mb_src" "$TESTDIR/tpl_mb_out"; then
        result="yes"
    else
        result="no"
    fi
    assert_eq "multi-megabyte template is byte-identical" "yes" "$result"
fi

# Template from standard input (--template=-)
echo "--- template from stdin ---"
echo "hello stdin" | "$NEWFILE" --template=- -A -m 0644 "$TESTDIR/tpl_stdin" 2>/dev/null
assert_eq "--template=- copies stdin content" "hello stdin" "$(cat "$TESTDIR/tpl_stdin")"
assert_eq "--template=- applies mode" "644" "$(get_perms "$TESTDIR/tpl_stdin")"

printf 'shared\n' | "$NEWFILE" --template=- \
    "$TESTDIR/tpl_si_a" "$TESTDIR/tpl_si_b" "$TESTDIR/tpl_si_c" 2>/dev/null
assert_eq "--template=- first of many gets stdin" "shared" "$(cat "$TESTDIR/tpl_si_a")"
assert_eq "--template=- second of many gets stdin" "shared" "$(cat "$TESTDIR/tpl_si_b")"
assert_eq "--template=- third of many gets stdin" "shared" "$(cat "$TESTDIR/tpl_si_c")"

: | "$NEWFILE" --template=- "$TESTDIR/tpl_si_e1" "$TESTDIR/tpl_si_e2" 2>/dev/null
assert_eq "--template=- empty stdin first file empty" "0" "$(get_size "$TESTDIR/tpl_si_e1")"
assert_eq "--template=- empty stdin second file empty" "0" "$(get_size "$TESTDIR/tpl_si_e2")"

# 128 KiB, so buffering stdin for several files has to grow the buffer
# past its initial size more than once.
BIG_SRC="$TESTDIR/tpl_big_src"
: > "$BIG_SRC"
i=0
while [ "$i" -lt 16 ]; do
    printf '%08192d' 0 >> "$BIG_SRC"
    i=$((i + 1))
done
BIG_SIZE=$(get_size "$BIG_SRC")
assert_eq "oversized template source is 128 KiB" "131072" "$BIG_SIZE"
"$NEWFILE" --template=- "$TESTDIR/tpl_big_a" "$TESTDIR/tpl_big_b" \
    < "$BIG_SRC" 2>/dev/null
assert_eq "oversized stdin first file has full size" "$BIG_SIZE" \
    "$(get_size "$TESTDIR/tpl_big_a")"
assert_eq "oversized stdin second file has full size" "$BIG_SIZE" \
    "$(get_size "$TESTDIR/tpl_big_b")"
if cmp -s "$BIG_SRC" "$TESTDIR/tpl_big_a" \
   && cmp -s "$BIG_SRC" "$TESTDIR/tpl_big_b"; then
    result="yes"
else
    result="no"
fi
assert_eq "oversized stdin content is identical in both" "yes" "$result"

# Parent directory edge cases
echo "--- parents edge cases ---"
mkdir -p "$TESTDIR/pe/existing"
"$NEWFILE" -p "$TESTDIR/pe/existing/f" 2>/dev/null
if [ -f "$TESTDIR/pe/existing/f" ]; then result="yes"; else result="no"; fi
assert_eq "-p with existing parent succeeds" "yes" "$result"

touch "$TESTDIR/pe/blk"
assert_exit "-p when parent path is a file exits 1" 1 "$NEWFILE" -p "$TESTDIR/pe/blk/f"

# Combined options
echo "--- combined options ---"
"$NEWFILE" -A -m 0755 "$TESTDIR/co_ref" 2>/dev/null
"$NEWFILE" -p -A --reference="$TESTDIR/co_ref" "$TESTDIR/co/a/b/f" 2>/dev/null
if [ -f "$TESTDIR/co/a/b/f" ]; then result="yes"; else result="no"; fi
assert_eq "-p --reference creates nested file" "yes" "$result"
assert_eq "-p --reference copies mode" "755" "$(get_perms "$TESTDIR/co/a/b/f")"

echo "old" > "$TESTDIR/co_fsm"
"$NEWFILE" -f -s 2K "$TESTDIR/co_fsm" 2>/dev/null
assert_eq "-f -s overwrites existing with new size" "2048" "$(get_size "$TESTDIR/co_fsm")"

echo "combo" > "$TESTDIR/co_tpl_src"
"$NEWFILE" --template="$TESTDIR/co_tpl_src" -p "$TESTDIR/co/c/d/f" 2>/dev/null
assert_eq "--template -p creates nested file with content" "combo" "$(cat "$TESTDIR/co/c/d/f")"

# Owner, current user (no root required)
echo "--- owner current user ---"
"$NEWFILE" -o "$(id -u)" "$TESTDIR/own_uid" 2>/dev/null
assert_eq "-o uid sets owner to current user" "$(id -u)" "$(get_owner "$TESTDIR/own_uid")"

"$NEWFILE" -o "$(id -un):$(id -gn)" "$TESTDIR/own_name" 2>/dev/null
assert_eq "-o user:group sets owner" "$(id -u)" "$(get_owner "$TESTDIR/own_name")"

"$NEWFILE" -o ":$(id -gn)" "$TESTDIR/own_group" 2>/dev/null
assert_eq "-o :group sets group" "$(id -g)" "$(get_group "$TESTDIR/own_group")"

# A group that is a number rather than a name falls back to the ID.
"$NEWFILE" -o ":$(id -g)" "$TESTDIR/own_gidnum" 2>/dev/null
assert_eq "-o :gid accepts a numeric group" "$(id -g)" \
    "$(get_group "$TESTDIR/own_gidnum")"

# Owner combined with mode: the requested mode and setuid bit must
# survive the chown, which clears set-user-ID/set-group-ID bits.
echo "--- owner with mode ---"
"$NEWFILE" -A -m 4755 -o "$(id -u)" "$TESTDIR/own_suid" 2>/dev/null
assert_eq "-o with -m 4755 keeps setuid bit" "4755" "$(get_perms "$TESTDIR/own_suid")"

"$NEWFILE" -A -m 0640 -o "$(id -un):$(id -gn)" "$TESTDIR/own_mode" 2>/dev/null
assert_eq "-o user:group with -m 0640 applies mode" "640" "$(get_perms "$TESTDIR/own_mode")"

printf 'seed\n' | "$NEWFILE" --template=- -o "$(id -u)" "$TESTDIR/own_tpl" 2>/dev/null
assert_eq "-o with --template=- keeps content" "seed" "$(cat "$TESTDIR/own_tpl")"

assert_exit "-o invalid_user exits 1" 1 "$NEWFILE" -o "no_such_user_xyz_9999" "$TESTDIR/own_bad"
assert_exit "-o ':' exits 1" 1 "$NEWFILE" -o ":" "$TESTDIR/own_empty"

# A colon promises a group.  POSIX leaves an empty one undefined, so it
# is refused rather than silently meaning "-o owner".
assert_exit "-o 'user:' exits 1" 1 \
    "$NEWFILE" -o "$(id -un):" "$TESTDIR/own_trailing"
if [ -e "$TESTDIR/own_trailing" ]; then result="yes"; else result="no"; fi
assert_eq "-o 'user:' creates nothing" "no" "$result"
"$NEWFILE" -o "$(id -un)" "$TESTDIR/own_nocolon" 2>/dev/null
assert_eq "-o 'user' without a colon still works" "$(id -u)" \
    "$(get_owner "$TESTDIR/own_nocolon")"
assert_exit "-o negative numeric id exits 1" 1 "$NEWFILE" -o "-1" "$TESTDIR/own_negative"
assert_exit "-o overflowing numeric id exits 1" 1 \
    "$NEWFILE" -o "999999999999999999999999999999" "$TESTDIR/own_huge"

# Force overwrite & flag-combination consistency
echo "--- force overwrite & consistency ---"

# -f -m on an existing file (no -o) now applies the mode
printf 'old' > "$TESTDIR/ov_mode"; chmod 0666 "$TESTDIR/ov_mode"
"$NEWFILE" -f -A -m 0600 "$TESTDIR/ov_mode" 2>/dev/null
assert_eq "-f -m on existing applies mode without -o" "600" "$(get_perms "$TESTDIR/ov_mode")"

# -f without -m normalizes an existing file to the default mode
printf 'old' > "$TESTDIR/ov_def"; chmod 0600 "$TESTDIR/ov_def"
"$NEWFILE" -f -A "$TESTDIR/ov_def" 2>/dev/null
assert_eq "-f without -m normalizes to default a=rw" "666" "$(get_perms "$TESTDIR/ov_def")"

# -f with no content given leaves an empty file
printf 'oldcontent' > "$TESTDIR/ov_trunc"
"$NEWFILE" -f "$TESTDIR/ov_trunc" 2>/dev/null
assert_eq "-f with no content leaves an empty file" "0" \
    "$(get_size "$TESTDIR/ov_trunc")"

# --backup with a bad control but no -f: warns, does not abort
assert_exit "--backup=garbage without -f exits 0" 0 "$NEWFILE" --backup=garbage "$TESTDIR/bk_nf"

# Bad option values are rejected
assert_exit "-m garbage exits 1" 1 "$NEWFILE" -m garbage "$TESTDIR/bad_mode"
assert_exit "-s garbage exits 1" 1 "$NEWFILE" -s garbage "$TESTDIR/bad_size"

# -f builds beside the target and renames it into place
echo "--- atomic replace ---"
printf 'before' > "$TESTDIR/at_ino"
AT_OLD=$(get_inode "$TESTDIR/at_ino")
"$NEWFILE" -f -A -m 0644 "$TESTDIR/at_ino" 2>/dev/null
if [ "$AT_OLD" = "$(get_inode "$TESTDIR/at_ino")" ]; then
    result="same"
else
    result="new"
fi
assert_eq "-f gives the target a new inode" "new" "$result"
assert_eq "-f leaves no temporary file" "0" "$(count_temps)"

# An operand with no slash makes the directory to flush the working one.
mkdir -p "$TESTDIR/at_bare"
printf 'old' > "$TESTDIR/at_bare/f"
(cd "$TESTDIR/at_bare" && "$NEWFILE" -f -A -m 0644 f 2>/dev/null)
assert_eq "-f replaces a bare operand name" "644" \
    "$(get_perms "$TESTDIR/at_bare/f")"
assert_eq "-f on a bare name leaves no temporary file" "0" "$(count_temps)"

printf 'precious' > "$TESTDIR/at_keep"
assert_exit "-f with an unreadable template exits 1" 1 \
    "$NEWFILE" -f --template="$TESTDIR/no_such_template" "$TESTDIR/at_keep"
assert_eq "a failed -f leaves the target intact" "precious" \
    "$(cat "$TESTDIR/at_keep")"
assert_eq "a failed -f leaves no temporary file" "0" "$(count_temps)"

# The backup is a hard link and holds the original inode
printf 'v1' > "$TESTDIR/at_bk"
AT_BK=$(get_inode "$TESTDIR/at_bk")
"$NEWFILE" -f -b "$TESTDIR/at_bk" 2>/dev/null
assert_eq "backup keeps the original inode" "$AT_BK" \
    "$(get_inode "$TESTDIR/at_bk~")"
assert_eq "backup keeps the original content" "v1" \
    "$(cat "$TESTDIR/at_bk~")"

# A replaced path gets a new inode.  Other links keep the old content
printf 'linked' > "$TESTDIR/at_hl"
ln "$TESTDIR/at_hl" "$TESTDIR/at_hl2"
printf 'fresh' | "$NEWFILE" -f --template=- "$TESTDIR/at_hl" 2>/dev/null
assert_eq "the replaced path has the new content" "fresh" \
    "$(cat "$TESTDIR/at_hl")"
assert_eq "another hard link keeps the old content" "linked" \
    "$(cat "$TESTDIR/at_hl2")"

# -f replaces a symbolic link itself rather than writing through it
printf 'target' > "$TESTDIR/at_tgt"
ln -s at_tgt "$TESTDIR/at_lnk"
"$NEWFILE" -f -A -m 0644 "$TESTDIR/at_lnk" 2>/dev/null
if [ -L "$TESTDIR/at_lnk" ]; then result="yes"; else result="no"; fi
assert_eq "-f replaces the symlink, not its target" "no" "$result"
assert_eq "-f leaves the old symlink target alone" "target" \
    "$(cat "$TESTDIR/at_tgt")"

# A directory cannot be replaced, and the attempt leaves no debris
mkdir "$TESTDIR/at_dir"
assert_exit "-f on a directory exits 1" 1 "$NEWFILE" -f "$TESTDIR/at_dir"
if [ -d "$TESTDIR/at_dir" ]; then result="yes"; else result="no"; fi
assert_eq "-f leaves the directory in place" "yes" "$result"
assert_eq "-f on a directory leaves no temporary file" "0" "$(count_temps)"

# ulimit -f kills the write with SIGXFSZ mid-copy, so this covers the
# signal handler too.  Run from TESTDIR so any core file lands there.
echo "--- write failure ---"
"$NEWFILE" -s 2M "$TESTDIR/wf_src" 2>/dev/null
printf 'PRECIOUS' > "$TESTDIR/wf_keep"
sh -c "cd '$TESTDIR' && ulimit -f 100 && '$NEWFILE' -f --template=wf_src wf_keep; true" \
    >/dev/null 2>&1
assert_eq "a failed write leaves the target byte-identical" "PRECIOUS" \
    "$(cat "$TESTDIR/wf_keep")"
assert_eq "a failed write leaves no temporary file" "0" "$(count_temps)"

# A create that fails after the file exists takes the file with it
echo "--- unfinished creates ---"
assert_exit "create with an unreadable template exits 1" 1 \
    "$NEWFILE" --template="$TESTDIR/no_such_template" "$TESTDIR/uc_tpl"
if [ -e "$TESTDIR/uc_tpl" ]; then result="yes"; else result="no"; fi
assert_eq "a failed create leaves no file behind" "no" "$result"

# SIGXFSZ is ignored: the write reports EFBIG and later operands are
# still attempted instead of the run being killed on the first one.
OUT=$(sh -c "cd '$TESTDIR' && ulimit -f 100 && '$NEWFILE' -s 1M uc_a uc_b uc_c 2>&1; true")
RC=$(sh -c "cd '$TESTDIR' && ulimit -f 100 && '$NEWFILE' -s 1M uc_d 2>/dev/null; echo \$?")
assert_eq "a size limit is a per-file error, not a kill" "1" "$RC"
if echo "$OUT" | grep -q "File too large"; then result="yes"; else result="no"; fi
assert_eq "a size limit names the problem" "yes" "$result"
result=$(echo "$OUT" | grep -c "File too large")
assert_eq "every operand is attempted after the first fails" "3" "$result"
for f in uc_a uc_b uc_c uc_d; do
    if [ -e "$TESTDIR/$f" ]; then result="yes"; else result="no"; fi
    assert_eq "$f removed after its create failed" "no" "$result"
done

# A signal mid-create removes the file too.  The fifo has no writer, so
# newfile blocks reading it with the target already created.  SIGTERM,
# not SIGINT: a shell starts background jobs with SIGINT ignored, and
# newfile honours an inherited SIG_IGN.
if mkfifo "$TESTDIR/uc_fifo" 2>/dev/null; then
    "$NEWFILE" --template="$TESTDIR/uc_fifo" "$TESTDIR/uc_sig" >/dev/null 2>&1 &
    UCPID=$!
    sleep 1
    kill -TERM "$UCPID" 2>/dev/null
    wait "$UCPID" 2>/dev/null
    if [ -e "$TESTDIR/uc_sig" ]; then result="yes"; else result="no"; fi
    assert_eq "a signal during create removes the unfinished file" "no" "$result"
fi

# Renaming into a directory needs write and execute, flushing it needs
# read.  A directory granting the first but not the second must not turn
# a completed replacement into a failure.
echo "--- unreadable parent directory ---"
mkdir "$TESTDIR/wx" && printf 'old' > "$TESTDIR/wx/f" && chmod 0300 "$TESTDIR/wx"
assert_exit "-f into a write-only directory exits 0" 0 \
    "$NEWFILE" -f -A -m 0644 "$TESTDIR/wx/f"
chmod 0755 "$TESTDIR/wx"
assert_eq "-f into a write-only directory still applied the mode" "644" \
    "$(get_perms "$TESTDIR/wx/f")"

# An inherited SIG_IGN is the caller's decision and must not be
# overridden.  The fifo has no writer, so newfile blocks with the target
# already created.  The SIGTERM case proves the SIGINT case is not
# passing vacuously.
echo "--- inherited SIG_IGN ---"
if mkfifo "$TESTDIR/ig_fifo" 2>/dev/null; then
    sh -c "trap '' INT; exec '$NEWFILE' --template='$TESTDIR/ig_fifo' \
        '$TESTDIR/ig_a'" >/dev/null 2>&1 &
    IGPID=$!
    sleep 1
    kill -INT "$IGPID" 2>/dev/null
    sleep 1
    if kill -0 "$IGPID" 2>/dev/null; then result="alive"; else result="dead"; fi
    assert_eq "SIGINT ignored by the caller is not overridden" "alive" "$result"
    kill -TERM "$IGPID" 2>/dev/null
    wait "$IGPID" 2>/dev/null
    if kill -0 "$IGPID" 2>/dev/null; then result="alive"; else result="dead"; fi
    assert_eq "SIGTERM still kills it" "dead" "$result"
    if [ -e "$TESTDIR/ig_a" ]; then result="yes"; else result="no"; fi
    assert_eq "and the unfinished file is removed" "no" "$result"
fi

# The temporary goes beside the target, never into TMPDIR.  TMPDIR is
# pointed at a real directory rather than a broken one, so other tools
# in the same process tree that honour it keep working.
echo "--- temporary file placement ---"
mkdir "$TESTDIR/td_tmp"
printf 'old' > "$TESTDIR/td_file"
TMPDIR="$TESTDIR/td_tmp" "$NEWFILE" -f -A -m 0644 "$TESTDIR/td_file" 2>/dev/null
RC=$?
assert_eq "-f succeeds with TMPDIR set elsewhere" "0" "$RC"
assert_eq "-f applied the mode" "644" "$(get_perms "$TESTDIR/td_file")"
assert_eq "-f left nothing in TMPDIR" "0" \
    "$(find "$TESTDIR/td_tmp" -type f | wc -l | tr -d ' ')"

# Conclusive where a second filesystem exists: a temporary built in
# TMPDIR could not be renamed onto a target on another one.
for alt in /dev/shm /var/tmp; do
    [ -d "$alt" ] && [ -w "$alt" ] || continue
    [ "$(get_dev "$alt")" = "$(get_dev "$TESTDIR")" ] && continue
    printf 'old' > "$TESTDIR/td_xdev"
    TMPDIR="$alt" "$NEWFILE" -f -A -m 0600 "$TESTDIR/td_xdev" 2>/dev/null
    assert_eq "-f works with TMPDIR on another filesystem" "600" \
        "$(get_perms "$TESTDIR/td_xdev")"
    break
done

# -A applies to the file mode only, not to -p directories
echo "--- -A scope ---"
(umask 022 && "$NEWFILE" -p -A -m 0600 "$TESTDIR/ascope/a/b/f" 2>/dev/null)
assert_eq "-p -A parent dir subject to umask" "755" "$(get_perms "$TESTDIR/ascope/a")"
assert_eq "-p -A nested parent subject to umask" "755" "$(get_perms "$TESTDIR/ascope/a/b")"
assert_eq "-p -A file mode exact" "600" "$(get_perms "$TESTDIR/ascope/a/b/f")"

# Invalid operands are per-file errors, later operands still processed
echo "--- operand validation ---"
"$NEWFILE" "$TESTDIR/eo_one" "" "$TESTDIR/eo_two" 2>/dev/null
RC=$?
assert_eq "empty operand exits 1" "1" "$RC"
if [ -f "$TESTDIR/eo_two" ]; then result="yes"; else result="no"; fi
assert_eq "empty operand does not stop later operands" "yes" "$result"

assert_exit "trailing slash operand exits 1" 1 "$NEWFILE" "$TESTDIR/ts_dir/"
if [ -e "$TESTDIR/ts_dir" ]; then result="yes"; else result="no"; fi
assert_eq "trailing slash creates nothing" "no" "$result"

# Backups are made only of regular files
echo "--- backup of non-regular files ---"
mkdir "$TESTDIR/bk_dir"
assert_exit "-f --backup on a directory exits 1" 1 \
    "$NEWFILE" -f --backup=simple "$TESTDIR/bk_dir"
if [ -d "$TESTDIR/bk_dir" ]; then result="yes"; else result="no"; fi
assert_eq "directory not renamed by backup" "yes" "$result"

echo "hello" > "$TESTDIR/bk_ltarget"
ln -s bk_ltarget "$TESTDIR/bk_link"
assert_exit "-f --backup on a symlink exits 1" 1 \
    "$NEWFILE" -f --backup=simple "$TESTDIR/bk_link"
if [ -L "$TESTDIR/bk_link" ]; then result="yes"; else result="no"; fi
assert_eq "symlink not renamed by backup" "yes" "$result"
assert_eq "symlink target untouched" "hello" "$(cat "$TESTDIR/bk_ltarget")"

# Missing option arguments are reported as such
echo "--- missing option arguments ---"
OUT=$("$NEWFILE" -s 2>&1)
RC=$?
assert_eq "-s without argument exits 1" "1" "$RC"
if echo "$OUT" | grep -q "requires an argument"; then result="yes"; else result="no"; fi
assert_eq "-s without argument names the problem" "yes" "$result"

OUT=$("$NEWFILE" --mode 2>&1)
RC=$?
assert_eq "--mode without argument exits 1" "1" "$RC"
if echo "$OUT" | grep -q "requires an argument"; then result="yes"; else result="no"; fi
assert_eq "--mode without argument names the problem" "yes" "$result"

# Bare invocation reports the missing operand
echo "--- no arguments ---"
OUT=$("$NEWFILE" 2>&1)
RC=$?
assert_eq "no arguments exits 1" "1" "$RC"
if echo "$OUT" | grep -q "missing operand"; then result="yes"; else result="no"; fi
assert_eq "no arguments reports missing operand" "yes" "$result"

# Size must start with a digit, mode must not end with a comma
echo "--- strict parsing ---"
assert_exit "-s +5 rejected" 1 "$NEWFILE" -s +5 "$TESTDIR/sz_plus"
assert_exit "-s ' 5' rejected" 1 "$NEWFILE" -s ' 5' "$TESTDIR/sz_ws"
assert_exit "-s -5 rejected" 1 "$NEWFILE" -s -5 "$TESTDIR/sz_neg"
assert_exit "trailing comma in mode rejected" 1 \
    "$NEWFILE" -m 'u=rw,' "$TESTDIR/m_trail"
assert_exit "empty mode rejected" 1 "$NEWFILE" -m '' "$TESTDIR/m_empty"

# Verbose names the backup
echo "--- verbose backup ---"
echo "old" > "$TESTDIR/vb_file"
OUT=$("$NEWFILE" -f -b -v "$TESTDIR/vb_file" 2>&1)
if echo "$OUT" | grep -q "backup"; then result="yes"; else result="no"; fi
assert_eq "-f -b -v reports the backup" "yes" "$result"

# Option parsing
echo "--- option parsing ---"
"$NEWFILE" "$TESTDIR/op_a" -A -m 0600 "$TESTDIR/op_b" 2>/dev/null
assert_eq "option after an operand still applies" "600" \
    "$(get_perms "$TESTDIR/op_a")"
assert_eq "operand before an option still created" "600" \
    "$(get_perms "$TESTDIR/op_b")"

"$NEWFILE" -A -m0600 "$TESTDIR/op_attached" 2>/dev/null
assert_eq "attached short argument -m0600" "600" \
    "$(get_perms "$TESTDIR/op_attached")"

"$NEWFILE" --mode=0640 -A "$TESTDIR/op_eq" 2>/dev/null
assert_eq "long option in --name=value form" "640" \
    "$(get_perms "$TESTDIR/op_eq")"

"$NEWFILE" -pfv "$TESTDIR/op_bundle/x" >/dev/null 2>&1
if [ -f "$TESTDIR/op_bundle/x" ]; then result="yes"; else result="no"; fi
assert_eq "bundled short options -pfv" "yes" "$result"

mkdir -p "$TESTDIR/op_dashdir"
(cd "$TESTDIR/op_dashdir" && "$NEWFILE" -- -dashed 2>/dev/null)
if [ -f "$TESTDIR/op_dashdir/-dashed" ]; then result="yes"; else result="no"; fi
assert_eq "-- ends option parsing" "yes" "$result"

assert_exit "abbreviated long option rejected" 1 \
    "$NEWFILE" --tem=/dev/null "$TESTDIR/op_abbrev"

OUT=$("$NEWFILE" -z "$TESTDIR/op_unknown" 2>&1)
RC=$?
assert_eq "unknown short option exits 1" "1" "$RC"
if echo "$OUT" | grep -q "unknown option"; then result="yes"; else result="no"; fi
assert_eq "unknown short option names the problem" "yes" "$result"

OUT=$("$NEWFILE" --bogus "$TESTDIR/op_unknown2" 2>&1)
RC=$?
assert_eq "unknown long option exits 1" "1" "$RC"
if echo "$OUT" | grep -q "unknown option"; then result="yes"; else result="no"; fi
assert_eq "unknown long option names the problem" "yes" "$result"

OUT=$("$NEWFILE" --verbose=1 "$TESTDIR/op_noarg" 2>&1)
RC=$?
assert_eq "argument to a no-argument long option exits 1" "1" "$RC"
if echo "$OUT" | grep -q "does not take an argument"; then
    result="yes"
else
    result="no"
fi
assert_eq "no-argument long option names the problem" "yes" "$result"

OUT=$("$NEWFILE" -s 2>&1)
if echo "$OUT" | grep -q "^usage:"; then result="yes"; else result="no"; fi
assert_eq "option error prints the usage synopsis" "yes" "$result"

# Output that cannot be written is a failure, not a silent success.
# /dev/full is not portable.  The check runs only where it exists.
if [ -c /dev/full ]; then
    echo "--- unwritable standard output ---"
    assert_exit "--help to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' --help > /dev/full"
    assert_exit "--version to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' --version > /dev/full"
    printf 'x' > "$TESTDIR/full_v"
    assert_exit "-v to a full device exits non-zero" 1 \
        sh -c "'$NEWFILE' -f -v '$TESTDIR/full_v' > /dev/full"
    assert_eq "but the file was still created" "0" \
        "$(get_size "$TESTDIR/full_v")"
fi

echo ""
echo "=== Results: $PASS passed, $FAIL failed ==="

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0

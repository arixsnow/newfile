#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# inside.sh - the jobs tests/ci/run.sh starts inside a test image

set -u

JOB=${1:-}
SRC=/src
BUILD=/home/tester/build
OUT=/tmp/ci-out
CONFIGS="default notmpfile portable nolink"
VM_TIMEOUT=18000
SETUP_TIMEOUT=300
NFS_MOUNT_TIMEOUT=120
VM_CPUS=8
CHECK_RE='^=== \([0-9]*\) passed, \([0-9]*\) failed, \([0-9]*\) skipped ===$'
FAULTS_RE='^# \([0-9]*\) runs, \([0-9]*\) violations$'

flags() {
    case $1 in
        notmpfile) echo "-DNEWFILE_NO_TMPFILE" ;;
        portable)  echo "-DNEWFILE_NO_FASTIO -DNEWFILE_NO_THREADS" ;;
        nolink)    echo "-DNEWFILE_NO_FASTIO -DNEWFILE_NO_THREADS" \
                        "-DNEWFILE_NO_LINK -DNEWFILE_NO_SEEKHOLE" ;;
        *)         echo "" ;;
    esac
}

as_tester() {
    runuser -u tester -- "$@"
}

as_user() {
    who=$1
    shift
    if [ "$who" = root ]; then
        "$@"
    else
        as_tester "$@"
    fi
}

failed_in() {
    echo "$1" | cut -d' ' -f2
}

fail() {
    if ! printf '%-7s %s\n' "$1" "$2" >>"$OUT/table" ||
            ! : >"$OUT/failed"; then
        echo "inside.sh: cannot record a failure in $OUT: $1 $2" >&2
        exit 1
    fi
}

setup() {
    timeout -k 5 "$SETUP_TIMEOUT" "$@"
    setup_rc=$?
    case $setup_rc in
        124|137) echo "timed out after $SETUP_TIMEOUT seconds: $*" >&2 ;;
    esac
    return "$setup_rc"
}

oneline() {
    tr -s ' \n' ' ' <"$1"
}

build() {
    name=$1
    shift
    if ! as_tester env "$@" make -s -C "$SRC" BUILDDIR="$BUILD/$name" \
            all helpers >"$OUT/build-$name" 2>&1; then
        echo "build $name failed:"
        cat "$OUT/build-$name"
        exit 1
    fi
}

build_config() {
    build "$1-$2" CC="$1" CPPFLAGS="$(flags "$2")"
}

build_package() {
    case $(sed -n 's/^ID=//p' /etc/os-release) in
        fedora) pkg_env=$(rpm --eval '%{set_build_flags}') ;;
        ubuntu) pkg_env=$(dpkg-buildflags --export=sh 2>/dev/null) ;;
        *) echo "no package flags for this image"; exit 1 ;;
    esac || exit 1
    eval "$pkg_env"
    build package CC=gcc CFLAGS="${CFLAGS:-}" CPPFLAGS="${CPPFLAGS:-}" \
        LDFLAGS="${LDFLAGS:-}"
}

check() {
    as_user "${4:-tester}" env TMPDIR="$2" NEWFILE="$BUILD/$3/newfile" \
        GAPCHECK="$BUILD/$3/gapcheck" sh "$SRC/tests/check.sh" >"$1" 2>&1
    counts=$(sed -n "s/$CHECK_RE/\\1 \\2 \\3/p" "$1")
    if [ -z "$counts" ]; then
        counts="0 1 0"
    fi
    if [ "$(failed_in "$counts")" -ne 0 ]; then
        : >"$OUT/failed"
    fi
    echo "$counts"
}

faults() {
    scenarios=
    if [ "${4:-}" = root ]; then
        scenarios=owner_other
    fi
    as_user "${4:-tester}" env TMPDIR="$2" NEWFILE="$BUILD/$3/newfile" \
        FAULTS_SCENARIOS="$scenarios" sh "$SRC/tests/faults.sh" >"$1" 2>&1
    bad=$(sed -n "s/$FAULTS_RE/\\2/p" "$1")
    if [ -z "$bad" ]; then
        bad=1
    fi
    if [ "$bad" -ne 0 ]; then
        : >"$OUT/failed"
    fi
    echo "$bad"
}

cell() {
    read -r passed failed skipped <<EOF
$1
EOF
    if [ "$skipped" -eq 0 ]; then
        printf '%s/%s' "$passed" "$((passed + failed))"
    else
        printf '%s/%s+%s' "$passed" "$((passed + failed))" "$skipped"
    fi
}

probe() {
    if ! p=$(as_tester mktemp -d "$1/probe.XXXXXX"); then
        echo "? ?"
        return
    fi
    as_tester sh -c "printf 'new\n' > '$p/tpl' && printf 'old\n' > '$p/bk'"

    trace=$(cd "$p" && as_tester timeout -k 5 60 strace -f -a0 \
        -e trace=openat,linkat,link,renameat2,rename \
        "$BUILD/$2/newfile" --template=tpl out 2>&1 >/dev/null)
    case $trace in
        *'/proc/self/fd/'*'"out"'*'= 0'*) create="unnamed" ;;
        *'"out", RENAME_NOREPLACE) = 0'*) create="noreplace" ;;
        *'link(".newfile.'*'"out") = 0'*) create="link" ;;
        *'"out", O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600)'*)
            create="placeholder" ;;
        *) create="?" ;;
    esac

    trace=$(cd "$p" && as_tester timeout -k 5 60 strace -f -a0 \
        -e trace=link,renameat2,rename \
        "$BUILD/$2/newfile" -f --backup=numbered --template=tpl bk \
        2>&1 >/dev/null)
    case $trace in
        *'RENAME_EXCHANGE) = 0'*) backup="exchange" ;;
        *'rename(".newfile.'*'"bk") = 0'*) backup="flush+rename" ;;
        *) backup="?" ;;
    esac

    rm -rf "$p"
    echo "$create $backup"
}

suite() {
    fs=$1
    dir=$2
    want=$3
    got=$(findmnt -n -o FSTYPE --target "$dir")
    if [ "$got" != "$want" ]; then
        fail "$fs" "$dir is on '$got', not $want"
        return
    fi
    echo "running: $fs"
    row=$(printf '%-7s %-19s' "$fs" "$got")
    rootfail=0
    violations=0

    for b in $BUILDS; do
        counts=$(check "$OUT/$fs-$b" "$dir" "$b")
        row="$row $(printf '%-12s' "$(cell "$counts")")"
        counts=$(check "$OUT/$fs-$b-root" "$dir" "$b" root)
        rootfail=$((rootfail + $(failed_in "$counts")))
        violations=$((violations + $(faults "$OUT/$fs-$b-faults" "$dir" "$b")))
        violations=$((violations + \
            $(faults "$OUT/$fs-$b-root-faults" "$dir" "$b" root)))
    done

    probed=$(probe "$dir" "${BUILDS%% *}")
    case $probed in
        *'?'*) : >"$OUT/failed" ;;
    esac
    echo "$row $(printf '%-5s %-7s' "$rootfail" "$violations") $probed" \
        >>"$OUT/table"
}

own() {
    chown tester:tester "$1"
    chmod 0755 "$1"
}

unmount() {
    what=$1
    shift
    if ! setup "$@" >"$OUT/unmount-$what" 2>&1; then
        fail "$what" "unmount: $(oneline "$OUT/unmount-$what")"
    fi
}

fuse() {
    name=$1
    shift
    fstype=fuse.$1
    mkdir -p "/mnt/$name"
    if setup "$@" "/mnt/$name" >"$OUT/mount-$name" 2>&1; then
        own "/mnt/$name"
        suite "$name" "/mnt/$name" "$fstype"
        unmount "$name" fusermount3 -u "/mnt/$name"
    else
        fail "$name" "mount: $(oneline "$OUT/mount-$name")"
    fi
}

run_container() {
    mkdir -p /work/overlay
    own /work/overlay
    suite overlay /work/overlay overlay

    own /mnt/tmpfs
    suite tmpfs /mnt/tmpfs tmpfs

    mkdir -p /work/bindfs
    own /work/bindfs
    fuse bindfs bindfs -o allow_other /work/bindfs

    fo=/work/fo
    mkdir -p "$fo/lower" "$fo/upper" "$fo/work"
    fuse fuseovl fuse-overlayfs \
        -o "allow_other,lowerdir=$fo/lower,upperdir=$fo/upper,workdir=$fo/work"
}

loop_up() {
    name=$1
    fstype=$2
    dir=$3
    shift 3
    img=/tmp/$name.img

    truncate -s 512M "$img"
    if ! setup "$@" "$img" >"$OUT/mkfs-$name" 2>&1; then
        fail "$name" "mkfs: $(oneline "$OUT/mkfs-$name")"
        rm -f "$img"
        return 1
    fi

    mkdir -p "$dir"
    if ! setup mount -t "$fstype" -o loop "$img" "$dir" \
            >"$OUT/mount-$name" 2>&1; then
        dmesg | tail -n 5 >>"$OUT/mount-$name"
        fail "$name" "mount: $(oneline "$OUT/mount-$name")"
        rm -f "$img"
        return 1
    fi
}

loop_down() {
    unmount "$1" umount "$2"
    rm -f "/tmp/$1.img"
}

loopfs() {
    fs=$1
    shift
    mnt=/tmp/mnt/$fs
    if loop_up "$fs" "$fs" "$mnt" "$@"; then
        own "$mnt"
        suite "$fs" "$mnt" "$fs"
        loop_down "$fs" "$mnt"
    fi
}

nfs_up() {
    setup modprobe nfsd &&
    setup mount -t nfsd nfsd /proc/fs/nfsd &&
    setup mount -t tmpfs tmpfs /var/lib/nfs &&
    mkdir -p /var/lib/nfs/v4recovery &&
    setup exportfs -r &&
    setup exportfs -o rw,sync,no_root_squash,no_subtree_check,insecure,fsid=0 \
        "127.0.0.1:$1" &&
    setup rpc.mountd -N 2 -N 3 &&
    setup rpc.nfsd -N 3 8
}

nfs_mount() {
    deadline=$(($(date +%s) + NFS_MOUNT_TIMEOUT))
    while :; do
        left=$((deadline - $(date +%s)))
        if [ "$left" -le 0 ]; then
            echo "no NFS mount within $NFS_MOUNT_TIMEOUT seconds"
            return 1
        fi
        if timeout -k 5 "$left" \
                mount -t nfs4 -o retry=0 127.0.0.1:/ "$1"; then
            return 0
        fi
        sleep 1
    done
}

run_nfs() {
    export=/tmp/export
    mnt=/tmp/mnt/nfs

    if ! loop_up nfs ext4 "$export" mkfs.ext4 -q -F; then
        return
    fi
    own "$export"
    mkdir -p "$mnt"

    if ! nfs_up "$export" >"$OUT/nfsd" 2>&1; then
        dmesg | tail -n 5 >>"$OUT/nfsd"
        fail nfs "server: $(oneline "$OUT/nfsd")"
    elif ! nfs_mount "$mnt" >"$OUT/nfs-client" 2>&1; then
        dmesg | tail -n 5 >>"$OUT/nfs-client"
        fail nfs "client: $(oneline "$OUT/nfs-client")"
    else
        suite nfs "$mnt" nfs4
        unmount nfs umount "$mnt"
    fi

    setup rpc.nfsd 0 2>/dev/null
    setup exportfs -ua 2>/dev/null
    loop_down nfs "$export"
}

vm_ready() {
    echo 1 >/proc/sys/fs/protected_hardlinks &&
    ip link set lo up &&
    setup modprobe loop &&
    setup modprobe fuse
}

run_kernel() {
    uname -r >"$OUT/kernel"
    if vm_ready >"$OUT/vm-ready" 2>&1; then
        loopfs ext4 mkfs.ext4 -q -F
        loopfs xfs mkfs.xfs -q -f
        loopfs btrfs mkfs.btrfs -q -f
        loopfs f2fs mkfs.f2fs -q -f
        run_nfs
    else
        fail vm "setup: $(oneline "$OUT/vm-ready")"
    fi
    : >"$OUT/vm-finished"
}

need_vm() {
    if [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
        echo "no usable /dev/kvm in the container"
        exit 1
    fi
    for tool in vng strace findmnt bindfs fuse-overlayfs fusermount3 \
            mkfs.ext4 mkfs.xfs mkfs.btrfs mkfs.f2fs \
            exportfs rpc.mountd rpc.nfsd; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "$tool is missing from the image"
            exit 1
        fi
    done
}

run_vm() {
    kver=$(ls /usr/lib/modules)
    cpus=$(getconf _NPROCESSORS_ONLN)
    if [ "$cpus" -gt "$VM_CPUS" ]; then
        cpus=$VM_CPUS
    fi
    echo "running: vm"
    cd / || exit 1
    timeout -k 30 "$VM_TIMEOUT" vng --verbose --run "$kver" --memory 2G \
        --cpus "$cpus" --rwdir "$OUT" \
        --exec "sh $SRC/tests/ci/inside.sh kernel $BUILDS" \
        >"$OUT/vng" 2>&1 &
    vm=$!
    trap 'kill "$vm" 2>/dev/null' HUP INT TERM
    wait "$vm"
    rc=$?
    while kill -0 "$vm" 2>/dev/null; do
        wait "$vm"
        rc=$?
    done
    trap - HUP INT TERM
    if [ "$rc" -ne 0 ]; then
        fail vm "virtme-ng failed"
    fi
    booted=$(cat "$OUT/kernel" 2>/dev/null)
    if [ "$booted" != "$kver" ]; then
        fail vm "booted '$booted', not $kver"
    fi
    if [ ! -e "$OUT/vm-finished" ]; then
        fail vm "ended before the last filesystem"
    fi
}

report() {
    cat "$OUT/package" 2>/dev/null
    printf 'vm kernel: %s\n' "$(cat "$OUT/kernel" 2>/dev/null)"
    printf '%-7s %-19s' fs type
    for b in $BUILDS; do
        printf ' %-12s' "$b"
    done
    printf ' %-5s %-7s %s %s\n' root faults create backup
    cat "$OUT/table"
    details
}

details() {
    if ! echo finished >"$OUT/writable"; then
        echo "inside.sh: $OUT stopped taking writes, results are incomplete"
        return 1
    fi
    if [ ! -e "$OUT/failed" ]; then
        return 0
    fi
    for log in "$OUT"/*; do
        if grep -q '^--- failures' "$log" 2>/dev/null; then
            echo
            echo "--- $(basename "$log")"
            sed -n '/^--- failures/,/^$/p' "$log"
        fi
        if grep -q '^not ok' "$log" 2>/dev/null; then
            echo
            echo "--- $(basename "$log")"
            sed -n '/^not ok/,/^ok/p' "$log" | grep -v '^ok'
        fi
    done
    if [ -s "$OUT/vng" ]; then
        echo
        echo "--- kernel log"
        tail -n 60 "$OUT/vng"
    fi
    return 1
}

start() {
    if ! { mkdir -p "$OUT" "$BUILD" && chmod 0777 "$OUT" &&
            chown tester:tester "$BUILD" && : >"$OUT/table" &&
            rm -f "$OUT/failed" "$OUT/vm-finished"; }; then
        echo "inside.sh: cannot prepare $OUT and $BUILD" >&2
        exit 1
    fi
}

job_style() {
    as_tester make -s -C "$SRC" check-style || exit 1
}

job_test() {
    cc=$1
    printf '%-10s %-14s %s\n' build tester root
    for c in $CONFIGS; do
        build_config "$cc" "$c"
        user=$(check "$OUT/$cc-$c" /tmp "$cc-$c")
        root=$(check "$OUT/$cc-$c-root" /tmp "$cc-$c" root)
        printf '%-10s %-14s %s\n' "$c" "$(cell "$user")" "$(cell "$root")"
    done
    details
}

job_sanitize() {
    if ! setarch -R true 2>/dev/null; then
        echo "inside.sh: ASLR cannot be turned off for ThreadSanitizer" >&2
        exit 1
    fi
    build asan CC=gcc CFLAGS="-O1 -g -fno-omit-frame-pointer \
-fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror" \
        LDFLAGS="-fsanitize=address,undefined"
    build tsan CC=clang \
        CFLAGS="-O1 -g -fsanitize=thread -Wall -Wextra -Werror" \
        LDFLAGS="-fsanitize=thread"
    for b in asan tsan; do
        printf '%-10s %s\n' "$b" "$(cell "$(ASAN_OPTIONS=detect_leaks=1 \
            UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
            TSAN_OPTIONS=halt_on_error=1 check "$OUT/$b" /tmp "$b")")"
    done
    details
}

runs() {
    sed -n "s/$FAULTS_RE/\\1/p" "$1"
}

job_faults() {
    printf '%-10s %-20s %s\n' build "tester runs/bad" "root runs/bad"
    for c in $CONFIGS; do
        build_config gcc "$c"
        user=$(faults "$OUT/$c-faults" /tmp "gcc-$c")
        root=$(faults "$OUT/$c-root-faults" /tmp "gcc-$c" root)
        printf '%-10s %-20s %s\n' "$c" "$(runs "$OUT/$c-faults")/$user" \
            "$(runs "$OUT/$c-root-faults")/$root"
    done
    details
}

job_fs() {
    need_vm
    BUILDS=
    for c in $CONFIGS; do
        build_config gcc "$c"
        BUILDS="$BUILDS gcc-$c"
    done
    BUILDS=${BUILDS# }
    run_container
    run_vm
    report
}

job_release() {
    need_vm
    build_package
    as_tester env CFLAGS="${CFLAGS:-}" CPPFLAGS="${CPPFLAGS:-}" \
        LDFLAGS="${LDFLAGS:-}" make -s -C "$SRC" BUILDDIR="$BUILD/package" \
        check >"$OUT/package-make-check" 2>&1 || : >"$OUT/failed"
    {
        echo "package CFLAGS: ${CFLAGS:-}"
        echo "package LDFLAGS: ${LDFLAGS:-}"
        echo "package make check: $(tail -n 1 "$OUT/package-make-check")"
    } >"$OUT/package"
    BUILDS=package
    run_container
    run_vm
    report
}

case $JOB in
    kernel)
        shift
        BUILDS=$*
        run_kernel
        exit 0 ;;
    style|test|sanitize|faults|fs|release) ;;
    *)
        echo "usage: $0 style|test CC|sanitize|faults|fs|release" >&2
        exit 2 ;;
esac

start
case $JOB in
    style)    job_style ;;
    test)     job_test "${2:-gcc}" ;;
    sanitize) job_sanitize ;;
    faults)   job_faults ;;
    fs)       job_fs ;;
    release)  job_release ;;
esac

#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# faults.sh - fails newfile's system calls one at a time and judges each run

set -u

NEWFILE="${NEWFILE:-./newfile}"
case "$NEWFILE" in
    /*) ;;
    *) NEWFILE="$PWD/$NEWFILE" ;;
esac

SELF=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")

SCENARIOS="${FAULTS_SCENARIOS:-plain template holes size sparse exact owner
           owner_other force numbered simple simplelink existing parents batch
           fbatch pipe verbose}"

OTHER_UID=61000

FAILABLE="open openat creat link linkat rename renameat renameat2 unlink
          unlinkat mkdir mkdirat fsync fdatasync read pread64 write pwrite64
          copy_file_range sendfile splice fallocate ftruncate fchmod fchown
          fstat newfstatat statx lseek close getdents64 memfd_create
          getrandom clone3"

CAUGHT="SIGALRM SIGINT SIGQUIT SIGHUP SIGTERM SIGPIPE SIGUSR1 SIGUSR2
        SIGILL SIGTRAP SIGABRT SIGBUS SIGFPE SIGSEGV SIGXCPU SIGSYS
        SIGVTALRM SIGPROF SIGIO SIGPWR SIGSTKFLT"
SIGNALS="SIGALRM SIGINT SIGHUP SIGPIPE SIGUSR1 SIGUSR2 SIGVTALRM SIGPROF
         SIGIO SIGPWR SIGSTKFLT"
SIGNAL_SCENARIO=fbatch
FAULTS_TIMEOUT=${FAULTS_TIMEOUT:-60}
FAULTS_GRACE=${FAULTS_GRACE:-5}

hung() {
    [ "$1" -eq 124 ] || [ "$1" -eq 137 ]
}

bounded() {
    exec 3<&0
    env --default-signal=INT,QUIT \
        timeout -k "$FAULTS_GRACE" "$FAULTS_TIMEOUT" "$@" <&3 3<&- &
    t_run=$!
    exec 3<&-
    wait "$t_run"
    t_rc=$?
    kill -s KILL -- "-$t_run" 2>/dev/null
    return "$t_rc"
}

signame() {
    case $1 in
        144) echo STKFLT ;;
        *) kill -l "$1" ;;
    esac
}

errors() {
    case $1 in
        open|openat|creat) echo EACCES ENOSPC EDQUOT EROFS EMFILE EIO ;;
        write|pwrite64|fallocate|ftruncate|copy_file_range|sendfile|splice)
            echo ENOSPC EDQUOT EIO EFBIG ;;
        read|pread64|getdents64) echo EIO ;;
        fsync|fdatasync) echo EIO ENOSPC EINTR ;;
        link|linkat) echo ENOSPC EDQUOT EROFS EIO EMLINK ;;
        rename|renameat|renameat2|mkdir|mkdirat)
            echo ENOSPC EDQUOT EROFS EIO ;;
        unlink|unlinkat) echo EROFS EIO EBUSY ;;
        fchmod|fchown) echo EPERM EROFS EIO ;;
        fstat|newfstatat|statx|lseek|close) echo EIO ;;
        memfd_create) echo EMFILE ENOMEM ;;
        getrandom) echo ENOSYS ;;
        clone3) echo EAGAIN ;;
    esac
}

sc_plain() {
    ARGS="a"
}

sc_template() {
    printf 'template\n' >t
    ARGS="--template=t a"
}

sc_holes() {
    truncate -s 1048575 t
    printf x >>t
    ARGS="--template=t a b"
}

sc_size() {
    ARGS="-s 64K a"
}

sc_sparse() {
    ARGS="-s 64K --sparse a"
}

sc_exact() {
    printf 'template\n' >t
    ARGS="-A -m 4755 --template=t a"
}

sc_owner() {
    ARGS="-o $(id -u) a"
}

sc_owner_other() {
    printf 'template\n' >t
    ARGS="-o $OTHER_UID -m 4755 --template=t a"
}

sc_force() {
    printf 'template\n' >t
    printf old >a
    ARGS="-f --template=t a"
}

sc_numbered() {
    printf old >a
    printf older >a.~1~
    ARGS="-f --backup=numbered -s 3 a"
}

sc_simple() {
    printf old >a
    printf older >a~
    ARGS="-f --backup=simple -s 3 a"
}

sc_simplelink() {
    printf old >a
    ln a a~
    ARGS="-f --backup=simple -s 3 a"
}

sc_existing() {
    printf old >a
    ARGS="-f --backup=existing -s 3 a"
}

sc_parents() {
    printf 'template\n' >t
    ARGS="-p --template=t x/y/a"
}

sc_batch() {
    printf 'template\n' >t
    printf old >b
    ARGS="--template=t a b c a"
}

sc_fbatch() {
    printf 'template\n' >t
    printf old >b
    ARGS="-f -b --template=t a b c"
}

sc_pipe() {
    printf 'piped\n' >in
    PIPE=in
    ARGS="--template=- a b"
}

sc_verbose() {
    ARGS="-v a b"
}

state() {
    (cd "$1" || exit
     find . -mindepth 1 -type f -exec cksum {} +
     find . -mindepth 1 -printf 'M %P %y %m\n') |
    awk '$1 == "M" { meta[$2] = $3 " " $4; next }
         { p = $3; sub(/^\.\//, "", p); sum[p] = $1 " " $2 }
         END { for (p in meta) print p, meta[p], (p in sum) ? sum[p] : "-" }' |
    sort
}

run_newfile() {
    out=$1
    shift
    # shellcheck disable=SC2086
    if [ -n "$PIPE" ]; then
        # shellcheck disable=SC2002
        cat -- "$PIPE" | bounded strace -f -qq "$@" "$NEWFILE" $ARGS \
            >"$out.out" 2>"$out.err"
    else
        bounded strace -f -qq "$@" "$NEWFILE" $ARGS </dev/null \
            >"$out.out" 2>"$out.err"
    fi
}

calls() {
    awk -v failable="$FAILABLE" '
        BEGIN {
            n = split(failable, f, /[ \n]+/)
            for (i = 1; i <= n; i++) ok[f[i]] = 1
        }
        {
            pid = $1
            rest = $0
            sub(/^[0-9]+ +/, "", rest)
            if (main == "") main = pid
            if (rest ~ /^(<\.\.\.|\+\+\+|---)/) next
            i = index(rest, "(")
            if (i == 0) next
            name = substr(rest, 1, i - 1)
            arg = substr(rest, i + 1)
            sub(/[,)].*/, "", arg)
            k = ++count[pid, name]
            if (pid == main && !started) {
                if (name == "fcntl" && arg == "1") started = 1
                else { skip[name] = k; next }
            }
            if (!(name in ok) || (name == "write" && arg == "2")) next
            if (k <= skip[name]) next
            gsub(/[ \t]/, "_", arg)
            if (pid == main) inmain[name " " k] = (arg == "") ? "-" : arg
            seen[name " " k] = 1
        }
        END {
            if (!started) exit 1
            for (c in seen)
                print c, (c in inmain) ? "main " inmain[c] : "thread -"
        }' "$1" >"$1.calls" || return 1
    sort "$1.calls"
}

judge() {
    awk -v rc="$5" -v rcc="$6" -v mute="$7" '
        FILENAME == ARGV[1] { before[$1] = $0; next }
        FILENAME == ARGV[2] { clean[$1] = $0; next }
        FILENAME == ARGV[3] { after[$1] = $0; next }
        {
            if ($0 ~ /^[^:]*: cannot remove \047[^\047]*\047/) {
                s = $0
                sub(/^[^:]*: cannot remove \047/, "", s)
                sub(/\047.*/, "", s)
                allowed[s] = 1
                left = 1
            }
            if (match($0, /kept \047[^\047]*\047/))
                allowed[substr($0, RSTART + 6, RLENGTH - 7)] = 1
            if ($0 ~ /^[^:]*: kept \047/)
                left = 1
            said = 1
        }
        END {
            if (rc == 1 && !said && !mute) print "exit 1 with no message"
            if (rc == 0 && rcc != 0) print "exit 0 where a clean run fails"
            if (rc == 0 && left) print "exit 0 with a name left behind"
            for (p in after) {
                if (p in allowed) continue
                n = split(p, part, "/")
                if (index(part[n], ".newfile.") == 1) {
                    print "temporary name left: " p
                    continue
                }
                if (rc == 0) {
                    if (!(p in clean) || clean[p] != after[p])
                        print "exit 0 but " after[p]
                    continue
                }
                if ((p in clean) && clean[p] == after[p]) {
                    t = p
                    if (!(p in before) && sub(/(\.~[0-9]+~|~)$/, "", t) \
                        && (t in before) && after[t] == before[t] \
                        && before[t] != clean[t])
                        print "backup left for a failed replace: " p
                    continue
                }
                if ((p in before) && before[p] == after[p]) continue
                print "neither before nor after: " after[p]
            }
            if (rc == 0)
                for (p in clean)
                    if (!(p in after)) print "exit 0 but missing: " p
        }' "$1" "$2" "$3" "$4"
}

job() {
    if [ -e "$WORK/stop" ]; then
        exit 0
    fi
    base=$WORK/$1
    d=$base/run.$$
    mkdir "$d" "$d.tmp" || exit 1
    cd "$d" || exit 1

    PIPE=
    "sc_$1"
    case $4 in
        sig)  spec="signal=SIGTERM"; want=TERM ;;
        pre)  spec="error=EINTR:signal=SIGTERM"; want=TERM ;;
        SIG*) spec="signal=$4"; want=${4#SIG} ;;
        *)    spec="error=$4"; want= ;;
    esac

    TMPDIR=$d.tmp
    export TMPDIR
    (run_newfile "$d" -o /dev/null -e trace="$2" \
        -e inject="$2:$spec:when=$3") 2>/dev/null
    rc=$?
    cd "$base" || exit 1

    if hung "$rc"; then
        : >"$WORK/stop"
        echo "$1 $2 #$3 $4: hung, stopped after $FAULTS_TIMEOUT seconds"
        rm -rf "$d" "$d.tmp" "$d.out" "$d.err"
        return
    fi

    : >"$d.bad"
    if [ -n "$want" ]; then
        if [ "$rc" -le 128 ] || [ "$(signame "$rc")" != "$want" ]; then
            echo "exit $rc, not killed by SIG$want" >>"$d.bad"
        fi
    elif [ "$rc" -gt 1 ]; then
        echo "exit $rc: $(head -1 "$d.err")" >>"$d.bad"
    fi
    state "$d" >"$d.state"
    mute=
    if [ "$2" = close ] && [ "$5" = 2 ]; then
        mute=1
    fi
    judge "$base/before.state" "$base/clean.state" "$d.state" "$d.err" \
        "$rc" "$(cat "$base/rc")" "$mute" >>"$d.bad"
    for left in "$d.tmp"/*; do
        if [ -e "$left" ] \
           && ! grep -qF "warning: cannot remove '$left'" "$d.err"; then
            echo "spool left in TMPDIR: ${left##*/}" >>"$d.bad"
        fi
    done

    sed "s|^|$1 $2 #$3 $4: |" "$d.bad"
    rm -rf "$d" "$d.tmp" "$d.out" "$d.err" "$d.state" "$d.bad"
}

prepare() {
    base=$WORK/$1
    mkdir -p "$base/before" "$base/clean" "$base/clean.tmp"
    (cd "$base/before" && PIPE= && "sc_$1")
    state "$base/before" >"$base/before.state"

    (cd "$base/clean" || exit 1
     PIPE=
     "sc_$1"
     TMPDIR=$base/clean.tmp
     export TMPDIR
     run_newfile "$base/clean" -o "$base/clean.log"
     echo $? >"$base/rc")
    if hung "$(cat "$base/rc")"; then
        : >"$WORK/stop"
        echo "$1: the clean run hung, stopped after $FAULTS_TIMEOUT seconds" \
            >>"$WORK/violations"
        return
    fi
    state "$base/clean" >"$base/clean.state"

    extra=
    if [ "$1" = "$SIGNAL_SCENARIO" ]; then
        extra=$SIGNALS
    fi

    if ! calls "$base/clean.log" >"$base/calls"; then
        echo "$1: main() not found in the trace" >>"$WORK/violations"
    fi
    while read -r name n where arg; do
        signals=
        if [ "$where" = main ]; then
            signals="sig pre $extra"
        fi
        for mode in $(errors "$name") $signals; do
            echo "$1 $name $n $mode $arg"
        done
    done <"$base/calls"
}

handlers() {
    d=$WORK/handlers
    mkdir "$d" || exit 1
    (cd "$d" && bounded strace -qq -o "$d.log" -e trace=rt_sigaction \
        "$NEWFILE" a </dev/null 2>/dev/null)
    rc=$?
    if hung "$rc"; then
        : >"$WORK/stop"
        echo "handlers: hung, stopped after $FAULTS_TIMEOUT seconds"
        return
    fi
    for s in $CAUGHT; do
        if ! grep -q "^rt_sigaction($s, {sa_handler=0x" "$d.log"; then
            echo "handlers: no handler for $s"
        fi
    done
}

if [ "${1:-}" = job ]; then
    shift
    job "$@"
    exit 0
fi

echo "TAP version 14"
if ! command -v strace >/dev/null 2>&1; then
    echo "1..0 # SKIP strace is not installed"
    exit 77
fi
if [ ! -x "$NEWFILE" ]; then
    echo "Bail out! no newfile at $NEWFILE"
    exit 99
fi

if ! WORK=$(mktemp -d); then
    echo "Bail out! cannot make a scratch directory"
    exit 99
fi
export WORK NEWFILE FAILABLE FAULTS_TIMEOUT FAULTS_GRACE
trap 'rm -rf "$WORK"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

JOBS="${FAULTS_JOBS:-$(getconf _NPROCESSORS_ONLN)}"

: >"$WORK/violations"
: >"$WORK/skipped"
: >"$WORK/unreached"
for s in $SCENARIOS; do
    if [ "$s" = owner_other ] && [ "$(id -u)" != 0 ]; then
        echo "$s" >>"$WORK/skipped"
        continue
    fi
    if [ -e "$WORK/stop" ]; then
        echo "$s" >>"$WORK/unreached"
        continue
    fi
    prepare "$s"
done >"$WORK/jobs"

if [ -e "$WORK/stop" ]; then
    echo handlers >>"$WORK/unreached"
else
    handlers >>"$WORK/violations"
fi
finished=yes
if [ -s "$WORK/jobs" ]; then
    if ! xargs -n 5 -P "$JOBS" sh "$SELF" job <"$WORK/jobs" \
            >>"$WORK/violations"; then
        finished=no
        echo "a fault run died, so later runs were not made" \
            >>"$WORK/violations"
    fi
fi

n=0
for s in handlers $SCENARIOS; do
    n=$((n + 1))
    if grep -qx "$s" "$WORK/skipped"; then
        echo "ok $n - $s # SKIP needs root"
        continue
    fi
    if grep -qx "$s" "$WORK/unreached"; then
        echo "ok $n - $s # SKIP a run hung"
        continue
    fi
    if [ "$s" = handlers ]; then
        runs=1
    else
        runs=$(grep -c "^$s " "$WORK/jobs")
    fi
    grep "^${s}[ :]" "$WORK/violations" | sort >"$WORK/found"
    if [ -s "$WORK/found" ]; then
        bad=$(wc -l <"$WORK/found" | tr -d ' ')
        echo "not ok $n - $s: $runs runs, $bad violations"
        sed 's/^/#   /' "$WORK/found"
    else
        echo "ok $n - $s: $runs runs"
    fi
done
n=$((n + 1))
if [ "$finished" = yes ]; then
    echo "ok $n - every fault run was made"
else
    echo "not ok $n - every fault run was made"
fi

runs=$(wc -l <"$WORK/jobs" | tr -d ' ')
bad=$(wc -l <"$WORK/violations" | tr -d ' ')
if [ -e "$WORK/stop" ]; then
    echo "# stopped at the first hang"
fi
echo "# $runs runs, $bad violations"
echo "1..$n"

[ "$bad" -eq 0 ]

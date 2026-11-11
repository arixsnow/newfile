#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# run.sh - builds the test images and runs every check inside them

set -u

DEV=fedora-45
TARGETS="fedora-44 fedora-45 fedora-rawhide ubuntu-26.04 ubuntu-26.10"
BUILD_LIMIT=1800
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH='' cd -- "$HERE/../.." && pwd)
STATUS=0

usage() {
    echo "usage: $0 dev image|style|sanitize|faults|fs|all" >&2
    echo "       $0 dev test [gcc|clang]" >&2
    echo "       $0 release $(echo "$TARGETS" | tr ' ' '|')|all" >&2
    exit 2
}

if ! command -v podman >/dev/null 2>&1; then
    echo "run.sh: podman is required" >&2
    exit 2
fi
if [ ! -c /dev/fuse ]; then
    echo "run.sh: /dev/fuse is required" >&2
    exit 2
fi

image() {
    target=$1
    shift
    if ! timeout --foreground -k 30 "$BUILD_LIMIT" \
            podman build -q "$@" --build-arg RELEASE="${target#*-}" \
            -t "localhost/newfile-ci:$target" \
            -f "$HERE/Containerfile.${target%%-*}" "$HERE" >/dev/null; then
        echo "run.sh: cannot build the $target image" >&2
        exit 1
    fi
}

job_limit() {
    case $1 in
        style) echo 1200 ;;
        test|sanitize) echo 2700 ;;
        faults) echo 6000 ;;
        *) echo 19800 ;;
    esac
}

job() {
    target=$1
    shift
    echo "=== $target: $*"
    extra=
    case $1 in
        fs|release)
            if [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
                echo "run.sh: $1 needs access to /dev/kvm" >&2
                STATUS=1
                return
            fi
            extra="--device /dev/kvm" ;;
        sanitize)
            extra="--security-opt seccomp=unconfined" ;;
    esac
    limit=$(job_limit "$1")
    # shellcheck disable=SC2086
    podman run --rm --timeout "$limit" --security-opt label=disable \
        --security-opt apparmor=unconfined --cap-add SYS_ADMIN \
        --device /dev/fuse $extra --tmpfs /mnt/tmpfs:rw,size=512m \
        -v "$TOP:/src:ro" "localhost/newfile-ci:$target" \
        sh /src/tests/ci/inside.sh "$@"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        STATUS=1
    fi
    if [ "$rc" -gt 1 ]; then
        echo "run.sh: $target $1 ended with status $rc, its limit is" \
            "$limit seconds" >&2
    fi
}

case ${1:-} in
    dev)
        if [ -z "${CI_IMAGE_LOADED:-}" ] ||
                ! podman image exists "localhost/newfile-ci:$DEV"; then
            image "$DEV" --pull=newer
        fi
        case ${2:-} in
            image) ;;
            style|sanitize|faults|fs) job "$DEV" "$2" ;;
            test)
                case ${3:-gcc} in
                    gcc|clang) job "$DEV" test "${3:-gcc}" ;;
                    *) usage ;;
                esac ;;
            all)
                job "$DEV" style
                job "$DEV" test gcc
                job "$DEV" test clang
                job "$DEV" sanitize
                job "$DEV" faults
                job "$DEV" fs ;;
            *) usage ;;
        esac ;;
    release)
        case ${2:-} in
            all) list=$TARGETS ;;
            *)
                case " $TARGETS " in
                    *" ${2:-none} "*) list=$2 ;;
                    *) usage ;;
                esac ;;
        esac
        for t in $list; do
            podman image rm "localhost/newfile-ci:$t" >/dev/null 2>&1
            image "$t" --pull=always --no-cache
            job "$t" release
        done ;;
    *) usage ;;
esac

exit "$STATUS"

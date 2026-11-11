/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* fastio.c - kernel fast paths that fall back to portable code */

#if defined(__linux__) && !defined(NEWFILE_NO_FASTIO)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "newfile.h"
#include "fastio.h"
#include <errno.h>

#if defined(__linux__) && !defined(NEWFILE_NO_FASTIO)

#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef NEWFILE_NO_SENDFILE
#include <sys/sendfile.h>
#endif

#ifndef NEWFILE_NO_TMPFILE
#include <sys/stat.h>
#endif

#define PROC_FD_MAX 32
#define SPLICE_MAX  ((size_t)1 << 30)

static size_t clamp_len(off_t len);
static int fast_sendfile(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off,
                         off_t len);

/* Returns 0, 1 if the filesystem cannot, or -1 with errno set. */
int fast_alloc(int fd, off_t off, off_t len)
{
    static bool limit_known;
    static rlim_t size_limit;
    struct rlimit rl;

    if (len <= 0) {
        return 1;
    }

    if (!limit_known) {
        size_limit = (getrlimit(RLIMIT_FSIZE, &rl) == 0) ? rl.rlim_cur
                                                         : RLIM_INFINITY;
        limit_known = true;
    }

    if (size_limit != RLIM_INFINITY
        && (uintmax_t)off + (uintmax_t)len > (uintmax_t)size_limit) {
        errno = EFBIG;
        return -1;
    }

    if (fallocate(fd, 0, off, len) == 0) {
        return 0;
    }

    if (errno == EOPNOTSUPP || errno == ENOSYS || errno == EINVAL) {
        return 1;
    }

    return -1;
}

static size_t clamp_len(off_t len)
{
    return ((uintmax_t)len > (uintmax_t)SSIZE_MAX) ? (size_t)SSIZE_MAX
                                                   : (size_t)len;
}

/* Both offsets advance.  Returns 0, 1 if unusable, or -1 with errno set. */
int fast_copy(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off, off_t len)
{
    ssize_t ncopied;

    while (len > 0) {
        ncopied = copy_file_range(src_fd, src_off, dst_fd, dst_off,
                                  clamp_len(len), 0);
        if (ncopied < 0) {
            if (errno == EXDEV || errno == EINVAL || errno == ENOSYS
                || errno == EOPNOTSUPP || errno == EBADF || errno == EPERM
                || errno == ETXTBSY) {
                return fast_sendfile(dst_fd, dst_off, src_fd, src_off, len);
            }

            return -1;
        }

        if (ncopied == 0) {
            return fast_sendfile(dst_fd, dst_off, src_fd, src_off, len);
        }

        len -= ncopied;
    }

    return 0;
}

#ifdef NEWFILE_NO_SENDFILE

static int fast_sendfile(ATTR_UNUSED int dst_fd, ATTR_UNUSED off_t *dst_off,
                         ATTR_UNUSED int src_fd, ATTR_UNUSED off_t *src_off,
                         ATTR_UNUSED off_t len)
{
    return 1;
}

#else

/* Returns 0, 1 if unusable, or -1 with errno set. */
static int fast_sendfile(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off,
                         off_t len)
{
    ssize_t nsent;

    if (lseek(dst_fd, *dst_off, SEEK_SET) == -1) {
        return 1;
    }

    while (len > 0) {
        nsent = sendfile(dst_fd, src_fd, src_off, clamp_len(len));
        if (nsent < 0) {
            if (errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP
                || errno == EBADF || errno == ESPIPE) {
                return 1;
            }

            return -1;
        }

        if (nsent == 0) {
            return 1;
        }

        *dst_off += nsent;
        len -= nsent;
    }

    return 0;
}

#endif

#ifdef NEWFILE_NO_TMPFILE

int fast_tmpfile(ATTR_UNUSED const char *dir, ATTR_UNUSED mode_t mode,
                 ATTR_UNUSED int *fd_out)
{
    return 1;
}

#else

/* Returns 0 with *fd_out set, 1 if unusable, or -1 with errno set. */
int fast_tmpfile(const char *dir, mode_t mode, int *fd_out)
{
    static int proc_usable;
    struct stat st;
    int fd;

    if (proc_usable == 0) {
        proc_usable = (stat("/proc/self/fd", &st) == 0 && S_ISDIR(st.st_mode))
                          ? 1
                          : -1;
    }
    if (proc_usable < 0) {
        return 1;
    }

    fd = open(dir, O_TMPFILE | O_WRONLY | O_CLOEXEC, mode);
    if (fd == -1) {
        if (errno == EOPNOTSUPP || errno == EISDIR) {
            return 1;
        }
        return -1;
    }

    *fd_out = fd;
    return 0;
}

#endif

/* Fails with EEXIST rather than replace.  Returns 0, or -1. */
int fast_link(int fd, const char *path)
{
    char proc_path[PROC_FD_MAX];

    if (snprintf(proc_path, sizeof proc_path, "/proc/self/fd/%d", fd) < 0) {
        return -1;
    }

    return linkat(AT_FDCWD, proc_path, AT_FDCWD, path, AT_SYMLINK_FOLLOW);
}

/* Returns 0, 1 if the filesystem cannot, or -1 with errno set. */
int fast_rename_noreplace(const char *from, const char *to)
{
    if (renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE) == 0) {
        return 0;
    }

    return (errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP) ? 1 : -1;
}

/* Returns 0, 1 if the filesystem cannot, or -1 with errno set. */
int fast_exchange(const char *a, const char *b)
{
    if (renameat2(AT_FDCWD, a, AT_FDCWD, b, RENAME_EXCHANGE) == 0) {
        return 0;
    }

    return (errno == EINVAL || errno == ENOSYS || errno == EOPNOTSUPP) ? 1 : -1;
}

void fast_writeback(int fd)
{
    sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WRITE);
}

/* Returns 0 with *fd_out set, 1 if unusable, or -1. */
int fast_spool(int *fd_out)
{
    int fd;

    fd = memfd_create("newfile-template", MFD_CLOEXEC);
    if (fd == -1) {
        if (errno == ENOSYS || errno == EPERM || errno == EINVAL) {
            return 1;
        }
        return -1;
    }

    *fd_out = fd;
    return 0;
}

/* Returns 0, 1 if src_fd cannot be spliced from, or -1. */
int fast_splice(int dst_fd, int src_fd)
{
    ssize_t nmoved;
    bool moved;

    moved = false;
    while (1) {
        nmoved = splice(src_fd, NULL, dst_fd, NULL, SPLICE_MAX, SPLICE_F_MOVE);
        if (nmoved == 0) {
            return 0;
        }
        if (nmoved < 0) {
            if (!moved && (errno == EINVAL || errno == ENOSYS)) {
                return 1;
            }
            return -1;
        }
        moved = true;
    }
}

#else

int fast_alloc(ATTR_UNUSED int fd, ATTR_UNUSED off_t off, ATTR_UNUSED off_t len)
{
    return 1;
}

int fast_copy(ATTR_UNUSED int dst_fd, ATTR_UNUSED off_t *dst_off,
              ATTR_UNUSED int src_fd, ATTR_UNUSED off_t *src_off,
              ATTR_UNUSED off_t len)
{
    return 1;
}

int fast_tmpfile(ATTR_UNUSED const char *dir, ATTR_UNUSED mode_t mode,
                 ATTR_UNUSED int *fd_out)
{
    return 1;
}

int fast_link(ATTR_UNUSED int fd, ATTR_UNUSED const char *path)
{
    errno = ENOSYS;
    return -1;
}

int fast_rename_noreplace(ATTR_UNUSED const char *from,
                          ATTR_UNUSED const char *to)
{
    return 1;
}

int fast_exchange(ATTR_UNUSED const char *a, ATTR_UNUSED const char *b)
{
    return 1;
}

void fast_writeback(ATTR_UNUSED int fd)
{
}

int fast_spool(ATTR_UNUSED int *fd_out)
{
    return 1;
}

int fast_splice(ATTR_UNUSED int dst_fd, ATTR_UNUSED int src_fd)
{
    return 1;
}

#endif

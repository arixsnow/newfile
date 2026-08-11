/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* fastio.c - bulk transfers the kernel can do without a buffer */

/* Must precede newfile.h, which pulls in system headers of its own. */
#if defined(__linux__) && !defined(NEWFILE_NO_FASTIO)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "newfile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__linux__) && !defined(NEWFILE_NO_FASTIO)

/*
 * fast_alloc - reserve size bytes for fd without writing them
 *
 * Leaves the file offset alone.  Returns 0, 1 if the filesystem cannot,
 * -1 with errno set.
 */
int fast_alloc(int fd, off_t size)
{
    static bool limit_known;
    static rlim_t size_limit;
    struct rlimit rl;

    if (size <= 0) {
        return 1;
    }

    if (!limit_known) {
        size_limit = (getrlimit(RLIMIT_FSIZE, &rl) == 0)
                     ? rl.rlim_cur : RLIM_INFINITY;
        limit_known = true;
    }

    /* fallocate(2) may not enforce RLIMIT_FSIZE; check it here */
    if (size_limit != RLIM_INFINITY
        && (uintmax_t)size > (uintmax_t)size_limit) {
        errno = EFBIG;
        return -1;
    }

    if (fallocate(fd, 0, 0, size) == 0) {
        return 0;
    }

    if (errno == EOPNOTSUPP || errno == ENOSYS || errno == EINVAL) {
        return 1;
    }

    return -1;
}

/*
 * fast_copy - copy src_fd to dst_fd inside the kernel
 *
 * Stops at the size read below.  Returns 0, 1 if unusable, -1 with
 * errno set.
 */
int fast_copy(int dst_fd, int src_fd)
{
    struct stat st;
    ssize_t ncopied;
    off_t limit;

    /* st_size 0 may still have content (procfs); fall back */
    if (fstat(src_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) {
        return 1;
    }

    limit = st.st_size;

    while (limit > 0) {
        ncopied = copy_file_range(src_fd, NULL, dst_fd, NULL,
                                  (size_t)limit, 0);
        if (ncopied < 0) {
            /* Both fds advanced; the read loop resumes there. */
            if (errno == EXDEV || errno == EINVAL || errno == ENOSYS
                || errno == EOPNOTSUPP || errno == EBADF
                || errno == EPERM || errno == ETXTBSY) {
                return 1;
            }

            return -1;
        }

        /* EOF, or a pre-5.19 kernel lying about it.  Fall back. */
        if (ncopied == 0) {
            return 1;
        }

        limit -= ncopied;
    }

    return 0;
}

#else

int fast_alloc(int fd, off_t size)
{
    (void)fd;
    (void)size;

    return 1;
}

int fast_copy(int dst_fd, int src_fd)
{
    (void)dst_fd;
    (void)src_fd;

    return 1;
}

#endif

/* SPDX-License-Identifier: BSD-3-Clause */
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
#include <unistd.h>

#if defined(__linux__) && !defined(NEWFILE_NO_FASTIO)

/*
 * fast_alloc - reserve len bytes at off for fd without writing them
 *
 * Leaves the file offset alone.  Returns 0, 1 if the filesystem cannot,
 * -1 with errno set.
 */
int fast_alloc(int fd, off_t off, off_t len)
{
    static bool limit_known;
    static rlim_t size_limit;
    struct rlimit rl;

    if (len <= 0) {
        return 1;
    }

    if (!limit_known) {
        size_limit = (getrlimit(RLIMIT_FSIZE, &rl) == 0)
                     ? rl.rlim_cur : RLIM_INFINITY;
        limit_known = true;
    }

    /* fallocate(2) may not enforce RLIMIT_FSIZE, so check it here */
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

/*
 * fast_copy - copy len bytes between descriptors inside the kernel
 *
 * Both offsets advance by the amount transferred.  Returns 0, 1 if
 * unusable, -1 with errno set.
 */
int fast_copy(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off,
              off_t len)
{
    ssize_t ncopied;

    while (len > 0) {
        ncopied = copy_file_range(src_fd, src_off, dst_fd, dst_off,
                                  (size_t)len, 0);
        if (ncopied < 0) {
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

        len -= ncopied;
    }

    return 0;
}

#else

int fast_alloc(ATTR_UNUSED int fd, ATTR_UNUSED off_t off,
               ATTR_UNUSED off_t len)
{
    return 1;
}

int fast_copy(ATTR_UNUSED int dst_fd, ATTR_UNUSED off_t *dst_off,
              ATTR_UNUSED int src_fd, ATTR_UNUSED off_t *src_off,
              ATTR_UNUSED off_t len)
{
    return 1;
}

#endif

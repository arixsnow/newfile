/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* io.c - whole writes, transfer buffers, and filling a range with NUL */

#include "newfile.h"
#include "io.h"
#include "fastio.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define IO_BUFSIZE_MIN 131072
#define IO_BUFSIZE_MAX 1048576

/* Returns 0, or -1 with errno set. */
int io_write(int fd, const char *buf, size_t len)
{
    ssize_t nwritten;
    size_t off;

    off = 0;
    while (off < len) {
        nwritten = write(fd, buf + off, len - off);
        if (nwritten == -1) {
            return -1;
        }
        if (nwritten == 0) {
            errno = EIO;
            return -1;
        }
        off += (size_t)nwritten;
    }

    return 0;
}

/* Leaves the file offset alone.  Returns 0, or -1 with errno set. */
int io_pwrite(int fd, const char *buf, size_t len, off_t off)
{
    ssize_t nwritten;
    size_t done;

    done = 0;
    while (done < len) {
        nwritten = pwrite(fd, buf + done, len - done, off + (off_t)done);
        if (nwritten == -1) {
            return -1;
        }
        if (nwritten == 0) {
            errno = EIO;
            return -1;
        }
        done += (size_t)nwritten;
    }

    return 0;
}

size_t io_bufsize(int fd)
{
    struct stat st;

    if (fstat(fd, &st) == 0 && st.st_blksize > IO_BUFSIZE_MIN) {
        return (st.st_blksize > IO_BUFSIZE_MAX) ? (size_t)IO_BUFSIZE_MAX
                                                : (size_t)st.st_blksize;
    }

    return IO_BUFSIZE_MIN;
}

/* The caller frees it.  NULL when out of memory. */
char *io_bufalloc(size_t size)
{
    void *buf;
    long page;

    page = sysconf(_SC_PAGESIZE);
    if (page > 0 && posix_memalign(&buf, (size_t)page, size) == 0) {
        return buf;
    }

    return malloc(size);
}

/* Leaves the file offset alone.  Returns 0, or -1 with errno set. */
int io_fill(int fd, off_t off, off_t len)
{
    char *buf;
    off_t pos, remaining;
    size_t bufsize, chunk;
    int ret, saved_errno;

    if (len <= 0) {
        return 0;
    }

    ret = fast_alloc(fd, off, len);
    if (ret <= 0) {
        return ret;
    }

    bufsize = io_bufsize(fd);
    if ((uintmax_t)len < (uintmax_t)bufsize) {
        bufsize = (size_t)len;
    }

    buf = calloc(bufsize, 1);
    if (buf == NULL) {
        return -1;
    }

    pos = off;
    remaining = len;
    while (remaining > 0) {
        chunk = ((uintmax_t)remaining > (uintmax_t)bufsize) ? bufsize
                                                            : (size_t)remaining;
        if (io_pwrite(fd, buf, chunk, pos) != 0) {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
        pos += (off_t)chunk;
        remaining -= (off_t)chunk;
    }

    free(buf);
    return 0;
}

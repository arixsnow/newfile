/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* template.c - what a new file starts out holding */

#include "newfile.h"
#include "template.h"
#include "error.h"
#include "fastio.h"
#include "holes.h"
#include "io.h"
#include "pending.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ST_NBLOCKSIZE 512
#define ZERO_BLOCK    4096

enum tmpl_kind {
    TMPL_EXTENTS,
    TMPL_ZEROS,
    TMPL_STREAM
};

struct tmpl {
    off_t size;
    enum tmpl_kind kind;
    int fd;
    bool own_fd;
    bool tail_hole;
};

struct copy_buf {
    char *data;
    size_t size;
};

static int copy_slow(int dst_fd, int src_fd, off_t *src_off, off_t *dst_off,
                     off_t end, struct copy_buf *cb);
static bool all_zero(const char *buf, size_t len);
static bool tail_zero(int fd, off_t size);
static int copy_zeros(int dst_fd, int src_fd, off_t size);
static int spool_open(void);
static int spool(int src_fd, int *fd_out, off_t *size_out);

/* Returns 0, or -1 with errno set. */
static int copy_stream(int dst_fd, int src_fd)
{
    char *buf;
    size_t bufsize;
    ssize_t nread;
    int saved_errno;

    bufsize = io_bufsize(dst_fd);
    buf = io_bufalloc(bufsize);
    if (buf == NULL) {
        return -1;
    }

    while ((nread = read(src_fd, buf, bufsize)) > 0) {
        if (io_write(dst_fd, buf, (size_t)nread) != 0) {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
    }

    if (nread == -1) {
        saved_errno = errno;
        free(buf);
        errno = saved_errno;
        return -1;
    }

    free(buf);
    return 0;
}

/* Returns 0, or -1 with errno set. */
static int copy_extents(int dst_fd, int src_fd, off_t size)
{
    struct copy_buf cb;
    off_t from, start, end, src_off, dst_off, written;
    bool overstated;
    int found, ret, saved_errno;

    cb.data = NULL;
    cb.size = 0;
    from = 0;
    written = 0;
    found = 0;
    overstated = false;

    while (!overstated
           && (found = hole_next_data(src_fd, from, size, &start, &end)) == 1) {
        src_off = start;
        dst_off = start;

        ret = fast_copy(dst_fd, &dst_off, src_fd, &src_off, end - start);
        if (ret >= 0) {
            ret = copy_slow(dst_fd, src_fd, &src_off, &dst_off, end, &cb);
        }
        if (ret < 0) {
            saved_errno = errno;
            free(cb.data);
            errno = saved_errno;
            return -1;
        }

        overstated = ret == 1;
        written = dst_off;
        from = end;
    }

    free(cb.data);

    if (found < 0) {
        return -1;
    }

    if (written < size && !overstated && ftruncate(dst_fd, size) != 0) {
        return -1;
    }

    return 0;
}

/* Returns 0, 1 when the source ends early, or -1 with errno set. */
static int copy_slow(int dst_fd, int src_fd, off_t *src_off, off_t *dst_off,
                     off_t end, struct copy_buf *cb)
{
    size_t dstsize, chunk;
    ssize_t nread;

    while (*src_off < end) {
        if (cb->data == NULL) {
            cb->size = io_bufsize(src_fd);
            dstsize = io_bufsize(dst_fd);
            if (dstsize > cb->size) {
                cb->size = dstsize;
            }

            cb->data = io_bufalloc(cb->size);
            if (cb->data == NULL) {
                return -1;
            }
        }

        chunk = ((uintmax_t)(end - *src_off) > (uintmax_t)cb->size)
                    ? cb->size
                    : (size_t)(end - *src_off);
        nread = pread(src_fd, cb->data, chunk, *src_off);
        if (nread < 0) {
            return -1;
        }
        if (nread == 0) {
            return 1;
        }

        if (io_pwrite(dst_fd, cb->data, (size_t)nread, *dst_off) != 0) {
            return -1;
        }

        *src_off += nread;
        *dst_off += nread;
    }

    return 0;
}

static bool all_zero(const char *buf, size_t len)
{
    return len == 0 || (buf[0] == 0 && memcmp(buf, buf + 1, len - 1) == 0);
}

static bool tail_zero(int fd, off_t size)
{
    char buf[ZERO_BLOCK];
    size_t len;

    len = (size < (off_t)sizeof buf) ? (size_t)size : sizeof buf;

    return pread(fd, buf, len, size - (off_t)len) == (ssize_t)len
           && all_zero(buf, len);
}

/* Returns 0, or -1 with errno set. */
static int copy_zeros(int dst_fd, int src_fd, off_t size)
{
    struct stat st;
    char *buf;
    size_t bufsize, blksize, chunk, i, len;
    ssize_t nread;
    off_t off;
    int saved_errno;

    bufsize = io_bufsize(src_fd);
    blksize = ZERO_BLOCK;
    if (fstat(dst_fd, &st) == 0 && st.st_blksize > 0
        && (size_t)st.st_blksize <= bufsize) {
        blksize = (size_t)st.st_blksize;
    }

    buf = io_bufalloc(bufsize);
    if (buf == NULL) {
        return -1;
    }

    off = 0;
    while (off < size) {
        chunk = ((uintmax_t)(size - off) > (uintmax_t)bufsize)
                    ? bufsize
                    : (size_t)(size - off);
        nread = pread(src_fd, buf, chunk, off);
        if (nread < 0) {
            goto fail;
        }

        if (nread == 0) {
            break;
        }

        for (i = 0; i < (size_t)nread; i += len) {
            len = ((size_t)nread - i < blksize) ? (size_t)nread - i : blksize;
            if (!all_zero(buf + i, len)
                && io_pwrite(dst_fd, buf + i, len, off + (off_t)i) != 0) {
                goto fail;
            }
        }
        off += nread;
    }

    free(buf);
    return ftruncate(dst_fd, off);

fail:
    saved_errno = errno;
    free(buf);
    errno = saved_errno;
    return -1;
}

/* Returns the descriptor, or -1 with errno set. */
static int spool_open(void)
{
    sigset_t old;
    const char *dir;
    char *path;
    size_t dirlen;
    int fd;

    switch (fast_spool(&fd)) {
    case 0:
        return fd;
    case 1:
        break;
    default:
        return -1;
    }

    dir = getenv("TMPDIR");
    if (dir == NULL || *dir == '\0') {
        dir = "/tmp";
    }

    dirlen = strlen(dir);
    path = malloc(dirlen + sizeof "/newfile.XXXXXX");
    if (path == NULL) {
        return -1;
    }
    memcpy(path, dir, dirlen);
    memcpy(path + dirlen, "/newfile.XXXXXX", sizeof "/newfile.XXXXXX");

    pending_hold(&old);
    fd = mkstemp(path);
    if (fd != -1 && unlink(path) != 0) {
        error_print("warning: cannot remove '%s': %s\n", path, strerror(errno));
    }
    pending_release(&old);
    free(path);

    return fd;
}

/* Returns 0, or -1 with errno set. */
static int spool(int src_fd, int *fd_out, off_t *size_out)
{
    struct stat st;
    int fd, ret, saved_errno;

    fd = spool_open();
    if (fd == -1) {
        return -1;
    }

    ret = fast_splice(fd, src_fd);
    if (ret == 1) {
        ret = copy_stream(fd, src_fd);
    }

    if (ret != 0 || fstat(fd, &st) != 0) {
        saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    *fd_out = fd;
    *size_out = st.st_size;
    return 0;
}

/* The caller closes it with tmpl_close().  NULL with errno set. */
struct tmpl *tmpl_open(const char *path, bool repeat)
{
    struct tmpl *tmpl;
    struct stat st;
    bool owned, at_start;
    int fd, spool_fd, saved_errno;

    tmpl = calloc(1, sizeof(*tmpl));
    if (tmpl == NULL) {
        return NULL;
    }

    if (strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
        owned = false;
        at_start = lseek(fd, 0, SEEK_CUR) == 0;
    } else {
        fd = open(path, O_RDONLY | O_NOCTTY | O_CLOEXEC);
        owned = true;
        at_start = true;
    }

    if (fd == -1 || fstat(fd, &st) != 0) {
        goto fail;
    }

    if (S_ISDIR(st.st_mode)) {
        errno = EISDIR;
        goto fail;
    }

    if (at_start && S_ISREG(st.st_mode) && st.st_size > 0) {
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
        tmpl->size = st.st_size;

        if (st.st_blocks < st.st_size / ST_NBLOCKSIZE && hole_unknown(fd)) {
            tmpl->kind = TMPL_ZEROS;
            tmpl->tail_hole = tail_zero(fd, st.st_size);
        } else {
            tmpl->kind = TMPL_EXTENTS;
            tmpl->tail_hole = hole_tail(fd, st.st_size);
        }
    } else if (repeat) {
        if (spool(fd, &spool_fd, &tmpl->size) != 0) {
            goto fail;
        }
        if (owned) {
            close(fd);
        }
        fd = spool_fd;
        owned = true;
        tmpl->kind = TMPL_EXTENTS;
    } else {
        tmpl->kind = TMPL_STREAM;
    }

    tmpl->fd = fd;
    tmpl->own_fd = owned;

    return tmpl;

fail:
    saved_errno = errno;
    if (owned && fd != -1) {
        close(fd);
    }
    free(tmpl);
    errno = saved_errno;
    return NULL;
}

int tmpl_apply(const struct tmpl *tmpl, int dst_fd)
{
    switch (tmpl->kind) {
    case TMPL_EXTENTS:
        return copy_extents(dst_fd, tmpl->fd, tmpl->size);
    case TMPL_ZEROS:
        return copy_zeros(dst_fd, tmpl->fd, tmpl->size);
    default:
        return copy_stream(dst_fd, tmpl->fd);
    }
}

bool tmpl_tail_hole(const struct tmpl *tmpl)
{
    return tmpl != NULL && tmpl->tail_hole;
}

void tmpl_close(struct tmpl *tmpl)
{
    if (tmpl == NULL) {
        return;
    }

    if (tmpl->own_fd) {
        close(tmpl->fd);
    } else if (tmpl->kind != TMPL_STREAM) {
        lseek(tmpl->fd, tmpl->size, SEEK_SET);
    }

    free(tmpl);
}

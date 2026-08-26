/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* template.c - what a new file starts out holding */

#include "newfile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Bounds on the transfer size a filesystem may ask for */
#define IO_BUFSIZE_MIN 131072
#define IO_BUFSIZE_MAX 1048576

/* How a template is applied to each new file */
enum tmpl_kind {
    TMPL_EXTENTS, /* a regular source, copied gap for gap */
    TMPL_STREAM,  /* no layout to reproduce, and one file wants it */
    TMPL_BUFFER   /* no layout to reproduce, and several files do */
};

struct tmpl {
    off_t size;
    char *buf;
    size_t len;
    enum tmpl_kind kind;
    int fd;      /* -1 once the source has been read into buf */
    bool own_fd; /* standard input is not ours to close */
    bool tail_hole;
};

/* write_buf - write len bytes from buf to dst_fd, retrying short
 * writes.  Returns 0, or -1. */
static int write_buf(int dst_fd, const char *buf, size_t len)
{
    ssize_t nwritten;
    size_t off;

    off = 0;
    while (off < len) {
        nwritten = write(dst_fd, buf + off, len - off);
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

/* write_at - write_buf() to a stated offset, leaving the file offset
 * alone.  Returns 0, or -1. */
static int write_at(int fd, const char *buf, size_t len, off_t off)
{
    ssize_t nwritten;
    size_t done;

    done = 0;
    while (done < len) {
        nwritten = pwrite(fd, buf + done, len - done,
                          off + (off_t)done);
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

/*
 * io_bufsize - the transfer size one descriptor prefers, bounded
 *
 * A filesystem states its preference through st_blksize.  The floor is
 * where a copy stops going faster for a larger buffer.  The cap keeps
 * a network mount reporting megabytes from setting the allocation.
 */
static size_t io_bufsize(int fd)
{
    struct stat st;

    if (fstat(fd, &st) == 0 && st.st_blksize > IO_BUFSIZE_MIN) {
        return (st.st_blksize > IO_BUFSIZE_MAX)
               ? (size_t)IO_BUFSIZE_MAX : (size_t)st.st_blksize;
    }

    return IO_BUFSIZE_MIN;
}

/* io_bufalloc - aligned, so no copy straddles a page at either end */
static char *io_bufalloc(size_t size)
{
    void *buf;
    long page;

    page = sysconf(_SC_PAGESIZE);
    if (page > 0 && posix_memalign(&buf, (size_t)page, size) == 0) {
        return buf;
    }

    return malloc(size);
}

/*
 * read_all - read src_fd to EOF into one heap buffer
 *
 * *buf_out is the caller's to free and is never NULL on success.
 * src_fd stays open.  Returns 0, or -1.
 */
static int read_all(int src_fd, char **buf_out, size_t *len_out)
{
    char *buf, *newbuf;
    size_t cap, len;
    ssize_t nread;
    int saved_errno;

    cap = IO_BUFSIZE_MIN;
    len = 0;
    buf = malloc(cap);
    if (buf == NULL) {
        return -1;
    }

    while ((nread = read(src_fd, buf + len, cap - len)) > 0) {
        len += (size_t)nread;
        if (len == cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                errno = ENOMEM;
                return -1;
            }
            cap *= 2;
            newbuf = realloc(buf, cap);
            if (newbuf == NULL) {
                saved_errno = errno;
                free(buf);
                errno = saved_errno;
                return -1;
            }
            buf = newbuf;
        }
    }

    if (nread == -1) {
        saved_errno = errno;
        free(buf);
        errno = saved_errno;
        return -1;
    }

    *buf_out = buf;
    *len_out = len;
    return 0;
}

/*
 * copy_stream - copy src_fd to dst_fd to end of input
 *
 * For sources with no layout to reproduce.  Returns 0, or -1.
 */
static int copy_stream(int dst_fd, int src_fd)
{
    char *buf;
    size_t bufsize;
    ssize_t nread;
    int saved_errno;

    /* The source is not a regular file, so it states no preference */
    bufsize = io_bufsize(dst_fd);
    buf = io_bufalloc(bufsize);
    if (buf == NULL) {
        return -1;
    }

    while ((nread = read(src_fd, buf, bufsize)) > 0) {
        if (write_buf(dst_fd, buf, (size_t)nread) != 0) {
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

/*
 * copy_extents - copy the written parts of src_fd to the same offsets
 *
 * Gaps in the source are left as gaps.  Both descriptors are addressed
 * explicitly, so neither file offset matters.  Returns 0, or -1.
 */
static int copy_extents(int dst_fd, int src_fd, off_t size)
{
    char *buf;
    size_t bufsize, dstsize, chunk;
    off_t from, start, end, src_off, dst_off, written;
    ssize_t nread;
    bool overstated;
    int found, saved_errno;

    buf = NULL;
    from = 0;
    written = 0;
    found = 0;
    overstated = false;

    while (!overstated
           && (found = hole_next_data(src_fd, from, size, &start,
                                      &end)) == 1) {
        src_off = start;
        dst_off = start;

        if (fast_copy(dst_fd, &dst_off, src_fd, &src_off,
                      end - start) < 0) {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }

        while (src_off < end) {
            if (buf == NULL) {
                /* Both sides move through it, so a size under either
                 * preference costs calls on that one. */
                bufsize = io_bufsize(src_fd);
                dstsize = io_bufsize(dst_fd);
                if (dstsize > bufsize) {
                    bufsize = dstsize;
                }

                buf = io_bufalloc(bufsize);
                if (buf == NULL) {
                    return -1;
                }
            }

            chunk = ((uintmax_t)(end - src_off) > (uintmax_t)bufsize)
                    ? bufsize : (size_t)(end - src_off);
            nread = pread(src_fd, buf, chunk, src_off);
            if (nread < 0) {
                saved_errno = errno;
                free(buf);
                errno = saved_errno;
                return -1;
            }

            /* st_size stood in for the content, as under sysfs */
            if (nread == 0) {
                overstated = true;
                break;
            }

            if (write_at(dst_fd, buf, (size_t)nread, dst_off) != 0) {
                saved_errno = errno;
                free(buf);
                errno = saved_errno;
                return -1;
            }

            src_off += nread;
            dst_off += nread;
        }

        written = dst_off;
        from = end;
    }

    free(buf);

    if (found < 0) {
        return -1;
    }

    /* A source ending in a gap keeps it */
    if (written < size && !overstated && ftruncate(dst_fd, size) != 0) {
        return -1;
    }

    return 0;
}

/*
 * tmpl_open - open path as the source for every file in the run
 *
 * A regular source with a usable size is copied extent by extent.
 * Anything else is read straight through, or read into memory first
 * when repeat says more than one file needs it.  The operand count is
 * consulted only in that second case, so it cannot decide a layout.
 *
 * The result is the caller's to close.  Returns NULL with errno set.
 */
struct tmpl *tmpl_open(const char *path, bool repeat)
{
    struct tmpl *tmpl;
    struct stat st;
    bool owned, at_start;
    int fd, saved_errno;

    tmpl = calloc(1, sizeof(*tmpl));
    if (tmpl == NULL) {
        return NULL;
    }

    if (strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
        owned = false;
        /* A partly read input has no layout left to reproduce */
        at_start = lseek(fd, 0, SEEK_CUR) == 0;
    } else {
        fd = open(path, O_RDONLY);
        owned = true;
        at_start = true;
    }

    if (fd == -1) {
        saved_errno = errno;
        free(tmpl);
        errno = saved_errno;
        return NULL;
    }

    /* A size of zero may still have content, as under procfs */
    if (at_start && fstat(fd, &st) == 0 && S_ISREG(st.st_mode)
        && st.st_size > 0) {
        /* Advisory, and it widens the read-ahead window */
        posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);

        tmpl->kind = TMPL_EXTENTS;
        tmpl->size = st.st_size;
        tmpl->tail_hole = hole_tail(fd, st.st_size);
    } else if (repeat) {
        if (read_all(fd, &tmpl->buf, &tmpl->len) != 0) {
            saved_errno = errno;
            if (owned) {
                close(fd);
            }
            free(tmpl);
            errno = saved_errno;
            return NULL;
        }

        tmpl->kind = TMPL_BUFFER;
        if (owned) {
            close(fd);
        }
        fd = -1;
        owned = false;
    } else {
        tmpl->kind = TMPL_STREAM;
    }

    tmpl->fd = fd;
    tmpl->own_fd = owned;

    return tmpl;
}

int tmpl_apply(const struct tmpl *tmpl, int dst_fd)
{
    switch (tmpl->kind) {
    case TMPL_EXTENTS:
        return copy_extents(dst_fd, tmpl->fd, tmpl->size);
    case TMPL_BUFFER:
        return write_buf(dst_fd, tmpl->buf, tmpl->len);
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
    }

    free(tmpl->buf);
    free(tmpl);
}

/*
 * fill_range - claim len bytes at off, reading as NUL
 *
 * Reserves the space if fast_alloc() can, otherwise writes it.  The
 * file offset is left alone.  Returns 0, or -1.
 */
int fill_range(int fd, off_t off, off_t len)
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
        chunk = ((uintmax_t)remaining > (uintmax_t)bufsize)
                ? bufsize : (size_t)remaining;
        if (write_at(fd, buf, chunk, pos) != 0) {
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

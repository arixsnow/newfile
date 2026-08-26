/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* holes.c - where a file's data sits, so a copy keeps its shape */

/*
 * Must precede newfile.h, which pulls in system headers of its own.
 * Not conditional on NEWFILE_NO_FASTIO: the shape of a copy cannot
 * depend on who performed it.
 */
#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include "newfile.h"
#include <errno.h>
#include <unistd.h>

#if defined(SEEK_DATA) && defined(SEEK_HOLE)

/*
 * hole_next_data - the first written extent at or after from
 *
 * A descriptor that cannot report its layout reports everything left as
 * written.  Returns 1 with *start and *end set, 0 past the last extent,
 * or -1 with errno set.
 */
int hole_next_data(int fd, off_t from, off_t size, off_t *start,
                   off_t *end)
{
    off_t data, gap;

    if (from >= size) {
        return 0;
    }

    data = lseek(fd, from, SEEK_DATA);
    if (data == -1) {
        if (errno == ENXIO) {
            return 0;
        }
        if (errno != EINVAL && errno != ENOTSUP) {
            return -1;
        }

        *start = from;
        *end = size;

        return 1;
    }

    gap = lseek(fd, data, SEEK_HOLE);
    if (gap == -1) {
        gap = size;
    }

    *start = data;
    *end = (gap > size) ? size : gap;

    return 1;
}

/* hole_tail - true when the last byte of the file lies in a gap */
bool hole_tail(int fd, off_t size)
{
    if (size <= 0) {
        return false;
    }

    if (lseek(fd, size - 1, SEEK_DATA) == -1) {
        return errno == ENXIO;
    }

    return false;
}

#else

int hole_next_data(ATTR_UNUSED int fd, off_t from, off_t size,
                   off_t *start, off_t *end)
{
    if (from >= size) {
        return 0;
    }

    *start = from;
    *end = size;

    return 1;
}

bool hole_tail(ATTR_UNUSED int fd, ATTR_UNUSED off_t size)
{
    return false;
}

#endif

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* holes.c - where a file's data sits, so a copy keeps its shape */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "newfile.h"
#include "holes.h"
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

static off_t seek_data(int fd, off_t from);

#ifdef NEWFILE_NO_SEEKHOLE

static off_t seek_data(ATTR_UNUSED int fd, ATTR_UNUSED off_t from)
{
    errno = EINVAL;
    return -1;
}

#else

static off_t seek_data(int fd, off_t from)
{
    return lseek(fd, from, SEEK_DATA);
}

#endif

/* Returns 1 with *start and *end set, 0 past the last extent, or -1. */
int hole_next_data(int fd, off_t from, off_t size, off_t *start, off_t *end)
{
    off_t data, gap;

    if (from >= size) {
        return 0;
    }

    data = seek_data(fd, from);
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

bool hole_tail(int fd, off_t size)
{
    if (size <= 0) {
        return false;
    }

    if (seek_data(fd, size - 1) == -1) {
        return errno == ENXIO;
    }

    return false;
}

bool hole_unknown(int fd)
{
    return seek_data(fd, 0) == -1 && (errno == EINVAL || errno == ENOTSUP);
}

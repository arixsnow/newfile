/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* gapcheck.c - whether newfile can see the gaps of a sparse file in DIR */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROBE_NAME    "/gapcheck.XXXXXX"
#define PROBE_GAP     1048576
#define ST_NBLOCKSIZE 512

static int fail(const char *what)
{
    fprintf(stderr, "gapcheck: %s: %s\n", what, strerror(errno));
    return 2;
}

int main(int argc, char **argv)
{
    struct stat st;
    char *path;
    size_t len;
    off_t data;
    int fd, ret;

    if (argc != 2) {
        fputs("usage: gapcheck DIR\n", stderr);
        return 2;
    }

    len = strlen(argv[1]);
    path = malloc(len + sizeof PROBE_NAME);
    if (path == NULL) {
        return fail("malloc");
    }
    memcpy(path, argv[1], len);
    memcpy(path + len, PROBE_NAME, sizeof PROBE_NAME);

    fd = mkstemp(path);
    if (fd == -1) {
        ret = fail(argv[1]);
        free(path);
        return ret;
    }
    unlink(path);
    free(path);

    if (pwrite(fd, "x", 1, PROBE_GAP) != 1 || fsync(fd) != 0
        || fstat(fd, &st) != 0) {
        ret = fail("probe file");
        close(fd);
        return ret;
    }

    if (st.st_blocks >= st.st_size / ST_NBLOCKSIZE) {
        close(fd);
        return 1;
    }

    data = lseek(fd, 0, SEEK_DATA);
    if (data == -1 && errno != EINVAL && errno != ENOTSUP) {
        ret = fail("SEEK_DATA");
        close(fd);
        return ret;
    }
    close(fd);

    return (data != 0) ? 0 : 1;
}

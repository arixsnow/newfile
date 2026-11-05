/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* paths.c - the directory part of a path, and making it exist */

#include "newfile.h"
#include "paths.h"
#include "flush.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int ensure_directory(const char *path);

/* Includes the trailing slash, 0 when path has none. */
size_t path_dir_len(const char *path)
{
    const char *slash;

    slash = strrchr(path, '/');

    return (slash != NULL) ? (size_t)(slash - path) + 1 : 0;
}

/* The caller frees it.  NULL when out of memory. */
char *path_dir(const char *path)
{
    char *dir;
    size_t dirlen;

    dirlen = path_dir_len(path);
    if (dirlen > 1) {
        dirlen--;
    }

    dir = malloc(dirlen + 2);
    if (dir == NULL) {
        return NULL;
    }

    if (dirlen == 0) {
        dir[0] = '.';
        dirlen = 1;
    } else {
        memcpy(dir, path, dirlen);
    }
    dir[dirlen] = '\0';

    return dir;
}

/* Returns 0, or -1 with errno set. */
int path_make_parents(const char *path)
{
    char *buf, *p, *parent;
    char held;
    int saved_errno;

    buf = strdup(path);
    if (buf == NULL) {
        return -1;
    }

    p = strrchr(buf, '/');
    if (p == NULL || p == buf) {
        free(buf);
        return 0;
    }
    *p = '\0';

    for (p = buf + 1;; p++) {
        if (*p != '/' && *p != '\0') {
            continue;
        }

        held = *p;
        *p = '\0';
        if (mkdir(buf, S_IRWXU | S_IRWXG | S_IRWXO) == 0) {
            parent = path_dir(buf);
            if (parent == NULL) {
                free(buf);
                return -1;
            }
            flush_set_add(parent);
            free(parent);
        } else if (errno != EEXIST || ensure_directory(buf) != 0) {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
        *p = held;

        if (held == '\0') {
            break;
        }
    }

    free(buf);
    return 0;
}

/* Returns 0, or -1 with errno set, ENOTDIR for a non-directory. */
static int ensure_directory(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }

    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    return 0;
}

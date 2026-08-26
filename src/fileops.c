/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* fileops.c - paths, atomic replacement and backups */

#include "newfile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Renames sharing one directory flush */
#define DIRSYNC_BATCH 64

static int find_next_backup_number(const char *filepath);
static int ensure_directory(const char *path);

/* Created but not committed.  Borrowed, never freed here */
static const char *volatile pending_path;

/* Renames waiting for the flush that makes them survive a crash */
static int dirsync_fd = -1;
static char *dirsync_name;
static int dirsync_pending;

/* dir_prefix_len - length of the directory part, with its slash */
static size_t dir_prefix_len(const char *path)
{
    const char *slash;

    slash = strrchr(path, '/');

    return (slash != NULL) ? (size_t)(slash - path) + 1 : 0;
}

/*
 * temp_open - open a 0600 temporary file beside target
 *
 * Same directory, so the committing rename cannot cross a filesystem.
 * *tmppath_out is the caller's to free.  Returns the fd, or -1.
 */
int temp_open(const char *target, char **tmppath_out)
{
    static const char stem[] = ".newfile.XXXXXX";
    char *tmppath;
    size_t dirlen;
    int fd, saved_errno;

    *tmppath_out = NULL;

    dirlen = dir_prefix_len(target);

    tmppath = malloc(dirlen + sizeof(stem));
    if (tmppath == NULL) {
        return -1;
    }

    memcpy(tmppath, target, dirlen);
    memcpy(tmppath + dirlen, stem, sizeof(stem));

    pending_path = tmppath;

    fd = mkstemp(tmppath);
    if (fd == -1) {
        saved_errno = errno;
        pending_path = NULL;
        free(tmppath);
        errno = saved_errno;
        return -1;
    }

    *tmppath_out = tmppath;
    return fd;
}

void pending_arm(const char *path)
{
    pending_path = path;
}

void pending_clear(void)
{
    pending_path = NULL;
}

void pending_discard(void)
{
    const char *path;

    path = pending_path;
    pending_path = NULL;
    if (path != NULL) {
        unlink(path);
    }
}

/* Runs from a signal handler, so it calls only unlink. */
void pending_cleanup(void)
{
    if (pending_path != NULL) {
        unlink(pending_path);
    }
}

/* dir_of - the directory part of path, as a string to free */
static char *dir_of(const char *path)
{
    char *dir;
    size_t dirlen;

    dirlen = dir_prefix_len(path);

    /* Strip the trailing slash, except for "/" */
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

/*
 * dirsync_flush - flush the renames noted since the last flush
 *
 * The batch is cleared either way.  Returns 0, or -1 with errno set.
 */
int dirsync_flush(void)
{
    int ret;

    /* Nothing is staged unless the descriptor is open. */
    if (dirsync_pending == 0) {
        return 0;
    }

    ret = fsync(dirsync_fd);
    dirsync_pending = 0;

    /* Not every filesystem can flush a directory. */
    if (ret != 0 && (errno == EINVAL || errno == ENOTSUP)) {
        ret = 0;
    }

    return ret;
}

/*
 * dirsync_stage - note a rename into the directory holding path
 *
 * One flush covers a run of renames into the same directory.  It runs
 * when the run ends or the batch fills.  Returns 0, or -1 with errno
 * set.
 */
int dirsync_stage(const char *path)
{
    char *dir;
    int fd, saved_errno;

    dir = dir_of(path);
    if (dir == NULL) {
        return -1;
    }

    if (dirsync_name != NULL && strcmp(dirsync_name, dir) != 0) {
        if (dirsync_flush() != 0) {
            saved_errno = errno;
            free(dir);
            errno = saved_errno;
            return -1;
        }
        close(dirsync_fd);
        dirsync_fd = -1;
        free(dirsync_name);
        dirsync_name = NULL;
    }

    if (dirsync_fd == -1) {
        fd = open(dir, O_RDONLY);
        if (fd == -1) {
            saved_errno = errno;
            free(dir);
            errno = saved_errno;
            /* Nothing to flush through a directory we cannot open */
            return (saved_errno == EACCES || saved_errno == EPERM)
                   ? 0 : -1;
        }
        dirsync_fd = fd;
        dirsync_name = dir;
    } else {
        free(dir);
    }

    dirsync_pending++;
    if (dirsync_pending >= DIRSYNC_BATCH) {
        return dirsync_flush();
    }

    return 0;
}

/* dirsync_dir - the directory a failed flush was covering, or NULL */
const char *dirsync_dir(void)
{
    return dirsync_name;
}

/* Runs after the last flush has been reported, so the name outlives it. */
void dirsync_release(void)
{
    if (dirsync_fd != -1) {
        close(dirsync_fd);
        dirsync_fd = -1;
    }

    free(dirsync_name);
    dirsync_name = NULL;
}

/*
 * make_parents - create the missing parent directories of path
 *
 * New directories get 0777 modified by the umask.  Fails if a component
 * exists and is not a directory.  Returns 0, or -1.
 */
int make_parents(const char *path)
{
    char *buf, *p;
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

    for (p = buf + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, 0777) != 0) {
                if (errno == EEXIST) {
                    if (ensure_directory(buf) != 0) {
                        saved_errno = errno;
                        free(buf);
                        errno = saved_errno;
                        return -1;
                    }
                } else {
                    saved_errno = errno;
                    free(buf);
                    errno = saved_errno;
                    return -1;
                }
            }
            *p = '/';
        }
    }

    if (mkdir(buf, 0777) != 0) {
        if (errno == EEXIST) {
            if (ensure_directory(buf) != 0) {
                saved_errno = errno;
                free(buf);
                errno = saved_errno;
                return -1;
            }
        } else {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
    }

    free(buf);
    return 0;
}

/* ensure_directory - 0 if path is a directory, else -1 with ENOTDIR */
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

/*
 * find_next_backup_number - smallest n >= 1 with no filepath.~n~
 *
 * Returns n, or -1 with EEXIST when every n below 10000 is taken.
 */
static int find_next_backup_number(const char *filepath)
{
    char *probe;
    struct stat st;
    size_t bufsz;
    int n;

    bufsz = strlen(filepath) + 16;
    probe = malloc(bufsz);
    if (probe == NULL) {
        return -1;
    }

    for (n = 1; n < 10000; n++) {
        snprintf(probe, bufsz, "%s.~%d~", filepath, n);
        /* lstat(2): a dangling symlink counts as taken */
        if (lstat(probe, &st) != 0) {
            free(probe);
            return n;
        }
    }

    free(probe);
    errno = EEXIST;
    return -1;
}

/*
 * make_backup - give filepath a second name before it is replaced
 *
 * control selects the name:
 *
 *   none, off        make no backup
 *   numbered, t      append .~n~ with the next free n
 *   simple, never    append ~
 *   existing, nil    numbered if any .~n~ exists, otherwise simple
 *
 * The backup is a hard link.  filepath never stops existing and the
 * backup keeps the original inode.  Only a
 * regular file is backed up.  Anything else fails with EISDIR for a
 * directory and EINVAL otherwise.  A filepath that does not exist is
 * not an error.  On success *backup_name_out holds the backup name for
 * the caller to free, or NULL when no backup was made.  Returns 0, or
 * -1 with errno set.
 */
int make_backup(const char *filepath, const char *control,
                char **backup_name_out)
{
    struct stat st;
    char *backup_name;
    size_t bufsz;
    int n;

    *backup_name_out = NULL;

    if (strcmp(control, "none") == 0 || strcmp(control, "off") == 0) {
        return 0;
    }

    if (lstat(filepath, &st) != 0) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        errno = S_ISDIR(st.st_mode) ? EISDIR : EINVAL;
        return -1;
    }

    bufsz = strlen(filepath) + 16;
    backup_name = malloc(bufsz);
    if (backup_name == NULL) {
        return -1;
    }

    if (strcmp(control, "numbered") == 0 || strcmp(control, "t") == 0) {
        n = find_next_backup_number(filepath);
        if (n < 0) {
            free(backup_name);
            return -1;
        }
        snprintf(backup_name, bufsz, "%s.~%d~", filepath, n);
    } else if (strcmp(control, "simple") == 0
               || strcmp(control, "never") == 0) {
        snprintf(backup_name, bufsz, "%s~", filepath);
    } else {
        snprintf(backup_name, bufsz, "%s.~1~", filepath);
        if (lstat(backup_name, &st) == 0) {
            n = find_next_backup_number(filepath);
            if (n < 0) {
                free(backup_name);
                return -1;
            }
            snprintf(backup_name, bufsz, "%s.~%d~", filepath, n);
        } else {
            snprintf(backup_name, bufsz, "%s~", filepath);
        }
    }

    /* link(2) fails on an existing name, so unlink first */
    if (link(filepath, backup_name) != 0) {
        if (errno != EEXIST
            || unlink(backup_name) != 0
            || link(filepath, backup_name) != 0) {
            free(backup_name);
            return -1;
        }
    }

    *backup_name_out = backup_name;
    return 0;
}

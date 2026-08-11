/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* fileops.c - file-system operations for newfile */

#include "newfile.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define IO_BUFSIZE 65536

static int find_next_backup_number(const char *filepath);
static int ensure_directory(const char *path);

/* Created but not committed.  Borrowed, never freed here */
static const char *volatile pending_path;

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

/*
 * sync_dir - fsync the directory holding path
 *
 * Makes a rename into it survive a crash.  Returns 0, or -1.
 */
int sync_dir(const char *path)
{
    char *dir;
    size_t dirlen;
    int fd, ret, saved_errno;

    dirlen = dir_prefix_len(path);

    /* Strip the trailing slash, except for "/" */
    if (dirlen > 1) {
        dirlen--;
    }

    dir = malloc(dirlen + 2);
    if (dir == NULL) {
        return -1;
    }

    if (dirlen == 0) {
        dir[0] = '.';
        dirlen = 1;
    } else {
        memcpy(dir, path, dirlen);
    }
    dir[dirlen] = '\0';

    fd = open(dir, O_RDONLY);
    free(dir);
    if (fd == -1) {
        if (errno == EACCES || errno == EPERM) {
            return 0;
        }
        return -1;
    }

    ret = fsync(fd);

    /* Not every filesystem can flush a directory. */
    if (ret != 0 && (errno == EINVAL || errno == ENOTSUP)) {
        ret = 0;
    }

    saved_errno = errno;
    if (close(fd) != 0 && ret == 0) {
        ret = -1;
        saved_errno = errno;
    }
    errno = saved_errno;

    return ret;
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

/* write_buf - write len bytes from buf to dst_fd, retrying short
 * writes.  Returns 0, or -1. */
int write_buf(int dst_fd, const char *buf, size_t len)
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

/*
 * read_all - read src_fd to EOF into one heap buffer
 *
 * *buf_out is the caller's to free and is never NULL on success.
 * src_fd stays open.  Returns 0, or -1.
 */
int read_all(int src_fd, char **buf_out, size_t *len_out)
{
    char *buf, *newbuf;
    size_t cap, len;
    ssize_t nread;
    int saved_errno;

    cap = IO_BUFSIZE;
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
 * copy_fd - copy src_fd to dst_fd, buffering if fast_copy() declines
 *
 * src_fd is read to EOF and left open.  Returns 0, or -1.
 */
int copy_fd(int dst_fd, int src_fd)
{
    char *buf;
    ssize_t nread;
    int ret, saved_errno;

    ret = fast_copy(dst_fd, src_fd);
    if (ret <= 0) {
        return ret;
    }

    buf = malloc(IO_BUFSIZE);
    if (buf == NULL) {
        return -1;
    }

    while ((nread = read(src_fd, buf, IO_BUFSIZE)) > 0) {
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

/* copy_template - copy template_path into dst_fd.  Returns 0, or -1. */
int copy_template(int dst_fd, const char *template_path)
{
    int src_fd, saved_errno;

    src_fd = open(template_path, O_RDONLY);
    if (src_fd == -1) {
        return -1;
    }

    if (copy_fd(dst_fd, src_fd) != 0) {
        saved_errno = errno;
        close(src_fd);
        errno = saved_errno;
        return -1;
    }

    if (close(src_fd) != 0) {
        return -1;
    }

    return 0;
}

/*
 * fill_file - give fd a size of size bytes reading as NUL
 *
 * Reserves the space if fast_alloc() can, otherwise writes it.  The
 * resulting file offset is unspecified.  Returns 0, or -1.
 */
int fill_file(int fd, off_t size)
{
    char *buf;
    off_t remaining;
    size_t chunk;
    ssize_t nwritten;
    int ret, saved_errno;

    ret = fast_alloc(fd, size);
    if (ret <= 0) {
        return ret;
    }

    buf = calloc(IO_BUFSIZE, 1);
    if (buf == NULL) {
        return -1;
    }

    remaining = size;
    while (remaining > 0) {
        chunk = (remaining > IO_BUFSIZE)
                ? IO_BUFSIZE : (size_t)remaining;
        nwritten = write(fd, buf, chunk);
        if (nwritten == -1) {
            saved_errno = errno;
            free(buf);
            errno = saved_errno;
            return -1;
        }
        if (nwritten == 0) {
            free(buf);
            errno = EIO;
            return -1;
        }
        remaining -= nwritten;
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

    /* link(2) fails on an existing name; unlink first */
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

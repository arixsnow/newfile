/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* place.c - how a file comes to hold its name */

#include "newfile.h"
#include "place.h"
#include "error.h"
#include "fastio.h"
#include "paths.h"
#include "pending.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEMP_STEM  ".newfile."
#define TEMP_RAND  6
#define TEMP_TRIES 100

#define SEED_PID_SHIFT 16
#define LCG_MUL        1103515245UL
#define LCG_INC        12345UL
#define LCG_OUT_SHIFT  16

int getentropy(void *buffer, size_t length);

struct make_ctx {
    const char *src;
    struct stat st;
    mode_t mode;
    int fd;
};

struct probe_entry {
    char *dir;
    mode_t granted;
};

static int create_excl(const char *path, mode_t mode, struct stat *st);
static void temp_fill(char *suffix);
static int temp_make(const char *target, size_t slot,
                     int (*make)(const char *name, struct make_ctx *mc),
                     struct make_ctx *mc, char **name_out);
static int make_open(const char *name, struct make_ctx *mc);
static int make_fdlink(const char *name, struct make_ctx *mc);
static int make_link(const char *name, struct make_ctx *mc);
static int place_over(const char *tmp, const char *target, size_t slot);

static struct probe_entry probes[NEWFILE_BATCH];
static size_t nprobes;

/* Returns the descriptor with *st filled, or -1 with errno set. */
static int create_excl(const char *path, mode_t mode, struct stat *st)
{
    int fd, saved_errno;

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd == -1 || fstat(fd, st) == 0) {
        return fd;
    }

    saved_errno = errno;
    close(fd);
    unlink(path);
    errno = saved_errno;
    return -1;
}

static void temp_fill(char *suffix)
{
    static const char chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    static unsigned long counter;
    unsigned char rnd[TEMP_RAND];
    unsigned long seed;
    size_t i;

    if (getentropy(rnd, sizeof rnd) != 0) {
        counter++;
        seed = ((unsigned long)getpid() << SEED_PID_SHIFT) ^ counter;
        for (i = 0; i < sizeof rnd; i++) {
            seed = (seed * LCG_MUL) + LCG_INC;
            rnd[i] = (unsigned char)(seed >> LCG_OUT_SHIFT);
        }
    }

    for (i = 0; i < TEMP_RAND; i++) {
        suffix[i] = chars[rnd[i] % (sizeof chars - 1)];
    }
}

/* *name_out is to free.  Returns 0, or -1 with errno set. */
static int temp_make(const char *target, size_t slot,
                     int (*make)(const char *name, struct make_ctx *mc),
                     struct make_ctx *mc, char **name_out)
{
    sigset_t old;
    char *name, *suffix;
    size_t dirlen;
    int ret, tries;

    dirlen = path_dir_len(target);
    name = malloc(dirlen + sizeof TEMP_STEM + TEMP_RAND);
    if (name == NULL) {
        return -1;
    }
    memcpy(name, target, dirlen);
    memcpy(name + dirlen, TEMP_STEM, sizeof TEMP_STEM - 1);
    suffix = name + dirlen + sizeof TEMP_STEM - 1;
    suffix[TEMP_RAND] = '\0';

    ret = -1;
    errno = EEXIST;
    for (tries = 0; tries < TEMP_TRIES; tries++) {
        temp_fill(suffix);
        pending_hold(&old);
        ret = make(name, mc);
        if (ret == 0) {
            pending_arm(slot, name, &mc->st);
        }
        pending_release(&old);
        if (ret == 0 || errno != EEXIST) {
            break;
        }
    }

    if (ret != 0) {
        free(name);
        return -1;
    }

    *name_out = name;
    return 0;
}

static int make_open(const char *name, struct make_ctx *mc)
{
    mc->fd = create_excl(name, mc->mode, &mc->st);

    return (mc->fd == -1) ? -1 : 0;
}

static int make_fdlink(const char *name, struct make_ctx *mc)
{
    return fast_link(mc->fd, name);
}

static int make_link(const char *name, struct make_ctx *mc)
{
    return link(mc->src, name);
}

/* Returns 0 with *fd_out set, or -1 with errno set. */
int place_create(const char *path, mode_t mode, size_t slot, int *fd_out)
{
    sigset_t old;
    struct stat st;
    int fd;

    pending_hold(&old);
    fd = create_excl(path, mode, &st);
    if (fd != -1) {
        pending_arm(slot, path, &st);
    }
    pending_release(&old);

    if (fd == -1) {
        return -1;
    }

    *fd_out = fd;
    return 0;
}

/* *tmppath_out is to free.  Returns 0, or -1 with errno set. */
int place_temp(const char *target, mode_t mode, size_t slot, int *fd_out,
               char **tmppath_out)
{
    struct make_ctx mc;

    mc.mode = mode;
    if (temp_make(target, slot, make_open, &mc, tmppath_out) != 0) {
        return -1;
    }

    *fd_out = mc.fd;
    return 0;
}

/* *tmppath_out is to free.  Returns 0, or -1 with errno set. */
int place_temp_link(int fd, const char *target, size_t slot, char **tmppath_out)
{
    struct make_ctx mc;

    if (fstat(fd, &mc.st) != 0) {
        return -1;
    }
    mc.fd = fd;

    return temp_make(target, slot, make_fdlink, &mc, tmppath_out);
}

/* *tmppath_out is to free.  Returns 0, or -1 with errno set. */
int place_temp_alias(const char *src, const struct stat *st, size_t slot,
                     char **tmppath_out)
{
    struct make_ctx mc;

    mc.src = src;
    mc.st = *st;

    return temp_make(src, slot, make_link, &mc, tmppath_out);
}

/* *linked_out is true when tmp still names the file.  Returns 0, or -1. */
int place_commit(const char *tmp, const char *target, size_t slot,
                 bool *linked_out)
{
    int ret;

    *linked_out = false;

    ret = fast_rename_noreplace(tmp, target);
    if (ret == 0) {
        pending_clear(slot);
        return 0;
    }
    if (ret < 0) {
        return -1;
    }

#ifndef NEWFILE_NO_LINK
    if (link(tmp, target) == 0) {
        *linked_out = true;
        return 0;
    }
    if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOSYS
        && errno != EINVAL) {
        return -1;
    }
#endif

    return place_over(tmp, target, slot);
}

/* Returns 0, or -1 with errno set. */
static int place_over(const char *tmp, const char *target, size_t slot)
{
    sigset_t old;
    struct stat st;
    int fd, saved_errno;

    pending_hold(&old);

    fd = create_excl(target, S_IRUSR | S_IWUSR, &st);
    if (fd == -1) {
        pending_release(&old);
        return -1;
    }
    close(fd);

    if (rename(tmp, target) == 0) {
        pending_clear(slot);
        pending_release(&old);
        return 0;
    }

    saved_errno = errno;
    if (pending_unlink(target, &st) < 0) {
        error_print("cannot remove '%s': %s\n", target, strerror(errno));
    }
    errno = saved_errno;
    pending_release(&old);
    return -1;
}

/* Returns 0, or -1 with errno set. */
int place_rename(const char *tmp, const char *target, size_t slot)
{
    sigset_t old;
    int ret;

    pending_hold(&old);
    ret = rename(tmp, target);
    if (ret == 0) {
        pending_clear(slot);
        pending_clear(PENDING_BACKUP);
    }
    pending_release(&old);

    return ret;
}

/* Returns 0, 1 if the filesystem cannot, or -1 with errno set. */
int place_exchange(const char *tmp, const char *target, size_t slot,
                   const struct stat *old_st)
{
    sigset_t old;
    int ret;

    pending_hold(&old);
    ret = fast_exchange(tmp, target);
    if (ret == 0) {
        pending_retarget(slot, old_st);
        pending_clear(PENDING_BACKUP);
    }
    pending_release(&old);

    return ret;
}

/*
 * *granted is the mode a file opened with mode gets beside target.  Returns 0,
 * 1 when the probe file is left behind and reported, or -1 with errno set.
 */
int place_probe_mode(const char *target, mode_t mode, size_t slot,
                     mode_t *granted)
{
    struct make_ctx mc;
    char *dir, *name;
    size_t i;
    bool left;
    int ret, saved_errno;

    dir = path_dir(target);
    if (dir == NULL) {
        return -1;
    }
    for (i = 0; i < nprobes; i++) {
        if (strcmp(probes[i].dir, dir) == 0) {
            *granted = probes[i].granted;
            free(dir);
            return 0;
        }
    }

    mc.mode = mode;
    if (temp_make(target, slot, make_open, &mc, &name) != 0) {
        saved_errno = errno;
        free(dir);
        errno = saved_errno;
        return -1;
    }

    ret = close(mc.fd);
    saved_errno = errno;
    left = pending_remove(slot) != 0;
    free(name);
    if (ret != 0) {
        free(dir);
        errno = saved_errno;
        return -1;
    }

    *granted = mc.st.st_mode;
    if (nprobes < NEWFILE_BATCH) {
        probes[nprobes].dir = dir;
        probes[nprobes].granted = mc.st.st_mode;
        nprobes++;
    } else {
        free(dir);
    }

    return left ? 1 : 0;
}

void place_probe_reset(void)
{
    size_t i;

    for (i = 0; i < nprobes; i++) {
        free(probes[i].dir);
    }
    nprobes = 0;
}

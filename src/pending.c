/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* pending.c - names this run made and has not committed */

#include "newfile.h"
#include "pending.h"
#include "error.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct pending_name {
    const char *volatile path;
    volatile dev_t dev;
    volatile ino_t ino;
};

static int remove_ours(const char *path, dev_t dev, ino_t ino);
static void cleanup_handler(int sig);

static struct pending_name pending[PENDING_SLOTS];

/*
 * Async-signal-safe.  Returns 0, 1 when path holds another file and is left,
 * or -1 with errno set.
 */
static int remove_ours(const char *path, dev_t dev, ino_t ino)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        return (errno == ENOENT) ? 0 : -1;
    }
    if (st.st_dev != dev || st.st_ino != ino) {
        return 1;
    }

    return (unlink(path) == 0 || errno == ENOENT) ? 0 : -1;
}

static void cleanup_handler(int sig)
{
    const char *path;
    size_t i;
    int saved_errno;

    saved_errno = errno;
    for (i = 0; i < PENDING_SLOTS; i++) {
        path = pending[i].path;
        if (path != NULL) {
            remove_ours(path, pending[i].dev, pending[i].ino);
        }
    }
    if (raise(sig) != 0) {
        _exit(EXIT_FAILURE);
    }
    errno = saved_errno;
}

void pending_install(void)
{
    static const int signals[] = {
        SIGALRM,   SIGINT, SIGQUIT, SIGHUP,  SIGTERM, SIGPIPE, SIGUSR1,
        SIGUSR2,   SIGILL, SIGTRAP, SIGABRT, SIGBUS,  SIGFPE,  SIGSEGV,
#ifdef SIGXCPU
        SIGXCPU,
#endif
#ifdef SIGSYS
        SIGSYS,
#endif
#ifdef SIGVTALRM
        SIGVTALRM,
#endif
#ifdef SIGPROF
        SIGPROF,
#endif
#ifdef SIGPOLL
        SIGPOLL,
#endif
#ifdef SIGPWR
        SIGPWR,
#endif
#ifdef SIGSTKFLT
        SIGSTKFLT,
#endif
    };
    struct sigaction sa, old;
    size_t i;

    sigfillset(&sa.sa_mask);

#ifdef SIGXFSZ
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGXFSZ, &sa, NULL);
#endif

    sa.sa_flags = (int)(SA_RESTART | SA_RESETHAND);
    sa.sa_handler = cleanup_handler;

    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (sigaction(signals[i], NULL, &old) == 0
            && old.sa_handler != SIG_IGN) {
            sigaction(signals[i], &sa, NULL);
        }
    }
}

void pending_hold(sigset_t *old)
{
    sigset_t all;

    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, old);
}

/* errno is kept. */
void pending_release(const sigset_t *old)
{
    int saved_errno;

    saved_errno = errno;
    pthread_sigmask(SIG_SETMASK, old, NULL);
    errno = saved_errno;
}

void pending_arm(size_t slot, const char *path, const struct stat *st)
{
    pending[slot].dev = st->st_dev;
    pending[slot].ino = st->st_ino;
    pending[slot].path = path;
}

/* Only with signals held. */
void pending_retarget(size_t slot, const struct stat *st)
{
    pending[slot].dev = st->st_dev;
    pending[slot].ino = st->st_ino;
}

void pending_clear(size_t slot)
{
    pending[slot].path = NULL;
}

/* Returns 0, or -1 after reporting a name left behind.  errno is kept. */
int pending_remove(size_t slot)
{
    const char *path;
    int ret, saved_errno;

    path = pending[slot].path;
    if (path == NULL) {
        return 0;
    }

    saved_errno = errno;
    ret = remove_ours(path, pending[slot].dev, pending[slot].ino);
    if (ret < 0) {
        error_print("cannot remove '%s': %s\n", path, strerror(errno));
    } else if (ret > 0) {
        error_print("kept '%s': it now holds a different file\n", path);
    }
    pending[slot].path = NULL;
    errno = saved_errno;

    return (ret == 0) ? 0 : -1;
}

/*
 * Removes path only while it holds st.  Returns 0, 1 when path holds another
 * file and is left, or -1 with errno set.
 */
int pending_unlink(const char *path, const struct stat *st)
{
    return remove_ours(path, st->st_dev, st->st_ino);
}

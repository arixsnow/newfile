/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* flush.c - make files, and the directories that name them, durable */

#include "newfile.h"
#include "flush.h"
#include "error.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(_POSIX_THREADS) && _POSIX_THREADS > 0 \
    && !defined(NEWFILE_NO_THREADS)
#define USE_THREADS 1
#include <pthread.h>
#include <signal.h>

#define FLUSHES_PER_CPU 4

struct flush_job {
    const int *fds;
    int *errs;
    size_t n;
    size_t next;
    size_t done;
    bool dir;
};
#endif

struct dir_failure {
    char *name;
    dev_t dev;
    ino_t ino;
    bool known;
};

static int sync_err(int err, bool dir);
static int sync_one(int fd, bool dir);
static void dir_failed(const char *name, int err, bool known, dev_t dev,
                       ino_t ino);

static char *dirs[NEWFILE_BATCH];
static size_t ndirs;
static size_t dirs_max = 1;
static size_t dirs_nfailed;

static struct dir_failure failures[NEWFILE_BATCH];
static size_t nfailures;
static bool failures_lost;

#ifdef USE_THREADS

static void flush_threaded(const int *fds, int *errs, size_t n, bool dir);
static void flush_take(void);
static void *flush_worker(void *arg);
static size_t flush_want(size_t n);
static void flush_grow(size_t want);

static pthread_mutex_t job_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t job_done = PTHREAD_COND_INITIALIZER;
static struct flush_job job;
static pthread_t workers[NEWFILE_BATCH];
static size_t nworkers;
static bool grow_failed;
static bool stopping;

#endif

static int sync_err(int err, bool dir)
{
    if (dir && (err == EINVAL || err == ENOTSUP)) {
        return 0;
    }

    return err;
}

static int sync_one(int fd, bool dir)
{
    int ret;

    do {
        ret = fsync(fd);
    } while (ret != 0 && errno == EINTR);

    return (ret == 0) ? 0 : sync_err(errno, dir);
}

#ifdef USE_THREADS

static void flush_threaded(const int *fds, int *errs, size_t n, bool dir)
{
    size_t i;

    flush_grow(flush_want(n));
    if (nworkers == 0) {
        for (i = 0; i < n; i++) {
            errs[i] = sync_one(fds[i], dir);
        }
        return;
    }

    pthread_mutex_lock(&job_lock);
    job.fds = fds;
    job.errs = errs;
    job.n = n;
    job.next = 0;
    job.done = 0;
    job.dir = dir;
    pthread_cond_broadcast(&job_ready);

    while (job.done < job.n) {
        pthread_cond_wait(&job_done, &job_lock);
    }

    job.n = 0;
    job.next = 0;
    pthread_mutex_unlock(&job_lock);
}

/* Called, and returns, with job_lock held. */
static void flush_take(void)
{
    const int *fds;
    size_t i;
    bool dir;
    int err;

    i = job.next++;
    fds = job.fds;
    dir = job.dir;
    pthread_mutex_unlock(&job_lock);

    err = sync_one(fds[i], dir);

    pthread_mutex_lock(&job_lock);
    job.errs[i] = err;
    job.done++;
    if (job.done == job.n) {
        pthread_cond_signal(&job_done);
    }
}

static void *flush_worker(ATTR_UNUSED void *arg)
{
    pthread_mutex_lock(&job_lock);
    while (1) {
        while (!stopping && job.next >= job.n) {
            pthread_cond_wait(&job_ready, &job_lock);
        }
        if (stopping) {
            break;
        }
        flush_take();
    }
    pthread_mutex_unlock(&job_lock);

    return NULL;
}

/* Workers to flush n descriptors at once. */
static size_t flush_want(size_t n)
{
    static long cpus;
    size_t limit;

    if (cpus == 0) {
        cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (cpus < 1) {
            cpus = 1;
        }
    }
    limit = (size_t)cpus * FLUSHES_PER_CPU;

    return (n < limit) ? n : limit;
}

static void flush_grow(size_t want)
{
    sigset_t all, old;

    if (grow_failed || want <= nworkers) {
        return;
    }

    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, &old);
    while (nworkers < want) {
        if (pthread_create(&workers[nworkers], NULL, flush_worker, NULL) != 0) {
            grow_failed = true;
            break;
        }
        nworkers++;
    }
    pthread_sigmask(SIG_SETMASK, &old, NULL);
}

#endif

/* errs[i] gets 0 or the error for fds[i].  n is at most NEWFILE_BATCH. */
void flush_fds(const int *fds, int *errs, size_t n, bool dir)
{
    size_t i;

#ifdef USE_THREADS
    if (n > 1) {
        flush_threaded(fds, errs, n, dir);
        return;
    }
#endif

    for (i = 0; i < n; i++) {
        errs[i] = sync_one(fds[i], dir);
    }
}

void flush_stop(void)
{
#ifdef USE_THREADS
    size_t i;

    pthread_mutex_lock(&job_lock);
    stopping = true;
    pthread_cond_broadcast(&job_ready);
    pthread_mutex_unlock(&job_lock);

    for (i = 0; i < nworkers; i++) {
        pthread_join(workers[i], NULL);
    }
    nworkers = 0;
    grow_failed = false;
    stopping = false;
#endif
}

/* Returns 0, or -1 with errno set. */
int flush_dir(const char *dir)
{
    int fd, err;

    fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd == -1) {
        return (errno == EACCES || errno == EPERM) ? 0 : -1;
    }

    flush_fds(&fd, &err, 1, true);
    close(fd);

    if (err != 0) {
        errno = err;
        return -1;
    }

    return 0;
}

void flush_set_init(size_t max)
{
    dirs_max = (max > NEWFILE_BATCH) ? NEWFILE_BATCH : max;
}

void flush_set_add(const char *dir)
{
    char *copy;
    size_t i;

    for (i = 0; i < ndirs; i++) {
        if (strcmp(dirs[i], dir) == 0) {
            return;
        }
    }

    if (ndirs == dirs_max) {
        flush_set_run();
    }

    copy = strdup(dir);
    if (copy == NULL) {
        if (flush_dir(dir) != 0) {
            dir_failed(dir, errno, false, 0, 0);
        }
        return;
    }

    dirs[ndirs++] = copy;
}

void flush_set_run(void)
{
    struct stat st;
    char *names[NEWFILE_BATCH];
    int fds[NEWFILE_BATCH], errs[NEWFILE_BATCH];
    dev_t devs[NEWFILE_BATCH];
    ino_t inos[NEWFILE_BATCH];
    bool known[NEWFILE_BATCH];
    size_t i, j, n;
    int fd;

    n = 0;
    for (i = 0; i < ndirs; i++) {
        fd = open(dirs[i], O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd == -1) {
            if (errno != EACCES && errno != EPERM) {
                dir_failed(dirs[i], errno, false, 0, 0);
            }
            free(dirs[i]);
            continue;
        }

        known[n] = fstat(fd, &st) == 0;
        if (known[n]) {
            for (j = 0; j < n; j++) {
                if (known[j] && devs[j] == st.st_dev && inos[j] == st.st_ino) {
                    break;
                }
            }
            if (j < n) {
                close(fd);
                free(dirs[i]);
                continue;
            }
            devs[n] = st.st_dev;
            inos[n] = st.st_ino;
        } else {
            devs[n] = 0;
            inos[n] = 0;
        }

        names[n] = dirs[i];
        fds[n++] = fd;
    }
    ndirs = 0;

    if (n == 0) {
        return;
    }

    flush_fds(fds, errs, n, true);

    for (i = 0; i < n; i++) {
        if (errs[i] != 0) {
            dir_failed(names[i], errs[i], known[i], devs[i], inos[i]);
        }
        close(fds[i]);
        free(names[i]);
    }
}

static void dir_failed(const char *name, int err, bool known, dev_t dev,
                       ino_t ino)
{
    struct dir_failure *f;

    error_print("cannot flush directory '%s': %s\n", name, strerror(err));
    dirs_nfailed++;

    if (nfailures == NEWFILE_BATCH) {
        failures_lost = true;
        return;
    }
    f = &failures[nfailures];
    f->name = strdup(name);
    if (f->name == NULL) {
        failures_lost = true;
        return;
    }
    f->known = known;
    f->dev = dev;
    f->ino = ino;
    nfailures++;
}

/* True when dir failed to flush since flush_set_forget(), or might have. */
bool flush_set_failed(const char *dir)
{
    struct stat st;
    bool stated;
    size_t i;

    if (failures_lost) {
        return true;
    }

    stated = false;
    for (i = 0; i < nfailures; i++) {
        if (strcmp(failures[i].name, dir) == 0) {
            return true;
        }
        if (!failures[i].known) {
            continue;
        }
        if (!stated) {
            if (stat(dir, &st) != 0) {
                return true;
            }
            stated = true;
        }
        if (st.st_dev == failures[i].dev && st.st_ino == failures[i].ino) {
            return true;
        }
    }

    return false;
}

void flush_set_forget(void)
{
    size_t i;

    for (i = 0; i < nfailures; i++) {
        free(failures[i].name);
    }
    nfailures = 0;
    failures_lost = false;
}

size_t flush_set_failures(void)
{
    return dirs_nfailed;
}

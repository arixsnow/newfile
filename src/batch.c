/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* batch.c - build, flush, name and report the operands, a batch at a time */

#include "newfile.h"
#include "batch.h"
#include "backup.h"
#include "error.h"
#include "fastio.h"
#include "flush.h"
#include "io.h"
#include "paths.h"
#include "pending.h"
#include "place.h"
#include "spec.h"
#include "template.h"
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#define FD_RESERVE 16

enum op_way {
    WAY_PLAIN,
    WAY_UNNAMED,
    WAY_TEMP
};

struct op {
    const char *path;
    char *tmppath;
    char *backup_name;
    char *spare;
    const char *fail;
    int err;
    int fd;
    enum op_way way;
    bool leftover;
};

static size_t batch_size(void);
static void op_fail(struct op *op, size_t slot, const char *what);
static void build_one(struct op *op, size_t slot, const struct file_spec *spec);
static int open_built(struct op *op, size_t slot, const struct file_spec *spec,
                      mode_t *granted);
static void finish_built(struct op *op, size_t slot,
                         const struct file_spec *spec, bool content,
                         mode_t granted);
static int fill_one(int fd, const struct file_spec *spec);
static size_t flush_ops(struct op *ops, size_t n, size_t *idx);
static int close_op(struct op *op, size_t slot, const char *what);
static void flush_batch(struct op *ops, size_t n);
static void name_one(struct op *op, size_t slot, const struct file_spec *spec);
static void settle_batch(struct op *ops, size_t n);
static int replace_one(struct op *op, size_t slot, const char *dir,
                       const struct file_spec *spec);
static bool report_one(struct op *op, const struct file_spec *spec);
static int apply_size(int fd, off_t target, bool sparse, bool tail_hole,
                      bool filled);
static void drop_spares(struct op *ops, size_t n);
static bool dir_unflushed(const char *path);

static sigset_t spare_mask;
static bool spare_held;

/* Returns true when every file was made and every directory flushed. */
bool batch_run(char *const *paths, size_t npaths, const struct file_spec *spec)
{
    struct op *ops;
    bool ok;
    size_t batch, done, n, i;

    batch = batch_size();
    flush_set_init(batch);
    ops = calloc(batch, sizeof(*ops));
    if (ops == NULL) {
        error_exit("%s\n", strerror(errno));
    }

    ok = true;
    for (done = 0; done < npaths; done += n) {
        n = npaths - done;
        if (n > batch) {
            n = batch;
        }
        flush_set_forget();
        place_probe_reset();

        for (i = 0; i < n; i++) {
            ops[i].path = paths[done + i];
            build_one(&ops[i], i, spec);
        }

        flush_batch(ops, n);

        backup_cache_reset();
        for (i = 0; i < n; i++) {
            name_one(&ops[i], i, spec);
        }

        settle_batch(ops, n);
        flush_set_run();
        drop_spares(ops, n);

        for (i = 0; i < n; i++) {
            if (!report_one(&ops[i], spec)) {
                ok = false;
            }
        }
    }

    flush_stop();
    flush_set_forget();
    place_probe_reset();
    backup_cache_reset();
    free(ops);

    return ok && flush_set_failures() == 0;
}

static void drop_spares(struct op *ops, size_t n)
{
    struct op *op;
    size_t i;

    for (i = 0; i < n; i++) {
        op = &ops[i];
        if (op->spare == NULL) {
            continue;
        }

        if (dir_unflushed(op->path)) {
            error_print("warning: kept '%s', the old '%s'\n", op->spare,
                        op->path);
            pending_clear(i);
        } else if (pending_remove(i) != 0) {
            op->leftover = true;
        }
        free(op->spare);
        op->spare = NULL;
    }

    if (spare_held) {
        spare_held = false;
        pending_release(&spare_mask);
    }
}

/* True when the directory holding path failed to flush in this batch. */
static bool dir_unflushed(const char *path)
{
    char *dir;
    bool failed;

    dir = path_dir(path);
    if (dir == NULL) {
        return true;
    }
    failed = flush_set_failed(dir);
    free(dir);

    return failed;
}

static size_t batch_size(void)
{
    struct rlimit rl;
    rlim_t room;

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY
        || rl.rlim_cur >= (2 * NEWFILE_BATCH) + FD_RESERVE) {
        return NEWFILE_BATCH;
    }

    room = (rl.rlim_cur > FD_RESERVE) ? (rl.rlim_cur - FD_RESERVE) / 2 : 0;

    return (room > 0) ? (size_t)room : 1;
}

/* errno is kept. */
static void op_fail(struct op *op, size_t slot, const char *what)
{
    int saved_errno;

    saved_errno = errno;
    op->fail = what;
    op->err = saved_errno;

    if (op->fd != -1) {
        close(op->fd);
        op->fd = -1;
    }

    if (op->spare == NULL) {
        pending_remove(slot);
    }
    free(op->tmppath);
    op->tmppath = NULL;

    errno = saved_errno;
}

static void build_one(struct op *op, size_t slot, const struct file_spec *spec)
{
    struct stat st;
    mode_t granted;
    size_t len;
    bool content;

    op->tmppath = NULL;
    op->backup_name = NULL;
    op->spare = NULL;
    op->fail = NULL;
    op->err = 0;
    op->fd = -1;
    op->way = WAY_PLAIN;
    op->leftover = false;

    len = strlen(op->path);
    if (len == 0 || op->path[len - 1] == '/') {
        errno = (len == 0) ? ENOENT : EISDIR;
        op_fail(op, slot, "cannot create");
        return;
    }

    if (spec->parents_mode && path_make_parents(op->path) != 0) {
        op_fail(op, slot, "cannot create parent directories for");
        return;
    }

    content = spec->tmpl != NULL || spec->size_given;

    if (!content && !spec->set_owner && !spec->absolute_given
        && !spec->force_mode) {
        if (place_create(op->path, spec->mode, slot, &op->fd) != 0) {
            op_fail(op, slot, "cannot create");
        }
        return;
    }

    if (content && !spec->force_mode && lstat(op->path, &st) == 0) {
        errno = EEXIST;
        op_fail(op, slot, "cannot create");
        return;
    }

    if (open_built(op, slot, spec, &granted) == 0) {
        finish_built(op, slot, spec, content, granted);
    }
}

/*
 * *granted is the mode a new file gets beside op->path.  Returns 0, or -1 with
 * the failure recorded in op.
 */
static int open_built(struct op *op, size_t slot, const struct file_spec *spec,
                      mode_t *granted)
{
    struct stat st;
    mode_t open_mode;
    char *dir;
    int ret;

    open_mode = spec->absolute_given ? S_IRUSR | S_IWUSR : spec->mode;
    *granted = spec->mode;

    dir = path_dir(op->path);
    if (dir == NULL) {
        op_fail(op, slot, "cannot create");
        return -1;
    }
    ret = fast_tmpfile(dir, open_mode, &op->fd);
    free(dir);

    if (ret == 0) {
        op->way = WAY_UNNAMED;
        if (fstat(op->fd, &st) == 0) {
            *granted = st.st_mode;
            return 0;
        }
    } else if (ret == 1) {
        op->way = WAY_TEMP;
        ret = spec->absolute_given
                  ? 0
                  : place_probe_mode(op->path, open_mode, slot, granted);
        if (ret >= 0) {
            if (ret == 1) {
                op->leftover = true;
            }
            if (place_temp(op->path, S_IRUSR | S_IWUSR, slot, &op->fd,
                           &op->tmppath)
                == 0) {
                return 0;
            }
        }
    }

    op_fail(op, slot, "cannot create");
    return -1;
}

static void finish_built(struct op *op, size_t slot,
                         const struct file_spec *spec, bool content,
                         mode_t granted)
{
    mode_t final_mode;
    int ret;

    final_mode = spec->absolute_given
                     ? spec->mode
                     : (granted & (S_IRWXU | S_IRWXG | S_IRWXO))
                           | (spec->mode & (S_ISUID | S_ISGID | S_ISVTX));

    if (op->way == WAY_UNNAMED && spec->uid_changes) {
        if (fchmod(op->fd, S_IRUSR | S_IWUSR) != 0) {
            op_fail(op, slot, "cannot set permissions of");
            return;
        }
        if (place_temp_link(op->fd, op->path, slot, &op->tmppath) != 0) {
            op_fail(op, slot, "cannot create");
            return;
        }
        op->way = WAY_TEMP;
    }

    ret = fill_one(op->fd, spec);
    if (ret != 0) {
        op_fail(op, slot,
                (ret == 1) ? "cannot copy template to" : "cannot set size of");
        return;
    }

    if (spec->set_owner
        && fchown(op->fd, spec->set_uid ? spec->uid : (uid_t)-1,
                  spec->set_gid ? spec->gid : (gid_t)-1)
               != 0) {
        op_fail(op, slot, "cannot change ownership of");
        return;
    }

    if (fchmod(op->fd, final_mode) != 0) {
        op_fail(op, slot, "cannot set permissions of");
        return;
    }

    if (content) {
        fast_writeback(op->fd);
    }
}

/* Returns 0, or 1 or 2 for the step that failed, with errno set. */
static int fill_one(int fd, const struct file_spec *spec)
{
    if (spec->tmpl != NULL && tmpl_apply(spec->tmpl, fd) != 0) {
        return 1;
    }

    if (spec->size_given
        && apply_size(fd, spec->size, spec->sparse_mode,
                      tmpl_tail_hole(spec->tmpl), spec->tmpl != NULL)
               != 0) {
        return 2;
    }

    return 0;
}

/* Fails each op whose flush fails.  Returns the others' count, in idx. */
static size_t flush_ops(struct op *ops, size_t n, size_t *idx)
{
    int fds[NEWFILE_BATCH], errs[NEWFILE_BATCH];
    size_t i, k, kept;

    k = 0;
    for (i = 0; i < n; i++) {
        if (ops[i].fail == NULL && ops[i].fd != -1) {
            fds[k] = ops[i].fd;
            idx[k++] = i;
        }
    }

    if (k > 0) {
        flush_fds(fds, errs, k, false);
    }

    kept = 0;
    for (i = 0; i < k; i++) {
        if (errs[i] != 0) {
            errno = errs[i];
            op_fail(&ops[idx[i]], idx[i], "cannot flush");
        } else {
            idx[kept++] = idx[i];
        }
    }

    return kept;
}

/* Returns 0, or -1 with the op failed as what. */
static int close_op(struct op *op, size_t slot, const char *what)
{
    int fd;

    fd = op->fd;
    op->fd = -1;
    if (close(fd) != 0) {
        op_fail(op, slot, what);
        return -1;
    }

    return 0;
}

static void flush_batch(struct op *ops, size_t n)
{
    size_t idx[NEWFILE_BATCH];
    size_t i, k;

    k = flush_ops(ops, n, idx);
    for (i = 0; i < k; i++) {
        if (ops[idx[i]].way == WAY_PLAIN) {
            close_op(&ops[idx[i]], idx[i], "cannot write");
        }
    }
}

static void name_one(struct op *op, size_t slot, const struct file_spec *spec)
{
    char *dir;
    bool linked;

    if (op->fail != NULL) {
        return;
    }

    dir = path_dir(op->path);
    if (dir == NULL) {
        op_fail(op, slot, "cannot create");
        return;
    }

    if (spec->force_mode) {
        if (op->way == WAY_UNNAMED
            && place_temp_link(op->fd, op->path, slot, &op->tmppath) != 0) {
            op_fail(op, slot, "cannot create");
            free(dir);
            return;
        }
        if (replace_one(op, slot, dir, spec) != 0) {
            free(dir);
            return;
        }
    } else if (op->way == WAY_UNNAMED) {
        if (fast_link(op->fd, op->path) != 0) {
            op_fail(op, slot, "cannot create");
            free(dir);
            return;
        }
    } else if (op->way == WAY_TEMP) {
        if (place_commit(op->tmppath, op->path, slot, &linked) != 0) {
            op_fail(op, slot, "cannot create");
            free(dir);
            return;
        }
        if (!linked) {
            free(op->tmppath);
            op->tmppath = NULL;
        }
    }

    if (op->way == WAY_PLAIN) {
        pending_clear(slot);
    }

    flush_set_add(dir);
    free(dir);
}

static void settle_batch(struct op *ops, size_t n)
{
    size_t idx[NEWFILE_BATCH];
    size_t i, k;
    struct op *op;

    k = flush_ops(ops, n, idx);
    for (i = 0; i < k; i++) {
        op = &ops[idx[i]];
        if (close_op(op, idx[i], "cannot close") != 0) {
            continue;
        }

        if (op->tmppath != NULL) {
            if (pending_remove(idx[i]) != 0) {
                op->leftover = true;
            }
            free(op->tmppath);
            op->tmppath = NULL;
        }
    }
}

/* Returns 0, or -1 with the failure recorded in op. */
static int replace_one(struct op *op, size_t slot, const char *dir,
                       const struct file_spec *spec)
{
    struct stat old;
    bool held;
    int ret;

    if (spec->backup_given) {
        ret = backup_make(op->path, spec->backup_control, &op->backup_name,
                          &old);
        if (ret < 0) {
            op_fail(op, slot, "cannot back up");
            return -1;
        }
        if (ret == 1) {
            op->leftover = true;
        }
    }

    ret = 1;
    if (op->backup_name != NULL) {
        held = spare_held;
        if (!held) {
            pending_hold(&spare_mask);
            spare_held = true;
        }
        ret = place_exchange(op->tmppath, op->path, slot, &old);
        if (ret == 0) {
            op->spare = op->tmppath;
            op->tmppath = NULL;
            return 0;
        }
        if (!held) {
            spare_held = false;
            pending_release(&spare_mask);
        }
        if (ret == 1 && flush_dir(dir) != 0) {
            pending_remove(PENDING_BACKUP);
            op_fail(op, slot, "cannot back up");
            return -1;
        }
    }

    if (ret < 0 || place_rename(op->tmppath, op->path, slot) != 0) {
        pending_remove(PENDING_BACKUP);
        op_fail(op, slot, "cannot replace");
        return -1;
    }

    free(op->tmppath);
    op->tmppath = NULL;

    return 0;
}

/* Returns true when op succeeded and left no temporary name behind. */
static bool report_one(struct op *op, const struct file_spec *spec)
{
    bool ok;

    ok = op->fail == NULL;
    if (!ok) {
        error_print("%s '%s': %s\n", op->fail, op->path, strerror(op->err));
    } else if (spec->verbose && op->backup_name != NULL) {
        printf("created '%s' (backup: '%s')\n", op->path, op->backup_name);
    } else if (spec->verbose) {
        printf("created '%s'\n", op->path);
    }

    free(op->backup_name);
    op->backup_name = NULL;

    return ok && !op->leftover;
}

/* fd is empty unless filled.  Returns 0, or -1 with errno set. */
static int apply_size(int fd, off_t target, bool sparse, bool tail_hole,
                      bool filled)
{
    struct stat st;
    off_t size;

    size = 0;
    if (filled) {
        if (fstat(fd, &st) != 0) {
            return -1;
        }
        size = st.st_size;
    }

    if (target <= size || sparse || tail_hole) {
        return ftruncate(fd, target);
    }

    return io_fill(fd, size, target - size);
}

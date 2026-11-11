/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* backup.c - a second name for a file about to be replaced */

#include "newfile.h"
#include "backup.h"
#include "paths.h"
#include "pending.h"
#include "place.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LIST_MIN 16

struct backup_entry {
    char *base;
    char *digits;
};

struct backup_dir {
    char *dir;
    struct backup_entry *entries;
    size_t n;
};

static int digits_cmp(const char *a, const char *b);
static int digits_inc(char **digits);
static int entry_cmp(const void *a, const void *b);
static bool backup_split(const char *name, size_t *baselen,
                         const char **digits);
static int backup_dir_list(struct backup_dir *bd);
static void entries_keep_highest(struct backup_dir *bd);
static void backup_dir_free(struct backup_dir *bd);
static struct backup_dir *backup_dir_get(const char *filepath);
static int backup_best(const char *filepath, char **best_out);
static struct backup_entry *backup_find(const struct backup_dir *bd,
                                        const char *filepath);
static void backup_note(const char *filepath, const char *digits);
static bool first_backup_exists(const char *filepath);
static int backup_next(const char *filepath, const char *control,
                       char **digits_out);
static char *backup_name(const char *filepath, const char *digits);
static int backup_link(const char *filepath, const char *name,
                       const struct stat *st);
static int backup_numbered(const char *filepath, const struct stat *st,
                           char **digits, char **backup_name_out);
static int backup_simple(const char *filepath, const struct stat *st,
                         char **backup_name_out);

static struct backup_dir backups[NEWFILE_BATCH];
static size_t nbackups;

static int digits_cmp(const char *a, const char *b)
{
    size_t alen, blen;

    alen = strlen(a);
    blen = strlen(b);
    if (alen != blen) {
        return (alen < blen) ? -1 : 1;
    }

    return strcmp(a, b);
}

/* *digits may be reallocated.  Returns 0, or -1. */
static int digits_inc(char **digits)
{
    char *d, *grown;
    size_t len, i;

    d = *digits;
    len = strlen(d);
    for (i = len; i > 0; i--) {
        if (d[i - 1] != '9') {
            d[i - 1]++;
            return 0;
        }
        d[i - 1] = '0';
    }

    grown = realloc(d, len + 2);
    if (grown == NULL) {
        return -1;
    }
    memmove(grown + 1, grown, len + 1);
    grown[0] = '1';
    *digits = grown;

    return 0;
}

static int entry_cmp(const void *a, const void *b)
{
    return strcmp(((const struct backup_entry *)a)->base,
                  ((const struct backup_entry *)b)->base);
}

/* True for base.~n~, n from 1 up, with *baselen and *digits set. */
static bool backup_split(const char *name, size_t *baselen, const char **digits)
{
    const char *end, *p;
    size_t len;

    len = strlen(name);
    if (len < sizeof "x.~1~" - 1 || name[len - 1] != '~') {
        return false;
    }

    end = name + len - 1;
    p = end;
    while (p > name && p[-1] >= '0' && p[-1] <= '9') {
        p--;
    }

    if (p == end || *p == '0' || p - name < 3 || p[-1] != '~' || p[-2] != '.') {
        return false;
    }

    *baselen = (size_t)(p - 2 - name);
    *digits = p;
    return true;
}

/* Returns 0, or -1 with errno set. */
static int backup_dir_list(struct backup_dir *bd)
{
    DIR *dp;
    struct dirent *de;
    struct backup_entry *grown, *e;
    const char *digits;
    size_t baselen, cap;
    int err;

    dp = opendir(bd->dir);
    if (dp == NULL) {
        return -1;
    }

    err = 0;
    cap = 0;
    errno = 0;
    while ((de = readdir(dp)) != NULL) {
        if (!backup_split(de->d_name, &baselen, &digits)) {
            continue;
        }

        if (bd->n == cap) {
            cap = (cap == 0) ? LIST_MIN : cap * 2;
            grown = realloc(bd->entries, cap * sizeof(*grown));
            if (grown == NULL) {
                err = ENOMEM;
                break;
            }
            bd->entries = grown;
        }

        e = &bd->entries[bd->n];
        e->base = strndup(de->d_name, baselen);
        e->digits = strndup(digits, strlen(digits) - 1);
        if (e->base == NULL || e->digits == NULL) {
            free(e->base);
            free(e->digits);
            err = ENOMEM;
            break;
        }
        bd->n++;
        errno = 0;
    }

    if (err == 0 && errno != 0) {
        err = errno;
    }
    closedir(dp);

    if (err != 0) {
        errno = err;
        return -1;
    }

    if (bd->n > 0) {
        entries_keep_highest(bd);
    }

    return 0;
}

/* Leaves the entries sorted by base, one per base. */
static void entries_keep_highest(struct backup_dir *bd)
{
    struct backup_entry *e;
    size_t i, kept;

    qsort(bd->entries, bd->n, sizeof(*bd->entries), entry_cmp);
    kept = 0;
    for (i = 0; i < bd->n; i++) {
        e = &bd->entries[i];
        if (kept > 0 && strcmp(bd->entries[kept - 1].base, e->base) == 0) {
            if (digits_cmp(e->digits, bd->entries[kept - 1].digits) > 0) {
                free(bd->entries[kept - 1].digits);
                bd->entries[kept - 1].digits = e->digits;
            } else {
                free(e->digits);
            }
            free(e->base);
            continue;
        }
        bd->entries[kept++] = *e;
    }
    bd->n = kept;
}

/* Valid until backup_cache_reset().  NULL with errno set. */
static struct backup_dir *backup_dir_get(const char *filepath)
{
    struct backup_dir *bd;
    char *dir;
    size_t i;
    int saved_errno;

    dir = path_dir(filepath);
    if (dir == NULL) {
        return NULL;
    }

    for (i = 0; i < nbackups; i++) {
        if (strcmp(backups[i].dir, dir) == 0) {
            free(dir);
            return &backups[i];
        }
    }

    if (nbackups == NEWFILE_BATCH) {
        backup_cache_reset();
    }

    bd = &backups[nbackups];
    bd->dir = dir;
    bd->entries = NULL;
    bd->n = 0;
    if (backup_dir_list(bd) != 0) {
        saved_errno = errno;
        backup_dir_free(bd);
        errno = saved_errno;
        return NULL;
    }
    nbackups++;

    return bd;
}

static void backup_dir_free(struct backup_dir *bd)
{
    size_t i;

    for (i = 0; i < bd->n; i++) {
        free(bd->entries[i].base);
        free(bd->entries[i].digits);
    }
    free(bd->entries);
    free(bd->dir);
}

void backup_cache_reset(void)
{
    size_t i;

    for (i = 0; i < nbackups; i++) {
        backup_dir_free(&backups[i]);
    }
    nbackups = 0;
}

/* *best_out is to free, or NULL.  Returns 0, or -1 with errno set. */
static int backup_best(const char *filepath, char **best_out)
{
    const struct backup_dir *bd;
    const struct backup_entry *e;

    *best_out = NULL;

    bd = backup_dir_get(filepath);
    if (bd == NULL) {
        return -1;
    }

    e = backup_find(bd, filepath);
    if (e == NULL) {
        return 0;
    }

    *best_out = strdup(e->digits);
    return (*best_out == NULL) ? -1 : 0;
}

static struct backup_entry *backup_find(const struct backup_dir *bd,
                                        const char *filepath)
{
    struct backup_entry key;

    if (bd->n == 0) {
        return NULL;
    }

    key.base = (char *)(filepath + path_dir_len(filepath));
    key.digits = NULL;
    return bsearch(&key, bd->entries, bd->n, sizeof(*bd->entries), entry_cmp);
}

static void backup_note(const char *filepath, const char *digits)
{
    struct backup_dir *bd;
    struct backup_entry *e, *grown;
    char *copy, *base;
    size_t i;

    bd = backup_dir_get(filepath);
    if (bd == NULL) {
        return;
    }

    copy = strdup(digits);
    if (copy == NULL) {
        return;
    }

    e = backup_find(bd, filepath);
    if (e != NULL) {
        if (digits_cmp(copy, e->digits) > 0) {
            free(e->digits);
            e->digits = copy;
        } else {
            free(copy);
        }
        return;
    }

    base = strdup(filepath + path_dir_len(filepath));
    grown = realloc(bd->entries, (bd->n + 1) * sizeof(*grown));
    if (base == NULL || grown == NULL) {
        free(base);
        free(copy);
        if (grown != NULL) {
            bd->entries = grown;
        }
        return;
    }
    bd->entries = grown;

    for (i = bd->n; i > 0 && strcmp(bd->entries[i - 1].base, base) > 0; i--) {
        bd->entries[i] = bd->entries[i - 1];
    }
    bd->entries[i].base = base;
    bd->entries[i].digits = copy;
    bd->n++;
}

static bool first_backup_exists(const char *filepath)
{
    struct stat st;
    char *name;
    bool exists;

    name = backup_name(filepath, "1");
    if (name == NULL) {
        return false;
    }

    exists = lstat(name, &st) == 0;
    free(name);

    return exists;
}

/* *digits_out is to free, or NULL for a simple backup.  Returns 0, or -1. */
static int backup_next(const char *filepath, const char *control,
                       char **digits_out)
{
    char *best;
    bool numbered;

    *digits_out = NULL;

    if (strcmp(control, "simple") == 0 || strcmp(control, "never") == 0) {
        return 0;
    }

    numbered = strcmp(control, "numbered") == 0 || strcmp(control, "t") == 0;

    if (backup_best(filepath, &best) != 0) {
        if (errno != EACCES && errno != EPERM) {
            return -1;
        }
        best = NULL;
        numbered = numbered || first_backup_exists(filepath);
    } else {
        numbered = numbered || best != NULL;
    }

    if (!numbered) {
        free(best);
        return 0;
    }

    if (best == NULL) {
        best = strdup("1");
        if (best == NULL) {
            return -1;
        }
    } else if (digits_inc(&best) != 0) {
        free(best);
        return -1;
    }

    *digits_out = best;
    return 0;
}

static char *backup_name(const char *filepath, const char *digits)
{
    char *name;
    size_t bufsz;

    bufsz = strlen(filepath) + strlen(digits) + sizeof ".~~";
    name = malloc(bufsz);
    if (name != NULL
        && snprintf(name, bufsz, "%s.~%s~", filepath, digits) < 0) {
        free(name);
        name = NULL;
    }

    return name;
}

/* Arms name in PENDING_BACKUP.  Returns 0, or -1 with errno set. */
static int backup_link(const char *filepath, const char *name,
                       const struct stat *st)
{
    sigset_t old;
    int ret;

    pending_hold(&old);
    ret = link(filepath, name);
    if (ret == 0) {
        pending_arm(PENDING_BACKUP, name, st);
    }
    pending_release(&old);

    return ret;
}

/* *backup_name_out is to free.  Returns 0, or -1 with errno set. */
static int backup_numbered(const char *filepath, const struct stat *st,
                           char **digits, char **backup_name_out)
{
    char *name;

    while (1) {
        name = backup_name(filepath, *digits);
        if (name == NULL) {
            return -1;
        }

        if (backup_link(filepath, name, st) == 0) {
            backup_note(filepath, *digits);
            *backup_name_out = name;
            return 0;
        }

        free(name);
        if (errno != EEXIST || digits_inc(digits) != 0) {
            return -1;
        }
    }
}

/*
 * *backup_name_out is to free.  Returns 0, 1 if a temporary name was left
 * behind, or -1 with errno set.
 */
static int backup_simple(const char *filepath, const struct stat *st,
                         char **backup_name_out)
{
    char *name, *tmp;
    size_t bufsz;
    int ret;

    bufsz = strlen(filepath) + sizeof "~";
    name = malloc(bufsz);
    if (name == NULL) {
        return -1;
    }
    if (snprintf(name, bufsz, "%s~", filepath) < 0) {
        free(name);
        return -1;
    }

    if (backup_link(filepath, name, st) == 0) {
        *backup_name_out = name;
        return 0;
    }

    if (errno != EEXIST
        || place_temp_alias(filepath, st, PENDING_BACKUP, &tmp) != 0) {
        free(name);
        return -1;
    }

    if (rename(tmp, name) != 0) {
        pending_remove(PENDING_BACKUP);
        free(tmp);
        free(name);
        return -1;
    }

    ret = (pending_remove(PENDING_BACKUP) == 0) ? 0 : 1;
    free(tmp);

    *backup_name_out = name;
    return ret;
}

/*
 * *backup_name_out is to free, or NULL.  Returns 0, 1 if a temporary name
 * was left behind, or -1.
 */
int backup_make(const char *filepath, const char *control,
                char **backup_name_out, struct stat *st_out)
{
    char *digits;
    int ret, saved_errno;

    *backup_name_out = NULL;

    if (strcmp(control, "none") == 0 || strcmp(control, "off") == 0) {
        return 0;
    }

    if (lstat(filepath, st_out) != 0) {
        return (errno == ENOENT) ? 0 : -1;
    }

    if (!S_ISREG(st_out->st_mode)) {
        errno = S_ISDIR(st_out->st_mode) ? EISDIR : EINVAL;
        return -1;
    }

    if (backup_next(filepath, control, &digits) != 0) {
        return -1;
    }

    if (digits == NULL) {
        return backup_simple(filepath, st_out, backup_name_out);
    }

    ret = backup_numbered(filepath, st_out, &digits, backup_name_out);
    saved_errno = errno;
    free(digits);
    errno = saved_errno;

    return ret;
}

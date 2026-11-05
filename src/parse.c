/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* parse.c - option arguments: modes, sizes and owners */

#include "newfile.h"
#include "parse.h"
#include "error.h"
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SIZE_K ((uintmax_t)1024)
#define SIZE_M (SIZE_K * SIZE_K)
#define SIZE_G (SIZE_M * SIZE_K)

static mode_t mode_who(const char **p);
static mode_t mode_perms(const char **p, const char *mode_str);
static mode_t mode_apply(mode_t mode, char op, mode_t who, mode_t perms);
static uintmax_t size_multiplier(const char *suffix, const char *size_str);
static int lookup_uid(const char *name, uid_t *uid);
static int lookup_gid(const char *name, gid_t *gid);
static int parse_uint_id(const char *id_str, uintmax_t *id);
static uintmax_t off_t_max_value(void);

/* Exits on invalid input. */
mode_t parse_mode(const char *mode_str)
{
    char *endptr;
    unsigned long val;
    mode_t mode, who, perms;
    const char *p;
    char op;

    if (*mode_str == '\0') {
        error_exit("invalid mode: '%s'\n", mode_str);
    }

    if (*mode_str >= '0' && *mode_str <= '7') {
        errno = 0;
        val = strtoul(mode_str, &endptr, 8);
        if (*endptr != '\0' || val > CHMOD_MODE_BITS) {
            error_exit("invalid mode: '%s'\n", mode_str);
        }
        return (mode_t)val;
    }

    mode = 0;
    p = mode_str;
    while (*p != '\0') {
        who = mode_who(&p);
        if (*p != '+' && *p != '-' && *p != '=') {
            error_exit("invalid mode: '%s'\n", mode_str);
        }
        op = *p++;
        perms = mode_perms(&p, mode_str);
        mode = mode_apply(mode, op, who, perms & who);

        if (*p == ',') {
            p++;
            if (*p == '\0') {
                error_exit("invalid mode: '%s'\n", mode_str);
            }
        }
    }

    return mode;
}

/* Advances *p past the who letters. */
static mode_t mode_who(const char **p)
{
    mode_t who;

    who = 0;
    for (;; (*p)++) {
        switch (**p) {
        case 'u':
            who |= S_IRWXU | S_ISUID;
            break;
        case 'g':
            who |= S_IRWXG | S_ISGID;
            break;
        case 'o':
            who |= S_IRWXO | S_ISVTX;
            break;
        case 'a':
            who |= CHMOD_MODE_BITS;
            break;
        default:
            return (who != 0) ? who : CHMOD_MODE_BITS;
        }
    }
}

/* Advances *p to the end of the clause.  Exits on an unknown letter. */
static mode_t mode_perms(const char **p, const char *mode_str)
{
    mode_t perms;

    perms = 0;
    for (; **p != '\0' && **p != ','; (*p)++) {
        switch (**p) {
        case 'r':
            perms |= S_IRUSR | S_IRGRP | S_IROTH;
            break;
        case 'w':
            perms |= S_IWUSR | S_IWGRP | S_IWOTH;
            break;
        case 'x':
            perms |= S_IXUSR | S_IXGRP | S_IXOTH;
            break;
        case 's':
            perms |= S_ISUID | S_ISGID;
            break;
        case 't':
            perms |= S_ISVTX;
            break;
        default:
            error_exit("invalid mode: '%s'\n", mode_str);
        }
    }

    return perms;
}

static mode_t mode_apply(mode_t mode, char op, mode_t who, mode_t perms)
{
    switch (op) {
    case '=':
        return (mode & ~who) | perms;
    case '+':
        return mode | perms;
    default:
        return mode & ~perms;
    }
}

/* Exits on invalid input or overflow. */
off_t parse_size(const char *size_str)
{
    char *endptr;
    uintmax_t val, multiplier;

    if (!isdigit((unsigned char)*size_str)) {
        error_exit("invalid size: '%s'\n", size_str);
    }

    errno = 0;
    val = strtoumax(size_str, &endptr, 10);

    if (errno != 0 || endptr == size_str) {
        error_exit("invalid size: '%s'\n", size_str);
    }

    multiplier = size_multiplier(endptr, size_str);
    if (val > off_t_max_value() / multiplier) {
        error_exit("size too large: '%s'\n", size_str);
    }

    return (off_t)(val * multiplier);
}

/* Exits on a suffix other than K, M or G. */
static uintmax_t size_multiplier(const char *suffix, const char *size_str)
{
    uintmax_t multiplier;

    if (*suffix == '\0') {
        return 1;
    }

    switch (toupper((unsigned char)*suffix)) {
    case 'K':
        multiplier = SIZE_K;
        break;
    case 'M':
        multiplier = SIZE_M;
        break;
    case 'G':
        multiplier = SIZE_G;
        break;
    default:
        error_exit("invalid size suffix in '%s'\n", size_str);
    }

    if (suffix[1] != '\0') {
        error_exit("invalid size: '%s'\n", size_str);
    }

    return multiplier;
}

/* Returns 0, or -1 on bad input. */
int parse_owner(const char *owner_str, uid_t *uid, gid_t *gid, bool *set_uid,
                bool *set_gid)
{
    char *buf, *colon;
    const char *group_str;
    int ret;

    if (owner_str == NULL || *owner_str == '\0') {
        return -1;
    }

    *set_uid = false;
    *set_gid = false;

    buf = strdup(owner_str);
    if (buf == NULL) {
        return -1;
    }

    ret = 0;
    colon = strchr(buf, ':');
    if (colon != NULL) {
        *colon = '\0';
        group_str = colon + 1;
        if (*group_str == '\0' && *buf != '\0') {
            ret = -1;
        } else if (*group_str != '\0') {
            ret = lookup_gid(group_str, gid);
            *set_gid = ret == 0;
        }
    }

    if (ret == 0 && *buf != '\0') {
        ret = lookup_uid(buf, uid);
        *set_uid = ret == 0;
    }

    if (ret == 0 && !*set_uid && !*set_gid) {
        ret = -1;
    }

    free(buf);
    return ret;
}

/* Returns 0, or -1 for an unknown name or an ID that cannot be set. */
static int lookup_uid(const char *name, uid_t *uid)
{
    const struct passwd *pwd;
    uintmax_t id;

    pwd = getpwnam(name);
    if (pwd != NULL) {
        *uid = pwd->pw_uid;
        return 0;
    }

    if (parse_uint_id(name, &id) != 0 || (uintmax_t)(uid_t)id != id
        || (uid_t)id == (uid_t)-1) {
        return -1;
    }

    *uid = (uid_t)id;
    return 0;
}

/* Returns 0, or -1 for an unknown name or an ID that cannot be set. */
static int lookup_gid(const char *name, gid_t *gid)
{
    const struct group *grp;
    uintmax_t id;

    grp = getgrnam(name);
    if (grp != NULL) {
        *gid = grp->gr_gid;
        return 0;
    }

    if (parse_uint_id(name, &id) != 0 || (uintmax_t)(gid_t)id != id
        || (gid_t)id == (gid_t)-1) {
        return -1;
    }

    *gid = (gid_t)id;
    return 0;
}

/* Returns 0, or -1 on a non-digit or out of range. */
static int parse_uint_id(const char *id_str, uintmax_t *id)
{
    const char *p;
    char *endptr;

    if (id_str == NULL || *id_str == '\0') {
        return -1;
    }

    for (p = id_str; *p != '\0'; p++) {
        if (!isdigit((unsigned char)*p)) {
            return -1;
        }
    }

    errno = 0;
    *id = strtoumax(id_str, &endptr, 10);
    if (errno == ERANGE || *endptr != '\0') {
        return -1;
    }

    return 0;
}

static uintmax_t off_t_max_value(void)
{
    return (((uintmax_t)1 << ((sizeof(off_t) * CHAR_BIT) - 1)) - 1);
}

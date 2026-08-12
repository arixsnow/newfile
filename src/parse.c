/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* parse.c - command-line argument parsing for newfile */

#include "newfile.h"
#include <ctype.h>
#include <errno.h>
#include <grp.h>
#include <inttypes.h>
#include <limits.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int parse_uint_id(const char *id_str, uintmax_t *id);
static uintmax_t off_t_max_value(void);

/*
 * parse_mode - parse an octal or symbolic permission mode
 *
 * Symbolic form is [ugoa...][=+-][rwxst...], comma-separated.  Calls
 * errorexit() on anything invalid.
 */
mode_t parse_mode(const char *mode_str)
{
    char *endptr;
    unsigned long val;
    mode_t mode, who_mask, perm_bits;
    const char *p;
    char op;
    bool who_specified;

    if (*mode_str == '\0') {
        errorexit("invalid mode: '%s'\n", mode_str);
    }

    if (*mode_str >= '0' && *mode_str <= '7') {
        errno = 0;
        val = strtoul(mode_str, &endptr, 8);
        if (*endptr != '\0' || val > 07777) {
            errorexit("invalid mode: '%s'\n", mode_str);
        }
        return (mode_t)val;
    }

    mode = 0;
    p = mode_str;

    while (*p != '\0') {
        who_mask = 0;
        perm_bits = 0;
        who_specified = false;

        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            who_specified = true;
            switch (*p) {
            case 'u':
                who_mask |= S_IRWXU | S_ISUID;
                break;
            case 'g':
                who_mask |= S_IRWXG | S_ISGID;
                break;
            case 'o':
                who_mask |= S_IRWXO | S_ISVTX;
                break;
            case 'a':
                who_mask |= S_IRWXU | S_IRWXG | S_IRWXO
                            | S_ISUID | S_ISGID | S_ISVTX;
                break;
            default:
                break;
            }
            p++;
        }

        if (!who_specified) {
            who_mask = S_IRWXU | S_IRWXG | S_IRWXO
                       | S_ISUID | S_ISGID | S_ISVTX;
        }

        if (*p != '+' && *p != '-' && *p != '=') {
            errorexit("invalid mode: '%s'\n", mode_str);
        }
        op = *p++;

        while (*p != '\0' && *p != ',') {
            switch (*p) {
            case 'r':
                perm_bits |= S_IRUSR | S_IRGRP | S_IROTH;
                break;
            case 'w':
                perm_bits |= S_IWUSR | S_IWGRP | S_IWOTH;
                break;
            case 'x':
                perm_bits |= S_IXUSR | S_IXGRP | S_IXOTH;
                break;
            case 's':
                perm_bits |= S_ISUID | S_ISGID;
                break;
            case 't':
                perm_bits |= S_ISVTX;
                break;
            default:
                errorexit("invalid mode: '%s'\n", mode_str);
            }
            p++;
        }

        perm_bits &= who_mask;

        switch (op) {
        case '=':
            mode &= ~who_mask;
            mode |= perm_bits;
            break;
        case '+':
            mode |= perm_bits;
            break;
        case '-':
            mode &= ~perm_bits;
            break;
        default:
            break;
        }

        if (*p == ',') {
            p++;
            if (*p == '\0') {
                errorexit("invalid mode: '%s'\n", mode_str);
            }
        }
    }

    return mode;
}

/*
 * parse_size - parse a decimal size with an optional K, M or G suffix
 *
 * Suffixes are powers of 1024.  Calls errorexit() on overflow or bad
 * input.
 */
off_t parse_size(const char *size_str)
{
    char *endptr;
    uintmax_t val;
    uintmax_t multiplier;
    uintmax_t max_size;

    /* strtoumax(3) would accept " +5". */
    if (!isdigit((unsigned char)*size_str)) {
        errorexit("invalid size: '%s'\n", size_str);
    }

    multiplier = 1;
    errno = 0;
    val = strtoumax(size_str, &endptr, 10);

    if (errno != 0 || endptr == size_str) {
        errorexit("invalid size: '%s'\n", size_str);
    }

    if (*endptr != '\0') {
        switch (toupper((unsigned char)*endptr)) {
        case 'K':
            multiplier = 1024;
            break;
        case 'M':
            multiplier = 1024UL * 1024;
            break;
        case 'G':
            multiplier = 1024UL * 1024 * 1024;
            break;
        default:
            errorexit("invalid size suffix in '%s'\n", size_str);
        }

        if (*(endptr + 1) != '\0') {
            errorexit("invalid size: '%s'\n", size_str);
        }
    }

    max_size = off_t_max_value();
    if (val > max_size / multiplier) {
        errorexit("size too large: '%s'\n", size_str);
    }

    return (off_t)(val * multiplier);
}

/*
 * parse_owner - parse "user[:group]", each field a name or numeric ID
 *
 * Sets *uid and *set_uid when a user was given, *gid and *set_gid when
 * a group was.  Returns 0, or -1 on bad input.
 */
int parse_owner(const char *owner_str, uid_t *uid, gid_t *gid,
                bool *set_uid, bool *set_gid)
{
    char *buf, *colon;
    const char *group_str;
    const struct group *grp;
    const struct passwd *pwd;
    uintmax_t id_val;

    if (owner_str == NULL || *owner_str == '\0') {
        return -1;
    }

    *set_uid = false;
    *set_gid = false;

    buf = strdup(owner_str);
    if (buf == NULL) {
        return -1;
    }

    colon = strchr(buf, ':');

    if (colon != NULL) {
        *colon = '\0';
        group_str = colon + 1;

        /* Reject "user:" - a colon requires a group */
        if (*group_str == '\0' && *buf != '\0') {
            free(buf);
            return -1;
        }

        if (*group_str != '\0') {
            grp = getgrnam(group_str);
            if (grp != NULL) {
                *gid = grp->gr_gid;
            } else {
                if (parse_uint_id(group_str, &id_val) != 0
                    || (uintmax_t)(gid_t)id_val != id_val) {
                    free(buf);
                    return -1;
                }
                *gid = (gid_t)id_val;
            }
            *set_gid = true;
        }
    }

    if (*buf != '\0') {
        pwd = getpwnam(buf);
        if (pwd != NULL) {
            *uid = pwd->pw_uid;
        } else {
            if (parse_uint_id(buf, &id_val) != 0
                || (uintmax_t)(uid_t)id_val != id_val) {
                free(buf);
                return -1;
            }
            *uid = (uid_t)id_val;
        }
        *set_uid = true;
    }

    if (!*set_uid && !*set_gid) {
        free(buf);
        return -1;
    }

    free(buf);
    return 0;
}

/*
 * parse_uint_id - parse a decimal non-negative integer into *id
 *
 * Returns 0, or -1 on a non-digit or out of range.
 */
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

/* off_t_max_value - largest off_t, computed because OFF_MAX is not
 * portable */
static uintmax_t off_t_max_value(void)
{
    return (((uintmax_t)1 << (sizeof(off_t) * CHAR_BIT - 1)) - 1);
}

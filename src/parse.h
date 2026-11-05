/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* parse.h - option arguments: modes, sizes and owners */

#ifndef NEWFILE_PARSE_H
#define NEWFILE_PARSE_H

#include "newfile.h"
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>

#define CHMOD_MODE_BITS \
    (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

mode_t parse_mode(const char *mode_str);
off_t parse_size(const char *size_str);
int parse_owner(const char *owner_str, uid_t *uid, gid_t *gid, bool *set_uid,
                bool *set_gid);

#endif

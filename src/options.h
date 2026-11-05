/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* options.h - the option table, and everything derived from it */

#ifndef NEWFILE_OPTIONS_H
#define NEWFILE_OPTIONS_H

#include "newfile.h"

enum {
    OPTVAL_HELP = 256,
    OPTVAL_VERSION,
    OPTVAL_REFERENCE,
    OPTVAL_TEMPLATE,
    OPTVAL_BACKUP,
    OPTVAL_SPARSE
};

struct opt_option;

const char *options_optstring(void);
const struct opt_option *options_long(void);
void options_usage(int status) ATTR_NORETURN;
void options_version(void) ATTR_NORETURN;

#endif

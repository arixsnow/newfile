/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* newfile.h - what every source file includes first */

#ifndef NEWFILE_H
#define NEWFILE_H

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#ifndef VERSION
#define VERSION "unknown"
#endif

#define BUG_URL "https://github.com/arixsnow/newfile/issues"

#ifndef NEWFILE_BATCH
#define NEWFILE_BATCH 64
#endif

#ifdef __GNUC__
#define ATTR_NORETURN __attribute__((__noreturn__))
#define ATTR_PRINTF(fmt, args) \
    __attribute__((__format__(__printf__, fmt, args)))
#define ATTR_UNUSED __attribute__((__unused__))
#else
#define ATTR_NORETURN
#define ATTR_PRINTF(fmt, args)
#define ATTR_UNUSED
#endif

#endif

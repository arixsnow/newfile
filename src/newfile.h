/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* newfile.h - shared declarations for newfile */

#ifndef NEWFILE_H
#define NEWFILE_H

/* Must precede any system header, so sources include this one first. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

/* Without this an off_t is 32 bits wherever that is the default. */
#ifndef _FILE_OFFSET_BITS
#define _FILE_OFFSET_BITS 64
#endif

#include <stdbool.h>
#include <sys/types.h>

#ifndef VERSION
#define VERSION "unknown"
#endif

#define BUG_URL "https://github.com/arixsnow/newfile/issues"

#if defined(__GNUC__)
#define ATTR_NORETURN __attribute__((__noreturn__))
#define ATTR_PRINTF(fmt, args) \
    __attribute__((__format__(__printf__, fmt, args)))
#define ATTR_UNUSED __attribute__((__unused__))
#else
#define ATTR_NORETURN
#define ATTR_PRINTF(fmt, args)
#define ATTR_UNUSED
#endif

/* Long-only option values */
enum {
    OPTVAL_HELP = 256,
    OPTVAL_VERSION,
    OPTVAL_REFERENCE,
    OPTVAL_TEMPLATE,
    OPTVAL_BACKUP,
    OPTVAL_SPARSE
};

extern const char *program_name;

/* error.c */
void errorexit(const char *format, ...) ATTR_NORETURN ATTR_PRINTF(1, 2);
void output_error(const char *format, ...) ATTR_PRINTF(1, 2);
int stdout_ok(void);

/* options.c - the option table is private, only its products are not */
struct opt_option;
const char *options_optstring(void);
const struct opt_option *options_long(void);
void options_usage(int status) ATTR_NORETURN;

/* parse.c */
mode_t parse_mode(const char *mode_str);
off_t parse_size(const char *size_str);
int parse_owner(const char *owner_str, uid_t *uid, gid_t *gid,
                bool *set_uid, bool *set_gid);

/* fileops.c */
int make_parents(const char *path);
int temp_open(const char *target, char **tmppath_out);
void pending_arm(const char *path);
void pending_clear(void);
void pending_discard(void);
void pending_cleanup(void);
int dirsync_stage(const char *path);
int dirsync_flush(void);
const char *dirsync_dir(void);
void dirsync_release(void);
int make_backup(const char *filepath, const char *control,
                char **backup_name_out);

/*
 * template.c - what a new file starts out holding
 *
 * The source is classified once, when it is opened, so the layout of a
 * copy cannot depend on how many files are being created.
 *
 * Named tmpl rather than template: the full word is a keyword in C++,
 * and a header a C++ parser cannot read trips tooling that guesses at
 * the language.
 */
struct tmpl;
struct tmpl *tmpl_open(const char *path, bool repeat);
int tmpl_apply(const struct tmpl *tmpl, int dst_fd);
bool tmpl_tail_hole(const struct tmpl *tmpl);
void tmpl_close(struct tmpl *tmpl);
int fill_range(int fd, off_t off, off_t len);

/*
 * holes.c - where a file's data sits, so a copy keeps its shape
 *
 * Both move the file offset, so callers address the file explicitly.
 */
int hole_next_data(int fd, off_t from, off_t size, off_t *start,
                   off_t *end);
bool hole_tail(int fd, off_t size);

/* fastio.c - each returns 1 where the platform cannot do the work */
int fast_alloc(int fd, off_t off, off_t len);
int fast_copy(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off,
              off_t len);

#endif

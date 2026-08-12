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
#define VERSION "0.1.0"
#endif

#define BUG_URL "https://github.com/arixsnow/newfile/issues"

#if defined(__GNUC__)
#define ATTR_NORETURN __attribute__((__noreturn__))
#define ATTR_PRINTF(fmt, args) \
    __attribute__((__format__(__printf__, fmt, args)))
#else
#define ATTR_NORETURN
#define ATTR_PRINTF(fmt, args)
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
int sync_dir(const char *path);
int write_buf(int dst_fd, const char *buf, size_t len);
int read_all(int src_fd, char **buf_out, size_t *len_out);
int copy_fd(int dst_fd, int src_fd);
int copy_template(int dst_fd, const char *template_path);
int fill_file(int fd, off_t size);
int make_backup(const char *filepath, const char *control,
                char **backup_name_out);

/* fastio.c - each returns 1 where the platform cannot do the work */
int fast_alloc(int fd, off_t size);
int fast_copy(int dst_fd, int src_fd);

#endif

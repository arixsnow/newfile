/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* fastio.h - kernel fast paths that fall back to portable code */

#ifndef NEWFILE_FASTIO_H
#define NEWFILE_FASTIO_H

#include "newfile.h"
#include <sys/types.h>

int fast_alloc(int fd, off_t off, off_t len);
int fast_copy(int dst_fd, off_t *dst_off, int src_fd, off_t *src_off,
              off_t len);
int fast_tmpfile(const char *dir, mode_t mode, int *fd_out);
int fast_link(int fd, const char *path);
int fast_rename_noreplace(const char *from, const char *to);
int fast_exchange(const char *a, const char *b);
void fast_writeback(int fd);
int fast_spool(int *fd_out);
int fast_splice(int dst_fd, int src_fd);

#endif

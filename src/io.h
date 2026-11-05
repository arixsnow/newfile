/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* io.h - whole writes, transfer buffers, and filling a range with NUL */

#ifndef NEWFILE_IO_H
#define NEWFILE_IO_H

#include "newfile.h"
#include <stddef.h>
#include <sys/types.h>

int io_write(int fd, const char *buf, size_t len);
int io_pwrite(int fd, const char *buf, size_t len, off_t off);
size_t io_bufsize(int fd);
char *io_bufalloc(size_t size);
int io_fill(int fd, off_t off, off_t len);

#endif

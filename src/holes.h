/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* holes.h - where a file's data sits, so a copy keeps its shape */

#ifndef NEWFILE_HOLES_H
#define NEWFILE_HOLES_H

#include "newfile.h"
#include <stdbool.h>
#include <sys/types.h>

int hole_next_data(int fd, off_t from, off_t size, off_t *start, off_t *end);
bool hole_tail(int fd, off_t size);
bool hole_unknown(int fd);

#endif

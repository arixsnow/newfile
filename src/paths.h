/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* paths.h - the directory part of a path, and making it exist */

#ifndef NEWFILE_PATHS_H
#define NEWFILE_PATHS_H

#include "newfile.h"
#include <stddef.h>

size_t path_dir_len(const char *path);
char *path_dir(const char *path);
int path_make_parents(const char *path);

#endif

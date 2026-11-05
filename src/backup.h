/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* backup.h - a second name for a file about to be replaced */

#ifndef NEWFILE_BACKUP_H
#define NEWFILE_BACKUP_H

#include "newfile.h"

struct stat;

int backup_make(const char *filepath, const char *control,
                char **backup_name_out, struct stat *st_out);
void backup_cache_reset(void);

#endif

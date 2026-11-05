/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* flush.h - make files, and the directories that name them, durable */

#ifndef NEWFILE_FLUSH_H
#define NEWFILE_FLUSH_H

#include "newfile.h"
#include <stdbool.h>
#include <stddef.h>

void flush_fds(const int *fds, int *errs, size_t n, bool dir);
void flush_stop(void);
int flush_dir(const char *dir);
void flush_set_init(size_t max);
void flush_set_add(const char *dir);
void flush_set_run(void);
bool flush_set_failed(const char *dir);
void flush_set_forget(void);
size_t flush_set_failures(void);

#endif

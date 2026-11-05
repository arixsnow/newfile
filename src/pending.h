/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* pending.h - names this run made and has not committed */

#ifndef NEWFILE_PENDING_H
#define NEWFILE_PENDING_H

#include "newfile.h"
#include <signal.h>
#include <stddef.h>

struct stat;

#define PENDING_BACKUP NEWFILE_BATCH
#define PENDING_SLOTS  (NEWFILE_BATCH + 1)

void pending_install(void);
void pending_hold(sigset_t *old);
void pending_release(const sigset_t *old);
void pending_arm(size_t slot, const char *path, const struct stat *st);
void pending_retarget(size_t slot, const struct stat *st);
void pending_clear(size_t slot);
int pending_remove(size_t slot);
int pending_unlink(const char *path, const struct stat *st);

#endif

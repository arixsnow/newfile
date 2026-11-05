/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* place.h - how a file comes to hold its name */

#ifndef NEWFILE_PLACE_H
#define NEWFILE_PLACE_H

#include "newfile.h"
#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct stat;

int place_create(const char *path, mode_t mode, size_t slot, int *fd_out);
int place_temp(const char *target, mode_t mode, size_t slot, int *fd_out,
               char **tmppath_out);
int place_temp_link(int fd, const char *target, size_t slot,
                    char **tmppath_out);
int place_temp_alias(const char *src, const struct stat *st, size_t slot,
                     char **tmppath_out);
int place_commit(const char *tmp, const char *target, size_t slot,
                 bool *linked_out);
int place_rename(const char *tmp, const char *target, size_t slot);
int place_exchange(const char *tmp, const char *target, size_t slot,
                   const struct stat *old_st);
int place_probe_mode(const char *target, mode_t mode, size_t slot,
                     mode_t *granted);
void place_probe_reset(void);

#endif

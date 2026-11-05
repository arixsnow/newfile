/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* batch.h - build, flush, name and report the operands, a batch at a time */

#ifndef NEWFILE_BATCH_H
#define NEWFILE_BATCH_H

#include "newfile.h"
#include <stdbool.h>
#include <stddef.h>

struct file_spec;

bool batch_run(char *const *paths, size_t npaths, const struct file_spec *spec);

#endif

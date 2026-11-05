/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* error.h - reporting to the user */

#ifndef NEWFILE_ERROR_H
#define NEWFILE_ERROR_H

#include "newfile.h"
#include <stdbool.h>

extern const char *program_name;

void error_exit(const char *format, ...) ATTR_NORETURN ATTR_PRINTF(1, 2);
void error_print(const char *format, ...) ATTR_PRINTF(1, 2);
bool error_close_std(void);

#endif

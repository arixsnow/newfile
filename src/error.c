/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* error.c - error-reporting functions for newfile */

#include "newfile.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void errorexit(const char *format, ...)
{
    va_list arg_list;

    fflush(stdout);
    fprintf(stderr, "%s: ", program_name);
    va_start(arg_list, format);
    vfprintf(stderr, format, arg_list);
    va_end(arg_list);

    exit(EXIT_FAILURE);
}

void output_error(const char *format, ...)
{
    va_list arg_list;

    fflush(stdout);
    fprintf(stderr, "%s: ", program_name);
    va_start(arg_list, format);
    vfprintf(stderr, format, arg_list);
    va_end(arg_list);
}

/* stdout_ok - check for a deferred write error before exit() */
int stdout_ok(void)
{
    if (fflush(stdout) != 0 || ferror(stdout)) {
        output_error("cannot write to standard output\n");
        return 0;
    }

    return 1;
}

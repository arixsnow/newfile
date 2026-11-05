/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* error.c - reporting to the user */

#include "newfile.h"
#include "error.h"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_HWADDRESS__) \
    || defined(__SANITIZE_LEAK__) || defined(__SANITIZE_MEMORY__) \
    || defined(__SANITIZE_SCUDO__) || defined(__SANITIZE_THREAD__)
#define CLOSE_STDERR false
#else
#define CLOSE_STDERR true
#endif

const char *program_name = "newfile";

static void report(const char *format, va_list args) ATTR_PRINTF(1, 0);
static void report_now(const char *format, ...) ATTR_PRINTF(1, 2);
static bool close_stream(FILE *stream);

static void report(const char *format, va_list args)
{
    fprintf(stderr, "%s: ", program_name);
    vfprintf(stderr, format, args);
}

void error_exit(const char *format, ...)
{
    va_list args;

    fflush(stdout);
    va_start(args, format);
    report(format, args);
    va_end(args);

    exit(EXIT_FAILURE);
}

void error_print(const char *format, ...)
{
    va_list args;

    fflush(stdout);
    va_start(args, format);
    report(format, args);
    va_end(args);
}

static void report_now(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    report(format, args);
    va_end(args);
}

/* errno is 0 when the failure was an earlier write. */
static bool close_stream(FILE *stream)
{
    bool prev_fail, close_fail;

    prev_fail = ferror(stream) != 0;
    close_fail = fclose(stream) != 0;
    if (prev_fail && !close_fail) {
        errno = 0;
    }

    return !prev_fail && !close_fail;
}

/* Returns false, after reporting a stdout failure, if either stream failed. */
bool error_close_std(void)
{
    if (!close_stream(stdout)) {
        if (errno != 0) {
            report_now("cannot write to standard output: %s\n",
                       strerror(errno));
        } else {
            report_now("cannot write to standard output\n");
        }
        return false;
    }

    return !CLOSE_STDERR || close_stream(stderr);
}

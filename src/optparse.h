/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* optparse.h - declarations for the command-line option parser */

#ifndef OPTPARSE_H
#define OPTPARSE_H

enum {
    OPT_NONE,
    OPT_REQUIRED,
    OPT_OPTIONAL
};

struct opt_option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};

extern char *opt_arg;
extern int opt_ind;
extern int opt_opt;
extern const char *opt_name;

int optparse_long(int argc, char **argv, const char *optstring,
                  const struct opt_option *longopts, int *longindex);

#endif

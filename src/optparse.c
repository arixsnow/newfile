/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* optparse.c - command-line option parser */

#include "newfile.h"
#include "optparse.h"
#include <string.h>

char *opt_arg = NULL;
int opt_ind = 1;
int opt_opt = 0;
const char *opt_name = NULL;

static char *nextchar;
static int started;

static void permute(int argc, char **argv, const char *optstring,
                    const struct opt_option *longopts);
static void rotate(char **argv, int first, int middle, int last);
static void reverse(char **argv, int first, int last);
static int long_takes_arg(const char *arg, const struct opt_option *longopts);
static int cluster_takes_arg(const char *chars, const char *optstring);
static int parse_long(int argc, char **argv, const struct opt_option *longopts,
                      int *longindex);
static int parse_short(int argc, char **argv, const char *optstring);

/* Returns val, 0 for a flag entry, '?', ':' or '=' on error, -1 at the end. */
int optparse_long(int argc, char **argv, const char *optstring,
                  const struct opt_option *longopts, int *longindex)
{
    char *arg;

    opt_arg = NULL;
    opt_name = NULL;

    if (opt_ind == 0) {
        opt_ind = 1;
        nextchar = NULL;
        started = 0;
    }

    if (!started) {
        permute(argc, argv, optstring, longopts);
        started = 1;
    }

    if (nextchar != NULL && *nextchar != '\0') {
        return parse_short(argc, argv, optstring);
    }

    nextchar = NULL;

    if (opt_ind >= argc) {
        return -1;
    }

    arg = argv[opt_ind];
    if (arg[0] != '-' || arg[1] == '\0') {
        return -1;
    }

    if (arg[1] == '-') {
        if (arg[2] == '\0') {
            opt_ind++;
            return -1;
        }
        return parse_long(argc, argv, longopts, longindex);
    }

    nextchar = arg + 1;
    return parse_short(argc, argv, optstring);
}

static void permute(int argc, char **argv, const char *optstring,
                    const struct opt_option *longopts)
{
    const char *arg;
    int end, start, i;

    end = 1;
    i = 1;
    while (i < argc) {
        arg = argv[i];
        start = i++;

        if (strcmp(arg, "--") == 0) {
            rotate(argv, end, start, i);
            return;
        }

        if (arg[0] != '-' || arg[1] == '\0') {
            continue;
        }

        if (arg[1] == '-') {
            if (long_takes_arg(arg + 2, longopts) && i < argc) {
                i++;
            }
        } else if (cluster_takes_arg(arg + 1, optstring) && i < argc) {
            i++;
        }

        rotate(argv, end, start, i);
        end += i - start;
    }
}

/* Moves argv[middle..last) in front of argv[first..middle). */
static void rotate(char **argv, int first, int middle, int last)
{
    reverse(argv, first, middle);
    reverse(argv, middle, last);
    reverse(argv, first, last);
}

static void reverse(char **argv, int first, int last)
{
    char *held;

    last--;
    while (first < last) {
        held = argv[first];
        argv[first] = argv[last];
        argv[last] = held;
        first++;
        last--;
    }
}

static int long_takes_arg(const char *arg, const struct opt_option *longopts)
{
    size_t len;
    int i;

    if (longopts == NULL || strchr(arg, '=') != NULL) {
        return 0;
    }

    len = strlen(arg);
    for (i = 0; longopts[i].name != NULL; i++) {
        if (strlen(longopts[i].name) == len
            && strncmp(longopts[i].name, arg, len) == 0) {
            return longopts[i].has_arg == OPT_REQUIRED;
        }
    }

    return 0;
}

static int cluster_takes_arg(const char *chars, const char *optstring)
{
    const char *spec;
    int c;

    while ((c = (unsigned char)*chars) != '\0') {
        chars++;
        if (c == ':') {
            return 0;
        }
        spec = strchr(optstring, c);
        if (spec == NULL || spec[1] != ':') {
            continue;
        }
        if (spec[2] == ':') {
            return 0;
        }
        return *chars == '\0';
    }

    return 0;
}

static int parse_long(int argc, char **argv, const struct opt_option *longopts,
                      int *longindex)
{
    char *name, *eq;
    size_t len;
    int i;

    name = argv[opt_ind] + 2;
    eq = strchr(name, '=');
    len = (eq != NULL) ? (size_t)(eq - name) : strlen(name);

    i = -1;
    if (longopts != NULL) {
        for (i = 0; longopts[i].name != NULL; i++) {
            if (strlen(longopts[i].name) == len
                && strncmp(longopts[i].name, name, len) == 0) {
                break;
            }
        }
        if (longopts[i].name == NULL) {
            i = -1;
        }
    }

    opt_name = argv[opt_ind];
    opt_ind++;

    if (i < 0) {
        opt_opt = 0;
        return '?';
    }

    opt_name = longopts[i].name;

    if (longindex != NULL) {
        *longindex = i;
    }

    switch (longopts[i].has_arg) {
    case OPT_REQUIRED:
        if (eq != NULL) {
            opt_arg = eq + 1;
        } else if (opt_ind < argc) {
            opt_arg = argv[opt_ind];
            opt_ind++;
        } else {
            return ':';
        }
        break;
    case OPT_OPTIONAL:
        opt_arg = (eq != NULL) ? eq + 1 : NULL;
        break;
    default:
        if (eq != NULL) {
            return '=';
        }
        break;
    }

    if (longopts[i].flag != NULL) {
        *longopts[i].flag = longopts[i].val;
        return 0;
    }

    return longopts[i].val;
}

static int parse_short(int argc, char **argv, const char *optstring)
{
    const char *spec;
    int c;

    c = (unsigned char)*nextchar;
    nextchar++;

    spec = (c == ':') ? NULL : strchr(optstring, c);

    if (spec == NULL || spec[1] != ':') {
        if (*nextchar == '\0') {
            nextchar = NULL;
            opt_ind++;
        }
        if (spec == NULL) {
            opt_opt = c;
            return '?';
        }
        return c;
    }

    if (*nextchar != '\0') {
        opt_arg = nextchar;
        nextchar = NULL;
        opt_ind++;
        return c;
    }

    nextchar = NULL;

    if (spec[2] == ':') {
        opt_ind++;
        return c;
    }

    if (opt_ind + 1 < argc) {
        opt_arg = argv[opt_ind + 1];
        opt_ind += 2;
        return c;
    }

    opt_opt = c;
    opt_ind++;
    return ':';
}

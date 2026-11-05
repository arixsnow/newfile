/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* spec.c - what the command line asks for, shared by every operand */

#include "newfile.h"
#include "spec.h"
#include "error.h"
#include "options.h"
#include "optparse.h"
#include "parse.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct spec_args {
    const char *mode;
    const char *owner;
    const char *reference;
    const char *size;
};

static void spec_reset(struct file_spec *spec, struct spec_args *args);
static void spec_option(int option, struct file_spec *spec,
                        struct spec_args *args);
static void spec_misuse(int option);
static void spec_check(struct file_spec *spec, const struct spec_args *args);
static bool backup_control_known(const char *control);
static void spec_values(struct file_spec *spec, const struct spec_args *args);

/* Exits on a bad option.  Returns the index of the first operand. */
int spec_parse(int argc, char **argv, struct file_spec *spec)
{
    struct spec_args args;
    int opt_index, option;

    spec_reset(spec, &args);
    opt_index = 0;

    while ((option = optparse_long(argc, argv, options_optstring(),
                                   options_long(), &opt_index))
           != -1) {
        spec_option(option, spec, &args);
    }

    if (opt_ind == argc) {
        error_print("missing operand\n");
        options_usage(EXIT_FAILURE);
    }

    spec_check(spec, &args);
    spec_values(spec, &args);

    return opt_ind;
}

static void spec_reset(struct file_spec *spec, struct spec_args *args)
{
    args->mode = NULL;
    args->owner = NULL;
    args->reference = NULL;
    args->size = NULL;

    spec->mode = 0;
    spec->size = 0;
    spec->uid = 0;
    spec->gid = 0;
    spec->tmpl = NULL;
    spec->template_file = NULL;
    spec->backup_control = NULL;
    spec->absolute_given = false;
    spec->force_mode = false;
    spec->parents_mode = false;
    spec->sparse_mode = false;
    spec->size_given = false;
    spec->backup_given = false;
    spec->set_owner = false;
    spec->set_uid = false;
    spec->uid_changes = false;
    spec->set_gid = false;
    spec->verbose = 0;
}

static void spec_option(int option, struct file_spec *spec,
                        struct spec_args *args)
{
    switch (option) {
    case OPTVAL_HELP:
        options_usage(EXIT_SUCCESS);
    case OPTVAL_VERSION:
        options_version();
    case 'm':
        args->mode = opt_arg;
        break;
    case 'A':
        spec->absolute_given = true;
        break;
    case 'p':
        spec->parents_mode = true;
        break;
    case 'f':
        spec->force_mode = true;
        break;
    case 'o':
        args->owner = opt_arg;
        break;
    case OPTVAL_REFERENCE:
        args->reference = opt_arg;
        break;
    case 's':
        spec->size_given = true;
        args->size = opt_arg;
        break;
    case OPTVAL_TEMPLATE:
        spec->template_file = opt_arg;
        break;
    case OPTVAL_BACKUP:
        spec->backup_given = true;
        if (opt_arg != NULL) {
            spec->backup_control = opt_arg;
        }
        break;
    case 'b':
        spec->backup_given = true;
        break;
    case OPTVAL_SPARSE:
        spec->sparse_mode = true;
        break;
    case 'v':
        spec->verbose = 1;
        break;
    default:
        spec_misuse(option);
    }
}

static void spec_misuse(int option)
{
    switch (option) {
    case ':':
        if (opt_name != NULL) {
            error_print("option '--%s' requires an argument\n", opt_name);
        } else {
            error_print("option requires an argument -- %c\n", opt_opt);
        }
        break;
    case '=':
        error_print("option '--%s' does not take an argument\n", opt_name);
        break;
    case '?':
        if (opt_name != NULL) {
            error_print("unknown option '%s'\n", opt_name);
        } else {
            error_print("unknown option -- %c\n", opt_opt);
        }
        break;
    default:
        break;
    }

    options_usage(EXIT_FAILURE);
}

static void spec_check(struct file_spec *spec, const struct spec_args *args)
{
    if (args->mode != NULL && args->reference != NULL) {
        error_exit("options '--mode' and '--reference' are "
                   "mutually exclusive\n");
    }

    if (spec->backup_given && !spec->force_mode) {
        error_print("warning: backup option has no effect"
                    " without --force\n");
    }

    if (spec->sparse_mode && !spec->size_given) {
        error_print("warning: --sparse has no effect"
                    " without --size\n");
    }

    if (spec->backup_given && spec->force_mode) {
        if (spec->backup_control == NULL) {
            spec->backup_control = getenv("VERSION_CONTROL");
            if (spec->backup_control == NULL) {
                spec->backup_control = "existing";
            }
        }
        if (!backup_control_known(spec->backup_control)) {
            error_exit("invalid backup control '%s'\n", spec->backup_control);
        }
    }
}

static bool backup_control_known(const char *control)
{
    static const char *const known[] = {
        "simple", "never", "none", "off", "numbered", "t", "existing", "nil",
    };
    size_t i;

    for (i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strcmp(control, known[i]) == 0) {
            return true;
        }
    }

    return false;
}

static void spec_values(struct file_spec *spec, const struct spec_args *args)
{
    struct stat ref_st;

    if (args->reference != NULL) {
        if (stat(args->reference, &ref_st) != 0) {
            error_exit("cannot stat reference file '%s': %s\n", args->reference,
                       strerror(errno));
        }
        spec->mode = ref_st.st_mode & CHMOD_MODE_BITS;
    } else if (args->mode != NULL) {
        spec->mode = parse_mode(args->mode);
    } else {
        spec->mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    }

    if (args->owner != NULL
        && parse_owner(args->owner, &spec->uid, &spec->gid, &spec->set_uid,
                       &spec->set_gid)
               != 0) {
        error_exit("invalid owner: '%s'\n", args->owner);
    }
    spec->set_owner = args->owner != NULL;
    spec->uid_changes = spec->set_uid && spec->uid != geteuid();

    if (spec->size_given) {
        spec->size = parse_size(args->size);
    }
}

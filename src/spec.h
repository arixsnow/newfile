/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* spec.h - what the command line asks for, shared by every operand */

#ifndef NEWFILE_SPEC_H
#define NEWFILE_SPEC_H

#include "newfile.h"
#include <stdbool.h>
#include <sys/types.h>

struct tmpl;

struct file_spec {
    off_t size;
    const struct tmpl *tmpl;
    const char *template_file;
    const char *backup_control;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    int verbose;
    bool absolute_given;
    bool force_mode;
    bool parents_mode;
    bool sparse_mode;
    bool size_given;
    bool backup_given;
    bool set_owner;
    bool set_uid;
    bool set_gid;
    bool uid_changes;
};

int spec_parse(int argc, char **argv, struct file_spec *spec);

#endif

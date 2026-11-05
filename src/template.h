/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* template.h - what a new file starts out holding */

#ifndef NEWFILE_TEMPLATE_H
#define NEWFILE_TEMPLATE_H

#include "newfile.h"
#include <stdbool.h>

struct tmpl;

struct tmpl *tmpl_open(const char *path, bool repeat);
int tmpl_apply(const struct tmpl *tmpl, int dst_fd);
bool tmpl_tail_hole(const struct tmpl *tmpl);
void tmpl_close(struct tmpl *tmpl);

#endif

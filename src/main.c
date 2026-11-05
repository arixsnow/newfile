/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* main.c - create file(s) with specified attributes */

#include "newfile.h"
#include "batch.h"
#include "error.h"
#include "pending.h"
#include "spec.h"
#include "template.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void reserve_std_fds(void);

int main(int argc, char **argv)
{
    struct file_spec spec;
    struct tmpl *tmpl;
    const char *base;
    bool ok;
    int first;

    reserve_std_fds();

    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        base = strrchr(argv[0], '/');
        program_name = (base != NULL) ? base + 1 : argv[0];
    }

    first = spec_parse(argc, argv, &spec);

    pending_install();

    tmpl = NULL;
    if (spec.template_file != NULL) {
        tmpl = tmpl_open(spec.template_file, argc - first > 1);
        if (tmpl == NULL) {
            error_exit("cannot read template '%s': %s\n", spec.template_file,
                       strerror(errno));
        }
        spec.tmpl = tmpl;
    }

    ok = batch_run(argv + first, (size_t)(argc - first), &spec);

    tmpl_close(tmpl);

    if (!error_close_std()) {
        ok = false;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void reserve_std_fds(void)
{
    int fd, nfd;

    for (fd = STDOUT_FILENO; fd <= STDERR_FILENO; fd++) {
        if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
            continue;
        }

        nfd = open("/dev/null", O_RDONLY);
        if (nfd == -1) {
            exit(EXIT_FAILURE);
        }
        if (nfd != fd) {
            if (dup2(nfd, fd) == -1) {
                exit(EXIT_FAILURE);
            }
            close(nfd);
        }
    }
}

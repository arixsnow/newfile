/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* options.c - the option table, and everything derived from it */

#include "newfile.h"
#include "options.h"
#include "error.h"
#include "optparse.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH_DEFAULT 80
#define WIDTH_MIN     40
#define WIDTH_MAX     1000

#define SPEC_MAX 128

struct option_def {
    const char *name;
    const char *argname;
    const char *desc;
    int val;
    int has_arg;
    bool in_synopsis;
};

static const struct option_def options[] = {
    {"mode", "mode", "set file permission bits (octal or symbolic)", 'm',
     OPT_REQUIRED, true},
    {"owner", "owner[:group]", "set file ownership (may require privileges)",
     'o', OPT_REQUIRED, true},
    {"reference", "file", "use file's mode instead of specifying -m",
     OPTVAL_REFERENCE, OPT_REQUIRED, true},
    {"size", "size", "fill file with NUL bytes to size", 's', OPT_REQUIRED,
     true},
    {"sparse", NULL, "with -s, create a sparse file instead", OPTVAL_SPARSE,
     OPT_NONE, true},
    {"template", "file", "copy content from file (- for stdin)",
     OPTVAL_TEMPLATE, OPT_REQUIRED, true},
    {"parents", NULL, "create parent directories as needed", 'p', OPT_NONE,
     true},
    {"force", NULL, "replace the file if it already exists", 'f', OPT_NONE,
     true},
    {"backup", "control", "make backup before replacing with -f", OPTVAL_BACKUP,
     OPT_OPTIONAL, false},
    {NULL, NULL, "like --backup with no control argument", 'b', OPT_NONE, true},
    {"absolute", NULL, "apply the file mode exactly, bypassing the umask", 'A',
     OPT_NONE, true},
    {"verbose", NULL, "explain what is being done", 'v', OPT_NONE, true},
    {"help", NULL, "display this help and exit", OPTVAL_HELP, OPT_NONE, false},
    {"version", NULL, "output version information and exit", OPTVAL_VERSION,
     OPT_NONE, false},
};

#define NOPTIONS (sizeof(options) / sizeof(options[0]))

#define SYNOPSIS_MAX ((NOPTIONS * 64) + 16)

static const char *const notes[] = {
    "Long options must be spelled in full.",
    "",
    "mode is octal (e.g. 0755) or symbolic (e.g. u=rwx,g=rx,o=r).",
    "Symbolic format: [ugoa...][=+-][rwxst...][,...]",
    "Default mode is a=rw if -m is not specified.",
    "",
    "With -s, the file is filled with NUL bytes by default.",
    "Use --sparse to create a sparse (hole) file instead.",
    "",
    "Backup control values: none (or off), numbered (or t),",
    "existing (or nil), simple (or never).",
    "Default control value is $VERSION_CONTROL, or 'existing' if unset.",
    "",
    "By default, an existing file is never touched.  Use -f to replace it.",
    "A file appears with its final mode, owner and content, or not at all.",
    "When a filesystem cannot rename without replacing and the file cannot",
    "be hard-linked either, an empty file with mode 0600 holds the name",
    "until the finished file replaces it.",
    "",
};

#define NNOTES (sizeof(notes) / sizeof(notes[0]))

static char optstring[(3 * NOPTIONS) + 1];
static struct opt_option long_opts[NOPTIONS + 1];
static bool tables_built;

static void build_tables(void);
static size_t out_width(void);
static size_t sappend(char *buf, size_t bufsz, size_t n, const char *s);
static size_t option_spec(char *buf, size_t bufsz,
                          const struct option_def *opt);
static void put_wrapped(FILE *fp, const char *text, size_t indent, size_t col,
                        size_t width);
static void put_synopsis(FILE *fp, size_t width);

const char *options_optstring(void)
{
    build_tables();

    return optstring;
}

const struct opt_option *options_long(void)
{
    build_tables();

    return long_opts;
}

void options_usage(int status)
{
    FILE *fp;
    char spec[SPEC_MAX];
    size_t i, width, col, speclen, desc_col;

    fp = (status == EXIT_SUCCESS) ? stdout : stderr;
    width = out_width();

    put_synopsis(fp, width);

    if (status != EXIT_SUCCESS) {
        exit(status);
    }

    fputs("\nCreate files with specified attributes.\n\n", stdout);

    desc_col = 0;
    for (i = 0; i < NOPTIONS; i++) {
        speclen = option_spec(spec, sizeof spec, &options[i]);
        if (speclen > desc_col) {
            desc_col = speclen;
        }
    }
    desc_col += 2;

    for (i = 0; i < NOPTIONS; i++) {
        speclen = option_spec(spec, sizeof spec, &options[i]);
        fputs(spec, stdout);
        for (col = speclen; col < desc_col; col++) {
            fputc(' ', stdout);
        }
        put_wrapped(stdout, options[i].desc, desc_col, desc_col, width);
    }

    fputc('\n', stdout);

    for (i = 0; i < NNOTES; i++) {
        put_wrapped(stdout, notes[i], 0, 0, width);
    }

    put_wrapped(stdout, "Report bugs at: " BUG_URL, 0, 0, width);

    exit(error_close_std() ? status : EXIT_FAILURE);
}

void options_version(void)
{
    printf("newfile %s\n", VERSION);
    puts("Copyright (c) 2026 Arka Mondal.");
    puts("License: BSD-3-Clause.");

    exit(error_close_std() ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void build_tables(void)
{
    size_t i, n, k;

    if (tables_built) {
        return;
    }

    n = 0;
    k = 0;
    for (i = 0; i < NOPTIONS; i++) {
        if (options[i].val < OPTVAL_HELP) {
            optstring[n++] = (char)options[i].val;
            if (options[i].has_arg != OPT_NONE) {
                optstring[n++] = ':';
            }
            if (options[i].has_arg == OPT_OPTIONAL) {
                optstring[n++] = ':';
            }
        }

        if (options[i].name != NULL) {
            long_opts[k].name = options[i].name;
            long_opts[k].has_arg = options[i].has_arg;
            long_opts[k].flag = NULL;
            long_opts[k].val = options[i].val;
            k++;
        }
    }

    optstring[n] = '\0';
    long_opts[k].name = NULL;
    long_opts[k].has_arg = 0;
    long_opts[k].flag = NULL;
    long_opts[k].val = 0;

    tables_built = true;
}

static size_t out_width(void)
{
    const char *env;
    char *end;
    long val;

    env = getenv("COLUMNS");
    if (env == NULL || *env < '0' || *env > '9') {
        return WIDTH_DEFAULT;
    }

    errno = 0;
    val = strtol(env, &end, 10);
    if (errno != 0 || *end != '\0' || val < WIDTH_MIN || val > WIDTH_MAX) {
        return WIDTH_DEFAULT;
    }

    return (size_t)val;
}

/* Returns the new length. */
static size_t sappend(char *buf, size_t bufsz, size_t n, const char *s)
{
    while (*s != '\0' && n + 1 < bufsz) {
        buf[n++] = *s++;
    }
    buf[n] = '\0';

    return n;
}

/* Returns the width written. */
static size_t option_spec(char *buf, size_t bufsz, const struct option_def *opt)
{
    size_t n;

    n = 0;
    if (opt->val < OPTVAL_HELP) {
        n = sappend(buf, bufsz, n, "  -");
        if (n + 1 < bufsz) {
            buf[n++] = (char)opt->val;
            buf[n] = '\0';
        }
        if (opt->name != NULL) {
            n = sappend(buf, bufsz, n, ", ");
        }
    } else {
        n = sappend(buf, bufsz, n, "      ");
    }

    if (opt->name != NULL) {
        n = sappend(buf, bufsz, n, "--");
        n = sappend(buf, bufsz, n, opt->name);
    }

    if (opt->has_arg == OPT_REQUIRED) {
        n = sappend(buf, bufsz, n, "=");
        n = sappend(buf, bufsz, n, opt->argname);
    } else if (opt->has_arg == OPT_OPTIONAL) {
        n = sappend(buf, bufsz, n, "[=");
        n = sappend(buf, bufsz, n, opt->argname);
        n = sappend(buf, bufsz, n, "]");
    }

    return n;
}

static void put_wrapped(FILE *fp, const char *text, size_t indent, size_t col,
                        size_t width)
{
    const char *word;
    size_t len, gap, i;

    while (*text != '\0') {
        gap = 0;
        while (*text == ' ') {
            text++;
            gap++;
        }
        if (*text == '\0') {
            break;
        }

        word = text;
        while (*text != '\0' && *text != ' ') {
            text++;
        }
        len = (size_t)(text - word);

        if (col > indent) {
            if (col + gap + len > width) {
                fputc('\n', fp);
                for (i = 0; i < indent; i++) {
                    fputc(' ', fp);
                }
                col = indent;
            } else {
                for (i = 0; i < gap; i++) {
                    fputc(' ', fp);
                }
                col += gap;
            }
        }

        fwrite(word, 1, len, fp);
        col += len;
    }

    fputc('\n', fp);
}

static bool idx_after(size_t a, size_t b, bool by_name)
{
    if (by_name) {
        return strcmp(options[a].name, options[b].name) > 0;
    }

    return options[a].val > options[b].val;
}

static void sort_idx(size_t *idx, size_t n, bool by_name)
{
    size_t i, j, held;

    for (i = 1; i < n; i++) {
        held = idx[i];
        j = i;
        while (j > 0 && idx_after(idx[j - 1], held, by_name)) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = held;
    }
}

static void put_synopsis(FILE *fp, size_t width)
{
    char line[SYNOPSIS_MAX];
    char cluster[NOPTIONS + 1];
    size_t shorts[NOPTIONS], longs[NOPTIONS];
    size_t i, j, n, nshort, nlong, ncluster, indent;
    char held;

    ncluster = 0;
    nshort = 0;
    nlong = 0;

    for (i = 0; i < NOPTIONS; i++) {
        if (!options[i].in_synopsis) {
            continue;
        }
        if (options[i].val >= OPTVAL_HELP) {
            longs[nlong++] = i;
        } else if (options[i].has_arg == OPT_NONE) {
            cluster[ncluster++] = (char)options[i].val;
        } else {
            shorts[nshort++] = i;
        }
    }

    for (i = 1; i < ncluster; i++) {
        held = cluster[i];
        j = i;
        while (j > 0 && cluster[j - 1] > held) {
            cluster[j] = cluster[j - 1];
            j--;
        }
        cluster[j] = held;
    }
    cluster[ncluster] = '\0';

    sort_idx(shorts, nshort, false);
    sort_idx(longs, nlong, true);

    n = 0;
    if (ncluster > 0) {
        n = sappend(line, sizeof line, n, "[-");
        n = sappend(line, sizeof line, n, cluster);
        n = sappend(line, sizeof line, n, "]");
    }

    for (i = 0; i < nshort; i++) {
        if (n > 0) {
            n = sappend(line, sizeof line, n, " ");
        }
        n = sappend(line, sizeof line, n, "[-");
        if (n + 1 < sizeof line) {
            line[n++] = (char)options[shorts[i]].val;
            line[n] = '\0';
        }
        n = sappend(line, sizeof line, n, " ");
        n = sappend(line, sizeof line, n, options[shorts[i]].argname);
        n = sappend(line, sizeof line, n, "]");
    }

    for (i = 0; i < nlong; i++) {
        if (n > 0) {
            n = sappend(line, sizeof line, n, " ");
        }
        n = sappend(line, sizeof line, n, "[--");
        n = sappend(line, sizeof line, n, options[longs[i]].name);
        if (options[longs[i]].has_arg == OPT_REQUIRED) {
            n = sappend(line, sizeof line, n, "=");
            n = sappend(line, sizeof line, n, options[longs[i]].argname);
        } else if (options[longs[i]].has_arg == OPT_OPTIONAL) {
            n = sappend(line, sizeof line, n, "[=");
            n = sappend(line, sizeof line, n, options[longs[i]].argname);
            n = sappend(line, sizeof line, n, "]");
        }
        n = sappend(line, sizeof line, n, "]");
    }

    if (n > 0) {
        n = sappend(line, sizeof line, n, " ");
    }
    sappend(line, sizeof line, n, "file ...");

    fprintf(fp, "usage: %s ", program_name);
    indent = strlen("usage: ") + strlen(program_name) + 1;
    put_wrapped(fp, line, indent, indent, width);
}

/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* options.c - the option table, and everything derived from it */

#include "newfile.h"
#include "optparse.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the help wraps when the environment says nothing useful. */
#define WIDTH_DEFAULT 80
#define WIDTH_MIN     40
#define WIDTH_MAX     1000

/* Long enough for any row the table below can produce. */
#define SPEC_MAX 128

struct option_def {
    int val;              /* short letter, or OPTVAL_* when long only */
    const char *name;     /* long name, NULL for a short-only option */
    int has_arg;
    const char *argname;  /* NULL when has_arg is OPT_NONE */
    const char *desc;
    bool in_synopsis;
};

/* The one place an option is written down.  Listed in help order; the
 * synopsis sorts its own groups. */
static const struct option_def options[] = {
    {'m', "mode", OPT_REQUIRED, "mode",
     "set file permission bits (octal or symbolic)", true},
    {'o', "owner", OPT_REQUIRED, "owner[:group]",
     "set file ownership (may require privileges)", true},
    {OPTVAL_REFERENCE, "reference", OPT_REQUIRED, "file",
     "use file's mode instead of specifying -m", true},
    {'s', "size", OPT_REQUIRED, "size",
     "fill file with NUL bytes to size", true},
    {OPTVAL_SPARSE, "sparse", OPT_NONE, NULL,
     "with -s, create a sparse file instead", true},
    {OPTVAL_TEMPLATE, "template", OPT_REQUIRED, "file",
     "copy content from file (- for stdin)", true},
    {'p', "parents", OPT_NONE, NULL,
     "create parent directories as needed", true},
    {'f', "force", OPT_NONE, NULL,
     "replace the file if it already exists", true},
    {OPTVAL_BACKUP, "backup", OPT_OPTIONAL, "control",
     "make backup before replacing with -f", false},
    {'b', NULL, OPT_NONE, NULL,
     "like --backup with no control argument", true},
    {'A', "absolute", OPT_NONE, NULL,
     "apply the file mode exactly, bypassing the umask", true},
    {'v', "verbose", OPT_NONE, NULL,
     "explain what is being done", true},
    {OPTVAL_HELP, "help", OPT_NONE, NULL,
     "display this help and exit", false},
    {OPTVAL_VERSION, "version", OPT_NONE, NULL,
     "output version information and exit", false}
};

#define NOPTIONS (sizeof(options) / sizeof(options[0]))

/* 64 per option covers "[--name[=argname]] " for any name here. */
#define SYNOPSIS_MAX (NOPTIONS * 64 + 16)

/* One logical line each, wrapped only when it does not fit.  An empty
 * entry is a blank line. */
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
    "Backup control values: none (or off), numbered (or t), "
    "existing (or nil),",
    "simple (or never).",
    "Default control value is $VERSION_CONTROL, or 'existing' if unset.",
    "",
    "By default, an existing file is never touched.  Use -f to replace it.",
    "A file appears with its final mode, owner and content, or not at all.",
    ""
};

#define NNOTES (sizeof(notes) / sizeof(notes[0]))

/* An optional argument needs two colons, hence three bytes per option. */
static char optstring[3 * NOPTIONS + 1];
static struct opt_option long_opts[NOPTIONS + 1];
static bool tables_built;

static void build_tables(void);
static size_t out_width(void);
static size_t sappend(char *buf, size_t bufsz, size_t n, const char *s);
static size_t option_spec(char *buf, size_t bufsz,
                          const struct option_def *opt);
static void put_wrapped(FILE *fp, const char *text, size_t indent,
                        size_t col, size_t width);
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

/*
 * options_usage - print the synopsis, plus the option summary on success
 *
 * Does not return.
 */
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

    exit(stdout_ok() ? status : EXIT_FAILURE);
}

/* build_tables - derive the optstring and opt_option[] from options[] */
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

/* Anything the environment cannot answer for falls back to the default. */
static size_t out_width(void)
{
    const char *env;
    char *end;
    long val;

    /* strtol would accept leading whitespace and a sign. */
    env = getenv("COLUMNS");
    if (env == NULL || !isdigit((unsigned char)*env)) {
        return WIDTH_DEFAULT;
    }

    errno = 0;
    val = strtol(env, &end, 10);
    if (errno != 0 || *end != '\0'
        || val < WIDTH_MIN || val > WIDTH_MAX) {
        return WIDTH_DEFAULT;
    }

    return (size_t)val;
}

/* Append s at n, stopping at the end of buf.  Returns the new length. */
static size_t sappend(char *buf, size_t bufsz, size_t n, const char *s)
{
    while (*s != '\0' && n + 1 < bufsz) {
        buf[n++] = *s++;
    }
    buf[n] = '\0';

    return n;
}

/* Render the "  -m, --mode=mode" column.  Returns its width. */
static size_t option_spec(char *buf, size_t bufsz,
                          const struct option_def *opt)
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

/*
 * put_wrapped - print text broken at spaces, hanging-indented by indent
 *
 * Starts at column col.  Words are never split, so a long one overruns
 * width.  Runs of spaces are preserved.
 */
static void put_wrapped(FILE *fp, const char *text, size_t indent,
                        size_t col, size_t width)
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

/* True when a sorts after b, by long name or by short letter. */
static bool idx_after(size_t a, size_t b, bool by_name)
{
    if (by_name) {
        return strcmp(options[a].name, options[b].name) > 0;
    }

    return options[a].val > options[b].val;
}

/* Insertion sort, over a list that never outgrows the table. */
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

/* put_synopsis - build the synopsis and hang it under the program name */
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

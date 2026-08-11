/*
 * Copyright (c) 2026, Arka Mondal. All rights reserved.
 * Use of this source code is governed by a BSD-style license that
 * can be found in the LICENSE file.
 */

/* main.c - create file(s) with specified attributes */

#include "newfile.h"
#include "optparse.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *program_name = "newfile";

/* Command-line settings, shared by every operand.  Widest members
 * first to avoid padding holes. */
struct file_spec {
    off_t size;
    const char *template_file;
    const char *backup_control;
    const char *stdin_buf;
    size_t stdin_len;
    mode_t mode;
    mode_t effective_umask;
    mode_t creation_umask;
    uid_t uid;
    gid_t gid;
    int verbose;
    bool absolute_given;
    bool force_mode;
    bool parents_mode;
    bool sparse_mode;
    bool size_given;
    bool template_given;
    bool backup_given;
    bool set_owner;
    bool set_uid;
    bool set_gid;
    bool umask_in_loop;
    bool stdin_buffered;
};

static void parse_options(int argc, char **argv, struct file_spec *spec);
static int create_one(const char *path, const struct file_spec *spec);
static void display_version(void);
static void cleanup_handler(int sig);
static void setup_sighandlers(void);

int main(int argc, char **argv)
{
    struct file_spec spec;
    bool error_occurred;
    bool exact_open;
    bool hoist_umask;
    const char *base;
    char *stdin_buf;
    size_t stdin_len;
    int nfiles;

    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        base = strrchr(argv[0], '/');
        program_name = (base != NULL) ? base + 1 : argv[0];
    }

    parse_options(argc, argv, &spec);

    setup_sighandlers();

    error_occurred = false;
    stdin_buf = NULL;
    stdin_len = 0;
    nfiles = argc - opt_ind;

    /* Buffer: a pipe cannot be re-read for each operand */
    if (spec.template_given && strcmp(spec.template_file, "-") == 0
        && nfiles > 1) {
        if (read_all(STDIN_FILENO, &stdin_buf, &stdin_len) != 0) {
            errorexit("cannot read standard input: %s\n",
                      strerror(errno));
        }
        spec.stdin_buf = stdin_buf;
        spec.stdin_len = stdin_len;
        spec.stdin_buffered = true;
    }

    /* Read the umask without changing it */
    spec.creation_umask = umask(0);
    umask(spec.creation_umask);
    spec.effective_umask = spec.absolute_given ? 0 : spec.creation_umask;

    exact_open = spec.set_owner || spec.absolute_given;
    spec.umask_in_loop = exact_open && spec.parents_mode;
    hoist_umask = exact_open && !spec.parents_mode && !spec.force_mode;

    /* -p directories must keep the umask, hence the exclusion above. */
    if (hoist_umask) {
        umask(0);
    }

    for (; opt_ind < argc; opt_ind++) {
        if (create_one(argv[opt_ind], &spec) != 0) {
            error_occurred = true;
        }
    }

    if (hoist_umask) {
        umask(spec.creation_umask);
    }

    free(stdin_buf);

    if (!stdout_ok()) {
        error_occurred = true;
    }

    return error_occurred ? EXIT_FAILURE : EXIT_SUCCESS;
}

/*
 * parse_options - fill spec from argv and validate the combination
 *
 * Exits on a bad option.  opt_ind indexes the first operand on return.
 */
static void parse_options(int argc, char **argv, struct file_spec *spec)
{
    bool mode_given;      /* -m/--mode */
    bool owner_given;     /* -o/--owner */
    bool reference_given; /* --reference */
    int opt_index, option;
    const char *reference_file;
    const char *mode_arg;
    const char *owner_arg;
    const char *size_arg;
    struct stat ref_st;

    mode_given = false;
    owner_given = false;
    reference_given = false;
    opt_index = 0;
    reference_file = NULL;
    mode_arg = NULL;
    owner_arg = NULL;
    size_arg = NULL;

    spec->mode = 0;
    spec->effective_umask = 0;
    spec->creation_umask = 0;
    spec->size = 0;
    spec->uid = 0;
    spec->gid = 0;
    spec->template_file = NULL;
    spec->backup_control = NULL;
    spec->stdin_buf = NULL;
    spec->stdin_len = 0;
    spec->absolute_given = false;
    spec->force_mode = false;
    spec->parents_mode = false;
    spec->sparse_mode = false;
    spec->size_given = false;
    spec->template_given = false;
    spec->backup_given = false;
    spec->set_owner = false;
    spec->set_uid = false;
    spec->set_gid = false;
    spec->umask_in_loop = false;
    spec->stdin_buffered = false;
    spec->verbose = 0;

    while ((option = optparse_long(argc, argv, options_optstring(),
                                   options_long(), &opt_index)) != -1) {
        switch (option) {
        case OPTVAL_HELP:
            options_usage(EXIT_SUCCESS);
        case OPTVAL_VERSION:
            display_version();
            exit(stdout_ok() ? EXIT_SUCCESS : EXIT_FAILURE);
        case 'm':
            mode_given = true;
            mode_arg = opt_arg;
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
            owner_given = true;
            owner_arg = opt_arg;
            break;
        case OPTVAL_REFERENCE:
            reference_given = true;
            reference_file = opt_arg;
            break;
        case 's':
            spec->size_given = true;
            size_arg = opt_arg;
            break;
        case OPTVAL_TEMPLATE:
            spec->template_given = true;
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
        case ':':
            if (opt_name != NULL) {
                output_error("option '--%s' requires an argument\n",
                             opt_name);
            } else {
                output_error("option requires an argument -- %c\n",
                             opt_opt);
            }
            options_usage(EXIT_FAILURE);
        case '=':
            output_error("option '--%s' does not take an argument\n",
                         opt_name);
            options_usage(EXIT_FAILURE);
        case '?':
            if (opt_name != NULL) {
                output_error("unknown option '%s'\n", opt_name);
            } else {
                output_error("unknown option -- %c\n", opt_opt);
            }
            options_usage(EXIT_FAILURE);
        default:
            options_usage(EXIT_FAILURE);
        }
    }

    if (opt_ind == argc) {
        output_error("missing operand\n");
        options_usage(EXIT_FAILURE);
    }

    if (mode_given && reference_given) {
        errorexit("options '--mode' and '--reference' are "
                  "mutually exclusive\n");
    }

    if (spec->template_given && spec->size_given) {
        errorexit("options '--template' and '--size' are "
                  "mutually exclusive\n");
    }

    if (spec->backup_given && !spec->force_mode) {
        output_error("warning: backup option has no effect"
                     " without --force\n");
    }

    if (spec->sparse_mode && !spec->size_given) {
        output_error("warning: --sparse has no effect"
                     " without --size\n");
    }

    if (spec->backup_given && spec->force_mode) {
        if (spec->backup_control == NULL) {
            spec->backup_control = getenv("VERSION_CONTROL");
            if (spec->backup_control == NULL) {
                spec->backup_control = "existing";
            }
        }
        if (strcmp(spec->backup_control, "simple") != 0
            && strcmp(spec->backup_control, "never") != 0
            && strcmp(spec->backup_control, "none") != 0
            && strcmp(spec->backup_control, "off") != 0
            && strcmp(spec->backup_control, "numbered") != 0
            && strcmp(spec->backup_control, "t") != 0
            && strcmp(spec->backup_control, "existing") != 0
            && strcmp(spec->backup_control, "nil") != 0) {
            errorexit("invalid backup control '%s'\n",
                      spec->backup_control);
        }
    }

    if (reference_given) {
        if (stat(reference_file, &ref_st) != 0) {
            errorexit("cannot stat reference file '%s': %s\n",
                      reference_file, strerror(errno));
        }
        spec->mode = ref_st.st_mode & 07777;
    } else if (mode_given) {
        spec->mode = parse_mode(mode_arg);
    } else {
        spec->mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP
                     | S_IROTH | S_IWOTH;
    }

    if (owner_given
        && parse_owner(owner_arg, &spec->uid, &spec->gid,
                       &spec->set_uid, &spec->set_gid) != 0) {
        errorexit("invalid owner: '%s'\n", owner_arg);
    }
    spec->set_owner = owner_given;

    if (spec->size_given) {
        spec->size = parse_size(size_arg);
    }
}

/*
 * create_one - create one file to spec
 *
 * Every exit closes the descriptor and discards
 * an uncommitted file.  Returns 0, or -1.
 */
static int create_one(const char *path, const struct file_spec *spec)
{
    bool file_failed;
    int fd, template_ret, ret;
    int operand_errno;
    mode_t open_mode;
    uid_t uid_arg;
    gid_t gid_arg;
    char *tmppath;
    char *backup_name;
    size_t arglen;

    /* An empty operand names nothing; a trailing slash names a
     * directory. */
    arglen = strlen(path);
    if (arglen == 0 || path[arglen - 1] == '/') {
        operand_errno = (arglen == 0) ? ENOENT : EISDIR;
        output_error("cannot create '%s': %s\n",
                     path, strerror(operand_errno));
        return -1;
    }

    ret = 0;
    fd = -1;
    file_failed = false;
    tmppath = NULL;
    backup_name = NULL;

    if (spec->parents_mode) {
        if (make_parents(path) != 0) {
            output_error("cannot create parent directories "
                         "for '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
    }

    if (spec->force_mode) {
        fd = temp_open(path, &tmppath);
        if (fd == -1) {
            output_error("cannot create a temporary file "
                         "for '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
    } else {
        if (spec->set_owner) {
            open_mode = S_IRUSR | S_IWUSR;
        } else {
            open_mode = spec->mode;
        }

        /*
         * Bypass the umask where the open mode must land exactly: the
         * private -o mode, and any mode under -A.
         */
        if (spec->umask_in_loop) {
            umask(0);
        }

        fd = open(path, O_WRONLY | O_CREAT | O_EXCL, open_mode);

        if (spec->umask_in_loop) {
            umask(spec->creation_umask);
        }

        if (fd == -1) {
            output_error("cannot create '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }

        pending_arm(path);
    }

    if (spec->size_given) {
        if (spec->sparse_mode) {
            if (ftruncate(fd, spec->size) != 0) {
                output_error("cannot set size of '%s': %s\n",
                             path, strerror(errno));
                ret = -1;
                file_failed = true;
                goto cleanup;
            }
        } else {
            if (fill_file(fd, spec->size) != 0) {
                output_error("cannot fill '%s': %s\n",
                             path, strerror(errno));
                ret = -1;
                file_failed = true;
                goto cleanup;
            }
        }
    }

    if (spec->template_given) {
        if (strcmp(spec->template_file, "-") != 0) {
            template_ret = copy_template(fd, spec->template_file);
        } else if (spec->stdin_buffered) {
            template_ret = write_buf(fd, spec->stdin_buf,
                                     spec->stdin_len);
        } else {
            template_ret = copy_fd(fd, STDIN_FILENO);
        }
        if (template_ret != 0) {
            output_error("cannot copy template to '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
    }

    if (spec->set_owner) {
        uid_arg = spec->set_uid ? spec->uid : (uid_t)-1;
        gid_arg = spec->set_gid ? spec->gid : (gid_t)-1;
        if (fchown(fd, uid_arg, gid_arg) != 0) {
            output_error("cannot change ownership of '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
    }

    /*
     * fchown clears the setuid and setgid bits, so the mode goes on
     * after it.  A replacement is still 0600 from mkstemp.
     */
    if (spec->set_owner || spec->force_mode) {
        if (fchmod(fd, spec->mode & ~spec->effective_umask) != 0) {
            output_error("cannot set permissions of '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
    }

    if (spec->force_mode) {
        if (fsync(fd) != 0) {
            output_error("cannot flush '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }

        /* Catch deferred write errors before touching the target */
        if (close(fd) != 0) {
            fd = -1;
            output_error("cannot write '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
        fd = -1;

        if (spec->backup_given
            && make_backup(path, spec->backup_control,
                           &backup_name) != 0) {
            output_error("cannot back up '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }

        if (rename(tmppath, path) != 0) {
            output_error("cannot replace '%s': %s\n",
                         path, strerror(errno));
            if (backup_name != NULL) {
                unlink(backup_name);
            }
            ret = -1;
            file_failed = true;
            goto cleanup;
        }

        pending_clear();

        /* Already renamed; only the flush failed */
        if (sync_dir(path) != 0) {
            output_error("replaced '%s' but could not flush its "
                         "directory: %s\n",
                         path, strerror(errno));
            ret = -1;
        }
    } else {
        if (close(fd) != 0) {
            fd = -1;
            output_error("cannot write '%s': %s\n",
                         path, strerror(errno));
            ret = -1;
            file_failed = true;
            goto cleanup;
        }
        fd = -1;
        pending_clear();
    }

    if (spec->verbose) {
        if (backup_name != NULL) {
            printf("created '%s' (backup: '%s')\n", path, backup_name);
        } else {
            printf("created '%s'\n", path);
        }
    }

cleanup:
    if (fd != -1 && close(fd) != 0 && !file_failed) {
        output_error("cannot close '%s': %s\n", path, strerror(errno));
        ret = -1;
    }
    /* Armed means uncommitted */
    pending_discard();
    free(tmppath);
    free(backup_name);

    return ret;
}

static void display_version(void)
{
    printf("newfile %s\n", VERSION);
    puts("Copyright (c) 2026 Arka Mondal.");
    puts("License: BSD-3-Clause.");
    puts("Written by Arka Mondal.");
}

/*
 * cleanup_handler - discard the unfinished file, then die of the signal
 *
 * sa_mask blocks everything, so raise() only marks the signal pending
 * and returns.
 */
static void cleanup_handler(int sig)
{
    int saved_errno;

    saved_errno = errno;
    pending_cleanup();
    raise(sig);
    errno = saved_errno;
}

/* Catch the signals that would otherwise strand an unfinished file. */
static void setup_sighandlers(void)
{
    static const int signals[] = {
        SIGHUP, SIGINT, SIGPIPE, SIGQUIT, SIGTERM, SIGXCPU
    };
    struct sigaction sa, old;
    size_t i;

    sigfillset(&sa.sa_mask);

    /* Ignore: the write reports EFBIG and later operands still run */
    sa.sa_flags = 0;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGXFSZ, &sa, NULL);

    /* SA_RESETHAND, or the re-raise re-enters the handler */
    sa.sa_flags = (int)(SA_RESTART | SA_RESETHAND);
    sa.sa_handler = cleanup_handler;

    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
        if (sigaction(signals[i], NULL, &old) == 0
            && old.sa_handler != SIG_IGN) {
            sigaction(signals[i], &sa, NULL);
        }
    }
}

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

NAME    = newfile
VERSION = 0.3.0

BASE_CFLAGS  = -std=c99 -fPIE
BASE_LDFLAGS = -pie

CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=3 \
            -fstack-protector-strong
CPPFLAGS ?=
LDFLAGS  ?= -Wl,-z,relro,-z,now

LIBS = -lpthread

ALL_CFLAGS   = $(BASE_CFLAGS) $(CFLAGS)
ALL_CPPFLAGS = -DVERSION=\"$(VERSION)\" $(CPPFLAGS)
ALL_LDFLAGS  = $(BASE_LDFLAGS) $(LDFLAGS)

PREFIX  = /usr/local
BINDIR  = $(PREFIX)/bin
MANDIR  = $(PREFIX)/share/man
LICDIR  = $(PREFIX)/share/licenses/$(NAME)
DESTDIR =

BUILDDIR = build
BIN      = $(BUILDDIR)/$(NAME)

OBJS = $(BUILDDIR)/main.o $(BUILDDIR)/error.o $(BUILDDIR)/options.o \
       $(BUILDDIR)/optparse.o $(BUILDDIR)/parse.o $(BUILDDIR)/spec.o \
       $(BUILDDIR)/batch.o $(BUILDDIR)/pending.o $(BUILDDIR)/place.o \
       $(BUILDDIR)/paths.o $(BUILDDIR)/backup.o $(BUILDDIR)/flush.o \
       $(BUILDDIR)/io.o $(BUILDDIR)/template.o $(BUILDDIR)/holes.o \
       $(BUILDDIR)/fastio.o

SRCS = src/main.c src/error.c src/options.c src/optparse.c src/parse.c \
       src/spec.c src/batch.c src/pending.c src/place.c src/paths.c \
       src/backup.c src/flush.c src/io.c src/template.c src/holes.c \
       src/fastio.c tests/gapcheck.c
HDRS = src/newfile.h src/error.h src/options.h src/optparse.h src/parse.h \
       src/spec.h src/batch.h src/pending.h src/place.h src/paths.h \
       src/backup.h src/flush.h src/io.h src/template.h src/holes.h \
       src/fastio.h

DISTFILES = Makefile LICENSE README.md ChangeLog src doc tests bench
DISTDIR   = $(NAME)-$(VERSION)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(ALL_LDFLAGS) -o $@ $(OBJS) $(LIBS)

.SUFFIXES:

$(BUILDDIR)/main.o: src/main.c src/newfile.h src/batch.h src/error.h \
    src/pending.h src/spec.h src/template.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/main.c

$(BUILDDIR)/error.o: src/error.c src/newfile.h src/error.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/error.c

$(BUILDDIR)/options.o: src/options.c src/newfile.h src/options.h \
    src/error.h src/optparse.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/options.c

$(BUILDDIR)/optparse.o: src/optparse.c src/newfile.h src/optparse.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/optparse.c

$(BUILDDIR)/parse.o: src/parse.c src/newfile.h src/parse.h src/error.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/parse.c

$(BUILDDIR)/spec.o: src/spec.c src/newfile.h src/spec.h src/error.h \
    src/options.h src/optparse.h src/parse.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/spec.c

$(BUILDDIR)/batch.o: src/batch.c src/newfile.h src/batch.h src/backup.h \
    src/error.h src/fastio.h src/flush.h src/io.h src/paths.h \
    src/pending.h src/place.h src/spec.h src/template.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/batch.c

$(BUILDDIR)/pending.o: src/pending.c src/newfile.h src/pending.h \
    src/error.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/pending.c

$(BUILDDIR)/place.o: src/place.c src/newfile.h src/place.h src/error.h \
    src/fastio.h src/paths.h src/pending.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/place.c

$(BUILDDIR)/paths.o: src/paths.c src/newfile.h src/paths.h src/flush.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/paths.c

$(BUILDDIR)/backup.o: src/backup.c src/newfile.h src/backup.h \
    src/paths.h src/pending.h src/place.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/backup.c

$(BUILDDIR)/flush.o: src/flush.c src/newfile.h src/flush.h src/error.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/flush.c

$(BUILDDIR)/io.o: src/io.c src/newfile.h src/io.h src/fastio.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/io.c

$(BUILDDIR)/template.o: src/template.c src/newfile.h src/template.h \
    src/error.h src/fastio.h src/holes.h src/io.h src/pending.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/template.c

$(BUILDDIR)/holes.o: src/holes.c src/newfile.h src/holes.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/holes.c

$(BUILDDIR)/fastio.o: src/fastio.c src/newfile.h src/fastio.h
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/fastio.c

$(BUILDDIR)/gapcheck: tests/gapcheck.c
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) $(ALL_LDFLAGS) -o $@ tests/gapcheck.c

helpers: $(BUILDDIR)/gapcheck

check: $(BIN) helpers
	NEWFILE=$(BIN) GAPCHECK=$(BUILDDIR)/gapcheck sh tests/check.sh

PORTABLE_FLAGS = -DNEWFILE_NO_FASTIO -DNEWFILE_NO_THREADS
NOLINK_FLAGS   = $(PORTABLE_FLAGS) -DNEWFILE_NO_LINK -DNEWFILE_NO_SEEKHOLE

check-portable:
	$(MAKE) BUILDDIR=build-notmpfile CPPFLAGS=-DNEWFILE_NO_TMPFILE check
	$(MAKE) BUILDDIR=build-portable CPPFLAGS='$(PORTABLE_FLAGS)' check
	$(MAKE) BUILDDIR=build-nolink CPPFLAGS='$(NOLINK_FLAGS)' check

check-faults: $(BIN)
	NEWFILE=$(BIN) sh tests/faults.sh

STYLE_CONFIGS = '' '$(NOLINK_FLAGS)' '-DNEWFILE_NO_TMPFILE -DNEWFILE_NO_SENDFILE'
ANALYZER_CC   = gcc
CPPCHECK_SUPPRESS = --suppress=missingIncludeSystem --suppress=checkersReport \
                    --suppress=unmatchedSuppression \
                    --suppress=variableScope \
                    --suppress=constParameterCallback:src/place.c \
                    --suppress=constParameterPointer:src/fastio.c \
                    --suppress=knownConditionTrueFalse:src/holes.c

SCRIPTS = tests/lib.sh tests/check.sh tests/faults.sh tests/cases/*.sh \
          tests/ci/*.sh bench/run_bench.sh

check-style:
	clang-format --dry-run --Werror $(SRCS) $(HDRS)
	shellcheck -x $(SCRIPTS)
	out=$$(groff -man -ww -z doc/$(NAME).1 2>&1) && [ -z "$$out" ] || { \
	    echo "$$out" >&2; exit 1; }
	for c in $(STYLE_CONFIGS); do \
	    for f in $(SRCS); do \
	        clang-tidy --quiet $$f -- -std=c99 $(ALL_CPPFLAGS) $$c || exit 1; \
	    done; \
	    cppcheck --enable=all --std=c99 --check-level=exhaustive -q \
	        --error-exitcode=1 $(CPPCHECK_SUPPRESS) -D__linux__ -D__GNUC__ \
	        $(ALL_CPPFLAGS) $$c $(SRCS) || exit 1; \
	    for f in $(SRCS); do \
	        case $$f in src/main.c) w=-Wno-analyzer-fd-leak ;; *) w= ;; esac; \
	        $(ANALYZER_CC) -std=c99 -O2 -fanalyzer -Wall -Wextra -Werror $$w \
	            $(ALL_CPPFLAGS) $$c -c -o /dev/null $$f || exit 1; \
	    done; \
	done

check-fs:
	sh tests/ci/run.sh dev fs

ci:
	sh tests/ci/run.sh dev all

release-check:
	sh tests/ci/run.sh release all

install: $(BIN)
	mkdir -p $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp $(BIN) $(DESTDIR)$(BINDIR)/$(NAME).new
	touch -r $(BIN) $(DESTDIR)$(BINDIR)/$(NAME).new
	chmod 755 $(DESTDIR)$(BINDIR)/$(NAME).new
	mv $(DESTDIR)$(BINDIR)/$(NAME).new $(DESTDIR)$(BINDIR)/$(NAME)
	cp doc/$(NAME).1 $(DESTDIR)$(MANDIR)/man1/$(NAME).1.new
	touch -r doc/$(NAME).1 $(DESTDIR)$(MANDIR)/man1/$(NAME).1.new
	chmod 644 $(DESTDIR)$(MANDIR)/man1/$(NAME).1.new
	mv $(DESTDIR)$(MANDIR)/man1/$(NAME).1.new \
	   $(DESTDIR)$(MANDIR)/man1/$(NAME).1
	mkdir -p $(DESTDIR)$(LICDIR)
	cp LICENSE $(DESTDIR)$(LICDIR)/LICENSE.new
	touch -r LICENSE $(DESTDIR)$(LICDIR)/LICENSE.new
	chmod 644 $(DESTDIR)$(LICDIR)/LICENSE.new
	mv $(DESTDIR)$(LICDIR)/LICENSE.new $(DESTDIR)$(LICDIR)/LICENSE

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(NAME)
	rm -f $(DESTDIR)$(MANDIR)/man1/$(NAME).1
	rm -f $(DESTDIR)$(LICDIR)/LICENSE

dist:
	rm -rf $(DISTDIR)
	mkdir $(DISTDIR)
	cp -R $(DISTFILES) $(DISTDIR)
	tar cf - $(DISTDIR) | xz > $(DISTDIR).tar.xz
	rm -rf $(DISTDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf build-notmpfile build-portable build-nolink
	rm -rf $(DISTDIR) $(DISTDIR).tar.xz

.PHONY: all helpers check check-portable check-faults check-style check-fs \
        ci release-check install uninstall dist clean

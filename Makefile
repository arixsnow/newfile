# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Arka Mondal. All rights reserved.
# Use of this source code is governed by a BSD-style license that
# can be found in the LICENSE file.

# Portable makefile: explicit rules only, no conditionals, no pattern
# rules.  Any POSIX-conforming make reads it.

NAME    = newfile
VERSION = 0.2.0

BASE_CFLAGS  = -std=c99 -fPIE
BASE_LDFLAGS = -pie

CC       ?= cc
CFLAGS   ?= -O2 -Wall -Wextra -Werror -D_FORTIFY_SOURCE=3 \
            -fstack-protector-strong
CPPFLAGS ?=
LDFLAGS  ?= -Wl,-z,relro,-z,now

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

OBJS = $(BUILDDIR)/main.o $(BUILDDIR)/error.o $(BUILDDIR)/parse.o \
       $(BUILDDIR)/fileops.o $(BUILDDIR)/template.o $(BUILDDIR)/fastio.o \
       $(BUILDDIR)/holes.o $(BUILDDIR)/options.o $(BUILDDIR)/optparse.o
HDRS = src/newfile.h src/optparse.h

DISTFILES = Makefile LICENSE README.md ChangeLog src doc tests bench
DISTDIR   = $(NAME)-$(VERSION)

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(ALL_LDFLAGS) -o $@ $(OBJS)

.SUFFIXES:

$(BUILDDIR)/main.o: src/main.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/main.c

$(BUILDDIR)/error.o: src/error.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/error.c

$(BUILDDIR)/parse.o: src/parse.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/parse.c

$(BUILDDIR)/fileops.o: src/fileops.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/fileops.c

$(BUILDDIR)/template.o: src/template.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/template.c

$(BUILDDIR)/fastio.o: src/fastio.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/fastio.c

$(BUILDDIR)/holes.o: src/holes.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/holes.c

$(BUILDDIR)/options.o: src/options.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/options.c

$(BUILDDIR)/optparse.o: src/optparse.c $(HDRS)
	mkdir -p $(BUILDDIR)
	$(CC) $(ALL_CFLAGS) $(ALL_CPPFLAGS) -c -o $@ src/optparse.c

check: $(BIN)
	NEWFILE=$(BIN) sh tests/run_tests.sh

# Builds with the platform fast paths left out.  Exercises the read and
# write path every other system takes.
check-portable:
	$(MAKE) BUILDDIR=build-portable CPPFLAGS=-DNEWFILE_NO_FASTIO all
	NEWFILE=build-portable/$(NAME) sh tests/run_tests.sh

# Renamed into place: replacing a running binary cannot fail with
# ETXTBSY, and no one sees a half-written file.
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

# POSIX tar has no compression flag, so the archive goes through a pipe.
dist:
	rm -rf $(DISTDIR)
	mkdir $(DISTDIR)
	cp -R $(DISTFILES) $(DISTDIR)
	tar cf - $(DISTDIR) | xz > $(DISTDIR).tar.xz
	rm -rf $(DISTDIR)

clean:
	rm -rf $(BUILDDIR)
	rm -rf build-portable
	rm -rf $(DISTDIR) $(DISTDIR).tar.xz

.PHONY: all check check-portable install uninstall dist clean

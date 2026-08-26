# newfile

Create a file with its permission mode, owner, size and initial content set,
in one command.

A file appears with all of them already set, or it does not appear. An existing
path is never replaced unless `-f` is given, and a replacement is committed
with `rename(2)`.

See `newfile(1)` for the full description.

## Examples

Create an executable shell script:

```sh
newfile -m 0755 script.sh
```

Drop a private key into a directory that may not yet exist:

```sh
newfile -p -m 0600 -o www-data:www-data /var/www/secrets/app.key
```

Replace a file, keeping a numbered backup of the previous version:

```sh
newfile -f --backup=numbered -m 0644 config.txt
```

Run `newfile --help` for the full option summary.

## Building

You need a C99 compiler and any POSIX-conforming `make`.

```sh
make
make check
make install
```

Output goes to `build/`, overridable with `BUILDDIR`.

`make install` puts the binary in `/usr/local/bin`, the man page in
`/usr/local/share/man/man1` and the licence in
`/usr/local/share/licenses/newfile`. Override with `PREFIX`, and use `DESTDIR`
when staging into a package root:

```sh
make install PREFIX=/usr DESTDIR=/tmp/pkg
```

`CC`, `CFLAGS`, `CPPFLAGS` and `LDFLAGS` are taken from the environment or the
command line:

```sh
make CC=clang CFLAGS='-O0 -g'
```

A value for `CFLAGS` replaces the default `-O2 -Wall -Wextra -Werror
-D_FORTIFY_SOURCE=3 -fstack-protector-strong`. A value for `LDFLAGS` replaces
`-Wl,-z,relro,-z,now`.

`BASE_CFLAGS` and `BASE_LDFLAGS` are prepended to those. They hold `-std=c99`
and the `-fPIE`/`-pie` pair, which must stay together because `-pie` without
`-fPIE` fails to link. `-DVERSION` is prepended to `CPPFLAGS` the same way, so
setting `CPPFLAGS` does not drop the version string. A compiler that rejects
any of these needs `BASE_CFLAGS` or `BASE_LDFLAGS` overridden too.

`-Werror` applies to a plain `make` only. Setting `CFLAGS` drops it.

`make check` runs the shell test suite in `tests/`, covering option parsing,
mode and size handling, the layout a template gives the copy, atomic
replacement, survival of a failed write, backups and per-file error behaviour.
`make check-portable` runs the same suite against a build with the
platform-specific fast paths compiled out, so the two runs together show that
the result does not depend on which path the copy took. `make dist` packs the
source into a `.tar.xz`.

`bench/run_bench.sh` times the copy and creation paths, and compares two
binaries when `NEWFILE_OLD` and `NEWFILE_NEW` are both set. Set `BENCHDIR` to
choose the filesystem measured. The default may put it in memory rather than on
a disk.

## Documentation

`man newfile` after installing, or `man -l doc/newfile.1` from the source tree.

## Reporting bugs

<https://github.com/arixsnow/newfile/issues>

## License

BSD 3-Clause. See [LICENSE](LICENSE) for the full terms. `make install` puts a
copy in `$PREFIX/share/licenses/newfile/`.

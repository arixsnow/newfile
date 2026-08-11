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

The compiler and flags are overridable the usual way:

```sh
make CC=clang CFLAGS='-std=c99 -O2 -Wall -Wextra -g'
```

The default build is hardened with `_FORTIFY_SOURCE=3`, a stack protector, PIE
and full RELRO. Clear both variables for a compiler that rejects those flags:

```sh
make CFLAGS='-std=c99 -O2 -Wall -Wextra' LDFLAGS=''
```

`make check` runs the shell test suite in `tests/`, covering option parsing,
mode and size handling, atomic replacement, survival of a failed write, backups
and per-file error behaviour. `make check-portable` runs the same suite against
a build with the platform-specific fast paths compiled out. `make dist` packs
the source into a `.tar.xz`.

## Documentation

`man newfile` after installing, or `man -l doc/newfile.1` from the source tree.

## Reporting bugs

<https://github.com/arixsnow/newfile/issues>

## License

BSD 3-Clause. See [LICENSE](LICENSE) for the full terms. `make install` puts a
copy in `$PREFIX/share/licenses/newfile/`.

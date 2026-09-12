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

## Installing

Ubuntu and derivatives, from the
[PPA](https://launchpad.net/~arixsnow/+archive/ubuntu/newfile):

```sh
sudo add-apt-repository ppa:arixsnow/newfile
sudo apt install newfile
```

Fedora, from
[Copr](https://copr.fedorainfracloud.org/coprs/arixsnow/newfile/):

```sh
sudo dnf copr enable arixsnow/newfile
sudo dnf install newfile
```

Anywhere else, build from source.

## Building

You need a C99 compiler and any POSIX-conforming `make`.

```sh
make
make check
make install
```

Output goes to `build/`, overridable with `BUILDDIR`.

`make install` uses `/usr/local`. Override with `PREFIX`, and use `DESTDIR`
when staging into a package root:

```sh
make install PREFIX=/usr DESTDIR=/tmp/pkg
```

`CC`, `CFLAGS`, `CPPFLAGS` and `LDFLAGS` are read from the environment or the
command line. A value for `CFLAGS` replaces the default rather than adding to
it.

`make check` runs the test suite. `make dist` packs the source into a
`.tar.xz`.

## Documentation

`man newfile` after installing, or `man -l doc/newfile.1` from the source tree.

## Reporting bugs

<https://github.com/arixsnow/newfile/issues>

## License

BSD 3-Clause. See [LICENSE](LICENSE) for the full terms. `make install` puts a
copy in `$PREFIX/share/licenses/newfile/`.

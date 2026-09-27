# tclsimrawreader - read SPICE .raw files with Tcl

`tclsimrawreader` is a Tcl C extension that allows to read SPICE3f5 raw-Binary and raw-ASCII files.
It doesn't read the whole file data into memory but read certain vectors lazily by request.

It targets Tcl 9.0 and works on Linux/macOS/Windows.

## What it gives you (at a glance)

- Reading raw files produced by Ngspice, Xyce, SPICE OPUS and LTspice
- Extension is written in C and allows to read large files without putting it into memory.
- Supports multi-plot files produced by .STEP or loops.
- Read both binary and ASCII formats.
- Ability to read only parts of the vector/vectors.

## Building & requirements

Requirements:

- Tcl headers/libs (9.0).

To install, run following commands:
- `git clone https://github.com/georgtree/tclsimrawreader.git`
- `./configure`
- `sudo make install`

During installation manpages are also installed.

For test package in place run `make test`.

For package uninstall run `sudo make uninstall`.

## Documentation

Documentation could be found [here](https://georgtree.github.io/tclsimrawreader/).

## Quick start

Package loading and initialization:

```tcl
package require tclsimrawreader

# Path to raw file
set rawFilePath /path/to/dc.raw

# Open a file by creating handle bounded to a Tcl command
set rawfile [tclsimrawreader::openraw $rawFilePath]
```

Read the vectors' names:

```tcl
$rawfile names
```

Get vector data:

```tcl
$rawfile vector ID(M1)
```

Get multiple vectors' data:

```tcl
$rawfile vectors {ID(M1) V(VD)}
```

Close file handle:

```tcl
$rawfile close
```

## Installation layout and removal

Installation follows the rbc-tk9 layout and honors the directories selected by `configure`:

- The package library, Tcl scripts and `pkgIndex.tcl` go together in `$(libdir)/$(PACKAGE_NAME)$(PACKAGE_VERSION)`.
- Any public headers and stub client sources go in `$(includedir)`; executable binaries go in `$(bindir)`.
- Manpages go in `$(mandir)/mann`.
- HTML documentation, its image/static resources, and `LICENSE` go in `$(datadir)/$(PACKAGE_NAME)$(PACKAGE_VERSION)/doc`.

Use `--prefix`, `--libdir`, `--includedir`, `--datadir` and `--mandir` at configure time to change these locations.
All install and uninstall targets honor `DESTDIR` for staging:

```sh
./configure --prefix=/your/prefix
make
make install DESTDIR=/your/staging/root
make uninstall DESTDIR=/your/staging/root
```

`make uninstall` runs `uninstall-binaries`, `uninstall-libraries` and `uninstall-doc`. It removes the package-owned
runtime directory, but removes only this package's named files from shared binary, include and documentation paths.
Unrelated documentation files are retained; empty documentation directories are removed. Keep the configured build
and source tree to uninstall the corresponding installation, and use the same path overrides for install and uninstall.

`DOC_INSTALL_DIR` can override the complete HTML destination. As in rbc-tk9, its default already includes `DESTDIR`;
when overriding it explicitly, include the staging root yourself if needed.

## Installation archives

`make dist` follows rbc-tk9: it builds the package, stages `make install` under the build directory, and creates
`dist/$(PACKAGE_NAME)$(PACKAGE_VERSION).tar.gz`. `make dist-zip` creates the same payload as a ZIP file as well.
`make dist-clean` removes this package's staging directory and both archives.

```sh
make -j4 dist
make dist-zip
```

These are platform-specific installation archives, not source distributions. Their contents are relative to the
configured installation prefix: normally `lib/`, `include/`, and `share/`, without an enclosing package directory.
Extract or merge the archive contents into the desired prefix. The payload uses the same install targets as normal
installation, including the built library, Tcl runtime files, public headers, manpages, HTML resources and license.
Tests, build files and examples that are not installed by `make install` are not included.

Configured installation directories must be below `prefix`; `dist` reports an error if one is outside it.
When using `--with-tcl` and a custom prefix, set `--exec-prefix` to the same prefix if TEA would otherwise inherit
Tcl's execution prefix, for example `sh ./configure --prefix=/opt/mypackages --exec-prefix=/opt/mypackages`. Custom
subdirectories inside that prefix are preserved. `DESTDIR` is not included in archive paths. The staging install does
not write to the configured system prefix. `DIST_ROOT` and `DIST_NAME` may be overridden to choose the archive output
directory and name; `DIST_NAME` must be a single directory name. Run `dist-clean` separately, not alongside `dist`
or `dist-zip` in the same parallel make invocation.


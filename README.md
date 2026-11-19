# tclsimrawreader - read SPICE .raw files with Tcl

[![Tests: Linux, Windows UCRT64 and MSVC](https://img.shields.io/github/actions/workflow/status/georgtree/tclsimrawreader/tests.yml?branch=main&label=Tests%3A%20Linux%2C%20Windows%20UCRT64%20and%20MSVC)](https://github.com/georgtree/tclsimrawreader/actions/workflows/tests.yml)

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

## Continuous integration and binary packages

Pushes build and test tclsimrawreader with direct RBC vector support on Linux GCC, Windows UCRT64 and Windows MSVC (x64),
using Tcl 9.1.0. Each workflow resolves the `latest` release of both
[RBC](https://github.com/georgtree/rbc-tk9/releases/tag/latest) and
[argparse](https://github.com/georgtree/argparse/releases/tag/latest) once for all three jobs. Each runner uses the
native RBC archive and the standalone argparse archive for its toolchain. The cache tracks the tag target, asset ID and
SHA-256 for each archive separately. Each download is verified before use.

Run **Actions → Build packages → Run workflow** to publish all three tested archives and their checksums to the [rolling
release](https://github.com/georgtree/tclsimrawreader/releases/tag/latest). Tcl, RBC, argparse and Tcllib are supplied
separately.

For a local MSVC build, use an **x64 Native Tools Command Prompt for Visual Studio 2022**, a matching Tcl 9.1
installation with headers and stub library, and the extracted native MSVC RBC distribution. The repository bundles the
Tcl Nmake rules and helper under `win/`, so Tcl installations without `lib/nmake` are supported.

```bat
cd win
set "INSTALLDIR=C:\Tcl91"
set "RBC_DIR=C:\deps\rbc"
set "PATH=%INSTALLDIR%\bin;%PATH%"
nmake /f makefile.vc
```

To run the tests with separately extracted dependencies, set `TCLLIBPATH` to a Tcl list of their directories.
For example, with argparse 0.70, RBC 0.8.5 and a Tcllib source checkout:

```bat
set "TCLLIBPATH={C:/deps/argparse/lib/argparse0.70} {C:/deps/rbc/lib/rbc0.8.5} {C:/deps/tcllib/modules}"
nmake /f makefile.vc test
```

The test target adds the build directory to the package search path. Use `nmake /f makefile.vc install` to install
the extension into the Tcl prefix, or `nmake /f makefile.vc dist-zip` to produce a distribution.

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

# Interactive help

All public methods have interactive help. To get information about command and its arguments call it with `-help`
switch:

```tcl
package require tclsimrawreader
namespace import ::tclsimrawreader::*
openraw -help
```

```text
Open a raw file and return its handle command.. Can accepts unambiguous prefixes
instead of switches names. Allows switches to appear after parameters.
    Switches:
        -dialect value - Raw-file dialect; defaults to generic. Value must be
            one of: generic or ltspice.
        -output value - Output representation; overrides the configured default.
            Value must be one of: list or vector.
        -ifexists value - Destination collision policy; overrides the configured
            default. Value must be one of: error or replace.
        -namespace value - Destination namespace; relative to the caller,
            created if missing.
        -help - Help switch, when provided, forces ignoring all other switches
            and parameters, prints the help message to stdout, and returns up to 1
            levels above the current level.
    Parameters:
        filepath - Path to the SPICE raw file.
```

Best to do it in interactive console, see [tkcon](https://github.com/bohagan1/TkCon)


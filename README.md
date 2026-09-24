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


## Optional RBC vector output

The default build and output remain Tcl lists (or dictionaries of lists), with no RBC dependency. To enable
real and complex [rbc-tk9](https://github.com/georgtree/rbc-tk9) output, use Tcl 9 and matching RBC 0.5.0 headers:

```sh
./configure --with-tcl=/path/to/tcl/lib --with-rbc=/path/to/rbc-tk9
make clean
make
```

`--with-rbc` accepts a source tree, installation prefix, or directory containing `rbcVector.h`, `rbcDecls.h` and
`rbcStubLib.c`, just as in tclmeasure. The portable stub client is compiled into this package; the RBC shared
library is not linked. `--without-rbc` selects the default list-only build. Even an enabled build can load and
read lists without RBC installed. `rbc::vector` is required only when vector output is selected. No Tk
initialization is needed, although the installed RBC binary must have its system library dependencies available.

Use an RBC build with literal parenthesized vector names (`vector create -literal true`) and namespace-safe
vector command deletion. The package version remains `rbc::vector 0.5.0`, so an older build with the same version
number is insufficient. This upgrade does not change the RBC stubs ABI or the optional `--with-rbc` configuration.

Set handle defaults with `-output list|vector` and `-ifexists error|replace`. Defaults are `list` and `error`.
Either option can be overridden for a single `vector` or `vectors` call, without changing the handle defaults:

```tcl
set raw [::tclsimrawreader::openraw file.raw -output vector -ifexists error]
set signal [$raw vector v(out)]
# -> ::v(out), if called from the global namespace
set values [$signal index :]
set commands [$raw vectors {time v(out)} -ifexists replace]
# -> time ::time v(out) ::v(out)
set values [$raw vector v(out) -output list]
$raw close
# Created vectors remain alive and are owned by the caller.
```

For `openraw`, options may precede or follow the file name. For read commands, `-output` and `-ifexists` may precede
or follow the selection; `-plot` precedes it as before. Existing `-from`, `-count`, selected-name lists, `-all`,
dialects and multi-plot handling work in both output modes. In vector mode, `vector` returns one fully qualified
command name; `vectors` returns a dictionary whose original raw-name keys map to fully qualified command names.
An empty selection returns an empty dictionary. A zero-length range creates an empty vector.

### Destination names and ownership

Raw names are preserved whenever they are safe literal RBC names. Balanced parentheses, underscores, periods,
letters, digits, `@` and single interior colons are retained. Vectors are created with `-literal true`, so numeric
names such as `v(1)` or `v(1)(2:4)` are never interpreted as length/range specifications. Examples:

| Raw name | Destination tail |
| --- | --- |
| `time` | `time` |
| `v(out)` | `v(out)` |
| `v(1)` | `v(1)` |
| `v_28out_29` | `v_28out_29` |
| `L2:flux` | `L2:flux` |
| `v(a-b)` | `_raw_7628612D6229` |
| `sub::node` | `_raw_7375623A3A6E6F6465` |

Names with unsupported characters, unbalanced parentheses, `::`, a leading/trailing colon, or the reserved prefix
`_raw_` use a fallback: `_raw_` followed by the uppercase hexadecimal UTF-8 bytes of the complete original name.
The empty name maps to `_raw_`. Reserving this prefix keeps fallback names distinct from literal names; a raw
name that already begins with `_raw_` is itself encoded. Original dictionary keys are always retained.

The destination is in the namespace active when the read command is invoked. Raw namespace separators are encoded,
so a raw name cannot select a different namespace. Use `namespace eval ::signals [list $raw vectors -all -output vector]`
to choose a namespace explicitly (create it first). Newly created vectors have no mapped Tcl array variable:
use the returned command, for example `$signal index 1:20`, rather than Tcl array substitution `$v(out)(1:20)`.
New vector indexes start at zero, including partial reads. Closing a handle does not destroy its output vectors;
use `rbc::vector destroy` when done.

This naming replaces the earlier encoding of every parenthesis and underscore. Existing vectors are not renamed.
Use returned command names or dictionary values rather than reconstructing names using the old encoding.

### Collisions and numeric types

`-ifexists error` rejects an existing vector or command. `-ifexists replace` replaces the data of an existing RBC
vector of the same numeric type, retaining the vector identity, clients, index offset and any existing array
mapping. It never overwrites an unrelated Tcl command. RBC vector types are immutable: a real/complex mismatch
is an error even with `replace`; explicitly destroy the old vector first if changing type is intended.

Each variable's resolved raw storage determines its RBC type. Complex samples become complex RBC vectors without
losing their imaginary components. Real samples (including real axes in mixed layouts) remain real vectors.
Numeric data is decoded into buffers directly, without a full intermediate Tcl list. All selected data is read
and all known name/type collisions are checked before publication, so read and preflight errors leave existing
values unchanged. RBC notifications and command traces may execute application callbacks during publication;
updates are not a transaction against reentrant application code.

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

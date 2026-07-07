# tclsimrawreader - read SPICE .raw file with Tcl

`tclsimrawreader` is a Tcl C extension that allows to read SPICE3f5 raw-Binary and raw-ASCII files.
It doesn't read the whole file data into memory but read certain vectors lazily by request.

It targets Tcl 9.0 and works on Linux/macOS/Windows.

## What it gives you (at a glance)

- Reading raw files produced by Ngspice, Xyce and SPICE OPUS
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

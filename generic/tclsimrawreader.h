#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcl.h>

typedef enum { ENC_KIND_UTF8, ENC_KIND_UTF16LE } EncKind;

typedef enum { DATA_BINARY, DATA_VALUES } DataKind;

typedef enum RawHeaderStatus {
    RAW_HEADER_ERROR = -1, /* Malformed/incomplete header or read/decode error */
    RAW_HEADER_EOF = 0,    /* Clean EOF before another header starts */
    RAW_HEADER_OK = 1      /* Complete header parsed; channel is at data block */
} RawHeaderStatus;

typedef enum {
    RAW_FLAG_REAL = 1u << 0,
    RAW_FLAG_DOUBLE = 1u << 1,
    RAW_FLAG_COMPLEX = 1u << 2,
    RAW_FLAG_STEPPED = 1u << 3,
    RAW_FLAG_FASTACCESS = 1u << 4
} RawFlag;

typedef enum RawValueStorage {
    RAW_VALUE_REAL32,    /* float, 4 bytes */
    RAW_VALUE_REAL64,    /* double, 8 bytes */
    RAW_VALUE_COMPLEX128 /* double real + double imag, 16 bytes */
} RawValueStorage;

typedef enum RawVectorResultMode { RAW_VECTOR_RESULT_LIST, RAW_VECTOR_RESULT_DICT } RawVectorResultMode;

typedef struct RawOutputOptions {
    int vectors; /* Zero returns lists; nonzero publishes RBC vectors. */
    int replace; /* Zero rejects collisions; nonzero replaces same-type RBC data. */
} RawOutputOptions;

typedef struct RawNumericColumn {
    double *values; /* Owned doubles; complex samples use interleaved real/imaginary components. */
    Tcl_Size count;
    Tcl_Size used;
    int complex;
} RawNumericColumn;

int RawRbcInit(Tcl_Interp *interp);
int RawRbcPublish(Tcl_Interp *interp, Tcl_Size numVars, Tcl_Obj **names, RawNumericColumn *columns, int replace,
                  int dictionary, Tcl_Obj **resultPtr);
Tcl_Obj *RawRbcName(const char *ns, const char *rawName);
int RawRbcCheck(Tcl_Interp *interp, Tcl_Obj *name, int complex, int replace);

typedef enum RawDialect { RAW_DIALECT_GENERIC, RAW_DIALECT_LTSPICE } RawDialect;

typedef enum RawAxisDirection {
    RAW_AXIS_DIRECTION_UNKNOWN,
    RAW_AXIS_DIRECTION_INCREASING,
    RAW_AXIS_DIRECTION_DECREASING
} RawAxisDirection;

typedef struct RawVariable {
    /*------------------------------------------------------------------------------------------------------------------
     * Variable identity from the raw-file Variables section
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size index; /* Numeric variable index as written in the raw header */
    char *name;     /* Owned variable name, for example "time", "v(out)", or "v(m1#body diode)" */
    char *type;     /* Owned variable type token, for example "voltage", "current", or "frequency" */

    /*------------------------------------------------------------------------------------------------------------------
     * Decoded storage/layout information
     *-----------------------------------------------------------------------------------------------------------------*/
    RawValueStorage storage; /* Resolved value representation for this variable */
    Tcl_Size valueBytes;     /* Number of bytes occupied by this variable in one binary point */
    Tcl_Size offsetBytes;    /* Byte offset of this variable within one binary point */
} RawVariable;

typedef struct RawHeader {
    /*------------------------------------------------------------------------------------------------------------------
     * Basic plot metadata from scalar header fields
     *-----------------------------------------------------------------------------------------------------------------*/
    char *title;    /* Owned Title: field */
    char *date;     /* Owned Date: field */
    char *plotname; /* Owned Plotname: field */

    /*------------------------------------------------------------------------------------------------------------------
     * Flags: field, both as parsed strings and as decoded bit mask
     *-----------------------------------------------------------------------------------------------------------------*/
    unsigned flagsMask; /* Decoded RAW_FLAG_* bit mask */
    Tcl_Size numFlags;  /* Number of parsed flag strings */
    char **flags;       /* Owned array of owned flag strings */

    /*------------------------------------------------------------------------------------------------------------------
     * Declared raw-data dimensions
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size numVariables; /* Number from No. Variables: */
    Tcl_Size numPoints;    /* Number from No. Points: */
    int haveNumVariables;  /* True after No. Variables: was parsed */
    int haveNumPoints;     /* True after No. Points: was parsed */

    /*------------------------------------------------------------------------------------------------------------------
     * Default storage inferred from Flags:
     *-----------------------------------------------------------------------------------------------------------------*/
    RawValueStorage defaultStorage; /* Default value representation for variables in this plot */
    Tcl_Size defaultValueBytes;     /* Default byte size per variable value in binary data */

    /*------------------------------------------------------------------------------------------------------------------
     * Per-point binary layout
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size pointStrideBytes; /* Total number of bytes per binary point */

    /*------------------------------------------------------------------------------------------------------------------
     * Variables section
     *-----------------------------------------------------------------------------------------------------------------*/
    RawVariable *variables; /* Owned array of parsed variables in physical data order */
} RawHeader;

typedef struct RawPlot {
    /*------------------------------------------------------------------------------------------------------------------
     * Parsed header and data-block kind
     *-----------------------------------------------------------------------------------------------------------------*/
    RawHeader header;  /* Parsed metadata and variable layout for this plot */
    DataKind dataKind; /* DATA_BINARY for Binary:, DATA_VALUES for ASCII Values: */

    /*------------------------------------------------------------------------------------------------------------------
     * Data-block file offsets
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_WideInt dataOffset; /* Offset immediately after Binary:/Values: marker line */
    Tcl_WideInt nextOffset; /* Offset immediately after this plot's data block */

    /*------------------------------------------------------------------------------------------------------------------
     * Data-block size
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size dataBytes; /* Binary byte count or scanned textual Values: block byte count */

    /*------------------------------------------------------------------------------------------------------------------
     * ASCII Values: random-access index
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_WideInt *pointOffsets; /* For Values: only; pointOffsets[i] is the offset of ASCII point i */
    Tcl_Size numPointOffsets;  /* Number of entries in pointOffsets */
} RawPlot;

typedef struct RawFile {
    /*------------------------------------------------------------------------------------------------------------------
     * Tcl command ownership
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Interp *interp; /* Owning Tcl interpreter */
    Tcl_Command token;  /* Per-file Tcl command token */

    /*------------------------------------------------------------------------------------------------------------------
     * Open channel and header/data text decoding
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Channel chan; /* Open raw-file channel, kept for lazy vector/dict reads */
    RawDialect dialect;
    Tcl_Obj *vectorNamespace; /* Explicit canonical destination, or NULL for the read caller. */
    RawOutputOptions output; /* Defaults copied for each vector/vectors call. */
    EncKind encKind;  /* Detected raw header/text encoding kind */
    Tcl_Encoding enc; /* Tcl encoding handle for decoded text, or NULL when not needed */

    /*------------------------------------------------------------------------------------------------------------------
     * Parsed plots
     *-----------------------------------------------------------------------------------------------------------------*/
    RawPlot *plots;        /* Owned dynamic array of parsed plots */
    Tcl_Size numPlots;     /* Number of valid entries in plots */
    Tcl_Size plotCapacity; /* Allocated capacity of plots array */
} RawFile;

extern DLLEXPORT int Tclsimrawreader_Init(Tcl_Interp *interp);

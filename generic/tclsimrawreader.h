#include <tcl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>


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
    RAW_FLAG_STEPPED = 1u << 3
} RawFlag;

typedef enum RawValueStorage {
    RAW_VALUE_REAL32,      /* float, 4 bytes */
    RAW_VALUE_REAL64,      /* double, 8 bytes */
    RAW_VALUE_COMPLEX128   /* double real + double imag, 16 bytes */
} RawValueStorage;

typedef struct RawVariable {
    /*------------------------------------------------------------------------------------------------------------------
     * Variable identity from the raw-file Variables section
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size index;              /* Numeric variable index as written in the raw header */
    char *name;                  /* Owned variable name, for example "time", "v(out)", or "v(m1#body diode)" */
    char *type;                  /* Owned variable type token, for example "voltage", "current", or "frequency" */

    /*------------------------------------------------------------------------------------------------------------------
     * Decoded storage/layout information
     *-----------------------------------------------------------------------------------------------------------------*/
    RawValueStorage storage;     /* Resolved value representation for this variable */
    Tcl_Size valueBytes;         /* Number of bytes occupied by this variable in one binary point */
    Tcl_Size offsetBytes;        /* Byte offset of this variable within one binary point */
} RawVariable;

typedef struct RawHeader {
    /*------------------------------------------------------------------------------------------------------------------
     * Basic plot metadata from scalar header fields
     *-----------------------------------------------------------------------------------------------------------------*/
    char *title;                 /* Owned Title: field */
    char *date;                  /* Owned Date: field */
    char *plotname;              /* Owned Plotname: field */

    /*------------------------------------------------------------------------------------------------------------------
     * Flags: field, both as parsed strings and as decoded bit mask
     *-----------------------------------------------------------------------------------------------------------------*/
    unsigned flagsMask;          /* Decoded RAW_FLAG_* bit mask */
    Tcl_Size numFlags;           /* Number of parsed flag strings */
    char **flags;                /* Owned array of owned flag strings */

    /*------------------------------------------------------------------------------------------------------------------
     * Declared raw-data dimensions
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size numVariables;       /* Number from No. Variables: */
    Tcl_Size numPoints;          /* Number from No. Points: */
    int haveNumVariables;        /* True after No. Variables: was parsed */
    int haveNumPoints;           /* True after No. Points: was parsed */

    /*------------------------------------------------------------------------------------------------------------------
     * Default storage inferred from Flags:
     *-----------------------------------------------------------------------------------------------------------------*/
    RawValueStorage defaultStorage; /* Default value representation for variables in this plot */
    Tcl_Size defaultValueBytes;     /* Default byte size per variable value in binary data */

    /*------------------------------------------------------------------------------------------------------------------
     * Per-point binary layout
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size pointStrideBytes;   /* Total number of bytes per binary point */

    /*------------------------------------------------------------------------------------------------------------------
     * Variables section
     *-----------------------------------------------------------------------------------------------------------------*/
    RawVariable *variables;      /* Owned array of parsed variables in physical data order */
} RawHeader;

typedef struct RawPlot {
    /*------------------------------------------------------------------------------------------------------------------
     * Parsed header and data-block kind
     *-----------------------------------------------------------------------------------------------------------------*/
    RawHeader header;            /* Parsed metadata and variable layout for this plot */
    DataKind dataKind;           /* DATA_BINARY for Binary:, DATA_VALUES for ASCII Values: */

    /*------------------------------------------------------------------------------------------------------------------
     * Data-block file offsets
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_WideInt dataOffset;      /* Offset immediately after Binary:/Values: marker line */
    Tcl_WideInt nextOffset;      /* Offset immediately after this plot's data block */

    /*------------------------------------------------------------------------------------------------------------------
     * Data-block size
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Size dataBytes;          /* Binary byte count or scanned textual Values: block byte count */

    /*------------------------------------------------------------------------------------------------------------------
     * ASCII Values: random-access index
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_WideInt *pointOffsets;   /* For Values: only; pointOffsets[i] is the offset of ASCII point i */
    Tcl_Size numPointOffsets;    /* Number of entries in pointOffsets */
} RawPlot;

typedef struct RawFile {
    /*------------------------------------------------------------------------------------------------------------------
     * Tcl command ownership
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Interp *interp;          /* Owning Tcl interpreter */
    Tcl_Command token;           /* Per-file Tcl command token */

    /*------------------------------------------------------------------------------------------------------------------
     * Open channel and header/data text decoding
     *-----------------------------------------------------------------------------------------------------------------*/
    Tcl_Channel chan;            /* Open raw-file channel, kept for lazy vector/dict reads */
    EncKind encKind;             /* Detected raw header/text encoding kind */
    Tcl_Encoding enc;            /* Tcl encoding handle for decoded text, or NULL when not needed */

    /*------------------------------------------------------------------------------------------------------------------
     * Parsed plots
     *-----------------------------------------------------------------------------------------------------------------*/
    RawPlot *plots;              /* Owned dynamic array of parsed plots */
    Tcl_Size numPlots;           /* Number of valid entries in plots */
    Tcl_Size plotCapacity;       /* Allocated capacity of plots array */
} RawFile;

//*** Header parsing functions
static void RawHeaderInit(RawHeader *h);
static void RawHeaderFree(RawHeader *h);
static char *StrDupLen(const char *s, Tcl_Size len);
static const char *SkipSpace(const char *s);
static Tcl_Size TrimmedLen(const char *s);
static int SetStringField(char **fieldPtr, const char *value);
static int StartsWith(const char *s, const char *prefix);
static int DetectEncoding(Tcl_Interp *interp, Tcl_Channel chan, EncKind *kindPtr, Tcl_Encoding *encPtr);
static int ReadRawHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_DString *rawLinePtr);
static int DecodeHeaderLine(Tcl_Interp *interp, Tcl_Encoding enc, Tcl_DString *rawLinePtr, Tcl_DString *utfLinePtr);
static int ReadDecodedHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                 Tcl_DString *utfLinePtr);
static void StripTrailingCR(Tcl_DString *dsPtr);
static int ParseSizeField(Tcl_Interp *interp, const char *s, Tcl_Size *outPtr);
static int AppendFlag(Tcl_Interp *interp, RawHeader *h, const char *start, Tcl_Size len);
static int ParseFlags(Tcl_Interp *interp, RawHeader *h, const char *value);
static int NextToken(const char **pPtr, const char **startPtr, Tcl_Size *lenPtr);
static int RawIsVariableTypeToken(const char *start, Tcl_Size len);
static const char *SkipToken(const char *p);
static int ParseVariableLine(Tcl_Interp *interp, const char *line, RawVariable *v);
static int ReadVariablesSection(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h);
static int ResolveDefaultStorage(Tcl_Interp *interp, RawHeader *h);
static int RawHeaderResolveVariableLayout(Tcl_Interp *interp, RawHeader *h);
static int ComputeBinaryByteCount(Tcl_Interp *interp, const RawHeader *h, Tcl_Size *nbytesPtr);
static RawHeaderStatus ReadHeader(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                      DataKind *dataKindPtr);
static Tcl_Obj *RawHeaderToDictObj(const RawHeader *h);

//*** Reading data block functions
static void RawPlotInit(RawPlot *p);
static void RawPlotFree(RawPlot *p);
static void RawPlotMove(RawPlot *dst, RawPlot *src);
static void RawHeaderMove(RawHeader *dst, RawHeader *src);
static int RawFileAppendPlotMove(Tcl_Interp *interp, RawFile *rf, RawPlot *plot);
static double ReadLEFloat32AsDouble(const unsigned char *p);
static double ReadLEFloat64(const unsigned char *p);
static int RawBinaryReadExactBytes(Tcl_Interp *interp, Tcl_Channel chan, unsigned char *buf, Tcl_Size nbytes);
static int RawAppendBinaryValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage,
                                 const unsigned char *p);
static int RawParseAsciiDoubleToken(Tcl_Interp *interp, const char *start, Tcl_Size len, double *valuePtr);
static int RawAppendAsciiValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage, const char *start,
                               Tcl_Size len);
static int RawAsciiReadOnePoint(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                                Tcl_Size selectedVarIndex, Tcl_Obj *selectedListObj, Tcl_Obj **vecObjs);
static int RawPlotScanAsciiValues(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawPlot *plot);
static int RawPlotAsciiVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex,
                                   Tcl_Size firstPoint, Tcl_Size count, Tcl_Obj **objPtr);
static int RawPlotAsciiDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                                 Tcl_Obj **objPtr);
static int RawPlotBinaryVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex,
                                    Tcl_Size firstPoint, Tcl_Size count, Tcl_Obj **objPtr);
static int RawPlotBinaryDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                            Tcl_Obj **objPtr);
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr);
static int RawPlotDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                            Tcl_Obj **objPtr);
static const char *RawDataKindName(DataKind kind);
static Tcl_Obj *RawPlotSummaryObj(const RawPlot *plot, Tcl_Size index);

//*** Tcl command registering/processing function
static void RawFileDeleteProc(void *clientData);
static int RawParsePlotIndex(Tcl_Interp *interp, RawFile *rf, Tcl_Obj *obj, Tcl_Size *plotIndexPtr);
static int RawParseRange(Tcl_Interp *interp, RawPlot *plot, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size firstOpt,
                         Tcl_Size *fromPtr, Tcl_Size *countPtr);
static int RawSelectPlotFromArgs(Tcl_Interp *interp, RawFile *rf, Tcl_Size objc, Tcl_Obj *const objv[],
                                 Tcl_Size firstOpt, RawPlot **plotPtr, Tcl_Size *nextArgPtr);
static int RawPlotFindVariable(Tcl_Interp *interp, RawPlot *plot, const char *name, Tcl_Size *indexPtr);
static int RawFileObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int RawOpenCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
extern DLLEXPORT int Tclsimrawreader_Init(Tcl_Interp *interp);

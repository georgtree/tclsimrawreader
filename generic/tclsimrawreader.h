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

#define RAW_HEADER_ERROR (-1)
#define RAW_HEADER_EOF 0
#define RAW_HEADER_OK 1

typedef enum {
    RAW_FLAG_REAL = 1u << 0,
    RAW_FLAG_DOUBLE = 1u << 1,
    RAW_FLAG_COMPLEX = 1u << 2,
    RAW_FLAG_STEPPED = 1u << 3
    /* add others if needed */
} RawFlag;

typedef enum RawValueStorage {
    RAW_VALUE_REAL32,      /* float, 4 bytes */
    RAW_VALUE_REAL64,      /* double, 8 bytes */
    RAW_VALUE_COMPLEX128   /* double real + double imag, 16 bytes */
} RawValueStorage;

typedef struct RawVariable {
    Tcl_Size index;
    char *name;
    char *type;
    RawValueStorage storage;
    Tcl_Size valueBytes;
    Tcl_Size offsetBytes;
} RawVariable;

typedef struct RawHeader {
    char *title;
    char *date;
    char *plotname;
    unsigned flagsMask;
    Tcl_Size numFlags;
    char **flags;
    Tcl_Size numVariables;
    Tcl_Size numPoints;
    int haveNumVariables;
    int haveNumPoints;
    /*
       Default storage inferred from Flags:.
       Individual variables can override it.
    */
    RawValueStorage defaultStorage;
    Tcl_Size defaultValueBytes;
    /*
       Total number of bytes per point.
       Usually sum of variables[i].valueBytes.
    */
    Tcl_Size pointStrideBytes;
    RawVariable *variables;
} RawHeader;

typedef struct RawPlot {
    RawHeader header;
    DataKind dataKind;
    /*
     * Offset immediately after Binary: marker line.
     */
    Tcl_WideInt dataOffset;
    /*
     * Offset immediately after the data block.
     */
    Tcl_WideInt nextOffset;
    /*
     * Binary block size in bytes.
     */
    Tcl_Size dataBytes;
} RawPlot;

typedef struct RawFile {
    Tcl_Interp *interp;
    Tcl_Command token;
    Tcl_Channel chan;
    EncKind encKind;
    Tcl_Encoding enc;
    RawPlot *plots;
    Tcl_Size numPlots;
    Tcl_Size plotCapacity;
} RawFile;

static void RawHeaderInit(RawHeader *h);
static char *TclStrDupLen(const char *s, Tcl_Size len);
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
static int ParseVariableLine(Tcl_Interp *interp, const char *line, RawVariable *v);
static int ReadVariablesSection(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h);
static int RawHeaderResolveVariableLayout(Tcl_Interp *interp, RawHeader *h);
static int ComputeBinaryByteCount(Tcl_Interp *interp, const RawHeader *h, Tcl_Size *nbytesPtr);
static int ReadHeader(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                      DataKind *dataKindPtr);
static void RawHeaderPrintStdout(const RawHeader *h);
static int ResolveDefaultStorage(Tcl_Interp *interp, RawHeader *h);

static void RawHeaderFree(RawHeader *h);
static int RawOpenCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int RawFileObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int RawSelectPlotFromArgs(Tcl_Interp *interp, RawFile *rf, Tcl_Size objc, Tcl_Obj *const objv[],
                                 Tcl_Size firstOpt, RawPlot **plotPtr, Tcl_Size *nextArgPtr);
static int RawParsePlotIndex(Tcl_Interp *interp, RawFile *rf, Tcl_Obj *obj, Tcl_Size *plotIndexPtr);

static void RawFileDeleteProc(void *clientData);
static Tcl_Obj *RawHeaderToDictObj(const RawHeader *h);
static Tcl_Obj *RawPlotSummaryObj(const RawPlot *plot, Tcl_Size index);
static const char *RawDataKindName(DataKind kind);
static int RawPlotDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                            Tcl_Obj **objPtr);
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr);
static int RawPlotFindVariable(Tcl_Interp *interp, RawPlot *plot, const char *name, Tcl_Size *indexPtr);

static int RawAppendDecodedValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage,
                                 const unsigned char *p);
static int RawParseRange(Tcl_Interp *interp, RawPlot *plot, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size firstOpt,
                         Tcl_Size *fromPtr, Tcl_Size *countPtr);
static int RawFileAppendPlotMove(Tcl_Interp *interp, RawFile *rf, RawPlot *plot);

static int ReadExact(Tcl_Interp *interp, Tcl_Channel chan, unsigned char *buf, Tcl_Size nbytes);
static void RawHeaderMove(RawHeader *dst, RawHeader *src);
static void RawPlotMove(RawPlot *dst, RawPlot *src);
static void RawPlotFree(RawPlot *p);
static void RawPlotInit(RawPlot *p);
static double ReadLEFloat64(const unsigned char *p);
static double ReadLEFloat32AsDouble(const unsigned char *p);

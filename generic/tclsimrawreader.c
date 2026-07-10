#include "tclsimrawreader.h"

//** forward declarations
static void RawHeaderInit(RawHeader *h);
static void RawHeaderFree(RawHeader *h);
static int RawHeaderClone(Tcl_Interp *interp, RawHeader *dst, const RawHeader *src);
static void RawHeaderMove(RawHeader *dst, RawHeader *src);
static void RawPlotInit(RawPlot *p);
static void RawPlotFree(RawPlot *p);
static void RawPlotMove(RawPlot *dst, RawPlot *src);
static int RawFileAppendPlotMove(Tcl_Interp *interp, RawFile *rf, RawPlot *plot);
static void RawFileDeleteProc(void *clientData);
static char *StrDupLen(const char *s, Tcl_Size len);
static const char *SkipSpace(const char *s);
static Tcl_Size TrimmedLen(const char *s);
static int SetStringField(char **fieldPtr, const char *value);
static int StartsWith(const char *s, const char *prefix);
static int NextToken(const char **pPtr, const char **startPtr, Tcl_Size *lenPtr);
static const char *SkipToken(const char *p);
static int ParseSizeField(Tcl_Interp *interp, const char *s, Tcl_Size *outPtr);
static int RawMulSize(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, const char *message, Tcl_Size *outPtr);
static int DetectEncoding(Tcl_Interp *interp, Tcl_Channel chan, EncKind *kindPtr, Tcl_Encoding *encPtr);
static int ReadRawHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_DString *rawLinePtr);
static int DecodeHeaderLine(Tcl_Interp *interp, Tcl_Encoding enc, Tcl_DString *rawLinePtr, Tcl_DString *utfLinePtr);
static int ReadDecodedHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                 Tcl_DString *utfLinePtr);
static void StripTrailingCR(Tcl_DString *dsPtr);
static int AppendFlag(Tcl_Interp *interp, RawHeader *h, const char *start, Tcl_Size len);
static int ParseFlags(Tcl_Interp *interp, RawHeader *h, const char *value);
static int RawIsVariableTypeToken(const char *start, Tcl_Size len);
static int ParseVariableLine(Tcl_Interp *interp, const char *line, RawVariable *v);
static int ReadVariablesSection(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h);
static RawHeaderStatus ReadHeader(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                                  DataKind *dataKindPtr);
static int ResolveDefaultStorage(Tcl_Interp *interp, RawHeader *h);
static int RawSetVariableLayout(Tcl_Interp *interp, RawHeader *h, Tcl_Size index, RawValueStorage storage,
                                Tcl_Size valueBytes, Tcl_Size *offsetPtr);
static int RawHeaderResolveVariableLayout(Tcl_Interp *interp, RawHeader *h, RawDialect dialect, int ltspiceAllDouble);
static int ComputeBinaryByteCount(Tcl_Interp *interp, const RawHeader *h, Tcl_Size *nbytesPtr);
static Tcl_Obj *RawHeaderToDictObj(const RawHeader *h);
static const char *RawDataKindName(DataKind kind);
static Tcl_Obj *RawPlotSummaryObj(const RawPlot *plot, Tcl_Size index);
static int RawBuildVectorResult(Tcl_Interp *interp, RawHeader *h, Tcl_Size numVars, Tcl_Size *varIndexes,
                                Tcl_Obj **vecObjs, RawVectorResultMode resultMode, Tcl_Obj **objPtr);
static void RawFreeVectorObjects(Tcl_Obj **vecObjs, Tcl_Size numVars);
static double ReadLEFloat32AsDouble(const unsigned char *p);
static double ReadLEFloat64(const unsigned char *p);
static int RawBinaryReadExactBytes(Tcl_Interp *interp, Tcl_Channel chan, unsigned char *buf, Tcl_Size nbytes);
static int RawAppendBinaryValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage, const unsigned char *p);
static int RawParseAsciiDoubleToken(Tcl_Interp *interp, const char *start, Tcl_Size len, double *valuePtr);
static int RawAppendAsciiValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage, const char *start,
                               Tcl_Size len);
static int RawAsciiReadOnePoint(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                                Tcl_Size selectedVarIndex, Tcl_Obj *selectedListObj, Tcl_Obj **vecObjs);
static int RawPlotScanAsciiValues(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawPlot *plot);
static int RawPlotFindVariable(Tcl_Interp *interp, RawPlot *plot, const char *name, Tcl_Size *indexPtr);
static int RawPlotResolveVariableList(Tcl_Interp *interp, RawPlot *plot, Tcl_Obj *namesObj, Tcl_Size *numVarsPtr,
                                      Tcl_Size **varIndexesPtr);
static int RawPlotResolveAllVariables(Tcl_Interp *interp, RawPlot *plot, Tcl_Size *numVarsPtr,
                                      Tcl_Size **varIndexesPtr);
static int RawPlotBinaryReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                         Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                         RawVectorResultMode resultMode, Tcl_Obj **objPtr);
static int RawPlotAsciiReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                        Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                        RawVectorResultMode resultMode, Tcl_Obj **objPtr);
static int RawPlotReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                   Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                   RawVectorResultMode resultMode, Tcl_Obj **objPtr);
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr);
static int RawGetChannelSize(Tcl_Interp *interp, Tcl_Channel chan, Tcl_WideInt *sizePtr);
static int RawLtspiceCandidateStrides(Tcl_Interp *interp, RawHeader *h, Tcl_Size *mixedStridePtr,
                                      Tcl_Size *doubleStridePtr);
static int RawLtspiceDetectAllDouble(Tcl_Interp *interp, RawHeader *h, Tcl_Size physicalDataBytes, int *allDoublePtr);
static int RawLtspicePrepareBinaryPlot(Tcl_Interp *interp, Tcl_Channel chan, RawPlot *plot,
                                       Tcl_Size *declaredPointsPtr);
static double RawDecodeBinaryAxisValue(const unsigned char *pointPtr, RawHeader *h);
static int RawAppendStepBoundary(Tcl_Interp *interp, Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *capacityPtr,
                                 Tcl_Size *numStepsPtr, Tcl_Size startPoint);
static int RawLtspiceFindAxisResetSteps(Tcl_Interp *interp, Tcl_Channel chan, RawPlot *plot, int transientMode,
                                        Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *numStepsPtr);
static int RawAxisStartsNewStep(double previous, double current, RawAxisDirection *directionPtr);
static int RawLtspiceBuildFixedSteps(Tcl_Interp *interp, Tcl_Size totalPoints, Tcl_Size pointsPerStep,
                                     Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *numStepsPtr);
static int RawLtspiceIsTransientPlot(const RawHeader *h);
static int RawLtspiceAppendSegmentPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot, Tcl_Size *starts,
                                        Tcl_Size *counts, Tcl_Size numSteps);
static int RawLtspiceAppendSplitBinaryPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot,
                                            Tcl_Size declaredPoints);
static int RawAsciiFindNextContentLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                       Tcl_WideInt *offsetPtr, int *headerPtr, int *eofPtr);
static int RawAsciiAxisObjToDouble(Tcl_Interp *interp, Tcl_Obj *valueObj, double *axisPtr);
static int RawAsciiReadAxisAtCurrentPoint(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                          RawHeader *h, double *axisPtr);
static int RawAppendAsciiScannedPoint(Tcl_Interp *interp, Tcl_WideInt **offsetsPtr, double **axisValuesPtr,
                                      Tcl_Size *capacityPtr, Tcl_Size *numPointsPtr, Tcl_WideInt offset,
                                      double axisValue);
static int RawLtspiceScanAllAsciiValues(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                        RawPlot *plot, double **axisValuesPtr);
static int RawLtspiceBuildAxisResetStepsFromValues(Tcl_Interp *interp, const double *axisValues, Tcl_Size totalPoints,
                                                   int transientMode, Tcl_Size **startsPtr, Tcl_Size **countsPtr,
                                                   Tcl_Size *numStepsPtr);
static int RawLtspiceAppendAsciiSegmentPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot,
                                             Tcl_WideInt *allPointOffsets, Tcl_Size totalPoints, Tcl_Size *starts,
                                             Tcl_Size *counts, Tcl_Size numSteps);
static int RawLtspiceAppendSplitAsciiPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot, Tcl_Size declaredPoints);
static int RawParseOpenArgs(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], RawDialect *dialectPtr,
                            Tcl_Obj **fileNameObjPtr);
static int RawParsePlotIndex(Tcl_Interp *interp, RawFile *rf, Tcl_Obj *obj, Tcl_Size *plotIndexPtr);
static int RawParseRange(Tcl_Interp *interp, RawPlot *plot, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size firstOpt,
                         Tcl_Size *fromPtr, Tcl_Size *countPtr);
static int RawSelectPlotFromArgs(Tcl_Interp *interp, RawFile *rf, Tcl_Size objc, Tcl_Obj *const objv[],
                                 Tcl_Size firstOpt, RawPlot **plotPtr, Tcl_Size *nextArgPtr);
static int RawFileObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int RawOpenCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

//** Header/object lifetime helpers
//***  RawHeaderInit function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderInit --
 *
 *      Initializes a RawHeader structure to an empty state.
 *
 * Parameters:
 *      RawHeader *h - Header structure to initialize.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Clears h with memset().
 *
 * Notes:
 *      Call RawHeaderFree() first if h already owns allocated memory.
 *      After initialization, h can be safely passed to RawHeaderFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawHeaderInit(RawHeader *h) { memset(h, 0, sizeof *h); }

//***  RawHeaderFree function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderFree --
 *
 *      Releases all memory owned by a RawHeader structure and resets the structure to an all-zero state.
 *
 *      This function is intended to be called after a header has been parsed, after an error during parsing, or before
 *      reusing an existing RawHeader object.
 *
 * Parameters:
 *      RawHeader *h   - Pointer to the RawHeader structure to clean up.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Frees dynamically allocated string fields:
 *            h->title
 *            h->date
 *            h->plotname
 *      - Frees each stored flag string in h->flags[], then frees the h->flags array itself.
 *      - Frees each variable name and type string in h->variables[], then frees the h->variables array itself.
 *      - Resets the complete RawHeader structure with memset(), so all pointers, counters, flags, and layout fields
 *        become zero/NULL.
 *
 * Notes:
 *      - The function assumes that h has either been initialized with RawHeaderInit() or otherwise contains a valid
 *        partially/fully parsed RawHeader.
 *      - The function assumes the structure invariants are valid:
 *            * if h->numFlags > 0, h->flags points to an array of that size
 *            * if h->numVariables > 0, h->variables points to an array of that size
 *      - After this function returns, the RawHeader is empty and may be safely reused or passed to RawHeaderFree()
 *        again.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawHeaderFree(RawHeader *h) {
    if (h->title) {
        Tcl_Free(h->title);
    }
    if (h->date) {
        Tcl_Free(h->date);
    }
    if (h->plotname) {
        Tcl_Free(h->plotname);
    }
    for (Tcl_Size i = 0; i < h->numFlags; i++) {
        Tcl_Free(h->flags[i]);
    }
    if (h->flags) {
        Tcl_Free((char *)h->flags);
    }
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        Tcl_Free(h->variables[i].name);
        Tcl_Free(h->variables[i].type);
    }
    if (h->variables) {
        Tcl_Free((char *)h->variables);
    }
    memset(h, 0, sizeof *h);
}

//***  RawHeaderClone function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderClone --
 *
 *      Creates a deep copy of a RawHeader.
 *
 * Parameters:
 *      Tcl_Interp *interp     - Interpreter used for error reporting.
 *      RawHeader *dst         - Destination header to initialize and fill.
 *      const RawHeader *src   - Source header to copy.
 *
 * Results:
 *      Returns TCL_OK if the header is cloned successfully.
 *      Returns TCL_ERROR if an array allocation would overflow.
 *
 * Side Effects:
 *      Initializes dst.
 *      Allocates owned copies of string fields, flags, and variables.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      On failure, any partially cloned storage in dst is released.
 *      The cloned header must be released with RawHeaderFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawHeaderClone(Tcl_Interp *interp, RawHeader *dst, const RawHeader *src) {
    RawHeaderInit(dst);
    dst->flagsMask = src->flagsMask;
    dst->numVariables = src->numVariables;
    dst->numPoints = src->numPoints;
    dst->haveNumVariables = src->haveNumVariables;
    dst->haveNumPoints = src->haveNumPoints;
    dst->defaultStorage = src->defaultStorage;
    dst->defaultValueBytes = src->defaultValueBytes;
    dst->pointStrideBytes = src->pointStrideBytes;
    if (src->title) {
        dst->title = StrDupLen(src->title, (Tcl_Size)strlen(src->title));
    }
    if (src->date) {
        dst->date = StrDupLen(src->date, (Tcl_Size)strlen(src->date));
    }
    if (src->plotname) {
        dst->plotname = StrDupLen(src->plotname, (Tcl_Size)strlen(src->plotname));
    }
    dst->numFlags = src->numFlags;
    if (src->numFlags > 0) {
        if ((size_t)src->numFlags > SIZE_MAX / sizeof(char *)) {
            RawHeaderFree(dst);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw flag array clone overflow", -1));
            return TCL_ERROR;
        }
        dst->flags = (char **)Tcl_Alloc(sizeof(char *) * (size_t)src->numFlags);
        memset(dst->flags, 0, sizeof(char *) * (size_t)src->numFlags);
        for (Tcl_Size i = 0; i < src->numFlags; i++) {
            if (src->flags[i]) {
                dst->flags[i] = StrDupLen(src->flags[i], (Tcl_Size)strlen(src->flags[i]));
            }
        }
    }
    if (src->numVariables > 0) {
        if ((size_t)src->numVariables > SIZE_MAX / sizeof(RawVariable)) {
            RawHeaderFree(dst);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw variable array clone overflow", -1));
            return TCL_ERROR;
        }
        dst->variables = (RawVariable *)Tcl_Alloc(sizeof(RawVariable) * (size_t)src->numVariables);
        memset(dst->variables, 0, sizeof(RawVariable) * (size_t)src->numVariables);
        for (Tcl_Size i = 0; i < src->numVariables; i++) {
            const RawVariable *sv = &src->variables[i];
            RawVariable *dv = &dst->variables[i];
            dv->index = sv->index;
            dv->storage = sv->storage;
            dv->valueBytes = sv->valueBytes;
            dv->offsetBytes = sv->offsetBytes;
            if (sv->name) {
                dv->name = StrDupLen(sv->name, (Tcl_Size)strlen(sv->name));
            }
            if (sv->type) {
                dv->type = StrDupLen(sv->type, (Tcl_Size)strlen(sv->type));
            }
        }
    }
    return TCL_OK;
}

//***  RawHeaderMove function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderMove --
 *
 *      Moves ownership of a RawHeader from src to dst.
 *
 * Parameters:
 *      RawHeader *dst - Destination header structure.
 *      RawHeader *src - Source header structure, reset after the move.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Copies src to dst with structure assignment.
 *      Reinitializes src so it no longer owns the moved resources.
 *
 * Notes:
 *      This is a move operation, not a deep copy.
 *      dst must not already own allocated resources.
 *      After the move, dst owns the resources previously owned by src.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawHeaderMove(RawHeader *dst, RawHeader *src) {
    *dst = *src;
    RawHeaderInit(src);
}

//***  RawPlotInit function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotInit --
 *
 *      Initializes a RawPlot structure to an empty state.
 *
 * Parameters:
 *      RawPlot *p - Plot structure to initialize.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Clears p with memset().
 *      Initializes the embedded RawHeader.
 *
 * Notes:
 *      Call RawPlotFree() first if p already owns allocated memory.
 *      After initialization, p can be safely passed to RawPlotFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawPlotInit(RawPlot *p) {
    memset(p, 0, sizeof *p);
    RawHeaderInit(&p->header);
}

//***  RawPlotFree function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotFree --
 *
 *      Releases all owned storage associated with a RawPlot and clears it.
 *
 * Parameters:
 *      RawPlot *p - Plot structure to release.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Frees owned header storage through RawHeaderFree().
 *      Frees p->pointOffsets if present.
 *      Clears p with memset().
 *
 * Notes:
 *      Safe to call on a zero-initialized RawPlot.
 *      p itself is not freed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawPlotFree(RawPlot *p) {
    RawHeaderFree(&p->header);
    if (p->pointOffsets) {
        Tcl_Free((char *)p->pointOffsets);
    }
    memset(p, 0, sizeof *p);
}

//***  RawPlotMove function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotMove --
 *
 *      Moves ownership of a RawPlot from src to dst.
 *
 * Parameters:
 *      RawPlot *dst - Destination plot structure.
 *      RawPlot *src - Source plot structure, reset after the move.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Copies src to dst with structure assignment.
 *      Reinitializes src so it no longer owns the moved resources.
 *
 * Notes:
 *      This is a move operation, not a deep copy.
 *      dst must not already own allocated resources.
 *      After the move, dst owns the resources previously owned by src.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawPlotMove(RawPlot *dst, RawPlot *src) {
    *dst = *src;
    RawPlotInit(src);
}

//***  RawFileAppendPlotMove function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileAppendPlotMove --
 *
 *      Appends plot to rf->plots, transferring ownership to the RawFile.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawFile *rf        - Raw file handle whose plot array is extended.
 *      RawPlot *plot      - Temporary plot to move into rf.
 *
 * Results:
 *      Returns TCL_OK if the plot is appended successfully.
 *      Returns TCL_ERROR if the plot array cannot be grown.
 *
 * Side Effects:
 *      May grow rf->plots with Tcl_Realloc().
 *      Moves plot into rf->plots[rf->numPlots] and increments rf->numPlots.
 *      Sets the interpreter result on allocation failure.
 *
 * Notes:
 *      This is a move operation, not a deep copy.
 *      On success, rf owns the resources previously owned by plot.
 *      On error, ownership of plot is unchanged.
 *      Assumes rf has been initialized and rf->plots is NULL or Tcl-allocated.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawFileAppendPlotMove(Tcl_Interp *interp, RawFile *rf, RawPlot *plot) {
    if (rf->numPlots == rf->plotCapacity) {
        Tcl_Size newCapacity = rf->plotCapacity ? rf->plotCapacity * 2 : 4;
        if ((size_t)newCapacity > SIZE_MAX / sizeof(RawPlot)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw plot array size overflow", -1));
            return TCL_ERROR;
        }
        RawPlot *newPlots = (RawPlot *)Tcl_Realloc((char *)rf->plots, sizeof(RawPlot) * (size_t)newCapacity);
        if (newPlots == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to allocate raw plot array", -1));
            return TCL_ERROR;
        }
        rf->plots = newPlots;
        rf->plotCapacity = newCapacity;
    }
    RawPlotMove(&rf->plots[rf->numPlots], plot);
    rf->numPlots++;
    return TCL_OK;
}

//***  RawFileDeleteProc function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileDeleteProc --
 *
 *      Releases all resources owned by a RawFile handle.
 *
 * Parameters:
 *      void *clientData - RawFile pointer supplied when the Tcl handle command was created.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Closes the file channel if open.
 *      Releases the encoding handle if present.
 *      Frees all plots, the plot array, and the RawFile structure itself.
 *
 * Notes:
 *      Registered as the Tcl command delete callback for the raw-file handle.
 *      Tcl_Close() is called with a NULL interpreter because this is cleanup code.
 *      After this function returns, the RawFile pointer is invalid.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawFileDeleteProc(void *clientData) {
    RawFile *rf = (RawFile *)clientData;
    if (rf->chan) {
        Tcl_Close(NULL, rf->chan);
        rf->chan = NULL;
    }
    if (rf->enc) {
        Tcl_FreeEncoding(rf->enc);
        rf->enc = NULL;
    }
    for (Tcl_Size i = 0; i < rf->numPlots; i++) {
        RawPlotFree(&rf->plots[i]);
    }
    if (rf->plots) {
        Tcl_Free((char *)rf->plots);
    }
    Tcl_Free((char *)rf);
}

//** Generic string/token/numeric helpers
//***  StrDupLen function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * StrDupLen --
 *
 *      Allocates a Tcl-managed NUL-terminated copy of exactly len bytes from s.
 *      The source does not need to be NUL-terminated.
 *
 * Parameters:
 *      const char *s - Pointer to the first byte to copy.
 *      Tcl_Size len  - Number of bytes to copy.
 *
 * Results:
 *      Returns a newly allocated NUL-terminated string.
 *
 * Side Effects:
 *      Allocates len + 1 bytes with Tcl_Alloc().
 *
 * Notes:
 *      The caller owns the returned pointer and must release it with Tcl_Free().
 *      len is a byte count, not a Tcl character count.
 *      Assumes len is non-negative and s points to at least len readable bytes.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static char *StrDupLen(const char *s, Tcl_Size len) {
    char *copy = Tcl_Alloc(len + 1);
    memcpy(copy, s, len);
    copy[len] = '\0';
    return copy;
}

//***  SkipSpace function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * SkipSpace --
 *
 *      Advances s past leading whitespace characters.
 *
 * Parameters:
 *      const char *s - NUL-terminated string pointer to scan.
 *
 * Results:
 *      Returns a pointer into the original string at the first non-whitespace character, or at the terminating NUL byte.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      Whitespace detection uses isspace(); the character is cast to unsigned char to avoid undefined behaviour.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static const char *SkipSpace(const char *s) {
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

//***  TrimmedLen function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * TrimmedLen --
 *
 *      Computes the byte length of s excluding trailing whitespace.
 *
 * Parameters:
 *      const char *s - NUL-terminated string to examine.
 *
 * Results:
 *      Returns the number of bytes before trailing whitespace. Returns 0 if s contains only whitespace.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      The returned length is a byte count, not a character count.
 *      Whitespace detection uses isspace(); bytes are cast to unsigned char to avoid undefined behaviour.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static Tcl_Size TrimmedLen(const char *s) {
    Tcl_Size len = (Tcl_Size)strlen(s);

    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        len--;
    }
    return len;
}

//***  SetStringField function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * SetStringField --
 *
 *      Stores a trimmed, owned copy of value in *fieldPtr.
 *
 * Parameters:
 *      char **fieldPtr   - Address of the string field to update. Existing value is freed before replacement.
 *      const char *value - NUL-terminated string to copy.
 *
 * Results:
 *      Returns TCL_OK.
 *
 * Side Effects:
 *      Frees the previous *fieldPtr value, if any.
 *      Allocates a new Tcl-managed string and updates *fieldPtr.
 *
 * Notes:
 *      The stored string must eventually be released with Tcl_Free().  Leading whitespace is skipped; trailing
 *      whitespace is trimmed.  The copied length is a byte count, not a character count.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int SetStringField(char **fieldPtr, const char *value) {
    value = SkipSpace(value);
    Tcl_Size len = TrimmedLen(value);
    if (*fieldPtr) {
        Tcl_Free(*fieldPtr);
    }
    *fieldPtr = StrDupLen(value, len);
    return TCL_OK;
}

//***  StartsWith function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * StartsWith --
 *
 *      Tests whether s begins with prefix.
 *
 * Parameters:
 *      const char *s      - NUL-terminated string to test.
 *      const char *prefix - NUL-terminated prefix to match.
 *
 * Results:
 *      Returns non-zero if s begins with prefix; zero otherwise.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      Assumes both s and prefix are valid NUL-terminated strings.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int StartsWith(const char *s, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(s, prefix, n) == 0;
}

//***  NextToken function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * NextToken --
 *
 *      Extracts the next whitespace-delimited token as a borrowed pointer/length pair.
 *
 * Parameters:
 *      const char **pPtr     - Address of the current scan position; advanced past the token on success.
 *      const char **startPtr - Output pointer to the first byte of the token.
 *      Tcl_Size *lenPtr      - Output token byte length.
 *
 * Results:
 *      Returns 1 if a token is found; 0 if no token remains.
 *
 * Side Effects:
 *      Advances *pPtr and writes *startPtr and *lenPtr on success.
 *
 * Notes:
 *      The returned token is not NUL-terminated and points into the original string.
 *      Tokens are separated with isspace() whitespace.
 *      *pPtr is left after the token, not after following whitespace.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int NextToken(const char **pPtr, const char **startPtr, Tcl_Size *lenPtr) {
    const char *p = *pPtr;
    p = SkipSpace(p);
    if (*p == '\0') {
        return 0;
    }
    *startPtr = p;
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    *lenPtr = (Tcl_Size)(p - *startPtr);
    *pPtr = p;
    return 1;
}

//***  SkipToken function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * SkipToken --
 *
 *      Advances p past one whitespace-delimited token.
 *
 * Parameters:
 *      const char *p - NUL-terminated string pointer at or before the token to skip.
 *
 * Results:
 *      Returns a pointer into the original string after the skipped token, or at the terminating NUL byte.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      Leading whitespace is skipped before scanning the token.
 *      The returned pointer is borrowed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static const char *SkipToken(const char *p) {
    p = SkipSpace(p);
    while (*p && !isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

//***  ParseSizeField function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ParseSizeField --
 *
 *      Parses a non-negative integer size field and stores it as a Tcl_Size.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for numeric conversion and error reporting.
 *      const char *s      - NUL-terminated value string to parse.
 *      Tcl_Size *outPtr   - Output location for the parsed size.
 *
 * Results:
 *      Returns TCL_OK if the value is parsed successfully and is non-negative.
 *      Returns TCL_ERROR on parse failure or negative input.
 *
 * Side Effects:
 *      Creates a temporary Tcl_Obj for Tcl_GetWideIntFromObj().
 *      Writes the parsed value to *outPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Leading whitespace is skipped before parsing.
 *      The input string is borrowed and is not modified.
 *      No upper-bound check is currently made before casting from Tcl_WideInt to Tcl_Size.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ParseSizeField(Tcl_Interp *interp, const char *s, Tcl_Size *outPtr) {
    Tcl_Obj *obj;
    Tcl_WideInt wide;

    s = SkipSpace(s);
    obj = Tcl_NewStringObj(s, -1);
    Tcl_IncrRefCount(obj);
    if (Tcl_GetWideIntFromObj(interp, obj, &wide) != TCL_OK) {
        Tcl_DecrRefCount(obj);
        return TCL_ERROR;
    }
    Tcl_DecrRefCount(obj);
    if (wide < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("negative size field in raw header", -1));
        return TCL_ERROR;
    }
    *outPtr = (Tcl_Size)wide;
    return TCL_OK;
}

//***  RawMulSize function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawMulSize --
 *
 *      Multiplies two Tcl_Size values with overflow checking.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      Tcl_Size a          - First factor.
 *      Tcl_Size b          - Second factor.
 *      const char *message - Error message to set on invalid input or overflow.
 *      Tcl_Size *outPtr    - Output location for the product.
 *
 * Results:
 *      Returns TCL_OK if the product is computed successfully.
 *      Returns TCL_ERROR if either factor is negative or the multiplication would overflow.
 *
 * Side Effects:
 *      Stores the product in *outPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The input values are treated as non-negative sizes.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawMulSize(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, const char *message, Tcl_Size *outPtr) {
    if (a < 0 || b < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(message, -1));
        return TCL_ERROR;
    }
    if (a != 0 && b > TCL_SIZE_MAX / a) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(message, -1));
        return TCL_ERROR;
    }
    *outPtr = a * b;
    return TCL_OK;
}

//** Encoding and decoded line reading
//***  DetectEncoding function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * DetectEncoding --
 *
 *      Detects whether the raw-file header text is UTF-8 or UTF-16LE by probing the first six bytes.
 *      On success, pushes the probe bytes back so normal reading starts from the beginning.
 *
 * Parameters:
 *      Tcl_Interp *interp   - Interpreter used for error reporting and Tcl_GetEncoding().
 *      Tcl_Channel chan     - Binary channel positioned at the start of the raw file.
 *      EncKind *kindPtr     - Output location for the detected encoding kind.
 *      Tcl_Encoding *encPtr - Output location for the Tcl encoding handle.
 *
 * Results:
 *      Returns TCL_OK if the encoding is detected and the probe bytes are restored.
 *      Returns TCL_ERROR if the file is too short, the encoding is unsupported, the Tcl encoding cannot be obtained,
 *      or the probe bytes cannot be pushed back.
 *
 * Side Effects:
 *      Reads six bytes from chan and restores them with Tcl_Ungets() on success.
 *      Stores a Tcl_Encoding handle in *encPtr; the caller must release it with Tcl_FreeEncoding().
 *      Sets the interpreter result on failure where appropriate.
 *
 * Notes:
 *      UTF-16LE detection checks only the first three characters of "Title:".
 *      Assumes the file begins directly with "Title:" or its UTF-16LE equivalent, without a BOM or leading whitespace.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int DetectEncoding(Tcl_Interp *interp, Tcl_Channel chan, EncKind *kindPtr, Tcl_Encoding *encPtr) {
    unsigned char buffer[6];
    Tcl_Size n;
    n = Tcl_Read(chan, (char *)buffer, 6);
    if (n != 6) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("file too short while detecting encoding", -1));
        return TCL_ERROR;
    }
    if (memcmp(buffer, "Title:", 6) == 0) {
        *kindPtr = ENC_KIND_UTF8;
        *encPtr = Tcl_GetEncoding(interp, "utf-8");
    } else {
        static const unsigned char utf16le_tit[6] = {'T', 0x00, 'i', 0x00, 't', 0x00};
        if (memcmp(buffer, utf16le_tit, 6) == 0) {
            *kindPtr = ENC_KIND_UTF16LE;
            *encPtr = Tcl_GetEncoding(interp, "utf-16le");
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown file text encoding", -1));
            return TCL_ERROR;
        }
    }
    if (*encPtr == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_Ungets(chan, (const char *)buffer, 6, 0) != 6) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to push back encoding-detection bytes", -1));
        Tcl_FreeEncoding(*encPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}

//***  ReadRawHeaderLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadRawHeaderLine --
 *
 *      Reads one raw encoded header line from chan, excluding the newline terminator.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      Tcl_Channel chan        - Binary input channel positioned at the start of a header line.
 *      EncKind kind            - Detected header encoding kind.
 *      Tcl_DString *rawLinePtr - Output raw byte buffer, initialized by this function.
 *
 * Results:
 *      Returns RAW_HEADER_OK if a line was read successfully.
 *      Returns RAW_HEADER_EOF if EOF is reached before any bytes of a new line are read.
 *      Returns RAW_HEADER_ERROR on input error or truncated UTF-16LE input.
 *
 * Side Effects:
 *      Consumes bytes from chan up to and including the line terminator.
 *      Initializes and appends raw encoded bytes to rawLinePtr.
 *      Sets the interpreter result for truncated UTF-16LE input.
 *
 * Notes:
 *      UTF-8 lines end at byte '\n'; UTF-16LE lines end at the byte sequence '\n' 0x00.
 *      The returned Tcl_DString contains raw bytes, not decoded text, and may contain embedded NUL bytes.
 *      A non-empty final line without a trailing newline is accepted.
 *      The caller should call Tcl_DStringFree(rawLinePtr) after RAW_HEADER_OK or RAW_HEADER_ERROR.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ReadRawHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_DString *rawLinePtr) {
    Tcl_DStringInit(rawLinePtr);
    if (kind == ENC_KIND_UTF8) {
        for (;;) {
            unsigned char c;
            Tcl_Size n = Tcl_Read(chan, (char *)&c, 1);
            if (n == 0) {
                if (Tcl_DStringLength(rawLinePtr) == 0) {
                    return RAW_HEADER_EOF;
                }
                return RAW_HEADER_OK;
            }
            if (n < 0) {
                return RAW_HEADER_ERROR;
            }
            if (c == '\n') {
                return RAW_HEADER_OK;
            }
            Tcl_DStringAppend(rawLinePtr, (const char *)&c, 1);
        }
    } else {
        for (;;) {
            unsigned char pair[2];
            Tcl_Size n = Tcl_Read(chan, (char *)pair, 2);
            if (n == 0) {
                if (Tcl_DStringLength(rawLinePtr) == 0) {
                    return RAW_HEADER_EOF;
                }
                return RAW_HEADER_OK;
            }
            if (n != 2) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("truncated UTF-16LE header line", -1));
                return RAW_HEADER_ERROR;
            }
            if (pair[0] == '\n' && pair[1] == 0x00) {
                return RAW_HEADER_OK;
            }
            Tcl_DStringAppend(rawLinePtr, (const char *)pair, 2);
        }
    }
}

//***  DecodeHeaderLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * DecodeHeaderLine --
 *
 *      Decodes one raw encoded header line into Tcl UTF-8.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for conversion error reporting.
 *      Tcl_Encoding enc        - Encoding handle used to decode the raw bytes.
 *      Tcl_DString *rawLinePtr - Input raw byte buffer.
 *      Tcl_DString *utfLinePtr - Output decoded UTF-8 string, initialized by this function.
 *
 * Results:
 *      Returns TCL_OK if decoding succeeds; TCL_ERROR if conversion fails.
 *
 * Side Effects:
 *      Initializes utfLinePtr and appends decoded UTF-8 text on success.
 *      Frees utfLinePtr before returning TCL_ERROR.
 *
 * Notes:
 *      rawLinePtr may contain embedded NUL bytes, especially for UTF-16LE input.
 *      The decoded line may still contain a trailing carriage return from CRLF line endings.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int DecodeHeaderLine(Tcl_Interp *interp, Tcl_Encoding enc, Tcl_DString *rawLinePtr, Tcl_DString *utfLinePtr) {
    Tcl_Size errorIndex = -1;

    Tcl_DStringInit(utfLinePtr);
    if (Tcl_ExternalToUtfDStringEx(interp, enc, Tcl_DStringValue(rawLinePtr), Tcl_DStringLength(rawLinePtr), 0,
                                   utfLinePtr, &errorIndex) != TCL_OK) {
        Tcl_DStringFree(utfLinePtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}

//***  ReadDecodedHeaderLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadDecodedHeaderLine --
 *
 *      Reads one encoded header line, decodes it to Tcl UTF-8, and removes a trailing carriage return.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      Tcl_Channel chan        - Binary input channel positioned at the start of a header line.
 *      EncKind kind            - Detected raw header encoding kind.
 *      Tcl_Encoding enc        - Encoding handle used to decode the raw bytes.
 *      Tcl_DString *utfLinePtr - Output decoded line, initialized on RAW_HEADER_OK.
 *
 * Results:
 *      Returns RAW_HEADER_OK if a line was read and decoded.
 *      Returns RAW_HEADER_EOF if EOF is reached before a new line is read.
 *      Returns RAW_HEADER_ERROR on read or decoding error.
 *
 * Side Effects:
 *      Consumes one encoded header line from chan.
 *      Uses a temporary raw byte buffer during reading.
 *      Removes a trailing '\r' from the decoded line.
 *
 * Notes:
 *      The returned line is decoded Tcl UTF-8 text suitable for header-key and marker comparisons.
 *      On RAW_HEADER_OK, the caller must free utfLinePtr with Tcl_DStringFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ReadDecodedHeaderLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                 Tcl_DString *utfLinePtr) {
    Tcl_DString rawLine;
    int r;

    r = ReadRawHeaderLine(interp, chan, kind, &rawLine);
    if (r == RAW_HEADER_EOF) {
        return RAW_HEADER_EOF;
    }
    if (r == RAW_HEADER_ERROR) {
        return RAW_HEADER_ERROR;
    }
    if (DecodeHeaderLine(interp, enc, &rawLine, utfLinePtr) != TCL_OK) {
        Tcl_DStringFree(&rawLine);
        return RAW_HEADER_ERROR;
    }
    Tcl_DStringFree(&rawLine);
    StripTrailingCR(utfLinePtr);
    return RAW_HEADER_OK;
}

//***  StripTrailingCR function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * StripTrailingCR --
 *
 *      Removes a trailing carriage return from a decoded header line.
 *
 * Parameters:
 *      Tcl_DString *dsPtr - Decoded Tcl_DString line to normalize.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Shortens dsPtr by one byte if it ends with '\r'; otherwise leaves it unchanged.
 *
 * Notes:
 *      Apply after decoding, not to the raw encoded line.
 *      Assumes dsPtr is initialized and contains decoded text.
 *      Pointers from Tcl_DStringValue(dsPtr) may become invalid if the string is shortened.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void StripTrailingCR(Tcl_DString *dsPtr) {
    const char *s = Tcl_DStringValue(dsPtr);
    Tcl_Size len = Tcl_DStringLength(dsPtr);

    if (len > 0 && s[len - 1] == '\r') {
        Tcl_DStringSetLength(dsPtr, len - 1);
    }
}

//** Header parsing
//***  AppendFlag function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * AppendFlag --
 *
 *      Appends one flag token to h->flags and updates h->flagsMask for recognized flags.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawHeader *h       - Header structure to update.
 *      const char *start  - Pointer to the first byte of the flag token.
 *      Tcl_Size len       - Number of bytes in the flag token.
 *
 * Results:
 *      Returns TCL_OK if the flag is appended successfully.
 *      Returns TCL_ERROR if the flags array cannot be resized.
 *
 * Side Effects:
 *      Resizes h->flags, stores an owned NUL-terminated copy, and increments h->numFlags.
 *      Updates h->flagsMask for recognized flags; unknown flags are preserved without setting a mask bit.
 *
 * Notes:
 *      Stored flag strings are owned by RawHeader and must be released by RawHeaderFree().
 *      Flag comparison is byte-wise and case-sensitive.
 *      len is a byte count, not a character count.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int AppendFlag(Tcl_Interp *interp, RawHeader *h, const char *start, Tcl_Size len) {
    char **newFlags;

    newFlags = (char **)Tcl_Realloc((char *)h->flags, sizeof(char *) * (h->numFlags + 1));
    if (newFlags == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to allocate raw flags", -1));
        return TCL_ERROR;
    }
    h->flags = newFlags;
    h->flags[h->numFlags] = StrDupLen(start, len);
    h->numFlags++;
    if (len == 4 && strncmp(start, "real", 4) == 0) {
        h->flagsMask |= RAW_FLAG_REAL;
    } else if (len == 6 && strncmp(start, "double", 6) == 0) {
        h->flagsMask |= RAW_FLAG_DOUBLE;
    } else if (len == 7 && strncmp(start, "complex", 7) == 0) {
        h->flagsMask |= RAW_FLAG_COMPLEX;
    } else if (len == 7 && strncmp(start, "stepped", 7) == 0) {
        h->flagsMask |= RAW_FLAG_STEPPED;
    } else if (len == 10 && strncmp(start, "FastAccess", 10) == 0) {
        h->flagsMask |= RAW_FLAG_FASTACCESS;
    }
    return TCL_OK;
}

//***  ParseFlags function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ParseFlags --
 *
 *      Parses whitespace-separated flag tokens from value and appends them to the RawHeader.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawHeader *h       - Header structure whose flag list and mask are updated.
 *      const char *value  - NUL-terminated Flags value string to parse.
 *
 * Results:
 *      Returns TCL_OK if all flag tokens are parsed and appended.
 *      Returns TCL_ERROR if storing a flag token fails.
 *
 * Side Effects:
 *      Appends owned flag strings to h->flags through AppendFlag().
 *      Updates h->numFlags and h->flagsMask through AppendFlag().
 *
 * Notes:
 *      Tokens are separated with isspace() whitespace.
 *      Parsing is byte-oriented and case-sensitive.
 *      Empty or all-whitespace values are accepted.
 *      The input string is borrowed and is not modified.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ParseFlags(Tcl_Interp *interp, RawHeader *h, const char *value) {
    const char *p = SkipSpace(value);
    while (*p) {
        const char *start;
        Tcl_Size len;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        len = (Tcl_Size)(p - start);
        if (AppendFlag(interp, h, start, len) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

//***  RawIsVariableTypeToken function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawIsVariableTypeToken --
 *
 *      Tests whether a token is a recognized raw variable type name.
 *
 * Parameters:
 *      const char *start - Pointer to the first byte of the token.
 *      Tcl_Size len      - Number of bytes in the token.
 *
 * Results:
 *      Returns 1 if the token matches a recognized type; 0 otherwise.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      The comparison is byte-wise and case-sensitive.
 *      The token is a pointer/length pair and does not need to be NUL-terminated.
 *      Add simulator-specific type names to the static types[] table if needed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawIsVariableTypeToken(const char *start, Tcl_Size len) {
    for (int i = 0; types[i] != NULL; i++) {
        size_t n = strlen(types[i]);
        if ((Tcl_Size)n == len && strncmp(start, types[i], n) == 0) {
            return 1;
        }
    }
    return 0;
}

//***  ParseVariableLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ParseVariableLine --
 *
 *      Parses one Variables-section entry into a RawVariable structure.
 *      Variable names may contain whitespace; the type is found by scanning for a recognized type token.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting and numeric parsing.
 *      const char *line   - Decoded NUL-terminated Variables-section line to parse.
 *      RawVariable *v     - Output variable structure to initialize and fill.
 *
 * Results:
 *      Returns TCL_OK if the line is parsed successfully.
 *      Returns TCL_ERROR if the index, name, or recognized type token is missing or invalid.
 *
 * Side Effects:
 *      Clears v with memset().
 *      Allocates owned strings for v->name and v->type.
 *      Sets the interpreter result on malformed input.
 *
 * Notes:
 *      The first token is interpreted as the variable index.
 *      The type is detected with RawIsVariableTypeToken(); later attributes are ignored.
 *      The search for the type starts after the first name token, so one-token names such as "frequency" are allowed.
 *      Stored strings are owned by RawVariable and must be released by RawHeaderFree().
 *      The input line is borrowed and is not modified.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ParseVariableLine(Tcl_Interp *interp, const char *line, RawVariable *v) {
    const char *p = line;
    const char *idxStart;
    const char *restStart;
    const char *scan;
    const char *typeStart = NULL;
    const char *nameEnd = NULL;
    Tcl_Size idxLen;
    Tcl_Size typeLen = 0;
    Tcl_Size nameLen;
    char *idxString;
    Tcl_Size index;
    memset(v, 0, sizeof *v);
    /*
     * First token is always the variable index.
     */
    if (!NextToken(&p, &idxStart, &idxLen)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("malformed Variables entry in raw header", -1));
        return TCL_ERROR;
    }
    idxString = StrDupLen(idxStart, idxLen);
    if (ParseSizeField(interp, idxString, &index) != TCL_OK) {
        Tcl_Free(idxString);
        return TCL_ERROR;
    }
    Tcl_Free(idxString);
    /*
     * Everything after the index starts with the variable name.
     * The name may contain spaces, for example:
     *
     *     v(m1#body diode)
     *
     * Therefore we cannot parse the name as a single whitespace token.
     */
    restStart = SkipSpace(p);
    if (*restStart == '\0') {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Variables entry has no name and type", -1));
        return TCL_ERROR;
    }
    /*
     * Find the variable type token. Attributes such as "grid=3" may follow
     * the type token, so the type is not necessarily the last token.
     *
     * To avoid treating a one-token name such as "frequency" as the type,
     * start checking from the second token after the index.
     */
    scan = SkipToken(restStart);
    while (*scan) {
        const char *tokStart;
        Tcl_Size tokLen;
        if (!NextToken(&scan, &tokStart, &tokLen)) {
            break;
        }
        if (RawIsVariableTypeToken(tokStart, tokLen)) {
            typeStart = tokStart;
            typeLen = tokLen;
            nameEnd = tokStart;
            while (nameEnd > restStart && isspace((unsigned char)nameEnd[-1])) {
                nameEnd--;
            }
            break;
        }
    }
    if (typeStart == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Variables entry has no recognized variable type", -1));
        return TCL_ERROR;
    }
    nameLen = (Tcl_Size)(nameEnd - restStart);
    if (nameLen <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Variables entry has empty variable name", -1));
        return TCL_ERROR;
    }
    v->index = index;
    v->name = StrDupLen(restStart, nameLen);
    v->type = StrDupLen(typeStart, typeLen);
    return TCL_OK;
}

//***  ReadVariablesSection function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadVariablesSection --
 *
 *      Reads and parses exactly h->numVariables entries from the Variables section.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tcl_Channel chan   - Binary input channel positioned after the "Variables:" marker.
 *      EncKind kind       - Detected raw header encoding kind.
 *      Tcl_Encoding enc   - Encoding handle used to decode variable lines.
 *      RawHeader *h       - Header being filled; h->haveNumVariables and h->numVariables must already be set.
 *
 * Results:
 *      Returns TCL_OK if all variable lines are read and parsed.
 *      Returns TCL_ERROR if the variable count is missing or invalid, allocation would overflow, EOF is reached early,
 *      or a variable line cannot be read, decoded, or parsed.
 *
 * Side Effects:
 *      Allocates and initializes h->variables.
 *      Reads h->numVariables decoded lines from chan.
 *      Fills h->variables entries.
 *      Sets the interpreter result on failure where appropriate.
 *
 * Notes:
 *      h->variables and strings owned by each RawVariable are released by RawHeaderFree().
 *      Assumes h has been initialized and h->variables is not already populated.
 *      On error after partial population, the caller should clean up with RawHeaderFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ReadVariablesSection(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h) {
    if (!h->haveNumVariables) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Variables section appeared before No. Variables", -1));
        return TCL_ERROR;
    }
    if (h->numVariables < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("negative variable count", -1));
        return TCL_ERROR;
    }
    if ((size_t)h->numVariables > SIZE_MAX / sizeof(RawVariable)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("variable count too large", -1));
        return TCL_ERROR;
    }
    h->variables = (RawVariable *)Tcl_Alloc(sizeof(RawVariable) * (size_t)h->numVariables);
    memset(h->variables, 0, sizeof(RawVariable) * (size_t)h->numVariables);
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        Tcl_DString utfLine;
        int r;
        r = ReadDecodedHeaderLine(interp, chan, kind, enc, &utfLine);
        if (r == RAW_HEADER_EOF) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unexpected EOF inside Variables section", -1));
            return TCL_ERROR;
        }
        if (r == RAW_HEADER_ERROR) {
            return TCL_ERROR;
        }
        if (ParseVariableLine(interp, Tcl_DStringValue(&utfLine), &h->variables[i]) != TCL_OK) {
            Tcl_DStringFree(&utfLine);
            return TCL_ERROR;
        }
        Tcl_DStringFree(&utfLine);
    }
    return TCL_OK;
}

//***  ReadHeader function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadHeader --
 *
 *      Reads and parses one raw-file plot header from the current channel position.
 *      Stops after consuming a Binary: or Values: marker, leaving chan at the data block.
 *
 * Parameters:
 *      Tcl_Interp *interp    - Interpreter used for error reporting.
 *      Tcl_Channel chan      - Binary input channel positioned at a header, separator, or EOF.
 *      EncKind kind          - Detected raw header encoding kind.
 *      Tcl_Encoding enc      - Encoding handle used to decode header lines.
 *      RawHeader *h          - Output header, initialized and filled by this function.
 *      DataKind *dataKindPtr - Output data block kind.
 *
 * Results:
 *      Returns RAW_HEADER_OK if a complete header and data marker are read.
 *      Returns RAW_HEADER_EOF if EOF is reached before any nonblank header line.
 *      Returns RAW_HEADER_ERROR on read, decode, parse, layout, or premature EOF error.
 *
 * Side Effects:
 *      Initializes and fills h.
 *      Reads and consumes the Variables: section when present.
 *      Resolves variable layout before returning RAW_HEADER_OK.
 *      Consumes the Binary: or Values: marker.
 *      Sets the interpreter result on errors.
 *
 * Notes:
 *      Blank lines before a header are ignored as separators.
 *      Unknown header lines are ignored.
 *      On RAW_HEADER_OK, the caller owns h and must release it with RawHeaderFree() or transfer ownership.
 *      On RAW_HEADER_ERROR after partial allocation, the caller should clean up h with RawHeaderFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static RawHeaderStatus ReadHeader(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                                  DataKind *dataKindPtr) {
    int sawAnyLine = 0;
    RawHeaderInit(h);
    for (;;) {
        Tcl_DString utfLine;
        const char *line;
        int r;
        r = ReadDecodedHeaderLine(interp, chan, kind, enc, &utfLine);
        if (r == RAW_HEADER_EOF) {
            if (!sawAnyLine) {
                return RAW_HEADER_EOF; /* clean EOF before next header */
            }
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unexpected EOF inside raw header", -1));
            return RAW_HEADER_ERROR;
        }
        if (r == RAW_HEADER_ERROR) {
            return RAW_HEADER_ERROR;
        }
        line = Tcl_DStringValue(&utfLine);
        /*
         * Blank lines can appear between raw plots or after the final data block.
         * They are separators, not header content.
         */
        if (*line == '\0') {
            Tcl_DStringFree(&utfLine);
            continue;
        }
        sawAnyLine = 1;
        if (strcmp(line, "Binary:") == 0) {
            *dataKindPtr = DATA_BINARY;
            Tcl_DStringFree(&utfLine);
            return RAW_HEADER_OK;
        }
        if (strcmp(line, "Values:") == 0) {
            *dataKindPtr = DATA_VALUES;
            Tcl_DStringFree(&utfLine);
            return RAW_HEADER_OK;
        }
        if (strcmp(line, "Variables:") == 0) {
            Tcl_DStringFree(&utfLine);
            if (ReadVariablesSection(interp, chan, kind, enc, h) != TCL_OK) {
                return RAW_HEADER_ERROR;
            }
            continue;
        }
        if (StartsWith(line, "Title:")) {
            if (SetStringField(&h->title, line + strlen("Title:")) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
        } else if (StartsWith(line, "Date:")) {
            if (SetStringField(&h->date, line + strlen("Date:")) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
        } else if (StartsWith(line, "Plotname:")) {
            if (SetStringField(&h->plotname, line + strlen("Plotname:")) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
        } else if (StartsWith(line, "Flags:")) {
            if (ParseFlags(interp, h, line + strlen("Flags:")) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
        } else if (StartsWith(line, "No. Variables:")) {
            if (ParseSizeField(interp, line + strlen("No. Variables:"), &h->numVariables) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
            h->haveNumVariables = 1;
        } else if (StartsWith(line, "No. Points:")) {
            if (ParseSizeField(interp, line + strlen("No. Points:"), &h->numPoints) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
            h->haveNumPoints = 1;
        } else if (*line == '\0') {
            /*
               Optional: ignore blank lines.
            */
        } else {
            /*
               Unknown header line.
            */
        }
        Tcl_DStringFree(&utfLine);
    }
}

//** Storage/layout resolution
//***  ResolveDefaultStorage function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ResolveDefaultStorage --
 *
 *      Resolves the header-level default value storage from h->flagsMask.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawHeader *h       - Header to update.
 *
 * Results:
 *      Returns TCL_OK if a recognized storage flag is present; TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Updates h->defaultStorage and h->defaultValueBytes.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Storage precedence is complex, then double, then real.
 *      Resolved sizes are real32 = 4 bytes, real64 = 8 bytes, complex128 = 16 bytes.
 *      Per-variable storage exceptions should be resolved separately.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ResolveDefaultStorage(Tcl_Interp *interp, RawHeader *h) {
    if (h->flagsMask & RAW_FLAG_COMPLEX) {
        h->defaultStorage = RAW_VALUE_COMPLEX128;
        h->defaultValueBytes = 16;
        return TCL_OK;
    }
    if ((h->flagsMask & RAW_FLAG_DOUBLE) || (h->flagsMask & RAW_FLAG_REAL)) {
        h->defaultStorage = RAW_VALUE_REAL64;
        h->defaultValueBytes = 8;
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("raw header has no recognized storage flag", -1));
    return TCL_ERROR;
}

//***  RawSetVariableLayout function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawSetVariableLayout --
 *
 *      Stores storage layout information for one variable and advances the running point-stride offset.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *      RawHeader *h             - Header containing the variable table to update.
 *      Tcl_Size index           - Physical variable index into h->variables.
 *      RawValueStorage storage  - Storage representation assigned to the variable.
 *      Tcl_Size valueBytes      - Number of bytes occupied by the variable in one binary point.
 *      Tcl_Size *offsetPtr      - Running byte offset, updated on success.
 *
 * Results:
 *      Returns TCL_OK if the variable layout is stored successfully.
 *      Returns TCL_ERROR if advancing the offset would overflow.
 *
 * Side Effects:
 *      Updates the selected variable's storage, valueBytes, and offsetBytes fields.
 *      Advances *offsetPtr by valueBytes on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The index is a physical index into h->variables.
 *      The caller is responsible for validating index before calling this function.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawSetVariableLayout(Tcl_Interp *interp, RawHeader *h, Tcl_Size index, RawValueStorage storage,
                                Tcl_Size valueBytes, Tcl_Size *offsetPtr) {
    RawVariable *v = &h->variables[index];
    v->storage = storage;
    v->valueBytes = valueBytes;
    v->offsetBytes = *offsetPtr;
    if (valueBytes > TCL_SIZE_MAX - *offsetPtr) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw point stride overflow", -1));
        return TCL_ERROR;
    }
    *offsetPtr += valueBytes;
    return TCL_OK;
}

//***  RawHeaderResolveVariableLayout function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderResolveVariableLayout --
 *
 *      Resolves per-variable storage, byte offsets, and total point stride for a parsed raw header.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      RawHeader *h            - Parsed header whose flags and variables have already been filled.
 *      RawDialect dialect      - Raw-file dialect used to resolve storage layout.
 *      int ltspiceAllDouble    - LTspice real-data flag; non-zero means all real values are stored as doubles.
 *
 * Results:
 *      Returns TCL_OK if storage and offsets are resolved.
 *      Returns TCL_ERROR if storage cannot be resolved or the point stride would overflow.
 *
 * Side Effects:
 *      Updates h->defaultStorage and h->defaultValueBytes.
 *      Updates each variable's storage, valueBytes, and offsetBytes.
 *      Updates h->pointStrideBytes.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The generic dialect applies the header-level storage format to every variable.
 *      The LTspice dialect applies LTspice binary layout rules, including mixed double/float real data.
 *      Assumes h->variables contains h->numVariables initialized RawVariable entries.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawHeaderResolveVariableLayout(Tcl_Interp *interp, RawHeader *h, RawDialect dialect, int ltspiceAllDouble) {
    Tcl_Size offset = 0;
    if (h->numVariables < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("negative variable count", -1));
        return TCL_ERROR;
    }
    if (dialect == RAW_DIALECT_LTSPICE) {
        if (h->flagsMask & RAW_FLAG_FASTACCESS) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice FastAccess raw files are not supported yet", -1));
            return TCL_ERROR;
        }
        if (h->flagsMask & RAW_FLAG_COMPLEX) {
            h->defaultStorage = RAW_VALUE_COMPLEX128;
            h->defaultValueBytes = 16;
            for (Tcl_Size i = 0; i < h->numVariables; i++) {
                if (RawSetVariableLayout(interp, h, i, RAW_VALUE_COMPLEX128, 16, &offset) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            h->pointStrideBytes = offset;
            return TCL_OK;
        }
        if (ltspiceAllDouble) {
            h->defaultStorage = RAW_VALUE_REAL64;
            h->defaultValueBytes = 8;
            for (Tcl_Size i = 0; i < h->numVariables; i++) {
                if (RawSetVariableLayout(interp, h, i, RAW_VALUE_REAL64, 8, &offset) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            h->pointStrideBytes = offset;
            return TCL_OK;
        }
        /*
         * LTspice normal binary real layout:
         *
         *     variable 0: double
         *     variables 1..N: float
         *
         * This covers transient time + traces and sweep axis + traces.
         */
        h->defaultStorage = RAW_VALUE_REAL32;
        h->defaultValueBytes = 4;
        for (Tcl_Size i = 0; i < h->numVariables; i++) {
            if (i == 0) {
                if (RawSetVariableLayout(interp, h, i, RAW_VALUE_REAL64, 8, &offset) != TCL_OK) {
                    return TCL_ERROR;
                }
            } else {
                if (RawSetVariableLayout(interp, h, i, RAW_VALUE_REAL32, 4, &offset) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
        }
        h->pointStrideBytes = offset;
        return TCL_OK;
    }
    /*
     * Generic/ngspice/Xyce layout.
     */
    if (ResolveDefaultStorage(interp, h) != TCL_OK) {
        return TCL_ERROR;
    }
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        if (RawSetVariableLayout(interp, h, i, h->defaultStorage, h->defaultValueBytes, &offset) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    h->pointStrideBytes = offset;
    return TCL_OK;
}

//***  ComputeBinaryByteCount function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ComputeBinaryByteCount --
 *
 *      Computes the byte size of the binary data block.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      const RawHeader *h  - Parsed header with No. Points and resolved pointStrideBytes.
 *      Tcl_Size *nbytesPtr - Output location for the computed byte count.
 *
 * Results:
 *      Returns TCL_OK if the byte count is computed successfully.
 *      Returns TCL_ERROR if No. Points is missing, size fields are invalid, or multiplication would overflow.
 *
 * Side Effects:
 *      Writes the computed byte count to *nbytesPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Assumes h->pointStrideBytes has already been resolved by RawHeaderResolveVariableLayout().
 *      This size rule applies to Binary: blocks, not textual Values: blocks.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ComputeBinaryByteCount(Tcl_Interp *interp, const RawHeader *h, Tcl_Size *nbytesPtr) {
    if (!h->haveNumPoints) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot compute binary size without No. Points", -1));
        return TCL_ERROR;
    }
    if (h->numPoints < 0 || h->pointStrideBytes < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid raw binary size fields", -1));
        return TCL_ERROR;
    }
    if (h->numPoints != 0 && h->pointStrideBytes > TCL_SIZE_MAX / h->numPoints) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw binary byte count overflow", -1));
        return TCL_ERROR;
    }
    *nbytesPtr = h->numPoints * h->pointStrideBytes;
    return TCL_OK;
}

//** Tcl object/result builders
//***  RawHeaderToDictObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderToDictObj --
 *
 *      Builds a Tcl dictionary containing parsed RawHeader metadata.
 *
 * Parameters:
 *      const RawHeader *h - Header to convert.
 *
 * Results:
 *      Returns a newly created Tcl dictionary object.
 *
 * Side Effects:
 *      Allocates the result dictionary, flags list, variables list, and per-variable dictionaries.
 *      Does not modify h.
 *
 * Notes:
 *      Missing string fields are represented as empty strings.
 *      Variable storage names are derived from resolved RawVariable storage fields.
 *      Layout fields such as pointstride, valuebytes, and offsetbytes are meaningful only after layout resolution.
 *      The returned object follows normal Tcl object ownership conventions.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static Tcl_Obj *RawHeaderToDictObj(const RawHeader *h) {
    Tcl_Obj *dictObj = Tcl_NewDictObj();
    Tcl_Obj *flagsObj = Tcl_NewListObj(0, NULL);
    Tcl_Obj *varsObj = Tcl_NewListObj(0, NULL);
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("title", -1), Tcl_NewStringObj(h->title ? h->title : "", -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("date", -1), Tcl_NewStringObj(h->date ? h->date : "", -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("plotname", -1),
                   Tcl_NewStringObj(h->plotname ? h->plotname : "", -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("nvariables", -1), Tcl_NewWideIntObj((Tcl_WideInt)h->numVariables));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("npoints", -1), Tcl_NewWideIntObj((Tcl_WideInt)h->numPoints));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("pointstride", -1),
                   Tcl_NewWideIntObj((Tcl_WideInt)h->pointStrideBytes));
    for (Tcl_Size i = 0; i < h->numFlags; i++) {
        Tcl_ListObjAppendElement(NULL, flagsObj, Tcl_NewStringObj(h->flags[i] ? h->flags[i] : "", -1));
    }
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("flags", -1), flagsObj);
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        RawVariable *v = &h->variables[i];
        Tcl_Obj *vdict = Tcl_NewDictObj();
        const char *storageName = "unknown";
        switch (v->storage) {
        case RAW_VALUE_REAL32:
            storageName = "real32";
            break;
        case RAW_VALUE_REAL64:
            storageName = "real64";
            break;
        case RAW_VALUE_COMPLEX128:
            storageName = "complex128";
            break;
        default:
            break;
        }
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("index", -1), Tcl_NewWideIntObj((Tcl_WideInt)v->index));
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("name", -1), Tcl_NewStringObj(v->name ? v->name : "", -1));
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("type", -1), Tcl_NewStringObj(v->type ? v->type : "", -1));
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("storage", -1), Tcl_NewStringObj(storageName, -1));
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("valuebytes", -1), Tcl_NewWideIntObj((Tcl_WideInt)v->valueBytes));
        Tcl_DictObjPut(NULL, vdict, Tcl_NewStringObj("offsetbytes", -1),
                       Tcl_NewWideIntObj((Tcl_WideInt)v->offsetBytes));
        Tcl_ListObjAppendElement(NULL, varsObj, vdict);
    }
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("variables", -1), varsObj);
    return dictObj;
}

//***  RawDataKindName function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawDataKindName --
 *
 *      Returns a human-readable name for a DataKind value.
 *
 * Parameters:
 *      DataKind kind - Data block kind to convert.
 *
 * Results:
 *      Returns "binary", "values", or "unknown".
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      The returned string is static storage and must not be freed or modified.
 *      Unknown or uninitialized values are mapped to "unknown".
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static const char *RawDataKindName(DataKind kind) {
    switch (kind) {
    case DATA_BINARY:
        return "binary";
    case DATA_VALUES:
        return "values";
    default:
        return "unknown";
    }
}

//***  RawPlotSummaryObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotSummaryObj --
 *
 *      Builds a compact Tcl dictionary summary of one RawPlot.
 *
 * Parameters:
 *      const RawPlot *plot - Plot to summarize.
 *      Tcl_Size index      - Plot index to store in the summary.
 *
 * Results:
 *      Returns a newly created Tcl dictionary object.
 *
 * Side Effects:
 *      Allocates a Tcl dictionary and contained Tcl objects.
 *      Does not modify plot.
 *
 * Notes:
 *      Includes plot index, data kind, title, plot name, variable count, point count, data offset, and data byte count.
 *      The data kind is converted with RawDataKindName().
 *      The returned object follows normal Tcl object ownership conventions.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static Tcl_Obj *RawPlotSummaryObj(const RawPlot *plot, Tcl_Size index) {
    const RawHeader *h = &plot->header;
    Tcl_Obj *dictObj = Tcl_NewDictObj();
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("index", -1), Tcl_NewWideIntObj((Tcl_WideInt)index));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("kind", -1), Tcl_NewStringObj(RawDataKindName(plot->dataKind), -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("title", -1), Tcl_NewStringObj(h->title ? h->title : "", -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("plotname", -1),
                   Tcl_NewStringObj(h->plotname ? h->plotname : "", -1));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("nvariables", -1), Tcl_NewWideIntObj((Tcl_WideInt)h->numVariables));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("npoints", -1), Tcl_NewWideIntObj((Tcl_WideInt)h->numPoints));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("dataoffset", -1), Tcl_NewWideIntObj(plot->dataOffset));
    Tcl_DictObjPut(NULL, dictObj, Tcl_NewStringObj("databytes", -1), Tcl_NewWideIntObj((Tcl_WideInt)plot->dataBytes));
    return dictObj;
}

//***  RawBuildVectorResult function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawBuildVectorResult --
 *
 *      Builds the final Tcl result object from an array of raw vector value lists.
 *
 *      The function supports two result shapes:
 *
 *          RAW_VECTOR_RESULT_LIST - Return the single selected vector list directly.
 *          RAW_VECTOR_RESULT_DICT - Return a dictionary mapping vector names to value lists.
 *
 * Parameters:
 *      Tcl_Interp *interp              - Interpreter used for dictionary operations and error reporting.
 *      RawHeader *h                    - Header containing variable names for dictionary keys.
 *      Tcl_Size numVars                - Number of selected vectors and entries in vecObjs/varIndexes.
 *      Tcl_Size *varIndexes            - Array of physical variable indexes into h->variables.
 *      Tcl_Obj **vecObjs               - Array of Tcl list objects containing decoded vector values.
 *      RawVectorResultMode resultMode  - Requested result shape.
 *      Tcl_Obj **objPtr                - Output location for the constructed result object.
 *
 * Results:
 *      Returns TCL_OK if the result object is built successfully.
 *      Returns TCL_ERROR if resultMode is invalid, or if list result mode is requested with anything other than one
 *      selected vector.
 *
 * Side Effects:
 *      In RAW_VECTOR_RESULT_LIST mode, stores vecObjs[0] in *objPtr and frees only the vecObjs array.
 *
 *      In RAW_VECTOR_RESULT_DICT mode, creates a new dictionary, inserts each vector list under its variable name,
 *      stores the dictionary in *objPtr, and frees the vecObjs array.
 *
 *      On error, releases all vector objects with RawFreeVectorObjects() and sets the interpreter result.
 *
 * Notes:
 *      On success, ownership of the returned Tcl object is transferred to the caller through *objPtr.
 *      On success, the vecObjs pointer array is always freed by this function.
 *      In dictionary mode, variable names are taken from h->variables[varIndexes[i]].name. Missing names are
 *      represented as empty strings.
 *      varIndexes contains physical indexes into h->variables, not necessarily the numeric indexes written in the
 *      raw-file Variables section.
 *      Duplicate dictionary keys should already have been rejected by the variable-resolution step.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawBuildVectorResult(Tcl_Interp *interp, RawHeader *h, Tcl_Size numVars, Tcl_Size *varIndexes,
                                Tcl_Obj **vecObjs, RawVectorResultMode resultMode, Tcl_Obj **objPtr) {
    if (resultMode == RAW_VECTOR_RESULT_LIST) {
        if (numVars != 1) {
            RawFreeVectorObjects(vecObjs, numVars);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("list result mode requires exactly one vector", -1));
            return TCL_ERROR;
        }
        *objPtr = vecObjs[0];
        Tcl_Free((char *)vecObjs);
        return TCL_OK;
    }
    if (resultMode == RAW_VECTOR_RESULT_DICT) {
        Tcl_Obj *dictObj = Tcl_NewDictObj();
        for (Tcl_Size i = 0; i < numVars; i++) {
            RawVariable *var = &h->variables[varIndexes[i]];
            const char *name = var->name ? var->name : "";

            Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj(name, -1), vecObjs[i]);
        }
        Tcl_Free((char *)vecObjs);
        *objPtr = dictObj;
        return TCL_OK;
    }
    RawFreeVectorObjects(vecObjs, numVars);
    Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw vector result mode", -1));
    return TCL_ERROR;
}

//***  RawFreeVectorObjects function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFreeVectorObjects --
 *
 *      Releases an array of Tcl list objects created for raw vector output and then frees the array itself.
 *
 * Parameters:
 *      Tcl_Obj **vecObjs - Array of Tcl object pointers to release. May be NULL.
 *      Tcl_Size numVars  - Number of entries in vecObjs.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases each non-NULL object in vecObjs.
 *      Frees the vecObjs array with Tcl_Free().
 *
 * Notes:
 *      This helper is intended for error cleanup paths before the vector objects have been returned to Tcl or inserted
 *      into another result object.
 *
 *      Each object is temporarily reference-counted with Tcl_IncrRefCount() and immediately released with
 *      Tcl_DecrRefCount(). This safely disposes of newly created Tcl objects whose reference count is still zero.
 *
 *      The function does not clear the caller's vecObjs pointer.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawFreeVectorObjects(Tcl_Obj **vecObjs, Tcl_Size numVars) {
    if (vecObjs == NULL) {
        return;
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (vecObjs[i]) {
            Tcl_IncrRefCount(vecObjs[i]);
            Tcl_DecrRefCount(vecObjs[i]);
        }
    }
    Tcl_Free((char *)vecObjs);
}

//** Binary value reading/decoding
//***  ReadLEFloat32AsDouble function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadLEFloat32AsDouble --
 *
 *      Reads a little-endian 32-bit floating-point value and returns it as a double.
 *
 * Parameters:
 *      const unsigned char *p - Pointer to four little-endian bytes.
 *
 * Results:
 *      Returns the decoded float value converted to double.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      Assumes p points to at least four readable bytes.
 *      memcpy() is used to avoid strict-aliasing and alignment issues.
 *      Conversion to double does not add precision beyond the original 32-bit value.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static double ReadLEFloat32AsDouble(const unsigned char *p) {
    uint32_t u = ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, sizeof f);
    return (double)f;
}

//***  ReadLEFloat64 function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadLEFloat64 --
 *
 *      Reads a little-endian 64-bit floating-point value and returns it as a double.
 *
 * Parameters:
 *      const unsigned char *p - Pointer to eight little-endian bytes.
 *
 * Results:
 *      Returns the decoded double value.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      Assumes p points to at least eight readable bytes.
 *      memcpy() is used to avoid strict-aliasing and alignment issues.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static double ReadLEFloat64(const unsigned char *p) {
    uint64_t u = ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
                 ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) | ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    double d;
    memcpy(&d, &u, sizeof d);
    return d;
}

//***  RawBinaryReadExactBytes function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawBinaryReadExactBytes --
 *
 *      Reads exactly nbytes bytes from chan into buf.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tcl_Channel chan   - Input channel to read from.
 *      unsigned char *buf - Destination buffer.
 *      Tcl_Size nbytes    - Exact number of bytes to read.
 *
 * Results:
 *      Returns TCL_OK if exactly nbytes bytes are read.
 *      Returns TCL_ERROR on input error or premature EOF.
 *
 * Side Effects:
 *      Advances the channel position and writes into buf.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The caller owns buf and must provide at least nbytes writable bytes.
 *      If nbytes is zero, no reads are performed and TCL_OK is returned.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawBinaryReadExactBytes(Tcl_Interp *interp, Tcl_Channel chan, unsigned char *buf, Tcl_Size nbytes) {
    Tcl_Size got = 0;
    while (got < nbytes) {
        Tcl_Size n = Tcl_Read(chan, (char *)buf + got, nbytes - got);
        if (n < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed while reading raw binary data", -1));
            return TCL_ERROR;
        }
        if (n == 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unexpected EOF while reading raw binary data", -1));
            return TCL_ERROR;
        }
        got += n;
    }
    return TCL_OK;
}

//***  RawAppendBinaryValue function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAppendBinaryValue --
 *
 *      Decodes one raw binary value and appends it to a Tcl list.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for list operations and error reporting.
 *      Tcl_Obj *listObj        - Tcl list object to append to.
 *      RawValueStorage storage - Effective binary storage format.
 *      const unsigned char *p  - Pointer to the encoded value bytes.
 *
 * Results:
 *      Returns TCL_OK if the value is decoded and appended.
 *      Returns TCL_ERROR if storage is unknown.
 *
 * Side Effects:
 *      Appends one element to listObj.
 *      Sets the interpreter result on unknown storage type.
 *
 * Notes:
 *      Real values are appended as Tcl double objects.
 *      Complex values are appended as two-element lists: {real imag}.
 *      Assumes p points to enough readable bytes for the selected storage format.
 *      p is not advanced.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAppendBinaryValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage, const unsigned char *p) {
    switch (storage) {
    case RAW_VALUE_REAL32:
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(ReadLEFloat32AsDouble(p)));
        return TCL_OK;
    case RAW_VALUE_REAL64:
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(ReadLEFloat64(p)));
        return TCL_OK;
    case RAW_VALUE_COMPLEX128: {
        Tcl_Obj *pairObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pairObj, Tcl_NewDoubleObj(ReadLEFloat64(p)));
        Tcl_ListObjAppendElement(interp, pairObj, Tcl_NewDoubleObj(ReadLEFloat64(p + 8)));
        Tcl_ListObjAppendElement(interp, listObj, pairObj);
        return TCL_OK;
    }
    default:
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw value storage type", -1));
        return TCL_ERROR;
    }
}

//** ASCII value reading/decoding
//***  RawParseAsciiDoubleToken function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawParseAsciiDoubleToken --
 *
 *      Parses one pointer/length ASCII numeric token as a double.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for conversion error reporting.
 *      const char *start  - Pointer to the first byte of the token.
 *      Tcl_Size len       - Number of bytes in the token.
 *      double *valuePtr   - Output parsed double value.
 *
 * Results:
 *      Returns TCL_OK if the token is parsed successfully; TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Creates a temporary Tcl_Obj for Tcl_GetDoubleFromObj().
 *      Writes the parsed value to *valuePtr on success.
 *      May set the interpreter result on conversion failure.
 *
 * Notes:
 *      The token does not need to be NUL-terminated.
 *      The input token is borrowed and is not modified.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawParseAsciiDoubleToken(Tcl_Interp *interp, const char *start, Tcl_Size len, double *valuePtr) {
    Tcl_Obj *obj;
    int r;
    obj = Tcl_NewStringObj(start, len);
    Tcl_IncrRefCount(obj);
    r = Tcl_GetDoubleFromObj(interp, obj, valuePtr);
    Tcl_DecrRefCount(obj);
    return r;
}

//***  RawAppendAsciiValue function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAppendAsciiValue --
 *
 *      Parses one ASCII raw-data value token and appends it to a Tcl list.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for conversion error reporting.
 *      Tcl_Obj *listObj        - Tcl list object to append to.
 *      RawValueStorage storage - Value storage/type descriptor.
 *      const char *start       - Pointer to the first byte of the token.
 *      Tcl_Size len            - Number of bytes in the token.
 *
 * Results:
 *      Returns TCL_OK if the value is parsed and appended.
 *      Returns TCL_ERROR on malformed input, conversion failure, or unknown storage type.
 *
 * Side Effects:
 *      Appends one Tcl object to listObj on success.
 *      May set the interpreter result on failure.
 *
 * Notes:
 *      Real values are appended as Tcl double objects.
 *      Complex values are appended as two-element lists: {real imag}.
 *      Complex tokens may be written as real,imag or with optional enclosing parentheses.
 *      RAW_VALUE_REAL32 and RAW_VALUE_REAL64 are parsed identically in ASCII mode.
 *      The input token is borrowed and is not modified.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAppendAsciiValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage, const char *start,
                               Tcl_Size len) {
    if (len >= 2 && start[0] == '(' && start[len - 1] == ')') {
        start++;
        len -= 2;
    }
    switch (storage) {
    case RAW_VALUE_REAL32:
    case RAW_VALUE_REAL64: {
        double value;
        if (RawParseAsciiDoubleToken(interp, start, len, &value) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewDoubleObj(value));
        return TCL_OK;
    }
    case RAW_VALUE_COMPLEX128: {
        const char *comma;
        Tcl_Size realLen;
        Tcl_Size imagLen;
        double realValue;
        double imagValue;
        Tcl_Obj *pairObj;
        comma = memchr(start, ',', (size_t)len);
        if (comma == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("malformed complex value in ASCII raw data", -1));
            return TCL_ERROR;
        }
        realLen = (Tcl_Size)(comma - start);
        imagLen = len - realLen - 1;
        if (RawParseAsciiDoubleToken(interp, start, realLen, &realValue) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawParseAsciiDoubleToken(interp, comma + 1, imagLen, &imagValue) != TCL_OK) {
            return TCL_ERROR;
        }
        pairObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, pairObj, Tcl_NewDoubleObj(realValue));
        Tcl_ListObjAppendElement(interp, pairObj, Tcl_NewDoubleObj(imagValue));
        Tcl_ListObjAppendElement(interp, listObj, pairObj);
        return TCL_OK;
    }
    default:
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw value storage type", -1));
        return TCL_ERROR;
    }
}

//***  RawAsciiReadOnePoint function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAsciiReadOnePoint --
 *
 *      Reads and parses one complete point from an ASCII Values: block.
 *
 * Parameters:
 *      Tcl_Interp *interp        - Interpreter used for error reporting.
 *      Tcl_Channel chan          - Channel positioned at the beginning of an ASCII point.
 *      EncKind kind              - Detected raw text encoding kind.
 *      Tcl_Encoding enc          - Encoding handle used to decode text lines.
 *      RawHeader *h              - Parsed header describing variables and storage types.
 *      Tcl_Size selectedVarIndex - Variable index used with selectedListObj.
 *      Tcl_Obj *selectedListObj  - Optional list receiving one selected variable value.
 *      Tcl_Obj **vecObjs         - Optional array of per-variable output lists.
 *
 * Results:
 *      Returns TCL_OK if one complete point is read and parsed.
 *      Returns TCL_ERROR on read, decode, parse, or premature EOF error.
 *
 * Side Effects:
 *      Advances chan past one ASCII point.
 *      Appends parsed values to selectedListObj and/or vecObjs when provided.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The first token of each point is skipped as the point index.
 *      Values are matched to variables by order in the Values: block.
 *      A point may span multiple decoded lines.
 *      Output lists are owned and managed by the caller.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAsciiReadOnePoint(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
                                Tcl_Size selectedVarIndex, Tcl_Obj *selectedListObj, Tcl_Obj **vecObjs) {
    Tcl_Size valuesSeen = 0;
    int sawPointIndex = 0;
    while (valuesSeen < h->numVariables) {
        Tcl_DString lineDs;
        const char *line;
        const char *p;
        const char *tokStart;
        Tcl_Size tokLen;
        int r;
        r = ReadDecodedHeaderLine(interp, chan, kind, enc, &lineDs);
        if (r == RAW_HEADER_EOF) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unexpected EOF inside ASCII Values block", -1));
            return TCL_ERROR;
        }
        if (r == RAW_HEADER_ERROR) {
            return TCL_ERROR;
        }
        line = Tcl_DStringValue(&lineDs);
        p = line;
        while (NextToken(&p, &tokStart, &tokLen)) {
            RawVariable *var;
            /*
             * First token of a point is the point index.
             */
            if (!sawPointIndex) {
                sawPointIndex = 1;
                continue;
            }
            if (valuesSeen >= h->numVariables) {
                break;
            }
            var = &h->variables[valuesSeen];
            /*
             * Xyce writes complex ASCII values as "real, imag", which appears as two
             * whitespace tokens. Treat "real," plus the following token as one value.
             */
            if (var->storage == RAW_VALUE_COMPLEX128) {
                const char *comma = memchr(tokStart, ',', (size_t)tokLen);

                if (comma != NULL && (Tcl_Size)(comma - tokStart) + 1 == tokLen) {
                    const char *imagStart;
                    Tcl_Size imagLen;
                    Tcl_DString ds;
                    const char *combined;
                    Tcl_Size combinedLen;

                    if (!NextToken(&p, &imagStart, &imagLen)) {
                        Tcl_DStringFree(&lineDs);
                        Tcl_SetObjResult(interp,
                                         Tcl_NewStringObj("malformed split complex value in ASCII raw data", -1));
                        return TCL_ERROR;
                    }
                    Tcl_DStringInit(&ds);
                    Tcl_DStringAppend(&ds, tokStart, tokLen);
                    Tcl_DStringAppend(&ds, imagStart, imagLen);
                    combined = Tcl_DStringValue(&ds);
                    combinedLen = (Tcl_Size)Tcl_DStringLength(&ds);
                    if (selectedListObj && valuesSeen == selectedVarIndex) {
                        if (RawAppendAsciiValue(interp, selectedListObj, var->storage, combined, combinedLen) !=
                            TCL_OK) {
                            Tcl_DStringFree(&ds);
                            Tcl_DStringFree(&lineDs);
                            return TCL_ERROR;
                         }
                    }
                    if (vecObjs && vecObjs[valuesSeen]) {
                        if (RawAppendAsciiValue(interp, vecObjs[valuesSeen], var->storage, combined, combinedLen) !=
                            TCL_OK) {
                            Tcl_DStringFree(&ds);
                            Tcl_DStringFree(&lineDs);
                            return TCL_ERROR;
                        }
                    }
                    Tcl_DStringFree(&ds);
                    valuesSeen++;
                    continue;
                }
            }
            if (selectedListObj && valuesSeen == selectedVarIndex) {
                if (RawAppendAsciiValue(interp, selectedListObj, var->storage, tokStart, tokLen) != TCL_OK) {
                    Tcl_DStringFree(&lineDs);
                    return TCL_ERROR;
                }
            }
            if (vecObjs && vecObjs[valuesSeen]) {
                if (RawAppendAsciiValue(interp, vecObjs[valuesSeen], var->storage, tokStart, tokLen) != TCL_OK) {
                    Tcl_DStringFree(&lineDs);
                    return TCL_ERROR;
                }
            }
            valuesSeen++;
        }
        Tcl_DStringFree(&lineDs);
    }
    return TCL_OK;
}

//***  RawPlotScanAsciiValues function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotScanAsciiValues --
 *
 *      Scans an ASCII Values: block and records the file offset of each point for lazy access.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tcl_Channel chan   - Channel positioned at the first ASCII point.
 *      EncKind kind       - Detected raw text encoding kind.
 *      Tcl_Encoding enc   - Encoding handle used to decode text lines.
 *      RawPlot *plot      - Plot whose ASCII indexing fields are filled.
 *
 * Results:
 *      Returns TCL_OK if the Values: block is scanned successfully.
 *      Returns TCL_ERROR on missing or invalid dimensions, offset/allocation overflow, seek-position failure, or parse
 *      error while skipping points.
 *
 * Side Effects:
 *      Allocates and fills plot->pointOffsets for non-empty plots.
 *      Advances chan past the ASCII Values: block.
 *      Updates plot->numPointOffsets, plot->nextOffset, and plot->dataBytes.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Numeric values are parsed only to validate and skip points; they are not stored.
 *      Zero-point plots allocate no point-offset array and have dataBytes set to zero.
 *      Later reads use pointOffsets to seek directly to the requested point range.
 *      On error after allocation, the caller should clean up with RawPlotFree().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotScanAsciiValues(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawPlot *plot) {
    RawHeader *h = &plot->header;
    Tcl_WideInt endOffset;
    if (!h->haveNumVariables || !h->haveNumPoints) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot scan ASCII Values block without variables and points", -1));
        return TCL_ERROR;
    }
    if (h->numVariables < 0 || h->numPoints < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid ASCII raw data dimensions", -1));
        return TCL_ERROR;
    }
    if ((size_t)h->numPoints > SIZE_MAX / sizeof(Tcl_WideInt)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII point offset array overflow", -1));
        return TCL_ERROR;
    }
    if (h->numPoints == 0) {
        plot->pointOffsets = NULL;
        plot->numPointOffsets = 0;
        plot->nextOffset = Tcl_Tell(chan);
        if (plot->nextOffset < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get ASCII raw data offset", -1));
            return TCL_ERROR;
        }
        plot->dataBytes = 0;
        return TCL_OK;
    }
    plot->pointOffsets = (Tcl_WideInt *)Tcl_Alloc(sizeof(Tcl_WideInt) * (size_t)h->numPoints);
    plot->numPointOffsets = h->numPoints;
    for (Tcl_Size point = 0; point < h->numPoints; point++) {
        Tcl_WideInt pointOffset;
        pointOffset = Tcl_Tell(chan);
        if (pointOffset < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get ASCII point offset", -1));
            return TCL_ERROR;
        }
        plot->pointOffsets[point] = pointOffset;
        if (RawAsciiReadOnePoint(interp, chan, kind, enc, h, -1, NULL, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    endOffset = Tcl_Tell(chan);
    if (endOffset < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get end of ASCII raw data block", -1));
        return TCL_ERROR;
    }
    plot->nextOffset = endOffset;
    if (endOffset < plot->dataOffset) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid ASCII raw data offsets", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideInt)(Tcl_Size)(endOffset - plot->dataOffset) != endOffset - plot->dataOffset) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII raw data byte count overflow", -1));
        return TCL_ERROR;
    }
    plot->dataBytes = (Tcl_Size)(endOffset - plot->dataOffset);
    return TCL_OK;
}

//** Variable selection
//***  RawPlotFindVariable function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotFindVariable --
 *
 *      Finds a variable by name in a RawPlot.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawPlot *plot      - Plot whose variable table is searched.
 *      const char *name   - NUL-terminated variable name to find.
 *      Tcl_Size *indexPtr - Output matching variable array index.
 *
 * Results:
 *      Returns TCL_OK if a matching variable is found; TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Writes the matching index to *indexPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Comparison is byte-wise and case-sensitive.
 *      The returned index is the array position in h->variables[].
 *      If duplicate names exist, the first match is returned.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotFindVariable(Tcl_Interp *interp, RawPlot *plot, const char *name, Tcl_Size *indexPtr) {
    RawHeader *h = &plot->header;
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        const char *varName = h->variables[i].name;
        if (varName && strcmp(varName, name) == 0) {
            *indexPtr = i;
            return TCL_OK;
        }
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("raw vector \"%s\" not found", name));
    return TCL_ERROR;
}

//***  RawPlotResolveVariableList function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotResolveVariableList --
 *
 *      Resolves a Tcl list of raw vector names to an owned array of variable indexes for a plot.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for list parsing and error reporting.
 *      RawPlot *plot            - Plot whose variable table is searched.
 *      Tcl_Obj *namesObj        - Tcl list object containing vector names to resolve.
 *      Tcl_Size *numVarsPtr     - Output location for the number of resolved variables.
 *      Tcl_Size **varIndexesPtr - Output location for the allocated array of resolved variable indexes.
 *
 * Results:
 *      Returns TCL_OK if namesObj is a valid list and every listed vector name is resolved successfully.
 *      Returns TCL_ERROR if namesObj is not a valid list, an index array allocation would overflow, a vector name is
 *      not found, or the same variable is requested more than once.
 *
 * Side Effects:
 *      Allocates a Tcl-managed array of Tcl_Size indexes and stores it in *varIndexesPtr.
 *      Stores the number of resolved variables in *numVarsPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The returned index array is owned by the caller and must be released with Tcl_Free().
 *      An empty name list is accepted. In that case, *numVarsPtr is set to 0 and *varIndexesPtr is set to NULL.
 *      Name lookup is delegated to RawPlotFindVariable(), so matching follows the same exact-name rules as the
 *      single-vector command.
 *      Duplicate variables are rejected because dictionary output cannot usefully represent the same variable key
 *      more than once.
 *      The returned indexes are physical indexes into plot->header.variables, not necessarily the numeric indexes
 *      written in the raw-file Variables section.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotResolveVariableList(Tcl_Interp *interp, RawPlot *plot, Tcl_Obj *namesObj, Tcl_Size *numVarsPtr,
                                      Tcl_Size **varIndexesPtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;
    Tcl_Size *indexes = NULL;
    if (Tcl_ListObjGetElements(interp, namesObj, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 0) {
        *numVarsPtr = 0;
        *varIndexesPtr = NULL;
        return TCL_OK;
    }
    if ((size_t)objc > SIZE_MAX / sizeof(Tcl_Size)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index array overflow", -1));
        return TCL_ERROR;
    }
    indexes = (Tcl_Size *)Tcl_Alloc(sizeof(Tcl_Size) * (size_t)objc);
    for (Tcl_Size i = 0; i < objc; i++) {
        const char *name = Tcl_GetString(objv[i]);
        Tcl_Size index;
        if (RawPlotFindVariable(interp, plot, name, &index) != TCL_OK) {
            Tcl_Free((char *)indexes);
            return TCL_ERROR;
        }
        /*
         * Dict output cannot represent duplicate requested keys usefully.
         */
        for (Tcl_Size j = 0; j < i; j++) {
            if (indexes[j] == index) {
                Tcl_Free((char *)indexes);
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("duplicate raw vector \"%s\" requested", name));
                return TCL_ERROR;
            }
        }
        indexes[i] = index;
    }
    *numVarsPtr = objc;
    *varIndexesPtr = indexes;
    return TCL_OK;
}

//***  RawPlotResolveAllVariables function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotResolveAllVariables --
 *
 *      Builds an owned index array selecting every variable in a plot.
 *
 *      This helper is used by full-vector reads, such as the vectors command with -all, to reuse the same selected
 *      vector reading path that is used for an explicit list of vector names.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *      RawPlot *plot            - Plot whose complete variable table is selected.
 *      Tcl_Size *numVarsPtr     - Output location for the number of selected variables.
 *      Tcl_Size **varIndexesPtr - Output location for the allocated array of selected variable indexes.
 *
 * Results:
 *      Returns TCL_OK if the index array is built successfully.
 *      Returns TCL_ERROR if the variable count is invalid or the index array allocation would overflow.
 *
 * Side Effects:
 *      Allocates a Tcl-managed array of Tcl_Size indexes and stores it in *varIndexesPtr.
 *      Stores the number of selected variables in *numVarsPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The returned index array is owned by the caller and must be released with Tcl_Free().
 *      If the plot has no variables, *numVarsPtr is set to 0 and *varIndexesPtr is set to NULL.
 *      The generated indexes are physical indexes into plot->header.variables, in the same order as values appear
 *      in the raw-file data block.
 *      This function does not inspect variable names and therefore cannot produce duplicate dictionary keys unless
 *      the raw file itself contains duplicate variable names.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotResolveAllVariables(Tcl_Interp *interp, RawPlot *plot, Tcl_Size *numVarsPtr,
                                      Tcl_Size **varIndexesPtr) {
    RawHeader *h = &plot->header;
    Tcl_Size *indexes = NULL;
    if (h->numVariables < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid raw variable count", -1));
        return TCL_ERROR;
    }
    if (h->numVariables == 0) {
        *numVarsPtr = 0;
        *varIndexesPtr = NULL;
        return TCL_OK;
    }
    if ((size_t)h->numVariables > SIZE_MAX / sizeof(Tcl_Size)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index array overflow", -1));
        return TCL_ERROR;
    }
    indexes = (Tcl_Size *)Tcl_Alloc(sizeof(Tcl_Size) * (size_t)h->numVariables);
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        indexes[i] = i;
    }
    *numVarsPtr = h->numVariables;
    *varIndexesPtr = indexes;
    return TCL_OK;
}

//** Generic vector extraction backends
//***  RawPlotBinaryReadVectorsToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotBinaryReadVectorsToObj --
 *
 *      Reads one or more selected vectors from a Binary: raw-data block and builds the requested Tcl result object.
 *
 *      The function reads the requested point range in binary chunks. Each chunk contains complete raw points. For each
 *      point, only the selected variable offsets are decoded and appended to their corresponding Tcl value lists.
 *
 * Parameters:
 *      Tcl_Interp *interp              - Interpreter used for error reporting and Tcl object operations.
 *      RawFile *rf                     - Open raw-file handle containing the channel to read from.
 *      RawPlot *plot                   - Binary plot to read.
 *      Tcl_Size numVars                - Number of selected variables.
 *      Tcl_Size *varIndexes            - Array of physical variable indexes into plot->header.variables.
 *      Tcl_Size firstPoint             - First point index to read.
 *      Tcl_Size count                  - Number of points to read.
 *      RawVectorResultMode resultMode  - Requested result shape.
 *      Tcl_Obj **objPtr                - Output location for the resulting Tcl object.
 *
 * Results:
 *      Returns TCL_OK if the requested vectors are read, decoded, and assembled successfully.
 *      Returns TCL_ERROR if the plot is not binary, the range is invalid, a selected variable index is out of range,
 *      the result mode is incompatible with the number of selected variables, a seek/read fails, or binary value
 *      decoding fails.
 *
 * Side Effects:
 *      Seeks and reads from rf->chan.
 *      Allocates a temporary binary chunk buffer.
 *      Allocates one Tcl list object per selected variable.
 *      Appends decoded values to the selected vector lists.
 *      Stores the final Tcl result object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      RAW_VECTOR_RESULT_LIST requires exactly one selected variable and returns that vector list directly.
 *      RAW_VECTOR_RESULT_DICT returns a dictionary mapping selected variable names to their value lists.
 *      A zero-variable selection is accepted only for dictionary result mode, where it returns an empty dictionary.
 *      Binary data is read in chunks of up to approximately one megabyte, rounded down to a whole number of points.
 *      Very large point strides are handled by reading at least one point per chunk.
 *      The function reads complete point records even when only a subset of variables is requested, because binary
 *      raw data is stored point-major.
 *      Value decoding is delegated to RawAppendBinaryValue().
 *      Final result construction and ownership transfer are delegated to RawBuildVectorResult().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotBinaryReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                         Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                         RawVectorResultMode resultMode, Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    Tcl_Obj **vecObjs = NULL;
    unsigned char *buf = NULL;
    Tcl_Size maxChunkBytes = 1024 * 1024;
    Tcl_Size chunkPoints;
    Tcl_Size done = 0;
    if (plot->dataKind != DATA_BINARY) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Binary: plot", -1));
        return TCL_ERROR;
    }
    if (resultMode == RAW_VECTOR_RESULT_LIST && numVars != 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector list result requires exactly one selected vector", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector range out of range", -1));
        return TCL_ERROR;
    }
    if (h->pointStrideBytes <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid raw point stride", -1));
        return TCL_ERROR;
    }
    if (numVars == 0) {
        if (resultMode == RAW_VECTOR_RESULT_LIST) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector list result requires one selected vector", -1));
            return TCL_ERROR;
        }
        *objPtr = Tcl_NewDictObj();
        return TCL_OK;
    }
    if ((size_t)numVars > SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector object array overflow", -1));
        return TCL_ERROR;
    }
    vecObjs = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *) * (size_t)numVars);
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (varIndexes[i] < 0 || varIndexes[i] >= h->numVariables) {
            Tcl_Free((char *)vecObjs);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index out of range", -1));
            return TCL_ERROR;
        }
        vecObjs[i] = Tcl_NewListObj(0, NULL);
    }
    if (count > 0) {
        chunkPoints = maxChunkBytes / h->pointStrideBytes;
        if (chunkPoints < 1) {
            chunkPoints = 1;
        }
        if (chunkPoints > count) {
            chunkPoints = count;
        }
        if (chunkPoints > TCL_SIZE_MAX / h->pointStrideBytes) {
            RawFreeVectorObjects(vecObjs, numVars);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw chunk size overflow", -1));
            return TCL_ERROR;
        }
        buf = (unsigned char *)Tcl_Alloc((size_t)(chunkPoints * h->pointStrideBytes));
        while (done < count) {
            Tcl_Size thisPoints = count - done;
            Tcl_Size thisBytes;
            Tcl_WideInt offset;
            if (thisPoints > chunkPoints) {
                thisPoints = chunkPoints;
            }
            thisBytes = thisPoints * h->pointStrideBytes;
            offset = plot->dataOffset + (Tcl_WideInt)(firstPoint + done) * (Tcl_WideInt)h->pointStrideBytes;
            if (Tcl_Seek(rf->chan, offset, SEEK_SET) < 0) {
                Tcl_Free((char *)buf);
                RawFreeVectorObjects(vecObjs, numVars);
                Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek inside raw binary data", -1));
                return TCL_ERROR;
            }
            if (RawBinaryReadExactBytes(interp, rf->chan, buf, thisBytes) != TCL_OK) {
                Tcl_Free((char *)buf);
                RawFreeVectorObjects(vecObjs, numVars);
                return TCL_ERROR;
            }
            for (Tcl_Size point = 0; point < thisPoints; point++) {
                const unsigned char *pointPtr = buf + point * h->pointStrideBytes;
                for (Tcl_Size selected = 0; selected < numVars; selected++) {
                    RawVariable *var = &h->variables[varIndexes[selected]];
                    const unsigned char *p = pointPtr + var->offsetBytes;
                    if (RawAppendBinaryValue(interp, vecObjs[selected], var->storage, p) != TCL_OK) {
                        Tcl_Free((char *)buf);
                        RawFreeVectorObjects(vecObjs, numVars);
                        return TCL_ERROR;
                    }
                }
            }
            done += thisPoints;
        }
        Tcl_Free((char *)buf);
    }
    return RawBuildVectorResult(interp, h, numVars, varIndexes, vecObjs, resultMode, objPtr);
}

//***  RawPlotAsciiReadVectorsToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotAsciiReadVectorsToObj --
 *
 *      Reads one or more selected vectors from an ASCII Values: raw-data block and builds the requested Tcl result
 *      object.
 *
 *      The function seeks to the first requested point using the plot's ASCII point-offset index, then parses each
 *      requested point once. A sparse per-variable output array is used so RawAsciiReadOnePoint() scans the complete
 *      point but appends values only for the selected variables.
 *
 * Parameters:
 *      Tcl_Interp *interp              - Interpreter used for error reporting and Tcl object operations.
 *      RawFile *rf                     - Open raw-file handle containing the channel and text decoding state.
 *      RawPlot *plot                   - ASCII Values: plot to read.
 *      Tcl_Size numVars                - Number of selected variables.
 *      Tcl_Size *varIndexes            - Array of physical variable indexes into plot->header.variables.
 *      Tcl_Size firstPoint             - First point index to read.
 *      Tcl_Size count                  - Number of points to read.
 *      RawVectorResultMode resultMode  - Requested result shape.
 *      Tcl_Obj **objPtr                - Output location for the resulting Tcl object.
 *
 * Results:
 *      Returns TCL_OK if the requested vectors are read, parsed, and assembled successfully.
 *      Returns TCL_ERROR if the plot is not an ASCII Values: plot, the point-offset index is missing, the range is
 *      invalid, a selected variable index is out of range, the result mode is incompatible with the number of selected
 *      variables, a seek/read/decode fails, or ASCII value parsing fails.
 *
 * Side Effects:
 *      Seeks and reads from rf->chan.
 *      Allocates one Tcl list object per selected variable.
 *      Allocates a temporary sparse array indexed by all plot variables when count is non-zero.
 *      Appends parsed values to the selected vector lists.
 *      Stores the final Tcl result object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      RAW_VECTOR_RESULT_LIST requires exactly one selected variable and returns that vector list directly.
 *      RAW_VECTOR_RESULT_DICT returns a dictionary mapping selected variable names to their value lists.
 *      A zero-variable selection is accepted only for dictionary result mode, where it returns an empty dictionary.
 *      allVecObjs is indexed by physical variable order. Entries for unselected variables are NULL, so the ASCII
 *      point parser still consumes every value in the point but appends only selected values.
 *      ASCII point parsing, including multi-line points and split Xyce-style complex values, is delegated to
 *      RawAsciiReadOnePoint().
 *      Final result construction and ownership transfer are delegated to RawBuildVectorResult().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotAsciiReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                        Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                        RawVectorResultMode resultMode, Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    Tcl_Obj **selectedVecObjs = NULL;
    Tcl_Obj **allVecObjs = NULL;
    if (plot->dataKind != DATA_VALUES) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Values: plot", -1));
        return TCL_ERROR;
    }
    if (resultMode == RAW_VECTOR_RESULT_LIST && numVars != 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector list result requires exactly one selected vector", -1));
        return TCL_ERROR;
    }
    if (plot->pointOffsets == NULL && h->numPoints > 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII plot has no point offset index", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector range out of range", -1));
        return TCL_ERROR;
    }
    if (numVars == 0) {
        if (resultMode == RAW_VECTOR_RESULT_LIST) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector list result requires one selected vector", -1));
            return TCL_ERROR;
        }
        *objPtr = Tcl_NewDictObj();
        return TCL_OK;
    }
    if ((size_t)numVars > SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector object array overflow", -1));
        return TCL_ERROR;
    }
    if ((size_t)h->numVariables > SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw full vector object array overflow", -1));
        return TCL_ERROR;
    }
    selectedVecObjs = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *) * (size_t)numVars);
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (varIndexes[i] < 0 || varIndexes[i] >= h->numVariables) {
            Tcl_Free((char *)selectedVecObjs);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index out of range", -1));
            return TCL_ERROR;
        }
        selectedVecObjs[i] = Tcl_NewListObj(0, NULL);
    }
    if (count > 0) {
        allVecObjs = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *) * (size_t)h->numVariables);
        for (Tcl_Size i = 0; i < h->numVariables; i++) {
            allVecObjs[i] = NULL;
        }
        for (Tcl_Size i = 0; i < numVars; i++) {
            allVecObjs[varIndexes[i]] = selectedVecObjs[i];
        }
        if (Tcl_Seek(rf->chan, plot->pointOffsets[firstPoint], SEEK_SET) < 0) {
            Tcl_Free((char *)allVecObjs);
            RawFreeVectorObjects(selectedVecObjs, numVars);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek to ASCII raw point", -1));
            return TCL_ERROR;
        }
        for (Tcl_Size point = 0; point < count; point++) {
            if (RawAsciiReadOnePoint(interp, rf->chan, rf->encKind, rf->enc, h, -1, NULL, allVecObjs) != TCL_OK) {
                Tcl_Free((char *)allVecObjs);
                RawFreeVectorObjects(selectedVecObjs, numVars);
                return TCL_ERROR;
            }
        }
        Tcl_Free((char *)allVecObjs);
    }
    return RawBuildVectorResult(interp, h, numVars, varIndexes, selectedVecObjs, resultMode, objPtr);
}

//***  RawPlotReadVectorsToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotReadVectorsToObj --
 *
 *      Dispatches a selected-vector read request to the data-block-specific reader for a plot.
 *
 *      This is the format-independent entry point used by vector, vectors, and full-dictionary reads after the caller
 *      has resolved the requested variables and parsed the requested point range.
 *
 * Parameters:
 *      Tcl_Interp *interp              - Interpreter used for error reporting.
 *      RawFile *rf                     - Open raw-file handle containing the channel and decoding state.
 *      RawPlot *plot                   - Plot to read from.
 *      Tcl_Size numVars                - Number of selected variables.
 *      Tcl_Size *varIndexes            - Array of physical variable indexes into plot->header.variables.
 *      Tcl_Size firstPoint             - First point index to read.
 *      Tcl_Size count                  - Number of points to read.
 *      RawVectorResultMode resultMode  - Requested result shape.
 *      Tcl_Obj **objPtr                - Output location for the resulting Tcl object.
 *
 * Results:
 *      Returns TCL_OK if the request is handled successfully by the appropriate backend.
 *      Returns TCL_ERROR if the plot data kind is unknown or if the selected backend reports an error.
 *
 * Side Effects:
 *      May seek and read from rf->chan through the selected backend.
 *      Stores the final Tcl result object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      DATA_BINARY plots are handled by RawPlotBinaryReadVectorsToObj().
 *      DATA_VALUES plots are handled by RawPlotAsciiReadVectorsToObj().
 *      The function does not validate varIndexes, firstPoint, count, or resultMode itself; detailed validation is
 *      delegated to the selected backend.
 *      The result shape is controlled by resultMode. Single-vector reads use RAW_VECTOR_RESULT_LIST, while selected
 *      vector and full-dictionary reads use RAW_VECTOR_RESULT_DICT.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotReadVectorsToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size numVars,
                                   Tcl_Size *varIndexes, Tcl_Size firstPoint, Tcl_Size count,
                                   RawVectorResultMode resultMode, Tcl_Obj **objPtr) {
    if (plot->dataKind == DATA_BINARY) {
        return RawPlotBinaryReadVectorsToObj(interp, rf, plot, numVars, varIndexes, firstPoint, count, resultMode,
                                             objPtr);
    }
    if (plot->dataKind == DATA_VALUES) {
        return RawPlotAsciiReadVectorsToObj(interp, rf, plot, numVars, varIndexes, firstPoint, count, resultMode,
                                            objPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw plot data kind", -1));
    return TCL_ERROR;
}

//***  RawPlotVectorToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotVectorToObj --
 *
 *      Reads one selected vector from a raw plot and returns it as a Tcl list object.
 *
 *      This is a compatibility wrapper for the single-vector command. The actual binary/ASCII reading and decoding is
 *      performed by the unified selected-vector reader.
 *
 * Parameters:
 *      Tcl_Interp *interp   - Interpreter used for error reporting.
 *      RawFile *rf          - Open raw-file handle containing the channel and decoding state.
 *      RawPlot *plot        - Plot to read from.
 *      Tcl_Size varIndex    - Physical variable index into plot->header.variables.
 *      Tcl_Size firstPoint  - First point index to read.
 *      Tcl_Size count       - Number of points to read.
 *      Tcl_Obj **objPtr     - Output location for the resulting Tcl list object.
 *
 * Results:
 *      Returns TCL_OK if the vector is read and stored in *objPtr successfully.
 *      Returns TCL_ERROR if the unified selected-vector reader reports an error.
 *
 * Side Effects:
 *      May seek and read from rf->chan through RawPlotReadVectorsToObj().
 *      Stores the resulting Tcl list object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The returned result shape is always a plain Tcl list of values.
 *      This function passes a one-element selection to RawPlotReadVectorsToObj() using RAW_VECTOR_RESULT_LIST.
 *      The variable index is a physical index into plot->header.variables, not necessarily the numeric index written
 *      in the raw-file Variables section.
 *      This wrapper preserves the old vector-command behaviour while sharing the same backend used by vectors and
 *      vectors -all.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr) {
    return RawPlotReadVectorsToObj(interp, rf, plot, 1, &varIndex, firstPoint, count, RAW_VECTOR_RESULT_LIST, objPtr);
}

//** LTspice binary dialect support
//***  RawGetChannelSize function
static int RawGetChannelSize(Tcl_Interp *interp, Tcl_Channel chan, Tcl_WideInt *sizePtr) {
    Tcl_WideInt oldPos;
    Tcl_WideInt endPos;
    oldPos = Tcl_Tell(chan);
    if (oldPos < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get current raw-file offset", -1));
        return TCL_ERROR;
    }
    if (Tcl_Seek(chan, 0, SEEK_END) < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek to end of raw file", -1));
        return TCL_ERROR;
    }
    endPos = Tcl_Tell(chan);
    if (endPos < 0) {
        Tcl_Seek(chan, oldPos, SEEK_SET);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get raw-file size", -1));
        return TCL_ERROR;
    }
    if (Tcl_Seek(chan, oldPos, SEEK_SET) < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to restore raw-file offset", -1));
        return TCL_ERROR;
    }
    *sizePtr = endPos;
    return TCL_OK;
}

//***  RawGetChannelSize function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawGetChannelSize --
 *
 *      Gets the total byte size of an open raw-file channel.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      Tcl_Channel chan        - Channel whose size is queried.
 *      Tcl_WideInt *sizePtr    - Output location for the channel size.
 *
 * Results:
 *      Returns TCL_OK if the channel size is obtained successfully.
 *      Returns TCL_ERROR if the current offset, end offset, or seek operation fails.
 *
 * Side Effects:
 *      Temporarily seeks chan to the end of the file.
 *      Restores the original channel position on success.
 *      Stores the file size in *sizePtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The returned size is a byte offset suitable for binary raw-file calculations.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceCandidateStrides(Tcl_Interp *interp, RawHeader *h, Tcl_Size *mixedStridePtr,
                                      Tcl_Size *doubleStridePtr) {
    Tcl_Size mixedStride;
    Tcl_Size doubleStride;
    if (h->numVariables <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice raw file has no variables", -1));
        return TCL_ERROR;
    }
    if (h->flagsMask & RAW_FLAG_COMPLEX) {
        if (RawMulSize(interp, h->numVariables, 16, "LTspice complex point stride overflow", doubleStridePtr) !=
            TCL_OK) {
            return TCL_ERROR;
        }
        *mixedStridePtr = *doubleStridePtr;
        return TCL_OK;
    }
    if (h->numVariables == 1) {
        mixedStride = 8;
    } else {
        Tcl_Size tailBytes;
        if (RawMulSize(interp, h->numVariables - 1, 4, "LTspice mixed point stride overflow", &tailBytes) != TCL_OK) {
            return TCL_ERROR;
        }
        if (tailBytes > TCL_SIZE_MAX - 8) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice mixed point stride overflow", -1));
            return TCL_ERROR;
        }
        mixedStride = 8 + tailBytes;
    }
    if (RawMulSize(interp, h->numVariables, 8, "LTspice double point stride overflow", &doubleStride) != TCL_OK) {
        return TCL_ERROR;
    }
    *mixedStridePtr = mixedStride;
    *doubleStridePtr = doubleStride;
    return TCL_OK;
}

//***  RawLtspiceDetectAllDouble function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceDetectAllDouble --
 *
 *      Determines whether LTspice real binary data uses mixed double/float storage or all-double storage.
 *
 * Parameters:
 *      Tcl_Interp *interp          - Interpreter used for error reporting.
 *      RawHeader *h                - Parsed header used to compute candidate point strides.
 *      Tcl_Size physicalDataBytes  - Physical byte size of the binary data block.
 *      int *allDoublePtr           - Output flag; non-zero means all real values are stored as doubles.
 *
 * Results:
 *      Returns TCL_OK if the storage mode is inferred successfully.
 *      Returns TCL_ERROR if the data size does not match any supported LTspice layout.
 *
 * Side Effects:
 *      Stores the inferred storage mode in *allDoublePtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Complex LTspice data is always treated as double-based complex storage.
 *      For stepped files, No. Points may describe one step rather than the whole physical data block.
 *      Mixed real storage is preferred as the fallback because it is LTspice's default binary layout.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceDetectAllDouble(Tcl_Interp *interp, RawHeader *h, Tcl_Size physicalDataBytes, int *allDoublePtr) {
    Tcl_Size mixedStride;
    Tcl_Size doubleStride;
    Tcl_Size mixedBytes;
    Tcl_Size doubleBytes;
    if (h->flagsMask & RAW_FLAG_COMPLEX) {
        *allDoublePtr = 1;
        return TCL_OK;
    }
    if (RawLtspiceCandidateStrides(interp, h, &mixedStride, &doubleStride) != TCL_OK) {
        return TCL_ERROR;
    }
    if (h->haveNumPoints && h->numPoints > 0) {
        if (RawMulSize(interp, h->numPoints, mixedStride, "LTspice mixed data byte count overflow", &mixedBytes) !=
            TCL_OK) {
            return TCL_ERROR;
        }
        if (RawMulSize(interp, h->numPoints, doubleStride, "LTspice double data byte count overflow", &doubleBytes) !=
            TCL_OK) {
            return TCL_ERROR;
        }
        if (physicalDataBytes == mixedBytes) {
            *allDoublePtr = 0;
            return TCL_OK;
        }
        if (physicalDataBytes == doubleBytes) {
            *allDoublePtr = 1;
            return TCL_OK;
        }
        /*
         * Some LTspice stepped real/sweep files use No. Points as the per-step count.
         * In that case the physical data size is an integer multiple of one step.
         */
        if ((h->flagsMask & RAW_FLAG_STEPPED) && mixedBytes > 0 && physicalDataBytes % mixedBytes == 0) {
            *allDoublePtr = 0;
            return TCL_OK;
        }
        if ((h->flagsMask & RAW_FLAG_STEPPED) && doubleBytes > 0 && physicalDataBytes % doubleBytes == 0) {
            *allDoublePtr = 1;
            return TCL_OK;
        }
    }
    /*
     * Last-resort inference. Prefer mixed because it is LTspice's default real-data binary layout.
     */
    if (mixedStride > 0 && physicalDataBytes % mixedStride == 0) {
        *allDoublePtr = 0;
        return TCL_OK;
    }
    if (doubleStride > 0 && physicalDataBytes % doubleStride == 0) {
        *allDoublePtr = 1;
        return TCL_OK;
    }
    Tcl_SetObjResult(interp,
                     Tcl_NewStringObj("LTspice binary data size does not match known mixed or double layout", -1));
    return TCL_ERROR;
}

//***  RawLtspicePrepareBinaryPlot function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspicePrepareBinaryPlot --
 *
 *      Resolves LTspice binary layout and physical point count for a plot.
 *
 * Parameters:
 *      Tcl_Interp *interp            - Interpreter used for error reporting.
 *      Tcl_Channel chan              - Raw-file channel used to determine physical data size.
 *      RawPlot *plot                 - Plot whose header and data offsets are updated.
 *      Tcl_Size *declaredPointsPtr   - Output location for the original No. Points value.
 *
 * Results:
 *      Returns TCL_OK if the LTspice binary plot is prepared successfully.
 *      Returns TCL_ERROR if FastAccess is used, offsets are invalid, layout cannot be inferred, or data size is invalid.
 *
 * Side Effects:
 *      Resolves LTspice variable layout in plot->header.
 *      Updates plot->header.numPoints to the physical point count.
 *      Updates plot->dataBytes and plot->nextOffset.
 *      Stores the declared point count in *declaredPointsPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Stepped LTspice files may use No. Points as a per-step count.
 *      The base plot keeps the full physical point count before optional splitting into pseudo-plots.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspicePrepareBinaryPlot(Tcl_Interp *interp, Tcl_Channel chan, RawPlot *plot,
                                       Tcl_Size *declaredPointsPtr) {
    RawHeader *h = &plot->header;
    Tcl_WideInt fileSize;
    Tcl_WideInt physicalWide;
    Tcl_Size physicalDataBytes;
    Tcl_Size physicalPoints;
    int allDouble;
    if (h->flagsMask & RAW_FLAG_FASTACCESS) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice FastAccess raw files are not supported yet", -1));
        return TCL_ERROR;
    }
    if (RawGetChannelSize(interp, chan, &fileSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (fileSize < plot->dataOffset) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid LTspice raw data offset", -1));
        return TCL_ERROR;
    }
    physicalWide = fileSize - plot->dataOffset;
    if ((Tcl_WideInt)(Tcl_Size)physicalWide != physicalWide) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice raw data byte count overflow", -1));
        return TCL_ERROR;
    }
    physicalDataBytes = (Tcl_Size)physicalWide;
    if (RawLtspiceDetectAllDouble(interp, h, physicalDataBytes, &allDouble) != TCL_OK) {
        return TCL_ERROR;
    }
    if (RawHeaderResolveVariableLayout(interp, h, RAW_DIALECT_LTSPICE, allDouble) != TCL_OK) {
        return TCL_ERROR;
    }
    if (h->pointStrideBytes <= 0 || physicalDataBytes % h->pointStrideBytes != 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice binary data is not an integer number of points", -1));
        return TCL_ERROR;
    }
    physicalPoints = physicalDataBytes / h->pointStrideBytes;
    *declaredPointsPtr = h->haveNumPoints ? h->numPoints : physicalPoints;
    /*
     * For LTspice, use the physical point count in the base plot. Stepped files may later
     * be split into pseudo-plots with smaller per-step point counts.
     */
    h->numPoints = physicalPoints;
    h->haveNumPoints = 1;
    plot->dataBytes = physicalDataBytes;
    plot->nextOffset = fileSize;
    return TCL_OK;
}

//***  RawDecodeBinaryAxisValue function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawDecodeBinaryAxisValue --
 *
 *      Decodes the first variable value from one binary point as a scalar axis value.
 *
 * Parameters:
 *      const unsigned char *pointPtr - Pointer to the beginning of one binary point.
 *      RawHeader *h                  - Header containing resolved variable layout.
 *
 * Results:
 *      Returns the decoded axis value.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      The axis variable is h->variables[0].
 *      For complex storage, only the real component is returned.
 *      Returns 0.0 for an unknown storage type.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static double RawDecodeBinaryAxisValue(const unsigned char *pointPtr, RawHeader *h) {
    RawVariable *axis = &h->variables[0];
    const unsigned char *p = pointPtr + axis->offsetBytes;
    switch (axis->storage) {
    case RAW_VALUE_REAL32:
        return ReadLEFloat32AsDouble(p);
    case RAW_VALUE_REAL64:
        return ReadLEFloat64(p);
    case RAW_VALUE_COMPLEX128:
        return ReadLEFloat64(p);
    default:
        return 0.0;
    }
}

//** LTspice stepped plot splitting
//***  RawAppendStepBoundary function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAppendStepBoundary --
 *
 *      Appends one LTspice step start point to the step-boundary arrays.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *      Tcl_Size **startsPtr     - Address of the step-start array.
 *      Tcl_Size **countsPtr     - Address of the step-count array.
 *      Tcl_Size *capacityPtr    - Current allocated capacity of both arrays.
 *      Tcl_Size *numStepsPtr    - Current number of stored steps.
 *      Tcl_Size startPoint      - Start point index for the new step.
 *
 * Results:
 *      Returns TCL_OK if the boundary is appended successfully.
 *      Returns TCL_ERROR if the arrays cannot be grown.
 *
 * Side Effects:
 *      May resize *startsPtr and *countsPtr.
 *      Appends startPoint and a zero placeholder count.
 *      Updates *capacityPtr and *numStepsPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The caller fills the step count after the step end is known.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAppendStepBoundary(Tcl_Interp *interp, Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *capacityPtr,
                                 Tcl_Size *numStepsPtr, Tcl_Size startPoint) {
    if (*numStepsPtr == *capacityPtr) {
        Tcl_Size newCapacity = *capacityPtr ? *capacityPtr * 2 : 8;
        Tcl_Size *newStarts;
        Tcl_Size *newCounts;
        if ((size_t)newCapacity > SIZE_MAX / sizeof(Tcl_Size)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice step boundary array overflow", -1));
            return TCL_ERROR;
        }
        newStarts = (Tcl_Size *)Tcl_Realloc((char *)*startsPtr, sizeof(Tcl_Size) * (size_t)newCapacity);
        newCounts = (Tcl_Size *)Tcl_Realloc((char *)*countsPtr, sizeof(Tcl_Size) * (size_t)newCapacity);
        if (newStarts == NULL || newCounts == NULL) {
            if (newStarts) {
                *startsPtr = newStarts;
            }
            if (newCounts) {
                *countsPtr = newCounts;
            }
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to allocate LTspice step boundaries", -1));
            return TCL_ERROR;
        }
        *startsPtr = newStarts;
        *countsPtr = newCounts;
        *capacityPtr = newCapacity;
    }
    (*startsPtr)[*numStepsPtr] = startPoint;
    (*countsPtr)[*numStepsPtr] = 0;
    (*numStepsPtr)++;
    return TCL_OK;
}

//***  RawLtspiceFindAxisResetSteps function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceFindAxisResetSteps --
 *
 *      Finds LTspice step boundaries by scanning the binary axis variable for resets.
 *
 * Parameters:
 *      Tcl_Interp *interp     - Interpreter used for error reporting.
 *      Tcl_Channel chan       - Raw-file channel used to read binary data.
 *      RawPlot *plot          - Plot whose binary data is scanned.
 *      int transientMode      - Non-zero to use transient time-reset detection.
 *      Tcl_Size **startsPtr   - Output location for the step-start array.
 *      Tcl_Size **countsPtr   - Output location for the step-count array.
 *      Tcl_Size *numStepsPtr  - Output location for the number of detected steps.
 *
 * Results:
 *      Returns TCL_OK if the step boundaries are detected successfully.
 *      Returns TCL_ERROR if plot dimensions are invalid, allocation fails, or binary reading fails.
 *
 * Side Effects:
 *      Seeks and reads from chan.
 *      Allocates step-start and step-count arrays owned by the caller.
 *      Stores the arrays and step count in the output locations.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The first point is always treated as the start of the first step.
 *      In transient mode, a new step is detected when the axis value is less than or equal to the previous value.
 *      In non-transient mode, sweep direction is detected and reset detection is direction-aware.
 *      Binary data is scanned in chunks of complete points.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceFindAxisResetSteps(Tcl_Interp *interp, Tcl_Channel chan, RawPlot *plot, int transientMode,
                                        Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *numStepsPtr) {
    RawHeader *h = &plot->header;
    RawAxisDirection direction = RAW_AXIS_DIRECTION_UNKNOWN;
    Tcl_Size capacity = 0;
    Tcl_Size numSteps = 0;
    Tcl_Size totalPoints = h->numPoints;
    Tcl_Size maxChunkBytes = 1024 * 1024;
    Tcl_Size chunkPoints;
    Tcl_Size done = 0;
    Tcl_Size *starts = NULL;
    Tcl_Size *counts = NULL;
    unsigned char *buf = NULL;
    double previous = 0.0;
    int havePrevious = 0;
    if (totalPoints < 0 || h->pointStrideBytes <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid LTspice stepped plot dimensions", -1));
        return TCL_ERROR;
    }
    if (RawAppendStepBoundary(interp, &starts, &counts, &capacity, &numSteps, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    if (totalPoints == 0) {
        *startsPtr = starts;
        *countsPtr = counts;
        *numStepsPtr = numSteps;
        return TCL_OK;
    }
    chunkPoints = maxChunkBytes / h->pointStrideBytes;
    if (chunkPoints < 1) {
        chunkPoints = 1;
    }
    if (chunkPoints > totalPoints) {
        chunkPoints = totalPoints;
    }
    if (chunkPoints > TCL_SIZE_MAX / h->pointStrideBytes) {
        Tcl_Free((char *)starts);
        Tcl_Free((char *)counts);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice stepped chunk size overflow", -1));
        return TCL_ERROR;
    }
    buf = (unsigned char *)Tcl_Alloc((size_t)(chunkPoints * h->pointStrideBytes));
    while (done < totalPoints) {
        Tcl_Size thisPoints = totalPoints - done;
        Tcl_Size thisBytes;
        Tcl_WideInt offset;
        if (thisPoints > chunkPoints) {
            thisPoints = chunkPoints;
        }
        thisBytes = thisPoints * h->pointStrideBytes;
        offset = plot->dataOffset + (Tcl_WideInt)done * (Tcl_WideInt)h->pointStrideBytes;
        if (Tcl_Seek(chan, offset, SEEK_SET) < 0) {
            Tcl_Free((char *)buf);
            Tcl_Free((char *)starts);
            Tcl_Free((char *)counts);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek inside LTspice stepped data", -1));
            return TCL_ERROR;
        }
        if (RawBinaryReadExactBytes(interp, chan, buf, thisBytes) != TCL_OK) {
            Tcl_Free((char *)buf);
            Tcl_Free((char *)starts);
            Tcl_Free((char *)counts);
            return TCL_ERROR;
        }
        for (Tcl_Size i = 0; i < thisPoints; i++) {
            Tcl_Size pointIndex = done + i;
            const unsigned char *pointPtr = buf + i * h->pointStrideBytes;
            double axisValue = RawDecodeBinaryAxisValue(pointPtr, h);
            /*
             * LTspice stepped transient runs can be separated without .log metadata by watching
             * the x-axis/time variable reset. The user requested "stops increasing", so equality
             * is also treated as a boundary.
             */
            if (havePrevious &&
                (transientMode ? (axisValue <= previous) : RawAxisStartsNewStep(previous, axisValue, &direction))) {
                counts[numSteps - 1] = pointIndex - starts[numSteps - 1];
                if (RawAppendStepBoundary(interp, &starts, &counts, &capacity, &numSteps, pointIndex) != TCL_OK) {
                    Tcl_Free((char *)buf);
                    Tcl_Free((char *)starts);
                    Tcl_Free((char *)counts);
                    return TCL_ERROR;
                }
                direction = RAW_AXIS_DIRECTION_UNKNOWN;
            }
            previous = axisValue;
            havePrevious = 1;
        }
        done += thisPoints;
    }
    counts[numSteps - 1] = totalPoints - starts[numSteps - 1];
    Tcl_Free((char *)buf);
    *startsPtr = starts;
    *countsPtr = counts;
    *numStepsPtr = numSteps;
    return TCL_OK;
}

//***  RawAxisStartsNewStep function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAxisStartsNewStep --
 *
 *      Tests whether a non-transient axis value starts a new step.
 *
 * Parameters:
 *      double previous                 - Previous axis value.
 *      double current                  - Current axis value.
 *      RawAxisDirection *directionPtr  - Current sweep direction, updated while scanning.
 *
 * Results:
 *      Returns non-zero if current starts a new step; zero otherwise.
 *
 * Side Effects:
 *      May update *directionPtr when the sweep direction is first detected.
 *
 * Notes:
 *      Increasing sweeps start a new step when current <= previous.
 *      Decreasing sweeps start a new step when current >= previous.
 *      Equal values before direction is known are treated as a step boundary.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAxisStartsNewStep(double previous, double current, RawAxisDirection *directionPtr) {
    if (*directionPtr == RAW_AXIS_DIRECTION_UNKNOWN) {
        if (current > previous) {
            *directionPtr = RAW_AXIS_DIRECTION_INCREASING;
            return 0;
        }
        if (current < previous) {
            *directionPtr = RAW_AXIS_DIRECTION_DECREASING;
            return 0;
        }
        /*
         * Equal axis before direction is known is treated as a boundary.
         */
        return 1;
    }
    if (*directionPtr == RAW_AXIS_DIRECTION_INCREASING) {
        return current <= previous;
    }
    return current >= previous;
}

//***  RawLtspiceBuildFixedSteps function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceBuildFixedSteps --
 *
 *      Builds LTspice step boundaries from a fixed number of points per step.
 *
 * Parameters:
 *      Tcl_Interp *interp     - Interpreter used for error reporting.
 *      Tcl_Size totalPoints   - Total number of physical points in the data block.
 *      Tcl_Size pointsPerStep - Number of points in each step.
 *      Tcl_Size **startsPtr   - Output location for the step-start array.
 *      Tcl_Size **countsPtr   - Output location for the step-count array.
 *      Tcl_Size *numStepsPtr  - Output location for the number of steps.
 *
 * Results:
 *      Returns TCL_OK if the fixed step arrays are built successfully.
 *      Returns TCL_ERROR if pointsPerStep is invalid, totalPoints is not divisible by it, or allocation would overflow.
 *
 * Side Effects:
 *      Allocates step-start and step-count arrays owned by the caller.
 *      Stores the arrays and step count in the output locations.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Each generated step has exactly pointsPerStep points.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceBuildFixedSteps(Tcl_Interp *interp, Tcl_Size totalPoints, Tcl_Size pointsPerStep,
                                     Tcl_Size **startsPtr, Tcl_Size **countsPtr, Tcl_Size *numStepsPtr) {
    Tcl_Size numSteps;
    Tcl_Size *starts;
    Tcl_Size *counts;
    if (pointsPerStep <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid LTspice per-step point count", -1));
        return TCL_ERROR;
    }
    if (totalPoints % pointsPerStep != 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice stepped data is not divisible by declared point count", -1));
        return TCL_ERROR;
    }
    numSteps = totalPoints / pointsPerStep;
    if ((size_t)numSteps > SIZE_MAX / sizeof(Tcl_Size)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice fixed step array overflow", -1));
        return TCL_ERROR;
    }
    starts = (Tcl_Size *)Tcl_Alloc(sizeof(Tcl_Size) * (size_t)numSteps);
    counts = (Tcl_Size *)Tcl_Alloc(sizeof(Tcl_Size) * (size_t)numSteps);
    for (Tcl_Size i = 0; i < numSteps; i++) {
        starts[i] = i * pointsPerStep;
        counts[i] = pointsPerStep;
    }
    *startsPtr = starts;
    *countsPtr = counts;
    *numStepsPtr = numSteps;
    return TCL_OK;
}

//***  RawLtspiceIsTransientPlot function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceIsTransientPlot --
 *
 *      Checks whether a parsed LTspice plot should be treated as transient data.
 *
 * Parameters:
 *      const RawHeader *h - Header to inspect.
 *
 * Results:
 *      Returns non-zero if the plot appears to be transient; zero otherwise.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      A plot is treated as transient if its Plotname contains "Transient" or its first variable is named "time".
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceIsTransientPlot(const RawHeader *h) {
    if (h->plotname && strstr(h->plotname, "Transient") != NULL) {
        return 1;
    }
    if (h->numVariables > 0 && h->variables && h->variables[0].name && strcmp(h->variables[0].name, "time") == 0) {
        return 1;
    }
    return 0;
}

//***  RawLtspiceAppendSegmentPlots function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceAppendSegmentPlots --
 *
 *      Appends binary LTspice step segments as separate pseudo-plots.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawFile *rf        - Raw file handle whose plot array is extended.
 *      RawPlot *basePlot  - Physical LTspice plot being split.
 *      Tcl_Size *starts   - Step-start point indexes.
 *      Tcl_Size *counts   - Point count for each step.
 *      Tcl_Size numSteps  - Number of step entries.
 *
 * Results:
 *      Returns TCL_OK if all non-empty step plots are appended successfully.
 *      Returns TCL_ERROR if header cloning, byte-count calculation, or plot append fails.
 *
 * Side Effects:
 *      Clones basePlot->header for each appended step plot.
 *      Appends new DATA_BINARY plots to rf.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The appended plots reference byte ranges inside the original binary data block.
 *      Empty step entries are skipped.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceAppendSegmentPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot, Tcl_Size *starts,
                                        Tcl_Size *counts, Tcl_Size numSteps) {
    RawHeader *h = &basePlot->header;
    for (Tcl_Size i = 0; i < numSteps; i++) {
        RawPlot stepPlot;
        Tcl_Size dataBytes;
        Tcl_WideInt byteOffset;
        if (counts[i] <= 0) {
            continue;
        }
        RawPlotInit(&stepPlot);
        if (RawHeaderClone(interp, &stepPlot.header, h) != TCL_OK) {
            RawPlotFree(&stepPlot);
            return TCL_ERROR;
        }
        if (RawMulSize(interp, counts[i], h->pointStrideBytes, "LTspice step byte count overflow", &dataBytes) !=
            TCL_OK) {
            RawPlotFree(&stepPlot);
            return TCL_ERROR;
        }
        byteOffset = (Tcl_WideInt)starts[i] * (Tcl_WideInt)h->pointStrideBytes;
        stepPlot.header.numPoints = counts[i];
        stepPlot.header.haveNumPoints = 1;
        stepPlot.dataKind = DATA_BINARY;
        stepPlot.dataOffset = basePlot->dataOffset + byteOffset;
        stepPlot.dataBytes = dataBytes;
        stepPlot.nextOffset = stepPlot.dataOffset + (Tcl_WideInt)dataBytes;
        if (RawFileAppendPlotMove(interp, rf, &stepPlot) != TCL_OK) {
            RawPlotFree(&stepPlot);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

//***  RawLtspiceAppendSplitBinaryPlots function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceAppendSplitBinaryPlots --
 *
 *      Appends an LTspice binary plot, splitting stepped data into pseudo-plots when needed.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *      RawFile *rf              - Raw file handle whose plot array is extended.
 *      RawPlot *basePlot        - Prepared LTspice binary plot to append or split.
 *      Tcl_Size declaredPoints  - Original No. Points value from the raw header.
 *
 * Results:
 *      Returns TCL_OK if the plot or split step plots are appended successfully.
 *      Returns TCL_ERROR if step detection or plot append fails.
 *
 * Side Effects:
 *      May move basePlot into rf for non-stepped data.
 *      May append multiple DATA_BINARY pseudo-plots for stepped data.
 *      Allocates and frees temporary step-boundary arrays.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Non-transient stepped data is split by declared point count when possible.
 *      Transient data, or ambiguous stepped data, is split by axis reset detection.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceAppendSplitBinaryPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot,
                                            Tcl_Size declaredPoints) {
    RawHeader *h = &basePlot->header;
    Tcl_Size *starts = NULL;
    Tcl_Size *counts = NULL;
    Tcl_Size numSteps = 0;
    int r;
    if (!(h->flagsMask & RAW_FLAG_STEPPED)) {
        return RawFileAppendPlotMove(interp, rf, basePlot);
    }
    if (!RawLtspiceIsTransientPlot(h) && declaredPoints > 0 && h->numPoints > declaredPoints &&
        h->numPoints % declaredPoints == 0) {
        r = RawLtspiceBuildFixedSteps(interp, h->numPoints, declaredPoints, &starts, &counts, &numSteps);
    } else {
        r = RawLtspiceFindAxisResetSteps(interp, rf->chan, basePlot, RawLtspiceIsTransientPlot(h), &starts, &counts,
                                         &numSteps);
    }
    if (r != TCL_OK) {
        return TCL_ERROR;
    }
    r = RawLtspiceAppendSegmentPlots(interp, rf, basePlot, starts, counts, numSteps);
    Tcl_Free((char *)starts);
    Tcl_Free((char *)counts);
    return r;
}

//** LTspice ASCII stepped plot splitting
//***  RawAsciiFindNextContentLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAsciiFindNextContentLine --
 *
 *      Finds the next non-blank ASCII Values line or the start of the next raw header.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      Tcl_Channel chan        - Channel positioned inside an ASCII Values block.
 *      EncKind kind            - Detected raw header/text encoding kind.
 *      Tcl_Encoding enc        - Encoding handle used to decode lines.
 *      Tcl_WideInt *offsetPtr  - Output location for the found line offset.
 *      int *headerPtr          - Output flag; non-zero if the found line starts a new header.
 *      int *eofPtr             - Output flag; non-zero if EOF is reached.
 *
 * Results:
 *      Returns TCL_OK if a content line, header line, or EOF is found.
 *      Returns TCL_ERROR if the current offset cannot be read, line reading fails, or seeking back fails.
 *
 * Side Effects:
 *      Reads and skips blank lines from chan.
 *      Restores chan to the found non-blank line offset.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      A line starting with "Title:" is treated as the beginning of the next raw header.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAsciiFindNextContentLine(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                       Tcl_WideInt *offsetPtr, int *headerPtr, int *eofPtr) {
    for (;;) {
        Tcl_WideInt offset;
        Tcl_DString lineDs;
        const char *line;
        int r;
        offset = Tcl_Tell(chan);
        if (offset < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get ASCII raw line offset", -1));
            return TCL_ERROR;
        }
        r = ReadDecodedHeaderLine(interp, chan, kind, enc, &lineDs);
        if (r == RAW_HEADER_EOF) {
            *eofPtr = 1;
            *headerPtr = 0;
            *offsetPtr = Tcl_Tell(chan);
            return TCL_OK;
        }
        if (r == RAW_HEADER_ERROR) {
            return TCL_ERROR;
        }
        line = Tcl_DStringValue(&lineDs);
        /*
         * Blank lines between ASCII points are not meaningful.  Consume them
         * while searching for the next point or the next header.
         */
        if (*line == '\0') {
            Tcl_DStringFree(&lineDs);
            continue;
        }
        *headerPtr = StartsWith(line, "Title:");
        *eofPtr = 0;
        *offsetPtr = offset;
        Tcl_DStringFree(&lineDs);
        if (Tcl_Seek(chan, offset, SEEK_SET) < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to restore ASCII raw line offset", -1));
            return TCL_ERROR;
        }
        return TCL_OK;
    }
}

//***  RawAsciiAxisObjToDouble function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAsciiAxisObjToDouble --
 *
 *      Converts an ASCII axis value object to a scalar double.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for numeric conversion and error reporting.
 *      Tcl_Obj *valueObj   - Parsed ASCII value object.
 *      double *axisPtr     - Output location for the scalar axis value.
 *
 * Results:
 *      Returns TCL_OK if the axis value is converted successfully.
 *      Returns TCL_ERROR if numeric conversion fails.
 *
 * Side Effects:
 *      Stores the converted value in *axisPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Complex values are represented as {real imag}; only the real component is used.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAsciiAxisObjToDouble(Tcl_Interp *interp, Tcl_Obj *valueObj, double *axisPtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;
    /*
     * Complex ASCII values are represented by this reader as a two-element list
     * {real imag}.  For frequency/axis splitting, use the real component.
     */
    if (Tcl_ListObjGetElements(NULL, valueObj, &objc, &objv) == TCL_OK && objc == 2) {
        return Tcl_GetDoubleFromObj(interp, objv[0], axisPtr);
    }
    return Tcl_GetDoubleFromObj(interp, valueObj, axisPtr);
}

//***  RawAsciiReadAxisAtCurrentPoint function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAsciiReadAxisAtCurrentPoint --
 *
 *      Reads the axis value from the ASCII point at the current channel position.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tcl_Channel chan   - Channel positioned at the start of an ASCII point.
 *      EncKind kind       - Detected raw header/text encoding kind.
 *      Tcl_Encoding enc   - Encoding handle used to decode lines.
 *      RawHeader *h       - Header describing the point layout.
 *      double *axisPtr    - Output location for the decoded axis value.
 *
 * Results:
 *      Returns TCL_OK if the axis value is read and converted successfully.
 *      Returns TCL_ERROR if point reading, list extraction, or numeric conversion fails.
 *
 * Side Effects:
 *      Reads one ASCII point from chan.
 *      Stores the decoded axis value in *axisPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Only variable index 0 is appended while the full point is consumed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAsciiReadAxisAtCurrentPoint(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                          RawHeader *h, double *axisPtr) {
    Tcl_Obj *listObj;
    Tcl_Size objc;
    Tcl_Obj **objv;
    int r;
    listObj = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObj);
    r = RawAsciiReadOnePoint(interp, chan, kind, enc, h, 0, listObj, NULL);
    if (r != TCL_OK) {
        Tcl_DecrRefCount(listObj);
        return TCL_ERROR;
    }
    if (Tcl_ListObjGetElements(interp, listObj, &objc, &objv) != TCL_OK) {
        Tcl_DecrRefCount(listObj);
        return TCL_ERROR;
    }
    if (objc != 1) {
        Tcl_DecrRefCount(listObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to extract ASCII raw axis value", -1));
        return TCL_ERROR;
    }
    r = RawAsciiAxisObjToDouble(interp, objv[0], axisPtr);
    Tcl_DecrRefCount(listObj);
    return r;
}

//***  RawAppendAsciiScannedPoint function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAppendAsciiScannedPoint --
 *
 *      Appends one scanned ASCII point offset and axis value to dynamic arrays.
 *
 * Parameters:
 *      Tcl_Interp *interp          - Interpreter used for error reporting.
 *      Tcl_WideInt **offsetsPtr    - Address of the point-offset array.
 *      double **axisValuesPtr      - Address of the axis-value array.
 *      Tcl_Size *capacityPtr       - Current allocated capacity of both arrays.
 *      Tcl_Size *numPointsPtr      - Current number of stored points.
 *      Tcl_WideInt offset          - File offset of the scanned point.
 *      double axisValue            - Decoded axis value for the scanned point.
 *
 * Results:
 *      Returns TCL_OK if the point is appended successfully.
 *      Returns TCL_ERROR if the arrays cannot be grown.
 *
 * Side Effects:
 *      May resize *offsetsPtr and *axisValuesPtr.
 *      Appends offset and axisValue.
 *      Updates *capacityPtr and *numPointsPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The two arrays are kept at the same length and capacity.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAppendAsciiScannedPoint(Tcl_Interp *interp, Tcl_WideInt **offsetsPtr, double **axisValuesPtr,
                                      Tcl_Size *capacityPtr, Tcl_Size *numPointsPtr, Tcl_WideInt offset,
                                      double axisValue) {
    if (*numPointsPtr == *capacityPtr) {
        Tcl_Size newCapacity = *capacityPtr ? *capacityPtr * 2 : 1024;
        Tcl_WideInt *newOffsets;
        double *newAxisValues;
        if (newCapacity < *capacityPtr) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII raw point index capacity overflow", -1));
            return TCL_ERROR;
        }
        if ((size_t)newCapacity > SIZE_MAX / sizeof(Tcl_WideInt)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII raw point offset array overflow", -1));
            return TCL_ERROR;
        }
        if ((size_t)newCapacity > SIZE_MAX / sizeof(double)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII raw axis value array overflow", -1));
            return TCL_ERROR;
        }
        newOffsets = (Tcl_WideInt *)Tcl_Alloc(sizeof(Tcl_WideInt) * (size_t)newCapacity);
        newAxisValues = (double *)Tcl_Alloc(sizeof(double) * (size_t)newCapacity);
        if (*numPointsPtr > 0) {
            memcpy(newOffsets, *offsetsPtr, sizeof(Tcl_WideInt) * (size_t)*numPointsPtr);
            memcpy(newAxisValues, *axisValuesPtr, sizeof(double) * (size_t)*numPointsPtr);
        }
        if (*offsetsPtr) {
            Tcl_Free((char *)*offsetsPtr);
        }
        if (*axisValuesPtr) {
            Tcl_Free((char *)*axisValuesPtr);
        }
        *offsetsPtr = newOffsets;
        *axisValuesPtr = newAxisValues;
        *capacityPtr = newCapacity;
    }
    (*offsetsPtr)[*numPointsPtr] = offset;
    (*axisValuesPtr)[*numPointsPtr] = axisValue;
    (*numPointsPtr)++;
    return TCL_OK;
}

//***  RawLtspiceScanAllAsciiValues function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceScanAllAsciiValues --
 *
 *      Scans an LTspice ASCII Values block and records every point offset and axis value.
 *
 * Parameters:
 *      Tcl_Interp *interp      - Interpreter used for error reporting.
 *      Tcl_Channel chan        - Channel positioned inside the ASCII Values block.
 *      EncKind kind            - Detected raw header/text encoding kind.
 *      Tcl_Encoding enc        - Encoding handle used to decode lines.
 *      RawPlot *plot           - Plot whose point-offset index and data bounds are updated.
 *      double **axisValuesPtr  - Output location for the scanned axis-value array.
 *
 * Results:
 *      Returns TCL_OK if the ASCII Values block is scanned successfully.
 *      Returns TCL_ERROR if line scanning, seeking, axis reading, or allocation fails.
 *
 * Side Effects:
 *      Reads and seeks in chan.
 *      Allocates plot->pointOffsets and the axis-value array.
 *      Updates plot->numPointOffsets, plot->header.numPoints, plot->nextOffset, and plot->dataBytes.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Scanning stops at EOF or at the next line starting with "Title:".
 *      The returned axis-value array is owned by the caller and must be freed with Tcl_Free().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceScanAllAsciiValues(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc,
                                        RawPlot *plot, double **axisValuesPtr) {
    RawHeader *h = &plot->header;
    Tcl_WideInt *offsets = NULL;
    double *axisValues = NULL;
    Tcl_Size capacity = 0;
    Tcl_Size numPoints = 0;
    Tcl_WideInt endOffset;
    for (;;) {
        Tcl_WideInt pointOffset;
        double axisValue;
        int isHeader;
        int isEof;
        if (RawAsciiFindNextContentLine(interp, chan, kind, enc, &pointOffset, &isHeader, &isEof) != TCL_OK) {
            if (offsets) {
                Tcl_Free((char *)offsets);
            }
            if (axisValues) {
                Tcl_Free((char *)axisValues);
            }
            return TCL_ERROR;
        }
        if (isEof) {
            endOffset = pointOffset;
            break;
        }
        if (isHeader) {
            /*
             * RawAsciiFindNextContentLine() has already restored the channel
             * to the beginning of the header line.
             */
            endOffset = pointOffset;
            break;
        }
        if (Tcl_Seek(chan, pointOffset, SEEK_SET) < 0) {
            if (offsets) {
                Tcl_Free((char *)offsets);
            }
            if (axisValues) {
                Tcl_Free((char *)axisValues);
            }
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek to ASCII raw point", -1));
            return TCL_ERROR;
        }
        if (RawAsciiReadAxisAtCurrentPoint(interp, chan, kind, enc, h, &axisValue) != TCL_OK) {
            if (offsets) {
                Tcl_Free((char *)offsets);
            }
            if (axisValues) {
                Tcl_Free((char *)axisValues);
            }
            return TCL_ERROR;
        }
        if (RawAppendAsciiScannedPoint(interp, &offsets, &axisValues, &capacity, &numPoints, pointOffset, axisValue) !=
            TCL_OK) {
            if (offsets) {
                Tcl_Free((char *)offsets);
            }
            if (axisValues) {
                Tcl_Free((char *)axisValues);
            }
            return TCL_ERROR;
        }
    }
    if (endOffset < plot->dataOffset) {
        if (offsets) {
            Tcl_Free((char *)offsets);
        }
        if (axisValues) {
            Tcl_Free((char *)axisValues);
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid LTspice ASCII raw data offsets", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideInt)(Tcl_Size)(endOffset - plot->dataOffset) != endOffset - plot->dataOffset) {
        if (offsets) {
            Tcl_Free((char *)offsets);
        }
        if (axisValues) {
            Tcl_Free((char *)axisValues);
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice ASCII raw data byte count overflow", -1));
        return TCL_ERROR;
    }
    plot->pointOffsets = offsets;
    plot->numPointOffsets = numPoints;
    plot->header.numPoints = numPoints;
    plot->header.haveNumPoints = 1;
    plot->nextOffset = endOffset;
    plot->dataBytes = (Tcl_Size)(endOffset - plot->dataOffset);
    *axisValuesPtr = axisValues;
    return TCL_OK;
}

//***  RawLtspiceBuildAxisResetStepsFromValues function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceBuildAxisResetStepsFromValues --
 *
 *      Builds LTspice step boundaries from scanned ASCII axis values.
 *
 * Parameters:
 *      Tcl_Interp *interp        - Interpreter used for error reporting.
 *      const double *axisValues  - Array of scanned axis values.
 *      Tcl_Size totalPoints      - Number of scanned points.
 *      int transientMode         - Non-zero to use transient time-reset detection.
 *      Tcl_Size **startsPtr      - Output location for the step-start array.
 *      Tcl_Size **countsPtr      - Output location for the step-count array.
 *      Tcl_Size *numStepsPtr     - Output location for the number of detected steps.
 *
 * Results:
 *      Returns TCL_OK if the step arrays are built successfully.
 *      Returns TCL_ERROR if allocation fails.
 *
 * Side Effects:
 *      Allocates step-start and step-count arrays owned by the caller.
 *      Stores the arrays and step count in the output locations.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The first point is always treated as the start of the first step.
 *      In transient mode, a new step is detected when the axis value is less than or equal to the previous value.
 *      In non-transient mode, sweep direction is detected and reset detection is direction-aware.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceBuildAxisResetStepsFromValues(Tcl_Interp *interp, const double *axisValues, Tcl_Size totalPoints,
                                                   int transientMode, Tcl_Size **startsPtr, Tcl_Size **countsPtr,
                                                   Tcl_Size *numStepsPtr) {
    RawAxisDirection direction = RAW_AXIS_DIRECTION_UNKNOWN;
    Tcl_Size *starts = NULL;
    Tcl_Size *counts = NULL;
    Tcl_Size capacity = 0;
    Tcl_Size numSteps = 0;
    if (totalPoints <= 0) {
        *startsPtr = NULL;
        *countsPtr = NULL;
        *numStepsPtr = 0;
        return TCL_OK;
    }
    if (RawAppendStepBoundary(interp, &starts, &counts, &capacity, &numSteps, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    for (Tcl_Size i = 1; i < totalPoints; i++) {
        if (transientMode ? (axisValues[i] <= axisValues[i - 1])
                          : RawAxisStartsNewStep(axisValues[i - 1], axisValues[i], &direction)) {
            counts[numSteps - 1] = i - starts[numSteps - 1];
            if (RawAppendStepBoundary(interp, &starts, &counts, &capacity, &numSteps, i) != TCL_OK) {
                Tcl_Free((char *)starts);
                Tcl_Free((char *)counts);
                return TCL_ERROR;
            }
            direction = RAW_AXIS_DIRECTION_UNKNOWN;
        }
    }
    counts[numSteps - 1] = totalPoints - starts[numSteps - 1];
    *startsPtr = starts;
    *countsPtr = counts;
    *numStepsPtr = numSteps;
    return TCL_OK;
}

//***  RawLtspiceAppendAsciiSegmentPlots function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceAppendAsciiSegmentPlots --
 *
 *      Appends ASCII LTspice step segments as separate pseudo-plots.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for error reporting.
 *      RawFile *rf                  - Raw file handle whose plot array is extended.
 *      RawPlot *basePlot            - Physical LTspice ASCII plot being split.
 *      Tcl_WideInt *allPointOffsets - Point-offset array for the full physical Values block.
 *      Tcl_Size totalPoints         - Total number of points in allPointOffsets.
 *      Tcl_Size *starts             - Step-start point indexes.
 *      Tcl_Size *counts             - Point count for each step.
 *      Tcl_Size numSteps            - Number of step entries.
 *
 * Results:
 *      Returns TCL_OK if all non-empty step plots are appended successfully.
 *      Returns TCL_ERROR if header cloning, point-offset allocation, offset calculation, or plot append fails.
 *
 * Side Effects:
 *      Clones basePlot->header for each appended step plot.
 *      Allocates a per-step point-offset array for each appended plot.
 *      Appends new DATA_VALUES plots to rf.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The appended plots reference line offsets inside the original ASCII Values block.
 *      Empty step entries are skipped.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceAppendAsciiSegmentPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot,
                                             Tcl_WideInt *allPointOffsets, Tcl_Size totalPoints, Tcl_Size *starts,
                                             Tcl_Size *counts, Tcl_Size numSteps) {
    for (Tcl_Size i = 0; i < numSteps; i++) {
        RawPlot stepPlot;
        Tcl_WideInt nextOffset;
        Tcl_WideInt dataBytesWide;
        if (counts[i] <= 0) {
            continue;
        }
        RawPlotInit(&stepPlot);
        if (RawHeaderClone(interp, &stepPlot.header, &basePlot->header) != TCL_OK) {
            RawPlotFree(&stepPlot);
            return TCL_ERROR;
        }
        stepPlot.header.numPoints = counts[i];
        stepPlot.header.haveNumPoints = 1;
        stepPlot.dataKind = DATA_VALUES;
        if ((size_t)counts[i] > SIZE_MAX / sizeof(Tcl_WideInt)) {
            RawPlotFree(&stepPlot);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice ASCII point offset array overflow", -1));
            return TCL_ERROR;
        }
        stepPlot.pointOffsets = (Tcl_WideInt *)Tcl_Alloc(sizeof(Tcl_WideInt) * (size_t)counts[i]);
        stepPlot.numPointOffsets = counts[i];
        for (Tcl_Size point = 0; point < counts[i]; point++) {
            stepPlot.pointOffsets[point] = allPointOffsets[starts[i] + point];
        }
        stepPlot.dataOffset = stepPlot.pointOffsets[0];
        if (starts[i] + counts[i] < totalPoints) {
            nextOffset = allPointOffsets[starts[i] + counts[i]];
        } else {
            nextOffset = basePlot->nextOffset;
        }
        if (nextOffset < stepPlot.dataOffset) {
            RawPlotFree(&stepPlot);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid LTspice ASCII step offsets", -1));
            return TCL_ERROR;
        }
        dataBytesWide = nextOffset - stepPlot.dataOffset;
        if ((Tcl_WideInt)(Tcl_Size)dataBytesWide != dataBytesWide) {
            RawPlotFree(&stepPlot);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("LTspice ASCII step byte count overflow", -1));
            return TCL_ERROR;
        }
        stepPlot.nextOffset = nextOffset;
        stepPlot.dataBytes = (Tcl_Size)dataBytesWide;
        if (RawFileAppendPlotMove(interp, rf, &stepPlot) != TCL_OK) {
            RawPlotFree(&stepPlot);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

//***  RawLtspiceAppendSplitAsciiPlots function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawLtspiceAppendSplitAsciiPlots --
 *
 *      Appends an LTspice ASCII plot, splitting stepped data into pseudo-plots when needed.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *      RawFile *rf              - Raw file handle whose plot array is extended.
 *      RawPlot *basePlot        - LTspice ASCII plot to append or split.
 *      Tcl_Size declaredPoints  - Original No. Points value from the raw header.
 *
 * Results:
 *      Returns TCL_OK if the plot or split step plots are appended successfully.
 *      Returns TCL_ERROR if ASCII scanning, step detection, or plot append fails.
 *
 * Side Effects:
 *      Scans the full ASCII Values block and updates basePlot.
 *      May move basePlot into rf for non-stepped or empty data.
 *      May append multiple DATA_VALUES pseudo-plots for stepped data.
 *      Allocates and frees temporary axis-value and step-boundary arrays.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Non-transient stepped data is split by declared point count when possible.
 *      Transient data, or ambiguous stepped data, is split by axis reset detection.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawLtspiceAppendSplitAsciiPlots(Tcl_Interp *interp, RawFile *rf, RawPlot *basePlot,
                                           Tcl_Size declaredPoints) {
    RawHeader *h = &basePlot->header;
    double *axisValues = NULL;
    Tcl_Size *starts = NULL;
    Tcl_Size *counts = NULL;
    Tcl_Size numSteps = 0;
    Tcl_Size totalPoints;
    int r;
    if (RawLtspiceScanAllAsciiValues(interp, rf->chan, rf->encKind, rf->enc, basePlot, &axisValues) != TCL_OK) {
        return TCL_ERROR;
    }
    totalPoints = basePlot->header.numPoints;
    if (!(h->flagsMask & RAW_FLAG_STEPPED)) {
        if (axisValues) {
            Tcl_Free((char *)axisValues);
        }
        return RawFileAppendPlotMove(interp, rf, basePlot);
    }
    if (totalPoints == 0) {
        if (axisValues) {
            Tcl_Free((char *)axisValues);
        }
        return RawFileAppendPlotMove(interp, rf, basePlot);
    }
    /*
     * For LTspice non-transient stepped sweeps, prefer fixed-size split by
     * declared No. Points when it divides the physical ASCII block cleanly.
     *
     * For transient, split by the x-axis/time reset, as requested.
     */
    if (!RawLtspiceIsTransientPlot(h) && declaredPoints > 0 && totalPoints > declaredPoints &&
        totalPoints % declaredPoints == 0) {
        r = RawLtspiceBuildFixedSteps(interp, totalPoints, declaredPoints, &starts, &counts, &numSteps);
    } else {
        r = RawLtspiceBuildAxisResetStepsFromValues(interp, axisValues, totalPoints, RawLtspiceIsTransientPlot(h),
                                                    &starts, &counts, &numSteps);
    }
    if (axisValues) {
        Tcl_Free((char *)axisValues);
    }
    if (r != TCL_OK) {
        return TCL_ERROR;
    }
    r = RawLtspiceAppendAsciiSegmentPlots(interp, rf, basePlot, basePlot->pointOffsets, totalPoints, starts, counts,
                                          numSteps);
    if (starts) {
        Tcl_Free((char *)starts);
    }
    if (counts) {
        Tcl_Free((char *)counts);
    }
    return r;
}

//** Tcl command argument parsing
//***  RawParseOpenArgs function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawParseOpenArgs --
 *
 *      Parses arguments for the openraw command.
 *
 * Parameters:
 *      Tcl_Interp *interp        - Interpreter used for error reporting.
 *      Tcl_Size objc             - Number of command arguments.
 *      Tcl_Obj *const objv[]     - Command argument objects.
 *      RawDialect *dialectPtr    - Output location for the selected raw-file dialect.
 *      Tcl_Obj **fileNameObjPtr  - Output location for the file name object.
 *
 * Results:
 *      Returns TCL_OK if the arguments are parsed successfully.
 *      Returns TCL_ERROR if the argument count is invalid or the dialect name is unknown.
 *
 * Side Effects:
 *      Stores the selected dialect in *dialectPtr.
 *      Stores the file name object in *fileNameObjPtr.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      If -dialect is omitted, RAW_DIALECT_GENERIC is used.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawParseOpenArgs(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], RawDialect *dialectPtr,
                            Tcl_Obj **fileNameObjPtr) {
    RawDialect dialect = RAW_DIALECT_GENERIC;
    Tcl_Obj *fileNameObj = NULL;
    if (objc == 2) {
        fileNameObj = objv[1];
    } else if (objc == 4 && strcmp(Tcl_GetString(objv[1]), "-dialect") == 0) {
        const char *dialectName = Tcl_GetString(objv[2]);
        if (strcmp(dialectName, "generic") == 0) {
            dialect = RAW_DIALECT_GENERIC;
        } else if (strcmp(dialectName, "ltspice") == 0) {
            dialect = RAW_DIALECT_LTSPICE;
        } else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown raw dialect \"%s\"", dialectName));
            return TCL_ERROR;
        }
        fileNameObj = objv[3];
    } else {
        Tcl_WrongNumArgs(interp, 1, objv, "?-dialect generic|ltspice? fileName");
        return TCL_ERROR;
    }
    *dialectPtr = dialect;
    *fileNameObjPtr = fileNameObj;
    return TCL_OK;
}

//***  RawParsePlotIndex function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawParsePlotIndex --
 *
 *      Parses and validates a plot array index.
 *
 * Parameters:
 *      Tcl_Interp *interp     - Interpreter used for numeric conversion and error reporting.
 *      RawFile *rf            - Raw file handle whose plot array defines the valid range.
 *      Tcl_Obj *obj           - Tcl object containing the plot index.
 *      Tcl_Size *plotIndexPtr - Output validated plot index.
 *
 * Results:
 *      Returns TCL_OK if the index is valid; TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Writes the validated index to *plotIndexPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The validated index is an array index into rf->plots[].
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawParsePlotIndex(Tcl_Interp *interp, RawFile *rf, Tcl_Obj *obj, Tcl_Size *plotIndexPtr) {
    Tcl_WideInt wide;
    if (Tcl_GetWideIntFromObj(interp, obj, &wide) != TCL_OK) {
        return TCL_ERROR;
    }
    if (wide < 0 || wide >= rf->numPlots) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("plot index %lld out of range", (long long)wide));
        return TCL_ERROR;
    }
    *plotIndexPtr = (Tcl_Size)wide;
    return TCL_OK;
}

//***  RawParseRange function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawParseRange --
 *
 *      Parses and validates optional -from and -count point-range arguments.
 *
 * Parameters:
 *      Tcl_Interp *interp    - Interpreter used for numeric conversion and error reporting.
 *      RawPlot *plot         - Selected plot whose header supplies the valid point range.
 *      Tcl_Size objc         - Number of command arguments.
 *      Tcl_Obj *const objv[] - Command argument objects.
 *      Tcl_Size firstOpt     - Index where range-option parsing begins.
 *      Tcl_Size *fromPtr     - Output first point index.
 *      Tcl_Size *countPtr    - Output point count.
 *
 * Results:
 *      Returns TCL_OK if the range is parsed and valid.
 *      Returns TCL_ERROR on missing option value, invalid integer, negative value, unknown option, or out-of-range span.
 *
 * Side Effects:
 *      Writes the parsed range to *fromPtr and *countPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      If -from is omitted, it defaults to 0.
 *      If -count is omitted, it extends from -from to the end of the plot.
 *      The effective range is half-open: [from, from + count), so count 0 is valid.
 *      A countSet flag distinguishes omitted -count from explicit "-count 0".
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawParseRange(Tcl_Interp *interp, RawPlot *plot, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size firstOpt,
                         Tcl_Size *fromPtr, Tcl_Size *countPtr) {
    RawHeader *h = &plot->header;
    Tcl_Size from = 0;
    Tcl_Size count = 0;
    int countSet = 0;

    for (Tcl_Size i = firstOpt; i < objc; i += 2) {
        const char *opt;
        Tcl_WideInt wide;
        if (i + 1 >= objc) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing value for option %s", Tcl_GetString(objv[i])));
            return TCL_ERROR;
        }
        opt = Tcl_GetString(objv[i]);
        if (Tcl_GetWideIntFromObj(interp, objv[i + 1], &wide) != TCL_OK) {
            return TCL_ERROR;
        }
        if (wide < 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("range option must be non-negative", -1));
            return TCL_ERROR;
        }
        if (strcmp(opt, "-from") == 0) {
            from = (Tcl_Size)wide;
        } else if (strcmp(opt, "-count") == 0) {
            count = (Tcl_Size)wide;
            countSet = 1;
        } else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown option %s", opt));
            return TCL_ERROR;
        }
    }
    if (from > h->numPoints) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("-from is outside raw data range", -1));
        return TCL_ERROR;
    }
    if (!countSet) {
        count = h->numPoints - from;
    }
    if (count > h->numPoints - from) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("-count extends past end of raw data", -1));
        return TCL_ERROR;
    }
    *fromPtr = from;
    *countPtr = count;
    return TCL_OK;
}

//***  RawSelectPlotFromArgs function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawSelectPlotFromArgs --
 *
 *      Selects a RawPlot by parsing an optional "-plot index" pair.
 *
 * Parameters:
 *      Tcl_Interp *interp    - Interpreter used for error reporting.
 *      RawFile *rf           - Raw file handle whose plot array is searched.
 *      Tcl_Size objc         - Number of command arguments.
 *      Tcl_Obj *const objv[] - Command argument objects.
 *      Tcl_Size firstOpt     - Index where optional "-plot index" parsing begins.
 *      RawPlot **plotPtr     - Output selected plot pointer.
 *      Tcl_Size *nextArgPtr  - Output next unconsumed argument index.
 *
 * Results:
 *      Returns TCL_OK if a plot is selected.
 *      Returns TCL_ERROR if "-plot" is missing its value, the index is invalid, or the file contains no plots.
 *
 * Side Effects:
 *      Writes the selected plot pointer and next argument index on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      "-plot" is recognized only when it appears at firstOpt.
 *      If "-plot" is omitted, plot 0 is selected.
 *      The returned RawPlot pointer is borrowed from rf->plots[].
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawSelectPlotFromArgs(Tcl_Interp *interp, RawFile *rf, Tcl_Size objc, Tcl_Obj *const objv[],
                                 Tcl_Size firstOpt, RawPlot **plotPtr, Tcl_Size *nextArgPtr) {
    Tcl_Size plotIndex = 0;
    Tcl_Size next = firstOpt;
    if (next < objc && strcmp(Tcl_GetString(objv[next]), "-plot") == 0) {
        if (next + 1 >= objc) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing value for option -plot", -1));
            return TCL_ERROR;
        }
        if (RawParsePlotIndex(interp, rf, objv[next + 1], &plotIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        next += 2;
    }
    if (rf->numPlots == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw file contains no plots", -1));
        return TCL_ERROR;
    }
    *plotPtr = &rf->plots[plotIndex];
    *nextArgPtr = next;
    return TCL_OK;
}

//** Tcl command implementations
//***  RawFileObjCmd function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileObjCmd --
 *
 *      Implements the Tcl command associated with an opened raw-file handle.
 *
 *      A RawFile handle command is created by ::tclsimrawreader::openraw and stores a pointer to the corresponding
 *      RawFile structure in its ClientData. This function dispatches handle subcommands for metadata queries,
 *      lazy vector extraction, and handle cleanup.
 *
 *      Supported subcommands are:
 *
 *          $handle close
 *          $handle plots
 *          $handle header ?-plot index?
 *          $handle names ?-plot index?
 *          $handle npoints ?-plot index?
 *          $handle vector ?-plot index? name ?-from index? ?-count count?
 *          $handle vectors ?-plot index? (-all|nameList) ?-from index? ?-count count?
 *
 *      The vector subcommand returns one selected vector as a Tcl list.
 *      The vectors subcommand returns a dictionary mapping selected vector names to value lists. With -all, vectors
 *      returns all vectors from the selected plot.
 *
 * Parameters:
 *      void *clientData      - RawFile pointer supplied when the handle command was created.
 *      Tcl_Interp *interp    - Interpreter used for results and error reporting.
 *      Tcl_Size objc         - Number of command arguments.
 *      Tcl_Obj *const objv[] - Command argument objects.
 *
 * Results:
 *      Returns TCL_OK if the subcommand completes successfully.
 *      Returns TCL_ERROR on invalid arguments, unknown subcommand, invalid plot/range/vector selection, or read/decode
 *      failure.
 *
 * Side Effects:
 *      Dispatches handle subcommands.
 *      Sets the interpreter result for metadata and data-extraction subcommands.
 *      Deletes the handle command for close, triggering RawFileDeleteProc().
 *      May seek and read from the underlying raw-file channel for vector extraction.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Plot selection is delegated to RawSelectPlotFromArgs().
 *      Point range parsing is delegated to RawParseRange().
 *      Single-vector lookup is delegated to RawPlotFindVariable().
 *      Vector-list resolution for the vectors command is delegated to RawPlotResolveVariableList().
 *      Full-vector selection for vectors -all is delegated to RawPlotResolveAllVariables().
 *      Data extraction is lazy. The raw data block is read only when vector or vectors is invoked.
 *      The vector command returns RAW_VECTOR_RESULT_LIST through RawPlotVectorToObj().
 *      The vectors command returns RAW_VECTOR_RESULT_DICT through RawPlotReadVectorsToObj().
 *      In the current command grammar, -plot must appear before the vector name list or -all argument.
 *      After close deletes the handle command, the RawFile pointer must not be used again.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawFileObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RawFile *rf = (RawFile *)clientData;
    const char *subcmd;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?arg ...?");
        return TCL_ERROR;
    }
    subcmd = Tcl_GetString(objv[1]);
    if (strcmp(subcmd, "close") == 0) {
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        Tcl_DeleteCommandFromToken(interp, rf->token);
        return TCL_OK;
    }
    if (strcmp(subcmd, "plots") == 0) {
        Tcl_Obj *listObj;
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "");
            return TCL_ERROR;
        }
        listObj = Tcl_NewListObj(0, NULL);
        for (Tcl_Size i = 0; i < rf->numPlots; i++) {
            Tcl_ListObjAppendElement(interp, listObj, RawPlotSummaryObj(&rf->plots[i], i));
        }
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    if (strcmp(subcmd, "header") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next != objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index?");
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, RawHeaderToDictObj(&plot->header));
        return TCL_OK;
    }
    if (strcmp(subcmd, "names") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        Tcl_Obj *listObj;
        RawHeader *h;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next != objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index?");
            return TCL_ERROR;
        }
        h = &plot->header;
        listObj = Tcl_NewListObj(0, NULL);
        for (Tcl_Size i = 0; i < h->numVariables; i++) {
            Tcl_ListObjAppendElement(interp, listObj,
                                     Tcl_NewStringObj(h->variables[i].name ? h->variables[i].name : "", -1));
        }
        Tcl_SetObjResult(interp, listObj);
        return TCL_OK;
    }
    if (strcmp(subcmd, "npoints") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next != objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index?");
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)plot->header.numPoints));
        return TCL_OK;
    }
    if (strcmp(subcmd, "vectors") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        Tcl_Size firstPoint;
        Tcl_Size count;
        Tcl_Size numVars;
        Tcl_Size *varIndexes = NULL;
        Tcl_Obj *dictObj;
        int r;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next >= objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index? -all|namesList ?-from index? ?-count count?");
            return TCL_ERROR;
        }
        if (strcmp(Tcl_GetString(objv[next]), "-all") == 0) {
            if (RawPlotResolveAllVariables(interp, plot, &numVars, &varIndexes) != TCL_OK) {
                return TCL_ERROR;
            }
            next++;
        } else {
            if (RawPlotResolveVariableList(interp, plot, objv[next], &numVars, &varIndexes) != TCL_OK) {
                return TCL_ERROR;
            }
            next++;
        }
        if (RawParseRange(interp, plot, objc, objv, next, &firstPoint, &count) != TCL_OK) {
            if (varIndexes) {
                Tcl_Free((char *)varIndexes);
            }
            return TCL_ERROR;
        }
        r = RawPlotReadVectorsToObj(interp, rf, plot, numVars, varIndexes, firstPoint, count, RAW_VECTOR_RESULT_DICT,
                                    &dictObj);
        if (varIndexes) {
            Tcl_Free((char *)varIndexes);
        }
        if (r != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, dictObj);
        return TCL_OK;
    }
    if (strcmp(subcmd, "vector") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        Tcl_Size firstPoint;
        Tcl_Size count;
        Tcl_Size varIndex;
        Tcl_Obj *vecObj;
        const char *name;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next >= objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index? name ?-from index? ?-count count?");
            return TCL_ERROR;
        }
        name = Tcl_GetString(objv[next]);
        if (RawPlotFindVariable(interp, plot, name, &varIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        next++;
        if (RawParseRange(interp, plot, objc, objv, next, &firstPoint, &count) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawPlotVectorToObj(interp, rf, plot, varIndex, firstPoint, count, &vecObj) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, vecObj);
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown raw handle subcommand \"%s\"", subcmd));
    return TCL_ERROR;
}

//***  RawOpenCmd function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawOpenCmd --
 *
 *      Opens a SPICE raw file, scans its plots, and creates a Tcl handle command for lazy data access.
 *
 * Parameters:
 *      void *clientData      - Unused.
 *      Tcl_Interp *interp    - Interpreter used for command creation, results, and error reporting.
 *      Tcl_Size objc         - Number of command arguments.
 *      Tcl_Obj *const objv[] - Command arguments; expects fileName.
 *
 * Results:
 *      Returns TCL_OK if the file is opened, at least one plot is found, and a handle command is created.
 *      Returns TCL_ERROR on argument, open, encoding, header parse, data scan/skip, allocation, or empty-file error.
 *
 * Side Effects:
 *      Opens the file and sets binary translation.
 *      Detects and stores the header encoding.
 *      Allocates a RawFile and scans all plot headers.
 *      For Binary: plots, records offsets and skips the byte-counted data block.
 *      For Values: plots, scans the ASCII block and builds point-offset indexes.
 *      Creates a Tcl handle command and returns its name.
 *      On error, releases owned resources and closes the channel.
 *
 * Notes:
 *      Numeric data is not cached at open time; vector and allvectors subcommands decode it lazily.
 *      ReadHeader() consumes the Binary: or Values: marker and leaves the channel at the data block.
 *      The returned handle command owns the RawFile and releases it through RawFileDeleteProc().
 *      rawHandleCounter is used only to generate unique handle command names.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static Tcl_Size rawHandleCounter = 0;
static int RawOpenCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Channel chan;
    EncKind encKind;
    Tcl_Encoding enc = NULL;
    RawFile *rf;
    Tcl_Obj *nameObj;
    Tcl_Obj *fileNameObj;
    const char *name;
    RawDialect dialect;
    if (RawParseOpenArgs(interp, objc, objv, &dialect, &fileNameObj) != TCL_OK) {
        return TCL_ERROR;
    }
    chan = Tcl_FSOpenFileChannel(interp, fileNameObj, "r", 0);
    if (chan == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, chan, "-translation", "binary") != TCL_OK) {
        Tcl_Close(interp, chan);
        return TCL_ERROR;
    }
    if (DetectEncoding(interp, chan, &encKind, &enc) != TCL_OK) {
        Tcl_Close(interp, chan);
        return TCL_ERROR;
    }
    rf = (RawFile *)Tcl_Alloc(sizeof *rf);
    memset(rf, 0, sizeof *rf);
    rf->interp = interp;
    rf->chan = chan;
    rf->dialect = dialect;
    rf->encKind = encKind;
    rf->enc = enc;
    for (;;) {
        RawHeader h;
        RawPlot plot;
        DataKind dataKind;
        Tcl_WideInt dataOffset;
        RawHeaderStatus r;
        RawHeaderInit(&h);
        RawPlotInit(&plot);
        r = ReadHeader(interp, chan, encKind, enc, &h, &dataKind);
        if (r == RAW_HEADER_EOF) {
            RawHeaderFree(&h);
            break;
        }
        if (r == RAW_HEADER_ERROR) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            return TCL_ERROR;
        }
        dataOffset = Tcl_Tell(chan);
        if (dataOffset < 0) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get raw data offset", -1));
            return TCL_ERROR;
        }
        plot.dataKind = dataKind;
        plot.dataOffset = dataOffset;
        RawHeaderMove(&plot.header, &h);
        if (dataKind == DATA_BINARY) {
            Tcl_WideInt nextOffset;
            Tcl_Size dataBytes;
            if (dialect == RAW_DIALECT_LTSPICE) {
                Tcl_Size declaredPoints;
                Tcl_WideInt endOffset;
                if (RawLtspicePrepareBinaryPlot(interp, chan, &plot, &declaredPoints) != TCL_OK) {
                    RawPlotFree(&plot);
                    RawFileDeleteProc(rf);
                    return TCL_ERROR;
                }
                endOffset = plot.nextOffset;
                if (RawLtspiceAppendSplitBinaryPlots(interp, rf, &plot, declaredPoints) != TCL_OK) {
                    RawPlotFree(&plot);
                    RawFileDeleteProc(rf);
                    return TCL_ERROR;
                }
                /*
                 * If the plot was moved by RawFileAppendPlotMove(), RawPlotFree() is safe because
                 * RawPlotMove() reinitialized it. If pseudo-plots were cloned, this releases the
                 * original base header.
                 */
                RawPlotFree(&plot);
                if (Tcl_Seek(chan, endOffset, SEEK_SET) < 0) {
                    RawFileDeleteProc(rf);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to skip LTspice binary data block", -1));
                    return TCL_ERROR;
                }
                continue;
            }
            if (RawHeaderResolveVariableLayout(interp, &plot.header, dialect, 0) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
            if (ComputeBinaryByteCount(interp, &plot.header, &dataBytes) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
            nextOffset = dataOffset + (Tcl_WideInt)dataBytes;
            if (nextOffset < dataOffset) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                Tcl_SetObjResult(interp, Tcl_NewStringObj("raw binary data offset overflow", -1));
                return TCL_ERROR;
            }
            plot.nextOffset = nextOffset;
            plot.dataBytes = dataBytes;
            if (Tcl_Seek(chan, nextOffset, SEEK_SET) < 0) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to skip raw binary data block", -1));
                return TCL_ERROR;
            }
            if (RawFileAppendPlotMove(interp, rf, &plot) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
        } else if (dataKind == DATA_VALUES) {
            Tcl_Size declaredPoints;
            /*
             * ASCII values are textual, so LTspice's binary mixed float/double layout
             * does not apply.  We only need real vs complex parsing here.
             */
            if (RawHeaderResolveVariableLayout(interp, &plot.header, RAW_DIALECT_GENERIC, 0) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
            declaredPoints = plot.header.haveNumPoints ? plot.header.numPoints : 0;
            if (dialect == RAW_DIALECT_LTSPICE && (plot.header.flagsMask & RAW_FLAG_STEPPED)) {
                if (RawLtspiceAppendSplitAsciiPlots(interp, rf, &plot, declaredPoints) != TCL_OK) {
                    RawPlotFree(&plot);
                    RawFileDeleteProc(rf);
                    return TCL_ERROR;
                }
                /*
                 * If RawLtspiceAppendSplitAsciiPlots() moved the plot, RawPlotMove()
                 * reinitialized it.  If it cloned pseudo-plots, this frees the scanned
                 * base plot and its physical point-offset index.
                 */
                RawPlotFree(&plot);
                continue;
            }
            if (RawPlotScanAsciiValues(interp, chan, encKind, enc, &plot) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
            if (RawFileAppendPlotMove(interp, rf, &plot) != TCL_OK) {
                RawPlotFree(&plot);
                RawFileDeleteProc(rf);
                return TCL_ERROR;
            }
        } else {
            RawPlotFree(&plot);
            RawFileDeleteProc(rf);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw data block kind", -1));
            return TCL_ERROR;
        }
    }
    if (rf->numPlots == 0) {
        RawFileDeleteProc(rf);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw file contains no plots", -1));
        return TCL_ERROR;
    }
    nameObj = Tcl_ObjPrintf("::tclsimrawreader::handle%lld", (long long)++rawHandleCounter);
    name = Tcl_GetString(nameObj);
    rf->token = Tcl_CreateObjCommand2(interp, name, RawFileObjCmd, rf, RawFileDeleteProc);
    Tcl_SetObjResult(interp, nameObj);
    return TCL_OK;
}

//***  Tclsimrawreader_Init function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * Tclsimrawreader_Init --
 *
 *      Initializes the tclsimrawreader package and registers its Tcl commands.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter in which the package is initialized.
 *
 * Results:
 *      Returns TCL_OK if initialization succeeds; TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Initializes Tcl stubs.
 *      Ensures the ::tclsimrawreader namespace exists.
 *      Registers the ::tclsimrawreader::openraw command.
 *      Provides the package with Tcl_PkgProvideEx().
 *
 * Notes:
 *      This is the package initialization entry point called by Tcl when the extension is loaded.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
extern DLLEXPORT int Tclsimrawreader_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "8.6-10.0", 0) == NULL) {
        return TCL_ERROR;
    }
    /* check the existence of the namespace */
    if (Tcl_Eval(interp, "namespace eval ::tclsimrawreader:: {}") != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_CreateObjCommand2(interp, "::tclsimrawreader::openraw", (Tcl_ObjCmdProc2 *)RawOpenCmd, NULL, NULL);
    /* Provide the current package */
    if (Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION, NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

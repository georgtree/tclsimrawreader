#include "tclsimrawreader.h"

//** Header parsing functions
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

//***  RawHeaderResolveVariableLayout function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderResolveVariableLayout --
 *
 *      Resolves per-variable storage, byte offsets, and total point stride for a parsed raw header.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      RawHeader *h       - Parsed header whose flags and variables have already been filled.
 *
 * Results:
 *      Returns TCL_OK if storage and offsets are resolved.
 *      Returns TCL_ERROR if default storage cannot be resolved or the point stride would overflow.
 *
 * Side Effects:
 *      Calls ResolveDefaultStorage(), updating h->defaultStorage and h->defaultValueBytes.
 *      Updates each variable's storage, valueBytes, and offsetBytes.
 *      Updates h->pointStrideBytes.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      The current implementation applies the same storage format to every variable.
 *      Dialect-specific per-variable storage exceptions can be added before assigning offsetBytes.
 *      Assumes h->variables contains h->numVariables initialized RawVariable entries.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawHeaderResolveVariableLayout(Tcl_Interp *interp, RawHeader *h) {
    Tcl_Size offset = 0;
    if (ResolveDefaultStorage(interp, h) != TCL_OK) {
        return TCL_ERROR;
    }
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        RawVariable *v = &h->variables[i];
        /*
           Default case: every variable uses the storage implied by Flags:.
        */
        v->storage = h->defaultStorage;
        v->valueBytes = h->defaultValueBytes;
        /*
           Dialect-specific exceptions can go here.
        */
        v->offsetBytes = offset;
        if (v->valueBytes > TCL_SIZE_MAX - offset) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw point stride overflow", -1));
            return TCL_ERROR;
        }
        offset += v->valueBytes;
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
            if (RawHeaderResolveVariableLayout(interp, h) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
            *dataKindPtr = DATA_BINARY;
            Tcl_DStringFree(&utfLine);
            return RAW_HEADER_OK;
        }
        if (strcmp(line, "Values:") == 0) {
            if (RawHeaderResolveVariableLayout(interp, h) != TCL_OK) {
                Tcl_DStringFree(&utfLine);
                return RAW_HEADER_ERROR;
            }
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

//** Reading data block functions
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
static int RawAppendBinaryValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage,
                                 const unsigned char *p) {
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
                    if (vecObjs) {
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
            if (vecObjs) {
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

//***  RawPlotAsciiVectorToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotAsciiVectorToObj --
 *
 *      Reads one variable vector from an ASCII Values: plot into a Tcl list.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle containing channel and decoding state.
 *      RawPlot *plot       - ASCII Values: plot to read from.
 *      Tcl_Size varIndex   - Variable array index to extract.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl list object.
 *
 * Results:
 *      Returns TCL_OK if the requested vector range is read successfully.
 *      Returns TCL_ERROR on wrong data kind, missing point index, invalid range, seek failure, or parse error.
 *
 * Side Effects:
 *      Seeks rf->chan to the requested first point.
 *      Reads and parses count ASCII points.
 *      Stores a newly created list object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Uses pointOffsets built by RawPlotScanAsciiValues().
 *      Values are selected by variable array index and physical order in the Values: block.
 *      Real values are appended as doubles; complex values as {real imag} lists.
 *      A count of zero returns an empty list without seeking or reading.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotAsciiVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex,
                                   Tcl_Size firstPoint, Tcl_Size count, Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    Tcl_Obj *listObj;

    if (plot->dataKind != DATA_VALUES) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Values: plot", -1));
        return TCL_ERROR;
    }
    if (plot->pointOffsets == NULL && h->numPoints > 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII plot has no point offset index", -1));
        return TCL_ERROR;
    }
    if (varIndex < 0 || varIndex >= h->numVariables) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index out of range", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector range out of range", -1));
        return TCL_ERROR;
    }
    listObj = Tcl_NewListObj(0, NULL);
    if (count == 0) {
        *objPtr = listObj;
        return TCL_OK;
    }
    if (Tcl_Seek(rf->chan, plot->pointOffsets[firstPoint], SEEK_SET) < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek to ASCII raw point", -1));
        return TCL_ERROR;
    }
    for (Tcl_Size i = 0; i < count; i++) {
        if (RawAsciiReadOnePoint(interp, rf->chan, rf->encKind, rf->enc, h, varIndex, listObj, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    *objPtr = listObj;
    return TCL_OK;
}

//***  RawPlotAsciiDictToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotAsciiDictToObj --
 *
 *      Reads all variable vectors from an ASCII Values: plot over a point range into a Tcl dictionary.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle containing channel and decoding state.
 *      RawPlot *plot       - ASCII Values: plot to read from.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl dictionary object.
 *
 * Results:
 *      Returns TCL_OK if the requested range is read and the dictionary is created.
 *      Returns TCL_ERROR on wrong data kind, missing point index, invalid range, allocation overflow, seek failure,
 *      or parse error.
 *
 * Side Effects:
 *      Allocates temporary per-variable list objects and a result dictionary.
 *      Seeks rf->chan to the requested first point when count is nonzero.
 *      Reads and parses count ASCII points.
 *      Stores the new dictionary in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Uses pointOffsets built by RawPlotScanAsciiValues().
 *      Values are assigned by variable array order in the Values: block.
 *      Real values are appended as doubles; complex values as {real imag} lists.
 *      A count of zero returns all variable names mapped to empty lists.
 *      Temporary objects are released on error.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotAsciiDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                                 Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    Tcl_Obj **vecObjs;
    Tcl_Obj *dictObj;
    if (plot->dataKind != DATA_VALUES) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Values: plot", -1));
        return TCL_ERROR;
    }
    if (plot->pointOffsets == NULL && h->numPoints > 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("ASCII plot has no point offset index", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw dict range out of range", -1));
        return TCL_ERROR;
    }
    if ((size_t)h->numVariables > SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw dict vector object array overflow", -1));
        return TCL_ERROR;
    }
    vecObjs = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *) * (size_t)h->numVariables);
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        vecObjs[i] = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(vecObjs[i]);
    }
    if (count > 0) {
        if (Tcl_Seek(rf->chan, plot->pointOffsets[firstPoint], SEEK_SET) < 0) {
            for (Tcl_Size i = 0; i < h->numVariables; i++) {
                Tcl_DecrRefCount(vecObjs[i]);
            }
            Tcl_Free((char *)vecObjs);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek to ASCII raw point", -1));
            return TCL_ERROR;
        }
        for (Tcl_Size point = 0; point < count; point++) {
            if (RawAsciiReadOnePoint(interp, rf->chan, rf->encKind, rf->enc, h, -1, NULL, vecObjs) != TCL_OK) {
                for (Tcl_Size i = 0; i < h->numVariables; i++) {
                    Tcl_DecrRefCount(vecObjs[i]);
                }
                Tcl_Free((char *)vecObjs);
                return TCL_ERROR;
            }
        }
    }
    dictObj = Tcl_NewDictObj();
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        const char *name = h->variables[i].name ? h->variables[i].name : "";

        Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj(name, -1), vecObjs[i]);
        Tcl_DecrRefCount(vecObjs[i]);
    }
    Tcl_Free((char *)vecObjs);
    *objPtr = dictObj;
    return TCL_OK;
}

//***  RawPlotBinaryVectorToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotBinaryVectorToObj --
 *
 *      Reads one variable vector from a Binary: plot over a point range into a Tcl list.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle containing the channel.
 *      RawPlot *plot       - Binary plot to read from.
 *      Tcl_Size varIndex   - Variable array index to extract.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl list object.
 *
 * Results:
 *      Returns TCL_OK if the requested vector range is read and decoded.
 *      Returns TCL_ERROR on wrong data kind, invalid variable/range, invalid stride, chunk-size overflow, seek/read
 *      failure, or unknown value storage.
 *
 * Side Effects:
 *      Allocates a temporary chunk buffer.
 *      Seeks and reads from rf->chan.
 *      Stores a newly created list object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Assumes point-major binary storage and resolved variable offsets.
 *      Data is read in chunks rather than decoding the whole block at once.
 *      Real values are appended as doubles; complex values as {real imag} lists.
 *      A count of zero returns an empty list without reading.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotBinaryVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex,
                                    Tcl_Size firstPoint, Tcl_Size count, Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    RawVariable *var;
    Tcl_Obj *listObj;
    unsigned char *buf = NULL;
    Tcl_Size maxChunkBytes = 1024 * 1024;
    Tcl_Size chunkPoints;
    Tcl_Size done = 0;
    if (plot->dataKind != DATA_BINARY) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Binary: plot", -1));
        return TCL_ERROR;
    }
    if (varIndex < 0 || varIndex >= h->numVariables) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector index out of range", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw vector range out of range", -1));
        return TCL_ERROR;
    }
    var = &h->variables[varIndex];
    if (h->pointStrideBytes <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid raw point stride", -1));
        return TCL_ERROR;
    }
    if (count == 0) {
        *objPtr = Tcl_NewListObj(0, NULL);
        return TCL_OK;
    }
    chunkPoints = maxChunkBytes / h->pointStrideBytes;
    if (chunkPoints < 1) {
        chunkPoints = 1;
    }
    if (chunkPoints > count) {
        chunkPoints = count;
    }
    if (chunkPoints > TCL_SIZE_MAX / h->pointStrideBytes) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw chunk size overflow", -1));
        return TCL_ERROR;
    }
    buf = (unsigned char *)Tcl_Alloc((size_t)(chunkPoints * h->pointStrideBytes));
    listObj = Tcl_NewListObj(0, NULL);
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
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek inside raw binary data", -1));
            return TCL_ERROR;
        }
        if (RawBinaryReadExactBytes(interp, rf->chan, buf, thisBytes) != TCL_OK) {
            Tcl_Free((char *)buf);
            return TCL_ERROR;
        }
        for (Tcl_Size i = 0; i < thisPoints; i++) {
            const unsigned char *p = buf + i * h->pointStrideBytes + var->offsetBytes;
            if (RawAppendBinaryValue(interp, listObj, var->storage, p) != TCL_OK) {
                Tcl_Free((char *)buf);
                return TCL_ERROR;
            }
        }
        done += thisPoints;
    }
    Tcl_Free((char *)buf);
    *objPtr = listObj;
    return TCL_OK;
}

//***  RawPlotBinaryDictToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotBinaryDictToObj --
 *
 *      Reads all variable vectors from a Binary: plot over a point range into a Tcl dictionary.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle containing the channel.
 *      RawPlot *plot       - Binary plot to read from.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl dictionary object.
 *
 * Results:
 *      Returns TCL_OK if the requested range is read and decoded.
 *      Returns TCL_ERROR on wrong data kind, invalid range, invalid stride, allocation/chunk overflow, seek/read
 *      failure, or unknown value storage.
 *
 * Side Effects:
 *      Allocates temporary per-variable list objects and a binary chunk buffer.
 *      Seeks and reads from rf->chan.
 *      Stores a newly created dictionary in *objPtr on success.
 *      Releases temporary objects and buffers on error.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Assumes point-major binary storage and resolved variable offsets.
 *      Data is read in chunks rather than decoding the whole block at once.
 *      Dictionary keys are variable names; values are per-variable lists.
 *      Real values are appended as doubles; complex values as {real imag} lists.
 *      A count of zero returns all variable names mapped to empty lists.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotBinaryDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                            Tcl_Obj **objPtr) {
    RawHeader *h = &plot->header;
    Tcl_Obj **vecObjs = NULL;
    Tcl_Obj *dictObj = NULL;
    unsigned char *buf = NULL;
    Tcl_Size maxChunkBytes = 1024 * 1024;
    Tcl_Size chunkPoints;
    Tcl_Size done = 0;
    if (plot->dataKind != DATA_BINARY) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("selected plot is not a Binary: plot", -1));
        return TCL_ERROR;
    }
    if (firstPoint < 0 || count < 0 || firstPoint > h->numPoints || count > h->numPoints - firstPoint) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw dict range out of range", -1));
        return TCL_ERROR;
    }
    if (h->pointStrideBytes <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid raw point stride", -1));
        return TCL_ERROR;
    }
    if ((size_t)h->numVariables > SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw dict vector object array overflow", -1));
        return TCL_ERROR;
    }
    vecObjs = (Tcl_Obj **)Tcl_Alloc(sizeof(Tcl_Obj *) * (size_t)h->numVariables);
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        vecObjs[i] = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(vecObjs[i]);
    }
    if (count == 0) {
        dictObj = Tcl_NewDictObj();
        for (Tcl_Size i = 0; i < h->numVariables; i++) {
            const char *name = h->variables[i].name ? h->variables[i].name : "";
            Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj(name, -1), vecObjs[i]);
            Tcl_DecrRefCount(vecObjs[i]);
        }
        Tcl_Free((char *)vecObjs);
        *objPtr = dictObj;
        return TCL_OK;
    }
    chunkPoints = maxChunkBytes / h->pointStrideBytes;
    if (chunkPoints < 1) {
        chunkPoints = 1;
    }
    if (chunkPoints > count) {
        chunkPoints = count;
    }
    if (chunkPoints > TCL_SIZE_MAX / h->pointStrideBytes) {
        for (Tcl_Size i = 0; i < h->numVariables; i++) {
            Tcl_DecrRefCount(vecObjs[i]);
        }
        Tcl_Free((char *)vecObjs);
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
            for (Tcl_Size i = 0; i < h->numVariables; i++) {
                Tcl_DecrRefCount(vecObjs[i]);
            }
            Tcl_Free((char *)vecObjs);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to seek inside raw binary data", -1));
            return TCL_ERROR;
        }
        if (RawBinaryReadExactBytes(interp, rf->chan, buf, thisBytes) != TCL_OK) {
            Tcl_Free((char *)buf);
            for (Tcl_Size i = 0; i < h->numVariables; i++) {
                Tcl_DecrRefCount(vecObjs[i]);
            }
            Tcl_Free((char *)vecObjs);
            return TCL_ERROR;
        }
        for (Tcl_Size point = 0; point < thisPoints; point++) {
            const unsigned char *pointPtr = buf + point * h->pointStrideBytes;
            for (Tcl_Size varIndex = 0; varIndex < h->numVariables; varIndex++) {
                RawVariable *var = &h->variables[varIndex];
                const unsigned char *p = pointPtr + var->offsetBytes;
                if (RawAppendBinaryValue(interp, vecObjs[varIndex], var->storage, p) != TCL_OK) {
                    Tcl_Free((char *)buf);
                    for (Tcl_Size i = 0; i < h->numVariables; i++) {
                        Tcl_DecrRefCount(vecObjs[i]);
                    }
                    Tcl_Free((char *)vecObjs);
                    return TCL_ERROR;
                }
            }
        }
        done += thisPoints;
    }
    Tcl_Free((char *)buf);
    dictObj = Tcl_NewDictObj();
    for (Tcl_Size i = 0; i < h->numVariables; i++) {
        const char *name = h->variables[i].name ? h->variables[i].name : "";
        Tcl_DictObjPut(interp, dictObj, Tcl_NewStringObj(name, -1), vecObjs[i]);
        Tcl_DecrRefCount(vecObjs[i]);
    }
    Tcl_Free((char *)vecObjs);
    *objPtr = dictObj;
    return TCL_OK;
}

//***  RawPlotVectorToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotVectorToObj --
 *
 *      Reads one variable vector from a plot into a Tcl list, dispatching by data kind.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle.
 *      RawPlot *plot       - Plot to read from.
 *      Tcl_Size varIndex   - Variable array index to extract.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl list object.
 *
 * Results:
 *      Returns TCL_OK if the requested vector range is read successfully.
 *      Returns TCL_ERROR on unknown data kind or backend read/conversion failure.
 *
 * Side Effects:
 *      Calls the Binary: or Values: vector reader.
 *      Stores a newly created list object in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Detailed range and variable validation is delegated to the selected backend.
 *      Real values are returned as doubles; complex values as {real imag} lists.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr) {
    if (plot->dataKind == DATA_BINARY) {
        return RawPlotBinaryVectorToObj(interp, rf, plot, varIndex, firstPoint, count, objPtr);
    }
    if (plot->dataKind == DATA_VALUES) {
        return RawPlotAsciiVectorToObj(interp, rf, plot, varIndex, firstPoint, count, objPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw plot data kind", -1));
    return TCL_ERROR;
}

//***  RawPlotDictToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotDictToObj --
 *
 *      Reads all variable vectors from a plot over a point range into a Tcl dictionary, dispatching by data kind.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter used for error reporting.
 *      RawFile *rf         - Open raw-file handle.
 *      RawPlot *plot       - Plot to read from.
 *      Tcl_Size firstPoint - First point index to read.
 *      Tcl_Size count      - Number of points to read.
 *      Tcl_Obj **objPtr    - Output Tcl dictionary object.
 *
 * Results:
 *      Returns TCL_OK if the requested range is read successfully.
 *      Returns TCL_ERROR on unknown data kind or backend read/conversion failure.
 *
 * Side Effects:
 *      Calls the Binary: or Values: dictionary reader.
 *      Stores a newly created dictionary in *objPtr on success.
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Detailed range validation is delegated to the selected backend.
 *      Dictionary keys are variable names; values are per-variable lists.
 *      Real values are returned as doubles; complex values as {real imag} lists.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
                            Tcl_Obj **objPtr) {
    if (plot->dataKind == DATA_BINARY) {
        return RawPlotBinaryDictToObj(interp, rf, plot, firstPoint, count, objPtr);
    }
    if (plot->dataKind == DATA_VALUES) {
        return RawPlotAsciiDictToObj(interp, rf, plot, firstPoint, count, objPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raw plot data kind", -1));
    return TCL_ERROR;
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

//** Tcl command registering/processing function
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

//***  RawFileObjCmd function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileObjCmd --
 *
 *      Implements the Tcl command associated with an opened raw-file handle.
 *
 *      A RawFile handle command is created by raw::openraw and stores a pointer to the corresponding RawFile structure
 *      in its ClientData. This function dispatches handle subcommands such as:
 *
 *          $handle close
 *          $handle plots
 *          $handle header ?-plot index?
 *          $handle names ?-plot index?
 *          $handle npoints ?-plot index?
 *          $handle vector ?-plot index? name ?-from index? ?-count count?
 *          $handle dict ?-plot index? ?-from index? ?-count count?
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
 *      Dispatches handle subcommands: close, plots, header, names, npoints, vector, and dict.
 *      Sets the interpreter result for metadata and data-extraction subcommands.
 *      Deletes the handle command for close, triggering RawFileDeleteProc().
 *      Sets the interpreter result on failure.
 *
 * Notes:
 *      Plot selection is delegated to RawSelectPlotFromArgs().
 *      Point range parsing is delegated to RawParseRange().
 *      Variable lookup is delegated to RawPlotFindVariable().
 *      Data extraction is lazy and delegated to RawPlotVectorToObj() or RawPlotDictToObj().
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
    if (strcmp(subcmd, "vector") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        const char *name;
        Tcl_Size varIndex;
        Tcl_Size from;
        Tcl_Size count;
        Tcl_Obj *vecObj;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (next >= objc) {
            Tcl_WrongNumArgs(interp, 2, objv, "?-plot index? name ?-from index? ?-count count?");
            return TCL_ERROR;
        }
        name = Tcl_GetString(objv[next]);
        next++;
        if (RawPlotFindVariable(interp, plot, name, &varIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawParseRange(interp, plot, objc, objv, next, &from, &count) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawPlotVectorToObj(interp, rf, plot, varIndex, from, count, &vecObj) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, vecObj);
        return TCL_OK;
    }
    if (strcmp(subcmd, "dict") == 0) {
        RawPlot *plot;
        Tcl_Size next;
        Tcl_Size from;
        Tcl_Size count;
        Tcl_Obj *dictObj;
        if (RawSelectPlotFromArgs(interp, rf, objc, objv, 2, &plot, &next) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawParseRange(interp, plot, objc, objv, next, &from, &count) != TCL_OK) {
            return TCL_ERROR;
        }
        if (RawPlotDictToObj(interp, rf, plot, from, count, &dictObj) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, dictObj);
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
 *      Numeric data is not cached at open time; vector and dict subcommands decode it lazily.
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
    const char *name;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "fileName");
        return TCL_ERROR;
    }
    chan = Tcl_FSOpenFileChannel(interp, objv[1], "r", 0);
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
    rf->encKind = encKind;
    rf->enc = enc;
    for (;;) {
        RawHeader h;
        RawPlot plot;
        DataKind dataKind;
        Tcl_WideInt dataOffset;
        Tcl_WideInt nextOffset;
        Tcl_Size dataBytes;
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
        } else if (dataKind == DATA_VALUES) {
            if (RawPlotScanAsciiValues(interp, chan, encKind, enc, &plot) != TCL_OK) {
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
        if (RawFileAppendPlotMove(interp, rf, &plot) != TCL_OK) {
            RawPlotFree(&plot);
            RawFileDeleteProc(rf);
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

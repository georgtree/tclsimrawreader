#include "tclsimrawreader.h"

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

static void RawHeaderInit(RawHeader *h) { memset(h, 0, sizeof *h); }

//***  RawHeaderFree function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderFree --
 *
 *      Releases all memory owned by a RawHeader structure and resets the structure
 *      to an all-zero state.
 *
 *      This function is intended to be called after a header has been parsed, after
 *      an error during parsing, or before reusing an existing RawHeader object.
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
 *
 *      - Frees each stored flag string in h->flags[], then frees the h->flags
 *        array itself.
 *
 *      - Frees each variable name and type string in h->variables[], then frees
 *        the h->variables array itself.
 *
 *      - Resets the complete RawHeader structure with memset(), so all pointers,
 *        counters, flags, and layout fields become zero/NULL.
 *
 * Notes:
 *      - The function assumes that h has either been initialized with
 *        RawHeaderInit() or otherwise contains a valid partially/fully parsed
 *        RawHeader.
 *
 *      - The function assumes the structure invariants are valid:
 *            * if h->numFlags > 0, h->flags points to an array of that size
 *            * if h->numVariables > 0, h->variables points to an array of that size
 *
 *      - After this function returns, the RawHeader is empty and may be safely
 *        reused or passed to RawHeaderFree() again.
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

//***  TclStrDupLen function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * TclStrDupLen --
 *
 *      Allocates a new Tcl-managed character buffer, copies exactly len bytes from
 *      the supplied source pointer, and appends a terminating NUL byte.
 *
 *      This helper is used when the parser has a pointer/length pair into a larger
 *      temporary buffer, such as a token inside a decoded header line, and needs to
 *      store an owned, NUL-terminated copy in a RawHeader or RawVariable field.
 *
 * Parameters:
 *      const char *s   - Pointer to the first byte to copy. The source does not
 *                        need to be NUL-terminated.
 *
 *      Tcl_Size len    - Number of bytes to copy from s.
 *
 * Results:
 *      Returns a newly allocated NUL-terminated string containing exactly the copied
 *      bytes.
 *
 * Side Effects:
 *      - Allocates len + 1 bytes with Tcl_Alloc().
 *
 *      - Writes a trailing '\0' byte after the copied range.
 *
 * Notes:
 *      - The returned pointer is owned by the caller and must eventually be released
 *        with Tcl_Free().
 *
 *      - This function copies bytes, not Tcl characters. It is intended for already
 *        decoded Tcl UTF-8 strings where len is a byte length into the decoded buffer.
 *
 *      - The function does not check for allocation failure because Tcl_Alloc()
 *        follows Tcl's allocation behaviour. It also assumes len is non-negative and
 *        that s points to at least len readable bytes.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static char *TclStrDupLen(const char *s, Tcl_Size len) {
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
 *      Advances a string pointer past any leading whitespace characters.
 *
 *      This helper is used when parsing decoded raw-header lines, where field values
 *      may contain leading spaces after the key separator, for example:
 *
 *          "No. Points: 51"
 *                      ^
 *
 *      Calling SkipSpace() on the value part returns a pointer to the first
 *      non-whitespace character.
 *
 * Parameters:
 *      const char *s   - NUL-terminated string pointer to scan.
 *
 * Results:
 *      Returns a pointer into the original string, positioned at the first
 *      non-whitespace character. If the string contains only whitespace, the
 *      returned pointer points to the terminating NUL byte.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      - Whitespace detection uses isspace(), so the character is cast to
 *        unsigned char before passing it to avoid undefined behaviour for bytes
 *        with the high bit set.
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
 *      Computes the byte length of a NUL-terminated string after excluding trailing
 *      whitespace characters.
 *
 *      This helper is used when copying parsed raw-header field values into owned
 *      strings. For example, a line such as:
 *
 *          "No. Points: 51      "
 *
 *      can be trimmed so only the meaningful part of the value is copied.
 *
 * Parameters:
 *      const char *s   - NUL-terminated string to examine.
 *
 * Results:
 *      Returns the number of bytes from the start of s up to, but not including,
 *      any trailing whitespace. If the string contains only whitespace, returns 0.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *
 *      - The returned length is a byte count, not a character count.
 *
 *      - Whitespace detection uses isspace(), so each byte is cast to unsigned char
 *        before passing it to avoid undefined behaviour for bytes with the high bit
 *        set.
 *
 *      - The function assumes s is a valid NUL-terminated string.
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
 *      Stores a trimmed, owned copy of a raw-header string field.
 *
 *      This helper is used for simple string-valued header fields such as:
 *
 *          Title: voltage divider netlist
 *          Date: Thu Nov 28 21:12:14  2024
 *          Plotname: DC transfer characteristic
 *
 *      The input value is expected to point into a temporary decoded header line,
 *      usually just after the field name and colon. The function skips leading
 *      whitespace, trims trailing whitespace, duplicates the remaining byte range,
 *      and stores the new owned string in *fieldPtr.
 *
 * Parameters:
 *      char **fieldPtr     - Address of the string field to update. If the field
 *                            already contains a non-NULL pointer, the previous
 *                            string is released with Tcl_Free() before replacing it.
 *
 *      const char *value   - NUL-terminated value string to copy. The pointer may
 *                            refer into a temporary decoded line buffer.
 *
 * Results:
 *      Returns TCL_OK.
 *
 * Side Effects:
 *      - May free the previous string stored in *fieldPtr.
 *
 *      - Allocates a new Tcl-managed string with TclStrDupLen().
 *
 *      - Updates *fieldPtr to point to the newly allocated NUL-terminated copy.
 *
 * Notes:
 *      - The returned/stored string is owned by the structure containing fieldPtr
 *        and must eventually be released with Tcl_Free(), normally from
 *        RawHeaderFree().
 *
 *      - The function copies bytes from an already decoded Tcl UTF-8 string. The
 *        length used for copying is therefore a byte length, not a character count.
 *
 *      - The function currently always returns TCL_OK because Tcl_Alloc() is used
 *        underneath and no recoverable error path is implemented here.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int SetStringField(char **fieldPtr, const char *value) {
    value = SkipSpace(value);
    Tcl_Size len = TrimmedLen(value);
    if (*fieldPtr) {
        Tcl_Free(*fieldPtr);
    }
    *fieldPtr = TclStrDupLen(value, len);
    return TCL_OK;
}

//***  StartsWith function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * StartsWith --
 *
 *      Tests whether a NUL-terminated string begins with the specified prefix.
 *
 *      This helper is used when dispatching decoded raw-header lines by field name,
 *      for example:
 *
 *          Title:
 *          Date:
 *          Plotname:
 *          No. Variables:
 *          No. Points:
 *
 * Parameters:
 *      const char *s        - NUL-terminated string to test.
 *
 *      const char *prefix   - NUL-terminated prefix string to match at the beginning
 *                             of s.
 *
 * Results:
 *      Returns non-zero if s begins with prefix.
 *      Returns zero otherwise.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *
 *      - The function assumes both s and prefix are valid NUL-terminated strings.
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
 *      Detects the text encoding used by the raw-file header.
 *
 *      The parser probes the first six bytes of the file and uses the expected
 *      raw-header prefix to distinguish between UTF-8 and UTF-16LE encoded text:
 *
 *          UTF-8:
 *              "Title:"
 *
 *          UTF-16LE:
 *              'T' 0x00 'i' 0x00 't' 0x00
 *
 *      After detecting the encoding, the function obtains the corresponding Tcl
 *      encoding handle and pushes the six probe bytes back into the channel so the
 *      normal header reader can read the first line from the beginning.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting and for
 *                                 Tcl_GetEncoding().
 *
 *      Tcl_Channel chan         - Open channel positioned at the start of the raw
 *                                 file. The channel is expected to be configured for
 *                                 binary translation before this function is called.
 *
 *      EncKind *kindPtr         - Output location for the detected internal encoding
 *                                 kind:
 *                                      ENC_KIND_UTF8
 *                                      ENC_KIND_UTF16LE
 *
 *      Tcl_Encoding *encPtr     - Output location for the Tcl encoding handle
 *                                 returned by Tcl_GetEncoding().
 *
 * Results:
 *      Returns TCL_OK if the encoding is detected and the probe bytes are pushed
 *      back successfully.
 *
 *      Returns TCL_ERROR if:
 *          - fewer than six bytes can be read
 *          - the byte pattern does not match a supported encoding
 *          - Tcl_GetEncoding() fails
 *          - Tcl_Ungets() cannot restore the probe bytes
 *
 * Side Effects:
 *      - Reads six bytes from chan.
 *
 *      - On success, restores those six bytes to chan with Tcl_Ungets(), leaving
 *        the channel positioned as it was before the probe.
 *
 *      - On success, stores a Tcl_Encoding handle in *encPtr. The caller becomes
 *        responsible for releasing it with Tcl_FreeEncoding().
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message where appropriate.
 *
 *      - If Tcl_Ungets() fails after Tcl_GetEncoding() succeeded, the acquired
 *        encoding handle is released before returning TCL_ERROR.
 *
 * Notes:
 *      - The UTF-16LE test checks only the first three characters of "Title:".
 *        This matches the six-byte probe size. A stricter check could read twelve
 *        bytes and compare the full UTF-16LE spelling of "Title:".
 *
 *      - The function assumes the raw header begins with "Title:" or its UTF-16LE
 *        equivalent. Files with a BOM or leading whitespace would need additional
 *        detection logic.
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
 *      Reads one raw encoded header line from a raw-file channel.
 *
 *      The channel is expected to be configured for binary translation. This
 *      function therefore reads bytes exactly as stored in the file and applies
 *      only the minimum line-boundary logic needed for the detected header
 *      encoding:
 *
 *          ENC_KIND_UTF8:
 *              read byte-by-byte until '\n' is encountered
 *
 *          ENC_KIND_UTF16LE:
 *              read two bytes at a time until the UTF-16LE newline sequence
 *              0x0A 0x00 is encountered
 *
 *      The line terminator is consumed but is not appended to rawLinePtr. Any
 *      preceding carriage return remains in the raw line and is handled later,
 *      after decoding, by StripTrailingCR().
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for error reporting.
 *
 *      Tcl_Channel chan             - Open input channel positioned at the start
 *                                     of a header line. The channel should be in
 *                                     binary translation mode.
 *
 *      EncKind kind                 - Detected text encoding kind for the header:
 *                                          ENC_KIND_UTF8
 *                                          ENC_KIND_UTF16LE
 *
 *      Tcl_DString *rawLinePtr      - Output dynamic string. On RAW_HEADER_OK, it
 *                                     contains the raw encoded bytes of the line,
 *                                     excluding the newline terminator. The caller
 *                                     is responsible for freeing it with
 *                                     Tcl_DStringFree().
 *
 * Results:
 *      Returns RAW_HEADER_OK if a line was read successfully.
 *
 *      Returns RAW_HEADER_EOF if EOF is encountered before any bytes of a new line
 *      are read.
 *
 *      Returns RAW_HEADER_ERROR if:
 *          - Tcl_Read() reports an input error
 *          - a UTF-16LE line is truncated after only one byte of a two-byte code unit
 *
 * Side Effects:
 *      - Consumes bytes from chan up to and including the line terminator.
 *
 *      - Initializes rawLinePtr with Tcl_DStringInit().
 *
 *      - Appends raw encoded bytes to rawLinePtr.
 *
 *      - Sets the interpreter result if a truncated UTF-16LE line is detected.
 *
 * Notes:
 *      - For UTF-8 input, the delimiter is the single byte '\n'.
 *
 *      - For UTF-16LE input, the delimiter is the two-byte sequence '\n' 0x00.
 *
 *      - The returned Tcl_DString contains raw file bytes, not decoded text. In the
 *        UTF-16LE case it may contain embedded NUL bytes, so it must not be parsed
 *        with ordinary C string functions such as strlen(), strcmp(), or strncmp().
 *
 *      - A non-empty final line without a trailing newline is accepted and returned
 *        as RAW_HEADER_OK.
 *
 *      - If this function returns RAW_HEADER_ERROR after partially appending data,
 *        the caller should still call Tcl_DStringFree(rawLinePtr) before abandoning
 *        the line.
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
 *      Converts one raw encoded header line into Tcl's internal UTF-8 string
 *      representation.
 *
 *      The input line is expected to have been read by ReadRawHeaderLine(), so it
 *      contains raw file bytes without the line terminator. Depending on the
 *      detected file encoding, those bytes may be UTF-8 or UTF-16LE. This function
 *      uses the Tcl_Encoding handle selected by DetectEncoding() to decode the raw
 *      bytes into a Tcl_DString that can be parsed with normal C string functions.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used by Tcl_ExternalToUtfDStringEx()
 *                                     for conversion error reporting.
 *
 *      Tcl_Encoding enc             - Tcl encoding handle used to decode the raw
 *                                     line bytes, usually "utf-8" or "utf-16le".
 *
 *      Tcl_DString *rawLinePtr      - Input dynamic string containing the raw
 *                                     encoded bytes of one header line. The input
 *                                     string is not modified.
 *
 *      Tcl_DString *utfLinePtr      - Output dynamic string. On success, it contains
 *                                     the decoded Tcl UTF-8 representation of the
 *                                     header line. The caller is responsible for
 *                                     freeing it with Tcl_DStringFree().
 *
 * Results:
 *      Returns TCL_OK if the line is decoded successfully.
 *
 *      Returns TCL_ERROR if Tcl_ExternalToUtfDStringEx() reports a conversion
 *      failure.
 *
 * Side Effects:
 *      - Initializes utfLinePtr with Tcl_DStringInit().
 *
 *      - Appends decoded UTF-8 text to utfLinePtr on success.
 *
 *      - Frees utfLinePtr before returning TCL_ERROR if conversion fails.
 *
 * Notes:
 *      - rawLinePtr contains raw bytes and may include embedded NUL bytes, especially
 *        for UTF-16LE input. After successful conversion, utfLinePtr can be treated
 *        as a normal Tcl UTF-8 string for ASCII header-key comparisons such as
 *        "Title:", "Variables:", "Binary:", and "Values:".
 *
 *      - The decoded line still may contain a trailing carriage return if the source
 *        file used CRLF line endings. StripTrailingCR() should be applied after this
 *        function when marker or key comparison is needed.
 *
 *      - errorIndex receives the byte index of a conversion problem from
 *        Tcl_ExternalToUtfDStringEx(), but this implementation currently does not
 *        add that index to the error message.
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
 *      Reads one raw encoded header line from the channel, decodes it into Tcl's
 *      internal UTF-8 string representation, and normalizes a possible trailing
 *      carriage return.
 *
 *      This helper combines the lower-level raw byte reader and encoding conversion
 *      steps:
 *
 *          1. ReadRawHeaderLine()
 *                 reads one line as raw file bytes according to the detected
 *                 encoding kind.
 *
 *          2. DecodeHeaderLine()
 *                 converts those raw bytes using the Tcl_Encoding selected by
 *                 DetectEncoding().
 *
 *          3. StripTrailingCR()
 *                 removes a trailing '\r' after decoding, allowing comparisons
 *                 against markers such as "Binary:" and "Values:" to work for
 *                 CRLF input files.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for error reporting by the
 *                                     lower-level read and decode helpers.
 *
 *      Tcl_Channel chan             - Open input channel positioned at the start of
 *                                     a header line. The channel is expected to be
 *                                     configured for binary translation.
 *
 *      EncKind kind                 - Detected raw header encoding kind:
 *                                          ENC_KIND_UTF8
 *                                          ENC_KIND_UTF16LE
 *
 *      Tcl_Encoding enc             - Tcl encoding handle used to decode the raw
 *                                     line bytes, usually "utf-8" or "utf-16le".
 *
 *      Tcl_DString *utfLinePtr      - Output dynamic string. On RAW_HEADER_OK, it
 *                                     contains the decoded header line without the
 *                                     line terminator and without a trailing '\r'.
 *                                     The caller is responsible for freeing it with
 *                                     Tcl_DStringFree().
 *
 * Results:
 *      Returns RAW_HEADER_OK if a line was read, decoded, and normalized
 *      successfully.
 *
 *      Returns RAW_HEADER_EOF if EOF is encountered before any bytes of a new line
 *      are read.
 *
 *      Returns RAW_HEADER_ERROR if:
 *          - ReadRawHeaderLine() reports an input or truncation error
 *          - DecodeHeaderLine() reports an encoding conversion error
 *
 * Side Effects:
 *      - Consumes one encoded header line from chan, including its line terminator.
 *
 *      - Allocates temporary storage in rawLine while reading the encoded line.
 *
 *      - Initializes utfLinePtr through DecodeHeaderLine() on successful decoding.
 *
 *      - Frees the temporary rawLine buffer after successful decoding or after a
 *        decoding failure.
 *
 *      - Removes a trailing carriage return from utfLinePtr if the decoded line came
 *        from CRLF input.
 *
 * Notes:
 *      - The returned utfLinePtr is decoded text and may be safely compared with
 *        ASCII raw-header keys and markers such as:
 *
 *              "Title:"
 *              "Variables:"
 *              "Binary:"
 *              "Values:"
 *
 *      - Pointers obtained from Tcl_DStringValue(utfLinePtr) are borrowed and remain
 *        valid only while utfLinePtr is alive and unchanged.
 *
 *      - If ReadRawHeaderLine() returns RAW_HEADER_ERROR after partially appending
 *        data to rawLine, this implementation currently returns immediately without
 *        freeing rawLine. If ReadRawHeaderLine() can fail after initializing rawLine,
 *        add Tcl_DStringFree(&rawLine) before returning RAW_HEADER_ERROR.
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
 *      Removes a trailing carriage return character from a decoded header line.
 *
 *      This helper is used after DecodeHeaderLine() when reading raw-file headers
 *      that may use CRLF line endings. ReadRawHeaderLine() consumes the newline
 *      terminator, but if the source line ended with "\r\n", the decoded line still
 *      contains the preceding '\r'. Removing it allows direct comparisons against
 *      raw-header keys and markers such as:
 *
 *          "Binary:"
 *          "Values:"
 *          "Variables:"
 *
 * Parameters:
 *      Tcl_DString *dsPtr   - Decoded Tcl_DString line to normalize.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - If dsPtr ends with '\r', shortens the Tcl_DString by one byte using
 *        Tcl_DStringSetLength().
 *
 *      - Otherwise leaves dsPtr unchanged.
 *
 * Notes:
 *      - This function should be applied after decoding, not to the raw encoded
 *        line. For UTF-16LE input, the raw carriage return would be represented as
 *        two bytes, but after decoding it appears as a normal '\r' byte in Tcl's
 *        UTF-8 string representation.
 *
 *      - The function assumes dsPtr has been initialized and contains decoded text.
 *
 *      - Any pointer previously obtained from Tcl_DStringValue(dsPtr) should be
 *        considered invalid after Tcl_DStringSetLength() is called. Get a fresh
 *        Tcl_DStringValue() pointer after calling this function.
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
 *      Parses a non-negative integer size field from a decoded raw-header value
 *      string and stores it as a Tcl_Size.
 *
 *      This helper is used for numeric header fields such as:
 *
 *          No. Variables: 4
 *          No. Points: 51
 *
 *      The input string may contain leading whitespace. The function skips that
 *      whitespace, lets Tcl parse the remaining value as a wide integer, rejects
 *      negative values, and stores the result through outPtr.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for numeric conversion and error
 *                                 reporting.
 *
 *      const char *s            - NUL-terminated string containing the numeric field
 *                                 value, usually pointing just after a header key
 *                                 such as "No. Points:".
 *
 *      Tcl_Size *outPtr         - Output location for the parsed non-negative size.
 *
 * Results:
 *      Returns TCL_OK if the value is parsed successfully and is non-negative.
 *
 *      Returns TCL_ERROR if:
 *          - Tcl_GetWideIntFromObj() cannot parse the value as an integer
 *          - the parsed value is negative
 *
 * Side Effects:
 *      - Creates a temporary Tcl_Obj containing the numeric text.
 *
 *      - Increments and decrements the temporary object's reference count around
 *        the Tcl_GetWideIntFromObj() call.
 *
 *      - On success, writes the parsed value to *outPtr.
 *
 *      - On negative input, sets the interpreter result to:
 *
 *            "negative size field in raw header"
 *
 * Notes:
 *      - Leading whitespace is skipped with SkipSpace().
 *
 *      - The function uses Tcl's integer parser rather than atoi(), so parse errors
 *        are reported through the Tcl interpreter in the usual Tcl style.
 *
 *      - The function currently checks for negative values, but it does not check
 *        whether the Tcl_WideInt value exceeds the range of Tcl_Size before casting.
 *        If raw files may contain very large counts, add an upper-bound check before:
 *
 *            *outPtr = (Tcl_Size)wide;
 *
 *      - The input string is borrowed and is not modified.
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
 *      Appends one parsed raw-header flag token to the RawHeader flags list and
 *      updates the corresponding flag bitmask when the token is recognized.
 *
 *      This helper is used while parsing the value part of a raw-header Flags line,
 *      for example:
 *
 *          Flags: real
 *          Flags: real stepped
 *          Flags: double
 *          Flags: complex
 *
 *      The input flag is supplied as a pointer/length pair into a temporary decoded
 *      header line. The function stores an owned, NUL-terminated copy of that token
 *      in h->flags[] and increments h->numFlags.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      RawHeader *h             - Header structure whose flag list and bitmask are
 *                                 updated.
 *
 *      const char *start        - Pointer to the first byte of the flag token. The
 *                                 token does not need to be NUL-terminated.
 *
 *      Tcl_Size len             - Number of bytes in the flag token.
 *
 * Results:
 *      Returns TCL_OK if the flag token is appended successfully.
 *
 *      Returns TCL_ERROR if the h->flags array cannot be resized.
 *
 * Side Effects:
 *      - Resizes h->flags with Tcl_Realloc() to make room for one more char *
 *        entry.
 *
 *      - Allocates an owned, NUL-terminated copy of the flag token with
 *        TclStrDupLen().
 *
 *      - Stores the copied token at h->flags[h->numFlags].
 *
 *      - Increments h->numFlags.
 *
 *      - Updates h->flagsMask for recognized flags:
 *
 *            "real"      -> RAW_FLAG_REAL
 *            "double"    -> RAW_FLAG_DOUBLE
 *            "complex"   -> RAW_FLAG_COMPLEX
 *            "stepped"   -> RAW_FLAG_STEPPED
 *
 *      - Unknown flags are still preserved in h->flags[], but they do not set any
 *        bit in h->flagsMask.
 *
 * Notes:
 *      - The stored flag strings are owned by RawHeader and must be released by
 *        RawHeaderFree().
 *
 *      - The flag comparison is byte-wise and case-sensitive.
 *
 *      - The function copies bytes from an already decoded Tcl UTF-8 header line.
 *        len is therefore a byte length, not a character count.
 *
 *      - The function assumes h has been initialized with RawHeaderInit() before
 *        use, so h->flags is either NULL or a valid Tcl-allocated array.
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
    h->flags[h->numFlags] = TclStrDupLen(start, len);
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
 *      Parses the value part of a raw-header Flags line into individual whitespace-
 *      separated flag tokens.
 *
 *      This helper is used after the header dispatcher has identified a line such as:
 *
 *          Flags: real
 *          Flags: real stepped
 *          Flags: double
 *          Flags: complex
 *
 *      The input pointer is expected to refer to the text after "Flags:". The
 *      function skips leading whitespace, scans each non-whitespace token, and
 *      passes the token as a pointer/length pair to AppendFlag().
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting by
 *                                 AppendFlag().
 *
 *      RawHeader *h             - Header structure whose flag list and flag mask are
 *                                 updated.
 *
 *      const char *value        - NUL-terminated value part of the Flags line,
 *                                 usually pointing just after the "Flags:" prefix.
 *
 * Results:
 *      Returns TCL_OK if all flag tokens are parsed and appended successfully.
 *
 *      Returns TCL_ERROR if AppendFlag() fails while storing any flag token.
 *
 * Side Effects:
 *      - Appends one owned string to h->flags[] for each parsed flag token.
 *
 *      - Increments h->numFlags for each appended flag.
 *
 *      - Updates h->flagsMask for any recognized flag tokens through AppendFlag().
 *
 * Notes:
 *      - Tokens are separated by C isspace() whitespace.
 *
 *      - The parser is byte-oriented and case-sensitive.
 *
 *      - Empty or all-whitespace flag values are accepted and leave h->flags and
 *        h->flagsMask unchanged.
 *
 *      - The input string is borrowed and is not modified. Stored flag strings are
 *        copied by AppendFlag() and later released by RawHeaderFree().
 *
 *      - The function only tokenizes and stores flags. Any higher-level decision
 *        based on the resulting flags, such as selecting the effective per-variable
 *        storage size, should be performed separately after the relevant header
 *        fields have been parsed.
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
 *      Extracts the next whitespace-delimited token from a NUL-terminated string.
 *
 *      This helper is used when parsing decoded raw-header lines that contain
 *      positional fields, especially entries from the Variables section, for
 *      example:
 *
 *          0   v(v-sweep)   voltage
 *          1   v(in)        voltage
 *          2   v(out)       voltage
 *          3   i(v1)        current
 *
 *      The function skips leading whitespace, identifies the next non-whitespace
 *      token, and returns that token as a pointer/length pair into the original
 *      input string.
 *
 * Parameters:
 *      const char **pPtr        - Address of the current scan position. On entry,
 *                                 *pPtr points to the location where token scanning
 *                                 should begin. On success, *pPtr is advanced to
 *                                 the first character after the returned token.
 *
 *      const char **startPtr    - Output location for a pointer to the first byte of
 *                                 the token. The pointer refers into the original
 *                                 input string and is not NUL-terminated.
 *
 *      Tcl_Size *lenPtr         - Output location for the token byte length.
 *
 * Results:
 *      Returns 1 if a token is found.
 *
 *      Returns 0 if the remaining input contains no token, i.e. only whitespace or
 *      the terminating NUL byte.
 *
 * Side Effects:
 *      - Advances *pPtr past the returned token when a token is found.
 *
 *      - Writes *startPtr and *lenPtr on success.
 *
 *      - Does not allocate, copy, or modify memory.
 *
 * Notes:
 *      - The returned token is a borrowed pointer/length pair into the original
 *        string. If the token needs to be stored beyond the lifetime of that string,
 *        it must be copied, for example with TclStrDupLen().
 *
 *      - Tokens are separated by characters recognized by isspace().
 *
 *      - The character passed to isspace() is cast to unsigned char to avoid
 *        undefined behaviour for bytes with the high bit set.
 *
 *      - The function leaves *pPtr positioned after the token, not after following
 *        whitespace. A subsequent call to NextToken() will skip that whitespace
 *        itself.
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

//***  ParseVariableLine function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ParseVariableLine --
 *
 *      Parses one entry from the raw-header Variables section and stores the result
 *      in a RawVariable structure.
 *
 *      Variable lines are expected to contain at least three whitespace-separated
 *      fields:
 *
 *          index   name          type
 *
 *      For example:
 *
 *          0       v(v-sweep)    voltage
 *          1       v(in)         voltage
 *          2       v(out)        voltage
 *          3       i(v1)         current
 *
 *      The function extracts the index, variable name, and variable type. The index
 *      is parsed as a non-negative Tcl_Size value, while the name and type are copied
 *      into owned NUL-terminated strings stored in the RawVariable.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting and numeric
 *                                 parsing through ParseSizeField().
 *
 *      const char *line         - Decoded NUL-terminated Variables-section line to
 *                                 parse.
 *
 *      RawVariable *v           - Output variable structure to initialize and fill.
 *
 * Results:
 *      Returns TCL_OK if the line is parsed successfully.
 *
 *      Returns TCL_ERROR if:
 *          - fewer than three tokens are present
 *          - the first token cannot be parsed as a non-negative size value
 *
 * Side Effects:
 *      - Clears the target RawVariable with memset().
 *
 *      - Allocates a temporary NUL-terminated copy of the index token so it can be
 *        passed to ParseSizeField().
 *
 *      - Allocates owned NUL-terminated copies of the variable name and type fields
 *        with TclStrDupLen().
 *
 *      - On malformed input, sets the interpreter result to:
 *
 *            "malformed Variables entry in raw header"
 *
 * Notes:
 *      - Tokens are extracted with NextToken(), so fields are separated by C
 *        isspace() whitespace.
 *
 *      - The input line is borrowed and is not modified.
 *
 *      - The stored v->name and v->type strings are owned by RawVariable and must
 *        eventually be released by RawHeaderFree().
 *
 *      - The parser currently ignores any tokens after the first three fields. If a
 *        raw dialect uses additional per-variable metadata, this function should be
 *        extended to parse or preserve those tokens.
 *
 *      - If ParseSizeField() fails after the temporary index string is allocated,
 *        the temporary string is released before returning TCL_ERROR.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ParseVariableLine(Tcl_Interp *interp, const char *line, RawVariable *v) {
    const char *p = line;
    const char *idxStart;
    const char *nameStart;
    const char *typeStart;
    Tcl_Size idxLen, nameLen, typeLen;
    char *idxString;
    Tcl_Size index;

    memset(v, 0, sizeof *v);
    if (!NextToken(&p, &idxStart, &idxLen) || !NextToken(&p, &nameStart, &nameLen) ||
        !NextToken(&p, &typeStart, &typeLen)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("malformed Variables entry in raw header", -1));
        return TCL_ERROR;
    }
    idxString = TclStrDupLen(idxStart, idxLen);
    if (ParseSizeField(interp, idxString, &index) != TCL_OK) {
        Tcl_Free(idxString);
        return TCL_ERROR;
    }
    Tcl_Free(idxString);
    v->index = index;
    v->name = TclStrDupLen(nameStart, nameLen);
    v->type = TclStrDupLen(typeStart, typeLen);
    return TCL_OK;
}

//***  ReadVariablesSection function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadVariablesSection --
 *
 *      Reads and parses the raw-header Variables section.
 *
 *      This function is called after the main header parser has consumed the
 *      "Variables:" marker line. It expects the header to have already provided a
 *      valid "No. Variables:" field, allocates an array of RawVariable structures
 *      of that size, then reads and parses exactly that many variable-definition
 *      lines from the channel.
 *
 *      Each variable line is decoded with ReadDecodedHeaderLine() and parsed with
 *      ParseVariableLine(). A typical Variables section looks like:
 *
 *          Variables:
 *              0   v(v-sweep)   voltage
 *              1   v(in)        voltage
 *              2   v(out)       voltage
 *              3   i(v1)        current
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      Tcl_Channel chan         - Open input channel positioned at the first
 *                                 variable-definition line after the "Variables:"
 *                                 marker. The channel is expected to be configured
 *                                 for binary translation.
 *
 *      EncKind kind             - Detected raw header encoding kind:
 *                                      ENC_KIND_UTF8
 *                                      ENC_KIND_UTF16LE
 *
 *      Tcl_Encoding enc         - Tcl encoding handle used to decode each raw
 *                                 variable line.
 *
 *      RawHeader *h             - Header structure being filled. h->haveNumVariables
 *                                 and h->numVariables must already be set.
 *
 * Results:
 *      Returns TCL_OK if all variable lines are read and parsed successfully.
 *
 *      Returns TCL_ERROR if:
 *          - the Variables section appears before "No. Variables:" was parsed
 *          - the variable count is invalid or too large to allocate
 *          - EOF is encountered before all variable lines are read
 *          - a line cannot be read, decoded, or parsed
 *
 * Side Effects:
 *      - Allocates h->variables with Tcl_Alloc().
 *
 *      - Initializes the allocated RawVariable array with memset().
 *
 *      - Reads exactly h->numVariables decoded lines from chan.
 *
 *      - Fills h->variables[i] for each parsed variable entry.
 *
 *      - On error, sets the interpreter result with a human-readable message where
 *        appropriate.
 *
 * Notes:
 *      - The allocated h->variables array and the strings owned by each RawVariable
 *        are released by RawHeaderFree().
 *
 *      - This function assumes h has been initialized with RawHeaderInit() and that
 *        h->variables is not already populated. Calling it more than once on the
 *        same RawHeader without first freeing the previous variable array would leak
 *        memory.
 *
 *      - The function checks for allocation-size overflow before allocating the
 *        RawVariable array.
 *
 *      - Tcl_Alloc() is used for allocation, so the current implementation does not
 *        include a recoverable NULL-allocation path.
 *
 *      - If parsing fails after some variables have already been populated, the
 *        caller should clean up the partially filled header with RawHeaderFree().
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
 *      Determines the default numeric storage format for values in a raw-file data
 *      block from the parsed raw-header flags.
 *
 *      The raw header Flags line describes how numeric values are stored. This
 *      function interprets the recognized storage-related bits in h->flagsMask and
 *      stores the corresponding default value representation in the RawHeader:
 *
 *          RAW_FLAG_COMPLEX:
 *              RAW_VALUE_COMPLEX128, 16 bytes per value
 *
 *          RAW_FLAG_DOUBLE:
 *              RAW_VALUE_REAL64, 8 bytes per value
 *
 *          RAW_FLAG_REAL:
 *              RAW_VALUE_REAL32, 4 bytes per value
 *
 *      The resolved default is later used to initialize the effective per-variable
 *      storage layout.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      RawHeader *h             - Header structure whose flagsMask has already been
 *                                 filled by ParseFlags(). On success, its
 *                                 defaultStorage and defaultValueBytes fields are
 *                                 updated.
 *
 * Results:
 *      Returns TCL_OK if a recognized storage flag is present.
 *
 *      Returns TCL_ERROR if no recognized storage flag is found.
 *
 * Side Effects:
 *      - Updates h->defaultStorage.
 *
 *      - Updates h->defaultValueBytes.
 *
 *      - On failure, sets the interpreter result to:
 *
 *            "raw header has no recognized storage flag"
 *
 * Notes:
 *      - Complex storage takes precedence over double storage, and double storage
 *        takes precedence over real storage. This makes the function deterministic
 *        even if a malformed or unusual header contains more than one storage flag.
 *
 *      - This function resolves only the header-level default storage. Dialect-
 *        specific exceptions, such as storing one variable with a different width,
 *        should be handled later when resolving the per-variable layout.
 *
 *      - The byte sizes used here are:
 *
 *            real      -> 4 bytes
 *            double    -> 8 bytes
 *            complex   -> 16 bytes
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
 *      Resolves the effective per-variable binary storage layout for a parsed raw
 *      header.
 *
 *      The raw header Flags line describes the default numeric storage format for
 *      data values, for example:
 *
 *          real      -> 4 bytes per value
 *          double    -> 8 bytes per value
 *          complex   -> 16 bytes per value
 *
 *      This function first resolves that default storage format with
 *      ResolveDefaultStorage(), then applies it to every RawVariable in the header.
 *      While doing so, it assigns each variable a byte offset within one complete
 *      data point and computes the total point stride in bytes.
 *
 *      For example, with four real variables:
 *
 *          variable 0   offset 0    size 4
 *          variable 1   offset 4    size 4
 *          variable 2   offset 8    size 4
 *          variable 3   offset 12   size 4
 *
 *      The resulting h->pointStrideBytes would be 16.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      RawHeader *h             - Parsed header whose flags, variable count, and
 *                                 variables array have already been filled. On
 *                                 success, the function updates h->defaultStorage,
 *                                 h->defaultValueBytes, each RawVariable layout
 *                                 field, and h->pointStrideBytes.
 *
 * Results:
 *      Returns TCL_OK if the default storage is resolved and all variable offsets
 *      are assigned successfully.
 *
 *      Returns TCL_ERROR if:
 *          - ResolveDefaultStorage() fails because the header has no recognized
 *            storage flag
 *          - computing the point stride would overflow Tcl_Size
 *
 * Side Effects:
 *      - Calls ResolveDefaultStorage(), which updates:
 *
 *            h->defaultStorage
 *            h->defaultValueBytes
 *
 *      - For each variable in h->variables[], updates:
 *
 *            v->storage
 *            v->valueBytes
 *            v->offsetBytes
 *
 *      - Updates h->pointStrideBytes to the total number of bytes occupied by one
 *        complete data point.
 *
 *      - On stride overflow, sets the interpreter result to:
 *
 *            "raw point stride overflow"
 *
 * Notes:
 *      - The current implementation applies the same storage format to every
 *        variable. This matches the simple case where the raw header Flags line
 *        fully determines the layout.
 *
 *      - Dialect-specific exceptions can be added in the marked section. For
 *        example, if a simulator stores the scale variable with a different width
 *        from the trace variables, that variable can override v->storage and
 *        v->valueBytes before v->offsetBytes is assigned.
 *
 *      - The computed layout is later used to determine the binary block size:
 *
 *            totalBytes = h->numPoints * h->pointStrideBytes
 *
 *        and to locate a variable value inside one data point:
 *
 *            pointBytes + h->variables[varIndex].offsetBytes
 *
 *      - This function assumes h->variables points to an array containing
 *        h->numVariables initialized RawVariable entries.
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
 *      Computes the number of bytes occupied by the binary data block associated
 *      with a parsed raw header.
 *
 *      The binary block size is determined from the number of data points and the
 *      resolved per-point stride:
 *
 *          totalBytes = h->numPoints * h->pointStrideBytes
 *
 *      h->pointStrideBytes is expected to have been computed earlier by
 *      RawHeaderResolveVariableLayout(), based on the per-variable storage sizes
 *      implied by the raw-header Flags line and any dialect-specific layout rules.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for error reporting.
 *
 *      const RawHeader *h           - Parsed raw header. The header must already
 *                                     contain a valid No. Points value and a
 *                                     resolved pointStrideBytes value.
 *
 *      Tcl_Size *nbytesPtr          - Output location for the computed binary block
 *                                     size in bytes.
 *
 * Results:
 *      Returns TCL_OK if the byte count is computed successfully.
 *
 *      Returns TCL_ERROR if:
 *          - No. Points was not present in the header
 *          - numPoints or pointStrideBytes contains an invalid negative value
 *          - multiplying numPoints by pointStrideBytes would overflow Tcl_Size
 *
 * Side Effects:
 *      - On success, writes the computed byte count to *nbytesPtr.
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message:
 *
 *            "cannot compute binary size without No. Points"
 *            "invalid raw binary size fields"
 *            "raw binary byte count overflow"
 *
 * Notes:
 *      - This function does not read from the channel. It only computes how many
 *        bytes the caller should read for the following Binary: block.
 *
 *      - The function assumes h->pointStrideBytes has already been resolved from
 *        the variable layout. If RawHeaderResolveVariableLayout() has not been
 *        called, pointStrideBytes may still be zero and the computed size will be
 *        wrong.
 *
 *      - The overflow check is performed before multiplication to avoid wrapping
 *        the Tcl_Size result.
 *
 *      - For ASCII Values: blocks, this function is not the right size rule. Those
 *        blocks should be parsed according to the number of points, variables, and
 *        textual value format rather than read as a byte-counted binary block.
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
 *      Reads and parses one complete raw-file text header from the channel.
 *
 *      The channel is expected to be positioned at the start of a header section,
 *      usually at a line beginning with "Title:". Header lines are read in the
 *      previously detected text encoding, decoded to Tcl's UTF-8 representation,
 *      and dispatched by their leading key.
 *
 *      The function parses standard raw-header fields such as:
 *
 *          Title:
 *          Date:
 *          Plotname:
 *          Flags:
 *          No. Variables:
 *          No. Points:
 *          Variables:
 *
 *      Header parsing stops when either of the data-section markers is encountered:
 *
 *          Binary:
 *          Values:
 *
 *      The marker line itself is consumed. The channel is left positioned at the
 *      first byte of the following data block.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for error reporting.
 *
 *      Tcl_Channel chan             - Open input channel positioned at the start of
 *                                     a raw-file header. The channel is expected to
 *                                     be configured for binary translation.
 *
 *      EncKind kind                 - Detected raw header encoding kind:
 *                                          ENC_KIND_UTF8
 *                                          ENC_KIND_UTF16LE
 *
 *      Tcl_Encoding enc             - Tcl encoding handle used to decode raw header
 *                                     lines.
 *
 *      RawHeader *h                 - Output header structure. The function
 *                                     initializes it with RawHeaderInit() before
 *                                     parsing and fills it with the parsed header
 *                                     fields.
 *
 *      DataKind *dataKindPtr        - Output location receiving the type of data
 *                                     block that follows the header:
 *                                          DATA_BINARY
 *                                          DATA_VALUES
 *
 * Results:
 *      Returns RAW_HEADER_OK if a complete header is read and a data-section marker
 *      is found.
 *
 *      Returns RAW_HEADER_EOF if EOF is encountered before any header line is read.
 *      This represents clean end-of-file between raw plots.
 *
 *      Returns RAW_HEADER_ERROR if:
 *          - EOF occurs after a partial header has already been read
 *          - a header line cannot be read or decoded
 *          - a known header field cannot be parsed
 *          - the Variables section is malformed
 *          - the per-variable storage layout cannot be resolved
 *
 * Side Effects:
 *      - Initializes *h with RawHeaderInit().
 *
 *      - Repeatedly reads decoded header lines from chan with
 *        ReadDecodedHeaderLine().
 *
 *      - Stores owned copies of string fields in h:
 *
 *            h->title
 *            h->date
 *            h->plotname
 *
 *      - Parses and stores flags through ParseFlags(), updating:
 *
 *            h->flags
 *            h->numFlags
 *            h->flagsMask
 *
 *      - Parses numeric count fields:
 *
 *            h->numVariables
 *            h->haveNumVariables
 *            h->numPoints
 *            h->haveNumPoints
 *
 *      - When "Variables:" is encountered, calls ReadVariablesSection() to read
 *        and parse exactly h->numVariables variable-definition lines.
 *
 *      - When "Binary:" or "Values:" is encountered, calls
 *        RawHeaderResolveVariableLayout() to compute the effective per-variable
 *        storage layout and point stride.
 *
 *      - On success, leaves chan positioned immediately after the marker line, so
 *        the caller can read the associated binary or textual values block.
 *
 *      - On error, sets the interpreter result with a human-readable message where
 *        appropriate.
 *
 * Notes:
 *      - The returned RawHeader owns dynamically allocated memory. The caller must
 *        eventually call RawHeaderFree(h) after RAW_HEADER_OK.
 *
 *      - If RAW_HEADER_ERROR is returned after partial parsing, the caller should
 *        also call RawHeaderFree(h) to release any fields that were already stored.
 *
 *      - If RAW_HEADER_EOF is returned, no header was parsed. The function has still
 *        initialized *h, so calling RawHeaderFree(h) is harmless but not required.
 *
 *      - Unknown non-empty header lines are currently ignored. This allows the
 *        parser to tolerate simulator-specific raw-header extensions. If those
 *        fields need to be preserved, add an extra-fields collection to RawHeader
 *        and store them in the final else branch.
 *
 *      - Blank decoded lines are ignored.
 *
 *      - The function consumes the "Variables:" line and all variable-definition
 *        lines, but it does not consume any bytes from the Binary: or Values: data
 *        block itself.
 *
 *      - The function assumes that DetectEncoding() has already been called and
 *        that the returned encoding kind and Tcl_Encoding handle are valid for the
 *        header being parsed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ReadHeader(Tcl_Interp *interp, Tcl_Channel chan, EncKind kind, Tcl_Encoding enc, RawHeader *h,
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
        sawAnyLine = 1;
        line = Tcl_DStringValue(&utfLine);
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

//***  ReadLEFloat32AsDouble function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadLEFloat32AsDouble --
 *
 *      Reads a 32-bit little-endian IEEE-754 floating-point value from a raw byte
 *      buffer and returns it as a double.
 *
 *      This helper is used when decoding binary raw-file data for variables whose
 *      effective storage format is RAW_VALUE_REAL32. The four input bytes are first
 *      assembled into a uint32_t in little-endian order, then copied into a float
 *      object with memcpy(), and finally converted to double for the internal/API
 *      numeric representation.
 *
 * Parameters:
 *      const unsigned char *p   - Pointer to the first of four bytes containing the
 *                                 little-endian 32-bit floating-point value.
 *
 * Results:
 *      Returns the decoded value as a double.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      - The function assumes p points to at least four readable bytes.
 *
 *      - The byte order is interpreted explicitly as little-endian:
 *
 *            p[0] = least significant byte
 *            p[3] = most significant byte
 *
 *      - memcpy() is used to transfer the uint32_t bit pattern into the float object
 *        instead of pointer casting. This avoids strict-aliasing violations and
 *        alignment assumptions.
 *
 *      - The returned double preserves the numeric value of the decoded float, but
 *        it does not add precision beyond what was present in the original 32-bit
 *        value.
 *
 *      - For ngspice binary raw files, "Flags: real" is normally decoded as
 *        64-bit double storage, not this 32-bit format. This helper is still useful
 *        for dialects or modes where 32-bit real values are actually present.
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
 *      Reads a 64-bit little-endian IEEE-754 floating-point value from a raw byte
 *      buffer and returns it as a double.
 *
 *      This helper is used when decoding binary raw-file data for variables whose
 *      effective storage format is RAW_VALUE_REAL64, and for each component of
 *      RAW_VALUE_COMPLEX128 values. The eight input bytes are assembled into a
 *      uint64_t in little-endian order, then copied into a double object with
 *      memcpy().
 *
 * Parameters:
 *      const unsigned char *p   - Pointer to the first of eight bytes containing
 *                                 the little-endian 64-bit floating-point value.
 *
 * Results:
 *      Returns the decoded double value.
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      - The function assumes p points to at least eight readable bytes.
 *
 *      - The byte order is interpreted explicitly as little-endian:
 *
 *            p[0] = least significant byte
 *            p[7] = most significant byte
 *
 *      - memcpy() is used to transfer the uint64_t bit pattern into the double
 *        object instead of pointer casting. This avoids strict-aliasing violations
 *        and alignment assumptions.
 *
 *      - This function is appropriate for ngspice binary raw files where real
 *        values are stored as 64-bit doubles, and for complex values where the real
 *        and imaginary parts are stored as two consecutive 64-bit doubles.
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

//***  RawPlotInit function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotInit --
 *
 *      Initializes a RawPlot structure to an empty, well-defined state.
 *
 *      A RawPlot represents one raw-file plot section, consisting of one parsed
 *      header and the metadata needed to locate the associated data block in the
 *      file. This function clears all plot-level fields and initializes the embedded
 *      RawHeader.
 *
 * Parameters:
 *      RawPlot *p   - Pointer to the RawPlot structure to initialize.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Clears the entire RawPlot structure with memset(), setting pointer fields,
 *        offsets, counters, data-kind fields, and byte-size fields to zero.
 *
 *      - Calls RawHeaderInit() for the embedded p->header member.
 *
 * Notes:
 *      - This function should be called before filling a RawPlot during raw-file
 *        parsing.
 *
 *      - After initialization, the plot owns no allocated memory and can be safely
 *        passed to RawPlotFree().
 *
 *      - If RawPlot already owns allocated memory, call RawPlotFree() before calling
 *        RawPlotInit() to avoid losing ownership of that memory.
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
 *      Releases all memory owned by a RawPlot structure and resets the structure
 *      to an all-zero state.
 *
 *      A RawPlot owns an embedded RawHeader, which in turn owns dynamically
 *      allocated strings, flag arrays, and variable metadata. This function frees
 *      that embedded header and then clears the complete RawPlot structure.
 *
 * Parameters:
 *      RawPlot *p   - Pointer to the RawPlot structure to clean up.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Calls RawHeaderFree() on p->header, releasing all memory owned by the
 *        parsed header.
 *
 *      - Resets the complete RawPlot structure with memset(), so data-kind fields,
 *        file offsets, byte counts, and embedded header fields become zero/NULL.
 *
 * Notes:
 *      - The function assumes p has either been initialized with RawPlotInit() or
 *        contains a valid partially/fully populated RawPlot.
 *
 *      - After this function returns, the RawPlot is empty and may be safely reused
 *        or passed to RawPlotFree() again.
 *
 *      - If RawPlot later gains additional owned fields, such as an ASCII point
 *        offset index for Values: blocks, those fields should be released here
 *        before the final memset().
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static void RawPlotFree(RawPlot *p) {
    RawHeaderFree(&p->header);
    memset(p, 0, sizeof *p);
}

//***  RawPlotMove function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotMove --
 *
 *      Transfers ownership of a RawPlot structure from one location to another.
 *
 *      This helper performs a shallow structure assignment from src to dst, then
 *      reinitializes src to an empty state. It is intended for moving parsed plot
 *      objects into arrays or containers without duplicating the memory owned by
 *      the embedded RawHeader.
 *
 * Parameters:
 *      RawPlot *dst   - Destination RawPlot structure that receives ownership of
 *                       the plot data from src.
 *
 *      RawPlot *src   - Source RawPlot structure whose contents are moved into dst.
 *                       After the move, src is reset with RawPlotInit().
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Copies the RawPlot structure fields from src to dst with structure
 *        assignment.
 *
 *      - Transfers ownership of all memory reachable from src, including memory
 *        owned by src->header, to dst.
 *
 *      - Reinitializes src with RawPlotInit(), so src no longer owns the moved
 *        header fields or data-block metadata.
 *
 * Notes:
 *      - This is a move operation, not a deep copy. After this function returns,
 *        dst owns the resources previously owned by src.
 *
 *      - Do not call RawPlotFree(src) to release the moved resources after this
 *        function; they now belong to dst.
 *
 *      - dst should not already own allocated resources unless they have been freed
 *        before calling this function. Otherwise, the assignment would overwrite
 *        dst's previous ownership and leak memory.
 *
 *      - This helper is useful when appending a temporary RawPlot into a RawFile
 *        plot array.
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
 *      Transfers ownership of a RawHeader structure from one location to another.
 *
 *      This helper performs a shallow structure assignment from src to dst, then
 *      reinitializes src to an empty state. It is intended for moving parsed header
 *      objects into another owning structure, such as a RawPlot, without duplicating
 *      the dynamically allocated fields owned by the header.
 *
 * Parameters:
 *      RawHeader *dst   - Destination RawHeader structure that receives ownership
 *                         of the header data from src.
 *
 *      RawHeader *src   - Source RawHeader structure whose contents are moved into
 *                         dst. After the move, src is reset with RawHeaderInit().
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Copies the RawHeader structure fields from src to dst with structure
 *        assignment.
 *
 *      - Transfers ownership of all dynamically allocated fields reachable from src,
 *        including:
 *
 *            src->title
 *            src->date
 *            src->plotname
 *            src->flags
 *            src->variables
 *
 *      - Reinitializes src with RawHeaderInit(), so src no longer owns the moved
 *        fields.
 *
 * Notes:
 *      - This is a move operation, not a deep copy. After this function returns,
 *        dst owns the resources previously owned by src.
 *
 *      - Do not call RawHeaderFree(src) to release the moved resources after this
 *        function; they now belong to dst.
 *
 *      - dst should not already own allocated resources unless they have been freed
 *        before calling this function. Otherwise, the assignment would overwrite
 *        dst's previous ownership and leak memory.
 *
 *      - This helper is useful when transferring a temporary RawHeader parsed by
 *        ReadHeader() into a RawPlot.
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
 *      Appends a RawPlot to the dynamic plot array owned by a RawFile, transferring
 *      ownership from the supplied temporary plot.
 *
 *      This helper is used while opening a raw file with one or more plot sections.
 *      Each parsed header/data-block pair is first assembled in a temporary RawPlot,
 *      then moved into rf->plots[] with this function.
 *
 * Parameters:
 *      Tcl_Interp *interp   - Interpreter used for error reporting.
 *
 *      RawFile *rf          - Raw file handle whose plot array is extended. The
 *                             structure owns rf->plots and tracks its current length
 *                             and capacity through rf->numPlots and
 *                             rf->plotCapacity.
 *
 *      RawPlot *plot        - Temporary RawPlot to append. On success, ownership of
 *                             all resources owned by plot is transferred into
 *                             rf->plots[rf->numPlots], and plot is reset to an
 *                             empty state by RawPlotMove().
 *
 * Results:
 *      Returns TCL_OK if the plot is appended successfully.
 *
 *      Returns TCL_ERROR if:
 *          - growing the plot array would overflow size_t
 *          - the plot array cannot be reallocated
 *
 * Side Effects:
 *      - May grow rf->plots with Tcl_Realloc().
 *
 *      - Updates rf->plotCapacity if the array is resized.
 *
 *      - Moves the supplied RawPlot into rf->plots[rf->numPlots].
 *
 *      - Increments rf->numPlots after a successful move.
 *
 *      - On allocation failure, sets the interpreter result with a human-readable
 *        error message.
 *
 * Notes:
 *      - This function performs a move, not a deep copy. After a successful call,
 *        the RawFile owns the resources previously owned by plot.
 *
 *      - Do not call RawPlotFree(plot) to release the appended plot after success;
 *        the moved resources now belong to rf->plots[].
 *
 *      - If the function returns TCL_ERROR, ownership of plot is unchanged and the
 *        caller remains responsible for freeing it with RawPlotFree().
 *
 *      - The initial capacity is four plots. Later growth doubles the previous
 *        capacity.
 *
 *      - The function assumes rf has been initialized so that rf->plots is either
 *        NULL or a Tcl-allocated array compatible with Tcl_Realloc().
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

//***  ReadExact function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * ReadExact --
 *
 *      Reads exactly the requested number of bytes from a Tcl channel into a caller-
 *      supplied buffer.
 *
 *      This helper is used when decoding Binary: raw-file data, where the parser
 *      already knows the exact byte count to read from the header-derived layout.
 *      The function repeatedly calls Tcl_Read() until either nbytes bytes have been
 *      read, an input error occurs, or EOF is encountered before the requested byte
 *      count is satisfied.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      Tcl_Channel chan         - Open input channel to read from. The channel is
 *                                 expected to be configured for binary translation
 *                                 when reading raw binary data.
 *
 *      unsigned char *buf       - Destination buffer. The buffer must have space for
 *                                 at least nbytes bytes.
 *
 *      Tcl_Size nbytes          - Exact number of bytes to read.
 *
 * Results:
 *      Returns TCL_OK if exactly nbytes bytes are read successfully.
 *
 *      Returns TCL_ERROR if:
 *          - Tcl_Read() reports an input error
 *          - EOF is reached before nbytes bytes have been read
 *
 * Side Effects:
 *      - Advances the channel position by nbytes bytes on success.
 *
 *      - Writes the bytes read from chan into buf.
 *
 *      - On failure, sets the interpreter result with one of:
 *
 *            "failed while reading raw binary data"
 *            "unexpected EOF while reading raw binary data"
 *
 * Notes:
 *      - This function does not allocate memory. The caller owns buf.
 *
 *      - A short read is not treated as success unless it is followed by additional
 *        successful reads that complete the requested byte count.
 *
 *      - If nbytes is zero, the function performs no reads and returns TCL_OK.
 *
 *      - The function assumes nbytes is non-negative and that buf points to a valid
 *        writable memory region of at least nbytes bytes.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int ReadExact(Tcl_Interp *interp, Tcl_Channel chan, unsigned char *buf, Tcl_Size nbytes) {
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

//***  RawAppendDecodedValue function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawAppendDecodedValue --
 *
 *      Decodes one raw binary value from a byte buffer and appends the corresponding
 *      Tcl object representation to a Tcl list.
 *
 *      The interpretation of the bytes is selected by the supplied RawValueStorage
 *      value:
 *
 *          RAW_VALUE_REAL32:
 *              reads one little-endian 32-bit floating-point value and appends it
 *              as a Tcl double object
 *
 *          RAW_VALUE_REAL64:
 *              reads one little-endian 64-bit floating-point value and appends it
 *              as a Tcl double object
 *
 *          RAW_VALUE_COMPLEX128:
 *              reads two consecutive little-endian 64-bit floating-point values and
 *              appends a two-element Tcl list containing:
 *
 *                  {real imag}
 *
 *      This helper is used by vector and dataset conversion routines when producing
 *      Tcl list results on demand from the file-backed raw data block.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for list append operations
 *                                     and for error reporting.
 *
 *      Tcl_Obj *listObj             - Tcl list object to which the decoded value is
 *                                     appended.
 *
 *      RawValueStorage storage      - Effective binary storage format of the value:
 *                                          RAW_VALUE_REAL32
 *                                          RAW_VALUE_REAL64
 *                                          RAW_VALUE_COMPLEX128
 *
 *      const unsigned char *p       - Pointer to the first byte of the encoded value
 *                                     inside a raw binary data buffer.
 *
 * Results:
 *      Returns TCL_OK if the value is decoded and appended successfully.
 *
 *      Returns TCL_ERROR if storage is not a recognized RawValueStorage value.
 *
 * Side Effects:
 *      - Appends one element to listObj.
 *
 *      - For real values, the appended element is a Tcl double object.
 *
 *      - For complex values, the appended element is a two-element Tcl list:
 *
 *            {real imag}
 *
 *      - On unknown storage type, sets the interpreter result to:
 *
 *            "unknown raw value storage type"
 *
 * Notes:
 *      - The function assumes p points to enough readable bytes for the selected
 *        storage format:
 *
 *            RAW_VALUE_REAL32      -> 4 bytes
 *            RAW_VALUE_REAL64      -> 8 bytes
 *            RAW_VALUE_COMPLEX128  -> 16 bytes
 *
 *      - The byte order is interpreted as little-endian by ReadLEFloat32AsDouble()
 *        and ReadLEFloat64().
 *
 *      - The function does not advance p. The caller is responsible for computing
 *        the correct address of each value using the point stride and variable
 *        offset stored in the RawHeader/RawVariable layout.
 *
 *      - For complex values, p points to the real component and p + 8 points to the
 *        imaginary component.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawAppendDecodedValue(Tcl_Interp *interp, Tcl_Obj *listObj, RawValueStorage storage,
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

//***  RawParseRange function
//***  RawParseRange function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawParseRange --
 *
 *      Parses optional point-range arguments for raw vector or dataset extraction.
 *
 *      The supported options are:
 *
 *          -from index
 *              First point index to read.
 *
 *          -count count
 *              Number of points to read.
 *
 *      If "-from" is omitted, reading starts from point 0.
 *
 *      If "-count" is omitted, the count is computed after parsing "-from" and
 *      extends to the end of the selected plot:
 *
 *          count = plot->header.numPoints - from
 *
 *      Therefore:
 *
 *          no options
 *              reads the complete plot
 *
 *          -from N
 *              reads from point N to the end of the plot
 *
 *          -count M
 *              reads M points starting from point 0
 *
 *          -from N -count M
 *              reads M points starting from point N
 *
 *      The parsed range is validated against the number of points stored in the
 *      selected RawPlot header.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for numeric conversion and error
 *                                 reporting.
 *
 *      RawPlot *plot            - Selected plot whose header supplies the valid
 *                                 point range through plot->header.numPoints.
 *
 *      Tcl_Size objc            - Number of Tcl command arguments in objv.
 *
 *      Tcl_Obj *const objv[]    - Tcl command argument objects containing optional
 *                                 range switches and their values.
 *
 *      Tcl_Size firstOpt        - Index in objv where range-option parsing should
 *                                 begin.
 *
 *      Tcl_Size *fromPtr        - Output location for the parsed first point index.
 *
 *      Tcl_Size *countPtr       - Output location for the parsed point count.
 *
 * Results:
 *      Returns TCL_OK if the range options are parsed and validated successfully.
 *
 *      Returns TCL_ERROR if:
 *          - an option is missing its value
 *          - an option value cannot be parsed as an integer
 *          - an option value is negative
 *          - an unknown option is encountered
 *          - -from is outside the available point range
 *          - -count extends beyond the end of the selected plot
 *
 * Side Effects:
 *      - On success, writes the parsed range to *fromPtr and *countPtr.
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message where appropriate.
 *
 * Notes:
 *      - Options are parsed as option/value pairs starting at firstOpt, so the
 *        number of remaining arguments must be even.
 *
 *      - The function currently accepts only "-from" and "-count".
 *
 *      - The range is half-open in effect:
 *
 *            [from, from + count)
 *
 *        so a count of zero is valid, and from may be exactly equal to
 *        h->numPoints only when count is zero.
 *
 *      - A separate countSet flag is used to distinguish an omitted "-count" from
 *        an explicit "-count 0". This allows "-from N" to mean "from N to the end"
 *        while still allowing callers to request an empty range explicitly.
 *
 *      - Numeric values are parsed with Tcl_GetWideIntFromObj(), then cast to
 *        Tcl_Size after checking that they are non-negative.
 *
 *      - The expression h->numPoints - from is used only after confirming that from
 *        is not greater than h->numPoints, avoiding underflow in the range check.
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

//***  RawPlotFindVariable function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotFindVariable --
 *
 *      Finds a variable in a RawPlot by its raw-header variable name.
 *
 *      This helper searches the variable table stored in plot->header.variables[]
 *      and returns the array index of the first variable whose name exactly matches
 *      the supplied name. It is used by Tcl-facing commands such as:
 *
 *          $handle vector ?-plot index? name
 *
 *      to translate a variable name into the internal variable index needed for
 *      binary data extraction.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      RawPlot *plot            - Plot whose header variable table is searched.
 *
 *      const char *name         - NUL-terminated variable name to find.
 *
 *      Tcl_Size *indexPtr       - Output location for the matching variable index.
 *
 * Results:
 *      Returns TCL_OK if a matching variable name is found.
 *
 *      Returns TCL_ERROR if no matching variable exists in the selected plot.
 *
 * Side Effects:
 *      - On success, writes the matching variable array index to *indexPtr.
 *
 *      - On failure, sets the interpreter result to an error message of the form:
 *
 *            raw vector "name" not found
 *
 * Notes:
 *      - The comparison is byte-wise and case-sensitive.
 *
 *      - The returned index is the position in h->variables[], not necessarily the
 *        same value as h->variables[index].index if the file contains unusual or
 *        non-sequential variable indices.
 *
 *      - If duplicate variable names are present, the first matching entry is
 *        returned.
 *
 *      - The function assumes plot->header.variables points to an initialized
 *        array containing plot->header.numVariables entries.
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

//***  RawPlotVectorToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotVectorToObj --
 *
 *      Reads one variable vector from a Binary: raw-file plot and converts the
 *      requested point range into a Tcl list object.
 *
 *      The plot is assumed to use point-major binary storage, where each data point
 *      contains all variables in header order:
 *
 *          point 0: var0 var1 var2 ...
 *          point 1: var0 var1 var2 ...
 *          point 2: var0 var1 var2 ...
 *
 *      The function reads the selected range in chunks, extracts the requested
 *      variable from each point using the variable's byte offset within the point,
 *      decodes each value according to its RawValueStorage type, and appends the
 *      Tcl representation to the result list.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting and Tcl
 *                                 object creation.
 *
 *      RawFile *rf              - Raw file handle owning the open channel from which
 *                                 binary data is read.
 *
 *      RawPlot *plot            - Selected plot containing the parsed header and
 *                                 Binary: block file offsets.
 *
 *      Tcl_Size varIndex        - Index of the variable to extract from
 *                                 plot->header.variables[].
 *
 *      Tcl_Size firstPoint      - First point index to read.
 *
 *      Tcl_Size count           - Number of points to read.
 *
 *      Tcl_Obj **objPtr         - Output location for the newly created Tcl list
 *                                 object containing the decoded vector values.
 *
 * Results:
 *      Returns TCL_OK if the requested vector range is read and converted
 *      successfully.
 *
 *      Returns TCL_ERROR if:
 *          - the selected plot is not a Binary: plot
 *          - varIndex is outside the variable array
 *          - the requested point range is invalid
 *          - the resolved point stride is invalid
 *          - the chunk byte count would overflow Tcl_Size
 *          - seeking inside the raw binary data fails
 *          - reading binary data fails
 *          - a value cannot be decoded because its storage type is unknown
 *
 * Side Effects:
 *      - Allocates a temporary chunk buffer with Tcl_Alloc().
 *
 *      - Seeks rf->chan to the relevant binary block offsets while reading chunks.
 *
 *      - Reads binary data from rf->chan with ReadExact().
 *
 *      - Creates a Tcl list object containing one element per requested point.
 *
 *      - For real variables, each list element is a Tcl double object.
 *
 *      - For complex variables, each list element is a two-element Tcl list:
 *
 *            {real imag}
 *
 *      - On success, stores the created list object in *objPtr.
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message where appropriate.
 *
 * Notes:
 *      - The function is lazy with respect to the raw file: it does not require the
 *        whole data block to be decoded or stored in memory before a single vector
 *        is requested.
 *
 *      - Data is read in chunks of approximately 1 MiB. The chunk size is converted
 *        to a whole number of points using h->pointStrideBytes.
 *
 *      - The byte address for each chunk is computed as:
 *
 *            plot->dataOffset
 *              + (firstPoint + done) * h->pointStrideBytes
 *
 *      - The byte address for the selected variable within a point is computed as:
 *
 *            pointBytes + var->offsetBytes
 *
 *      - The function assumes that RawHeaderResolveVariableLayout() has already
 *        populated h->pointStrideBytes and each variable's offsetBytes, valueBytes,
 *        and storage fields.
 *
 *      - The returned Tcl object is owned by the caller in the normal Tcl result
 *        sense. The function does not increment its reference count.
 *
 *      - If count is zero, the function returns an empty Tcl list without reading
 *        from the file.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotVectorToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size varIndex, Tcl_Size firstPoint,
                              Tcl_Size count, Tcl_Obj **objPtr) {
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
        if (ReadExact(interp, rf->chan, buf, thisBytes) != TCL_OK) {
            Tcl_Free((char *)buf);
            return TCL_ERROR;
        }
        for (Tcl_Size i = 0; i < thisPoints; i++) {
            const unsigned char *p = buf + i * h->pointStrideBytes + var->offsetBytes;
            if (RawAppendDecodedValue(interp, listObj, var->storage, p) != TCL_OK) {
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

//***  RawPlotDictToObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawPlotDictToObj --
 *
 *      Reads all variable vectors from a Binary: raw-file plot over the requested
 *      point range and converts them into a Tcl dictionary object.
 *
 *      The resulting dictionary uses raw variable names as keys. Each value is a
 *      Tcl list containing the decoded values for that variable:
 *
 *          variableName -> {value0 value1 value2 ...}
 *
 *      For complex variables, each vector element is represented as a two-element
 *      Tcl list:
 *
 *          {real imag}
 *
 *      The function reads the binary data in point-major chunks. For each point in
 *      each chunk, it decodes every variable using the per-variable offset and
 *      storage layout resolved from the raw header.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting and Tcl
 *                                 object creation.
 *
 *      RawFile *rf              - Raw file handle owning the open channel from which
 *                                 binary data is read.
 *
 *      RawPlot *plot            - Selected plot containing the parsed header and
 *                                 Binary: block file offsets.
 *
 *      Tcl_Size firstPoint      - First point index to read.
 *
 *      Tcl_Size count           - Number of points to read.
 *
 *      Tcl_Obj **objPtr         - Output location for the newly created Tcl
 *                                 dictionary object.
 *
 * Results:
 *      Returns TCL_OK if the requested point range is read and converted into a
 *      dictionary successfully.
 *
 *      Returns TCL_ERROR if:
 *          - the selected plot is not a Binary: plot
 *          - the requested point range is invalid
 *          - the resolved point stride is invalid
 *          - the vector-object pointer array would overflow size_t
 *          - the chunk byte count would overflow Tcl_Size
 *          - seeking inside the raw binary data fails
 *          - reading binary data fails
 *          - a value cannot be decoded because its storage type is unknown
 *
 * Side Effects:
 *      - Allocates an array of Tcl_Obj * pointers, one per variable.
 *
 *      - Creates one Tcl list object for each variable and temporarily increments
 *        its reference count while values are appended.
 *
 *      - Allocates a temporary binary chunk buffer with Tcl_Alloc().
 *
 *      - Seeks rf->chan to the relevant binary block offsets while reading chunks.
 *
 *      - Reads binary data from rf->chan with ReadExact().
 *
 *      - Appends decoded values to the per-variable Tcl lists.
 *
 *      - Creates a Tcl dictionary mapping variable names to their decoded value
 *        lists.
 *
 *      - On success, stores the created dictionary object in *objPtr.
 *
 *      - On failure, releases temporary buffers and decrements references to any
 *        partially built vector lists before returning.
 *
 * Notes:
 *      - The function is intended as a convenience conversion for Tcl callers that
 *        want all vectors from a selected plot or range. For very large raw files,
 *        this can create a very large Tcl object and use substantially more memory
 *        than reading a single vector with RawPlotVectorToObj().
 *
 *      - Data is read in chunks of approximately 1 MiB. The chunk size is converted
 *        to a whole number of points using h->pointStrideBytes.
 *
 *      - The byte address for each chunk is computed as:
 *
 *            plot->dataOffset
 *              + (firstPoint + done) * h->pointStrideBytes
 *
 *      - The byte address for each variable inside a point is computed as:
 *
 *            pointBytes + var->offsetBytes
 *
 *      - The function assumes that RawHeaderResolveVariableLayout() has already
 *        populated h->pointStrideBytes and each variable's offsetBytes, valueBytes,
 *        and storage fields.
 *
 *      - If count is zero, the function returns a dictionary containing all variable
 *        names, each mapped to an empty Tcl list.
 *
 *      - The returned Tcl object is owned by the caller in the normal Tcl result
 *        sense. The function does not increment the dictionary object's reference
 *        count before returning it through objPtr.
 *
 *      - Tcl_DictObjPut() is called while constructing the final dictionary. The
 *        current implementation assumes those calls succeed.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
static int RawPlotDictToObj(Tcl_Interp *interp, RawFile *rf, RawPlot *plot, Tcl_Size firstPoint, Tcl_Size count,
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
        if (ReadExact(interp, rf->chan, buf, thisBytes) != TCL_OK) {
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
                if (RawAppendDecodedValue(interp, vecObjs[varIndex], var->storage, p) != TCL_OK) {
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

//***  RawDataKindName function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawDataKindName --
 *
 *      Returns a human-readable name for a DataKind value.
 *
 *      This helper converts the internal data-block kind enum used by the raw-file
 *      parser into a short string suitable for Tcl-facing metadata dictionaries,
 *      debug output, and plot summaries.
 *
 * Parameters:
 *      DataKind kind   - Data block kind to convert:
 *
 *                            DATA_BINARY
 *                            DATA_VALUES
 *
 * Results:
 *      Returns a pointer to a static string describing the supplied kind:
 *
 *          DATA_BINARY   -> "binary"
 *          DATA_VALUES   -> "values"
 *          otherwise     -> "unknown"
 *
 * Side Effects:
 *      None.
 *
 * Notes:
 *      - The returned string is static storage and must not be freed or modified by
 *        the caller.
 *
 *      - Unknown or uninitialized DataKind values are mapped to "unknown" rather
 *        than treated as an error.
 *
 *      - The returned strings are lower-case because they are intended for Tcl API
 *        results such as plot summary dictionaries.
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
 *      Builds a Tcl dictionary containing a compact summary of one RawPlot.
 *
 *      This helper is used by Tcl-facing plot-list commands, such as:
 *
 *          $handle plots
 *
 *      to report the available plot sections in an opened raw file without
 *      returning the full header or any waveform data.
 *
 * Parameters:
 *      const RawPlot *plot     - Plot whose metadata should be summarized.
 *
 *      Tcl_Size index          - Plot index to store in the summary dictionary.
 *
 * Results:
 *      Returns a newly created Tcl dictionary object containing the following keys:
 *
 *          index        - Plot index in the RawFile plot array.
 *          kind         - Data block kind, such as "binary" or "values".
 *          title        - Header title string, or an empty string if absent.
 *          plotname     - Header plot name string, or an empty string if absent.
 *          nvariables   - Number of variables in the plot.
 *          npoints      - Number of points in the plot.
 *          dataoffset   - File offset where the plot's data block begins.
 *          databytes    - Size of the binary data block in bytes.
 *
 * Side Effects:
 *      - Allocates a Tcl dictionary object and the Tcl objects stored inside it.
 *
 *      - Does not modify plot or its embedded header.
 *
 * Notes:
 *      - The returned Tcl object is not reference-counted by this function. The
 *        caller receives it in the normal Tcl object ownership sense and may place
 *        it directly in an interpreter result or append it to another Tcl object.
 *
 *      - The data block kind is converted with RawDataKindName().
 *
 *      - For non-binary Values: plots, databytes may be zero or otherwise not
 *        meaningful unless the parser later records a textual block byte length.
 *
 *      - Tcl_DictObjPut() is called with a NULL interpreter. The current
 *        implementation assumes these dictionary insertions succeed.
 *
 *      - This function intentionally returns only a compact summary. Full header
 *        details, including flags and variable metadata, are returned separately by
 *        RawHeaderToDictObj().
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

//***  RawHeaderToDictObj function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawHeaderToDictObj --
 *
 *      Builds a Tcl dictionary containing the parsed metadata from a RawHeader.
 *
 *      This helper converts the internal raw-header representation into a Tcl-facing
 *      object suitable for commands such as:
 *
 *          $handle header ?-plot index?
 *
 *      The returned dictionary contains general plot metadata, the parsed Flags
 *      list, and one dictionary for each variable described by the Variables
 *      section.
 *
 * Parameters:
 *      const RawHeader *h   - Parsed raw header to convert into a Tcl dictionary.
 *
 * Results:
 *      Returns a newly created Tcl dictionary object containing the following
 *      top-level keys:
 *
 *          title        - Header title string, or an empty string if absent.
 *          date         - Header date string, or an empty string if absent.
 *          plotname     - Header plot name string, or an empty string if absent.
 *          nvariables   - Number of variables in the plot.
 *          npoints      - Number of points in the plot.
 *          pointstride  - Number of bytes occupied by one complete binary point.
 *          flags        - List of parsed raw-header flag strings.
 *          variables    - List of variable metadata dictionaries.
 *
 *      Each element of the variables list is a dictionary containing:
 *
 *          index        - Variable index parsed from the Variables section.
 *          name         - Variable name.
 *          type         - Variable type string.
 *          storage      - Effective storage name:
 *
 *                            "real32"
 *                            "real64"
 *                            "complex128"
 *                            "unknown"
 *
 *          valuebytes   - Number of bytes occupied by this variable in one point.
 *          offsetbytes  - Byte offset of this variable inside one point.
 *
 * Side Effects:
 *      - Allocates a Tcl dictionary object for the header.
 *
 *      - Allocates Tcl list objects for the flags and variables collections.
 *
 *      - Allocates one Tcl dictionary object for each variable entry.
 *
 *      - Does not modify the RawHeader or any memory owned by it.
 *
 * Notes:
 *      - The returned Tcl object is not reference-counted by this function. The
 *        caller receives it in the normal Tcl object ownership sense and may place
 *        it directly in an interpreter result or append it to another Tcl object.
 *
 *      - Missing string fields are represented as empty strings rather than omitted
 *        dictionary keys.
 *
 *      - The variable storage string is derived from each RawVariable's resolved
 *        storage field, not directly from the original Flags line.
 *
 *      - The pointstride, valuebytes, offsetbytes, and storage fields are meaningful
 *        only after RawHeaderResolveVariableLayout() has been called for the header.
 *
 *      - Tcl_DictObjPut() and Tcl_ListObjAppendElement() are called with a NULL
 *        interpreter. The current implementation assumes these object construction
 *        operations succeed.
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

//***  RawFileDeleteProc function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileDeleteProc --
 *
 *      Releases all resources owned by a RawFile handle when its Tcl command is
 *      deleted.
 *
 *      This function is registered as the delete callback for the Tcl command
 *      created by raw::open. It closes the underlying file channel, releases the
 *      Tcl encoding handle, frees every parsed RawPlot stored in the file handle,
 *      releases the plot array itself, and finally frees the RawFile structure.
 *
 * Parameters:
 *      void *clientData    - Client data pointer supplied when the Tcl handle
 *                            command was created. It is expected to point to a
 *                            RawFile structure.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      - Closes rf->chan with Tcl_Close() if the channel is still open.
 *
 *      - Releases rf->enc with Tcl_FreeEncoding() if an encoding handle is stored.
 *
 *      - Calls RawPlotFree() for each plot in rf->plots[].
 *
 *      - Frees the rf->plots array with Tcl_Free().
 *
 *      - Frees the RawFile structure itself with Tcl_Free().
 *
 * Notes:
 *      - This function is called automatically when the Tcl handle command is
 *        deleted, for example by:
 *
 *            $handle close
 *
 *        or when the interpreter deletes the command during cleanup.
 *
 *      - Tcl_Close() is called with a NULL interpreter because this is a cleanup
 *        callback and there is no useful command result to report.
 *
 *      - The function assumes rf was allocated with Tcl_Alloc() and that rf->plots,
 *        if non-NULL, was allocated with Tcl_Alloc() or Tcl_Realloc().
 *
 *      - Each RawPlot owns its embedded RawHeader, so RawPlotFree() is responsible
 *        for releasing parsed header strings, flags, and variable metadata.
 *
 *      - After this function returns, the RawFile pointer is invalid and must not be
 *        used again.
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
 *      Parses and validates a Tcl plot-index argument for a RawFile handle.
 *
 *      This helper converts a Tcl object to an integer plot index and checks that
 *      the index refers to an existing RawPlot in the RawFile plot array. It is used
 *      when processing Tcl-facing commands that accept a plot selector, for example:
 *
 *          $handle header -plot 1
 *          $handle names -plot 1
 *          $handle vector -plot 1 v(out)
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter used for numeric conversion and
 *                                     error reporting.
 *
 *      RawFile *rf                  - Raw file handle whose plot array defines the
 *                                     valid index range.
 *
 *      Tcl_Obj *obj                 - Tcl object containing the plot index value to
 *                                     parse.
 *
 *      Tcl_Size *plotIndexPtr       - Output location for the validated plot index.
 *
 * Results:
 *      Returns TCL_OK if obj is parsed successfully and refers to an existing plot.
 *
 *      Returns TCL_ERROR if:
 *          - obj cannot be parsed as an integer
 *          - the parsed value is negative
 *          - the parsed value is greater than or equal to rf->numPlots
 *
 * Side Effects:
 *      - On success, writes the validated plot index to *plotIndexPtr.
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message where appropriate.
 *
 * Notes:
 *      - Numeric parsing is performed with Tcl_GetWideIntFromObj(), so invalid Tcl
 *        integer syntax is reported through the normal Tcl conversion mechanism.
 *
 *      - The validated index is an array index into rf->plots[], not a value stored
 *        in the raw file itself.
 *
 *      - The function assumes rf points to an initialized RawFile and that
 *        rf->numPlots accurately describes the number of populated entries in
 *        rf->plots[].
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

//***  RawSelectPlotFromArgs function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawSelectPlotFromArgs --
 *
 *      Selects a RawPlot from a RawFile by parsing an optional "-plot index"
 *      argument pair.
 *
 *      This helper is used by Tcl-facing handle subcommands that operate on one
 *      plot, for example:
 *
 *          $handle header ?-plot index?
 *          $handle names ?-plot index?
 *          $handle vector ?-plot index? name ?options?
 *          $handle dict ?-plot index? ?options?
 *
 *      If the "-plot" option is not present at firstOpt, plot 0 is selected by
 *      default and argument parsing continues at firstOpt.
 *
 * Parameters:
 *      Tcl_Interp *interp       - Interpreter used for error reporting.
 *
 *      RawFile *rf              - Raw file handle whose plot array is searched.
 *
 *      Tcl_Size objc            - Number of Tcl command arguments in objv.
 *
 *      Tcl_Obj *const objv[]    - Tcl command argument objects.
 *
 *      Tcl_Size firstOpt        - Index in objv where an optional "-plot index"
 *                                 pair may begin.
 *
 *      RawPlot **plotPtr        - Output location for the selected RawPlot pointer.
 *
 *      Tcl_Size *nextArgPtr     - Output location for the next unconsumed argument
 *                                 index after the optional plot selector.
 *
 * Results:
 *      Returns TCL_OK if a plot is selected successfully.
 *
 *      Returns TCL_ERROR if:
 *          - "-plot" is present but has no following value
 *          - the supplied plot index cannot be parsed or is out of range
 *          - the RawFile contains no plots
 *
 * Side Effects:
 *      - On success, writes the selected plot pointer to *plotPtr.
 *
 *      - On success, writes the next unconsumed argument index to *nextArgPtr.
 *
 *      - On failure, sets the interpreter result with a human-readable error
 *        message where appropriate.
 *
 * Notes:
 *      - The "-plot" option is recognized only if it appears exactly at firstOpt.
 *        This keeps command parsing simple and predictable.
 *
 *      - If "-plot" is omitted, plot index 0 is selected by default.
 *
 *      - The returned RawPlot pointer is borrowed from rf->plots[] and must not be
 *        freed by the caller.
 *
 *      - The caller should continue parsing command-specific arguments starting at
 *        *nextArgPtr.
 *
 *      - Plot index validation is delegated to RawParsePlotIndex().
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

//***  RawFileObjCmd function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawFileObjCmd --
 *
 *      Implements the Tcl command associated with an opened raw-file handle.
 *
 *      A RawFile handle command is created by raw::openraw and stores a pointer to the
 *      corresponding RawFile structure in its ClientData. This function dispatches
 *      handle subcommands such as:
 *
 *          $handle close
 *          $handle plots
 *          $handle header ?-plot index?
 *          $handle names ?-plot index?
 *          $handle npoints ?-plot index?
 *          $handle vector ?-plot index? name ?-from index? ?-count count?
 *          $handle dict ?-plot index? ?-from index? ?-count count?
 *
 *      Metadata subcommands return information from the parsed RawPlot/RawHeader
 *      structures. Data subcommands lazily read the selected Binary: plot from the
 *      file and convert the requested values into Tcl objects.
 *
 * Parameters:
 *      void *clientData        - Client data pointer supplied when the Tcl handle
 *                                command was created. It is expected to point to a
 *                                RawFile structure.
 *
 *      Tcl_Interp *interp      - Interpreter used for result objects and error
 *                                reporting.
 *
 *      Tcl_Size objc           - Number of Tcl command arguments in objv.
 *
 *      Tcl_Obj *const objv[]   - Tcl command argument objects. objv[0] is the
 *                                handle command name and objv[1] is the subcommand
 *                                name.
 *
 * Results:
 *      Returns TCL_OK if the subcommand is recognized and completes successfully.
 *
 *      Returns TCL_ERROR if:
 *          - no subcommand is supplied
 *          - the subcommand has the wrong number of arguments
 *          - a plot selector is invalid
 *          - a requested variable name is not found
 *          - a requested point range is invalid
 *          - reading or decoding raw binary data fails
 *          - the subcommand name is unknown
 *
 * Side Effects:
 *      - For "close", deletes the Tcl handle command with
 *        Tcl_DeleteCommandFromToken(). This triggers RawFileDeleteProc(), which
 *        closes the channel and frees the RawFile.
 *
 *      - For "plots", sets the interpreter result to a list of compact plot summary
 *        dictionaries.
 *
 *      - For "header", sets the interpreter result to a dictionary containing the
 *        selected plot's parsed header metadata.
 *
 *      - For "names", sets the interpreter result to a list of variable names from
 *        the selected plot.
 *
 *      - For "npoints", sets the interpreter result to the number of points in the
 *        selected plot.
 *
 *      - For "vector", reads the requested variable and point range from the
 *        selected Binary: plot and sets the interpreter result to a Tcl list of
 *        decoded values.
 *
 *      - For "dict", reads all variables over the requested point range from the
 *        selected Binary: plot and sets the interpreter result to a Tcl dictionary
 *        mapping variable names to value lists.
 *
 *      - On failure, sets the interpreter result with a human-readable error message
 *        where appropriate.
 *
 * Notes:
 *      - The "-plot index" selector is optional for plot-specific subcommands. If it
 *        is omitted, plot 0 is selected.
 *
 *      - Plot selection is delegated to RawSelectPlotFromArgs().
 *
 *      - Point range parsing for "vector" and "dict" is delegated to
 *        RawParseRange().
 *
 *      - Variable name lookup for "vector" is delegated to RawPlotFindVariable().
 *
 *      - Raw data conversion is performed lazily by RawPlotVectorToObj() and
 *        RawPlotDictToObj(); the whole file is not decoded when the handle is
 *        opened.
 *
 *      - The "dict" subcommand can create very large Tcl objects for large raw
 *        files. For large datasets, "vector" with an explicit "-from/-count" range
 *        is usually more memory-efficient.
 *
 *      - After the "close" subcommand deletes the handle command, the RawFile
 *        pointer must not be used again.
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
 *      Implements the Tcl command used to open a raw file and create a file-backed
 *      raw-file handle.
 *
 *      This command opens the supplied file in binary translation mode, detects the
 *      text encoding used by the raw-file headers, scans the file for one or more
 *      Binary: plot sections, records the metadata and file offsets for each plot,
 *      and returns a new Tcl handle command.
 *
 *      The returned handle command can then be used to inspect metadata and lazily
 *      read waveform data:
 *
 *          set r [::tclsimrawreader::openraw file.raw]
 *
 *          $r plots
 *          $r header ?-plot index?
 *          $r names ?-plot index?
 *          $r vector ?-plot index? name ?-from index? ?-count count?
 *          $r dict ?-plot index? ?-from index? ?-count count?
 *          $r close
 *
 *      Only Binary: plots are currently accepted. Values: plots are detected by the
 *      header parser but rejected by this command.
 *
 * Parameters:
 *      void *clientData        - Client data supplied when the Tcl command was
 *                                registered. This implementation does not use it.
 *
 *      Tcl_Interp *interp      - Interpreter used for command result objects and
 *                                error reporting.
 *
 *      Tcl_Size objc           - Number of Tcl command arguments in objv.
 *
 *      Tcl_Obj *const objv[]   - Tcl command argument objects. objv[1] must contain
 *                                the raw-file path to open.
 *
 * Results:
 *      Returns TCL_OK if the file is opened, at least one Binary: plot is found, and
 *      a new handle command is created successfully.
 *
 *      Returns TCL_ERROR if:
 *          - the wrong number of command arguments is supplied
 *          - the file cannot be opened
 *          - the channel cannot be configured for binary translation
 *          - the raw-file header encoding cannot be detected
 *          - a header cannot be read or parsed
 *          - a non-Binary: plot is encountered
 *          - the binary data offset cannot be determined
 *          - the computed binary byte count is invalid or overflows
 *          - seeking over a Binary: block fails
 *          - no Binary: plots are found
 *
 * Side Effects:
 *      - Opens the requested file with Tcl_FSOpenFileChannel().
 *
 *      - Configures the channel with:
 *
 *            -translation binary
 *
 *      - Detects the header text encoding with DetectEncoding().
 *
 *      - Allocates and initializes a RawFile structure.
 *
 *      - Repeatedly calls ReadHeader() to parse each plot header.
 *
 *      - For each Binary: plot:
 *
 *            * records the data block offset
 *            * computes the binary byte count with ComputeBinaryByteCount()
 *            * records the offset immediately after the data block
 *            * moves the parsed RawHeader into a RawPlot
 *            * appends the RawPlot to the RawFile plot array
 *            * seeks past the binary block to find the next plot
 *
 *      - Creates a Tcl handle command named:
 *
 *            ::tclsimrawreader::handleN
 *
 *        where N is generated from rawHandleCounter.
 *
 *      - Stores the created command token in rf->token.
 *
 *      - Sets the interpreter result to the new handle command name.
 *
 *      - On failure after allocating the RawFile structure, calls
 *        RawFileDeleteProc() to release the channel, encoding handle, plots, and
 *        RawFile memory.
 *
 * Notes:
 *      - The command scans all Binary: plots when the file is opened, but it does
 *        not decode the waveform data at open time. Data values are read lazily by
 *        the returned handle command.
 *
 *      - ReadHeader() consumes the Binary: marker line and leaves the channel
 *        positioned at the first byte of the binary data block. This position is
 *        stored as plot.dataOffset.
 *
 *      - Binary block size is computed from the parsed header using:
 *
 *            h->numPoints * h->pointStrideBytes
 *
 *        through ComputeBinaryByteCount().
 *
 *      - After a plot is recorded, the channel is advanced to plot.nextOffset so
 *        the next ReadHeader() call starts at the next raw-file header or reaches
 *        EOF.
 *
 *      - Ownership of each temporary RawHeader is transferred into a RawPlot with
 *        RawHeaderMove(). Ownership of each temporary RawPlot is then transferred
 *        into the RawFile plot array with RawFileAppendPlotMove().
 *
 *      - The returned Tcl handle owns the RawFile. Deleting the handle command, for
 *        example with "$handle close", triggers RawFileDeleteProc().
 *
 *      - The function currently rejects Values: plots. Support for ASCII Values:
 *        blocks should be added by extending RawPlot with text-block indexing or
 *        scanning metadata.
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
        int r;
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
        if (dataKind != DATA_BINARY) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw::openraw currently supports only Binary: plots", -1));
            return TCL_ERROR;
        }
        dataOffset = Tcl_Tell(chan);
        if (dataOffset < 0) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("failed to get raw binary data offset", -1));
            return TCL_ERROR;
        }
        if (ComputeBinaryByteCount(interp, &h, &dataBytes) != TCL_OK) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            return TCL_ERROR;
        }
        nextOffset = dataOffset + (Tcl_WideInt)dataBytes;
        if (nextOffset < dataOffset) {
            RawHeaderFree(&h);
            RawFileDeleteProc(rf);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("raw binary data offset overflow", -1));
            return TCL_ERROR;
        }
        plot.dataKind = dataKind;
        plot.dataOffset = dataOffset;
        plot.nextOffset = nextOffset;
        plot.dataBytes = dataBytes;
        RawHeaderMove(&plot.header, &h);
        /*
         * Skip this binary block so the next ReadHeader() starts at the next plot
         * header, or reaches EOF.
         */
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
    }
    if (rf->numPlots == 0) {
        RawFileDeleteProc(rf);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("raw file contains no Binary: plots", -1));
        return TCL_ERROR;
    }
    nameObj = Tcl_ObjPrintf("::tclsimrawreader::handle%lld", (long long)++rawHandleCounter);
    name = Tcl_GetString(nameObj);
    rf->token = Tcl_CreateObjCommand2(interp, name, RawFileObjCmd, rf, RawFileDeleteProc);
    Tcl_SetObjResult(interp, nameObj);
    return TCL_OK;
}



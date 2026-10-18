#include "tclsimrawreader.h"

#ifdef HAVE_RBC
#include <rbcVector.h>
#include <rbcDecls.h>
#ifndef Rbc_VectorCheckWritable
#error "RBC 0.8.1 or newer stubs headers are required"
#endif
/* Compile the portable stub client installed with the RBC public headers. */
#include <rbcStubLib.c>
#endif

//***  RawRbcAutoName function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcAutoName --
 *
 *      Checks whether a vector destination requests automatic naming through RBC's #auto mechanism.
 *
 * Parameters:
 *      Tcl_Obj *name - Non-NULL object containing the destination name.
 *
 * Results:
 *      Returns nonzero if the name is exactly "#auto" or ends with "::#auto".
 *      Returns zero otherwise.
 *
 * Side Effects:
 *      None beyond obtaining the object's string representation.
 *
 * Notes:
 *      Only recognizes the automatic-name marker; does not generate a name, validate its namespace,
 *      or create a vector. The name "auto" without a leading hash is treated as an ordinary name.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcAutoName(Tcl_Obj *name) {
    const char *s = Tcl_GetString(name);
    size_t n = strlen(s);
    return strcmp(s, "#auto") == 0 || (n >= 7 && strcmp(s + n - 7, "::#auto") == 0);
}

//***  RawRbcInit function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcInit --
 *
 *      Initializes the optional RBC vector stubs interface in the current interpreter.
 *      Requires RBC vector version 0.8.1 or a compatible later version.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter in which to initialize RBC vector support.
 *
 * Results:
 *      Returns TCL_OK if the RBC vector stubs interface is available.
 *      Returns TCL_ERROR if initialization fails or the extension was built without RBC support.
 *
 * Side Effects:
 *      May load the RBC vector package and evaluate Tcl code during package initialization.
 *      Initializes the RBC vector stubs pointer and sets the interpreter result on failure.
 *
 * Notes:
 *      Called only when vector output is required, before reading or publishing vector data.
 *      A previously initialized global stubs pointer does not establish that RBC is available
 *      in the current interpreter.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcInit(Tcl_Interp *interp) {
#ifdef HAVE_RBC
    if (Rbc_VectorInitStubs(interp, "0.8.1", 0) == NULL) {
        if (Tcl_GetStringResult(interp)[0] == '\0') {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("RBC vector stubs are unavailable", -1));
        }
        return TCL_ERROR;
    }
    return TCL_OK;
#else
    Tcl_SetObjResult(interp, Tcl_NewStringObj("tclsimrawreader was built without RBC vector support", -1));
    return TCL_ERROR;
#endif
}

//***  RawRbcName function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcName --
 *
 *      Constructs a fully qualified RBC vector destination from a namespace and raw signal name.
 *      Preserves supported literal names and encodes other names to avoid invalid or ambiguous destinations.
 *
 * Parameters:
 *      const char *ns      - Canonical fully qualified destination namespace.
 *      const char *rawName - Null-terminated raw signal name.
 *
 * Results:
 *      Returns a new Tcl string object with reference count zero containing the qualified destination name.
 *
 * Side Effects:
 *      Allocates the result object. Does not create namespaces or vectors.
 *
 * Notes:
 *      Literal names may contain ASCII letters, digits, underscores, dots, at signs, balanced parentheses,
 *      and isolated interior colons. Empty names, names containing namespace separators, leading or trailing
 *      colons, unsupported characters, unbalanced parentheses, or the reserved "_raw_" prefix are encoded.
 *      Encoding uses "_raw_" followed by uppercase hexadecimal pairs for every byte of the raw name.
 *      Reserving the prefix prevents encoded names from colliding with preserved literal names.
 *      Does not check destination availability or interpret #auto as an automatic-naming request.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
Tcl_Obj *RawRbcName(const char *ns, const char *rawName) {
    static const char hex[] = "0123456789ABCDEF";
    Tcl_Obj *name = Tcl_NewStringObj(ns, -1);
    Tcl_Size depth = 0;
    size_t length = strlen(rawName);
    int literal = (length > 0 && rawName[0] != ':' && rawName[length - 1] != ':' && strstr(rawName, "::") == NULL &&
                   strncmp(rawName, "_raw_", 5) != 0);

    for (const unsigned char *p = (const unsigned char *)rawName; literal && *p != '\0'; p++) {
        if (*p == '(') {
            depth++;
        } else if (*p == ')' && depth > 0) {
            depth--;
        } else if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_' ||
                     *p == '.' || *p == ':' || *p == '@')) {
            literal = 0;
        }
    }
    if (strcmp(Tcl_GetString(name), "::") != 0) {
        Tcl_AppendToObj(name, "::", 2);
    }
    if (literal && depth == 0) {
        Tcl_AppendToObj(name, rawName, -1);
    } else {
        Tcl_AppendToObj(name, "_raw_", 5);
        for (const unsigned char *p = (const unsigned char *)rawName; *p != '\0'; p++) {
            char encoded[2] = {hex[*p >> 4], hex[*p & 15]};
            Tcl_AppendToObj(name, encoded, 2);
        }
    }
    return name;
}

//***  RawRbcCheck function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcCheck --
 *
 *      Checks whether a destination can receive an RBC vector without modifying it.
 *      Replacement requires an existing vector of the same numeric type and its registered instance command.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter containing the destination and receiving error diagnostics.
 *      Tcl_Obj *nameObj   - Fully qualified destination name.
 *      int complex       - Nonzero for complex output; zero for real output.
 *      int replace       - Nonzero to allow replacement of an existing compatible vector.
 *      int writable      - Nonzero to require that an existing vector permits modification.
 *
 * Results:
 *      Returns TCL_OK if the destination is unused or satisfies the replacement requirements.
 *      Returns TCL_ERROR on a collision, incompatible numeric type, invalid instance command,
 *      failed writability check, or a build without RBC support.
 *
 * Side Effects:
 *      Sets the interpreter result on failure.
 *      Does not create, resize, replace, or otherwise modify the destination.
 *
 * Notes:
 *      Requires initialized RBC stubs in RBC-enabled builds.
 *      An unrelated command occupying the destination is always rejected.
 *      For an existing vector, verifies that the command's client data identifies that same vector.
 *      Publication must recheck the destination if intervening callbacks could change it.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcCheck(Tcl_Interp *interp, Tcl_Obj *nameObj, int complex, int replace, int writable) {
#ifdef HAVE_RBC
    const char *name = Tcl_GetString(nameObj);
    Tcl_CmdInfo info;
    int commandExists = Tcl_GetCommandInfo(interp, name, &info);
    if (Rbc_VectorExists2(interp, name)) {
        Rbc_Vector *vector;
        if (!replace) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" already exists", name));
            return TCL_ERROR;
        }
        if (Rbc_GetVector(interp, name, &vector) != TCL_OK) {
            return TCL_ERROR;
        }
        /* Current RBC instance commands use the opaque vector itself as ObjCommand2 client data. */
        if (!commandExists || info.isNativeObjectProc != 2 || info.objClientData2 != (void *)vector) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("command \"%s\" is not the vector's instance command", name));
            return TCL_ERROR;
        }
        if (Rbc_VectorGetType(vector) != (complex ? RBC_VECTOR_COMPLEX : RBC_VECTOR_REAL)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" has a different numeric type", name));
            return TCL_ERROR;
        }
        if (writable && Rbc_VectorCheckWritable(interp, vector) != TCL_OK) {
            return TCL_ERROR;
        }
    } else if (commandExists) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("command \"%s\" already exists and is not an RBC vector", name));
        return TCL_ERROR;
    }
    return TCL_OK;
#else
    return RawRbcInit(interp);
#endif
}

//***  RawRbcPublish function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcPublish --
 *
 *      Publishes previously decoded numeric columns. Preflights every destination and prepares complex buffers
 *      before changing data. New vectors use -literal true so numeric names such as v(1) cannot become size
 *      specifications, and -variable {} to avoid mapped Tcl arrays. Real buffers are transferred directly; complex
 * buffers are packed into RBC's public Rbc_Complex representation.
 *
 * Parameters:
 *      numVars/names/columns - Selected columns and referenced, fully qualified destination requests. A final
 *                              #auto component is expanded by RBC; names are replaced with actual owned names.
 *      replace               - Permit same-type replacement; otherwise reject any existing destination.
 *      dictionary            - Return a list of names for the caller to assemble into a dictionary, or one name.
 *      resultPtr             - Receives a new Tcl result object on success.
 *
 * Results:
 *      TCL_OK or TCL_ERROR. Transferred column buffers are set to NULL. Newly created commands are removed if
 *      publication fails; existing vectors are never deleted. Read/naming/type errors precede publication. RBC
 *      notifications may execute application callbacks, so this is not a transaction against reentrant callbacks.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcPublish(Tcl_Interp *interp, Tcl_Size numVars, Tcl_Obj **names, RawNumericColumn *columns, int replace,
                  int dictionary, Tcl_Obj **resultPtr) {
#ifdef HAVE_RBC
    Rbc_Complex **packed = (Rbc_Complex **)Tcl_Alloc((size_t)numVars * sizeof(*packed));
    Tcl_Command *created = (Tcl_Command *)Tcl_Alloc((size_t)numVars * sizeof(*created));
    int status = TCL_ERROR;
    memset(packed, 0, (size_t)numVars * sizeof(*packed));
    memset(created, 0, (size_t)numVars * sizeof(*created));
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (!RawRbcAutoName(names[i]) && RawRbcCheck(interp, names[i], columns[i].complex, replace, 1) != TCL_OK) {
            goto done;
        }
        if (columns[i].complex) {
            size_t bytes;
            if ((size_t)columns[i].count > SIZE_MAX / sizeof(Rbc_Complex)) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("complex vector size overflow", -1));
                goto done;
            }
            bytes = (size_t)columns[i].count * sizeof(Rbc_Complex);
            packed[i] = (Rbc_Complex *)Tcl_AttemptAlloc(bytes ? bytes : sizeof(Rbc_Complex));
            if (packed[i] == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("cannot allocate complex vector", -1));
                goto done;
            }
            for (Tcl_Size j = 0; j < columns[i].count; j++) {
                packed[i][j].real = columns[i].values[2 * j];
                packed[i][j].imag = columns[i].values[2 * j + 1];
            }
        }
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        const char *name = Tcl_GetString(names[i]);
        if (!RawRbcAutoName(names[i]) && RawRbcCheck(interp, names[i], columns[i].complex, replace, 1) != TCL_OK) {
            goto done;
        }
        if (RawRbcAutoName(names[i]) || !Rbc_VectorExists2(interp, name)) {
            Tcl_Obj *args[9] = {Tcl_NewStringObj("::rbc::vector", -1),
                                Tcl_NewStringObj("create", -1),
                                names[i],
                                Tcl_NewStringObj("-variable", -1),
                                Tcl_NewObj(),
                                Tcl_NewStringObj("-type", -1),
                                Tcl_NewStringObj(columns[i].complex ? "complex" : "real", -1),
                                Tcl_NewStringObj("-literal", -1),
                                Tcl_NewBooleanObj(1)};
            for (int j = 0; j < 9; j++) {
                Tcl_IncrRefCount(args[j]);
            }
            int code = Tcl_EvalObjv(interp, 9, args, TCL_EVAL_DIRECT);
            for (int j = 0; j < 9; j++) {
                Tcl_DecrRefCount(args[j]);
            }
            if (code != TCL_OK) {
                goto done;
            }
            /* RBC returns the fully qualified command name, not a list. */
            Tcl_Obj *actual = Tcl_GetObjResult(interp);
            Tcl_IncrRefCount(actual);
            Tcl_DecrRefCount(names[i]);
            names[i] = actual;
            name = Tcl_GetString(actual);
            created[i] = Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY);
            if (created[i] == NULL || !Rbc_VectorExists2(interp, name)) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("RBC did not create vector \"%s\"", name));
                goto done;
            }
        }
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        for (Tcl_Size j = 0; j < i; j++) {
            if (strcmp(Tcl_GetString(names[i]), Tcl_GetString(names[j])) == 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("duplicate resolved vector destination", -1));
                goto done;
            }
        }
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        Rbc_Vector *vector;
        /* A creation trace or previous vector's notification may have changed a later destination. */
        if (RawRbcCheck(interp, names[i], columns[i].complex, 1, 1) != TCL_OK ||
            Rbc_GetVector(interp, Tcl_GetString(names[i]), &vector) != TCL_OK) {
            goto done;
        }
        if (columns[i].complex) {
            if (Rbc_ResetComplexVector(vector, packed[i], columns[i].count, columns[i].count, TCL_DYNAMIC) != TCL_OK) {
                goto done;
            }
            packed[i] = NULL;
        } else {
            if (Rbc_ResetVector(vector, columns[i].values, columns[i].count, columns[i].count, TCL_DYNAMIC) != TCL_OK) {
                goto done;
            }
            columns[i].values = NULL;
        }
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (!Rbc_VectorExists2(interp, Tcl_GetString(names[i]))) {
            Tcl_SetObjResult(interp,
                             Tcl_ObjPrintf("vector \"%s\" was removed during publication", Tcl_GetString(names[i])));
            goto done;
        }
        if (RawRbcCheck(interp, names[i], columns[i].complex, 1, 0) != TCL_OK) {
            goto done;
        }
    }
    *resultPtr = dictionary ? Tcl_NewListObj(numVars, names) : Tcl_DuplicateObj(names[0]);
    status = TCL_OK;
done:
    if (status != TCL_OK) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, status);
        for (Tcl_Size i = 0; i < numVars; i++) {
            if (created[i] != NULL &&
                Tcl_FindCommand(interp, Tcl_GetString(names[i]), NULL, TCL_GLOBAL_ONLY) == created[i]) {
                Tcl_DeleteCommandFromToken(interp, created[i]);
            }
        }
        Tcl_RestoreInterpState(interp, state);
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        if (packed[i] != NULL) {
            Tcl_Free((char *)packed[i]);
        }
    }
    Tcl_Free((char *)packed);
    Tcl_Free((char *)created);
    return status;
#else
    return RawRbcInit(interp);
#endif
}

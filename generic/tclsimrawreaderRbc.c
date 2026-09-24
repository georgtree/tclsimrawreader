#include "tclsimrawreader.h"

#ifdef HAVE_RBC
#include <rbcVector.h>
#include <rbcDecls.h>
/* Compile the portable stub client installed with the RBC public headers. */
#include <rbcStubLib.c>
#endif

//***  RawRbcInit function
/*
 *----------------------------------------------------------------------------------------------------------------------
 *
 * RawRbcInit --
 *
 *      Initializes optional RBC stubs in the current interpreter. Called only for vector output, before reading or
 *      publishing data. Package initialization may evaluate Tcl. Returns TCL_OK, or TCL_ERROR with a load/build error.
 *      A global stub pointer does not establish that RBC is initialized in this particular interpreter.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcInit(Tcl_Interp *interp) {
#ifdef HAVE_RBC
    if (Rbc_VectorInitStubs(interp, "0.5.0", 0) == NULL) {
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
 *      Returns an unreferenced Tcl object containing the destination in the caller's current namespace. ASCII
 *      letters, digits and periods are retained; every other UTF-8 byte (including underscore and colon) becomes
 *      _HH. This injective encoding avoids RBC's parentheses/range syntax, namespace injection and Tcl array mapping.
 *      The empty name is represented by _00. Existing raw names remain the keys in dictionary results.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
Tcl_Obj *RawRbcName(Tcl_Interp *interp, const char *rawName) {
    static const char hex[] = "0123456789ABCDEF";
    Tcl_Obj *name = Tcl_NewStringObj(Tcl_GetCurrentNamespace(interp)->fullName, -1);
    if (strcmp(Tcl_GetString(name), "::") != 0) {
        Tcl_AppendToObj(name, "::", 2);
    }
    if (*rawName == '\0') {
        Tcl_AppendToObj(name, "_00", 3);
    }
    for (const unsigned char *p = (const unsigned char *)rawName; *p != '\0'; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '.') {
            Tcl_AppendToObj(name, (const char *)p, 1);
        } else {
            char encoded[3] = {'_', hex[*p >> 4], hex[*p & 15]};
            Tcl_AppendToObj(name, encoded, 3);
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
 *      Checks one fully qualified destination without changing it. Replacement updates only an existing RBC vector
 *      of the same numeric type, preserving its clients and mapping. Unrelated commands and renamed/aliased vector
 *      commands are rejected. Returns TCL_OK or TCL_ERROR with a collision/type diagnostic. Requires initialized stubs.
 *
 *----------------------------------------------------------------------------------------------------------------------
 */
int RawRbcCheck(Tcl_Interp *interp, Tcl_Obj *nameObj, int complex, int replace) {
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
 *      before changing data. New vectors are created with -variable {} to avoid mapped Tcl arrays. Real buffers
 *      are transferred directly; complex buffers are packed into RBC's public Rbc_Complex representation.
 *
 * Parameters:
 *      numVars/names/columns - Selected columns and referenced, fully qualified destination name objects.
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
        if (RawRbcCheck(interp, names[i], columns[i].complex, replace) != TCL_OK) {
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
        if (RawRbcCheck(interp, names[i], columns[i].complex, replace) != TCL_OK) {
            goto done;
        }
        if (!Rbc_VectorExists2(interp, name)) {
            Tcl_Obj *args[7] = {Tcl_NewStringObj("::rbc::vector", -1),
                                Tcl_NewStringObj("create", -1),
                                names[i],
                                Tcl_NewStringObj("-variable", -1),
                                Tcl_NewObj(),
                                Tcl_NewStringObj("-type", -1),
                                Tcl_NewStringObj(columns[i].complex ? "complex" : "real", -1)};
            for (int j = 0; j < 7; j++) {
                Tcl_IncrRefCount(args[j]);
            }
            int code = Tcl_EvalObjv(interp, 7, args, TCL_EVAL_DIRECT);
            for (int j = 0; j < 7; j++) {
                Tcl_DecrRefCount(args[j]);
            }
            if (code != TCL_OK) {
                goto done;
            }
            created[i] = Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY);
            if (created[i] == NULL || !Rbc_VectorExists2(interp, name)) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("RBC did not create vector \"%s\"", name));
                goto done;
            }
        }
    }
    for (Tcl_Size i = 0; i < numVars; i++) {
        Rbc_Vector *vector;
        /* A creation trace or previous vector's notification may have changed a later destination. */
        if (RawRbcCheck(interp, names[i], columns[i].complex, 1) != TCL_OK ||
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
        if (RawRbcCheck(interp, names[i], columns[i].complex, 1) != TCL_OK) {
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

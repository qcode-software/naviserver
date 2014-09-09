/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/*
 * tclobj.c --
 *
 *      Helper routines for managing Tcl object types.
 */

#include "nsd.h"

/*
 * Local functions defined in this file.
 */

static Tcl_UpdateStringProc UpdateStringOfAddr;
static Tcl_SetFromAnyProc   SetAddrFromAny;

/*
 * Local variables defined in this file.
 */

static Tcl_ObjType addrType = {
    "ns:addr",
    (Tcl_FreeInternalRepProc *) NULL,
    (Tcl_DupInternalRepProc *)  NULL,
    UpdateStringOfAddr,
    SetAddrFromAny
};

static const Tcl_ObjType *byteArrayTypePtr; /* For NsTclIsByteArray(). */


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitAddrType --
 *
 *      Initialize the Tcl address object type and cache the bytearray
 *      Tcl built-in type.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitAddrType(void)
{
    Tcl_RegisterObjType(&addrType);
    byteArrayTypePtr = Tcl_GetObjType("bytearray");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclResetObjType --
 *
 *      Reset the given Tcl_Obj type, freeing any type specific
 *      internal representation.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on object type.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclResetObjType(Tcl_Obj *objPtr, Tcl_ObjType *newTypePtr)
{
    const Tcl_ObjType *typePtr = objPtr->typePtr;

    if (typePtr != NULL && typePtr->freeIntRepProc != NULL) {
        (*typePtr->freeIntRepProc)(objPtr);
    }
    objPtr->typePtr = newTypePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetTwoPtrValue --
 *
 *      Reset the given objects type and values, freeing any existing
 *      internal rep.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on object type.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetTwoPtrValue(Tcl_Obj *objPtr, Tcl_ObjType *newTypePtr,
                     void *ptr1, void *ptr2)
{
    Ns_TclResetObjType(objPtr, newTypePtr);
    objPtr->internalRep.twoPtrValue.ptr1 = ptr1;
    objPtr->internalRep.twoPtrValue.ptr2 = ptr2;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetOtherValuePtr --
 *
 *      Reset the given objects type and value, freeing any existing
 *      internal rep.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on object type.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetOtherValuePtr(Tcl_Obj *objPtr, Tcl_ObjType *newTypePtr, void *value)
{
    Ns_TclResetObjType(objPtr, newTypePtr);
    objPtr->internalRep.otherValuePtr = value;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetStringRep --
 *
 *      Copy length bytes and set objects string rep.  The objects
 *      existing string rep *must* have already been freed. Tcl uses
 *      as well "int" and not "size" (internally and via interface)
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is allocated.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetStringRep(Tcl_Obj *objPtr, char *bytes, int length)
{
    if (length < 1) {
      length = (int)strlen(bytes);
    }
    objPtr->length = length;
    objPtr->bytes = ckalloc((size_t) length + 1);
    memcpy(objPtr->bytes, bytes, (size_t) length + 1);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetFromAnyError --
 *
 *      This procedure is registered as the setFromAnyProc for an
 *      object type when it doesn't make sense to generate it's internal
 *      form from the string representation alone.
 *
 * Results:
 *      The return value is always TCL_ERROR, and an error message is
 *      left in interp's result if interp isn't NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclSetFromAnyError(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    Tcl_AppendToObj(Tcl_GetObjResult(interp),
                    "can't convert value to requested type except via prescribed API",
                    -1);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetAddrFromObj --
 *
 *      Return the internal pointer of an address Tcl_Obj.
 *
 * Results:
 *      TCL_OK or TCL_ERROR if conversion failed or not the correct type.
 *
 * Side effects:
 *      Object may be converted to address type.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetAddrFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr,
                     CONST char *type, void **addrPtrPtr)
{
    if (Tcl_ConvertToType(interp, objPtr, &addrType) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objPtr->internalRep.twoPtrValue.ptr1 != (void *) type) {
        Tcl_AppendResult(interp, "incorrect type: ", Tcl_GetString(objPtr), NULL);
        return TCL_ERROR;
    }
    *addrPtrPtr = objPtr->internalRep.twoPtrValue.ptr2;

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetAddrObj --
 *
 *      Convert the given object to the ns:addr type.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetAddrObj(Tcl_Obj *objPtr, CONST char *type, void *addr)
{
    if (Tcl_IsShared(objPtr)) {
        Tcl_Panic("Ns_TclSetAddrObj called with shared object");
    }
    Ns_TclSetTwoPtrValue(objPtr, &addrType, (void *) type, addr);
    Tcl_InvalidateStringRep(objPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetOpaqueFromObj --
 *
 *      Get the internal pointer of an address Tcl_Obj.
 *
 * Results:
 *      TCL_OK or TCL_ERROR if object was not of the ns:addr type.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetOpaqueFromObj(Tcl_Obj *objPtr, CONST char *type, void **addrPtrPtr)
{
    if (objPtr->typePtr != &addrType
        || objPtr->internalRep.twoPtrValue.ptr1 != (void *) type) {
        return TCL_ERROR;
    }
    *addrPtrPtr = objPtr->internalRep.twoPtrValue.ptr2;

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclSetOpaqueObj --
 *
 *      Convert the given object to the ns:addr type without
 *      invalidating the current string rep.  It is OK if the object
 *      is shared.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclSetOpaqueObj(Tcl_Obj *objPtr, CONST char *type, void *addr)
{
    Ns_TclSetTwoPtrValue(objPtr, &addrType, (void *) type, addr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclObjIsByteArray --
 *
 *      Does the given Tcl object have a byte array internal rep?  The
 *      function determines when it is safe to interpret a string as a
 *      byte array directly. It is the same as Tcl 8.6's
 *      TclIsPureByteArray(Tcl_Obj *objPtr)
 *
 * Results:
 *      Boolean.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclObjIsByteArray(Tcl_Obj *objPtr)
{
    return (objPtr->typePtr == byteArrayTypePtr && (objPtr->bytes==NULL));
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfAddr --
 *
 *      Update the string representation for an address object.
 *      Note: This procedure does not free an existing old string rep
 *      so storage will be lost if this has not already been done. 
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfAddr(Tcl_Obj *objPtr)
{
    void   *type = objPtr->internalRep.twoPtrValue.ptr1;
    void   *addr = objPtr->internalRep.twoPtrValue.ptr2;
    char    buf[128];
    size_t  len;

    len = snprintf(buf, sizeof(buf), "t%p-a%p-%s",
                   type, addr, (char *) type);
    Ns_TclSetStringRep(objPtr, buf, (int)len);
}


/*
 *----------------------------------------------------------------------
 *
 * SetAddrFromAny --
 *
 *      Attempt to generate an address internal form for the Tcl object.
 *
 * Results:
 *      The return value is a standard Tcl result. If an error occurs
 *      during conversion, an error message is left in the interpreter's
 *      result unless interp is NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
SetAddrFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    void *type, *addr;
    char *string;

    string = Tcl_GetString(objPtr);
    if (sscanf(string, "t%p-a%p", &type, &addr) != 2
        || type == NULL || addr == NULL) {
        Tcl_AppendResult(interp, "invalid address \"", string, "\"", NULL);
        return TCL_ERROR;
    }
    Ns_TclSetTwoPtrValue(objPtr, &addrType, type, addr);

    return TCL_OK;
}

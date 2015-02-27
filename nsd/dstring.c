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
 * dstring.c --
 *
 *      Ns_DString routines.  Ns_DString's are now compatible 
 *      with Tcl_DString's.
 */

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringVarAppend --
 *
 *      Append a variable number of string arguments to a dstring.
 *
 * Results:
 *      Pointer to current dstring value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringVarAppend(Ns_DString *dsPtr, ...)
{
    register const char *s;
    va_list              ap;

    va_start(ap, dsPtr);
    while ((s = va_arg(ap, char *)) != NULL) {
        Ns_DStringAppend(dsPtr, s);
    }
    va_end(ap);

    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringExport --
 *
 *      Return a copy of the string value on the heap.
 *      Ns_DString is left in an initialized state.
 *
 * Results:
 *      Pointer to ns_malloc'ed string which must be eventually freed.
 *
 * Side effects:
 *      None.
 *
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringExport(Ns_DString *dsPtr)
{
    char *s;

    assert(dsPtr != NULL);

    if (dsPtr->string != dsPtr->staticSpace) {
        s = dsPtr->string;
        dsPtr->string = dsPtr->staticSpace;
    } else {
        s = ns_malloc((size_t)dsPtr->length + 1u);
        memcpy(s, dsPtr->string, (size_t)dsPtr->length + 1u);  
    }
    Ns_DStringFree(dsPtr);

    return s;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringAppendArg --
 *
 *      Append a string including its terminating null byte.
 *
 * Results:
 *      Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringAppendArg(Ns_DString *dsPtr, const char *bytes)
{
    assert(dsPtr != NULL);
    assert(bytes != NULL);
    
    return Ns_DStringNAppend(dsPtr, bytes, (int) strlen(bytes) + 1);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringPrintf --
 *
 *      Append a sequence of values using a format string.
 *
 * Results:
 *      Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringPrintf(Ns_DString *dsPtr, const char *fmt, ...)
{
    char           *str;
    va_list         ap;

    assert(dsPtr != NULL);

    va_start(ap, fmt);
    str = Ns_DStringVPrintf(dsPtr, fmt, ap);
    va_end(ap);

    return str;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringVPrintf --
 *
 *      Append a sequence of values using a format string.
 *
 * Results:
 *      Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringVPrintf(Ns_DString *dsPtr, const char *fmt, va_list apSrc)
{
    char    *buf;
    int      origLength, newLength, result;
    size_t   bufLength;
    va_list  ap;

    assert(dsPtr != NULL);
    assert(fmt != NULL);
    
    origLength = dsPtr->length;

    /*
     * Extend the dstring, trying first to firt everything in the
     * static space (unless it is unreasonably small), or if
     * we already have an allocated buffer just bump it up by 1k.
     */

    if (dsPtr->spaceAvl < TCL_INTEGER_SPACE) {
        newLength = dsPtr->length + 1024;
    } else {
        newLength = dsPtr->spaceAvl -1; /* leave space for dstring NIL */
    }
    Ns_DStringSetLength(dsPtr, newLength);

    /*
     * Now that any dstring buffer relocation has taken place it's
     * safe to point into the middle of it at the end of the
     * existing data.
     */

    buf = dsPtr->string + origLength;
    bufLength = (size_t)newLength - (size_t)origLength;

    va_copy(ap, apSrc);
    result = vsnprintf(buf, bufLength, fmt, ap);
    va_end(ap);

    /*
     * Check for overflow and retry. For win32 just double the buffer size
     * and iterate, otherwise we should get this correct first time.
     */

#ifdef _WIN32
    while (result == -1 && errno == ERANGE) {
        newLength = dsPtr->spaceAvl * 2;
#else
    if (result >= bufLength) {
        newLength = dsPtr->spaceAvl + (result - bufLength);
#endif

        Ns_DStringSetLength(dsPtr, newLength);

        buf = dsPtr->string + origLength;
        bufLength = (size_t)newLength - (size_t)origLength;

        va_copy(ap, apSrc);
        result = vsnprintf(buf, bufLength, fmt, ap);
        va_end(ap);
    }

    /*
     * Set the dstring buffer to the actual length.
     * NB: Eat any errors.
     */

    if (result > 0) {
        Ns_DStringSetLength(dsPtr, origLength + result);
    } else {
        Ns_DStringSetLength(dsPtr, origLength);
    }

    return Ns_DStringValue(dsPtr);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringAppendArgv --
 *
 *      Append an argv vector pointing to the null terminated
 *      strings in the given dstring.
 *
 * Results:
 *      Pointer char ** vector appended to end of dstring.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char **
Ns_DStringAppendArgv(Ns_DString *dsPtr)
{
    char *s, **argv;
    int   i, argc, len, size;

    /* 
     * Determine the number of strings.
     */

    assert(dsPtr != NULL);
    
    argc = 0;
    s = dsPtr->string;
    while (*s != '\0') {
        ++argc;
        s += strlen(s) + 1u;
    }

    /*
     * Resize the dstring with space for the argv aligned
     * on an 8 byte boundry.
     */

    len = ((dsPtr->length / 8) + 1) * 8;
    size = len + ((int)sizeof(char *) * (argc + 1));
    Ns_DStringSetLength(dsPtr, size);

    /*
     * Set the argv elements to the strings.
     */

    s = dsPtr->string;
    argv = (char **) (s + len);
    for (i = 0; i < argc; ++i) {
        argv[i] = s;
        s += strlen(s) + 1u;
    }
    argv[i] = NULL;

    return argv;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringPop --
 *
 *      Allocate a new dstring.
 *      Deprecated.
 *
 * Results:
 *      Pointer to Ns_DString.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Ns_DString *
Ns_DStringPop(void)
{
    Ns_DString *dsPtr;

    dsPtr = ns_malloc(sizeof(Ns_DString));
    Ns_DStringInit(dsPtr);
    return dsPtr;
}

/*
 *----------------------------------------------------------------------
 * Ns_DStringPush --
 *
 *      Free a dstring.
 *      Deprecated.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringPush(Ns_DString *dsPtr)
{
    Ns_DStringFree(dsPtr);
    ns_free(dsPtr);
}


/*
 *----------------------------------------------------------------------
 * Compatibility routines --
 *
 *  Wrappers for old Ns_DString functions.
 *
 * Results:
 *      See Tcl_DString routine.
 *
 * Side effects:
 *      See Tcl_DString routine.
 *
 *----------------------------------------------------------------------
 */

#undef Ns_DStringInit

void
Ns_DStringInit(Ns_DString *dsPtr)
{
    Tcl_DStringInit(dsPtr);
}

#undef Ns_DStringFree

void
Ns_DStringFree(Ns_DString *dsPtr)
{
    Tcl_DStringFree(dsPtr);
}

#undef Ns_DStringSetLength

void
Ns_DStringSetLength(Ns_DString *dsPtr, int length)
{
    Tcl_DStringSetLength(dsPtr, length);
}

#undef Ns_DStringTrunc

void
Ns_DStringTrunc(Ns_DString *dsPtr, int length)
{
    Tcl_DStringSetLength(dsPtr, length);
}

#undef Ns_DStringNAppend

char *
Ns_DStringNAppend(Ns_DString *dsPtr, const char *bytes, int length)
{
    return Tcl_DStringAppend(dsPtr, bytes, length);
}

#undef Ns_DStringAppend

char *
Ns_DStringAppend(Ns_DString *dsPtr, const char *bytes)
{
    return Tcl_DStringAppend(dsPtr, bytes, -1);
}

#undef Ns_DStringAppendElement

char *
Ns_DStringAppendElement(Ns_DString *dsPtr, const char *bytes)
{
    return Tcl_DStringAppendElement(dsPtr, bytes);
}

#undef Ns_DStringLength

int
Ns_DStringLength(const Ns_DString *dsPtr)
{
    return dsPtr->length;
}

#undef Ns_DStringValue

char *
Ns_DStringValue(const Ns_DString *dsPtr)
{
    return dsPtr->string;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

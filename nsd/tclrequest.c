/*
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://mozilla.com/.
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
 * tclrequest.c --
 *
 *      Routines for Tcl proc, filter and ADP registered requests.
 */

#include "nsd.h"

/*
 * Static variables defined in this file.
 */

static Ns_ObjvTable filters[] = {
    {"preauth",  (unsigned int)NS_FILTER_PRE_AUTH},
    {"postauth", (unsigned int)NS_FILTER_POST_AUTH},
    {"trace",    (unsigned int)NS_FILTER_TRACE},
    {NULL, 0U}
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRequest --
 *
 *      Dummy up a direct call to NsTclRequestProc for a connection.
 *
 * Results:
 *      See NsTclRequest.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclRequest(Ns_Conn *conn, const char *name)
{
    Ns_TclCallback cb;

    cb.cbProc = (Ns_Callback *) NsTclRequestProc;
    cb.server = Ns_ConnServer(conn);
    cb.script = name;
    cb.argc   = 0;
    cb.argv   = NULL;

    return NsTclRequestProc(&cb, conn);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterProcObjCmd --
 *
 *      Implements ns_register_proc as obj command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterProcObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp       *itPtr = arg;
    Ns_TclCallback *cbPtr;
    Tcl_Obj        *scriptObj;
    const char     *method, *url;
    int             remain = 0, noinherit = 0;
    unsigned int    flags = 0U;

    Ns_ObjvSpec opts[] = {
        {"-noinherit", Ns_ObjvBool,  &noinherit, INT2PTR(1)},
        {"--",         Ns_ObjvBreak, NULL,       NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"method",     Ns_ObjvString, &method,    NULL},
        {"url",        Ns_ObjvString, &url,       NULL},
        {"script",     Ns_ObjvObj,    &scriptObj, NULL},
        {"?args",      Ns_ObjvArgs,   &remain,    NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    if (noinherit != 0) {flags |= NS_OP_NOINHERIT;}

    cbPtr = Ns_TclNewCallback(interp, (Ns_Callback *)NsTclRequestProc, scriptObj,
                              remain, objv + (objc - remain));
    Ns_RegisterRequest(itPtr->servPtr->server, method, url,
                       NsTclRequestProc, Ns_TclFreeCallback, cbPtr, flags);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterProxyObjCmd --
 *
 *      Implements ns_register_proxy as obj command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterProxyObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp       *itPtr = arg;
    Ns_TclCallback *cbPtr;
    Tcl_Obj        *scriptObj;
    const char     *method, *protocol;
    int             remain = 0;

    Ns_ObjvSpec opts[] = {
        {"--",         Ns_ObjvBreak, NULL,   NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"method",     Ns_ObjvString, &method,    NULL},
        {"protocol",   Ns_ObjvString, &protocol,  NULL},
        {"script",     Ns_ObjvObj,    &scriptObj, NULL},
        {"?args",      Ns_ObjvArgs,   &remain,    NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    cbPtr = Ns_TclNewCallback(interp, (Ns_Callback *)NsTclRequestProc, 
			      scriptObj, remain, objv + (objc - remain));
    Ns_RegisterProxyRequest(itPtr->servPtr->server, method, protocol,
                       NsTclRequestProc, Ns_TclFreeCallback, cbPtr);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterFastPathObjCmd --
 *
 *      Implements ns_register_fastpath as obj command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterFastPathObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp       *itPtr = arg;
    const char     *method, *url;
    int             noinherit = 0;
    unsigned int    flags = 0U;

    Ns_ObjvSpec opts[] = {
        {"-noinherit", Ns_ObjvBool,  &noinherit, INT2PTR(NS_OP_NOINHERIT)},
        {"--",         Ns_ObjvBreak, NULL,       NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"method",     Ns_ObjvString, &method, NULL},
        {"url",        Ns_ObjvString, &url,    NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    if (noinherit != 0) {
	flags |= NS_OP_NOINHERIT;
    }

    Ns_RegisterRequest(itPtr->servPtr->server, method, url,
                       Ns_FastPathProc, NULL, NULL, flags);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnRegisterObjCmd --
 *
 *      Implement the ns_unregister_op command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Depends on any delete callbacks.
 *
 *----------------------------------------------------------------------
 */

int
NsTclUnRegisterOpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp   *itPtr = arg;
    const char *method = NULL, *url = NULL;
    int         noinherit = 0, recurse = 0;

    Ns_ObjvSpec opts[] = {
        {"-noinherit", Ns_ObjvBool,  &noinherit, INT2PTR(NS_OP_NOINHERIT)},
        {"-recurse",   Ns_ObjvBool,  &recurse,   INT2PTR(NS_OP_RECURSE)},
        {"--",         Ns_ObjvBreak, NULL,   NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"method",   Ns_ObjvString, &method, NULL},
        {"url",      Ns_ObjvString, &url,    NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }
    Ns_UnRegisterRequestEx(itPtr->servPtr->server, method, url,
                           ((unsigned int)noinherit | (unsigned int)recurse));

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterFilterObjCmd --
 *
 *      Implements ns_register_filter.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterFilterObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp        *itPtr = arg;
    Ns_TclCallback  *cbPtr;
    const char      *method, *urlPattern;
    Tcl_Obj         *scriptObj;
    int              remain = 0, first = 0;
    unsigned int     when = 0U;

    Ns_ObjvSpec opts[] = {
        {"-first", Ns_ObjvBool,  &first, INT2PTR(1)},
        {"--",     Ns_ObjvBreak, NULL,   NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"when",       Ns_ObjvFlags,  &when,       filters},
        {"method",     Ns_ObjvString, &method,     NULL},
        {"urlPattern", Ns_ObjvString, &urlPattern, NULL},
        {"script",     Ns_ObjvObj,    &scriptObj,  NULL},
        {"?args",      Ns_ObjvArgs,   &remain,     NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    cbPtr = Ns_TclNewCallback(interp, (Ns_Callback *)NsTclFilterProc, 
			      scriptObj, remain, objv + (objc - remain));
    (void)Ns_RegisterFilter(itPtr->servPtr->server, method, urlPattern,
			    NsTclFilterProc, (Ns_FilterType)when, cbPtr, first);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclShortcutFilterObjCmd --
 *
 *      Implements ns_shortcut_filter.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Other filters that also match when+method+urlPattern will not run.
 *
 *----------------------------------------------------------------------
 */

int
NsTclShortcutFilterObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp    *itPtr = arg;
    const char  *server = itPtr->servPtr->server;
    const char  *method, *urlPattern;
    unsigned int when = 0U;

    Ns_ObjvSpec args[] = {
        {"when",       Ns_ObjvFlags,  &when,       filters},
        {"method",     Ns_ObjvString, &method,     NULL},
        {"urlPattern", Ns_ObjvString, &urlPattern, NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    (void)Ns_RegisterFilter(server, method, urlPattern,
			    NsShortcutFilterProc, (Ns_FilterType)when, NULL, 0);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterTraceObjCmd --
 *
 *      Implements ns_register_trace as obj command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRegisterTraceObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    NsInterp       *itPtr = arg;
    Ns_TclCallback *cbPtr;
    const char     *method, *urlPattern;
    Tcl_Obj        *scriptObj;
    int             remain = 0;

    Ns_ObjvSpec args[] = {
        {"method",     Ns_ObjvString, &method,     NULL},
        {"urlPattern", Ns_ObjvString, &urlPattern, NULL},
        {"script",     Ns_ObjvObj,    &scriptObj,  NULL},
        {"?args",      Ns_ObjvArgs,   &remain,     NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    cbPtr = Ns_TclNewCallback(interp, (Ns_Callback *)NsTclFilterProc, 
			      scriptObj, remain, objv + (objc - remain));
    (void)Ns_RegisterFilter(itPtr->servPtr->server, method, urlPattern,
			    NsTclFilterProc, NS_FILTER_VOID_TRACE, cbPtr, 0);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRequstProc --
 *
 *      Ns_OpProc for Tcl operations.
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      '500 Internal Server Error' sent to client if Tcl script
 *      fails, or '503 Service Unavailable' on an NS_TIMEOUT exception.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRequestProc(void *arg, Ns_Conn *conn)
{
    Ns_TclCallback *cbPtr = arg;
    Tcl_Interp     *interp;
    Ns_DString      ds;
    int             status = NS_OK;

    interp = Ns_GetConnInterp(conn);
    if (Ns_TclEvalCallback(interp, cbPtr, NULL, (char *)0) != TCL_OK) {
        if (NsTclTimeoutException(interp) == NS_TRUE) {
            Ns_DStringInit(&ds);
            Ns_GetProcInfo(&ds, (Ns_Callback *)NsTclRequestProc, arg);
            Ns_Log(Dev, "%s: %s", ds.string, Tcl_GetStringResult(interp));
            Ns_DStringFree(&ds);
            status = Ns_ConnReturnUnavailable(conn);
        } else {
	    (void) Ns_TclLogErrorInfo(interp, "\n(context: request proc)");
            status = Ns_ConnReturnInternalError(conn);
        }
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclFilterProc --
 *
 *      The callback for Tcl filters. Run the script.
 *
 * Results:
 *      NS_OK, NS_FILTER_RETURN, or NS_FILTER_BREAK.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclFilterProc(void *arg, Ns_Conn *conn, Ns_FilterType why)
{
    Ns_TclCallback      *cbPtr = arg;
    Tcl_DString          ds;
    Tcl_Interp          *interp;
    int                  ii, status;
    const char          *result;

    interp = Ns_GetConnInterp(conn);
    Tcl_DStringInit(&ds);

    /*
     * Append the command
     */

    Tcl_DStringAppend(&ds, cbPtr->script, -1);

    /*
     * Append the 'why' argument
     */

    switch (why) {
    case NS_FILTER_PRE_AUTH:
        Tcl_DStringAppendElement(&ds, "preauth");
        break;
    case NS_FILTER_POST_AUTH:
        Tcl_DStringAppendElement(&ds, "postauth");
        break;
    case NS_FILTER_TRACE:
        Tcl_DStringAppendElement(&ds, "trace");
        break;
    case NS_FILTER_VOID_TRACE:
        /* Registered with ns_register_trace; always type VOID TRACE, so don't append. */
        break;
    default:
        /* unexpected value */
        assert(why && 0);
        break;
    }

    /*
     * Append optional arguments
     */

    for (ii = 0; ii < cbPtr->argc; ii++) {
        Tcl_DStringAppendElement(&ds, cbPtr->argv[ii]);
    }

    /*
     * Run the script.
     */

    Tcl_AllowExceptions(interp);
    status = Tcl_EvalEx(interp, ds.string, ds.length, 0);
    result = Tcl_GetStringResult(interp);
    Ns_DStringSetLength(&ds, 0);

    if (status != TCL_OK) {

        /*
         * Handle Tcl errors and timeouts.
         */

        if (NsTclTimeoutException(interp) == NS_TRUE) {
	    Ns_GetProcInfo(&ds, (Ns_Callback *)NsTclFilterProc, arg);
	    Ns_Log(Dev, "%s: %s", ds.string, result);
            (void) Ns_ConnReturnUnavailable(conn);
            status = NS_FILTER_RETURN;
        } else {
            (void) Ns_TclLogErrorInfo(interp, "\n(context: filter proc)");
            status = NS_ERROR;
        }
    } else {

        /*
         * Determine the filter result code.
         */

        if (why == NS_FILTER_VOID_TRACE) {
            status = NS_OK;
        } else if (STREQ(result, "filter_ok")) {
            status = NS_OK;
        } else if (STREQ(result, "filter_break")) {
            status = NS_FILTER_BREAK;
        } else if (STREQ(result, "filter_return")) {
            status = NS_FILTER_RETURN;
        } else {
            Ns_Log(Error, "ns:tclfilter: %s return invalid result: %s",
                   cbPtr->script, result);
            status = NS_ERROR;
        }
    }
    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsShortcutFilterProc --
 *
 *      The callback for Tcl shortcut filters.
 *
 * Results:
 *      Always NS_FILTER_BREAK.
 *
 * Side effects:
 *      No other filters of this type will run for this connection.
 *
 *----------------------------------------------------------------------
 */

int
NsShortcutFilterProc(void *UNUSED(arg), Ns_Conn *UNUSED(conn), Ns_FilterType UNUSED(why))
{
    return NS_FILTER_BREAK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeoutException --
 *
 *      Check for a NS_TIMEOUT exception in the Tcl errorCode variable.
 *
 * Results:
 *      1 if there is an exception, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
NsTclTimeoutException(Tcl_Interp *interp)
{
    const char *errorCode;

    assert(interp != NULL);
    
    errorCode = Tcl_GetVar(interp, "errorCode", TCL_GLOBAL_ONLY);
    if (strncmp(errorCode, "NS_TIMEOUT", 10u) == 0) {
        return NS_TRUE;
    }
    return NS_FALSE;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

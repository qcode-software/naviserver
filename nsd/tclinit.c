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
 * tclinit.c --
 *
 *      Initialization and resource management routines for Tcl.
 */

#include "nsd.h"

/*
 * The following structure maintains interp trace callbacks.
 */

typedef struct TclTrace {
    struct TclTrace    *nextPtr;
    struct TclTrace    *prevPtr;
    Ns_TclTraceProc    *proc;
    void               *arg;
    int                 when;
} TclTrace;

/*
 * The following structure maintains procs to call during interp garbage
 * collection.  Unlike traces, these callbacks are one-shot events
 * registered during normal Tcl script evaluation. The callbacks are
 * invoked in FIFO order (LIFO would probably have been better). In
 * practice this API is rarely used. Instead, more specific garbage
 * collection schemes are used; see the "ns_cleanup" script in init.tcl
 * for examples.
 */

typedef struct Defer {
    struct Defer    *nextPtr;
    Ns_TclDeferProc *proc;
    void            *arg;
} Defer;

/*
 * The following structure maintains scripts to execute when the
 * connection is closed.  The scripts are invoked in LIFO order.
 */

typedef struct AtClose {
    struct AtClose *nextPtr;
    Tcl_Obj        *objPtr;
} AtClose;

/*
 * Static functions defined in this file.
 */

static NsInterp *PopInterp(NsServer *servPtr, Tcl_Interp *interp);
static void PushInterp(NsInterp *itPtr);
static Tcl_HashEntry *GetCacheEntry(NsServer *servPtr);
static Tcl_Interp *CreateInterp(NsInterp **itPtrPtr, NsServer *servPtr);
static NsInterp *NewInterpData(Tcl_Interp *interp, NsServer *servPtr);
static int UpdateInterp(NsInterp *itPtr);
static Tcl_InterpDeleteProc FreeInterpData;
static void RunTraces(NsInterp *itPtr, int why);
static void LogTrace(NsInterp *itPtr, TclTrace *tracePtr, int why);
static int RegisterAt(Ns_TclTraceProc *proc, void *arg, int when);
static Ns_TlsCleanup DeleteInterps;
static Ns_ServerInitProc ConfigServerTcl;

/*
 * Static variables defined in this file.
 */

static Ns_Tls tls;  /* Slot for per-thread Tcl interp cache. */



/*
 *----------------------------------------------------------------------
 *
 * Nsd_Init --
 *
 *      Init routine called when libnsd is loaded via the Tcl
 *      load command.
 *
 * Results:
 *      Always TCL_OK.
 *
 * Side effects:
 *      See Ns_TclInit.
 *
 *----------------------------------------------------------------------
 */

int
Nsd_Init(Tcl_Interp *interp)
{
    return Ns_TclInit(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitTcl --
 *
 *      Initialize the Tcl interp interface.
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
NsInitTcl(void)
{
    /*
     * Allocate the thread storage slot for the table of interps
     * per-thread. At thread exit, DeleteInterps will be called
     * to free any interps remaining on the thread cache.
     */

    Ns_TlsAlloc(&tls, DeleteInterps);

    NsRegisterServerInit(ConfigServerTcl);
}

static int
ConfigServerTcl(CONST char *server)
{
    NsServer   *servPtr;
    Ns_DString  ds;
    CONST char *path, *p;
    int         n;

    servPtr = NsGetServer(server);
    path = Ns_ConfigGetPath(server, NULL, "tcl", NULL);

    Ns_DStringInit(&ds);

    servPtr->tcl.library = (char *) Ns_ConfigString(path, "library", "modules/tcl");
    if (!Ns_PathIsAbsolute(servPtr->tcl.library)) {
        Ns_HomePath(&ds, servPtr->tcl.library, NULL);
        servPtr->tcl.library = Ns_DStringExport(&ds);
    }

    servPtr->tcl.initfile = (char *) Ns_ConfigString(path, "initfile", "bin/init.tcl");
    if (!Ns_PathIsAbsolute(servPtr->tcl.initfile) ) {
        Ns_HomePath(&ds, servPtr->tcl.initfile, NULL);
        servPtr->tcl.initfile = Ns_DStringExport(&ds);
    }

    servPtr->tcl.modules = Tcl_NewObj();
    Tcl_IncrRefCount(servPtr->tcl.modules);

    Ns_RWLockInit(&servPtr->tcl.lock);
    Ns_MutexInit(&servPtr->tcl.cachelock);
    Ns_MutexSetName2(&servPtr->tcl.cachelock, "ns:tcl.cache", server);
    Tcl_InitHashTable(&servPtr->tcl.caches, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.runTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.synch.mutexTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.synch.csTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.synch.semaTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.synch.condTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.synch.rwTable, TCL_STRING_KEYS);

    servPtr->nsv.nbuckets = Ns_ConfigIntRange(path, "nsvbuckets", 8, 1, INT_MAX);
    servPtr->nsv.buckets = NsTclCreateBuckets(server, servPtr->nsv.nbuckets);

    /*
     * Initialize the list of connection headers to log for Tcl errors.
     */

    p = Ns_ConfigGetValue(path, "errorlogheaders");
    if (p != NULL && Tcl_SplitList(NULL, p, &n, &servPtr->tcl.errorLogHeaders)
            != TCL_OK) {
        Ns_Log(Error, "config: errorlogheaders is not a list: %s", p);
    }

    /*
     * Initialize the Tcl detached channel support.
     */

    Tcl_InitHashTable(&servPtr->chans.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->chans.lock, "nstcl:chans", server);


    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclCreateInterp --
 *
 *      Create a new interp with basic commands.
 *
 * Results:
 *      Pointer to new interp.
 *
 * Side effects:
 *      Depends on Tcl library init scripts.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclCreateInterp(void)
{
    return Ns_TclAllocateInterp(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInit --
 *
 *      Initialize the given interp with basic commands.
 *
 * Results:
 *      Always TCL_OK.
 *
 * Side effects:
 *      Depends on Tcl library init scripts.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInit(Tcl_Interp *interp)
{
    NsServer *servPtr = NsGetServer(NULL);

    NewInterpData(interp, servPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclEval --
 *
 *      Execute a tcl script in the context of the given server.
 *
 * Results:
 *      Tcl result code. String result or error placed in dsPtr if
 *      not NULL.
 *
 * Side effects:
 *      Tcl interp may be allocated, initialized and cached if none
 *      available.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclEval(Ns_DString *dsPtr, CONST char *server, CONST char *script)
{
    Tcl_Interp *interp;
    CONST char *result;
    int         retcode = NS_ERROR;

    interp = Ns_TclAllocateInterp(server);
    if (interp != NULL) {
        if (Tcl_EvalEx(interp, script, -1, 0) != TCL_OK) {
            result = Ns_TclLogError(interp);
        } else {
            result = Tcl_GetStringResult(interp);
            retcode = NS_OK;
        }
        if (dsPtr != NULL) {
            Ns_DStringAppend(dsPtr, result);
        }
        Ns_TclDeAllocateInterp(interp);
    }
    return retcode;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclAllocateInterp --
 *
 *      Return a pre-initialized interp for the given server or create
 *      a new one and cache it for the current thread.
 *
 * Results:
 *      Pointer to Tcl_Interp or NULL if invalid server.
 *
 * Side effects:
 *      May invoke alloc and create traces.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclAllocateInterp(CONST char *server)
{
    NsServer *servPtr;
    NsInterp *itPtr;

    /*
     * Verify the server.  NULL (i.e., no server) is valid but
     * a non-null, unknown server is an error.
     */

    if (server == NULL) {
        servPtr = NULL;
    } else {
        servPtr = NsGetServer(server);
        if (servPtr == NULL) {
            return NULL;
        }
    }
    itPtr = PopInterp(servPtr, NULL);

    return itPtr->interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDeAllocateInterp --
 *
 *      Return an interp to the per-thread cache.  If the interp is
 *      associated with a connection, simply adjust the refcnt as
 *      cleanup will occur later when the connection closes.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      See notes on garbage collection in PushInterp.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclDeAllocateInterp(Tcl_Interp *interp)
{
    NsInterp *itPtr;

    itPtr = NsGetInterpData(interp);
    if (itPtr == NULL) {
        Ns_Log(Bug, "Ns_TclDeAllocateInterp: no interp data");
        Tcl_DeleteInterp(interp);
    } else if (itPtr->conn == NULL) {
        PushInterp(itPtr);
    } else {
        itPtr->refcnt--;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetConnInterp --
 *
 *      Get an interp for the given connection.  The interp will be
 *      automatically cleaned up at the end of the connection via a
 *      call to NsFreeConnInterp().
 *
 * Results:
 *      Pointer to Tcl_interp.
 *
 * Side effects:
 *      Interp may be allocated, initialized and cached. Interp traces
 *      may run.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_GetConnInterp(Ns_Conn *conn)
{
    Conn     *connPtr = (Conn *) conn;
    NsInterp *itPtr;

    if (connPtr->itPtr == NULL) {
        itPtr = PopInterp(connPtr->servPtr, NULL);
        itPtr->conn = conn;
        itPtr->nsconn.flags = 0;
        connPtr->itPtr = itPtr;
        RunTraces(itPtr, NS_TCL_TRACE_GETCONN);
    }
    return connPtr->itPtr->interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_FreeConnInterp --
 *
 *      Deprecated.  See: NsFreeConnInterp.
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
Ns_FreeConnInterp(Ns_Conn *conn)
{
    return;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetConn --
 *
 *      Get the Ns_Conn structure associated with an interp.
 *
 * Results:
 *      Pointer to Ns_Conn or NULL.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Ns_Conn *
Ns_TclGetConn(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterpData(interp);

    return (itPtr ? itPtr->conn : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDestroyInterp --
 *
 *      Delete an interp.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on delete traces, if any.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclDestroyInterp(Tcl_Interp *interp)
{
    NsInterp      *itPtr;

    itPtr = NsGetInterpData(interp);
    /*
     * If this naviserver interp, clean it up
     */

    if (itPtr != NULL) {
      Tcl_HashTable *tablePtr = Ns_TlsGet(&tls);
      Tcl_HashEntry *hPtr;

      /*
       * Run traces (behaves gracefully, if there is no server
       * associated.
       */
      RunTraces(itPtr, NS_TCL_TRACE_DELETE);

      /*
       * During shutdown, don't fetch entries via GetCacheEntry(),
       * since this function might create new cache entries. Note,
       * that the thread local cache table might contain as well
       * entries with itPtr->servPtr == NULL.
       */
      hPtr = tablePtr ? Tcl_CreateHashEntry(tablePtr, (char *)itPtr->servPtr, NULL) : NULL;
      
      /*
       * Make sure to delete the entry in the thread local cache to
       * avoid double frees in DeleteInterps()
       */
      if (hPtr != NULL) {
        Tcl_SetHashValue(hPtr, NULL);
      }
    }
    
    /*
     * All other cleanup, including the NsInterp data, if any, will
     * be handled by Tcl's normal delete mechanisms.
     */

    Tcl_DeleteInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclMarkForDelete --
 *
 *      Mark the interp to be deleted after next cleanup.  This routine
 *      is useful for destory interps after they've been modified in
 *      weird ways, e.g., by the TclPro debugger.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Interp will be deleted on next de-allocate.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclMarkForDelete(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterpData(interp);

    if (itPtr != NULL) {
        itPtr->deleteInterp = 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterTrace --
 *
 *      Add an interp trace.  Traces are called in FIFO order.  Valid
 *      traces are: NS_TCL_TRACE... CREATE, DELETE, ALLOCATE,
 *      DEALLOCATE, GETCONN, and FREECONN.
 *
 * Results:
 *      NS_OK if called with a non-NULL server before startup has
 *      completed, NS_ERROR otherwise.
 *
 * Side effects:
 *      CREATE and ALLOCATE traces are run immediately in the current
 *      interp (the initial bootstrap interp).
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclRegisterTrace(CONST char *server, Ns_TclTraceProc *proc,
                    void *arg, int when)
{
    TclTrace   *tracePtr;
    NsServer   *servPtr;
    Tcl_Interp *interp;

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
        Ns_Log(Error, "Ns_TclRegisterTrace: Invalid server: %s", server);
        return NS_ERROR;
    }
    if (Ns_InfoStarted()) {
        Ns_Log(Error, "Can not register Tcl trace, server already started.");
        return NS_ERROR;
    }

    tracePtr = ns_malloc(sizeof(TclTrace));
    tracePtr->proc = proc;
    tracePtr->arg = arg;
    tracePtr->when = when;
    tracePtr->nextPtr = NULL;

    tracePtr->prevPtr = servPtr->tcl.lastTracePtr;
    servPtr->tcl.lastTracePtr = tracePtr;
    if (tracePtr->prevPtr != NULL) {
        tracePtr->prevPtr->nextPtr = tracePtr;
    } else {
        servPtr->tcl.firstTracePtr = tracePtr;
    }

    /*
     * Run CREATE and ALLOCATE traces immediately so that commands registered
     * by binary modules can be called by Tcl init scripts sourced by the
     * already initialised interp which loads the modules.
     */

    if (when & NS_TCL_TRACE_CREATE || when & NS_TCL_TRACE_ALLOCATE) {
        interp = Ns_TclAllocateInterp(server);
        if ((*proc)(interp, arg) != TCL_OK) {
            Ns_TclLogError(interp);
        }
        Ns_TclDeAllocateInterp(interp);
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterAtCreate, Ns_TclRegisterAtCleanup,
 * Ns_TclRegisterAtDelete --
 *
 *      Register callbacks for interp create, cleanup, and delete at
 *      startup.  These routines are deprecated in favor of the more
 *      general Ns_TclRegisterTrace. In particular, they do not take a
 *      virtual server argument so must assume the currently
 *      initializing server is the intended server.
 *
 * Results:
 *      See Ns_TclRegisterTrace.
 *
 * Side effects:
 *      See Ns_TclRegisterTrace.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclRegisterAtCreate(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_CREATE);
}

int
Ns_TclRegisterAtCleanup(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_DEALLOCATE);
}

int
Ns_TclRegisterAtDelete(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_DELETE);
}

static int
RegisterAt(Ns_TclTraceProc *proc, void *arg, int when)
{
    NsServer *servPtr;

    servPtr = NsGetInitServer();
    if (servPtr == NULL) {
        return NS_ERROR;
    }
    return Ns_TclRegisterTrace(servPtr->server, proc, arg, when);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitInterps --
 *
 *      Arrange for the given proc to be called on newly created
 *      interps.  This routine now simply uses the more general Tcl
 *      interp tracing facility.  Earlier versions would invoke the
 *      given proc immediately on each interp in a shared pool which
 *      explains this otherwise misnamed API.
 *
 * Results:
 *      See Ns_TclRegisterTrace.
 *
 * Side effects:
 *      See Ns_TclRegisterTrace.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitInterps(CONST char *server, Ns_TclInterpInitProc *proc, void *arg)
{
    return Ns_TclRegisterTrace(server, proc, arg, NS_TCL_TRACE_CREATE);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterDeferred --
 *
 *      Register a procedure to be called when the interp is deallocated.
 *      This is a one-shot FIFO order callback mechanism which is seldom
 *      used.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Procedure will be called later.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclRegisterDeferred(Tcl_Interp *interp, Ns_TclDeferProc *proc, void *arg)
{
    NsInterp   *itPtr = NsGetInterpData(interp);
    Defer      *deferPtr, **nextPtrPtr;

    if (itPtr == NULL) {
        return;
    }
    deferPtr = ns_malloc(sizeof(Defer));
    deferPtr->proc = proc;
    deferPtr->arg = arg;
    deferPtr->nextPtr = NULL;
    nextPtrPtr = &itPtr->firstDeferPtr;
    while (*nextPtrPtr != NULL) {
        nextPtrPtr = &((*nextPtrPtr)->nextPtr);
    }
    *nextPtrPtr = deferPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclLibrary --
 *
 *      Return the name of the private tcl lib if configured, or the
 *      global shared library otherwise.
 *
 * Results:
 *      Tcl lib name.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclLibrary(CONST char *server)
{
    NsServer *servPtr = NsGetServer(server);

    return (servPtr ? servPtr->tcl.library : nsconf.tcl.sharedlibrary);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInterpServer --
 *
 *      Return the name of the server.
 *
 * Results:
 *      Server name, or NULL if not a server interp.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclInterpServer(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterpData(interp);

    if (itPtr != NULL && itPtr->servPtr != NULL) {
        return itPtr->servPtr->server;
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitModule --
 *
 *      Add a module name to the init list.
 *
 * Results:
 *      NS_ERROR if no such server, NS_OK otherwise.
 *
 * Side effects:
 *      Module will be initialized by the init script later.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitModule(CONST char *server, CONST char *module)
{
    NsServer *servPtr;

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
        return NS_ERROR;
    }
    (void) Tcl_ListObjAppendElement(NULL, servPtr->tcl.modules,
                                    Tcl_NewStringObj(module, -1));
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclICtlObjCmd --
 *
 *      Implements ns_ictl command to control interp state for
 *      virtual server interps.  This command provide internal control
 *      functions required by the init.tcl script and is not intended
 *      to be called by a user directly.  It supports four activities:
 *
 *      1. Managing the list of "modules" to initialize.
 *      2. Saving the init script for evaluation with new interps.
 *      3. Checking for change of the init script.
 *      4. Register script-level traces.
 *
 *      See init.tcl for details.
 *
 * Results:
 *      Standar Tcl result.
 *
 * Side effects:
 *      May update current saved server Tcl state.
 *
 *----------------------------------------------------------------------
 */

int
NsTclICtlObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp       *itPtr = arg;
    NsServer       *servPtr = itPtr->servPtr;
    TclTrace       *tracePtr;
    Defer          *deferPtr;
    Ns_TclCallback *cbPtr;
    Tcl_Obj        *scriptObj;
    Ns_DString      ds;
    char           *script;
    int             remain = 0, opt, length, when = 0, result = TCL_OK;

    static CONST char *opts[] = {
        "addmodule", "cleanup", "epoch", "get", "getmodules",
        "gettraces", "markfordelete", "oncreate", "oncleanup", "ondelete",
        "oninit", "runtraces", "save", "trace", "update",
        NULL
    };
    enum {
        IAddModuleIdx, ICleanupIdx, IEpochIdx, IGetIdx, IGetModulesIdx,
        IGetTracesIdx, IMarkForDeleteIdx, IOnCreateIdx, IOnCleanupIdx, IOnDeleteIdx,
        IOnInitIdx, IRunTracesIdx, ISaveIdx, ITraceIdx, IUpdateIdx
    };
    Ns_ObjvTable traceWhen[] = {
        {"create",     NS_TCL_TRACE_CREATE},
        {"delete",     NS_TCL_TRACE_DELETE},
        {"allocate",   NS_TCL_TRACE_ALLOCATE},
        {"deallocate", NS_TCL_TRACE_DEALLOCATE},
        {"getconn",    NS_TCL_TRACE_GETCONN},
        {"freeconn",   NS_TCL_TRACE_FREECONN},
        {NULL, 0}
    };
    Ns_ObjvSpec addTraceArgs[] = {
        {"when",       Ns_ObjvFlags,  &when,      traceWhen},
        {"script",     Ns_ObjvObj,    &scriptObj, NULL},
        {"?args",      Ns_ObjvArgs,   &remain,    NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec runTraceArgs[] = {
        {"when",       Ns_ObjvFlags,  &when,      traceWhen},
        {NULL, NULL, NULL, NULL}
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case IAddModuleIdx:
        /*
         * Add a Tcl module to the list for later initialization.
         */

        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "module");
            return TCL_ERROR;
        }
        if (servPtr != NsGetInitServer()) {
            Tcl_SetResult(interp, "cannot add module after server startup",
                          TCL_STATIC);
            return TCL_ERROR;
        }
        if (Tcl_ListObjAppendElement(interp, servPtr->tcl.modules,
                                     objv[2]) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, servPtr->tcl.modules);
        break;

    case IGetModulesIdx:
        /*
         * Get the list of modules for initialization.  See inti.tcl
         * for expected use.
         */

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, servPtr->tcl.modules);
        break;

    case IGetIdx:
        /*
         * Get the current init script to evaluate in new interps.
         */

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
        Ns_RWLockRdLock(&servPtr->tcl.lock);
        Tcl_SetResult(interp, servPtr->tcl.script, TCL_VOLATILE);
        Ns_RWLockUnlock(&servPtr->tcl.lock);
        break;

    case IEpochIdx:
        /*
         * Check the version of this interp against current init script.
         */

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
        Ns_RWLockRdLock(&servPtr->tcl.lock);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(servPtr->tcl.epoch));
        Ns_RWLockUnlock(&servPtr->tcl.lock);
        break;

    case IMarkForDeleteIdx:
        /*
         * The interp will be deleted on next deallocation.
         */

        itPtr->deleteInterp = 1;
        break;

    case ISaveIdx:
        /*
         * Save the init script.
         */

        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
            return TCL_ERROR;
        }
        script = ns_strdup(Tcl_GetStringFromObj(objv[2], &length));
        Ns_RWLockWrLock(&servPtr->tcl.lock);
        ns_free(servPtr->tcl.script);
        servPtr->tcl.script = script;
        servPtr->tcl.length = length;
        if (++servPtr->tcl.epoch == 0) {
            /* NB: Epoch zero reserved for new interps. */
            ++itPtr->servPtr->tcl.epoch;
        }
        Ns_RWLockUnlock(&servPtr->tcl.lock);
        break;

    case IUpdateIdx:
        /*
         * Check for and process possible change in the init script.
         */

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
        result = UpdateInterp(itPtr);
        break;

    case ICleanupIdx:
        /*
         * Invoke the legacy defer callbacks.
         */

        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
        while ((deferPtr = itPtr->firstDeferPtr) != NULL) {
            itPtr->firstDeferPtr = deferPtr->nextPtr;
            (*deferPtr->proc)(interp, deferPtr->arg);
            ns_free(deferPtr);
        }
        break;

    case IOnInitIdx:
    case IOnCreateIdx:
    case IOnCleanupIdx:
    case IOnDeleteIdx:
        /*
         * Register script-level interp traces (deprecated 3-arg form).
         */

        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
            return TCL_ERROR;
        }
        scriptObj = objv[objc-1];

        switch (opt) {
        case IOnInitIdx:
        case IOnCreateIdx:
            when = NS_TCL_TRACE_CREATE;
            break;
        case IOnCleanupIdx:
            when = NS_TCL_TRACE_DEALLOCATE;
            break;
        case IOnDeleteIdx:
            when = NS_TCL_TRACE_DELETE;
            break;
        default:
            /* NB: Silence compiler. */
            break;
        }
        goto trace;
        break;

    case ITraceIdx:
        /*
         * Register script-level interp traces.
         */
        if (Ns_ParseObjv(NULL, addTraceArgs, interp, 2, objc, objv) != NS_OK) {
            return TCL_ERROR;
        }

    trace:
        if (servPtr != NsGetInitServer()) {
            Tcl_SetResult(interp, "cannot register trace after server startup",
                          TCL_STATIC);
            return TCL_ERROR;
        }
        cbPtr = Ns_TclNewCallback(interp, (Ns_Callback *)NsTclTraceProc, 
				  scriptObj, remain, objv + (objc - remain));
        (void) Ns_TclRegisterTrace(servPtr->server, NsTclTraceProc, cbPtr, when);
        break;

    case IGetTracesIdx:
    case IRunTracesIdx:
        if (Ns_ParseObjv(NULL, runTraceArgs, interp, 2, objc, objv) != NS_OK) {
            return TCL_ERROR;
        }
        if (opt == IRunTracesIdx) {
            RunTraces(itPtr, when);
        } else {
            Ns_DStringInit(&ds);
            tracePtr = servPtr->tcl.firstTracePtr;
            while (tracePtr != NULL) {
                if (tracePtr->when & when) {
		  Ns_GetProcInfo(&ds, (void *)tracePtr->proc, tracePtr->arg);
                }
                tracePtr = tracePtr->nextPtr;
            }
            Tcl_DStringResult(interp, &ds);
        }
        break;

    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAtCloseObjCmd --
 *
 *      Implements ns_atclose.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Script will be invoked when the connection is closed.  Note
 *      the connection may continue execution, e.g., with continued
 *      ADP code, traces, etc.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAtCloseObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp  *itPtr = arg;
    AtClose   *atPtr;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "script ?args?");
        return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
        Tcl_SetResult(interp, "no connection", TCL_STATIC);
        return TCL_ERROR;
    }
    atPtr = ns_malloc(sizeof(AtClose));
    atPtr->nextPtr = itPtr->firstAtClosePtr;
    itPtr->firstAtClosePtr = atPtr;
    atPtr->objPtr = Tcl_ConcatObj(objc-1, objv+1);
    Tcl_IncrRefCount(atPtr->objPtr);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRunAtClose --
 *
 *      Run and then free any registered connection at-close scripts.
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
NsTclRunAtClose(NsInterp *itPtr)
{
    Tcl_Interp  *interp = itPtr->interp;
    AtClose     *atPtr;

    while ((atPtr = itPtr->firstAtClosePtr) != NULL) {
        itPtr->firstAtClosePtr = atPtr->nextPtr;
        if (Tcl_EvalObjEx(interp, atPtr->objPtr, TCL_EVAL_DIRECT) != TCL_OK) {
            Ns_TclLogError(interp);
        }
        Tcl_DecrRefCount(atPtr->objPtr);
        ns_free(atPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitServer --
 *
 *      Evaluate server initialization script at startup.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on init script (normally init.tcl).
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitServer(CONST char *server)
{
    NsServer *servPtr = NsGetServer(server);
    Tcl_Interp *interp;

    if (servPtr != NULL) {
        interp = Ns_TclAllocateInterp(server);
        if (Tcl_EvalFile(interp, servPtr->tcl.initfile) != TCL_OK) {
            Ns_TclLogError(interp);
        }
        Ns_TclDeAllocateInterp(interp);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * NsTclAppInit --
 *
 *      Initialize an interactive command interp with basic and
 *      server commands using the default virtual server.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Override Tcl exit command so that propper server shutdown
 *      takes place.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAppInit(Tcl_Interp *interp)
{
    NsServer      *servPtr;

    servPtr = NsGetServer(nsconf.defaultServer);
    if (servPtr == NULL) {
        Ns_Log(Bug, "NsTclAppInit: invalid default server: %s",
               nsconf.defaultServer);
        return TCL_ERROR;
    }
    if (Tcl_Init(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetVar(interp, "tcl_rcFileName", "~/.nsdrc", TCL_GLOBAL_ONLY);
    Tcl_Eval(interp, "proc exit {} ns_shutdown");
    (void) PopInterp(servPtr, interp);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetInterpData --
 *
 *      Return the interp's NsInterp structure from assoc data.
 *      This routine is used when the NsInterp is needed and
 *      not available as command ClientData.
 *
 * Results:
 *      Pointer to NsInterp or NULL if none.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NsInterp *
NsGetInterpData(Tcl_Interp *interp)
{
    return (interp ? Tcl_GetAssocData(interp, "ns:data", NULL) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeConnInterp --
 *
 *      Free the interp data, if any, for given connection.  This
 *      routine is called at the end of connection processing.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      See PushInterp.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeConnInterp(Conn *connPtr)
{
    NsInterp *itPtr = connPtr->itPtr;

    if (itPtr != NULL) {
        RunTraces(itPtr, NS_TCL_TRACE_FREECONN);
        itPtr->conn = NULL;
        itPtr->nsconn.flags = 0;
        PushInterp(itPtr);
        connPtr->itPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTraceProc --
 *
 *      Eval a registered Tcl interp trace callback.
 *
 * Results:
 *      Status from script eval.
 *
 * Side effects:
 *      Depends on script.
 *
 *----------------------------------------------------------------------
 */

int
NsTclTraceProc(Tcl_Interp *interp, void *arg)
{
    Ns_TclCallback *cbPtr = arg;
    int             status;

    status = Ns_TclEvalCallback(interp, cbPtr, NULL, NULL);
    if (status != TCL_OK) {
        Ns_TclLogError(interp);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * PopInterp --
 *
 *      Get virtual-server interp from the per-thread cache and
 *      increment the reference count.  Allocate a new interp if
 *      necessary.
 *
 * Results:
 *      NsInterp.
 *
 * Side effects:
 *      Will invoke alloc traces if not recursively allocated and, if
 *      the interp is new, create traces.
 *
 *----------------------------------------------------------------------
 */

static NsInterp *
PopInterp(NsServer *servPtr, Tcl_Interp *interp)
{
    NsInterp      *itPtr;
    Tcl_HashEntry *hPtr;
    static Ns_Cs   lock;

    /*
     * Get an already initialized interp for the given virtual server
     * on this thread.  If it doesn't yet exist, create and
     * initialize one.
     */
    hPtr = GetCacheEntry(servPtr);
    itPtr = Tcl_GetHashValue(hPtr);
    if (itPtr == NULL) {
        if (nsconf.tcl.lockoninit) {
            Ns_CsEnter(&lock);
        }
        if (interp != NULL) {
            itPtr = NewInterpData(interp, servPtr);
        } else {
            interp = CreateInterp(&itPtr, servPtr);
        }
        if (servPtr != NULL) {
            itPtr->servPtr = servPtr;
            NsTclAddServerCmds(itPtr);
            RunTraces(itPtr, NS_TCL_TRACE_CREATE);
            if (UpdateInterp(itPtr) != TCL_OK) {
                Ns_TclLogError(interp);
            }
        } else {
            RunTraces(itPtr, NS_TCL_TRACE_CREATE);
        }
        if (nsconf.tcl.lockoninit) {
            Ns_CsLeave(&lock);
        }
        Tcl_SetHashValue(hPtr, itPtr);
    }

    /*
     * Run allocation traces once.
     */

    if (++itPtr->refcnt == 1) {
        RunTraces(itPtr, NS_TCL_TRACE_ALLOCATE);
    }

    return itPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * PushInterp --
 *
 *      Return a virtual-server interp to the thread cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      May invoke de-alloc traces, destroy interp if no longer
 *      being used.
 *
 *----------------------------------------------------------------------
 */

static void
PushInterp(NsInterp *itPtr)
{
    Tcl_Interp *interp = itPtr->interp;

    /*
     * Evaluate the dellocation traces once to perform various garbage
     * collection and then either delete the interp or push it back on the
     * per-thread list.
     */

    if (itPtr->refcnt == 1) {
        RunTraces(itPtr, NS_TCL_TRACE_DEALLOCATE);
        if (itPtr->deleteInterp) {
            Ns_Log(Debug, "ns:markfordelete: true");
            Ns_TclDestroyInterp(interp);
            return;
        }
    }
    Tcl_ResetResult(interp);
    itPtr->refcnt--;

    assert(itPtr->refcnt >= 0);
}


/*
 *----------------------------------------------------------------------
 *
 * GetCacheEntry --
 *
 *      Get hash entry in per-thread interp cache for given virtual
 *      server.
 *
 * Results:
 *      Pointer to hash entry.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
GetCacheEntry(NsServer *servPtr)
{
    Tcl_HashTable *tablePtr;
    int ignored;

    tablePtr = Ns_TlsGet(&tls);
    if (tablePtr == NULL) {
        tablePtr = ns_malloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(tablePtr, TCL_ONE_WORD_KEYS);
        Ns_TlsSet(&tls, tablePtr);
    }
    return Tcl_CreateHashEntry(tablePtr, (char *) servPtr, &ignored);
}


/*
 *----------------------------------------------------------------------
 *
 * CreateInterp --
 *
 *      Create a fresh new Tcl interp.
 *
 * Results:
 *      Tcl_Interp pointer.
 *
 * Side effects:
 *      Depends on Tcl library init scripts, errors will be logged.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Interp *
CreateInterp(NsInterp **itPtrPtr, NsServer *servPtr)
{
    NsInterp   *itPtr;
    Tcl_Interp *interp;

    /*
     * Create and initialize a basic Tcl interp.
     */

    interp = Tcl_CreateInterp();
    Tcl_InitMemory(interp);
    if (Tcl_Init(interp) != TCL_OK) {
        Ns_TclLogError(interp);
    }

    /*
     * Allocate and associate a new NsInterp struct for the interp.
     */

    itPtr = NewInterpData(interp, servPtr);
    if (itPtrPtr != NULL) {
        *itPtrPtr = itPtr;
    }

    return interp;
}


/*
 *----------------------------------------------------------------------
 *
 * NewInterpData --
 *
 *      Create a new NsInterp struct for the given interp, adding
 *      basic commands and associating it with the interp.
 *
 * Results:
 *      Pointer to new NsInterp struct.
 *
 * Side effects:
 *      Depends on Tcl init script sourced by Tcl_Init.  Some Tcl
 *      object types will be initialized on first call.
 *
 *----------------------------------------------------------------------
 */

static NsInterp *
NewInterpData(Tcl_Interp *interp, NsServer *servPtr)
{
    static volatile int initialized = 0;
    NsInterp *itPtr;

    /*
     * Core one-time server initialization to add a few Tcl_Obj
     * types.  These calls cannot be in NsTclInit above because
     * Tcl is not fully initialized at libnsd load time.
     */

    if (!initialized) {
        Ns_MasterLock();
        if (!initialized) {
            NsTclInitQueueType();
            NsTclInitAddrType();
            NsTclInitTimeType();
            NsTclInitKeylistType();
            initialized = 1;
        }
        Ns_MasterUnlock();
    }

    /*
     * Allocate and initialize a new NsInterp struct.
     */

    itPtr = NsGetInterpData(interp);
    if (itPtr == NULL) {
        itPtr = ns_calloc(1, sizeof(NsInterp));
        itPtr->interp = interp;
        itPtr->servPtr = servPtr;
        Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
        Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);
        Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);
        NsAdpInit(itPtr);

        /*
         * Associate the new NsInterp with this interp.  At interp delete
         * time, Tcl will call FreeInterpData to cleanup the struct.
         */

        Tcl_SetAssocData(interp, "ns:data", FreeInterpData, itPtr);

        /*
         * Add basic commands which function without a virtual server.
         */

        NsTclAddBasicCmds(itPtr);
    }

    return itPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateInterp --
 *
 *      Update the state of an interp by evaluating the saved script
 *      whenever the epoch changes.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateInterp(NsInterp *itPtr)
{
    NsServer *servPtr = itPtr->servPtr;
    int       result = TCL_OK;

    /*
     * A reader-writer lock is used on the assumption updates are
     * rare and likley expensive to evaluate if the virtual server
     * contains significant state.
     */

    Ns_RWLockRdLock(&servPtr->tcl.lock);
    if (itPtr->epoch != servPtr->tcl.epoch) {
        result = Tcl_EvalEx(itPtr->interp, servPtr->tcl.script,
                            servPtr->tcl.length, TCL_EVAL_GLOBAL);
        itPtr->epoch = servPtr->tcl.epoch;
    }
    Ns_RWLockUnlock(&servPtr->tcl.lock);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * RunTraces, LogTrace --
 *
 *      Execute interp trace callbacks.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depeneds on callbacks. Event may be logged.
 *
 *----------------------------------------------------------------------
 */

static void
RunTraces(NsInterp *itPtr, int why)
{
    TclTrace *tracePtr;
    NsServer *servPtr = itPtr->servPtr;

    if (servPtr != NULL) {

        if ((why & NS_TCL_TRACE_FREECONN)
            || (why & NS_TCL_TRACE_DEALLOCATE)
            || (why & NS_TCL_TRACE_DELETE)) {

            /* Run finalisation traces in LIFO order. */

            tracePtr = servPtr->tcl.lastTracePtr;
            while (tracePtr != NULL) {
                if ((tracePtr->when & why)) {
                    LogTrace(itPtr, tracePtr, why);
                    if ((*tracePtr->proc)(itPtr->interp, tracePtr->arg) != TCL_OK) {
                        Ns_TclLogError(itPtr->interp);
                    }
                }
                tracePtr = tracePtr->prevPtr;
            }

        } else {
            /* Run initialisation traces in FIFO order. */

            tracePtr = servPtr->tcl.firstTracePtr;
            while (tracePtr != NULL) {
                if ((tracePtr->when & why)) {
                    LogTrace(itPtr, tracePtr, why);
                    if ((*tracePtr->proc)(itPtr->interp, tracePtr->arg) != TCL_OK) {
                        Ns_TclLogError(itPtr->interp);
                    }
                }
                tracePtr = tracePtr->nextPtr;
            }
        }
    }
}

static void
LogTrace(NsInterp *itPtr, TclTrace *tracePtr, int why)
{
    Ns_DString  ds;

    if (Ns_LogSeverityEnabled(Debug)) {
        Ns_DStringInit(&ds);
        switch (why) {
        case NS_TCL_TRACE_CREATE:
            Tcl_DStringAppendElement(&ds, "create");
            break;
        case NS_TCL_TRACE_DELETE:
            Tcl_DStringAppendElement(&ds, "delete");
            break;
        case NS_TCL_TRACE_ALLOCATE:
            Tcl_DStringAppendElement(&ds, "allocate");
            break;
        case NS_TCL_TRACE_DEALLOCATE:
            Tcl_DStringAppendElement(&ds, "deallocate");
            break;
        case NS_TCL_TRACE_GETCONN:
            Tcl_DStringAppendElement(&ds, "getconn");
            break;
        case NS_TCL_TRACE_FREECONN:
            Tcl_DStringAppendElement(&ds, "freeconn");
            break;
        }
        Ns_GetProcInfo(&ds, (void *)tracePtr->proc, tracePtr->arg);
        Ns_Log(Debug, "ns:interptrace[%s]: %s",
               itPtr->servPtr->server, Ns_DStringValue(&ds));
        Ns_DStringFree(&ds);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FreeInterpData --
 *
 *      Tcl assoc data callback to destroy the per-interp NsInterp
 *      structure at interp delete time.
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
FreeInterpData(ClientData arg, Tcl_Interp *interp)
{
    NsInterp *itPtr = arg;

    NsAdpFree(itPtr);
    Tcl_DeleteHashTable(&itPtr->sets);
    Tcl_DeleteHashTable(&itPtr->chans);
    Tcl_DeleteHashTable(&itPtr->https);

    ns_free(itPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * DeleteInterps --
 *
 *      Tls callback to delete all cache virtual-server interps at
 *      thread exit time.
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
DeleteInterps(void *arg)
{
    Tcl_HashTable  *tablePtr = arg;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;
    NsInterp       *itPtr;
 
    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
        if ((itPtr = Tcl_GetHashValue(hPtr)) != NULL) {
	    if (itPtr->interp) {
	        Ns_TclDestroyInterp(itPtr->interp);
	    }
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}

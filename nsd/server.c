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
 * server.c --
 *
 *      Routines for managing NsServer structures.
 */

#include "nsd.h"

/*
 * The following structure maintains virtual server
 * configuration callbacks.
 */

typedef struct ServerInit {
    struct ServerInit  *nextPtr;
    Ns_ServerInitProc  *proc;
} ServerInit;


/*
 * Local functions defined in this file.
 */

static void CreatePool(NsServer *servPtr, char *pool);


/*
 * Static variables defined in this file.
 */

static NsServer   *initServPtr;  /* Currently initializing server. */

static ServerInit *firstInitPtr; /* First in list of server config callbacks. */
static ServerInit *lastInitPtr;  /* Last in list of server config callbacks. */


/*
 *----------------------------------------------------------------------
 *
 * NsGetServer --
 *
 *      Return the NsServer structure, allocating if necessary.
 *
 * Results:
 *      Pointer to NsServer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NsServer *
NsGetServer(CONST char *server)
{
    Tcl_HashEntry *hPtr;

    if (server != NULL) {
        hPtr = Tcl_FindHashEntry(&nsconf.servertable, server);
        if (hPtr != NULL) {
            return Tcl_GetHashValue(hPtr);
        }
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetInitServer --
 *
 *      Return the currently initializing server.
 *
 * Results:
 *      Pointer to NsServer.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

NsServer *
NsGetInitServer(void)
{
    return initServPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartServers --
 *
 *      Start all configured servers.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      See NsStartServer.
 *
 *----------------------------------------------------------------------
 */

void
NsStartServers(void)
{
    NsServer      *servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&nsconf.servertable, &search);
    while (hPtr != NULL) {
        servPtr = Tcl_GetHashValue(hPtr);
        NsStartServer(servPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopServers --
 *
 *      Signal stop and wait for all configured servers.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      See NsStopServer and NsWaitServer.
 *
 *----------------------------------------------------------------------
 */

void
NsStopServers(Ns_Time *toPtr)
{
    NsServer      *servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&nsconf.servertable, &search);
    while (hPtr != NULL) {
        servPtr = Tcl_GetHashValue(hPtr);
        NsStopServer(servPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
    hPtr = Tcl_FirstHashEntry(&nsconf.servertable, &search);
    while (hPtr != NULL) {
        servPtr = Tcl_GetHashValue(hPtr);
        NsWaitServer(servPtr, toPtr);
        hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitServer --
 *
 *      Initialize a virtual server and all its crazy state.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Server will later be started.
 *
 *----------------------------------------------------------------------
 */

void
NsInitServer(char *server, Ns_ServerInitProc *staticInitProc)
{
    Tcl_HashEntry     *hPtr;
    Ns_DString         ds;
    NsServer          *servPtr;
    ServerInit        *initPtr;
    CONST char        *path, *p;
    Ns_Set            *set;
    int                i, n;

    hPtr = Tcl_CreateHashEntry(&nsconf.servertable, server, &n);
    if (!n) {
        Ns_Log(Error, "duplicate server: %s", server);
        return;
    }

    /*
     * Create a new NsServer.
     */

    servPtr = ns_calloc(1, sizeof(NsServer));
    servPtr->server = server;

    Tcl_SetHashValue(hPtr, servPtr);
    Tcl_DStringAppendElement(&nsconf.servers, server);
    initServPtr = servPtr;

    /*
     * Run the library init procs in the order they were registerd.
     */

    initPtr = firstInitPtr;
    while (initPtr != NULL) {
        (void) (*initPtr->proc)(server);
        initPtr = initPtr->nextPtr;
    }

    Ns_DStringInit(&ds);
    path = Ns_ConfigGetPath(server, NULL, NULL);

    /*
     * Set some server options.
     */

    servPtr->opts.realm = Ns_ConfigString(path, "realm", server);
    servPtr->opts.modsince = Ns_ConfigBool(path, "checkmodifiedsince", NS_TRUE);
    servPtr->opts.noticedetail = Ns_ConfigBool(path, "noticedetail", NS_TRUE);
    servPtr->opts.errorminsize = Ns_ConfigInt(path, "errorminsize", 514);
    /* Does not seem to be used
    servPtr->opts.flushcontent = Ns_ConfigBool(path, "flushcontent", NS_FALSE);
    */
    servPtr->opts.hdrcase = Preserve;
    p = Ns_ConfigString(path, "headercase", "preserve");
    if (STRIEQ(p, "tolower")) {
        servPtr->opts.hdrcase = ToLower;
    } else if (STRIEQ(p, "toupper")) {
        servPtr->opts.hdrcase = ToUpper;
    }

    /*
     * Initialize on-the-fly compression support.
     */

    servPtr->compress.enable = Ns_ConfigBool(path, "compressenable", NS_FALSE);
    servPtr->compress.level = Ns_ConfigIntRange(path, "compresslevel", 4, 1, 9);
    servPtr->compress.minsize = Ns_ConfigIntRange(path, "compressminsize", 512, 0, INT_MAX);

    /*
     * Call the static server init proc, if any, which may register
     * static modules.
     */

    if (staticInitProc != NULL) {
        (void) (*staticInitProc)(server);
    }

    /*
     * Load modules and initialize Tcl.  The order is significant.
     */

    Ns_MutexInit(&servPtr->pools.lock);
    Ns_MutexSetName2(&servPtr->pools.lock, "nsd:queue", server);
    Ns_MutexInit(&servPtr->filter.lock);
    Ns_MutexSetName2(&servPtr->filter.lock, "nsd:filter", server);

    CreatePool(servPtr, "");
    path = Ns_ConfigGetPath(server, NULL, "pools", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
        CreatePool(servPtr, Ns_SetKey(set, i));
    }
    NsTclInitServer(server);
    NsInitStaticModules(server);
    initServPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NsRegisterServerInit --
 *
 *      Add a libnsd Ns_ServerInitProc to the end of the virtual server
 *      initialisation list.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Proc will be called when virtual server is initialised.
 *
 *----------------------------------------------------------------------
 */

void
NsRegisterServerInit(Ns_ServerInitProc *proc)
{
    ServerInit *initPtr;

    initPtr = ns_malloc(sizeof(ServerInit));
    initPtr->proc = proc;
    initPtr->nextPtr = NULL;

    if (firstInitPtr == NULL) {
        firstInitPtr = lastInitPtr = initPtr;
    } else {
        lastInitPtr->nextPtr = initPtr;
        lastInitPtr = initPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CreatePool --
 *
 *      Create a connection thread pool.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Requests for specified URL's will be handled by given pool.
 *
 *----------------------------------------------------------------------
 */

static void
CreatePool(NsServer *servPtr, char *pool)
{
    ConnPool *poolPtr;
    Conn     *connBufPtr, *connPtr;
    int       i, n, maxconns, threshold;
    char     *path;
    Ns_Set   *set;

    poolPtr = ns_calloc(1, sizeof(ConnPool));
    poolPtr->pool = pool;
    poolPtr->servPtr = servPtr;
    if (*pool == '\0') {
        /* NB: Default options from pre-4.0 ns/server/server1 section. */
        path = Ns_ConfigGetPath(servPtr->server, NULL, NULL);
        servPtr->pools.defaultPtr = poolPtr;
    } else {

        /*
         * Map requested method/URL's to this pool.
         */

        path = Ns_ConfigGetPath(servPtr->server, NULL, "pool", pool, NULL);
        set = Ns_ConfigGetSection(path);
        for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
            if (!strcasecmp(Ns_SetKey(set, i), "map")) {
                NsMapPool(poolPtr, Ns_SetValue(set, i));
            }
        }
    }

    poolPtr->nextPtr = servPtr->pools.firstPtr;
    servPtr->pools.firstPtr = poolPtr;

    /*
     * Pre-allocate all available connection structures to avoid having
     * to repeatedly allocate and free them at run time and to ensure there
     * is a per-set maximum number of simultaneous connections to handle
     * before NsQueueConn begins to return NS_ERROR.
     */

    maxconns = Ns_ConfigIntRange(path, "maxconnections", 100, 1, INT_MAX);
    connBufPtr = ns_calloc((size_t) maxconns, sizeof(Conn));
    for (n = 0; n < maxconns - 1; ++n) {
        connPtr = &connBufPtr[n];
        connPtr->nextPtr = &connBufPtr[n+1];
        Ns_CompressInit(&connPtr->stream);
    }
    connBufPtr[n].nextPtr = NULL;
    poolPtr->queue.freePtr = &connBufPtr[0];

    poolPtr->threads.max =
        Ns_ConfigIntRange(path, "maxthreads", 10, 0, maxconns);
    poolPtr->threads.min =
        Ns_ConfigIntRange(path, "minthreads", 1, 1, poolPtr->threads.max);
    poolPtr->threads.timeout =
        Ns_ConfigIntRange(path, "threadtimeout", 120, 0, INT_MAX);
    threshold =
      Ns_ConfigIntRange(path, "concurrentcreatethreshold", 80, 0, 100);

    if (threshold == 0) {
        /* 
	 * Eager parallel thread creation: allow parallel
	 * connectionthread creation whenever requests are queued.
	 */
        poolPtr->queue.highwatermark = 0;
    } else if (threshold == 100) {
        /* 
	 * No parallel thread creation 
	 */
        poolPtr->queue.highwatermark = INT_MAX;
    } else {
        /* 
	 * Parallel threads are allowed, when the number of queued
	 * requests is higher than the configured value. The value is
	 * specified as the percentage of the queue size.
	 *
	 * Since every connection thread requires a connection, the
	 * actual number of slots for queued requests is (maxconns -
	 * maxthreads).
	 */
        int queueDepth = maxconns - poolPtr->threads.max;

        poolPtr->queue.highwatermark = (queueDepth * threshold) / 100;
    }
}

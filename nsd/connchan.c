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
 * connchan.c --
 *
 *      Support functions for connection channels
 */

#include "nsd.h"


/*
 * Structure handling one registered channel for the [ns_connchan]
 * command.
 */

typedef struct {
    const char      *channelName;
    char             peer[NS_IPADDR_SIZE];  /* Client peer address */
    size_t           rBytes;
    size_t           wBytes;
    bool             binary;
    Ns_Time          startTime;
    Sock            *sockPtr;
    Ns_Time          recvTimeout;
    Ns_Time          sendTimeout;
    const char      *clientData;
    struct Callback *cbPtr;
} NsConnChan;


typedef struct Callback {
    NsConnChan  *connChanPtr;
    const char  *threadName;
    unsigned int when;
    size_t       scriptLength;
    char         script[1];
} Callback;


/*
 * Local functions defined in this file
 */

static void CancelCallback(const NsConnChan *connChanPtr)
    NS_GNUC_NONNULL(1);

static NsConnChan *ConnChanCreate(NsServer *servPtr, Sock *sockPtr,
                                  const Ns_Time *startTime, const char *peer, bool binary, 
                                  const char *clientData) 
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3) NS_GNUC_NONNULL(4)
    NS_GNUC_RETURNS_NONNULL;

static void ConnChanFree(NsConnChan *connChanPtr) 
    NS_GNUC_NONNULL(1);

static NsConnChan *ConnChanGet(Tcl_Interp *interp, NsServer *servPtr, const char *name)
    NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3);

static Ns_ReturnCode SockCallbackRegister(NsConnChan *connChanPtr, const char *script, unsigned int when, const Ns_Time *timeoutPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static ssize_t DriverRecv(Sock *sockPtr, struct iovec *bufs, int nbufs, Ns_Time *timeoutPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(4);

static ssize_t DriverSend(Tcl_Interp *interp, const NsConnChan *connChanPtr, struct iovec *bufs, int nbufs, unsigned int flags, const Ns_Time *timeoutPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(6);

static Ns_SockProc NsTclConnChanProc;

static Tcl_ObjCmdProc   ConnChanCallbackObjCmd;
static Tcl_ObjCmdProc   ConnChanCloseObjCmd;
static Tcl_ObjCmdProc   ConnChanDetachObjCmd;
static Tcl_ObjCmdProc   ConnChanExistsObjCmd;
static Tcl_ObjCmdProc   ConnChanListObjCmd;
static Tcl_ObjCmdProc   ConnChanOpenObjCmd;
static Tcl_ObjCmdProc   ConnChanReadObjCmd;
static Tcl_ObjCmdProc   ConnChanWriteObjCmd;

static Ns_SockProc CallbackFree;


/*
 *----------------------------------------------------------------------
 *
 * CallbackFree --
 *
 *    Free Callback structure and unregister socket callback. This
 *    function is itself implemented as a callback (Ns_SockProc),
 *    which is called, whenever a callback is freed from the socket
 *    thread. Not that it is necessary to implement it as a callback,
 *    since all sock callbacks are implemented via a queue operation
 *    (in sockcallback.c).
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Freeing memory.
 *
 *----------------------------------------------------------------------
 */

static bool CallbackFree(NS_SOCKET UNUSED(sock), void *arg, unsigned int why) {
    bool result;

    if (why != (unsigned int)NS_SOCK_CANCEL) {
        Ns_Log(Warning, "connchan: callback free operation called with unexpected reason code %u", why);
        result = NS_FALSE;
        
    } else {
        Callback *cbPtr = arg;

        ns_free(cbPtr);
        result = NS_TRUE;
    }
    
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * CancelCallback --
 *
 *    Register socket callback cancel operation for unregistering the
 *    socket callback.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Freeing memory.
 *
 *----------------------------------------------------------------------
 */

static void
CancelCallback(const NsConnChan *connChanPtr)
{
    NS_NONNULL_ASSERT(connChanPtr != NULL);
    NS_NONNULL_ASSERT(connChanPtr->cbPtr != NULL);

    (void)Ns_SockCancelCallbackEx(connChanPtr->sockPtr->sock, CallbackFree, connChanPtr->cbPtr, NULL);    
}


/*
 *----------------------------------------------------------------------
 *
 * ConnChanCreate --
 *
 *    Allocate a connecion channel strucuture and initialize its fields.
 *
 * Results:
 *    Initialized connection channel structure.
 *
 * Side effects:
 *    Allocating memory.
 *
 *----------------------------------------------------------------------
 */
static NsConnChan *
ConnChanCreate(NsServer *servPtr, Sock *sockPtr,
               const Ns_Time *startTime, const char *peer, bool binary,
               const char *clientData) {
    static uintptr_t     connchanCount = 0;
    NsConnChan          *connChanPtr;
    Tcl_HashEntry       *hPtr;
    char                 name[5 + TCL_INTEGER_SPACE];
    int                  isNew;
    
    NS_NONNULL_ASSERT(servPtr != NULL);
    NS_NONNULL_ASSERT(sockPtr != NULL);
    NS_NONNULL_ASSERT(startTime != NULL);
    NS_NONNULL_ASSERT(peer != NULL);

    /*
     * Lock the channel table and create a new entry for the
     * connection.
     */

    Ns_MutexLock(&servPtr->connchans.lock);
    snprintf(name, sizeof(name), "conn%" PRIuPTR, connchanCount ++);
    hPtr = Tcl_CreateHashEntry(&servPtr->connchans.table, name, &isNew);
    Ns_MutexUnlock(&servPtr->connchans.lock);

    if (likely(isNew == 0)) {
        Ns_Log(Warning, "duplicate connchan name '%s'", name);
    }
   
    connChanPtr = ns_malloc(sizeof(NsConnChan));
    Tcl_SetHashValue(hPtr, connChanPtr);
        
    connChanPtr->channelName = ns_strdup(name);
    connChanPtr->cbPtr = NULL;
    connChanPtr->startTime = *startTime;
    connChanPtr->rBytes = 0;
    connChanPtr->wBytes = 0;
    connChanPtr->recvTimeout.sec = 0;
    connChanPtr->recvTimeout.usec = 0;
    connChanPtr->sendTimeout.sec = 0;
    connChanPtr->sendTimeout.usec = 0;
    connChanPtr->clientData = clientData != NULL ? ns_strdup(clientData) : NULL;

    strncpy(connChanPtr->peer, peer, NS_IPADDR_SIZE);
    connChanPtr->sockPtr = sockPtr;
    connChanPtr->binary = binary;

    return connChanPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnChanFree --
 *
 *    Free NsConnChan structure and remove the entry from the hash
 *    table of open connection channel structures.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Freeing memory
 *
 *----------------------------------------------------------------------
 */
static void
ConnChanFree(NsConnChan *connChanPtr) {
    NsServer      *servPtr;
    Tcl_HashEntry *hPtr;

    NS_NONNULL_ASSERT(connChanPtr != NULL);
    
    assert(connChanPtr->sockPtr != NULL);
    assert(connChanPtr->sockPtr->servPtr != NULL);

    servPtr = connChanPtr->sockPtr->servPtr;
    /*
     * Remove entry from hash table.
     */
    Ns_MutexLock(&servPtr->connchans.lock);
    hPtr = Tcl_FindHashEntry(&servPtr->connchans.table, connChanPtr->channelName);
    if (hPtr != NULL) {
        Tcl_DeleteHashEntry(hPtr);
    } else {
        Ns_Log(Error, "ns_connchan: could not delete hash entry for channel '%s'",
               connChanPtr->channelName);
    }
    Ns_MutexUnlock(&servPtr->connchans.lock);


    if (hPtr != NULL) {
        /*
         * Only in cases, where we found the entry, we can free the
         * connChanPtr content.
         */
        if (connChanPtr->cbPtr != NULL) {
            /*
             * Add CancelCallback() to the sock callback queue.
             */
            CancelCallback(connChanPtr);
            /*
             * There might be a race condition, when a previously
             * registered callback is currently active (or going to be
             * processed). So make sure, it won't access a stale
             * connChanPtr member.
             */
            connChanPtr->cbPtr->connChanPtr = NULL;
            /*
             * The cancel callback takes care about freeing the
             * actual callback.
             */
            connChanPtr->cbPtr = NULL;
        }
        ns_free((char *)connChanPtr->channelName);
        if (connChanPtr->clientData != NULL) {
            ns_free((char *)connChanPtr->clientData);
        }

        NsSockClose(connChanPtr->sockPtr, (int)NS_FALSE);
        ns_free((char *)connChanPtr);
    } else {
        Ns_Log(Bug, "ns_connchan: could not delete hash entry for channel '%s'",
               connChanPtr->channelName);
    }

}


/*
 *----------------------------------------------------------------------
 *
 * ConnChanGet --
 *
 *    Access a NsConnChan from the per-server table via its name.
 *
 * Results:
 *    ConnChan* or NULL if not found.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */
static NsConnChan *
ConnChanGet(Tcl_Interp *interp, NsServer *servPtr, const char *name) {
    const Tcl_HashEntry *hPtr;
    NsConnChan          *connChanPtr = NULL;

    NS_NONNULL_ASSERT(servPtr != NULL);
    NS_NONNULL_ASSERT(name != NULL);
    
    Ns_MutexLock(&servPtr->connchans.lock);
    hPtr = Tcl_FindHashEntry(&servPtr->connchans.table, name);
    if (hPtr != NULL) {
        connChanPtr = (NsConnChan *)Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&servPtr->connchans.lock);

    if (connChanPtr == NULL && interp != NULL) {
        Ns_TclPrintfResult(interp, "connchan \"%s\" does not exist", name);
    }

    return connChanPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnChanProc --
 *
 *      A wrapper function callback that is called, when the callback
 *      is fired. The function allocates an interpreter if necessary,
 *      builds the argument list for invocation and calls the
 *      registered Tcl script.
 *
 * Results:
 *      NS_TRUE or NS_FALSE on error 
 *
 * Side effects:
 *      Will run Tcl script. 
 *
 *----------------------------------------------------------------------
 */

static bool
NsTclConnChanProc(NS_SOCKET UNUSED(sock), void *arg, unsigned int why)
{

    const Callback *cbPtr;
    bool            success = NS_TRUE;

    NS_NONNULL_ASSERT(arg != NULL);

    cbPtr = arg;

    if (cbPtr->connChanPtr == NULL) {
        /*
         * Safety belt.
         */
        Ns_Log(Ns_LogConnchanDebug, "NsTclConnChanProc called on a probably deleted callback %p", (void*)cbPtr);
        success = NS_FALSE;
        
    } else {
        /*
         * We should have a valid callback structure, that we test
         * with asserts.
         */
        Ns_Log(Ns_LogConnchanDebug, "NsTclConnChanProc why %u", why);
    
        assert(cbPtr->connChanPtr != NULL);
        assert(cbPtr->connChanPtr->sockPtr != NULL);

        if (why == (unsigned int)NS_SOCK_EXIT) {
            /*
             * Treat the "exit" case like error cases and free in such
             * cases the connChanPtr structure.
             */
            success = NS_FALSE;
            
        } else {
            int             result;
            Tcl_DString     script;
            Tcl_Interp     *interp;
            const char     *w;

            /*
             * In all remaining cases, the Tcl callback is executed.
             */
            assert(cbPtr->connChanPtr->sockPtr->servPtr != NULL);
            
            Tcl_DStringInit(&script);
            Tcl_DStringAppend(&script, cbPtr->script, (int)cbPtr->scriptLength);
    
            if ((why & (unsigned int)NS_SOCK_TIMEOUT) != 0u) {
                w = "t";
            } else if ((why & (unsigned int)NS_SOCK_READ) != 0u) {
                w = "r";
            } else if ((why & (unsigned int)NS_SOCK_WRITE) != 0u) {
                w = "w";
            } else if ((why & (unsigned int)NS_SOCK_EXCEPTION) != 0u) {
                w = "e";
            } else {
                w = "x";
            }
        
            Tcl_DStringAppendElement(&script, w);
            interp = NsTclAllocateInterp(cbPtr->connChanPtr->sockPtr->servPtr);
            result = Tcl_EvalEx(interp, script.string, script.length, 0);
    
            if (result != TCL_OK) {
                (void) Ns_TclLogErrorInfo(interp, "\n(context: connchan proc)");
            } else {
                Tcl_Obj *objPtr = Tcl_GetObjResult(interp);
                int      ok = 1;

                /*
                 * The Tcl callback can signal with the result "0",
                 * that the connection channel should be closed
                 * automatically.
                 */
                Ns_Log(Ns_LogConnchanDebug, "NsTclConnChanProc: Tcl eval returned <%s>", Tcl_GetString(objPtr));
                result = Tcl_GetBooleanFromObj(interp, objPtr, &ok);
                if ((result == TCL_OK) && (ok == 0)) {
                    result = TCL_ERROR;
                }
            }
            Ns_TclDeAllocateInterp(interp);
            Tcl_DStringFree(&script);
    
            if (result != TCL_OK) {
                success = NS_FALSE;
            }
        }
        
        if (!success) {
            if (cbPtr->connChanPtr != NULL) {
                ConnChanFree(cbPtr->connChanPtr);
            }
        }
    }
    return success;
}


/*
 *----------------------------------------------------------------------
 *
 * SockCallbackRegister --
 *
 *      Register a callback for the connection channel. Due to the
 *      underlying infrastructure, one socket has at most one callback
 *      registered.
 *
 * Results:
 *      Tcl result code.
 *
 * Side effects:
 *      Memory management for the callback strucuture.
 *
 *----------------------------------------------------------------------
 */

static Ns_ReturnCode
SockCallbackRegister(NsConnChan *connChanPtr, const char *script,
                     unsigned int when, const Ns_Time *timeoutPtr)
{
    Callback     *cbPtr;
    size_t        scriptLength;
    Ns_ReturnCode result;

    NS_NONNULL_ASSERT(connChanPtr != NULL);
    NS_NONNULL_ASSERT(script != NULL);

    scriptLength = strlen(script);
    
    /*
     * If there is already a callback registered, free and cancel
     * it. This has to be done as first step, since CancelCallback()
     * calls finally Ns_SockCancelCallbackEx(), which deletes all
     * callbacks registered for the associated socket.
     */
    if (connChanPtr->cbPtr != NULL) {
        cbPtr = ns_realloc(connChanPtr->cbPtr, sizeof(Callback) + (size_t)scriptLength);
    } else {
        cbPtr = ns_malloc(sizeof(Callback) + (size_t)scriptLength);
    }
    memcpy(cbPtr->script, script, scriptLength + 1u);
    cbPtr->scriptLength = scriptLength;
    cbPtr->when = when;
    cbPtr->threadName = NULL;
    cbPtr->connChanPtr = connChanPtr;
    
    result = Ns_SockCallbackEx(connChanPtr->sockPtr->sock, NsTclConnChanProc, cbPtr,
                               when | (unsigned int)NS_SOCK_EXIT, 
                               timeoutPtr, &cbPtr->threadName);
    if (result == NS_OK) {
        connChanPtr->cbPtr = cbPtr;
    } else {
        /*
         * The callback could not be registered, maybe the socket is
         * not valid anymore. Free the callback.
         */
        (void) CallbackFree(connChanPtr->sockPtr->sock, cbPtr, (unsigned int)NS_SOCK_CANCEL);
        connChanPtr->cbPtr = NULL;
    } 
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * DriverRecv --
 *
 *      Read data from the socket into the given vector of buffers.
 *
 * Results:
 *      Number of bytes read, or -1 on error.
 *
 * Side effects:
 *      Depends on driver.
 *
 *----------------------------------------------------------------------
 */

static ssize_t
DriverRecv(Sock *sockPtr, struct iovec *bufs, int nbufs, Ns_Time *timeoutPtr)
{
    Ns_Time timeout;
    ssize_t result;

    NS_NONNULL_ASSERT(sockPtr != NULL);
    NS_NONNULL_ASSERT(bufs != NULL);
    NS_NONNULL_ASSERT(timeoutPtr != NULL);

    if (timeoutPtr->sec == 0 && timeoutPtr->usec == 0) {
        /*
         * Use configured receivewait as timeout.
         */
        timeout.sec = sockPtr->drvPtr->recvwait;
        timeoutPtr = &timeout;
    }
    if (likely(sockPtr->drvPtr->recvProc != NULL)) {
        result = (*sockPtr->drvPtr->recvProc)((Ns_Sock *) sockPtr, bufs, nbufs, timeoutPtr, 0u);
    } else {
        Ns_Log(Warning, "connchan: no recvProc registered for driver %s", sockPtr->drvPtr->moduleName);
        result = -1;
    }
    
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * DriverSend --
 *
 *      Write a vector of buffers to the socket via the driver callback.
 *
 * Results:
 *      Number of bytes written, or -1 on error.
 *
 * Side effects:
 *      Depends on driver.
 *
 *----------------------------------------------------------------------
 */

static ssize_t
DriverSend(Tcl_Interp *interp, const NsConnChan *connChanPtr,
           struct iovec *bufs, int nbufs, unsigned int flags,
           const Ns_Time *timeoutPtr)
{
    Ns_Time  timeout;
    ssize_t  result;
    Sock    *sockPtr;
    
    NS_NONNULL_ASSERT(connChanPtr != NULL);
    NS_NONNULL_ASSERT(timeoutPtr != NULL);

    sockPtr = connChanPtr->sockPtr;
    
    assert(sockPtr != NULL);
    assert(sockPtr->drvPtr != NULL);

    if (timeoutPtr->sec == 0 && timeoutPtr->usec == 0) {
        /*
         * Use configured sendwait as timeout,
         */
        timeout.sec = sockPtr->drvPtr->sendwait;
        timeoutPtr = &timeout;
    }

    if (likely(sockPtr->drvPtr->sendProc != NULL)) {
        bool    haveTimeout = NS_FALSE, partial = NS_FALSE;
        ssize_t nSent = 0, toSend = (ssize_t)Ns_SumVec(bufs, nbufs);

        do {
            /*Ns_Log(Ns_LogConnchanDebug,"DriverSend %s: try to send [0] %" PRIdz " bytes (total %"  PRIdz ")",
                   connChanPtr->channelName,
                   bufs->iov_len, (ssize_t)Ns_SumVec(bufs, nbufs));*/

            result = (*sockPtr->drvPtr->sendProc)((Ns_Sock *) sockPtr, bufs, nbufs,
                                                  timeoutPtr, flags);
            if (result == -1 && ((errno == EAGAIN) || (errno == NS_EWOULDBLOCK))) {
                /*
                 * Retry, when the socket is writeable
                 */
                if (Ns_SockTimedWait(sockPtr->sock, (unsigned int)NS_SOCK_WRITE, timeoutPtr) == NS_OK) {
                    result = (*sockPtr->drvPtr->sendProc)((Ns_Sock *) sockPtr, bufs, nbufs,
                                                          timeoutPtr, flags);
                } else {
                    haveTimeout = NS_TRUE;
                    Ns_TclPrintfResult(interp, "connchan %s: timeout on send operation (%ld:%ld)",
                                       connChanPtr->channelName, timeoutPtr->sec, timeoutPtr->usec);
                    result = -1;
                }
            }

            partial = NS_FALSE;
            if (result != -1) {
                nSent += result;
                if (nSent < toSend) {
                    /*
                     * Partial write operation: part of the iovec has
                     * been sent, we have to retransmit the rest. We
                     * could advance the nbufs counter, but the only
                     * case of nbufs > 0 is the sending of the
                     * headers, which is a one-time operation.
                     */
                    Ns_Log(Ns_LogConnchanDebug,
                           "DriverSend %s: partial write operation, sent %" PRIdz " instead of %" PRIdz " bytes",
                           connChanPtr->channelName, nSent, toSend);
                    (void) Ns_ResetVec(bufs, nbufs, (size_t)nSent);
                    toSend -= result;
                    partial = NS_TRUE;
                }
            } else if (!haveTimeout) {
                /*
                 * Timeout is handled above.
                 */
                Ns_TclPrintfResult(interp, "connchan %s: send operation failed: %s",
                                   connChanPtr->channelName, strerror(errno));
            }

            /*Ns_Log(Notice, "### check result %ld == -1 || %ld == %ld (%d && %d) == %d",
                   result, toSend, nSent,
                   (result != -1), (nSent < toSend), ((result != -1) && (nSent < toSend)));*/
            
        } while (partial && (result != -1));

        
    } else {
        Ns_TclPrintfResult(interp, "connchan %s: no sendProc registered for driver %s",
                           connChanPtr->channelName, sockPtr->drvPtr->moduleName);
        result = -1;
    }

    return result;   
}


/*
 *----------------------------------------------------------------------
 *
 * ConnChanDeatchObjCmd --
 *
 *    Implements the "ns_connchan detach" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanDetachObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const NsInterp *itPtr = clientData;
    Conn           *connPtr = (Conn *)itPtr->conn;
    int             result = TCL_OK;

    if (Ns_ParseObjv(NULL, NULL, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
        
    } else if (connPtr == NULL) {
        Ns_TclPrintfResult(interp, "no current connection");
        result = TCL_ERROR;
        
    } else {
        NsServer         *servPtr = itPtr->servPtr;
        const NsConnChan *connChanPtr;
        
        /*
         * Lock the channel table and create a new entry for the
         * connection. After this operation the channel is responsible
         * for managing the sockPtr, so we have to remove it from the
         * connection structure.
         */
        connChanPtr = ConnChanCreate(servPtr,
                                     connPtr->sockPtr,
                                     Ns_ConnStartTime((Ns_Conn *)connPtr),
                                     connPtr->reqPtr->peer,
                                     (connPtr->flags & NS_CONN_WRITE_ENCODED) != 0u ? NS_FALSE : NS_TRUE,
                                     connPtr->clientData);
        Ns_Log(Ns_LogConnchanDebug, "ConnChanDetachObjCmd sock %d", connPtr->sockPtr->sock);
        connPtr->sockPtr = NULL;

        Tcl_SetObjResult(interp, Tcl_NewStringObj(connChanPtr->channelName, -1));
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanOpenObjCmd --
 *
 *    Implements the "ns_connchan open" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanOpenObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    int           result = TCL_OK;
    Sock         *sockPtr = NULL;
    Ns_Set       *hdrPtr = NULL;
    char         *url, *method = (char *)"GET", *version = (char *)"1.0", *driverName = NULL;
    Ns_Time       timeout = {1, 0}, *timeoutPtr = &timeout; 
    Ns_ObjvSpec   lopts[] = {
        {"-headers", Ns_ObjvSet,    &hdrPtr, NULL},
        {"-method",  Ns_ObjvString, &method, NULL},
        {"-timeout", Ns_ObjvTime,   &timeoutPtr,  NULL},
        {"-version", Ns_ObjvString, &version, NULL},
        {"-driver",  Ns_ObjvString, &driverName, NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec   largs[] = {
        {"url", Ns_ObjvString, &url, NULL},
        {NULL, NULL, NULL, NULL}
    };
    
    if (Ns_ParseObjv(lopts, largs, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        NsConnChan     *connChanPtr;

        result = NSDriverClientOpen(interp, driverName, url, method, version, timeoutPtr, &sockPtr);
        if (likely(result == TCL_OK)) {

            if (STREQ(sockPtr->drvPtr->protocol, "https")) {
                NS_TLS_SSL_CTX *ctx;

                assert(sockPtr->drvPtr->clientInitProc != NULL);

                /* 
                 * For the time being, just pass NULL
                 * structures. Probably, we could create the
                 * SSLcontext.
                 */
                result = Ns_TLS_CtxClientCreate(interp,
                                                NULL /*cert*/, NULL /*caFile*/,
                                                NULL /* caPath*/, NS_FALSE /*verify*/,
                                                &ctx);
                    
                if (likely(result == TCL_OK)) {
                    result = (*sockPtr->drvPtr->clientInitProc)(interp, (Ns_Sock *)sockPtr, ctx);
                        
                    /*
                     * For the time being, we create/delete the ctx in
                     * an eager fashion. We could probably make it
                     * reusable and keep it around.
                     */
                    if (ctx != NULL)  {
                        Ns_TLS_CtxFree(ctx);
                    }
                }
            }

            if (likely(result == TCL_OK)) {
                struct iovec buf[4];
                Ns_Time      now;
                ssize_t      nSent;

                Ns_GetTime(&now);
                connChanPtr = ConnChanCreate(servPtr,
                                             sockPtr,
                                             &now,
                                             sockPtr->reqPtr->peer,
                                             NS_TRUE /* binary, fixed for the time being */,
                                             NULL);
                if (hdrPtr != NULL) {
                    size_t i;
                    
                    for (i = 0u; i < Ns_SetSize(hdrPtr); i++) {
                        const char *key = Ns_SetKey(hdrPtr, i);
                        Ns_DStringPrintf(&sockPtr->reqPtr->buffer, "%s: %s\r\n", key, Ns_SetValue(hdrPtr, i));
                    }
                }
                
                /*
                 * Write the request header via the "send" operation of
                 * the driver.
                 */
                buf[0].iov_base = (void *)sockPtr->reqPtr->request.line;
                buf[0].iov_len  = strlen(buf[0].iov_base);

                buf[1].iov_base = (void *)"\r\n";
                buf[1].iov_len  = 2u;

                buf[2].iov_base = (void *)sockPtr->reqPtr->buffer.string;
                buf[2].iov_len  = (size_t)Tcl_DStringLength(&sockPtr->reqPtr->buffer);

                buf[3].iov_base = (void *)"\r\n";
                buf[3].iov_len  = 2u;

                nSent = DriverSend(interp, connChanPtr, buf, 4, 0u, &connChanPtr->sendTimeout);
                Ns_Log(Ns_LogConnchanDebug, "DriverSend sent %ld bytes <%s>", nSent, strerror(errno));

                if (nSent > -1) {
                    connChanPtr->wBytes += (size_t)nSent;
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(connChanPtr->channelName, -1));
                } else {
                    result = TCL_ERROR;
                }
            }
        }
        
        if (unlikely(result != TCL_OK && sockPtr != NULL && sockPtr->sock > 0)) {
            ns_sockclose(sockPtr->sock);
        }
        
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanListObjCmd --
 *
 *    Implements the "ns_connchan list" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanListObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const NsInterp *itPtr = clientData;
    NsServer       *servPtr = itPtr->servPtr;
    int             result = TCL_OK;
    char           *server = NULL;
    Ns_ObjvSpec     lopts[] = {
        {"-server", Ns_ObjvString, &server, NULL},
        {NULL, NULL, NULL, NULL}
    };
    
    if (Ns_ParseObjv(lopts, NULL, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else if (server != NULL) {
        servPtr = NsGetServer(server);
        if (servPtr == NULL) {
            Ns_TclPrintfResult(interp, "server \"%s\" does not exist", server);
            result = TCL_ERROR;
        }
    }

    if (result == TCL_OK) {
        Tcl_HashSearch  search;
        Tcl_HashEntry  *hPtr;
        Tcl_DString     ds, *dsPtr = &ds;

        /*
         * The provided parameter appear to be valid. Lock the channel
         * table and return the infos for every existing entry in the
         * conneciton channel table.
         */
        Tcl_DStringInit(dsPtr);

        Ns_MutexLock(&servPtr->connchans.lock);
        hPtr = Tcl_FirstHashEntry(&servPtr->connchans.table, &search);
        while (hPtr != NULL) {
            NsConnChan     *connChanPtr;

            connChanPtr = (NsConnChan *)Tcl_GetHashValue(hPtr);
            Ns_DStringPrintf(dsPtr, "{%s %s %" PRIu64 ".%06ld %s %s %" PRIdz " %" PRIdz,
                             (char *)Tcl_GetHashKey(&servPtr->connchans.table, hPtr),
                             ((connChanPtr->cbPtr != NULL && connChanPtr->cbPtr->threadName != NULL) ?
                              connChanPtr->cbPtr->threadName : "{}"),
                             (int64_t) connChanPtr->startTime.sec, connChanPtr->startTime.usec,
                             connChanPtr->sockPtr->drvPtr->moduleName,
                             connChanPtr->peer,
                             connChanPtr->wBytes,
                             connChanPtr->rBytes);
            Ns_DStringAppendElement(dsPtr,
                                    (connChanPtr->clientData != NULL) ? connChanPtr->clientData : "");
            Ns_DStringAppend(dsPtr, "} ");
            hPtr = Tcl_NextHashEntry(&search);
        }
        Ns_MutexUnlock(&servPtr->connchans.lock);

        Tcl_DStringResult(interp, dsPtr);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanCloseObjCmd --
 *
 *    Implements the "ns_connchan close" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanCloseObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    char        *name;
    int          result = TCL_OK;
    Ns_ObjvSpec  args[] = {
        {"channel", Ns_ObjvString, &name, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(NULL, args, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        NsConnChan     *connChanPtr;
        
        connChanPtr = ConnChanGet(interp, servPtr, name);
        Ns_Log(Ns_LogConnchanDebug, "ns_connchan %s close connChanPtr %p", name, (void*)connChanPtr);
                
        if (connChanPtr != NULL) {
            ConnChanFree(connChanPtr);
        } else {
            result = TCL_ERROR;
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanCallbackObjCmd --
 *
 *    Implements the "ns_connchan callback" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanCallbackObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    int      result = TCL_OK;
    char    *name, *script, *whenString;
    Ns_Time *pollTimeoutPtr = NULL, *recvTimeoutPtr = NULL, *sendTimeoutPtr = NULL;
            
    Ns_ObjvSpec lopts[] = {
        {"-timeout",        Ns_ObjvTime, &pollTimeoutPtr, NULL},
        {"-receivetimeout", Ns_ObjvTime, &recvTimeoutPtr, NULL},
        {"-sendtimeout",    Ns_ObjvTime, &sendTimeoutPtr, NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"channel", Ns_ObjvString, &name, NULL},
        {"script",  Ns_ObjvString, &script, NULL},
        {"when",    Ns_ObjvString, &whenString, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(lopts, args, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        NsConnChan     *connChanPtr = ConnChanGet(interp, servPtr, name);

        assert(whenString != NULL);
        
        if (unlikely(connChanPtr == NULL)) {
            result = TCL_ERROR;
        } else {
            /*
             * The provided channel name exists. In a first step get
             * the flags from the when string.
             */
            unsigned int  when = 0u;
            const char   *s = whenString;
                
            while (*s != '\0') {
                if (*s == 'r') {
                    when |= (unsigned int)NS_SOCK_READ;
                } else if (*s == 'w') {
                    when |= (unsigned int)NS_SOCK_WRITE;
                } else if (*s == 'e') {
                    when |= (unsigned int)NS_SOCK_EXCEPTION;
                } else if (*s == 'x') {
                    when |= (unsigned int)NS_SOCK_EXIT;
                } else {
                    Ns_TclPrintfResult(interp, "invalid when specification: \"%s\":"
                                       " should be one/more of r, w, e, or x", whenString);
                    result = TCL_ERROR;
                    break;
                }
                s++;
            }

            if (result == TCL_OK) {
                Ns_ReturnCode status;
                
                /*
                 * Fill in the timeouts, when these are provided.
                 */
                if (recvTimeoutPtr != NULL) {
                    connChanPtr->recvTimeout = *recvTimeoutPtr;
                }
                if (sendTimeoutPtr != NULL) {
                    connChanPtr->sendTimeout = *sendTimeoutPtr;
                }

                /*
                 * Register the callback.
                 */
                status = SockCallbackRegister(connChanPtr, script, when, pollTimeoutPtr);
                
                if (unlikely(status != NS_OK)) {
                    Ns_TclPrintfResult(interp, "could not register callback");
                    ConnChanFree(connChanPtr);
                    result = TCL_ERROR;
                }
            }
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanExistsObjCmd --
 *
 *    Implements the "ns_connchan exists" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanExistsObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    char         *name;
    int           result = TCL_OK;
    Ns_ObjvSpec   args[] = {
        {"channel", Ns_ObjvString, &name, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(NULL, args, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        const NsInterp   *itPtr = clientData;
        NsServer         *servPtr = itPtr->servPtr;
        const NsConnChan *connChanPtr;
        
        connChanPtr = ConnChanGet(interp, servPtr, name);
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(connChanPtr != NULL));
    }
    return result;
}
        

/*
 *----------------------------------------------------------------------
 *
 * ConnChanReadObjCmd --
 *
 *    Implements the "ns_connchan read" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanReadObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    char        *name;
    int          result = TCL_OK;
    Ns_ObjvSpec  args[] = {
        {"channel", Ns_ObjvString, &name, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(NULL, args, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        NsConnChan     *connChanPtr = ConnChanGet(interp, servPtr, name);

        if (unlikely(connChanPtr == NULL)) {
            result = TCL_ERROR;
        } else {
            /*
             * The provided channel exists.
             */
            ssize_t      nRead;
            struct iovec buf;
            char         buffer[4096];

            if (!connChanPtr->binary) {
                Ns_Log(Warning, "ns_connchan: only binary channels are currently supported. "
                       "Channel %s is not binary", name);
            }

            /*
             * Read the data via the "receive" operation of the driver.
             */
            buf.iov_base = buffer;
            buf.iov_len = sizeof(buffer);
            nRead = DriverRecv(connChanPtr->sockPtr, &buf, 1, &connChanPtr->recvTimeout);

            if (nRead > -1) {
                connChanPtr->rBytes += (size_t)nRead;
                Tcl_SetObjResult(interp, Tcl_NewByteArrayObj((unsigned char *)buffer, (int)nRead));
            } else {
                /*
                 * The receive operation failed, maybe a receive
                 * timeout happend.  The read call will simply return
                 * an empty string. We could notice this fact
                 * internally by a timeout counter, but for the time
                 * being no application has usage for it.
                 */
            }
        }
    }

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ConnChanWriteObjCmd --
 *
 *    Implements the "ns_connchan write" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */
static int
ConnChanWriteObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    char       *name;
    int         result = TCL_OK;
    Tcl_Obj    *msgObj;
    Ns_ObjvSpec args[] = {
        {"channel", Ns_ObjvString, &name,   NULL},
        {"msg",     Ns_ObjvObj,    &msgObj, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(NULL, args, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        NsConnChan     *connChanPtr = ConnChanGet(interp, servPtr, name);
        
        if (unlikely(connChanPtr == NULL)) {
            result = TCL_ERROR;
        } else {
            /*
             * The provided channel name exists.
             */
            struct iovec buf;
            ssize_t      nSent;
            int          msgLen;
            const char  *msgString = (const char *)Tcl_GetByteArrayFromObj(msgObj, &msgLen);

            if (!connChanPtr->binary) {
                Ns_Log(Warning, "ns_connchan: only binary channels are currently supported. "
                       "Channel %s is not binary", name);
            }

            /*
             * Write the data via the "send" operation of the driver.
             */
            buf.iov_base = (void *)msgString;
            buf.iov_len = (size_t)msgLen;
            nSent = DriverSend(interp, connChanPtr, &buf, 1, 0u, &connChanPtr->sendTimeout);

            if (nSent > -1) {
                connChanPtr->wBytes += (size_t)nSent;
                Tcl_SetObjResult(interp, Tcl_NewLongObj((long)nSent));
            } else {
                result = TCL_ERROR;
            }
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnChanObjCmd --
 *
 *    Implements the "ns_connchan" command.
 *
 * Results:
 *    Tcl result. 
 *
 * Side effects:
 *    Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */

int
NsTclConnChanObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const Ns_SubCmdSpec subcmds[] = {
        {"callback", ConnChanCallbackObjCmd},
        {"close",    ConnChanCloseObjCmd},
        {"detach",   ConnChanDetachObjCmd},
        {"exists",   ConnChanExistsObjCmd},
        {"list",     ConnChanListObjCmd},
        {"open",     ConnChanOpenObjCmd},
        {"read",     ConnChanReadObjCmd},
        {"write",    ConnChanWriteObjCmd},
        {NULL, NULL}
    };
    
    return Ns_SubcmdObjv(subcmds, clientData, interp, objc, objv);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 70
 * indent-tabs-mode: nil
 * End:
 */

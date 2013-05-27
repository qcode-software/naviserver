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
 * sockcallback.c --
 *
 *	Support for the socket callback thread.
 */

#include "nsd.h"

/*
 * The following defines a socket being monitored.
 */

typedef struct Callback {
    struct Callback     *nextPtr;
    NS_SOCKET            sock;
    int			 idx;
    int                  when;
    int                  timeout;
    time_t               expires;
    Ns_SockProc         *proc;
    void                *arg;
} Callback;

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc SockCallbackThread;
static int Queue(NS_SOCKET sock, Ns_SockProc *proc, void *arg, int when, int timeout);
static void CallbackTrigger(void);

/*
 * Static variables defined in this file
 */

static Callback	    *firstQueuePtr, *lastQueuePtr;
static int	     shutdownPending;
static int	     running;
static Ns_Thread     sockThread;
static Ns_Mutex      lock;
static Ns_Cond	     cond;
static NS_SOCKET     trigPipe[2];
static Tcl_HashTable table;


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCallback --
 *
 *	Register a callback to be run when a socket reaches a certain
 *	state.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	Will wake up the callback thread.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockCallback(NS_SOCKET sock, Ns_SockProc *proc, void *arg, int when)
{
    return Queue(sock, proc, arg, when, 0);
}

int
Ns_SockCallbackEx(NS_SOCKET sock, Ns_SockProc *proc, void *arg, int when, int timeout)
{
    return Queue(sock, proc, arg, when, timeout);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCancelCallback, Ns_SockCancelCallbackEx --
 *
 *	Remove a callback registered on a socket.  Optionally execute
 *	a callback from the SockCallbackThread.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	Will wake up the callback thread.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SockCancelCallback(NS_SOCKET sock)
{
    (void) Ns_SockCancelCallbackEx(sock, NULL, NULL);
}

int
Ns_SockCancelCallbackEx(NS_SOCKET sock, Ns_SockProc *proc, void *arg)
{
    return Queue(sock, proc, arg, NS_SOCK_CANCEL, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartSockShutdown, NsWaitSockShutdown --
 *
 *	Initiate and then wait for socket callbacks shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May timeout waiting for shutdown.
 *
 *----------------------------------------------------------------------
 */

void
NsStartSockShutdown(void)
{
    Ns_MutexLock(&lock);
    if (running) {
	shutdownPending = 1;
	CallbackTrigger();
    }
    Ns_MutexUnlock(&lock);
}

void
NsWaitSockShutdown(Ns_Time *toPtr)
{
    int status;

    status = NS_OK;
    Ns_MutexLock(&lock);
    while (status == NS_OK && running) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "socks: timeout waiting for callback shutdown");
    } else if (sockThread != NULL) {
	Ns_ThreadJoin(&sockThread, NULL);
	sockThread = NULL;
    	ns_sockclose(trigPipe[0]);
    	ns_sockclose(trigPipe[1]);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * CallbackTrigger --
 *
 *	Wakeup the callback thread if it's in poll().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CallbackTrigger(void)
{
    if (send(trigPipe[1], "", 1, 0) != 1) {
	Ns_Fatal("trigger send() failed: %s", ns_sockstrerror(ns_sockerrno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Queue --
 *
 *	Queue a callback for socket.
 *
 * Results:
 *	NS_OK or NS_ERROR on shutdown pending.
 *
 * Side effects:
 *	Socket thread may be created or signalled.
 *
 *----------------------------------------------------------------------
 */

static int
Queue(NS_SOCKET sock, Ns_SockProc *proc, void *arg, int when, int timeout)
{
    Callback   *cbPtr;
    int         status, trigger, create;

    cbPtr = ns_calloc(1, sizeof(Callback));
    cbPtr->sock = sock;
    cbPtr->proc = proc;
    cbPtr->arg = arg;
    cbPtr->when = when;
    cbPtr->timeout = timeout;
    trigger = create = 0;
    Ns_MutexLock(&lock);
    if (shutdownPending) {
	ns_free(cbPtr);
    	status = NS_ERROR;
    } else {
	if (!running) {
    	    Tcl_InitHashTable(&table, TCL_ONE_WORD_KEYS);
	    Ns_MutexSetName(&lock, "ns:sockcallbacks");
	    create = 1;
	    running = 1;
	} else if (firstQueuePtr == NULL) {
	    trigger = 1;
	}
        if (firstQueuePtr == NULL) {
            firstQueuePtr = cbPtr;
        } else {
            lastQueuePtr->nextPtr = cbPtr;
        }
        cbPtr->nextPtr = NULL;
        lastQueuePtr = cbPtr;
    	status = NS_OK;
    }
    Ns_MutexUnlock(&lock);
    if (trigger) {
	CallbackTrigger();
    } else if (create) {
    	if (ns_sockpair(trigPipe) != 0) {
	    Ns_Fatal("ns_sockpair() failed: %s", ns_sockstrerror(ns_sockerrno));
    	}
    	Ns_ThreadCreate(SockCallbackThread, NULL, 0, &sockThread);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * SockCallbackThread --
 *
 *	Run callbacks registered with Ns_SockCallback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on callbacks.
 *
 *----------------------------------------------------------------------
 */

static void
SockCallbackThread(void *ignored)
{
    char           c;
    int            when[3], events[3];
    int            n, i, isNew, stop;
    int		   max, nfds, pollto;
    Callback      *cbPtr, *nextPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    struct pollfd *pfds;
    time_t         now;

    Ns_ThreadSetName("-socks-");
    Ns_WaitForStartup();
    Ns_Log(Notice, "socks: starting");

    events[0] = POLLIN;
    events[1] = POLLOUT;
    events[2] = POLLPRI;
    when[0] = NS_SOCK_READ;
    when[1] = NS_SOCK_WRITE;
    when[2] = NS_SOCK_EXCEPTION | NS_SOCK_DONE;
    max = 100;
    pfds = ns_malloc(sizeof(struct pollfd) * max);
    pfds[0].fd = trigPipe[0];
    pfds[0].events = POLLIN;

    while (1) {

	/*
	 * Grab the list of any queue updates and the shutdown
	 * flag.
	 */

    	Ns_MutexLock(&lock);
	cbPtr = firstQueuePtr;
	firstQueuePtr = NULL;
        lastQueuePtr = NULL;
	stop = shutdownPending;
	Ns_MutexUnlock(&lock);

	/*
    	 * Move any queued callbacks to the active table.
	 */

        while (cbPtr != NULL) {
            nextPtr = cbPtr->nextPtr;
            if (cbPtr->when & NS_SOCK_CANCEL) {
                hPtr = Tcl_FindHashEntry(&table, (char *)(intptr_t) cbPtr->sock);
                if (hPtr != NULL) {
                    ns_free(Tcl_GetHashValue(hPtr));
                    Tcl_DeleteHashEntry(hPtr);
                }
                if (cbPtr->proc != NULL) {
                    (*cbPtr->proc)(cbPtr->sock, cbPtr->arg, NS_SOCK_CANCEL);
                }
                ns_free(cbPtr);
            } else {
                hPtr = Tcl_CreateHashEntry(&table, (char *)(intptr_t) cbPtr->sock, &isNew);
                if (!isNew) {
                    ns_free(Tcl_GetHashValue(hPtr));
                }
                Tcl_SetHashValue(hPtr, cbPtr);
            }
            cbPtr = nextPtr;
        }

	/*
	 * Verify and set the poll bits for all active callbacks.
	 */

	if (max <= table.numEntries) {
	    max  = table.numEntries + 100;
	    pfds = ns_realloc(pfds, (size_t)max);
	}

        /*
         * Wake up every 5 seconds to process expired sockets
         */

        pollto = 30000;
        now = time(0);
	nfds = 1;
	hPtr = Tcl_FirstHashEntry(&table, &search);
	while (hPtr != NULL) {
	    cbPtr = Tcl_GetHashValue(hPtr);
            if (cbPtr->timeout > 0 && cbPtr->expires > 0 && cbPtr->expires < now) {
                (*cbPtr->proc)(cbPtr->sock, cbPtr->arg, NS_SOCK_TIMEOUT);
                cbPtr->when = 0;
            }
	    if (!(cbPtr->when & NS_SOCK_ANY)) {
	    	Tcl_DeleteHashEntry(hPtr);
		ns_free(cbPtr);
	    } else {
		cbPtr->idx = nfds;
		pfds[nfds].fd = cbPtr->sock;
		pfds[nfds].events = pfds[nfds].revents = 0;
        	for (i = 0; i < 3; ++i) {
                    if (cbPtr->when & when[i]) {
			pfds[nfds].events |= events[i];
                    }
        	}
		++nfds;
                if (cbPtr->timeout > 0) {
                    if (cbPtr->timeout * 1000 < pollto)  {
                        pollto = cbPtr->timeout * 1000;
                    }

                    /*
                     * Set expiration time for this callback, every time
                     * event occures on this socket we reset expiration so
                     * expiration is processed since the last event
                     */

                    if (cbPtr->expires == 0) {
                        cbPtr->expires = now + cbPtr->timeout;
                    }
                }
	    }
	    hPtr = Tcl_NextHashEntry(&search);
        }

    	/*
	 * Select on the sockets and drain the trigger pipe if
	 * necessary.
	 */

	if (stop) {
	    break;
	}
	pfds[0].revents = 0;
        do {
            n = ns_poll(pfds, nfds, pollto);
        } while (n < 0  && errno == EINTR);
        if (n < 0) {
            Ns_Fatal("sockcallback: ns_poll() failed: %s",
                     ns_sockstrerror(ns_sockerrno));
        }
	if ((pfds[0].revents & POLLIN) && recv(trigPipe[0], &c, 1, 0) != 1) {
	    Ns_Fatal("trigger read() failed: %s", strerror(errno));
	}

    	/*
	 * Execute any ready callbacks.
	 */

    	hPtr = Tcl_FirstHashEntry(&table, &search);
	while (n > 0 && hPtr != NULL) {
	    cbPtr = Tcl_GetHashValue(hPtr);
            for (i = 0; i < 3; ++i) {
                if ((cbPtr->when & when[i]) &&
		    (pfds[cbPtr->idx].revents & events[i])) {
                    if (!((*cbPtr->proc)(cbPtr->sock, cbPtr->arg, when[i]))) {
			cbPtr->when = 0;
		    }
                    cbPtr->expires = 0;
                }
            }
	    hPtr = Tcl_NextHashEntry(&search);
        }
    }

    /*
     * Invoke any exit callabacks, cleanup the callback
     * system, and signal shutdown complete.
     */

    Ns_Log(Notice, "socks: shutdown pending");
    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	cbPtr = Tcl_GetHashValue(hPtr);
	if (cbPtr->when & NS_SOCK_EXIT) {
	    (void) ((*cbPtr->proc)(cbPtr->sock, cbPtr->arg, NS_SOCK_EXIT));
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	ns_free(Tcl_GetHashValue(hPtr));
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&table);

    Ns_Log(Notice, "socks: shutdown complete");
    Ns_MutexLock(&lock);
    running = 0;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
}


void
NsGetSockCallbacks(Tcl_DString *dsPtr)
{
    Tcl_HashSearch  search;

    Ns_MutexLock(&lock);
    if (running) {
        Tcl_HashEntry *hPtr = Tcl_FirstHashEntry(&table, &search);

        while (hPtr != NULL) {
	    Callback *cbPtr = Tcl_GetHashValue(hPtr);
	    char      buf[TCL_INTEGER_SPACE];

            Tcl_DStringStartSublist(dsPtr);
            snprintf(buf, sizeof(buf), "%d", (int) cbPtr->sock);
            Tcl_DStringAppendElement(dsPtr, buf);
            Tcl_DStringStartSublist(dsPtr);
            if (cbPtr->when & NS_SOCK_READ) {
                Tcl_DStringAppendElement(dsPtr, "read");
            }
            if (cbPtr->when & NS_SOCK_WRITE) {
                Tcl_DStringAppendElement(dsPtr, "write");
            }
            if (cbPtr->when & NS_SOCK_EXCEPTION) {
                Tcl_DStringAppendElement(dsPtr, "exception");
            }
            if (cbPtr->when & NS_SOCK_EXIT) {
                Tcl_DStringAppendElement(dsPtr, "exit");
            }
            Tcl_DStringEndSublist(dsPtr);
            Ns_GetProcInfo(dsPtr, (void *)cbPtr->proc, cbPtr->arg);
            snprintf(buf, sizeof(buf), "%d", cbPtr->timeout);
            Tcl_DStringAppendElement(dsPtr, buf);
            Tcl_DStringEndSublist(dsPtr);
            hPtr = Tcl_NextHashEntry(&search);
        }
    }
    Ns_MutexUnlock(&lock);
}

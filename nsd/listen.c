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
 * listen.c --
 *
 *	Listen on sockets and register callbacks for incoming 
 *	connections. 
 */

#include "nsd.h"

/*
 * This the context used by the socket callback.
 */

typedef struct ListenData {
    Ns_SockProc *proc;
    void        *arg;
} ListenData;

/*
 * Local functions defined in this file
 */

static Ns_SockProc  ListenCallback; 

/*
 * Static variables defined in this file
 */

static Tcl_HashTable portsTable;      /* Table of per-port data. */
static Ns_Mutex      lock;            /* Lock around portsTable. */


/*
 *----------------------------------------------------------------------
 *
 * NsInitListen --
 *
 *	Initialize listen callback API.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void
NsInitListen(void)
{
    Ns_MutexInit(&lock);
    Ns_MutexSetName(&lock, "ns:listencallbacks");
    Tcl_InitHashTable(&portsTable, TCL_ONE_WORD_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenCallback --
 *
 *	Listen on an address/port and register a callback to be run 
 *	when connections come in on it. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
Ns_SockListenCallback(const char *addr, int port, Ns_SockProc *proc, void *arg)
{
    Tcl_HashTable        *tablePtr = NULL;
    Tcl_HashEntry        *hPtr;
    NS_SOCKET             sock;
    int                   isNew;
    Ns_ReturnCode         status;
    struct NS_SOCKADDR_STORAGE sa;
    struct sockaddr      *saPtr = (struct sockaddr *)&sa;

    NS_NONNULL_ASSERT(proc != NULL);
    NS_NONNULL_ASSERT(arg != NULL);

    if (Ns_GetSockAddr(saPtr, addr, port) != NS_OK) {
        return NS_ERROR;
    }

    if (addr != NULL) {
        /*
	 * Make sure we can bind to the specified interface.
	 */
        Ns_SockaddrSetPort(saPtr, 0u);
        sock = Ns_SockBind(saPtr);
        if (sock == NS_INVALID_SOCKET) {
            return NS_ERROR;
        }
        ns_sockclose(sock);
    }
    status = NS_OK;

    Ns_MutexLock(&lock);

    /*
     * Update the global hash table that keeps track of which ports
     * we're listening on.
     */
  
    hPtr = Tcl_CreateHashEntry(&portsTable, INT2PTR(port), &isNew);
    if (isNew == 0) {
        tablePtr = Tcl_GetHashValue(hPtr);
    } else {
        
        sock = Ns_SockListen(NULL, port);
        if (sock == NS_INVALID_SOCKET) {
            Tcl_DeleteHashEntry(hPtr);
            status = NS_ERROR;
        } else {
	    status = Ns_SockSetNonBlocking(sock);
	}
	if (status == NS_OK) {
            tablePtr = ns_malloc(sizeof(Tcl_HashTable));
            Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
            Tcl_SetHashValue(hPtr, tablePtr);
            status = Ns_SockCallback(sock, ListenCallback, tablePtr,
				     (unsigned int)NS_SOCK_READ | (unsigned int)NS_SOCK_EXIT);
        }
    }
    if (status == NS_OK) {
        char ipString[NS_IPADDR_SIZE] = {'\0'};

	assert(tablePtr != NULL);
        
        hPtr = Tcl_CreateHashEntry(tablePtr,
                                   ns_inet_ntop(saPtr, ipString, NS_IPADDR_SIZE),
                                   &isNew);

        if (isNew == 0) {
            Ns_Log(Error, "listen callback: there is already a listen callback registered");
            status = NS_ERROR;
        } else {
            ListenData *ldPtr;

            ldPtr = ns_malloc(sizeof(ListenData));
            ldPtr->proc = proc;
            ldPtr->arg = arg;
            Tcl_SetHashValue(hPtr, ldPtr);
        }
    }
    Ns_MutexUnlock(&lock);
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockPortBound --
 *
 *	Determine if we're already listening on a given port on any 
 *	address. 
 *
 * Results:
 *	Boolean
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

bool
Ns_SockPortBound(int port)
{
    Tcl_HashEntry  *hPtr;

    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&portsTable, INT2PTR(port));
    Ns_MutexUnlock(&lock);
    return (hPtr != NULL ? NS_TRUE : NS_FALSE);
}


/*
 *----------------------------------------------------------------------
 *
 * ListenCallback --
 *
 *	This is a wrapper callback that runs the user's callback iff 
 *	a valid socket exists. 
 *
 * Results:
 *	NS_TRUE or NS_FALSE 
 *
 * Side effects:
 *	May close the socket if no user context can be found. 
 *
 *----------------------------------------------------------------------
 */

static bool
ListenCallback(NS_SOCKET sock, void *arg, unsigned int why)
{
    struct NS_SOCKADDR_STORAGE sa;
    socklen_t           len;
    Tcl_HashTable      *tablePtr;
    NS_SOCKET           newSock;
    bool                success;

    tablePtr = arg;

    if (why == (unsigned int)NS_SOCK_EXIT) {
        (void) ns_sockclose(sock);
        return NS_FALSE;
    }

    newSock = Ns_SockAccept(sock, NULL, NULL);
    
    if (likely(newSock != NS_INVALID_SOCKET)) {
        Tcl_HashEntry *hPtr;
        ListenData    *ldPtr;
        int            retVal;
        char           ipString[NS_IPADDR_SIZE] = {'\0'};

        (void) Ns_SockSetBlocking(newSock);
        len = (socklen_t)sizeof(sa);
        retVal = getsockname(newSock, (struct sockaddr *) &sa, &len);
        if (retVal == -1) {
            Ns_Log(Warning, "listencallback: can't obtain socket info %s", ns_sockstrerror(ns_sockerrno));
            (void) ns_sockclose(sock);
            return NS_FALSE;
        }
        ldPtr = NULL;
        
        (void)ns_inet_ntop((struct sockaddr *)&sa, ipString, NS_IPADDR_SIZE);
        
        Ns_MutexLock(&lock);
        hPtr = Tcl_FindHashEntry(tablePtr, ipString);
        if (hPtr == NULL) {
            hPtr = Tcl_FindHashEntry(tablePtr, NS_IP_UNSPECIFIED);
        }
        if (hPtr != NULL) {
            ldPtr = Tcl_GetHashValue(hPtr);
        }
        Ns_MutexUnlock(&lock);

        if (ldPtr == NULL) {
            /*
             * There was no hash entry for the listen callback
             */
            int result = ns_sockclose(newSock);
            success = (result == 0) ? NS_TRUE : NS_FALSE;
        } else {
            /*
             * The hash entry was found, so fire the callback (e.g. exec tcl
             * code).
             */
            success = (*ldPtr->proc) (newSock, ldPtr->arg, why);
        }
    } else {
        success = NS_FALSE;
    }
    return success;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

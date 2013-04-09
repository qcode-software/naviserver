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
 * mutex.c --
 *
 *	Mutex locks with metering.
 */

#include "thread.h"

/*
 * The following structure defines a mutex with
 * string name and lock and busy counters.
 */

typedef struct Mutex {
    void	    *lock;
    struct Mutex    *nextPtr;
    unsigned int     id;
    unsigned long    nlock;
    unsigned long    nbusy;
    Ns_Time          total_waiting_time;
    Ns_Time          max_waiting_time;
    char	     name[NS_THREAD_NAMESIZE+1];
} Mutex;

#define GETMUTEX(mutex) (*(mutex)?((Mutex *)*(mutex)):GetMutex((mutex)))
static Mutex *GetMutex(Ns_Mutex *mutex);
static Mutex *firstMutexPtr;


/*
 *----------------------------------------------------------------------
 
 * Ns_MutexInit --
 *
 *	Mutex initialization, often called the first time a mutex
 *	is locked.
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
Ns_MutexInit(Ns_Mutex *mutex)
{
    Mutex *mutexPtr;
    static unsigned int nextid;

    mutexPtr = ns_calloc(1, sizeof(Mutex));
    mutexPtr->lock = NsLockAlloc();
    Ns_MasterLock();
    mutexPtr->nextPtr = firstMutexPtr;
    firstMutexPtr = mutexPtr;
    mutexPtr->id = nextid++;
    snprintf(mutexPtr->name, sizeof(mutexPtr->name), "mu%u", mutexPtr->id);
    Ns_MasterUnlock();
    *mutex = (Ns_Mutex) mutexPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexSetName, Ns_MutexSetName2 --
 *
 *	Update the string name of a mutex.
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
Ns_MutexSetName(Ns_Mutex *mutex, CONST char *name)
{
    Ns_MutexSetName2(mutex, name, NULL);
}

void
Ns_MutexSetName2(Ns_Mutex *mutex, CONST char *prefix, CONST char *name)
{
    Mutex *mutexPtr = GETMUTEX(mutex);
    size_t plen, nlen;
    char *p;

    plen = strlen(prefix);
    if (plen > NS_THREAD_NAMESIZE) {
	plen = NS_THREAD_NAMESIZE;
	nlen = 0;
    } else {
    	nlen = name ? strlen(name) : 0;
	if ((nlen + plen + 1) > NS_THREAD_NAMESIZE) {
	    nlen = NS_THREAD_NAMESIZE - plen - 1;
	}
    }
    Ns_MasterLock();
    p = strncpy(mutexPtr->name, prefix, (size_t)plen) + plen;
    if (nlen > 0) {
	*p++ = ':';
	p = strncpy(p, name, (size_t)nlen) + nlen;
    }
    *p = '\0';
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexDestroy --
 *
 *	Mutex destroy.  Note this routine is not used very often
 *	as mutexes normally exists in memory until the process exits.
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
Ns_MutexDestroy(Ns_Mutex *mutex)
{
    Mutex       **mutexPtrPtr;
    Mutex	 *mutexPtr = (Mutex *) *mutex;

    if (mutexPtr != NULL) {
	NsLockFree(mutexPtr->lock);
    	Ns_MasterLock();
    	mutexPtrPtr = &firstMutexPtr;
    	while ((*mutexPtrPtr) != mutexPtr) {
	    mutexPtrPtr = &(*mutexPtrPtr)->nextPtr;
    	}
    	*mutexPtrPtr = mutexPtr->nextPtr;
    	Ns_MasterUnlock();
    	ns_free(mutexPtr);
	*mutex = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexLock --
 *
 *	Lock a mutex, tracking the number of locks and the number of
 *	which were not aquired immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may be suspended if the lock is held.
 *
 *----------------------------------------------------------------------
 */

void
Ns_MutexLock(Ns_Mutex *mutex)
{
    Mutex *mutexPtr = GETMUTEX(mutex);
    Ns_Time start, end, diff;

    Ns_GetTime(&start);
    if (!NsLockTry(mutexPtr->lock)) {
	NsLockSet(mutexPtr->lock);
	++mutexPtr->nbusy;
        /*
         * Measure total and max waiting time for busy mutex locks.
         */
        Ns_GetTime(&end);
        Ns_DiffTime(&end, &start, &diff);
        Ns_IncrTime(&mutexPtr->total_waiting_time, diff.sec, diff.usec);
        /* 
         * Keep max waiting time since server start. It might be a
	 * good idea to either provide a call to reset the max-time,
	 * or to report wait times above a certain threshold (as an
	 * extra value in the statistics, or in the log file).
         */
        if (Ns_DiffTime(&mutexPtr->max_waiting_time, &diff, NULL) < 0) {
            mutexPtr->max_waiting_time = diff;
            /*fprintf(stderr, "Mutex %s max time %" PRIu64 ".%.6ld\n", 
	      mutexPtr->name, (int64_t)diff.sec, diff.usec);*/
        }
    }
    ++mutexPtr->nlock;

}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexTryLock --
 *
 *	Attempt to lock a mutex.
 *
 * Results:
 *	NS_OK if locked, NS_TIMEOUT if lock already held.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_MutexTryLock(Ns_Mutex *mutex)
{
    Mutex *mutexPtr = GETMUTEX(mutex);

    if (!NsLockTry(mutexPtr->lock)) {
    	return NS_TIMEOUT;
    }
    ++mutexPtr->nlock;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexUnlock --
 *
 *	Unlock a mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Other waiting thread, if any, is resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_MutexUnlock(Ns_Mutex *mutex)
{
    Mutex *mutexPtr = (Mutex *) *mutex;

    NsLockUnset(mutexPtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexList --
 *
 *	Append info on each lock.
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
Ns_MutexList(Tcl_DString *dsPtr)
{
    Mutex *mutexPtr;
    char buf[200];

    Ns_MasterLock();
    mutexPtr = firstMutexPtr;
    while (mutexPtr != NULL) {
        Tcl_DStringStartSublist(dsPtr);
        Tcl_DStringAppendElement(dsPtr, mutexPtr->name);
        Tcl_DStringAppendElement(dsPtr, ""); /* unused? */
        snprintf(buf, sizeof(buf), " %u %lu %lu %" PRIu64 ".%.6ld %" PRIu64 ".%.6ld", 
                 mutexPtr->id, mutexPtr->nlock, mutexPtr->nbusy, 
                 (int64_t)mutexPtr->total_waiting_time.sec, mutexPtr->total_waiting_time.usec,
                 (int64_t)mutexPtr->max_waiting_time.sec, mutexPtr->max_waiting_time.usec);
        Tcl_DStringAppend(dsPtr, buf, -1);
        Tcl_DStringEndSublist(dsPtr);
        mutexPtr = mutexPtr->nextPtr;
    }
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexInitNext --
 *
 *	Initialize and name the next internal mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Given counter is updated.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexInitNext(Ns_Mutex *mutex, char *prefix, unsigned int *nextPtr)
{
    unsigned int id;
    char buf[NS_THREAD_NAMESIZE];

    Ns_MasterLock();
    id = *nextPtr;
    *nextPtr = id + 1;
    Ns_MasterUnlock();
    snprintf(buf, sizeof(buf), "ns:%s:%u", prefix, id);
    Ns_MutexInit(mutex);
    Ns_MutexSetName(mutex, buf);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetLock --
 *
 *	Return the private lock pointer for a Ns_Mutex.
 *
 * Results:
 *	Pointer to lock.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsGetLock(Ns_Mutex *mutex)
{
    Mutex *mutexPtr = GETMUTEX(mutex);

    return mutexPtr->lock;
}


/*
 *----------------------------------------------------------------------
 *
 * GetMutex --
 *
 *	Cast an Ns_Mutex to a Mutex, initializing if needed.
 *
 * Results:
 *	Pointer to Mutex.
 *
 * Side effects:
 *	Mutex is initialized the first time.
 *
 *----------------------------------------------------------------------
 */

static Mutex *
GetMutex(Ns_Mutex *mutex)
{
    Ns_MasterLock();
    if (*mutex == NULL) {
	Ns_MutexInit(mutex);
    }
    Ns_MasterUnlock();
    return (Mutex *) *mutex;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexGetName --
 *
 *	Obtain the name of a mutex.
 *
 * Results:
 *	String name of the mutex.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
char *
Ns_MutexGetName(Ns_Mutex *mutex)
{
    Mutex *mutexPtr = GETMUTEX(mutex);

    return mutexPtr->name;
}

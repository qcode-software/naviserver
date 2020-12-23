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

#ifdef _WIN32

/*
 * win32.c --
 *
 *  Interface routines for nsthreads using WIN32 functions.
 *
 * TODO: since there there exists also a pthreads library for windows, this
 * file should be split up into parts still required here, and other parts
 * that can be replaced by the pthreads library.
 *
 */

#include "thread.h"
#include <io.h>

/*
 * The following structure maintains the Win32-specific state
 * of a thread and mutex and condition waits pointers.
 */

typedef struct WinThread {
    struct WinThread *nextPtr;
    struct WinThread *wakeupPtr;
    HANDLE self;
    HANDLE event;
    int    condwait;
    void  *slots[NS_THREAD_MAXTLS];
} WinThread;

typedef struct ThreadArg {
    HANDLE  self;
    void   *arg;
} ThreadArg;

/*
 * The following structure defines a condition variable
 * as a set of a critical section and a thread wait queue.
 */

typedef struct {
    CRITICAL_SECTION critsec;
    WinThread *waitPtr;
} Cond;

/*
 * The following structure is used to maintain state during
 * an open directory search.
 */

typedef struct Search {
    intptr_t           handle;
    struct _finddata_t fdata;
    struct dirent      ent;
} Search;


/*
 * Static functions defined in this file.
 */

static void   Wakeup(WinThread *wPtr, char *func);
static void   Queue(WinThread **waitPtrPtr, WinThread *wPtr);
static Cond  *GetCond(Ns_Cond *cond);

static unsigned __stdcall ThreadMain(void *arg);

/*
 * The following single Tls key is used to store the nsthread
 * structure.  It's initialized in DllMain.
 */

static DWORD tlskey;

#define MAX_THREAD_EXIT_RESULTS 256

static struct threadExitResults {
    unsigned size;
    void *data[MAX_THREAD_EXIT_RESULTS];
} threadExitResults;

#define GETWINTHREAD()  TlsGetValue(tlskey)


/*
 *----------------------------------------------------------------------
 *
 * Nsthreads_LibInit --
 *
 *      Pthread library initialization routine.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates pthread key.
 *
 *----------------------------------------------------------------------
 */


void
Nsthreads_LibInit(void)
{
    static bool initialized = NS_FALSE;

    if (!initialized) {
        initialized = NS_TRUE;
        tlskey = TlsAlloc();
        if (TLS_OUT_OF_INDEXES == tlskey) {
            return;
        }
        NsInitThreads();
        threadExitResults.size = 1;
    }
}



/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *      Thread library DLL main, managing each thread's WinThread
 *      structure and the master critical section lock.
 *
 * Results:
 *      TRUE.
 *
 * Side effects:
 *      On error will abort process.
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD why, LPVOID lpReserved)
{
    WinThread *wPtr;

    switch (why) {
    case DLL_PROCESS_ATTACH:
        Nsthreads_LibInit();

        NS_FALL_THROUGH; /* fall through */
    case DLL_THREAD_ATTACH:
        wPtr = ns_calloc(1u, sizeof(WinThread));
        wPtr->event = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (wPtr->event == NULL) {
            NsThreadFatal("DllMain", "CreateEvent", GetLastError());
        }
        if (!TlsSetValue(tlskey, wPtr)) {
            NsThreadFatal("DllMain", "TlsSetValue", GetLastError());
        }
        break;

    case DLL_THREAD_DETACH:
        /*
         * Note this code does not execute for the final thread on
         * exit because the TLS callbacks may invoke code from an
         * unloaded DLL, e.g., Tcl.
         */

        wPtr = TlsGetValue(tlskey);
        if (wPtr != NULL) {
            NsCleanupTls(wPtr->slots);
            if (!CloseHandle(wPtr->event)) {
                NsThreadFatal("DllMain", "CloseHandle", GetLastError());
            }
            if (!TlsSetValue(tlskey, NULL)) {
                NsThreadFatal("DllMain", "TlsSetValue", GetLastError());
            }
            ns_free(wPtr);
        }
        break;

    case DLL_PROCESS_DETACH:
        if (!TlsFree(tlskey)) {
            NsThreadFatal("DllMain", "TlsFree", GetLastError());
        }
        break;
    }

    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetTls --
 *
 *      Return the TLS slots for this thread.
 *
 * Results:
 *      Pointer to slots array.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void **
NsGetTls(void)
{
    WinThread *wPtr = GETWINTHREAD();

    return wPtr->slots;
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadLibName --
 *
 *      Return the string name of the thread library.
 *
 * Results:
 *      Pointer to static string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
NsThreadLibName(void)
{
    return "win32";
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockAlloc --
 *
 *      Allocate and initialize a mutex lock.
 *
 * Results:
 *      Pointer to the initialized mutex
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void *
NsLockAlloc(void)
{
    CRITICAL_SECTION *csPtr;

    csPtr = (CRITICAL_SECTION *)ns_calloc(1u, sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(csPtr);

    return (void *)csPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockFree --
 *
 *      Free a mutex lock.
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
NsLockFree(void *lock)
{
    CRITICAL_SECTION *csPtr = (CRITICAL_SECTION *)lock;

    DeleteCriticalSection(csPtr);
    ns_free(csPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockSet --
 *
 *      Set a mutex lock.
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
NsLockSet(void *lock)
{
    EnterCriticalSection((CRITICAL_SECTION *)lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockTry --
 *
 *      Try to set a mutex lock once.
 *
 * Results:
 *      1 if lock set, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(void *lock)
{
    return TryEnterCriticalSection((CRITICAL_SECTION *)lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockUnset --
 *
 *      Unset a mutex lock.
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
NsLockUnset(void *lock)
{
    LeaveCriticalSection((CRITICAL_SECTION *)lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *      Initialize a condition variable.  Note that this function
 *      is rarely called directly as condition variables are now
 *      self initialized when first accessed.
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
Ns_CondInit(Ns_Cond *cond)
{
    Cond *condPtr;

    condPtr = ns_calloc(1u, sizeof(Cond));
    InitializeCriticalSection(&condPtr->critsec);
    *cond = (Ns_Cond)condPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *      Destroy a previously initialized condition variable.
 *      Note this function is almost never called as condition
 *      variables normally exist until the process exits.
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
Ns_CondDestroy(Ns_Cond *cond)
{
    Cond *condPtr = GetCond(cond);

    if (condPtr != NULL) {
        DeleteCriticalSection(&condPtr->critsec);
        ns_free(condPtr);
        *cond = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *      Signal a condition variable, releasing a single thread if one
 *      is waiting.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A single waiting thread may be resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *cond)
{
    Cond      *condPtr = GetCond(cond);
    WinThread *wPtr;

    EnterCriticalSection(&condPtr->critsec);
    wPtr = condPtr->waitPtr;
    if (wPtr != NULL) {
        condPtr->waitPtr = wPtr->nextPtr;
        wPtr->nextPtr = NULL;
        wPtr->condwait = 0;

        /*
         * The Wakeup() must be done before the unlock
         * as the other thread may have been in a timed
         * wait which just timed out.
         */

        Wakeup(wPtr, "Ns_CondSignal");
    }
    LeaveCriticalSection(&condPtr->critsec);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *      Broadcast a condition, resuming all waiting threads, if any.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      First thread, if any, is awoken.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *cond)
{
    Cond      *condPtr = GetCond(cond);
    WinThread *wPtr;

    EnterCriticalSection(&condPtr->critsec);

    /*
     * Set each thread to wake up the next thread on the
     * waiting list.  This results in a rolling wakeup
     * which should reduce lock contention as the threads
     * are awoken.
     */

    wPtr = condPtr->waitPtr;
    while (wPtr != NULL) {
        wPtr->wakeupPtr = wPtr->nextPtr;
        wPtr->nextPtr = NULL;
        wPtr->condwait = 0;
        wPtr = wPtr->wakeupPtr;
    }

    /*
     * Wake up the first thread to start the rolling
     * wakeup.
     */

    wPtr = condPtr->waitPtr;
    if (wPtr != NULL) {
        condPtr->waitPtr = NULL;
        /* NB: See Wakeup() comment in Ns_CondSignal(). */
        Wakeup(wPtr, "Ns_CondBroadcast");
    }

    LeaveCriticalSection(&condPtr->critsec);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *      Wait indefinitely for a condition to be signaled.
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
Ns_CondWait(Ns_Cond *cond, Ns_Mutex *mutex)
{
    (void) Ns_CondTimedWait(cond, mutex, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *      Wait for a condition to be signaled up to a given absolute
 *      timeout. This code is very tricky to avoid the race condition
 *      between locking and unlocking the coordinating mutex and catching
 *      a wakeup signal. Be sure you understand how condition variables
 *      work before screwing around with this code.
 *
 * Results:
 *      NS_OK on signal being received within the timeout period,
 *      otherwise NS_TIMEOUT.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
Ns_CondTimedWait(Ns_Cond *cond, Ns_Mutex *mutex, const Ns_Time *timePtr)
{
    Ns_ReturnCode status;
    Cond         *condPtr;
    WinThread    *wPtr;
    Ns_Time       now, wait;
    time_t        msec;
    DWORD         w;

    /*
     * Convert to relative wait time and verify.
     */

    if (timePtr == NULL) {
        msec = INFINITE;
    } else {
        Ns_GetTime(&now);
        Ns_DiffTime(timePtr, &now, &wait);
        msec = Ns_TimeToMilliseconds(&wait);
        if (msec < 0) {
            return NS_TIMEOUT;
        }
    }

    /*
     * Lock the condition and add this thread to the end of
     * the wait list.
     */

    condPtr = GetCond(cond);
    wPtr = GETWINTHREAD();

    EnterCriticalSection(&condPtr->critsec);
    wPtr->condwait = 1;
    Queue(&condPtr->waitPtr, wPtr);
    LeaveCriticalSection(&condPtr->critsec);

    /*
     * Release the outer mutex and wait for the signal to arrive
     * or timeout.
     */

    Ns_MutexUnlock(mutex);
    w = WaitForSingleObject(wPtr->event, (DWORD)msec);
    if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) {
        NsThreadFatal("Ns_CondTimedWait", "WaitForSingleObject", GetLastError());
    }

    /*
     * Lock the condition and check if wakeup was signaled. Note
     * that the signal may have arrived as the event was timing
     * out so the return of WaitForSingleObject can't be relied on.
     * If there was no wakeup, remove this thread from the list.
     */

    EnterCriticalSection(&condPtr->critsec);
    if (wPtr->condwait == 0) {
        status = NS_OK;
    } else {
        WinThread  **waitPtrPtr;

        status = NS_TIMEOUT;
        waitPtrPtr = &condPtr->waitPtr;
        while (*waitPtrPtr != wPtr) {
            waitPtrPtr = &(*waitPtrPtr)->nextPtr;
        }
        *waitPtrPtr = wPtr->nextPtr;
        wPtr->nextPtr = NULL;
        wPtr->condwait = 0;
    }

    /*
     * Wakeup the next thread in a rolling broadcast if necessary.
     * Note, as with Ns_CondSignal, the wakeup must be sent while
     * the lock is held.
     */

    if (wPtr->wakeupPtr != NULL) {
        Wakeup(wPtr->wakeupPtr, "Ns_CondTimedWait");
        wPtr->wakeupPtr = NULL;
    }
    LeaveCriticalSection(&condPtr->critsec);

    /*
     * Re-acquire the outer lock and return.
     */

    Ns_MutexLock(mutex);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsCreateThread --
 *
 *      WinThread specific thread create function called by
 *      Ns_ThreadCreate. Note the use of _beginthreadex. CreateThread
 *      does not initialize the C runtime library fully and could
 *      lead to memory leaks on thread exit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on thread startup routine.
 *
 *----------------------------------------------------------------------
 */

void
NsCreateThread(void *arg, ssize_t stacksize, Ns_Thread *threadPtr)
{
    ThreadArg *argPtr;
    unsigned   tid, flags;
    uintptr_t  hdl;

    flags = ((threadPtr != NULL) ? CREATE_SUSPENDED : 0u);
    argPtr = ns_malloc(sizeof(ThreadArg));
    argPtr->arg = arg;
    argPtr->self = NULL;

    hdl = _beginthreadex(NULL, (unsigned int)stacksize, ThreadMain, argPtr, flags, &tid);
    if (hdl == 0u) {
        NsThreadFatal("NsCreateThread", "_beginthreadex", errno);
    }
    if (threadPtr == NULL) {
        /*
         * Provide a value for the closed handle, since this might be returned
         * by "ns_thread handle".
         */
        argPtr->self = (HANDLE) -1;
        CloseHandle((HANDLE) hdl);
    } else {
        argPtr->self = (HANDLE) hdl;
        ResumeThread(argPtr->self);
        *threadPtr = (Ns_Thread) hdl;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *      Terminate a thread via _endthreadex(). The result of the thread is
 *      passed via (the 32-bit) argument of _endthreadex(), which will be
 *      accessible after the join. We use the argument as a pointer to the
 *      threadExitResults table.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Thread will clean itself up via the DllMain thread detach code.
 *      Potentially updating the thread results table.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadExit(void *arg)
{
    unsigned index;

    if (arg == NULL) {
        /*
         * Use always index "0" for NULL result.
         */
        index = 0u;
    } else {
        Ns_MasterLock();
        index = threadExitResults.size;
        if (index < MAX_THREAD_EXIT_RESULTS-1) {
            threadExitResults.data[index] = arg;
            threadExitResults.size++;
        } else {
            int ii;
            for (ii = 1; ii < MAX_THREAD_EXIT_RESULTS; ii++) {
                if (threadExitResults.data[ii] == NULL) {
                    index = ii;
                    break;
                }
            }

            if (ii == MAX_THREAD_EXIT_RESULTS) {
                index = 0;
                fprintf(stderr, "NsThreadExit: thread %" PRIxPTR
                       " %s - no free space in return table -"
                       "returning NULL instead of %p.",
                       Ns_ThreadId(), Ns_ThreadGetName(), (void*)arg);

            }
        }
        Ns_MasterUnlock();
        /*fprintf(stderr, "%" PRIxPTR " %s === NsThreadExit receives %p sets index %u\n",
                Ns_ThreadId(), Ns_ThreadGetName(), (void*)arg, index); */
    }
    /*
     * Return the index in the threadExitResults table.
     */
    _endthreadex(index);
}

/*
 *----------------------------------------------------------------------
 *
 * NsThreadResult --
 *
 *      Access the result of an exiting thread available. The only reason for
 *      having this function is that the Windows function _endthreadex() just
 *      accepts an "unsigned" value, whele we can access via the pthread
 *      interface a pointer. This is cruical, since with 64-bit windows,
 *      "unsigned" is just a 32-bit value. Therefore, In the windows version of
 *      NaviServer we keep a table of results and pass just he index of this
 *      table via _endthreadex().
 *
 * Results:
 *      Pointer value (can be NULL).
 *
 * Side effects:
 *      Potentially updating the thread results table.
 *
 *----------------------------------------------------------------------
 */
void *
NsThreadResult(void *arg)
{
    unsigned  index = PTR2UINT(arg);
    void     *result;

    if (index == 0) {
        /*
         * Index "0" is used always for NULL result, no obtain pointer from
         * the results table.
         */
        result = NULL;
    } else {
        Ns_MasterLock();
        /*fprintf(stderr, "%" PRIxPTR " %s === NsThreadResult receives index %u\n",
          Ns_ThreadId(), Ns_ThreadGetName(), index);*/
        result = threadExitResults.data[index];
        threadExitResults.data[index] = NULL;
        if (threadExitResults.size == index+1) {
            unsigned  i;

            threadExitResults.size --;
            /*
             * Try to compact further all NULL entries (below top entry).
             */
            for (i = index; i>1; i--) {
                if (threadExitResults.data[i] == NULL) {
                    threadExitResults.size --;
                }
            }
            /*fprintf(stderr, "%" PRIxPTR " %s === NsThreadResult last arg %p"
                    " final size %u\n",
                    Ns_ThreadId(), Ns_ThreadGetName(), (void*)result,
                    threadExitResults.size);*/
        } else {
            /*
             * When the result is not the top value, we can't reduce the
             * threadExitResults.size. We have to rely on the compacting
             * above for shrinking the array.
             */
            fprintf(stderr, "%" PRIxPTR " %s ===### NsThreadResult inner arg %p size %u index %lu ###\n",
                    Ns_ThreadId(), Ns_ThreadGetName(),
                    (void*)result, (unsigned)threadExitResults.size , index);
        }
        Ns_MasterUnlock();
    }
    /*fprintf(stderr, "%" PRIxPTR " %s === NsThreadResult returns index %u arg %p\n",
      Ns_ThreadId(), Ns_ThreadGetName(), index, (void*)result);*/
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadJoin --
 *
 *      Wait for exit of a non-detached thread.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Requested thread is destroyed after join.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadJoin(Ns_Thread *thread, void **argPtr)
{
    HANDLE hdl = (HANDLE) *thread;
    DWORD exitcode;

    if (WaitForSingleObject(hdl, INFINITE) != WAIT_OBJECT_0) {
        NsThreadFatal("Ns_ThreadJoin", "WaitForSingleObject", GetLastError());
    }
    if (!GetExitCodeThread(hdl, &exitcode)) {
        NsThreadFatal("Ns_ThreadJoin", "GetExitCodeThread", GetLastError());
    }
    if (!CloseHandle(hdl)) {
        NsThreadFatal("Ns_ThreadJoin", "CloseHandle", GetLastError());
    }
    if (argPtr != NULL) {
        *argPtr = UINT2PTR(exitcode);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *      Yield the cpu to another thread.
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
Ns_ThreadYield(void)
{
    Sleep(0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadId --
 *
 *      Return the numeric thread id.
 *
 * Results:
 *      Integer thread id.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

uintptr_t
Ns_ThreadId(void)
{
    return (uintptr_t) GetCurrentThreadId();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadSelf --
 *
 *      Return thread handle suitable for Ns_ThreadJoin.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Value at threadPtr is updated with thread's handle.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadSelf(Ns_Thread *threadPtr)
{
    WinThread *wPtr = GETWINTHREAD();

    *threadPtr = (Ns_Thread) wPtr->self;
}


/*
 *----------------------------------------------------------------------
 *
 * ThreadMain --
 *
 *      Win32 thread startup which simply calls NsThreadMain.
 *
 * Results:
 *      Does not return.
 *
 * Side effects:
 *      NsThreadMain will call Ns_ThreadExit.
 *
 *----------------------------------------------------------------------
 */

static unsigned __stdcall
ThreadMain(void *arg)
{
    WinThread *wPtr = GETWINTHREAD();
    ThreadArg *argPtr = arg;

    wPtr->self = argPtr->self;
    arg = argPtr->arg;
    ns_free(argPtr);
    NsThreadMain(arg);

    /* NOT REACHED */
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Queue --
 *
 *      Add a thread on a condition wait queue.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Thread wakeup event is reset in case it's holding
 *      a lingering wakeup.
 *
 *----------------------------------------------------------------------
 */

static void
Queue(WinThread **waitPtrPtr, WinThread *wPtr)
{
    while (*waitPtrPtr != NULL) {
        waitPtrPtr = &(*waitPtrPtr)->nextPtr;
    }
    *waitPtrPtr = wPtr;
    wPtr->nextPtr = wPtr->wakeupPtr = NULL;
    if (!ResetEvent(wPtr->event)) {
        NsThreadFatal("Queue", "ResetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Wakeup --
 *
 *      Wakeup a thread waiting on a condition wait queue.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Thread wakeup event is set.
 *
 *----------------------------------------------------------------------
 */

static void
Wakeup(WinThread *wPtr, char *func)
{
    if (!SetEvent(wPtr->event)) {
        NsThreadFatal(func, "SetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetCond --
 *
 *      Return the Cond struct for given Ns_Cond, initializing
 *      if necessary.
 *
 * Results:
 *      Pointer to Cond.
 *
 * Side effects:
 *      Ns_Cond may be updated.
 *
 *----------------------------------------------------------------------
 */

static Cond *
GetCond(Ns_Cond *cond)
{
    if (*cond == NULL) {
        Ns_MasterLock();
        if (*cond == NULL) {
            Ns_CondInit(cond);
        }
        Ns_MasterUnlock();
    }
    return (Cond *) *cond;
}


/*
 *----------------------------------------------------------------------
 *
 * opendir --
 *
 *      Start a directory search.
 *
 * Results:
 *      Pointer to DIR.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

DIR *
opendir(char *pathname)
{
    Search *sPtr;
    char pattern[PATH_MAX];

    if (strlen(pathname) > PATH_MAX - 3) {
        errno = EINVAL;
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "%s/*", pathname);
    sPtr = ns_malloc(sizeof(Search));
    sPtr->handle = _findfirst(pattern, &sPtr->fdata);
    if (sPtr->handle == -1) {
        ns_free(sPtr);
        return NULL;
    }
    sPtr->ent.d_name = NULL;

    return (DIR *) sPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * closedir --
 *
 *      Closes and active directory search.
 *
 * Results:
 *      0.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
closedir(DIR *dp)
{
    Search *sPtr = (Search *) dp;

    _findclose(sPtr->handle);
    ns_free(sPtr);

    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * readdir --
 *
 *      Returns the next file in an active directory search.
 *
 * Results:
 *      Pointer to thread-local struct dirent.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

struct dirent  *
readdir(DIR * dp)
{
    Search *sPtr = (Search *) dp;

    if (sPtr->ent.d_name != NULL
        && _findnext(sPtr->handle, &sPtr->fdata) != 0) {
        return NULL;
    }
    sPtr->ent.d_name = sPtr->fdata.name;

    return &sPtr->ent;
}

/*
 *----------------------------------------------------------------------
 *
 * link, symlink --
 *
 *      Stubs for missing Unix routines. This is done simply to avoid
 *      more ifdef's in the code.
 *
 * Results:
 *      -1.
 *
 * Side effects:
 *      Sets errno to EINVAL.
 *
 *----------------------------------------------------------------------
 */

int
link(char *UNUSED(from), char *UNUSED(to))
{
    errno = EINVAL;
    return -1;
}

int
symlink(const char *UNUSED(from), const char *UNUSED(to))
{
    errno = EINVAL;
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * kill --
 *
 *      Sends signal to Win process.
 *
 * Results:
 *      Pointer to thread-local struct dirent.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

#ifndef SIGKILL
#define SIGKILL 9
#endif

int
kill(pid_t pid, int sig)
{
    HANDLE handle;
    BOOL rv;

    switch (sig) {
    case 0:
        handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE,
                             FALSE, pid);
        if (handle == NULL) {
            goto bad;
        }
        CloseHandle(handle);
        break;
    case SIGTERM:
    case SIGABRT:
    case SIGKILL:
        handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE,
                             FALSE, pid);
        if (handle != NULL) {
            rv = TerminateProcess(handle, 0);
            CloseHandle(handle);
            if (rv != 0) {
                break;
            }
        }
    default:
    bad:
        errno = EINVAL;
        return -1;
    }

    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * truncate --
 *
 *      Implement Unix truncate.
 *
 * Results:
 *      0 if ok or -1 on error.
 *
 * Side effects:
 *      File is opened, truncated, and closed.
 *
 *----------------------------------------------------------------------
 */

int
truncate(const char *path, off_t length)
{
    int fd;

    fd = _open(path, O_WRONLY|O_BINARY);
    if (fd < 0) {
        return -1;
    }
    length = _chsize(fd, length);
    _close(fd);
    if (length != 0) {
        return -1;
    }

    return 0;
}
#else
/*
 *avoid empty translation unit
 */
   typedef void empty;
#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

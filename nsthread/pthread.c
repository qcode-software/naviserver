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

#ifndef _WIN32

/*
 * pthread.c --
 *
 *	Interface routines for nsthreads using pthreads.
 *
 */

#include "thread.h"
#include <pthread.h>

/*
 * Local functions defined in this file.
 */

static pthread_cond_t *GetCond(Ns_Cond *cond)   NS_GNUC_NONNULL(1) NS_GNUC_RETURNS_NONNULL;
static void CleanupTls(void *arg)               NS_GNUC_NONNULL(1);
static void *ThreadMain(void *arg);

/*
 * Solaris has weird way to declare this one so
 * we just make a shortcut because this is what
 * the (solaris) definition really does.
 */

#if defined(__sun__)
#define PTHREAD_STACK_MIN  ((size_t)sysconf(_SC_THREAD_STACK_MIN))
#endif

/*
 * The following single Tls key is used to store the nsthread
 * Tls slots.
 */

static pthread_key_t	key;


/*
 *----------------------------------------------------------------------
 *
 * Nsthreads_LibInit --
 *
 *      Pthread library initialisation routine.
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
    static int once = 0;

    if (once == 0) {
        int err;
        once = 1;
        err = pthread_key_create(&key, CleanupTls);
        if (err != 0) {
            NsThreadFatal("Nsthreads_LibInit", "pthread_key_create", err);
        }
        NsInitThreads();
    }

#ifdef __linux
    {
    size_t n;
    n = confstr(_CS_GNU_LIBPTHREAD_VERSION, NULL, 0);
    if (n > 0) {
        char *buf = alloca(n);
        confstr(_CS_GNU_LIBPTHREAD_VERSION, buf, n);
        if (!strstr (buf, "NPTL")) {
            Tcl_Panic("Linux \"NPTL\" thread library required. Found: \"%s\"", buf);
        }
    }
    }
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetTls --
 *
 *	Return the TLS slots.
 *
 * Results:
 *	Pointer to slots array.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void **
NsGetTls(void)
{
    void **slots;

    slots = pthread_getspecific(key);
    if (slots == NULL) {
	slots = ns_calloc(NS_THREAD_MAXTLS, sizeof(void *));
	pthread_setspecific(key, slots);
    }
    return slots;
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadLibName --
 *
 *	Return the string name of the thread library.
 *
 * Results:
 *	Pointer to static string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

const char *
NsThreadLibName(void)
{
    return "pthread";
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockAlloc --
 *
 *	Allocate and initialize a mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsLockAlloc(void)
{
    pthread_mutex_t *lock;
    int err;

    lock = ns_malloc(sizeof(pthread_mutex_t));
    err = pthread_mutex_init(lock, NULL);
    if (err != 0) {
    	NsThreadFatal("NsLockAlloc", "pthread_mutex_init", err);
    }
    return lock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockFree --
 *
 *	Free a mutex lock.
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
NsLockFree(void *lock)
{
    int err;

    assert(lock != NULL);

    err = pthread_mutex_destroy((pthread_mutex_t *) lock);
    if (err != 0) {
    	NsThreadFatal("NsLockFree", "pthread_mutex_destroy", err);
    }
    ns_free(lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockSet --
 *
 *	Set a mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May wait wakeup event if lock already held.
 *
 *----------------------------------------------------------------------
 */

void
NsLockSet(void *lock)
{
    int err;

    assert(lock != NULL);

    err = pthread_mutex_lock((pthread_mutex_t *) lock);
    if (err != 0) {
    	NsThreadFatal("NsLockSet", "pthread_mutex_lock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockTry --
 *
 *	Try to set a mutex lock once.
 *
 * Results:
 *	1 if lock set, 0 otherwise.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(void *lock)
{
    int err;

    assert(lock != NULL);

    err = pthread_mutex_trylock((pthread_mutex_t *) lock);
    if (unlikely(err == EBUSY)) {
	return 0;
    } else if (unlikely(err != 0)) {
    	NsThreadFatal("NsLockTry", "pthread_mutex_trylock", err);
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockUnset --
 *
 *	Unset a mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May signal wakeup event for a waiting thread.
 *
 *----------------------------------------------------------------------
 */

void
NsLockUnset(void *lock)
{
    int err;

    assert(lock != NULL);

    err = pthread_mutex_unlock((pthread_mutex_t *) lock);
    if (unlikely(err != 0)) {
    	NsThreadFatal("NsLockUnset", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsCreateThread --
 *
 *	Pthread specific thread create function called by
 *	Ns_ThreadCreate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on thread startup routine.
 *
 *----------------------------------------------------------------------
 */

void
NsCreateThread(void *arg, long stacksize, Ns_Thread *resultPtr)
{
    static char *func = "NsCreateThread";
    pthread_attr_t attr;
    pthread_t thr;
    int err;

    err = pthread_attr_init(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_init", err);
    }

    /*
     * Set the stack size if specified explicitly.  It is smarter
     * to leave the default on platforms which map large stacks
     * with guard zones (e.g., Solaris and Linux).
     */

    if (stacksize > 0) {
        if (stacksize < PTHREAD_STACK_MIN) {
            stacksize = PTHREAD_STACK_MIN;
        } else {
	  /*
	   * The stack-size has to be a multiple of the page-size,
	   * otherwise pthread_attr_setstacksize fails. When we have
	   * _SC_PAGESIZE defined, try to be friendly and round the
	   * stack-size to the next multiple of the page-size.
	   */
#if defined(_SC_PAGESIZE)
	    int pageSize = sysconf(_SC_PAGESIZE);
	    stacksize = (((stacksize-1) / pageSize) + 1) * pageSize;
#endif
	}
        err = pthread_attr_setstacksize(&attr, (size_t) stacksize);
        if (err != 0) {
            NsThreadFatal(func, "pthread_attr_setstacksize", err);
        }
    }

    /*
     * System scope always preferred, ignore any unsupported error.
     */

    err = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if (err != 0 && err != ENOTSUP) {
        NsThreadFatal(func, "pthread_setscope", err);
    }

    err = pthread_create(&thr, &attr, ThreadMain, arg);
    if (err != 0) {
        NsThreadFatal(func, "pthread_create", err);
    }
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_destroy", err);
    }
    if (resultPtr != NULL) {
	*resultPtr = (Ns_Thread) thr;
    } else {
    	err = pthread_detach(thr);
        if (err != 0) {
            NsThreadFatal(func, "pthread_detach", err);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadExit --
 *
 *	Terminate a thread.  Note the use of _endthreadex instead of
 *	ExitThread which, as above, is corrent.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread will clean itself up via the DllMain thread detach code.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadExit(void *arg)
{
    NsThreadShutdownStarted();
    pthread_exit(arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadJoin --
 *
 *	Wait for exit of a non-detached thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Requested thread is destroyed after join.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadJoin(Ns_Thread *thread, void **argPtr)
{
    pthread_t thr = (pthread_t) *thread;
    int err;

    assert(thread != NULL);

    err = pthread_join(thr, argPtr);
    if (err != 0) {
	NsThreadFatal("Ns_ThreadJoin", "pthread_join", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *	Yield the cpu to another thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See sched_yield().
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadYield(void)
{
    sched_yield();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadId --
 *
 *	Return the numeric thread id.
 *
 * Results:
 *	Integer thread id.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

uintptr_t
Ns_ThreadId(void)
{
    return (uintptr_t) pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadSelf --
 *
 *	Return thread handle suitable for Ns_ThreadJoin.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Value at threadPtr is updated with thread's handle.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadSelf(Ns_Thread *threadPtr)
{
    assert(threadPtr != NULL);
    
    *threadPtr = (Ns_Thread) pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Pthread condition variable initialization.  Note this routine
 *	isn't used directly very often as static condition variables
 *	are now self initialized when first used.
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
Ns_CondInit(Ns_Cond *cond)
{
    pthread_cond_t *condPtr;
    int             err;

    assert(cond != NULL);
    
    condPtr = ns_malloc(sizeof(pthread_cond_t));
    err = pthread_cond_init(condPtr, NULL);
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "pthread_cond_init", err);
    }
    *cond = (Ns_Cond) condPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	Pthread condition destroy.  Note this routine is almost never
 *	used as condition variables normally exist in memory until
 *	the process exits.
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
Ns_CondDestroy(Ns_Cond *cond)
{
    pthread_cond_t *condPtr = (pthread_cond_t *) *cond;

    if (condPtr != NULL) {
        int err;

    	err = pthread_cond_destroy(condPtr);
    	if (err != 0) {
    	    NsThreadFatal("Ns_CondDestroy", "pthread_cond_destroy", err);
    	}
    	ns_free(condPtr);
    	*cond = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *	Pthread condition signal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_signal.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *cond)
{
    int             err;

    assert(cond != NULL);
    
    err = pthread_cond_signal(GetCond(cond));
    if (err != 0) {
        NsThreadFatal("Ns_CondSignal", "pthread_cond_signal", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Pthread condition broadcast.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_broadcast.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *cond)
{
    int             err;

    assert(cond != NULL);

    err = pthread_cond_broadcast(GetCond(cond));
    if (err != 0) {
        NsThreadFatal("Ns_CondBroadcast", "pthread_cond_broadcast", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *	Pthread indefinite condition wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_wait.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondWait(Ns_Cond *cond, Ns_Mutex *mutex)
{
    int err;

    assert(cond != NULL);
    assert(mutex != NULL);

    err = pthread_cond_wait(GetCond(cond), NsGetLock(mutex));
    if (err != 0) {
	NsThreadFatal("Ns_CondWait", "pthread_cond_wait", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *	Pthread absolute time wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_timewait.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *cond, Ns_Mutex *mutex, const Ns_Time *timePtr)
{
    int              err, status = NS_ERROR;
    struct timespec  ts;

    assert(cond != NULL);
    assert(mutex != NULL);
    
    if (timePtr == NULL) {
	Ns_CondWait(cond, mutex);
	return NS_OK;
    }

    /*
     * Convert the microsecond-based Ns_Time to a nanosecond-based
     * struct timespec.
     */

    ts.tv_sec = timePtr->sec;
    ts.tv_nsec = timePtr->usec * 1000;

    /*
     * As documented on Linux, pthread_cond_timedwait may return
     * EINTR if a signal arrives.  We have noticed that
     * EINTR can be returned on Solaris as well although this
     * is not documented.  We assume the wakeup is truely
     * spurious and simply restart the wait knowing that the
     * ts structure has not been modified.
     */

    do {
    	err = pthread_cond_timedwait(GetCond(cond), NsGetLock(mutex), &ts);
    } while (err == EINTR);
    if (err == ETIMEDOUT) {
	status = NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_timedwait", err);
    } else {
	status = NS_OK;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCond --
 *
 *	Cast an Ns_Cond to pthread_cond_t, initializing if needed.
 *
 * Results:
 *	Pointer to pthread_cond_t.
 *
 * Side effects:
 *	Ns_Cond is initialized the first time.
 *
 *----------------------------------------------------------------------
 */

static pthread_cond_t *
GetCond(Ns_Cond *cond)
{
    assert(cond != NULL);
    
    if (*cond == NULL) {
    	Ns_MasterLock();
    	if (*cond == NULL) {
	    Ns_CondInit(cond);
    	}
    	Ns_MasterUnlock();
    }
    return (pthread_cond_t *) *cond;
}


/*
 *----------------------------------------------------------------------
 *
 * ThreadMain --
 *
 *	Pthread startup routine.
 *
 * Results:
 *	Does not return.
 *
 * Side effects:
 *	NsThreadMain will call Ns_ThreadExit.
 *
 *----------------------------------------------------------------------
 */

static void *
ThreadMain(void *arg)
{
    NsThreadMain(arg);
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * CleanupTls --
 *
 *	Pthread TLS cleanup.  This routine is called during thread
 *	exit.  This routine could be called more than once if some
 *	other pthread cleanup requires nsthreads TLS.
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
CleanupTls(void *arg)
{
    void **slots = arg;
    Ns_Thread thread = NULL;
    
    assert(arg != NULL);
    
    /*
     * Restore the current slots during cleanup so handlers can access
     * TLS in other slots.
     */

    pthread_setspecific(key, arg);
    Ns_ThreadSelf(&thread);
    NsCleanupTls(slots);
    pthread_setspecific(key, NULL);
    ns_free(slots);
}

#endif

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

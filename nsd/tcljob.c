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
 * tcljob.c --
 *
 *	Tcl job queueing routines.
 *
 * Lock rules:
 *
 *   o. lock the queuelock when modifing tp structure elements.
 *   o. lock the queue's lock when modifing queue structure elements.
 *   o  jobs are shared between tp and the queue but are owned by the queue,
 *      so use queue's lock is used to control access to the jobs.
 *   o. to avoid deadlock, when locking both the queuelock and queue's
 *      lock lock the queuelock first
 *   o. to avoid deadlock, the tp queuelock should be locked before the
 *      queue's lock.
 *
 *
 * Notes:
 *
 *   The threadpool's max number of thread is the sum of all the current
 *   queue's max threads.
 *
 *   The number of threads in the thread pool can be greater than
 *   the current max number of threads. This situtation can occur when
 *   a queue is deleted. Later on if a new queue is created it will simply
 *   use one of the previously created threads. Basically the number of
 *   threads is a "high water mark".
 *
 *   The queues are reference counted. Only when a queue is empty and
 *   its reference count is zero can it be deleted.
 *
 *   We can no longer use a Tcl_Obj to represent the queue because queues can
 *   now be deleted. Tcl_Objs are deleted when the object goes out of
 *   scope, whereas queues are deleted when delete is called. By doing
 *   this the queue can be used across tcl interpreters.
 *
 * ToDo:
 *
 *   Users can leak queues. A queue will stay around until a user
 *   cleans it up. It order to help the user out we would like to
 *   add an "-autoclean" option to queue create function. However,
 *   AOLServer does not currently supply a "good" connection cleanup
 *   callback. We tryed to use "Ns_RegisterConnCleanup" however it does
 *   not have a facility to remove registered callbacks.
 *
 */

#include "nsd.h"

/*
 * If a user does not specify the a max number of threads for a queue,
 * then the following default is used.
 */

#define NS_JOB_DEFAULT_MAXTHREADS 4

typedef enum JobStates {
    JOB_SCHEDULED = 0,
    JOB_RUNNING,
    JOB_DONE
} JobStates;

typedef enum JobTypes {
    JOB_NON_DETACHED = 0,
    JOB_DETACHED
} JobTypes;

typedef enum JobRequests {
    JOB_NONE = 0,
    JOB_WAIT
} JobRequests;

typedef enum QueueRequests {
    QUEUE_REQ_NONE = 0,
    QUEUE_REQ_DELETE
} QueueRequests;

typedef enum ThreadPoolRequests {
    THREADPOOL_REQ_NONE = 0,
    THREADPOOL_REQ_STOP
} ThreadPoolRequests;


/*
 * Jobs are enqueued on queues.
 */

typedef struct Job {
    struct Job       *nextPtr;
    CONST char       *server;
    JobStates         state;
    int               code;
    int               cancel;
    JobTypes          type;
    JobRequests       req;
    char             *errorCode;
    char             *errorInfo;
    char             *queueId;
    uintptr_t         tid;
    Tcl_AsyncHandler  async;
    Tcl_DString       id;
    Tcl_DString       script;
    Tcl_DString       results;
    Ns_Time           startTime;
    Ns_Time           endTime;
} Job;

/*
 * A queue manages a set of jobs.
 */

typedef struct Queue {
    char              *name;
    char              *desc;
    Ns_Mutex           lock;
    Ns_Cond            cond;
    unsigned int       nextid;
    QueueRequests      req;
    int                maxThreads;
    int                nRunning;
    Tcl_HashTable      jobs;
    int                refCount;
} Queue;


/*
 * A threadpool mananges a global set of threads.
 */
typedef struct ThreadPool {
    Ns_Cond            cond;
    Ns_Mutex           queuelock;
    Tcl_HashTable      queues;
    ThreadPoolRequests req;
    int                nextThreadId;
    unsigned long      nextQueueId;
    int                maxThreads;
    int                nthreads;
    int                nidle;
    Job               *firstPtr;
    int                jobsPerThread;
    int                timeout;
} ThreadPool;


/*
 * Function prototypes/forward declarations.
 */

static void   JobThread(void *arg);
static Job*   GetNextJob(void);

static Queue* NewQueue(CONST char* queueName, CONST char* queueDesc,
                       int maxThreads);
static void   FreeQueue(Queue *queuePtr);

static Job*   NewJob(CONST char* server, CONST char* queueName,
                     int type, char *script);
static void   FreeJob(Job *jobPtr);
static int    JobAbort(ClientData cd, Tcl_Interp *interp, int code);

static int    LookupQueue(Tcl_Interp *interp, CONST char *queue_name,
                          Queue **queuePtr, int locked);
static int    ReleaseQueue(Queue *queuePtr, int locked);
static int    AnyDone(Queue *queue);
static void   SetupJobDefaults(void);

static CONST char* GetJobCodeStr(int code);
static CONST char* GetJobStateStr(JobStates state);
static CONST char* GetJobTypeStr(JobTypes type);
static CONST char* GetJobReqStr(JobRequests req);
static CONST char* GetQueueReqStr(QueueRequests req);
static CONST char* GetTpReqStr(ThreadPoolRequests req);

static int AppendField(Tcl_Interp *interp, Tcl_Obj *list,
                       CONST char *name, CONST char *value);

static int AppendFieldInt(Tcl_Interp *interp, Tcl_Obj *list,
                          CONST char *name, int value);

static int AppendFieldLong(Tcl_Interp *interp, Tcl_Obj *list,
                           CONST char *name, long value);

static int AppendFieldDouble(Tcl_Interp *interp, Tcl_Obj *list,
                             CONST char *name, double value);

static double ComputeDelta(Ns_Time *start, Ns_Time *end);

/*
 * Globals
 */

static ThreadPool tp;


/*
 *----------------------------------------------------------------------
 *
 * NsInitTclQueueType --
 *
 *	    Initialize the Tcl job queue.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitQueueType(void)
{
    Tcl_InitHashTable(&tp.queues, TCL_STRING_KEYS);
    Ns_MutexSetName(&tp.queuelock, "jobThreadPool");
    tp.nextThreadId = 0;
    tp.nextQueueId = 0;
    tp.maxThreads = 0;
    tp.nthreads = 0;
    tp.nidle = 0;
    tp.firstPtr = NULL;
    tp.req = THREADPOOL_REQ_NONE;
    tp.jobsPerThread = 0;
    tp.timeout = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartJobsShutdown --
 *
 *	    Signal stop of the Tcl job threads.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    All pending jobs are removed and waiting threads interrupted.
 *
 *----------------------------------------------------------------------
 */

void
NsStartJobsShutdown(void)
{
    Tcl_HashSearch  search;
    Tcl_HashEntry  *hPtr;

    hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
    while (hPtr != NULL) {
        Ns_MutexLock(&tp.queuelock);
    	tp.req = THREADPOOL_REQ_STOP;
    	Ns_CondBroadcast(&tp.cond);
    	Ns_MutexUnlock(&tp.queuelock);
        hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitJobsShutdown --
 *
 *	    Wait for Tcl job threads to exit.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

void
NsWaitJobsShutdown(Ns_Time *toPtr)
{
    Tcl_HashSearch  search;
    Tcl_HashEntry  *hPtr;
    int             status = NS_OK;

    hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
    while (status == NS_OK && hPtr != NULL) {
        Ns_MutexLock(&tp.queuelock);
    	while (status == NS_OK && tp.nthreads > 0) {
            status = Ns_CondTimedWait(&tp.cond, &tp.queuelock, toPtr);
    	}
    	Ns_MutexUnlock(&tp.queuelock);
        hPtr = Tcl_NextHashEntry(&search);
    }
    if (status != NS_OK) {
        Ns_Log(Warning, "tcljobs: timeout waiting for exit");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclJobCmd --
 *
 *	    Implement the ns_job command to manage background tasks.
 *
 * Results:
 *	    Standard Tcl result.
 *
 * Side effects:
 *	    Jobs may be queued to run in another thread.
 *
 *----------------------------------------------------------------------
 */

int
NsTclJobObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp       *itPtr = arg;
    Queue          *queuePtr = NULL;
    Job            *jobPtr = NULL;
    int             code, isNew, max, opt, argIndex;
    char           *jobId = NULL, buf[100], *queueId = NULL;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch search;

    static CONST char *opts[] = {
        "cancel", "create", "delete", "genid", "jobs", "joblist",
        "threadlist", "queue", "queues", "queuelist", "wait",
        "waitany",  "exists", "configure", NULL
    };

    enum {
        JCancelIdx, JCreateIdx, JDeleteIdx, JGenIDIdx, JJobsIdx, JJobsListIdx,
        JThreadListIdx, JQueueIdx, JQueuesIdx, JQueueListIdx, JWaitIdx,
        JWaitAnyIdx, JExistsIdx, JConfigureIdx
    };

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", TCL_EXACT,
                            &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    code = TCL_OK;

    switch (opt) {
    case JConfigureIdx:
        {
            /*
             * ns_job configure
             *
             * Configure jobs subsystem
             */

            int jpt = -1, timeout = -1;

            Ns_ObjvSpec opts[] = {
                {"-jobsperthread",  Ns_ObjvInt,  &jpt,     NULL},
                {"-timeout",        Ns_ObjvInt,  &timeout, NULL},
                {NULL, NULL, NULL, NULL}
            };
            Ns_ObjvSpec args[] = {
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(opts, args, interp, 2, objc, objv) != NS_OK) {
                return TCL_ERROR;
            }
            Ns_MutexLock(&tp.queuelock);
            SetupJobDefaults();

            if (jpt >= 0) {
                tp.jobsPerThread = jpt;
            }
            if (timeout >= 0) {
                tp.timeout = timeout;
            }
            snprintf(buf, sizeof(buf), "jobsperthread %d timeout %d",
                     tp.jobsPerThread, tp.timeout);
            Ns_MutexUnlock(&tp.queuelock);
            Tcl_AppendResult(interp, buf, NULL);
        }
        break;

    case JCreateIdx:
        {
            /*
             * ns_job create
             *
             * Create a new thread pool queue.
             */

            Tcl_Obj *queueIdObj = NULL;
            char    *queueDesc  = "";

            argIndex = 2;
            if ((objc < 3) || (objc > 6)) {
                Tcl_WrongNumArgs(interp, 2, objv,
                                 "?-desc description? queueId ?maxThreads?");
                return TCL_ERROR;
            }
            if (objc > 3) {
                if (strncmp(Tcl_GetString(objv[argIndex]), "-desc",
                            strlen("-desc")) == 0) {
                    ++argIndex;
                    queueDesc = Tcl_GetString(objv[argIndex++]);
                }
            }

            queueIdObj = objv[argIndex++];
            queueId = Tcl_GetString(queueIdObj);

            max = NS_JOB_DEFAULT_MAXTHREADS;
            if ((objc > argIndex) &&
                Tcl_GetIntFromObj(interp, objv[argIndex++], &max) != TCL_OK) {
                return TCL_ERROR;
            }

            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_CreateHashEntry(&tp.queues, Tcl_GetString(queueIdObj),
                                       &isNew);
            if (isNew) {
                queuePtr = NewQueue(Tcl_GetHashKey(&tp.queues, hPtr),
                                    queueDesc, max);
                Tcl_SetHashValue(hPtr, queuePtr);
            }
            Ns_MutexUnlock(&tp.queuelock);
            if (!isNew) {
                Tcl_AppendResult(interp, "queue already exists: ", queueId, NULL);
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, queueIdObj);
        }
        break;

    case JDeleteIdx:
        {
            /*
             * ns_job delete
             *
             * Request that the specified queue be deleted. The queue will
             * only be deleted when all jobs are removed.
             */

            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            queuePtr->req = QUEUE_REQ_DELETE;
            ReleaseQueue(queuePtr, 0);
            Ns_CondBroadcast(&tp.cond);
        }
        break;

    case JQueueIdx:
        {
            /*
             * ns_job queue
             *
             * Add a new job the specified queue.
             */

	    int   create = 0, head = 0, jobType = JOB_NON_DETACHED;
            char *script = NULL;

            Ns_ObjvSpec opts[] = {
                {"-head",      Ns_ObjvBool,    &head,     (void *) 1},
                {"-detached",  Ns_ObjvBool,    &jobType,  (void *) JOB_DETACHED},
                {"-jobid",     Ns_ObjvString,  &jobId,    NULL},
                {NULL, NULL, NULL, NULL}
            };
            Ns_ObjvSpec args[] = {
                {"queueId",  Ns_ObjvString,  &queueId,  NULL},
                {"script",   Ns_ObjvString,  &script,   NULL},
                {NULL, NULL, NULL, NULL}
            };

            if (Ns_ParseObjv(opts, args, interp, 2, objc, objv) != NS_OK) {
                return TCL_ERROR;
            }

            Ns_MutexLock(&tp.queuelock);

            if (LookupQueue(interp, queueId, &queuePtr, 1) != TCL_OK) {
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }

            /*
             * Create a new job and add to the Thread Pool's list of jobs.
             */

            jobPtr = NewJob((itPtr->servPtr ? itPtr->servPtr->server : NULL),
                            queuePtr->name, jobType, script);
            Ns_GetTime(&jobPtr->startTime);
            if (tp.req == THREADPOOL_REQ_STOP
                || queuePtr->req == QUEUE_REQ_DELETE) {
                Tcl_AppendResult(interp,
                                 "The specified queue is being deleted or "
                                 "the system is stopping.", NULL);
                FreeJob(jobPtr);
                ReleaseQueue(queuePtr, 1);
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }

            /*
             * Job id is given, try to see if it is taken already,
             * if yes, return error, it should be unique
             */

            if (jobId != NULL && *jobId != '\0') {
                hPtr = Tcl_CreateHashEntry(&queuePtr->jobs, jobId, &isNew);
                if (!isNew) {
                    FreeJob(jobPtr);
                    ReleaseQueue(queuePtr, 1);
                    Ns_MutexUnlock(&tp.queuelock);
                    Tcl_AppendResult(interp, "Job ", jobId,
                                     " already exists", NULL);
                    return TCL_ERROR;
                }
            } else {

                /*
                 * Add the job to queue.
                 */

                do {
                    snprintf(buf, sizeof(buf), "job%d", queuePtr->nextid++);
                    hPtr = Tcl_CreateHashEntry(&queuePtr->jobs, buf, &isNew);
                } while (!isNew);

                jobId = buf;
            }

            /*
             * Add the job to the thread pool's job list, if -head is
             * specified, insert new job at the beginning, otherwise append
             * new job to the end
             */

            if (head) {
                jobPtr->nextPtr = tp.firstPtr;
                tp.firstPtr = jobPtr;
            } else {
	        Job  **nextPtrPtr = &tp.firstPtr;

                while (*nextPtrPtr != NULL) {
                    nextPtrPtr = &((*nextPtrPtr)->nextPtr);
                }
                *nextPtrPtr = jobPtr;
            }

            /*
             * Start a new thread if there are less than maxThreads currently
             * running and there currently no idle threads.
             */

            if (tp.nidle == 0 && tp.nthreads < tp.maxThreads) {
                create = 1;
                ++tp.nthreads;
            } else {
                create = 0;
            }

            Tcl_DStringAppend(&jobPtr->id, jobId, -1);
            Tcl_SetHashValue(hPtr, jobPtr);
            Ns_CondBroadcast(&tp.cond);

            ReleaseQueue(queuePtr, 1);
            Ns_MutexUnlock(&tp.queuelock);
            if (create) {
                Ns_ThreadCreate(JobThread, 0, 0, NULL);
            }
            Tcl_SetResult(interp, jobId, TCL_VOLATILE);
        }
        break;

    case JWaitIdx:
        {
            /*
             * ns_job wait
             *
             * Wait for the specified job.
             */

            int     timeoutFlag = 0;
            Ns_Time timeout, delta_timeout;

            argIndex = 2;
            if (objc != 4 && objc != 6) {
                Tcl_WrongNumArgs(interp, 2, objv,
                                 "?-timeout timeout? queueId jobId");
                return TCL_ERROR;
            }
            if (objc > 4) {
                if (strcmp(Tcl_GetString(objv[argIndex++]), "-timeout") == 0) {
                    timeoutFlag = 1;
                    if (Ns_TclGetTimeFromObj(interp, objv[argIndex++],
                                             &delta_timeout) != TCL_OK) {
                        return TCL_ERROR;
                    }

                    /*
                     * Set the timeout time. This is an absolute time.
                     */

                    Ns_GetTime(&timeout);
                    Ns_IncrTime(&timeout,delta_timeout.sec,delta_timeout.usec);
                }
            }

            if (LookupQueue(interp, Tcl_GetString(objv[argIndex++]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            jobId = Tcl_GetString(objv[argIndex++]);
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);
            if (hPtr == NULL) {
                ReleaseQueue(queuePtr, 0);
                Tcl_AppendResult(interp, "no such job: ", jobId, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(hPtr);

            if (jobPtr->type == JOB_DETACHED) {
                Tcl_AppendResult(interp, "can't wait on detached job: ",
                                 jobId, NULL);
                ReleaseQueue(queuePtr, 0);
                return TCL_ERROR;
            }

            if (jobPtr->req == JOB_WAIT) {
                Tcl_AppendResult(interp, "can't wait on waited job: ",
                                 jobId, NULL);
                ReleaseQueue(queuePtr, 0);
                return TCL_ERROR;
            }

            jobPtr->req = JOB_WAIT;

            if (timeoutFlag) {
                while (jobPtr->state != JOB_DONE) {
                    int timedOut = Ns_CondTimedWait(&queuePtr->cond,
						    &queuePtr->lock, &timeout);
                    if (timedOut == NS_TIMEOUT) {
                        Tcl_SetResult(interp, "Wait timed out.", TCL_STATIC);
                        Tcl_SetErrorCode(interp, "NS_TIMEOUT", NULL);
                        jobPtr->req = JOB_NONE;
                        ReleaseQueue(queuePtr, 0);
                        return TCL_ERROR;
                    }
                }
            } else {
                while (jobPtr->state != JOB_DONE) {
                    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
                }
            }

            /*
             * At this point the job we were waiting on has completed,
             * so we return the job's results and errorcodes, then
             * clean up the job.
             */

            /*
             * The following is a sanity check that ensures no other
             * process removed this job's entry.
             */

            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);

            if (hPtr == NULL || jobPtr == Tcl_GetHashValue(hPtr)) {
                Tcl_SetResult(interp, "Internal ns_job error.", TCL_STATIC);
            }

            Tcl_DeleteHashEntry(hPtr);
            ReleaseQueue(queuePtr, 0);

            Tcl_DStringResult(interp, &jobPtr->results);
            code = jobPtr->code;
            if (code == TCL_ERROR) {
                if (jobPtr->errorCode != NULL) {
                    Tcl_SetErrorCode(interp, jobPtr->errorCode, NULL);
                }
                if (jobPtr->errorInfo != NULL) {
                     Tcl_AddObjErrorInfo(interp, "\n", 1);
                     Tcl_AddObjErrorInfo(interp, jobPtr->errorInfo, -1);
                }
            }
            FreeJob(jobPtr);
        }
        break;

    case JCancelIdx:
        {
            /*
             * ns_job cancel
             *
             * Cancel the specified job.
             */

            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId jobId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]), 
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            jobId = Tcl_GetString(objv[3]);
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);
            if (hPtr == NULL) {
                ReleaseQueue(queuePtr, 0);
                Tcl_AppendResult(interp, "no such job: ", jobId, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(hPtr);

            if (jobPtr->req == JOB_WAIT) {
                Tcl_AppendResult(interp,"can't cancel job \"",
                                 Tcl_DStringValue(&jobPtr->id),
                                 "\", someone is waiting on it", NULL);
                ReleaseQueue(queuePtr, 0);
                return TCL_ERROR;
            }
            jobPtr->cancel = 1;
            if (jobPtr->async != NULL) {
                Tcl_AsyncMark(jobPtr->async);
            }
            Ns_CondBroadcast(&queuePtr->cond);
            Ns_CondBroadcast(&tp.cond);
            Tcl_SetObjResult(interp,
                             Tcl_NewBooleanObj(jobPtr->state == JOB_RUNNING));
            ReleaseQueue(queuePtr, 0);
        }
        break;

    case JExistsIdx:
        {
            /*
             * ns_job exists
             *
             * Returns 1 if job is running otherwise 0
             */

            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId jobId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]), 
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            jobId = Tcl_GetString(objv[3]);
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);
            ReleaseQueue(queuePtr, 0);
            Tcl_SetObjResult(interp, Tcl_NewBooleanObj(hPtr != NULL));
        }
        break;

    case JWaitAnyIdx:
        {
            /*
             * ns_job waitany
             *
             * Wait for any job on the queue complete.
             */

            int     timeoutFlag = 0;
            Ns_Time timeout, delta_timeout;

            argIndex = 2;
            if (objc != 3 && objc != 5) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-timeout timeout? queueId");
                return TCL_ERROR;
            }
            if (objc > 3) {
                if (strcmp(Tcl_GetString(objv[argIndex++]), "-timeout") == 0) {
                    timeoutFlag = 1;
                    if (Ns_TclGetTimeFromObj(interp, objv[argIndex++],
                                             &delta_timeout) != TCL_OK) {
                        return TCL_ERROR;
                    }

                    /*
                     * Set the timeout time. This is an absolute time.
                     */

                    Ns_GetTime(&timeout);
                    Ns_IncrTime(&timeout,delta_timeout.sec,delta_timeout.usec);
                }
            }

            if (LookupQueue(interp, Tcl_GetString(objv[argIndex++]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }

            /*
             * While there are jobs in queue or no jobs are "done", wait
             * on the queue condition variable.
             */

            if (timeoutFlag) {
                while ((Tcl_FirstHashEntry(&queuePtr->jobs, &search) != NULL)
                       && !AnyDone(queuePtr)) {
                    int timedOut = Ns_CondTimedWait(&queuePtr->cond,
						    &queuePtr->lock, &timeout);
                    if (timedOut == NS_TIMEOUT) {
                        Tcl_SetResult(interp, "Wait timed out.", TCL_STATIC);
                        Tcl_SetErrorCode(interp, "NS_TIMEOUT", NULL);
                        ReleaseQueue(queuePtr, 0);
                        return TCL_ERROR;
                    }
                }
            } else {
                while ((Tcl_FirstHashEntry(&queuePtr->jobs, &search) != NULL)
                       && !AnyDone(queuePtr)) {
                    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
                }
            }

            ReleaseQueue(queuePtr, 0);
        }
        break;

    case JJobsIdx:
        {
            /*
             * ns_job jobs
             *
             * Returns a list of job IDs in arbitrary order.
             */

            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
            while (hPtr != NULL) {
                jobId = Tcl_GetHashKey(&queuePtr->jobs, hPtr);
                Tcl_AppendElement(interp, jobId);
                hPtr = Tcl_NextHashEntry(&search);
            }
            ReleaseQueue(queuePtr, 0);
        }
        break;

    case JQueuesIdx:
        {
            /*
             * ns_job queues
             *
             * Returns a list of the current queues.
             */

            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
            while (hPtr != NULL) {
                queuePtr = Tcl_GetHashValue(hPtr);
                Tcl_AppendElement(interp, queuePtr->name);
                hPtr = Tcl_NextHashEntry(&search);
            }
            Ns_MutexUnlock(&tp.queuelock);
        }
        break;

    case JJobsListIdx:
        {
            /*
             * ns_job joblist
             *
             * Returns a list of all the jobs in the queue.
             * The "job" consists of:
             *    ID
             *    State   (Scheduled, Running, or Done)
             *    Results (or job script, if job has not yet completed).
             *    Code    (Standard Tcl result code)
             */

            Tcl_Obj    *jobList;
            char        thrId[32];

            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }

            /* Create a Tcl List to hold the list of jobs. */
            jobList = Tcl_NewListObj(0, NULL);
            hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
            while (hPtr != NULL) {
		CONST char *jobId, *jobState, *jobCode, *jobType, *jobReq;
		char       *jobResults, *jobScript;
		Tcl_Obj    *jobFieldList;
		double      delta;

                jobPtr = (Job *)Tcl_GetHashValue(hPtr);
                jobId      = Tcl_GetHashKey(&queuePtr->jobs, hPtr);
                jobCode    = GetJobCodeStr(jobPtr->code);
                jobState   = GetJobStateStr(jobPtr->state);
                jobType    = GetJobTypeStr(jobPtr->type);
                jobReq     = GetJobReqStr(jobPtr->req);
                jobResults = Tcl_DStringValue(&jobPtr->results);
                jobScript  = Tcl_DStringValue(&jobPtr->script);
                if (   jobPtr->state == JOB_SCHEDULED
                    || jobPtr->state == JOB_RUNNING) {
                    Ns_GetTime(&jobPtr->endTime);
                }
                delta = ComputeDelta(&jobPtr->startTime, &jobPtr->endTime);
                snprintf(thrId, sizeof(thrId), "%" PRIxPTR, jobPtr->tid);

                /* Create a Tcl List to hold the list of job fields. */
                jobFieldList = Tcl_NewListObj(0, NULL);
                if (   AppendField(interp, jobFieldList, "id",
                                   jobId) != TCL_OK
                    || AppendField(interp, jobFieldList, "state",
                                   jobState) != TCL_OK
                    || AppendField(interp, jobFieldList, "results",
                                   jobResults) != TCL_OK
                    || AppendField(interp, jobFieldList, "script",
                                   jobScript) != TCL_OK
                    || AppendField(interp, jobFieldList, "code",
                                   jobCode) != TCL_OK
                    || AppendField(interp, jobFieldList, "type",
                                   jobType) != TCL_OK
                    || AppendField(interp, jobFieldList, "req",
                                   jobReq) != TCL_OK
                    || AppendField(interp, jobFieldList, "thread",
                                   thrId) != TCL_OK
                    || AppendFieldDouble(interp, jobFieldList, "time",
                                         delta) != TCL_OK
                    || AppendFieldLong(interp, jobFieldList, "starttime",
                                       (long)jobPtr->startTime.sec) != TCL_OK
                    || AppendFieldLong(interp, jobFieldList, "endtime",
                                       (long)jobPtr->endTime.sec) != TCL_OK) {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    ReleaseQueue(queuePtr, 0);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, jobList,
                                             jobFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    ReleaseQueue(queuePtr, 0);
                    return TCL_ERROR;
                }
                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, jobList);
            ReleaseQueue(queuePtr, 0);
        }
        break;

    case JQueueListIdx:
        {
            /*
             * ns_job queuelist
             *
             * Returns a list of all the queues and the queue information.
             */

            Tcl_Obj    *queueList;

            /* Create a Tcl List to hold the list of jobs. */
            queueList = Tcl_NewListObj(0, NULL);
            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_FirstHashEntry(&tp.queues, &search);

            while (hPtr != NULL) {
	        CONST char *queueReq;
		Tcl_Obj    *queueFieldList;

                queuePtr = Tcl_GetHashValue(hPtr);
                /* Create a Tcl List to hold the list of queue fields. */
                queueFieldList = Tcl_NewListObj(0, NULL);
                queueReq = GetQueueReqStr(queuePtr->req);
                /* Add queue name */
                if (AppendField(interp, queueFieldList, "name",
                                queuePtr->name) != TCL_OK
                    || AppendField(interp, queueFieldList, "desc",
                                   queuePtr->desc) != TCL_OK
                    || AppendFieldInt(interp, queueFieldList, "maxthreads",
                                      queuePtr->maxThreads) != TCL_OK
                    || AppendFieldInt(interp, queueFieldList, "numrunning",
                                      queuePtr->nRunning) != TCL_OK
                    || AppendField(interp, queueFieldList, "req",
                                   queueReq) != TCL_OK) {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&tp.queuelock);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, queueList,
                                             queueFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&tp.queuelock);
                    return TCL_ERROR;
                }
                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, queueList);
            Ns_MutexUnlock(&tp.queuelock);
        }
        break;

    case JGenIDIdx:
        {
            /*
             * ns_job genID
             *
             * Generate a unique queue name.
             */

            Ns_Time currentTime;

            Ns_GetTime(&currentTime);
            Ns_MutexLock(&tp.queuelock);
            snprintf(buf, sizeof(buf), "queue_id_%lx_%" TCL_LL_MODIFIER "x",
                     tp.nextQueueId++, (Tcl_WideInt) currentTime.sec);
            Ns_MutexUnlock(&tp.queuelock);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
        }
        break;

    case JThreadListIdx:
        {
            /*
             * ns_job threadlist
             *
             * Return a list of the thread pool's fields.
             *
             */

            Tcl_Obj    *tpFieldList;
            CONST char *tpReq;

            /* Create a Tcl List to hold the list of thread fields. */
            tpFieldList = Tcl_NewListObj(0, NULL);
            Ns_MutexLock(&tp.queuelock);
            tpReq = GetTpReqStr(tp.req);
            if (AppendFieldInt(interp, tpFieldList, "maxthreads",
                               tp.maxThreads) != TCL_OK
                || AppendFieldInt(interp, tpFieldList, "numthreads",
                                  tp.nthreads) != TCL_OK
                || AppendFieldInt(interp, tpFieldList, "numidle",
                                  tp.nidle) != TCL_OK
                || AppendField(interp, tpFieldList, "req", tpReq) != TCL_OK) {
                Tcl_DecrRefCount(tpFieldList);
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }
            Ns_MutexUnlock(&tp.queuelock);
            Tcl_SetObjResult(interp, tpFieldList);
        }
        break;
    }
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * JobThread --
 *
 *	    Background thread for the ns_job command.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    Jobs will be run from the queue.
 *
 *----------------------------------------------------------------------
 */

static void
JobThread(void *arg)
{
    CONST char        *err;
    Queue             *queuePtr;
    Tcl_HashEntry     *hPtr;
    Tcl_AsyncHandler  async;
    Ns_Time           *timePtr, wait;
    int               jpt, njobs, tid;

    Ns_WaitForStartup();
    Ns_MutexLock(&tp.queuelock);
    tid = tp.nextThreadId++;
    Ns_ThreadSetName("-ns_job_%x-", tid);
    Ns_Log(Notice, "Starting thread: -ns_job_%x-", tid);

    async = Tcl_AsyncCreate(JobAbort, NULL);

    SetupJobDefaults();

    /*
     * Setting this parameter to > 0 will cause the thread to
     * graceously exit after processing that many job requests,
     * thus initiating kind-of Tcl-level garbage collection.
     */

    jpt = njobs = tp.jobsPerThread;

    while (jpt == 0 || njobs > 0) {
	Job         *jobPtr;
	Tcl_Interp *interp;
	int         status, code;

        ++tp.nidle;
        status = NS_OK;
        if (tp.timeout > 0) {
            Ns_GetTime(&wait);
            Ns_IncrTime(&wait, tp.timeout, 0);
            timePtr = &wait;
        } else {
            timePtr = NULL;
        }
        jobPtr = NULL;
        while (status == NS_OK &&
               !(tp.req == THREADPOOL_REQ_STOP) &&
               ((jobPtr = GetNextJob()) == NULL)) {
            status = Ns_CondTimedWait(&tp.cond, &tp.queuelock, timePtr);
        }
        --tp.nidle;
        if (tp.req == THREADPOOL_REQ_STOP || jobPtr == NULL) {
            break;
        }

        if (LookupQueue(NULL, jobPtr->queueId, &queuePtr, 1) != TCL_OK) {
            Ns_Log(Fatal, "cannot find queue: %s", jobPtr->queueId);
        }
	assert(queuePtr);

        interp = Ns_TclAllocateInterp(jobPtr->server);

        Ns_GetTime(&jobPtr->endTime);
        Ns_GetTime(&jobPtr->startTime);

        jobPtr->tid   = Ns_ThreadId();
        jobPtr->code  = TCL_OK;
        jobPtr->state = JOB_RUNNING;
        jobPtr->async = async;

        if (jobPtr->cancel) {
            Tcl_AsyncMark(jobPtr->async);
        }

        Ns_ThreadSetName("-%s:%x", jobPtr->queueId, tid);
        ++queuePtr->nRunning;

        Ns_MutexUnlock(&queuePtr->lock);
        Ns_MutexUnlock(&tp.queuelock);

        code = Tcl_EvalEx(interp, jobPtr->script.string, -1, 0);

        Ns_MutexLock(&tp.queuelock);
        Ns_MutexLock(&queuePtr->lock);

        --queuePtr->nRunning;
        Ns_ThreadSetName("-ns_job_%x-", tid);

        jobPtr->state  = JOB_DONE;
        jobPtr->code   = code;
        jobPtr->tid    = 0;
        jobPtr->async  = NULL;

        Ns_GetTime(&jobPtr->endTime);

        /*
         * Make sure we show error message for detached job, otherwise 
         * it will silently disappear
         */

        if (jobPtr->type == JOB_DETACHED && jobPtr->code != TCL_OK) {
            Ns_TclLogError(interp);
        }

        /*
         * Save the results.
         */

        Tcl_DStringAppend(&jobPtr->results, Tcl_GetStringResult(interp), -1);
        if (jobPtr->code == TCL_ERROR) {
            err = Tcl_GetVar(interp, "errorCode", TCL_GLOBAL_ONLY);
            if (err != NULL) {
                jobPtr->errorCode = ns_strdup(err);
            }
            err = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
            if (err != NULL) {
                jobPtr->errorInfo = ns_strdup(err);
            }
        }

        Ns_TclDeAllocateInterp(interp);

        /*
         * Clean any detached jobs.
         */

        if (jobPtr->type == JOB_DETACHED) {
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs,
                                     Tcl_DStringValue(&jobPtr->id));
            if (hPtr != NULL) {
                Tcl_DeleteHashEntry(hPtr);
            }
            FreeJob(jobPtr);
        }

        Ns_CondBroadcast(&queuePtr->cond);
        ReleaseQueue(queuePtr, 1);

        if (jpt && --njobs <= 0) {
            break; /* Served given # of jobs in this thread */
        }
    }

    --tp.nthreads;

    Tcl_AsyncDelete(async);
    Ns_CondBroadcast(&tp.cond);
    Ns_MutexUnlock(&tp.queuelock);

    Ns_Log(Notice, "exiting");
}

/*
 *----------------------------------------------------------------------
 +
 * JobAbort --
 *
 *      Called by Tcl async handling when somebody cancels the job.
 *
 * Results:
 *      Always TCL_ERROR.
 *
 * Side effects:
 *      Causes currently executing Tcl command to return TCL_ERROR.
 *
 *----------------------------------------------------------------------
 */

static int
JobAbort(ClientData cd, Tcl_Interp *interp, int code)
{
    if (interp != NULL) {
        Tcl_SetErrorCode(interp, "ECANCEL", NULL);
        Tcl_SetResult(interp, "Job cancelled.", TCL_STATIC);
    } else {
        Ns_Log(Warning, "ns_job: job cancelled");
    }

    return TCL_ERROR; /* Forces current command error */
}

/*
 *----------------------------------------------------------------------
 *
 * GetNextJob --
 *
 *      Get the next job from the queue.
 *      The queuelock should be held locked.
 *
 * Results:
 *      The job.
 *
 * Side effects:
 *      Queues have a "maxThreads" so if the queue is already
 *      at "maxThreads", jobs of that queue will be skipped.
 *
 *----------------------------------------------------------------------
 */

static Job*
GetNextJob(void)
{
    Queue         *queuePtr;
    Job           *prevPtr, *jobPtr;
    int            done = 0;

    jobPtr = prevPtr = tp.firstPtr;

    while (!done && jobPtr != NULL) {

        if (LookupQueue(NULL, jobPtr->queueId, &queuePtr, 1) != TCL_OK) {
            Ns_Log(Fatal, "cannot find queue: %s", jobPtr->queueId);
        }
	assert(queuePtr);

        if (queuePtr->nRunning < queuePtr->maxThreads) {
            
            /*
             * Job can be serviced; remove from the pending list
             */
            
            if (jobPtr == tp.firstPtr) {
                tp.firstPtr = jobPtr->nextPtr;
            } else {
                prevPtr->nextPtr = jobPtr->nextPtr;
            }
            
            done = 1;

        } else {

            /*
             * Go to next job.
             */

            prevPtr = jobPtr;
            jobPtr = jobPtr->nextPtr;
        }

        ReleaseQueue(queuePtr, 1);
    }

    return jobPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NewQueue --
 *
 *	    Create a thread pool queue.
 *
 * Results:
 *	    Thread pool queue.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

static Queue*
NewQueue(CONST char* queueName, CONST char* queueDesc, int maxThreads)
{
    Queue *queuePtr = NULL;

    queuePtr = ns_calloc(1, sizeof(Queue));
    queuePtr->req = QUEUE_REQ_NONE;

    queuePtr->name = ns_calloc(1, strlen(queueName) + 1);
    strcpy(queuePtr->name, queueName);

    queuePtr->desc = ns_calloc(1, strlen(queueDesc) + 1);
    strcpy(queuePtr->desc, queueDesc);
    queuePtr->maxThreads = maxThreads;

    queuePtr->refCount = 0;

    Ns_MutexSetName2(&queuePtr->lock, "tcljob", queueName);
    Tcl_InitHashTable(&queuePtr->jobs, TCL_STRING_KEYS);

    tp.maxThreads += maxThreads;

    return queuePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeQueue --
 *
 *	    Cleanup the queue
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeQueue(Queue *queuePtr)
{
    Ns_MutexDestroy(&queuePtr->lock);
    Tcl_DeleteHashTable(&queuePtr->jobs);
    ns_free(queuePtr->desc);
    ns_free(queuePtr->name);
    ns_free(queuePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NewJob --
 *
 *	    Create a new job and initialize it.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

static Job*
NewJob(CONST char* server, CONST char* queueId, int type, char *script)
{
    Job *jobPtr;

    jobPtr = ns_calloc(1, sizeof(Job));

    jobPtr->server = server;
    jobPtr->type   = type;
    jobPtr->state  = JOB_SCHEDULED;
    jobPtr->code   = TCL_OK;
    jobPtr->req    = JOB_NONE;

    jobPtr->queueId = ns_calloc(1, strlen(queueId) + 1);
    strcpy(jobPtr->queueId, queueId);

    Tcl_DStringInit(&jobPtr->id);
    Tcl_DStringInit(&jobPtr->script);
    Tcl_DStringAppend(&jobPtr->script, script, -1);
    Tcl_DStringInit(&jobPtr->results);

    return jobPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeJob --
 *
 *	    Destory a Job structure.
 *
 * Results:
 *	    None.
 *
 * Side effects:
 *	    None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeJob(Job *jobPtr)
{
    Tcl_DStringFree(&jobPtr->results);
    Tcl_DStringFree(&jobPtr->script);
    Tcl_DStringFree(&jobPtr->id);

    ns_free(jobPtr->queueId);

    if (jobPtr->errorCode) {
        ns_free(jobPtr->errorCode);
    }
    if (jobPtr->errorInfo) {
        ns_free(jobPtr->errorInfo);
    }

    ns_free(jobPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * LookupQueue --
 *
 *      Find the specified queue and lock it if found.
 *      Specify "locked" true if the "queuelock" is already locked.
 *
 *      With the new locking scheme refCount is not longer necessary.
 *      However, if there is a case in the future where an unlocked
 *      queue can be referenced then we will again need the refCount.
 *
 * Results:
 *      Stanard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
LookupQueue(Tcl_Interp *interp, CONST char *queueId, Queue **queuePtr,
            int locked)
{
    Tcl_HashEntry *hPtr;

    if (!locked) {
        Ns_MutexLock(&tp.queuelock);
    }

    *queuePtr = NULL;

    hPtr = Tcl_FindHashEntry(&tp.queues, queueId);
    if (hPtr != NULL) {
        *queuePtr = Tcl_GetHashValue(hPtr);
        Ns_MutexLock(&(*queuePtr)->lock);
        ++(*queuePtr)->refCount;
    }

    if (!locked) {
        Ns_MutexUnlock(&tp.queuelock);
    }

    if (*queuePtr == NULL) {
        if (interp != NULL) {
            Tcl_AppendResult(interp, "no such queue: ", queueId, NULL);
        }
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ReleaseQueue --
 *
 *      Releases (unlocks) the queue, deleting it of no other thread
 *      is referencing it (queuePtr->refCount <= 0), queue is empty 
 *      and queue delete has been requested.
 * 
 *      Pass "locked" as true if the queuelock is already held locked.
 *
 * Results:
 *      1 if queue was deleted.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static int
ReleaseQueue(Queue *queuePtr, int locked)
{
    Tcl_HashSearch  search;
    int             deleted = 0;

    --queuePtr->refCount;

    /*
     * Delete the queue, honouring constraints
     */

    if (queuePtr->req == QUEUE_REQ_DELETE
        && queuePtr->refCount <= 0
        && (Tcl_FirstHashEntry(&queuePtr->jobs, &search) == NULL)) {
        Tcl_HashEntry *qPtr;

        if (!locked) {
            Ns_MutexLock(&tp.queuelock);
        }

        qPtr = Tcl_FindHashEntry(&tp.queues, queuePtr->name);
        if (qPtr != NULL) {
            Tcl_DeleteHashEntry(qPtr);
            tp.maxThreads -= queuePtr->maxThreads;
            deleted = 1;
        }

        Ns_MutexUnlock(&queuePtr->lock);
        FreeQueue(queuePtr);

        if (!locked) {
            Ns_MutexUnlock(&tp.queuelock);
        }
    } else {
        Ns_MutexUnlock(&queuePtr->lock);
    }

    return deleted;
}


/*
 *----------------------------------------------------------------------
 *
 * AnyDone --
 *
 *      Check if any jobs on the queue are "done".
 *
 * Results:
 *      True: there is at least one job done.
 *      False: there are no jobs done.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
AnyDone(Queue *queue)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;

    hPtr = Tcl_FirstHashEntry(&queue->jobs, &search);

    while (hPtr != NULL) {
	Job *jobPtr = Tcl_GetHashValue(hPtr);

        if (jobPtr->state == JOB_DONE) {
            return 1;
        }
        hPtr = Tcl_NextHashEntry(&search);
    }

    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * GetJobCodeStr --
 *
 *     Convert the job code into a string.
 *
 * Results:
 *     Standard Tcl result number.
 *
 * Side effects:
 *     None.
 *
 *----------------------------------------------------------------------
 */

static CONST char*
GetJobCodeStr(int code)
{
    static int max_code_index = 5;
    static CONST char *codeArr[] = {
        "TCL_OK",       /* 0 */
        "TCL_ERROR",    /* 1 */
        "TCL_RETURN",   /* 2 */
        "TCL_BREAK",    /* 3 */
        "TCL_CONTINUE", /* 4 */
        "UNKNOWN_CODE"  /* 5 */
    };

    /* Check the caller's input. */
    if (code > (max_code_index)) {
        code = max_code_index;
    }

    return codeArr[code];
}


/*
 *----------------------------------------------------------------------
 *
 * GetJobStateStr --
 *
 *      Convert the job states into a string.
 *
 * Results:
 *      The job state.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static CONST char*
GetJobStateStr(JobStates state)
{
    static int max_state_index = 3;
    static CONST char *stateArr[] = {
        "scheduled",        /* 0 */
        "running",          /* 1 */
        "done",             /* 2 */
        "unknown"           /* 3 */
    };

    if (state > (max_state_index)) {
        state = max_state_index;
    }

    return stateArr[state];
}


/*
 *----------------------------------------------------------------------
 *
 * GetJobTypeStr --
 *
 *      Convert the job states into a string.
 *
 * Results:
 *      The job type.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetJobTypeStr(JobTypes type)
{
    static int max_type_index = 2;
    static CONST char *typeArr[] = {
        "nondetached",     /* 0 */
        "detached",        /* 1 */
        "unknown"          /* 2 */
    };

    if (type > (max_type_index)) {
        type = max_type_index;
    }

    return typeArr[type];
}


/*
 *----------------------------------------------------------------------
 *
 * GetJobReqStr --
 *
 *      Convert the job req into a string.
 *
 * Results:
 *      The job status.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetJobReqStr(JobRequests req)
{
    static int req_max_index = 2;
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "wait",     /* 1 */
        "unknown"   /* 2 */
    };

    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 *
 * GetQueueReqStr --
 *
 *      Convert the queue req into a string.
 *
 * Results:
 *      The queue status
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetQueueReqStr(QueueRequests req)
{
    static int req_max_index = 1;
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "delete"    /* 1 */
    };

    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 *
 * GetTpReqStr --
 *
 *      Convert the thread pool req into a string.
 *
 * Results:
 *      The threadpool status.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetTpReqStr(ThreadPoolRequests req)
{
    static int req_max_index = 1;
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "stop"      /* 1 */
    };

    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 *
 * AppendField --
 *
 *      Append job field to the job field list.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
AppendField(Tcl_Interp *interp, Tcl_Obj *list, CONST char *name,
            CONST char *value)
{
    Tcl_Obj *elObj;

    /*
     * Note: If there is an error occurs within Tcl_ListObjAppendElement
     * it will set the result anyway.
     */

    elObj = Tcl_NewStringObj(name, (int)strlen(name));
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    elObj = Tcl_NewStringObj(value, (int)strlen(value));
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendFieldInt --
 *
 *      Append job field to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static int
AppendFieldInt(Tcl_Interp *interp, Tcl_Obj *list, CONST char *name, int value)
{
    Tcl_Obj *elObj;

    /*
     * Note: If there is an error occurs within Tcl_ListObjAppendElement
     * it will set the result anyway
     */

    elObj = Tcl_NewStringObj(name, (int)strlen(name));
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    elObj = Tcl_NewIntObj(value);
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendFieldLong --
 *
 *      Append job field to the job field list.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
AppendFieldLong(Tcl_Interp *interp, Tcl_Obj *list, CONST char *name,
                long value)
{
    Tcl_Obj *elObj;

    elObj = Tcl_NewStringObj(name, (int)strlen(name));
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    elObj = Tcl_NewLongObj(value);
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendFieldDouble --
 *
 *      Append the job field to the job field list.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int
AppendFieldDouble(Tcl_Interp *interp, Tcl_Obj *list, CONST char *name,
                  double value)
{
    Tcl_Obj *elObj;

    elObj = Tcl_NewStringObj(name, (int)strlen(name));
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    elObj = Tcl_NewDoubleObj(value);
    if (Tcl_ListObjAppendElement(interp, list, elObj) != TCL_OK) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ComputeDelta --
 *
 *      Compute the time difference   .
 *
 * Results:
 *      Difference in milliseconds
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static double
ComputeDelta(Ns_Time *start, Ns_Time *end)
{
    Ns_Time diff;

    Ns_DiffTime(end, start, &diff);

    return ((double)diff.sec * 1000.0) + ((double)diff.usec / 1000.0);
}

/*
 *----------------------------------------------------------------------
 *
 * SetupJobDefaults --
 *
 *      Assigns default configuration parameters if not set yet
 *
 * Results:
 *      None
 *
 * Side effects:
 *      jobsperthread and jobtimeout may be changed
 *
 *----------------------------------------------------------------------
 */
static void
SetupJobDefaults(void)
{
    if(tp.jobsPerThread == 0) {
       tp.jobsPerThread = nsconf.job.jobsperthread;
    }
    if (tp.timeout == 0) {
        tp.timeout = nsconf.job.timeout;
    }
}

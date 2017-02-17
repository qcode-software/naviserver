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
 * tclcache.c --
 *
 *      Tcl cache commands.
 */

#include "nsd.h"

/*
 * The following defines a Tcl cache.
 */

typedef struct TclCache {
    Ns_Cache   *cache;
    Ns_Time     timeout;  /* Default timeout for concurrent updates. */
    Ns_Time     expires;  /* Default time-to-live for cache entries. */
    size_t      maxEntry; /* Maximum size of a single entry in the cache. */
    size_t      maxSize;  /* Maximum size of the entire cache. */
} TclCache;


/*
 * Local functions defined in this file
 */

static int CacheAppendObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv, int append);

static Ns_Entry *CreateEntry(const NsInterp *itPtr, TclCache *cPtr, const char *key,
                             int *newPtr, Ns_Time *timeoutPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3) NS_GNUC_NONNULL(4);

static void SetEntry(TclCache *cPtr, Ns_Entry *entry, Tcl_Obj *valObj, Ns_Time *expPtr, int cost)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3);

static bool noGlobChars(const char *pattern) 
    NS_GNUC_NONNULL(1);

static Ns_ObjvProc ObjvCache;



/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheCreateObjCmd --
 *
 *      Create a new Tcl cache.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheCreateObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    char    *name = NULL;
    int      result = TCL_OK;
    long     maxSize = 0, maxEntry = 0;
    Ns_Time *timeoutPtr = NULL, *expPtr = NULL;

    Ns_ObjvSpec opts[] = {
        {"-timeout",  Ns_ObjvTime,  &timeoutPtr, NULL},
        {"-expires",  Ns_ObjvTime,  &expPtr,     NULL},
        {"-maxentry", Ns_ObjvLong,  &maxEntry,   NULL},
        {"--",        Ns_ObjvBreak, NULL,        NULL},
        {NULL, NULL,  NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache",   Ns_ObjvString, &name,    NULL},
        {"size",    Ns_ObjvLong,   &maxSize, NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;
        
    } else if (maxSize < 0 || maxEntry < 0) {
      Ns_TclPrintfResult(interp, "maxsize and maxentry must be positive numbers");
      result = TCL_ERROR;

    } else {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        Tcl_HashEntry  *hPtr;
        int             isNew;        

        Ns_MutexLock(&servPtr->tcl.cachelock);
        hPtr = Tcl_CreateHashEntry(&servPtr->tcl.caches, name, &isNew);
        if (isNew != 0) {
            TclCache *cPtr = ns_calloc(1u, sizeof(TclCache));

            cPtr->cache = Ns_CacheCreateSz(name, TCL_STRING_KEYS, (size_t)maxSize, ns_free);
            cPtr->maxEntry = (size_t)maxEntry;
            cPtr->maxSize  = (size_t)maxSize;
            if (timeoutPtr != NULL) {
                cPtr->timeout = *timeoutPtr;
            }
            if (expPtr != NULL) {
                cPtr->expires = *expPtr;
            }
            Tcl_SetHashValue(hPtr, cPtr);
        }
        Ns_MutexUnlock(&servPtr->tcl.cachelock);
        
        if (isNew == 0) {
            Ns_TclPrintfResult(interp, "duplicate cache name: %s", name);
            result = TCL_ERROR;
        }
    }
    
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheConfigureObjCmd --
 *
 *      Configure a Tcl cache. Usage:
 *         ns_cache_configure /cache/ ?-timeout T1? ?-expires T2? ?-maxentry E? ?-maxsize S? 
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheConfigureObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    int       result = TCL_OK, nargs = 0;
    long      maxSize = 0, maxEntry = 0;
    Ns_Time  *timeoutPtr = NULL, *expPtr = NULL;
    TclCache *cPtr = NULL;
    Ns_ObjvSpec opts[] = {
        {"-timeout",  Ns_ObjvTime,  &timeoutPtr, NULL},
        {"-expires",  Ns_ObjvTime,  &expPtr,     NULL},
        {"-maxentry", Ns_ObjvLong,  &maxEntry,   NULL},
        {"-maxsize",  Ns_ObjvLong,  &maxSize,    NULL},        
        {NULL, NULL,  NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache",     ObjvCache,     &cPtr,   clientData},
        {"?args",     Ns_ObjvArgs,   &nargs,  NULL},
        {NULL, NULL, NULL, NULL}
    };

    /*
     * We want to have to configure options after the cache name. So parse the
     * argument vector in two parts.
     */
    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else if (objc > 2 && Ns_ParseObjv(opts, NULL, interp, 2, objc, objv) != NS_OK) {
        result = TCL_ERROR;
        
    } else if (maxSize < 0) {
        Ns_TclPrintfResult(interp, "maxsize must be a positive number");
        result = TCL_ERROR;

    } else if (maxEntry < 0) {
        Ns_TclPrintfResult(interp, "maxEntry must be a positive number");
        result = TCL_ERROR;
        
    } else if (objc > 2) {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;

        Ns_MutexLock(&servPtr->tcl.cachelock);
        if (maxEntry > 0) {
            cPtr->maxEntry = (size_t)maxEntry;
        }
        if (maxSize > 0) {
            cPtr->maxSize = (size_t)maxSize;
        }
        if (timeoutPtr != NULL) {
            cPtr->timeout = *timeoutPtr;
        }
        if (expPtr != NULL) {
            cPtr->expires = *expPtr;
        }
        Ns_MutexUnlock(&servPtr->tcl.cachelock);

    } else if (objc == 2) {
        const NsInterp *itPtr = clientData;
        NsServer       *servPtr = itPtr->servPtr;
        Tcl_Obj        *resultObj = Tcl_NewListObj(0, NULL);
        Tcl_DString     ds;

        Tcl_DStringInit(&ds);
                
        Ns_MutexLock(&servPtr->tcl.cachelock);
        maxSize  = (long)cPtr->maxSize;
        maxEntry = (long)cPtr->maxEntry;
        Ns_MutexUnlock(&servPtr->tcl.cachelock);

        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("maxsize", 7));
        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewLongObj(maxSize));

        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("maxentry", 8));
        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewLongObj(maxEntry));
                
        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("expires", 7));
        if (cPtr->expires.sec != 0 || cPtr->expires.usec != 0) {
            Ns_DStringAppendTime(&ds, &cPtr->expires);
            Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj(ds.string, ds.length));
            Tcl_DStringTrunc(&ds, 0);
        } else {
            Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("", 0));
        }
        
        Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("timeout", 7));
        if (cPtr->timeout.sec != 0 || cPtr->timeout.usec != 0) {
            Ns_DStringAppendTime(&ds, &cPtr->timeout);
            Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj(ds.string, ds.length));
        } else {
            Tcl_ListObjAppendElement(interp, resultObj, Tcl_NewStringObj("", 0));
        }

        Tcl_DStringFree(&ds);
        Tcl_SetObjResult(interp, resultObj);
    }

    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheEvalObjCmd --
 *
 *      Get data from a cache by key.  If key is not present or data
 *      is stale, script is evaluated (with args appended, if present),
 *      and the result is stored in the cache and returned. Script
 *      errors are propagated.
 *
 *      The -force switch causes an existing valid entry to replaced.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      Other threads may block waiting for this update to complete.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheEvalObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const NsInterp *itPtr = clientData;
    TclCache        *cPtr = NULL;
    char            *key;
    Ns_Time         *timeoutPtr = NULL, *expPtr = NULL;
    int              nargs = 0, isNew, force = (int)NS_FALSE, status;

    Ns_ObjvSpec opts[] = {
        {"-timeout", Ns_ObjvTime,  &timeoutPtr, NULL},
        {"-expires", Ns_ObjvTime,  &expPtr,     NULL},
        {"-force",   Ns_ObjvBool,  &force,      INT2PTR(NS_TRUE)},
        {"--",       Ns_ObjvBreak, NULL,        NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache",    ObjvCache,     &cPtr,   clientData},
        {"key",      Ns_ObjvString, &key,    NULL},
        {"args",     Ns_ObjvArgs,   &nargs,  NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        status = TCL_ERROR;

    } else {
        Ns_Entry *entry = CreateEntry(itPtr, cPtr, key, &isNew, timeoutPtr);

        if (unlikely(entry == NULL)) {
            status = TCL_ERROR;
            
        } else if (likely(isNew == 0 && force == 0)) {
            Tcl_Obj *resultObj = Tcl_NewStringObj(Ns_CacheGetValue(entry),
                                                  (int)Ns_CacheGetSize(entry));
            Ns_CacheUnlock(cPtr->cache);
            Tcl_SetObjResult(interp, resultObj);
            status = TCL_OK;

        } else {
            Ns_Time start, end, diff;
            
            Ns_CacheUnlock(cPtr->cache);
            Ns_GetTime(&start);
            
            if (nargs == 1) {
                status = Tcl_EvalObjEx(interp, objv[objc-1], 0);
            } else {
                status = Tcl_EvalObjv(interp, nargs, objv + (objc-nargs), 0);
            }
            Ns_GetTime(&end);
            (void)Ns_DiffTime(&end, &start, &diff);
            
            if (status != TCL_OK && status != TCL_RETURN) {

                /* 
                 * Don't cache anything, if the status code is not TCL_OK
                 * or TCL_RETURN.
                 * 
                 * The remaining status codes are TCL_BREAK, TCL_CONTINUE
                 * and TCL_ERROR. Regarding TCL_BREAK and TCL_CONTINUE as
                 * signals for not cacheing is used e.g. in
                 * OpenACS. Therefore we want to return TCL_BREAK or
                 * TCL_CONTINUE as well.
                 *
                 * Certainly, we could map unknown error codes to TCL_ERROR
                 * as it was done in earlier versions of NaviServer.
                 *
                 * if (status != TCL_BREAK && status != TCL_CONTINUE) {
                 *     status = TCL_ERROR;
                 * }
                 */
                Ns_CacheLock(cPtr->cache);
                Ns_CacheDeleteEntry(entry);
            } else {
                Tcl_Obj *resultObj = Tcl_GetObjResult(interp);
                
                status = TCL_OK;
                Ns_CacheLock(cPtr->cache);
                SetEntry(cPtr, entry, resultObj, expPtr, 
                         (int)(diff.sec * 1000000 + diff.usec));
            }
            Ns_CacheBroadcast(cPtr->cache);
            Ns_CacheUnlock(cPtr->cache);
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheIncrObjCmd --
 *
 *      Treat the value of the cached object as in integer and
 *      increment it.  New values start at zero.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheIncrObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    const NsInterp *itPtr = clientData;
    TclCache       *cPtr;
    char           *key;
    int             isNew, incr = 1, result;
    Ns_Time        *timeoutPtr = NULL, *expPtr = NULL;

    Ns_ObjvSpec opts[] = {
        {"-timeout", Ns_ObjvTime,  &timeoutPtr, NULL},
        {"-expires", Ns_ObjvTime,  &expPtr,     NULL},
        {"--",       Ns_ObjvBreak, NULL,        NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache",    ObjvCache,     &cPtr,  clientData},
        {"key",      Ns_ObjvString, &key,   NULL},
        {"?incr",    Ns_ObjvInt,    &incr,  NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        Ns_Entry   *entry = CreateEntry(itPtr, cPtr, key, &isNew, timeoutPtr);
        int         cur = 0;
        
        if (entry == NULL) {
            result = TCL_ERROR;
        } else if ((isNew == 0)
                   && (Tcl_GetInt(interp, Ns_CacheGetValue(entry), &cur) != TCL_OK)) {
            Ns_CacheUnlock(cPtr->cache);
            result = TCL_ERROR;
        } else {
            Tcl_Obj    *valObj = Tcl_NewIntObj(cur + incr);
            
            SetEntry(cPtr, entry, valObj, expPtr, 0);
            Tcl_SetObjResult(interp, valObj);
            Ns_CacheUnlock(cPtr->cache);
            result = TCL_OK;
        }
    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheAppendObjCmd, NsTclCacheLappendObjCmd --
 *
 *      Append one or more elements to cached value.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheAppendObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    return CacheAppendObjCmd(clientData, interp, objc, objv, 1);
}

int
NsTclCacheLappendObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    return CacheAppendObjCmd(clientData, interp, objc, objv, 0);
}

static int
CacheAppendObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv, int append)
{
    const NsInterp *itPtr = clientData;
    TclCache       *cPtr = NULL;
    char           *key = NULL;
    int             result = TCL_OK, nelements = 0;
    Ns_Time        *timeoutPtr = NULL, *expPtr = NULL;

    Ns_ObjvSpec opts[] = {
        {"-timeout", Ns_ObjvTime, &timeoutPtr, NULL},
        {"-expires", Ns_ObjvTime, &expPtr,     NULL},
        {"--",       Ns_ObjvBreak, NULL,       NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache", ObjvCache,     &cPtr,      clientData},
        {"key",   Ns_ObjvString, &key,       NULL},
        {"args",  Ns_ObjvArgs,   &nelements, NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else {
        int         isNew;
        Ns_Entry   *entry = CreateEntry(itPtr, cPtr, key, &isNew, timeoutPtr);
            
        if (entry == NULL) {
            result = TCL_ERROR;
        } else {
            Tcl_Obj  *valObj = Tcl_NewObj();
            int       i;
            
            if (isNew == 0) {
                Tcl_SetStringObj(valObj, Ns_CacheGetValue(entry), 
                                 (int)Ns_CacheGetSize(entry));
            }
            for (i = objc - nelements; i < objc; i++) {
                if (append != 0) {
                    Tcl_AppendObjToObj(valObj, objv[i]);
                } else if (Tcl_ListObjAppendElement(interp, valObj, objv[i]) != TCL_OK) {
                    result = TCL_ERROR;
                    break;
                }
            }
            if (result == TCL_OK) {
                SetEntry(cPtr, entry, valObj, expPtr, 0);
                Tcl_SetObjResult(interp, valObj);
            }
            Ns_CacheUnlock(cPtr->cache);
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheNamesObjCmd --
 *
 *      Return a list of Tcl cache names for the current server.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheNamesObjCmd(ClientData clientData, Tcl_Interp *interp, int UNUSED(objc), Tcl_Obj *CONST* UNUSED(objv))
{
    const NsInterp      *itPtr = clientData;
    NsServer            *servPtr = itPtr->servPtr;
    const Tcl_HashEntry *hPtr;
    Tcl_HashSearch       search;
    Tcl_Obj             *listObj = Tcl_NewListObj(0, NULL);

    Ns_MutexLock(&servPtr->tcl.cachelock);
    for (hPtr = Tcl_FirstHashEntry(&servPtr->tcl.caches, &search);
         hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&search)
         ) {
        const char *key = Tcl_GetHashKey(&servPtr->tcl.caches, hPtr);
        Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(key, -1));
    }
    Ns_MutexUnlock(&servPtr->tcl.cachelock);

    Tcl_SetObjResult(interp, listObj);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheKeysObjCmd --
 *
 *      Get a list of all valid keys in a cache, or only those matching
 *      pattern, if given.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static bool
noGlobChars(const char *pattern) 
{
    register char c;
    const char   *p = pattern;
    bool          result = NS_TRUE;

    NS_NONNULL_ASSERT(pattern != NULL);

    for (c = *p; likely(c != '\0'); c = *++p) {
	if (unlikely(c == '*') || unlikely(c == '?') || unlikely(c == '[')) {
            result = NS_FALSE;
            break;
        }
    }
    return result;
}


int
NsTclCacheKeysObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    TclCache       *cPtr = NULL;
    const Ns_Entry *entry;
    char           *pattern = NULL;
    int             exact = (int)NS_FALSE, result = TCL_OK;

    Ns_ObjvSpec opts[] = {
        {"-exact",   Ns_ObjvBool,  &exact,     INT2PTR(NS_TRUE)},
        {"--",       Ns_ObjvBreak, NULL,       NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache",    ObjvCache,     &cPtr,    clientData},
        {"?pattern", Ns_ObjvString, &pattern, NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else if (pattern != NULL && (exact != 0 || noGlobChars(pattern))) {
        Tcl_Obj  *listObj = Tcl_NewListObj(0, NULL);

        /*
         * If the provided pattern (key) contains no glob characters,
         * there will be zero or one entry for the given key. In such
         * cases, or when the option "-exact" is specified, a single hash
         * lookup is sufficient.
         */
        assert(cPtr != NULL);
        Ns_CacheLock(cPtr->cache);
        entry = Ns_CacheFindEntry(cPtr->cache, pattern);
        if (entry != NULL && Ns_CacheGetValue(entry) != NULL) {
            Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(pattern, -1));
        }
        Ns_CacheUnlock(cPtr->cache);
        Tcl_SetObjResult(interp, listObj);
        
    } else {
        Ns_CacheSearch  search;
        Tcl_Obj        *listObj = Tcl_NewListObj(0, NULL);
        
        /*
         * We have either no pattern or the pattern contains meta
         * characters. We need to iterate over all entries, which can
         * take a while for large caches.
         */
        assert(cPtr != NULL);
        Ns_CacheLock(cPtr->cache);
        entry = Ns_CacheFirstEntry(cPtr->cache, &search);
        while (entry != NULL) {
            const char *key = Ns_CacheKey(entry);

            if (pattern == NULL || Tcl_StringMatch(key, pattern) == 1) {
                Tcl_ListObjAppendElement(interp, listObj, Tcl_NewStringObj(key, -1));
            }
            entry = Ns_CacheNextEntry(&search);
        }
        Ns_CacheUnlock(cPtr->cache);
        Tcl_SetObjResult(interp, listObj);
    }
    
    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheFlushObjCmd --
 *
 *      Flush all entries from a cache, or the entries identified
 *      by the given keys.  Return the number of entries flushed.
 *      NB: Concurrent updates are skipped.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheFlushObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    TclCache     *cPtr = NULL;
    int           glob = (int)NS_FALSE, npatterns = 0, result = TCL_OK;
    Ns_ObjvSpec   opts[] = {
        {"-glob",    Ns_ObjvBool,  &glob, INT2PTR(NS_TRUE)},
        {"--",       Ns_ObjvBreak, NULL,  NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec   args[] = {
        {"cache",    ObjvCache,    &cPtr,      clientData},
        {"?args",    Ns_ObjvArgs,  &npatterns, NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;
    } else {
        Ns_Entry  *entry;
        int        nflushed = 0, i;
        Ns_Cache  *cache;

        assert(cPtr != NULL);
        cache = cPtr->cache;

        Ns_CacheLock(cache);
        if (npatterns == 0) {
            nflushed = Ns_CacheFlush(cache);

        } else if (glob == 0) {
            for (i = npatterns; i > 0; i--) {
                entry = Ns_CacheFindEntry(cache, Tcl_GetString(objv[objc-i]));
                if (entry != NULL && Ns_CacheGetValue(entry) != NULL) {
                    Ns_CacheFlushEntry(entry);
                    nflushed++;
                }
            }
            
        } else {
            Ns_CacheSearch  search;
                
            entry = Ns_CacheFirstEntry(cache, &search);
            while (entry != NULL) {
                const char *key = Ns_CacheKey(entry);

                for (i = npatterns; i > 0; i--) {
                    const char *pattern = Tcl_GetString(objv[objc-i]);

                    if (Tcl_StringMatch(key, pattern) == 1) {
                        Ns_CacheFlushEntry(entry);
                        nflushed++;
                        break;
                    }
                }
                entry = Ns_CacheNextEntry(&search);
            }
        }
        Ns_CacheUnlock(cache);
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nflushed));
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheGetObjCmd --
 *
 *      Return an entry from the cache. This function behaves similar to
 *      nsv_get; if the optional varname is passed, it returns on the Tcl
 *      level 0 or 1 depending on succes and bind the variable on success. If
 *      no varName is provided, it returns the value or an error.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
int
NsTclCacheGetObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    TclCache   *cPtr = NULL;
    char       *key;
    int         result = TCL_OK;
    Tcl_Obj    *varNameObj = NULL;
    Ns_ObjvSpec args[] = {
        {"cache",    ObjvCache,     &cPtr,        clientData},
        {"key",      Ns_ObjvString, &key,         NULL},
        {"?varName", Ns_ObjvObj,    &varNameObj,  NULL},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else {
        const Ns_Entry *entry;
        Tcl_Obj        *resultObj;
        
        assert(cPtr != NULL);

        Ns_CacheLock(cPtr->cache);
        entry = Ns_CacheFindEntry(cPtr->cache, key);
        resultObj = (entry != NULL) ? Tcl_NewStringObj(Ns_CacheGetValue(entry), -1) : NULL;
        Ns_CacheUnlock(cPtr->cache);

        if (unlikely(varNameObj != NULL)) {
            Tcl_SetObjResult(interp, Tcl_NewBooleanObj(resultObj != NULL));
            if (likely(resultObj != NULL)) {
                if (unlikely(Tcl_ObjSetVar2(interp, varNameObj, NULL, resultObj, TCL_LEAVE_ERR_MSG) == NULL)) {
                    result = TCL_ERROR;
                }
            }
        } else {
            if (likely(resultObj != NULL)) {
                Tcl_SetObjResult(interp, resultObj);
            } else {
                Ns_TclPrintfResult(interp, "no such key: %s", key);
                result = TCL_ERROR;
            }
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheStatsObjCmd --
 *
 *      Returns stats on a cache. The size and expirey time of each
 *      entry in the cache is also appended if the -contents switch
 *      is given.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheStatsObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST* objv)
{
    TclCache   *cPtr = NULL;
    int         contents = (int)NS_FALSE, reset = (int)NS_FALSE, result = TCL_OK;
    Ns_ObjvSpec opts[] = {
        {"-contents", Ns_ObjvBool,  &contents, INT2PTR(NS_TRUE)},
        {"-reset",    Ns_ObjvBool,  &reset,    INT2PTR(NS_TRUE)},
        {"--",        Ns_ObjvBreak, NULL,      NULL},
        {NULL, NULL, NULL, NULL}
    };
    Ns_ObjvSpec args[] = {
        {"cache", ObjvCache, &cPtr, clientData},
        {NULL, NULL, NULL, NULL}
    };
    args[0].arg = clientData; /* pass non-constant clientData for "cache" */

    if (Ns_ParseObjv(opts, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;
        
    } else {
        Ns_DString      ds;
        Ns_Cache       *cache;

        assert(cPtr != NULL);
        
        cache = cPtr->cache;
        Ns_DStringInit(&ds);

        Ns_CacheLock(cache);
        if (contents != 0) {
            Ns_CacheSearch  search;
            const Ns_Entry *entry;

            Tcl_DStringStartSublist(&ds);
            entry = Ns_CacheFirstEntry(cache, &search);
            while (entry != NULL) {
                size_t         size    = Ns_CacheGetSize(entry);
                const Ns_Time *timePtr = Ns_CacheGetExpirey(entry);

                if (timePtr->usec == 0) {
                    Ns_DStringPrintf(&ds, "%" PRIdz " %" PRIu64 " ",
                                     size, (int64_t) timePtr->sec);
                } else {
                    Ns_DStringPrintf(&ds, "%" PRIdz " %" PRIu64 ":%ld ",
                                     size, (int64_t) timePtr->sec, timePtr->usec);
                }
                entry = Ns_CacheNextEntry(&search);
            }
            Tcl_DStringEndSublist(&ds);
        } else {
            (void) Ns_CacheStats(cache, &ds);
        }
        if (reset != 0) {
            Ns_CacheResetStats(cache);
        }
        Ns_CacheUnlock(cache);

        Tcl_DStringResult(interp, &ds);
    }
    
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateEntry --
 *
 *      Lock the cache and create a new entry or return existing entry,
 *      waiting up to timeout seconds for another thread to complete
 *      an update.
 *
 * Results:
 *      Pointer to entry, or NULL on timeout.
 *
 * Side effects:
 *      Cache will be left locked if function returns non-NULL entry.
 *
 *----------------------------------------------------------------------
 */

static Ns_Entry *
CreateEntry(const NsInterp *itPtr, TclCache *cPtr, const char *key, int *newPtr,
            Ns_Time *timeoutPtr)
{
    Ns_Cache *cache;
    Ns_Entry *entry;
    Ns_Time   t;

    NS_NONNULL_ASSERT(itPtr != NULL);
    NS_NONNULL_ASSERT(cPtr != NULL);
    NS_NONNULL_ASSERT(key != NULL);
    NS_NONNULL_ASSERT(newPtr != NULL);

    cache = cPtr->cache;

    if (timeoutPtr == NULL
        && (cPtr->timeout.sec > 0 || cPtr->timeout.usec > 0)) {
        timeoutPtr = Ns_AbsoluteTime(&t, &cPtr->timeout);
    } else {
        timeoutPtr = Ns_AbsoluteTime(&t, timeoutPtr);
    }
    Ns_CacheLock(cache);
    entry = Ns_CacheWaitCreateEntry(cache, key, newPtr, timeoutPtr);
    if (entry == NULL) {
        Ns_CacheUnlock(cache);
        Tcl_SetErrorCode(itPtr->interp, "NS_TIMEOUT", (char *)0L);
        Ns_TclPrintfResult(itPtr->interp, "timeout waiting for concurrent update: %s", key);
    }
    return entry;
}


/*
 *----------------------------------------------------------------------
 *
 * SetEntry --
 *
 *      Set the value of the cache entry if not above max entry size.
 *
 * Results:
 *      1 if entry set, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
SetEntry(TclCache *cPtr, Ns_Entry *entry, Tcl_Obj *valObj, Ns_Time *expPtr, int cost)
{
    const char *bytes;
    int         len;
    size_t      length;
    Ns_Time     t;

    NS_NONNULL_ASSERT(cPtr != NULL);
    NS_NONNULL_ASSERT(entry != NULL);
    NS_NONNULL_ASSERT(valObj != NULL);

    bytes = Tcl_GetStringFromObj(valObj, &len);
    assert(len >= 0);
    length = (size_t)len;

    if (cPtr->maxEntry > 0u && length > cPtr->maxEntry) {
        Ns_CacheDeleteEntry(entry);
    } else {
        char *value = ns_malloc(length + 1u);

        memcpy(value, bytes, length);
        value[length] = '\0';
        if (expPtr == NULL
            && (cPtr->expires.sec > 0 || cPtr->expires.usec > 0)) {
            expPtr = Ns_AbsoluteTime(&t, &cPtr->expires);
        } else {
            expPtr = Ns_AbsoluteTime(&t, expPtr);
        }
        Ns_CacheSetValueExpires(entry, value, length, expPtr, cost, cPtr->maxSize);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ObjvCache --
 *
 *      Get a cache by name.
 *
 * Results:
 *      TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *      Tcl obj will be converted to ns:cache type.
 *
 *----------------------------------------------------------------------
 */

static int
ObjvCache(Ns_ObjvSpec *spec, Tcl_Interp *interp, int *objcPtr,
          Tcl_Obj *CONST* objv)
{
    int                 result = TCL_OK;
    TclCache          **cPtrPtr = spec->dest;

    if (unlikely(*objcPtr < 1)) {
        result = TCL_ERROR;

    } else {
        static const char  *const cacheType = "ns:cache";
        const NsInterp     *itPtr = spec->arg;
        Tcl_Obj            *cacheNameObj = objv[0];
        const char         *cacheName = Tcl_GetString(cacheNameObj);

        if (unlikely(Ns_TclGetOpaqueFromObj(cacheNameObj, cacheType, (void **)cPtrPtr) != TCL_OK)) {
            /*
             * Ns_TclGetOpaqueFromObj failed, try to add the Tcl_Objobject to
             * tcl.caches, when we have an interpreter with an attached
             * server.
             */
            if ((itPtr != NULL) && (itPtr->servPtr != NULL)) {
                NsServer            *servPtr = itPtr->servPtr;
                const Tcl_HashEntry *hPtr;

                Ns_MutexLock(&servPtr->tcl.cachelock);
                hPtr = Tcl_FindHashEntry(&servPtr->tcl.caches, (const void *)cacheName);
                Ns_MutexUnlock(&servPtr->tcl.cachelock);
                if (hPtr == NULL) {
                    Ns_TclPrintfResult(interp, "no such cache: %s", cacheName);
                    result = TCL_ERROR;
                } else {
                    *cPtrPtr = Tcl_GetHashValue(hPtr);
                    Ns_TclSetOpaqueObj(cacheNameObj, cacheType, *cPtrPtr);
                }
            } else {
                /*
                 * No server, we can't store the entry in the cache
                 */
                Ns_TclPrintfResult(interp, "no server for cache %s", cacheName);
                result = TCL_ERROR;
            }
        } else {
            /*
             * Common case: we could fetch the cache pointer from the Tcl_Obj.
             */
        }
        
        if (result == TCL_OK) {
            *objcPtr -= 1;
        }
    }

    return result;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

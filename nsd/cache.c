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
 * cache.c --
 *
 *      Size and time limited caches.
 */

#include "nsd.h"

struct Cache;

/*
 * An Entry is a node in a linked list as well as being a
 * hash table entry. The linked list is there to keep track of
 * usage for the purposes of cache pruning.
 */

typedef struct Entry {
    struct Entry   *nextPtr;
    struct Entry   *prevPtr;
    struct Cache   *cachePtr;
    Tcl_HashEntry  *hPtr;
    Ns_Time         expires;  /* Absolute ttl timeout. */
    size_t          size;
    int		    cost;     /* cost to compute a single entry */
    int		    count;    /* reuse of this entry */
    void           *value;    /* Will appear NULL for concurrent updates. */
} Entry;

/*
 * The following structure defines a cache
 */

typedef struct Cache {
    Entry         *firstEntryPtr;
    Entry         *lastEntryPtr;
    Tcl_HashEntry *hPtr;
    int            keys;
    size_t         maxSize;
    size_t         currentSize;
    Ns_Callback   *freeProc;
    Ns_Mutex       lock;
    Ns_Cond        cond;
    Tcl_HashTable  entriesTable;

    struct {
        unsigned long   nhit;      /* Successful gets. */
        unsigned long   nmiss;     /* Unsuccessful gets. */
        unsigned long   nexpired;  /* Unsuccessful gets due to entry expiry. */
        unsigned long   nflushed;  /* Explicit flushes by user code. */
        unsigned long   npruned;   /* Evictions due to size constraint. */
    } stats;

    char name[1];

} Cache;


/*
 * Local functions defined in this file
 */

static int  Expired(Entry *ePtr, Ns_Time *nowPtr);
static void Delink(Entry *ePtr);
static void Push(Entry *ePtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheCreateSz --
 *
 *      Create a new size limited cache.
 *
 * Results:
 *      A pointer to the new cache.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

Ns_Cache *
Ns_CacheCreateSz(CONST char *name, int keys, size_t maxSize, Ns_Callback *freeProc)
{
    Cache *cachePtr;

    cachePtr = ns_calloc(1, sizeof(Cache) + strlen(name));
    strcpy(cachePtr->name, name);
    cachePtr->freeProc       = freeProc;
    cachePtr->maxSize        = maxSize;
    cachePtr->currentSize    = 0;
    cachePtr->keys           = keys;
    cachePtr->stats.nhit     = 0;
    cachePtr->stats.nmiss    = 0;
    cachePtr->stats.nexpired = 0;
    cachePtr->stats.nflushed = 0;
    cachePtr->stats.npruned  = 0;

    Ns_MutexSetName2(&cachePtr->lock, "ns:cache", name);
    Tcl_InitHashTable(&cachePtr->entriesTable, keys);

    return (Ns_Cache *) cachePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheDestroy
 *
 *      Flush all entries and delete a cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Statistics logged, cache no longer usable.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheDestroy(Ns_Cache *cache)
{
    Cache      *cachePtr = (Cache *) cache;

    Ns_CacheFlush(cache);
    Ns_MutexDestroy(&cachePtr->lock);
    Ns_CondDestroy(&cachePtr->cond);
    Tcl_DeleteHashTable(&cachePtr->entriesTable);
    ns_free(cachePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFindEntry --
 *
 *      Find a cache entry given it's key.
 *
 * Results:
 *      A pointer to an Ns_Entry cache entry, or NULL if the key does
 *      not exist or the entry has expired.
 *
 * Side effects:
 *      A valid entry will move to the top of the LRU list.
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheFindEntry(Ns_Cache *cache, CONST char *key)
{
    Cache         *cachePtr = (Cache *) cache;
    Tcl_HashEntry *hPtr;
    Entry         *ePtr;

    hPtr = Tcl_FindHashEntry(&cachePtr->entriesTable, key);
    if (hPtr == NULL) {
        /*
         * Entry does not exist at all.
         */
        ++cachePtr->stats.nmiss;
        return NULL;
    }
    ePtr = Tcl_GetHashValue(hPtr);
    if (ePtr->value == NULL) {
        /*
         * Entry is being updated by some other thread.
         */
        ++cachePtr->stats.nmiss;
        return NULL;
    }
    if (Expired(ePtr, NULL)) {
        /*
         * Entry exists but has expired.
         */
        Ns_CacheDeleteEntry((Ns_Entry *) ePtr);
        ++cachePtr->stats.nmiss;
        return NULL;
    }
    ++cachePtr->stats.nhit;
    Delink(ePtr);
    ePtr->count ++;
    Push(ePtr);

    return (Ns_Entry *) ePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheCreateEntry --
 *
 *      Create a new cache entry or return an existing one with the
 *      given key.
 *
 * Results:
 *      A pointer to a cache entry.
 *
 * Side effects:
 *      Existing, expired entries will be unset and reported as new.
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheCreateEntry(Ns_Cache *cache, CONST char *key, int *newPtr)
{
    Cache         *cachePtr = (Cache *) cache;
    Tcl_HashEntry *hPtr;
    Entry         *ePtr;
    int            isNew;

    hPtr = Tcl_CreateHashEntry(&cachePtr->entriesTable, key, &isNew);
    if (isNew) {
        ePtr = ns_calloc(1, sizeof(Entry));
        ePtr->hPtr = hPtr;
        ePtr->cachePtr = cachePtr;
        Tcl_SetHashValue(hPtr, ePtr);
        ++cachePtr->stats.nmiss;
    } else {
        ePtr = Tcl_GetHashValue(hPtr);
        if (Expired(ePtr, NULL)) {
            ++cachePtr->stats.nexpired;
            Ns_CacheUnsetValue((Ns_Entry *) ePtr);
            isNew = 1;
        } else {
	    ePtr->count ++;
            ++cachePtr->stats.nhit;
        }
        Delink(ePtr);
    }
    Push(ePtr);
    *newPtr = isNew;

    return (Ns_Entry *) ePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheWaitCreateEntry --
 *
 *      Create a new cache entry or return an existing one with the
 *      given key.  Wait until the given absolute timeout for another
 *      thread to complete an update.
 *
 * Results:
 *      A pointer to a new cache entry, or NULL on timeout.
 *
 * Side effects:
 *      Cache lock may be released and re-acquired.
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheWaitCreateEntry(Ns_Cache *cache, CONST char *key, int *newPtr,
                        Ns_Time *timeoutPtr)
{
    Ns_Entry      *entry;
    int            isNew, status = NS_OK;

    entry = Ns_CacheCreateEntry(cache, key, &isNew);
    if (!isNew && Ns_CacheGetValue(entry) == NULL) {
        do {
            status = Ns_CacheTimedWait(cache, timeoutPtr);
            entry = Ns_CacheCreateEntry(cache, key, &isNew);
        } while (status == NS_OK
                 && !isNew
                 && Ns_CacheGetValue(entry) == NULL);
    }
    *newPtr = isNew;

    return status == NS_OK ? entry : NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheKey --
 *
 *      Gets the key of a cache entry.
 *
 * Results:
 *      A pointer to the key for the given entry.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_CacheKey(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    return Tcl_GetHashKey(&ePtr->cachePtr->entriesTable, ePtr->hPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheGetValue, Ns_CacheGetSize, Ns_CacheGetExpirey --
 *
 *      Get the value, size or expirey of a cache entry.
 *
 * Results:
 *      As specified.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_CacheGetValue(Ns_Entry *entry)
{
    return ((Entry *) entry)->value;
}

size_t
Ns_CacheGetSize(Ns_Entry *entry)
{
    return ((Entry *) entry)->size;
}

Ns_Time *
Ns_CacheGetExpirey(Ns_Entry *entry)
{
    return &((Entry *) entry)->expires;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheSetValue, Ns_CacheSetValueSz, Ns_CacheSetValueExpires --
 *
 *      Free the cache entry's previous contents, set it to the new
 *      contents, increase the size of the cache, and prune until
 *      it's back under the maximum size.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Cache pruning and freeing of old contents may occur.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheSetValue(Ns_Entry *entry, void *value)
{
    Ns_CacheSetValueSz(entry, value, 0);
}

void
Ns_CacheSetValueSz(Ns_Entry *entry, void *value, size_t size)
{
  Ns_CacheSetValueExpires(entry, value, size, NULL, 0);
}

void
Ns_CacheSetValueExpires(Ns_Entry *entry, void *value, size_t size,
                        Ns_Time *timeoutPtr, int cost)
{
    Entry *ePtr = (Entry *) entry;
    Cache *cachePtr = ePtr->cachePtr;

    Ns_CacheUnsetValue(entry);
    ePtr->value = value;
    ePtr->size = size;
    ePtr->cost = cost;
    ePtr->count = 1;

    if (timeoutPtr != NULL) {
        ePtr->expires = *timeoutPtr;
    }
    cachePtr->currentSize += size;
    if (ePtr->cachePtr->maxSize > 0) {
        /* 
	 * Make space for the new entry, but don't delete the current
	 * entry, and don't delete other newborn entries (with a value
	 * of NULL) of some other threads which are concurrently
	 * created.  There might be concurrent updates, since
	 * e.g. nscache_eval releases its mutex.
	 */
        while (cachePtr->currentSize > cachePtr->maxSize &&
               cachePtr->lastEntryPtr != ePtr &&
               cachePtr->lastEntryPtr->value != NULL) {
            Ns_CacheDeleteEntry((Ns_Entry *) cachePtr->lastEntryPtr);
            ++cachePtr->stats.npruned;
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheUnsetValue --
 *
 *      Reset the value of an entry to NULL, calling the free proc for
 *      any previous entry and updating the cache size.
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
Ns_CacheUnsetValue(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;
    Cache *cachePtr;

    if (ePtr->value != NULL) {
        cachePtr = ePtr->cachePtr;
        cachePtr->currentSize -= ePtr->size;
        if (cachePtr->freeProc != NULL) {
            (*cachePtr->freeProc)(ePtr->value);
        }
        ePtr->size = 0;
        ePtr->value = NULL;
        ePtr->expires.sec = ePtr->expires.usec = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFlushEntry, Ns_CacheDeleteEntry --
 *
 *      Delete an entry from the cache table after first unsetting
 *      the current entry value (if any).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Statistics updated on flush.
 *      NB: Entries under concurrent update are also deleted.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheFlushEntry(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    ePtr->cachePtr->stats.nflushed++;
    Ns_CacheDeleteEntry(entry);
}

void
Ns_CacheDeleteEntry(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    Ns_CacheUnsetValue(entry);
    Delink(ePtr);
    Tcl_DeleteHashEntry(ePtr->hPtr);
    ns_free(ePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFlush --
 *
 *      Flush every entry from a cache.
 *
 * Results:
 *      Number of entries flushed.
 *
 * Side effects:
 *      Cache stats updated, concurrent updates skipped.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CacheFlush(Ns_Cache *cache)
{
    Cache          *cachePtr = (Cache *) cache;
    Ns_CacheSearch  search;
    Ns_Entry       *entry;
    int             nflushed = 0;

    entry = Ns_CacheFirstEntry(cache, &search);
    while (entry != NULL) {
        Ns_CacheDeleteEntry(entry);
        entry = Ns_CacheNextEntry(&search);
        nflushed++;
    }
    ++cachePtr->stats.nflushed;

    return nflushed;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFirstEntry --
 *
 *      Return a pointer to the first valid entry in the cache (in no
 *      particular order).
 *
 * Results:
 *      A pointer to said entry, or NULL if no valid entries.
 *
 * Side effects:
 *      Expired entries are flushed, concurrent updates are skipped.
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheFirstEntry(Ns_Cache *cache, Ns_CacheSearch *search)
{
    Cache          *cachePtr = (Cache *) cache;
    Tcl_HashEntry  *hPtr;
    Ns_Entry       *entry;

    Ns_GetTime(&search->now);
    hPtr = Tcl_FirstHashEntry(&cachePtr->entriesTable, &search->hsearch);
    while (hPtr != NULL) {
        entry = Tcl_GetHashValue(hPtr);
        if (Ns_CacheGetValue(entry) != NULL) {
            if (!Expired((Entry *) entry, &search->now)) {
                return entry;
            }
            ++cachePtr->stats.nexpired;
            Ns_CacheDeleteEntry(entry);
        }
        hPtr = Tcl_NextHashEntry(&search->hsearch);
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheNextEntry --
 *
 *      When used in conjunction with Ns_CacheFirstEntry, one may
 *      walk through the whole cache.
 *
 * Results:
 *      Pointer to next valid entry, or NULL when all entries visited.
 *
 * Side effects:
 *      Expired entries are flushed, concurrent updates skipped.
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheNextEntry(Ns_CacheSearch *search)
{
    Tcl_HashEntry  *hPtr;
    Ns_Entry       *entry;

    hPtr = Tcl_NextHashEntry(&search->hsearch);
    while (hPtr != NULL) {
        entry = Tcl_GetHashValue(hPtr);
        if (Ns_CacheGetValue(entry) != NULL) {
            if (!Expired((Entry *) entry, &search->now)) {
                return entry;
            }
            ((Entry *) entry)->cachePtr->stats.nexpired++;
            Ns_CacheDeleteEntry(entry);
        }
        hPtr = Tcl_NextHashEntry(&search->hsearch);
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheLock --
 *
 *      Lock the cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Mutex locked.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheLock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_MutexLock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheTryLock --
 *
 *      Try to lock the cache.
 *
 * Results:
 *      NS_OK if successfully locked, NS_TIMEOUT if already locked.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CacheTryLock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    return Ns_MutexTryLock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheUnlock --
 *
 *      Unlock the cache.
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
Ns_CacheUnlock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_MutexUnlock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheWait, Ns_CacheTimedWait --
 *
 *      Wait for the cache's condition variable to be signaled or for
 *      the given absolute timeout if timePtr is not NULL.
 *
 * Results:
 *      NS_OK or NS_TIMEOUT if timeout specified.
 *
 * Side effects:
 *      Thread is suspended until condition is signaled or timeout.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheWait(Ns_Cache *cache)
{
    Ns_CacheTimedWait(cache, NULL);
}

int
Ns_CacheTimedWait(Ns_Cache *cache, Ns_Time *timePtr)
{
    Cache *cachePtr = (Cache *) cache;

    return Ns_CondTimedWait(&cachePtr->cond, &cachePtr->lock, timePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheSignal --
 *
 *      Signal the cache's condition variable, waking the first waiting
 *      thread (if any).
 *
 *      NOTE:  Be sure you don't really want to wake all threads with
 *      Ns_CacheBroadcast.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A single thread may resume.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheSignal(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_CondSignal(&cachePtr->cond);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheBroadcast --
 *
 *      Broadcast the cache's condition variable, waking all waiting
 *      threads (if any).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Waiting threads may resume.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheBroadcast(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_CondBroadcast(&cachePtr->cond);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheStats --
 *
 *      Append statistics about cache usage to dstring.
 *
 * Results:
 *      Pointer to current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_CacheStats(Ns_Cache *cache, Ns_DString *dest)
{
    Cache          *cachePtr = (Cache *)cache;
    unsigned long   total, hitrate;
    Entry          *ePtr;
    Ns_CacheSearch  search;
    double          savedCost = 0.0;

    total = cachePtr->stats.nhit + cachePtr->stats.nmiss;
    hitrate = (total ? (cachePtr->stats.nhit * 100) / total : 0);

    ePtr = (Entry *)Ns_CacheFirstEntry(cache, &search);
    while (ePtr != NULL) {
      savedCost += ((int64_t) ePtr->count * ePtr->cost) / 1000000.0;
      ePtr = (Entry *)Ns_CacheNextEntry(&search);
    }

    return Ns_DStringPrintf(dest, "maxsize %lu size %lu entries %d "
               "flushed %lu hits %lu missed %lu hitrate %lu "
               "expired %lu pruned %lu saved %.6f",
               (unsigned long) cachePtr->maxSize,
               (unsigned long) cachePtr->currentSize,
               cachePtr->entriesTable.numEntries, cachePtr->stats.nflushed,
               cachePtr->stats.nhit, cachePtr->stats.nmiss, hitrate,
			    cachePtr->stats.nexpired, cachePtr->stats.npruned,
			    savedCost);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheResetStats --
 *
 *      Set all statictics to zero.
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
Ns_CacheResetStats(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    memset(&cachePtr->stats, 0, sizeof(cachePtr->stats));
}


/*
 *----------------------------------------------------------------------
 *
 * Expired --
 *
 *      Has the absolute ttl expired?
 *
 * Results:
 *      1 if entry has expired, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
Expired(Entry *ePtr, Ns_Time *nowPtr)
{
    Ns_Time  now;

    if (unlikely(ePtr->expires.sec > 0)) {
        if (nowPtr == NULL) {
            Ns_GetTime(&now);
            nowPtr = &now;
        }
        if (Ns_DiffTime(&ePtr->expires, nowPtr, NULL) < 0) {
            return 1;
        }
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Delink --
 *
 *      Remove a cache entry from the linked list of entries; this
 *      is used for maintaining the LRU list as well as removing entries
 *      that are still in use.
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
Delink(Entry *ePtr)
{
    if (ePtr->prevPtr != NULL) {
        ePtr->prevPtr->nextPtr = ePtr->nextPtr;
    } else {
        ePtr->cachePtr->firstEntryPtr = ePtr->nextPtr;
    }
    if (ePtr->nextPtr != NULL) {
        ePtr->nextPtr->prevPtr = ePtr->prevPtr;
    } else {
        ePtr->cachePtr->lastEntryPtr = ePtr->prevPtr;
    }
    ePtr->prevPtr = ePtr->nextPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Push --
 *
 *      Push an entry to the top of the linked list of entries, making
 *      it the Most Recently Used
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
Push(Entry *ePtr)
{
    if (likely(ePtr->cachePtr->firstEntryPtr != NULL)) {
        ePtr->cachePtr->firstEntryPtr->prevPtr = ePtr;
    }
    ePtr->prevPtr = NULL;
    ePtr->nextPtr = ePtr->cachePtr->firstEntryPtr;
    ePtr->cachePtr->firstEntryPtr = ePtr;
    if (unlikely(ePtr->cachePtr->lastEntryPtr == NULL)) {
        ePtr->cachePtr->lastEntryPtr = ePtr;
    }
}

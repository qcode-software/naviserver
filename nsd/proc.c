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
 * proc.c --
 *
 *      Support for describing procs and their arguments (thread routines,
 *      callbacks, scheduled procs, etc.).
 */

#include "nsd.h"

/*
 * The following struct maintains callback and description for
 * Ns_GetProcInfo.
 */

typedef struct Info {
    Ns_ArgProc  *proc;
    const char  *desc;
} Info;

/*
 * Static functions defined in this file.
 */

static Ns_ArgProc ServerArgProc;
static void AppendAddr(Tcl_DString *dsPtr, const char *prefix, const void *addr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable infoHashTable;

static const struct proc {
    ns_funcptr_t    procAddr;
    const char  *desc;
    Ns_ArgProc  *argProc;
} procs[] = {
    { (ns_funcptr_t)NsTclThread,          "ns:tclthread",        NsTclThreadArgProc},
    { (ns_funcptr_t)Ns_TclCallbackProc,   "ns:tclcallback",      Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsTclConnLocation,    "ns:tclconnlocation",  Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsTclSchedProc,       "ns:tclschedproc",     Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsTclServerRoot,      "ns:tclserverroot",    Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsTclSockProc,        "ns:tclsockcallback",  NsTclSockArgProc},
    { (ns_funcptr_t)NsConnThread,         "ns:connthread",       NsConnArgProc},
    { (ns_funcptr_t)NsTclFilterProc,      "ns:tclfilter",        Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsShortcutFilterProc, "ns:shortcutfilter",   NULL},
    { (ns_funcptr_t)NsTclRequestProc,     "ns:tclrequest",       Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsAdpPageProc,        "ns:adppage",          NsAdpPageArgProc},
    { (ns_funcptr_t)Ns_FastPathProc,      "ns:fastget",          NULL},
    { (ns_funcptr_t)NsTclTraceProc,       "ns:tcltrace",         Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsTclUrl2FileProc,    "ns:tclurl2file",      Ns_TclCallbackArgProc},
    { (ns_funcptr_t)NsMountUrl2FileProc,  "ns:mounturl2file",    NsMountUrl2FileArgProc},
    { (ns_funcptr_t)Ns_FastUrl2FileProc,  "ns:fasturl2file",     ServerArgProc},
    {NULL, NULL, NULL}
};


/*
 *----------------------------------------------------------------------
 *
 * NsInitProcInfo --
 *
 *      Initialize the proc info API and default compiled-in callbacks.
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
NsInitProcInfo(void)
{
    const struct proc *procPtr;

    Tcl_InitHashTable(&infoHashTable, TCL_ONE_WORD_KEYS);
    procPtr = procs;
    while (procPtr->procAddr != NULL) {
        Ns_RegisterProcInfo(procPtr->procAddr, procPtr->desc,
                            procPtr->argProc);
        ++procPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterProcInfo --
 *
 *      Register a proc description and a callback to describe the
 *      arguments e.g., a thread start arg.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Given argProc will be invoked for given procAddr by
 *      Ns_GetProcInfo.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterProcInfo(ns_funcptr_t procAddr, const char *desc, Ns_ArgProc *argProc)
{
    Tcl_HashEntry *hPtr;
    Info          *infoPtr;
    int            isNew;

    NS_NONNULL_ASSERT(procAddr != NULL);
    NS_NONNULL_ASSERT(desc != NULL);
    
    hPtr = Tcl_CreateHashEntry(&infoHashTable, (char *)procAddr, &isNew);
    if (isNew == 0) {
        infoPtr = Tcl_GetHashValue(hPtr);
    } else {
        infoPtr = ns_malloc(sizeof(Info));
        Tcl_SetHashValue(hPtr, infoPtr);
    }
    infoPtr->desc = desc;
    infoPtr->proc = argProc;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetProcInfo --
 *
 *      Format a string of information for the given proc
 *      and arg, invoking the argProc callback if it exists.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      String will be appended to given dsPtr.
 *
 *----------------------------------------------------------------------
 */

void
Ns_GetProcInfo(Tcl_DString *dsPtr, ns_funcptr_t procAddr, const void *arg)
{
    const Tcl_HashEntry  *hPtr;
    const Info           *infoPtr;
    static const Info     nullInfo = {NULL, NULL};

    NS_NONNULL_ASSERT(dsPtr != NULL);
    
    hPtr = Tcl_FindHashEntry(&infoHashTable, (char *) procAddr);
    if (hPtr != NULL) {
        infoPtr = Tcl_GetHashValue(hPtr);
    } else {
        infoPtr = &nullInfo;
    }
    /* Ns_Log(Notice, "Ns_GetProcInfo: infoPtr->desc %p", infoPtr->desc);*/
    if (infoPtr->desc != NULL) {
        Tcl_DStringAppendElement(dsPtr, infoPtr->desc);
    } else {
        AppendAddr(dsPtr, "p", (void *)procAddr);
    }
    /*Ns_Log(Notice, "Ns_GetProcInfo: infoPtr->proc %p", infoPtr->proc);*/
    if (infoPtr->proc != NULL) {
        (*infoPtr->proc)(dsPtr, arg);
    } else {
        AppendAddr(dsPtr, "a", arg);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StringArgProc --
 *
 *      Info callback for procs which take a C string arg.
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
Ns_StringArgProc(Tcl_DString *dsPtr, const void *arg)
{
    const char *str = arg;

    NS_NONNULL_ASSERT(dsPtr != NULL);
    
    Tcl_DStringAppendElement(dsPtr, (str != NULL) ? str : NS_EMPTY_STRING);
}


/*
 *----------------------------------------------------------------------
 *
 * ServerArgProc --
 *
 *      Info callback for procs which take an NsServer arg.
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
ServerArgProc(Tcl_DString *dsPtr, const void *arg)
{
    const NsServer *servPtr = arg;

    Tcl_DStringAppendElement(dsPtr, (servPtr != NULL) ? servPtr->server : NS_EMPTY_STRING);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendAddr -- 
 *
 *      Format a simple string with the given address.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      String will be appended to given dsPtr.
 *
 *----------------------------------------------------------------------
 */

static void
AppendAddr(Tcl_DString *dsPtr, const char *prefix, const void *addr)
{
    NS_NONNULL_ASSERT(dsPtr != NULL);
    NS_NONNULL_ASSERT(prefix != NULL);
    
    Ns_DStringPrintf(dsPtr, " %s:%p", prefix, addr);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

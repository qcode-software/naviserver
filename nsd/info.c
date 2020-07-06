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
 * info.c --
 *
 *  Ns_Info* API and ns_info command support.
 */

#include "nsd.h"

/*
 * Static variables defined in this file.
 */

static Ns_ThreadArgProc ThreadArgProc;


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoHomePath --
 *
 *      Return the home dir.
 *
 * Results:
 *      Home dir.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoHomePath(void)
{
    return nsconf.home;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServerName --
 *
 *      Return the server name.
 *
 * Results:
 *      Server name
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoServerName(void)
{
    return nsconf.name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServerVersion --
 *
 *      Returns the server version
 *
 * Results:
 *      String server version.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoServerVersion(void)
{
    return nsconf.version;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoConfigFile --
 *
 *      Returns path to config file.
 *
 * Results:
 *      Path to config file.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoConfigFile(void)
{
    return nsconf.config;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoPid --
 *
 *      Returns server's PID
 *
 * Results:
 *      PID (thread like pid_t)
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

pid_t
Ns_InfoPid(void)
{
    return nsconf.pid;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoNameOfExecutable --
 *
 *      Returns the name of the nsd executable.  Quirky name is from Tcl.
 *
 * Results:
 *      Name of executable, string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoNameOfExecutable(void)
{
    return nsconf.nsd;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoPlatform --
 *
 *      Return platform name
 *
 * Results:
 *      Platform name, string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoPlatform(void)
{

#if defined(__linux)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__sgi)
    return "irix";
#elif defined(__sun)

#if defined(__i386)
    return "solaris/intel";
#else
    return "solaris";
#endif

#elif defined(__alpha)
    return "OSF/1 - Alpha";
#elif defined(__hp10)
    return "hp10";
#elif defined(__hp11)
    return "hp11";
#elif defined(__unixware)
    return "UnixWare";
#elif defined(__APPLE__)
    return "osx";
#elif defined(_WIN32)
    return "win32";
#else
    return "?";
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoUptime --
 *
 *      Returns time server has been up.
 *
 * Results:
 *      Seconds server has been running.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

long
Ns_InfoUptime(void)
{
    double diff = difftime(time(NULL), nsconf.boot_t);

    return (long)diff;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoBootTime --
 *
 *      Returns time server started.
 *
 * Results:
 *      Treat as time_t.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

time_t
Ns_InfoBootTime(void)
{
    return nsconf.boot_t;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoHostname --
 *
 *      Return server hostname
 *
 * Results:
 *      Hostname
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoHostname(void)
{
    return nsconf.hostname;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoAddress --
 *
 *      Return server IP address
 *
 * Results:
 *      Primary (first) IP address of this machine.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoAddress(void)
{
    return nsconf.address;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoBuildDate --
 *
 *      Returns time server was compiled.
 *
 * Results:
 *      String build date and time.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoBuildDate(void)
{
    return nsconf.build;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoShutdownPending --
 *
 *      Boolean: is a shutdown pending?
 *
 * Results:
 *      NS_TRUE: yes, NS_FALSE: no
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_InfoShutdownPending(void)
{
    bool stopping;

    Ns_MutexLock(&nsconf.state.lock);
    stopping = nsconf.state.stopping;
    Ns_MutexUnlock(&nsconf.state.lock);

    return stopping;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoStarted --
 *
 *      Boolean: has the server started up all the way yet?
 *
 * Results:
 *      NS_TRUE: yes, NS_FALSE: no
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_InfoStarted(void)
{
    bool started;

    Ns_MutexLock(&nsconf.state.lock);
    started = nsconf.state.started;
    Ns_MutexUnlock(&nsconf.state.lock);

    return started;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServersStarted --
 *
 *      Compatibility function, same as Ns_InfoStarted
 *
 * Results:
 *      See Ns_InfoStarted
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_InfoServersStarted(void)
{
    return Ns_InfoStarted();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoTag --
 *
 *      Returns revision tag of this build
 *
 * Results:
 *      A string version name.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_InfoTag(void)
{
    return PACKAGE_TAG;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoIPv6 --
 *
 *      Returns information if the binary was compiled with IPv6 support
 *
 * Results:
 *      Boolean result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_InfoIPv6(void)
{
#ifdef HAVE_IPV6
    return NS_TRUE;
#else
    return NS_FALSE;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoSSL --
 *
 *      Returns information if the binary was compiled with OpenSSL support
 *
 * Results:
 *      Boolean result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

bool
Ns_InfoSSL(void)
{
#ifdef HAVE_OPENSSL_EVP_H
    return NS_TRUE;
#else
    return NS_FALSE;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInfoObjCmd --
 *
 *      Implements ns_info.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclInfoObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const* objv)
{
    int             opt, result = TCL_OK;
    bool            done = NS_TRUE;
    const NsInterp *itPtr = clientData;
    Tcl_DString     ds;

    static const char *const opts[] = {
        "address", "argv0", "boottime", "builddate", "callbacks",
        "config", "home", "hostname", "ipv6", "locks", "log",
        "major", "minor", "mimetypes", "name", "nsd", "pagedir",
        "pageroot", "patchlevel", "pid", "platform", "pools",
        "scheduled", "server", "servers",
        "sockcallbacks", "ssl", "tag", "tcllib", "threads", "uptime",
        "version", "winnt", "filters", "traces", "requestprocs",
        "url2file", "shutdownpending", "started", NULL
    };

    enum {
        IAddressIdx, IArgv0Idx, IBoottimeIdx, IBuilddateIdx, ICallbacksIdx,
        IConfigIdx, IHomeIdx, IHostNameIdx, IIpv6Idx, ILocksIdx, ILogIdx,
        IMajorIdx, IMinorIdx, IMimeIdx, INameIdx, INsdIdx,
        IPageDirIdx, IPageRootIdx, IPatchLevelIdx,
        IPidIdx, IPlatformIdx, IPoolsIdx,
        IScheduledIdx, IServerIdx, IServersIdx,
        ISockCallbacksIdx, ISSLIdx, ITagIdx, ITclLibIdx, IThreadsIdx, IUptimeIdx,
        IVersionIdx, IWinntIdx, IFiltersIdx, ITracesIdx, IRequestProcsIdx,
        IUrl2FileIdx, IShutdownPendingIdx, IStartedIdx
    };

    if (unlikely(objc != 2)) {
        Tcl_WrongNumArgs(interp, 1, objv, "option");
        return TCL_ERROR;
    }
    if (unlikely(Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                                     &opt) != TCL_OK)) {
        return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);

    switch (opt) {
    case IArgv0Idx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(nsconf.argv0, -1));
        break;

    case IStartedIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_InfoStarted() ? 1 : 0));
        break;

    case IShutdownPendingIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_InfoShutdownPending() ? 1 : 0));
        break;

    case INsdIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(nsconf.nsd, -1));
        break;

    case INameIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoServerName(), -1));
        break;

    case IConfigIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoConfigFile(), -1));
        break;

    case ICallbacksIdx:
        NsGetCallbacks(&ds);
        Tcl_DStringResult(interp, &ds);
        break;

    case ISockCallbacksIdx:
        NsGetSockCallbacks(&ds);
        Tcl_DStringResult(interp, &ds);
        break;

    case IScheduledIdx:
        NsGetScheduled(&ds);
        Tcl_DStringResult(interp, &ds);
        break;

    case ILocksIdx:
        Ns_MutexList(&ds);
        Tcl_DStringResult(interp, &ds);
        break;

    case IThreadsIdx:
        Ns_ThreadList(&ds, ThreadArgProc);
        Tcl_DStringResult(interp, &ds);
        break;

    case IPoolsIdx:
#ifdef HAVE_TCL_GETMEMORYINFO
        Tcl_GetMemoryInfo(&ds);
        Tcl_DStringResult(interp, &ds);
#endif
        break;

    case ILogIdx:
        {
            const char *elog = Ns_InfoErrorLog();
            Tcl_SetObjResult(interp, Tcl_NewStringObj(elog == NULL ? "STDOUT" : elog, -1));
        }
        break;

    case IPlatformIdx:
        Ns_LogDeprecated(objv, 2, "$::tcl_platform(platform)", NULL);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoPlatform(), -1));
        break;

    case IHostNameIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoHostname(), -1));
        break;

    case IIpv6Idx:
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(Ns_InfoIPv6()));
        break;

    case IAddressIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoAddress(), -1));
        break;

    case IUptimeIdx:
        Tcl_SetObjResult(interp, Tcl_NewLongObj(Ns_InfoUptime()));
        break;

    case IBoottimeIdx:
        Tcl_SetObjResult(interp, Tcl_NewLongObj((long)Ns_InfoBootTime()));
        break;

    case IPidIdx:
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)Ns_InfoPid()));
        break;

    case IMajorIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(NS_MAJOR_VERSION));
        break;

    case IMinorIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(NS_MINOR_VERSION));
        break;

    case IMimeIdx:
        NsGetMimeTypes(&ds);
        Tcl_DStringResult(interp, &ds);
        break;

    case IVersionIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(NS_VERSION, -1));
        break;

    case IPatchLevelIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(NS_PATCH_LEVEL, -1));
        break;

    case IHomeIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoHomePath(), -1));
        break;

    case IWinntIdx:
        Ns_LogDeprecated(objv, 2, "$::tcl_platform(platform)", NULL);
#ifdef _WIN32
        Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
#else
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
#endif
        break;

    case IBuilddateIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoBuildDate(), -1));
        break;

    case ITagIdx:
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Ns_InfoTag(), -1));
        break;

    case IServersIdx:
        {
            const Tcl_DString *dsPtr = &nsconf.servers;
            Tcl_SetObjResult(interp, Tcl_NewStringObj(dsPtr->string, dsPtr->length));
            break;
        }

    case ISSLIdx:
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(Ns_InfoSSL()));
        break;

    default:
        /* cases handled below */
        done = NS_FALSE;
        break;
    }

    if (!done) {
        /*
         * The following subcommands require a virtual server.
         */

        if (unlikely(itPtr->servPtr == NULL)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("no server", -1));
            result = TCL_ERROR;

        } else {
            const char *server;

            server = itPtr->servPtr->server;

            switch (opt) {
            case IServerIdx:
                Tcl_SetObjResult(interp,  Tcl_NewStringObj(server, -1));
                break;

                /*
                 * All following cases are deprecated.
                 */

            case IPageDirIdx: NS_FALL_THROUGH; /* fall through */
            case IPageRootIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? pagedir", NULL);
                NsPageRoot(&ds, itPtr->servPtr, NULL);
                Tcl_DStringResult(interp, &ds);
                break;

            case ITclLibIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? tcllib", NULL);
                Tcl_SetObjResult(interp, Tcl_NewStringObj(itPtr->servPtr->tcl.library, -1));
                break;

            case IFiltersIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? filters", NULL);
                NsGetFilters(&ds, server);
                Tcl_DStringResult(interp, &ds);
                break;

            case ITracesIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? traces", NULL);
                NsGetTraces(&ds, server);
                Tcl_DStringResult(interp, &ds);
                break;

            case IRequestProcsIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? requestprocs", NULL);
                NsGetRequestProcs(&ds, server);
                Tcl_DStringResult(interp, &ds);
                break;

            case IUrl2FileIdx:
                Ns_LogDeprecated(objv, 2, "ns_server ?-server s? url2file", NULL);
                NsGetUrl2FileProcs(&ds, server);
                Tcl_DStringResult(interp, &ds);
                break;

            default:
                Tcl_SetObjResult(interp, Tcl_NewStringObj("unrecognized option", -1));
                result = TCL_ERROR;
                break;
            }
        }
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLibraryObjCmd --
 *
 *  Implements ns_library.
 *
 * Results:
 *  Tcl result.
 *
 * Side effects:
 *  See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclLibraryObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const* objv)
{
    int          result = TCL_OK;
    char        *kindString = (char *)NS_EMPTY_STRING, *moduleString = NULL;
    const char  *lib = NS_EMPTY_STRING;
    const NsInterp *itPtr = clientData;
    Ns_ObjvSpec  args[] = {
        {"kind",    Ns_ObjvString,  &kindString, NULL},
        {"?module", Ns_ObjvString,  &moduleString, NULL},
        {NULL, NULL, NULL, NULL}
    };

    if (Ns_ParseObjv(NULL, args, interp, 1, objc, objv) != NS_OK) {
        result = TCL_ERROR;

    } else if (STREQ(kindString, "private")) {
        lib = itPtr->servPtr->tcl.library;
    } else if (STREQ(kindString, "shared")) {
        lib = nsconf.tcl.sharedlibrary;
    } else {
        Ns_TclPrintfResult(interp, "unknown library \"%s\":"
                           " should be private or shared", kindString);
        result = TCL_ERROR;
    }

    if (result == TCL_OK) {
        Ns_DString ds;

        Ns_DStringInit(&ds);
        if (moduleString != NULL) {
            Ns_MakePath(&ds, lib, moduleString, (char *)0L);
        } else {
            Ns_MakePath(&ds, lib, (char *)0L);
        }
        Tcl_DStringResult(interp, &ds);
    }
    return result;
}


static void
ThreadArgProc(Tcl_DString *dsPtr, Ns_ThreadProc proc, const void *arg)
{
    Ns_GetProcInfo(dsPtr, (ns_funcptr_t)proc, arg);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 70
 * indent-tabs-mode: nil
 * End:
 */

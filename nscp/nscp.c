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
 * nscp.c --
 *
 *      Simple control port module for AOLserver which allows
 *      one to telnet to a specified port, login, and issue
 *      Tcl commands.
 */

#include "ns.h"

/*
 * The following structure is allocated each instance of
 * the loaded module.
 */

typedef struct Mod {
    Tcl_HashTable users;
    const char *server;
    const char *addr;
    int port;
    int echo;
    int commandLogging;
} Mod;

static Ns_ThreadProc EvalThread;

/*
 * The following structure is allocated for each session.
 */

typedef struct Sess {
    Mod *modPtr;
    const char *user;
    int id;
    NS_SOCKET sock;
    struct sockaddr_in sa;
} Sess;

/*
 * The following functions are defined locally.
 */

static Ns_SockProc AcceptProc;
static Tcl_CmdProc ExitCmd;
static int Login(const Sess *sessPtr, Tcl_DString *unameDSPtr);
static int GetLine(NS_SOCKET sock, const char *prompt, Tcl_DString *dsPtr, int echo);
static Ns_ArgProc ArgProc;

/*
 * The following values are sent to the telnet client to enable
 * and disable password prompt echo.
 */

#define TN_IAC  255U
#define TN_WILL 251U
#define TN_WONT 252U
#define TN_DO   253U
#define TN_DONT 254U
#define TN_EOF  236U
#define TN_IP   244U
#define TN_ECHO   1U

static const unsigned char do_echo[]    = {TN_IAC, TN_DO,   TN_ECHO};
static const unsigned char dont_echo[]  = {TN_IAC, TN_DONT, TN_ECHO};
static const unsigned char will_echo[]  = {TN_IAC, TN_WILL, TN_ECHO};
static const unsigned char wont_echo[]  = {TN_IAC, TN_WONT, TN_ECHO};

/*
 * Define the version of the module (ususally 1).
 */

NS_EXPORT const int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Load the config parameters, setup the structures, and
 *	listen on the control port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will listen for control connections on specified
 *  	address and port.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT int
Ns_ModuleInit(const char *server, const char *module)
{
    Mod           *modPtr;
    char          *end;
    const char    *addr, *path;
    int            isNew, port, result;
    size_t         i;
    NS_SOCKET      lsock;
    Tcl_HashEntry *hPtr;
    Ns_Set        *set;

    /*
     * Create the listening socket and callback.
     */

    path = Ns_ConfigGetPath(server, module, (char *)0);
    addr = Ns_ConfigString(path, "address", "127.0.0.1");
    port = Ns_ConfigInt(path, "port", 2080);

    if ((addr == NULL) || (port <= 0 ))  {
	Ns_Log(Error, "nscp: address and port must be specified in config");
	return NS_ERROR;
    }
    lsock = Ns_SockListen(addr, port);
    if (lsock == NS_INVALID_SOCKET) {
	Ns_Log(Error, "nscp: could not listen on %s:%d", addr, port);
	return NS_ERROR;
    }
    Ns_Log(Notice, "nscp: listening on %s:%d", addr, port);

    /*
     * Create a new Mod structure for this instance.
     */

    modPtr = ns_malloc(sizeof(Mod));
    modPtr->server = server;
    modPtr->addr = addr;
    modPtr->port = port;
    modPtr->echo = Ns_ConfigBool(path, "echopasswd", NS_TRUE);
    modPtr->commandLogging = Ns_ConfigBool(path, "cpcmdlogging", NS_FALSE);

    /*
     * Initialize the hash table of authorized users.  Entry values
     * are either NULL indicating authorization should be checked
     * via the Ns_AuthorizeUser() API or contain a Unix crypt(3)
     * sytle encrypted password.  For the later, the entry is
     * compatible with /etc/passwd (i.e., username followed by
     * password separated by colons).
     */

    Tcl_InitHashTable(&modPtr->users, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "users", (char *)0);
    set = Ns_ConfigGetSection(path);

    /*
     * In default local mode just create empty user without password
     */

    if (set == NULL && STREQ(addr, "127.0.0.1")) {
        Ns_DString ds;

        Ns_DStringInit(&ds);
        path = Ns_ModulePath(&ds, server, module, "users", (char *)0);
        set = Ns_ConfigCreateSection(path);
        Ns_SetUpdate(set, "user", "::");
        Ns_DStringFree(&ds);
    }

    /*
     * Process the setup ns_set
     */
    for (i = 0U; set != NULL && i < Ns_SetSize(set); ++i) {
	const char *key  = Ns_SetKey(set, i);
	const char *user = Ns_SetValue(set, i);
        const char *passPart;
        char *scratch, *p;
        size_t userLength;
 
        if (!STRIEQ(key, "user")) {
            continue;
        }
        passPart = strchr(user, ':');
        if (passPart == NULL) {
            Ns_Log(Warning, "nscp: user entry '%s' contains no colon; ignored.", user);
	    continue;
        }

        /*
         * Copy string to avoid conflicts with const property.
         */
        p = scratch = ns_strdup(user);

        /*
         * Terminate user part.
         */
        userLength = (size_t)(passPart - user);
	*(p + userLength) = '\0';

	hPtr = Tcl_CreateHashEntry(&modPtr->users, p, &isNew);
	if (isNew != 0) {
	    Ns_Log(Notice, "nscp: added user: %s", p);
	} else {
	    Ns_Log(Warning, "nscp: duplicate user: %s", p);
	    ns_free(Tcl_GetHashValue(hPtr));
	}
        /*
         * Advance to password part in scratch copy.
         */
	p += userLength + 1u;

        /* 
         * look for end of password.
         */
	end = strchr(p, ':');
	if (end != NULL) {
	    *end = '\0';
	}

        /*
         * Save the password.
         */
	Tcl_SetHashValue(hPtr, ns_strdup(p));

        ns_free(scratch);
    }
    if (modPtr->users.numEntries == 0) {
	Ns_Log(Warning, "nscp: no authorized users");
    }
    result = Ns_SockCallback(lsock, AcceptProc, modPtr, 
                             ((unsigned int)NS_SOCK_READ | (unsigned int)NS_SOCK_EXIT));
    if (result == TCL_OK) {
        Ns_RegisterProcInfo((Ns_Callback *)AcceptProc, "nscp", ArgProc);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ArgProc --
 *
 *	Append listen port info for query callback.
 *
 * Results:
 *	None
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static void
ArgProc(Tcl_DString *dsPtr, const void *arg)
{
    const Mod *modPtr = arg;

    Tcl_DStringStartSublist(dsPtr);
    Ns_DStringPrintf(dsPtr, "%s %d", modPtr->addr, modPtr->port);
    Tcl_DStringEndSublist(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AcceptProc --
 *
 *	Socket callback to accept a new connection.
 *
 * Results:
 *	NS_TRUE to keep listening unless shutdown is in progress.
 *
 * Side effects:
 *  	New EvalThread will be created.
 *
 *----------------------------------------------------------------------
 */

static bool
AcceptProc(NS_SOCKET sock, void *arg, unsigned int why)
{
    Mod       *modPtr = arg;
    Sess      *sessPtr;
    socklen_t  len;

    if (why == (unsigned int)NS_SOCK_EXIT) {
	Ns_Log(Notice, "nscp: shutdown");
	(void )ns_sockclose(sock);
	return NS_FALSE;
    }

    sessPtr = ns_malloc(sizeof(Sess));
    sessPtr->modPtr = modPtr;
    len = (socklen_t)sizeof(struct sockaddr_in);
    sessPtr->sock = Ns_SockAccept(sock, (struct sockaddr *) &sessPtr->sa, &len);
    if (sessPtr->sock == NS_INVALID_SOCKET) {
	Ns_Log(Error, "nscp: accept() failed: %s",
	       ns_sockstrerror(ns_sockerrno));
	ns_free(sessPtr);
    } else {
        static int next = 0;

	sessPtr->id = ++next;
	Ns_ThreadCreate(EvalThread, sessPtr, 0, NULL);
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * EvalThread --
 *
 *	Thread to read and evaluate commands from remote.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Depends on commands.
 *
 *----------------------------------------------------------------------
 */

static void
EvalThread(void *arg)
{
    Tcl_Interp *interp;
    Tcl_DString ds;
    Tcl_DString unameDS;
    char buf[64];
    int ncmd, stop;
    size_t len;
    Sess *sessPtr = arg;
    const char *res, *server = sessPtr->modPtr->server;

    /*
     * Initialize the thread and login the user.
     */

    interp = NULL;
    Tcl_DStringInit(&ds);
    Tcl_DStringInit(&unameDS);
    snprintf(buf, sizeof(buf), "-nscp:%d-", sessPtr->id);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "nscp: %s connected", ns_inet_ntoa(sessPtr->sa.sin_addr));
    if (Login(sessPtr, &unameDS) == 0) {
	goto done;
    }

    sessPtr->user = Tcl_DStringValue(&unameDS);

    /*
     * Loop until the remote shuts down, evaluating complete
     * commands.
     */

    interp = Ns_TclAllocateInterp(server);

    /*
     * Create a special exit command for this interp only.
     */

    stop = 0;
    Tcl_CreateCommand(interp, "exit", ExitCmd, (ClientData) &stop, NULL);

    ncmd = 0;
    while (stop == 0) {
	Tcl_DStringTrunc(&ds, 0);
	++ncmd;
retry:
	snprintf(buf, sizeof(buf), "%s:nscp %d> ", server, ncmd);
	while (1) {
	    if (GetLine(sessPtr->sock, buf, &ds, 1) == 0) {
		goto done;
	    }
	    if (Tcl_CommandComplete(ds.string)) {
		break;
	    }
	    snprintf(buf, sizeof(buf), "%s:nscp %d>>> ", server, ncmd);
	}
	while (ds.length > 0 && ds.string[ds.length-1] == '\n') {
	    Tcl_DStringTrunc(&ds, ds.length-1);
	}
	if (STREQ(ds.string, "")) {
	    goto retry; /* Empty command - try again. */
	}

        if (sessPtr->modPtr->commandLogging != 0) {
            Ns_Log(Notice, "nscp: %s %d: %s", sessPtr->user, ncmd, ds.string);
        }

	if (Tcl_RecordAndEval(interp, ds.string, 0) != TCL_OK) {
	    (void) Ns_TclLogErrorInfo(interp, "\n(context: nscp)");
	}
	Tcl_AppendResult(interp, "\r\n", NULL);
	res = Tcl_GetStringResult(interp);
	len = strlen(res);
	while (len > 0u) {
	    ssize_t sent = ns_send(sessPtr->sock, res, len, 0);
	    if (sent <= 0) {
		goto done;
	    }
	    len -= (size_t)sent;
	    res += sent;
	}

        if (sessPtr->modPtr->commandLogging != 0) {
            Ns_Log(Notice, "nscp: %s %d: done", sessPtr->user, ncmd);
        }
    }
done:
    Tcl_DStringFree(&ds);
    Tcl_DStringFree(&unameDS);
    if (interp != NULL) {
    	Ns_TclDeAllocateInterp(interp);
    }
    Ns_Log(Notice, "nscp: %s disconnected", ns_inet_ntoa(sessPtr->sa.sin_addr));
    ns_sockclose(sessPtr->sock);
    ns_free(sessPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetLine --
 *
 *	Prompt for a line of input from the remote.  \r\n sequences
 *  	are translated to \n.
 *
 * Results:
 *  	1 if line received, 0 if remote dropped.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetLine(NS_SOCKET sock, const char *prompt, Tcl_DString *dsPtr, int echo)
{
    char   buf[2048];
    int    result = 0, retry = 0;
    ssize_t n;
    size_t promptLength;

    /*
     * Suppress output on things like password prompts.
     */

    if (echo == 0) {
	(void)ns_send(sock, (const void*)will_echo, 3u, 0);
	(void)ns_send(sock, (const void*)dont_echo, 3u, 0);
	(void)ns_recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
    promptLength = strlen(prompt);
    if (ns_send(sock, prompt, promptLength, 0) != (ssize_t)promptLength) {
	result = 0;
	goto bail;
    }

    do {
	n = ns_recv(sock, buf, sizeof(buf), 0);
	if (n <= 0) {
	    result = 0;
	    goto bail;
	}

	if (n > 1 && buf[n-1] == '\n' && buf[n-2] == '\r') {
	    buf[n-2] = '\n';
	    --n;
	}

	/*
	 * This EOT checker cannot happen in the context of telnet.
	 */
	if (n == 1 && buf[0] == '\4') {
	    result = 0;
	    goto bail;
	}

	/*
	 * Deal with telnet IAC commands in some sane way.
	 */

	if (n > 1 && UCHAR(buf[0]) == TN_IAC) {
	    if ( UCHAR(buf[1]) == TN_EOF) {
		result = 0;
		goto bail;
	    } else if (UCHAR(buf[1]) == TN_IP) {
		result = 0;
		goto bail;
            } else if ((UCHAR(buf[1]) == TN_WONT) && (retry < 2)) {
                /*
                 * It seems like the flush at the bottom of this func
                 * does not always get all the acks, thus an echo ack
                 * showing up here. Not clear why this would be.  Need
                 * to investigate further. For now, breeze past these
                 * (within limits).
                 */
                retry++;
                continue;
	    } else {
		Ns_Log(Warning, "nscp: "
		       "unsupported telnet IAC code received from client");
		result = 0;
		goto bail;
	    }
	}

	Tcl_DStringAppend(dsPtr, buf, (int)n);
	result = 1;

    } while (buf[n-1] != '\n');

 bail:
    if (echo == 0) {
	(void)ns_send(sock, (const void*)wont_echo, 3u, 0);
	(void)ns_send(sock, (const void*)do_echo, 3u, 0);
	(void)ns_recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Login --
 *
 *	Attempt to login the user.
 *
 * Results:
 *  	1 if login ok, 0 otherwise.
 *
 * Side effects:
 *  	Stores user's login name into unameDSPtr.
 *
 *----------------------------------------------------------------------
 */

static int
Login(const Sess *sessPtr, Tcl_DString *unameDSPtr)
{
    Tcl_DString uds, pds, msgDs;
    const char *user = NULL;
    int         ok = 0;

    Tcl_DStringInit(&uds);
    Tcl_DStringInit(&pds);
    if (GetLine(sessPtr->sock, "login: ", &uds, 1) != 0 &&
	GetLine(sessPtr->sock, "Password: ", &pds, sessPtr->modPtr->echo) != 0) {
        Tcl_HashEntry  *hPtr;
	const char     *pass;

	user = Ns_StrTrim(uds.string);
	pass = Ns_StrTrim(pds.string);
    	hPtr = Tcl_FindHashEntry(&sessPtr->modPtr->users, user);
	if (hPtr != NULL) {
	    const char *encpass = Tcl_GetHashValue(hPtr);
	    char  buf[NS_ENCRYPT_BUFSIZE];

	    (void) Ns_Encrypt(pass, encpass, buf);
    	    if (STREQ(buf, encpass)) {
		ok = 1;
	    }
	}
    }

    /*
     * Report the result of the login to the user.
     */

    Ns_DStringInit(&msgDs);
    if (ok != 0) {
        Ns_Log(Notice, "nscp: %s logged in", user);
        Tcl_DStringAppend(unameDSPtr, user, -1);
        Ns_DStringPrintf(&msgDs,
            "\nWelcome to %s running at %s (pid %d)\n"
            "%s/%s for %s built on %s\nTag: %s\n",
            sessPtr->modPtr->server,
            Ns_InfoNameOfExecutable(), Ns_InfoPid(),
            Ns_InfoServerName(), Ns_InfoServerVersion(),
            Ns_InfoPlatform(), Ns_InfoBuildDate(), Ns_InfoTag());
    } else {
	Ns_Log(Warning, "nscp: login failed: '%s'", (user != NULL) ? user : "?");
        Ns_DStringAppend(&msgDs, "Access denied!\n");
    }
    (void) ns_send(sessPtr->sock, msgDs.string, (size_t)msgDs.length, 0);

    Tcl_DStringFree(&msgDs);
    Tcl_DStringFree(&uds);
    Tcl_DStringFree(&pds);

    return ok;
}


/*
 *----------------------------------------------------------------------
 *
 * ExitCmd --
 *
 *	Special exit command for nscp.
 *
 * Results:
 *  	Standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
ExitCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST84 char *argv[])
{
    int *stopPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], "\"", NULL);
	return TCL_ERROR;
    }

    stopPtr = (int *) arg;
    *stopPtr = 1;
    Tcl_SetResult(interp, "\nGoodbye!", TCL_STATIC);
    return TCL_OK;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

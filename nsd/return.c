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
 * return.c --
 *
 *      Functions that construct a response and return it to the client.
 */

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int ReturnOpen(Ns_Conn *conn, int status, CONST char *type, Tcl_Channel chan,
                      FILE *fp, int fd, size_t len);
static int ReturnRange(Ns_Conn *conn, CONST char *type,
                       int fd, CONST void *data, size_t len);

/*
 * This structure connections HTTP response codes to their descriptions.
 */

static struct {
    int         status;
    CONST char *reason;
} reasons[] = {
    {100, "Continue"},
    {101, "Switching Protocols"},
    {102, "Processing"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {207, "Multi-Status"},
    {300, "Multiple Choices"},
    {301, "Moved"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Request Entity Too Large"},
    {414, "Request-URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Requested Range Not Satisfiable"},
    {417, "Expectation Failed"},
    {422, "Unprocessable Entity"},
    {423, "Locked"},
    {424, "Method Failure"},
    {425, "Insufficient Space On Resource"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {507, "Insufficient Storage"}
};

/*
 * Static variables defined in this file.
 */

static int nreasons = (sizeof(reasons) / sizeof(reasons[0]));



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetHeaders --
 *
 *      Add an output header.
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
Ns_ConnSetHeaders(Ns_Conn *conn, CONST char *field, CONST char *value)
{
    Ns_SetPut(conn->outputheaders, field, value);
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnUpdateHeaders --
 *
 *      Update an output header.
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
Ns_ConnUpdateHeaders(Ns_Conn *conn, CONST char *field, CONST char *value)
{
    Ns_SetUpdate(conn->outputheaders, field, value);
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPrintfHeaders --
 *
 *      Add a printf-style string as an output header.
 *
 * Results:
 *      None
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnPrintfHeaders(Ns_Conn *conn, CONST char *field, CONST char *fmt,...)
{
    Ns_DString ds;
    va_list ap;

    Ns_DStringInit(&ds);
    va_start(ap, fmt);
    Ns_DStringVPrintf(&ds, fmt, ap);
    va_end(ap);
    Ns_SetPut(conn->outputheaders, field, ds.string);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCondSetHeaders --
 *
 *      Add an output header, only if it doesn't already exist.
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
Ns_ConnCondSetHeaders(Ns_Conn *conn, CONST char *field, CONST char *value)
{
    if (Ns_SetIGet(conn->outputheaders, field) == NULL) {
        Ns_SetPut(conn->outputheaders, field, value);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReplaceHeaders --
 *
 *      Free the existing outpheaders and set them to a copy of
 *      newheaders.
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
Ns_ConnReplaceHeaders(Ns_Conn *conn, Ns_Set *newheaders)
{
    Ns_SetFree(conn->outputheaders);
    conn->outputheaders = Ns_SetCopy(newheaders);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetTypeHeader --
 *
 *      Sets the Content-Type HTTP output header
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
Ns_ConnSetTypeHeader(Ns_Conn *conn, CONST char *type)
{
    Ns_ConnUpdateHeaders(conn, "Content-Type", type);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetEncodedTypeHeader --
 *
 *      Sets the Content-Type HTTP output header and charset for
 *      text and other types which may need to be transcoded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      My change the output encoding if charset specified or add a
 *      charset to the mime-type otherwise.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnSetEncodedTypeHeader(Ns_Conn *conn, CONST char *type)
{
    Tcl_Encoding  encoding;
    CONST char   *charset;
    Ns_DString    ds;
    size_t        len;

    Ns_DStringInit(&ds);
    charset = NsFindCharset(type, &len);

    if (charset != NULL) {
        encoding = Ns_GetCharsetEncodingEx(charset, len);
        Ns_ConnSetEncoding(conn, encoding);
    } else {
        encoding = Ns_ConnGetEncoding(conn);
        charset = Ns_GetEncodingCharset(encoding);
        Ns_DStringVarAppend(&ds, type, "; charset=", charset, NULL);
        type = ds.string;
    }

    Ns_ConnSetTypeHeader(conn, type);
    conn->flags |= NS_CONN_WRITE_ENCODED;

    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetLengthHeader --
 *
 *      Set the Content-Length output header.
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
Ns_ConnSetLengthHeader(Ns_Conn *conn, Tcl_WideInt length)
{
    Conn *connPtr = (Conn *) conn;

    if (length >= 0) {
        char strlength[TCL_INTEGER_SPACE];
        snprintf(strlength, sizeof(strlength), "%" TCL_LL_MODIFIER "d", length);
        Ns_ConnUpdateHeaders(conn, "Content-Length", strlength);
    } else {
        Ns_SetIDeleteKey(conn->outputheaders, "Content-Length");
    }
    connPtr->responseLength = length;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetLastModifiedHeader --
 *
 *      Set the Last-Modified output header if it isn't already set.
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
Ns_ConnSetLastModifiedHeader(Ns_Conn *conn, time_t *mtime)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_ConnCondSetHeaders(conn, "Last-Modified", Ns_HttpTime(&ds, mtime));
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetExpiresHeader --
 *
 *      Set the Expires output header.
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
Ns_ConnSetExpiresHeader(Ns_Conn *conn, CONST char *expires)
{
    Ns_ConnSetHeaders(conn, "Expires", expires);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnConstructHeaders --
 *
 *      Put the header of an HTTP response into the dstring.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Content length and connection-keepalive headers will be added
 *      if possible.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnConstructHeaders(Ns_Conn *conn, Ns_DString *dsPtr)
{
    Conn       *connPtr = (Conn *) conn;
    int         i;
    CONST char *reason, *value, *key;

    /*
     * Construct the HTTP response status line.
     */

    reason = "Unknown Reason";
    for (i = 0; i < nreasons; i++) {
        if (reasons[i].status == connPtr->responseStatus) {
            reason = reasons[i].reason;
            break;
        }
    }

    Ns_DStringPrintf(dsPtr, "HTTP/%.1f %d %s\r\n",
                     MIN(connPtr->request->version, 1.1),
                     connPtr->responseStatus,
                     reason);

    /*
     * Add the basic required headers if they.
     */

    Ns_DStringVarAppend(dsPtr,
        "MIME-Version: 1.0\r\n"
        "Server: ", Ns_InfoServerName(), "/", Ns_InfoServerVersion(), "\r\n",
        "Date: ",
        NULL);
    Ns_HttpTime(dsPtr, NULL);
    Ns_DStringNAppend(dsPtr, "\r\n", 2);

    /*
     * Output any extra headers.
     */

    if (conn->outputheaders != NULL) {
        for (i = 0; i < Ns_SetSize(conn->outputheaders); i++) {
            key = Ns_SetKey(conn->outputheaders, i);
            value = Ns_SetValue(conn->outputheaders, i);
            if (key != NULL && value != NULL) {
		char *lineBreak = strchr(value, '\n');

		if (!lineBreak) {
		    Ns_DStringVarAppend(dsPtr, key, ": ", value, "\r\n", NULL);
		} else {
		    Ns_DString sanitize, *sPtr = &sanitize;
		    /*
		     * We have to sanititize the header field to avoid
		     * a HTTP response splitting attack. After each
		     * newline in the value, we insert a TAB character
		     * (see Section 4.2 in RFC 2616)
		     */

		    Ns_DStringInit(&sanitize);

		    do {
			size_t offset = lineBreak - value;
			
			if (offset > 0) {
			    Tcl_DStringAppend(sPtr, value, offset);
			}
			Tcl_DStringAppend(sPtr, "\n\t", 2);

			offset ++;
			value += offset;
			lineBreak = strchr(value, '\n');

		    } while (lineBreak != NULL);

		    Tcl_DStringAppend(sPtr, value, -1);

		    Ns_DStringVarAppend(dsPtr, key, ": ", Tcl_DStringValue(sPtr), "\r\n", NULL);
		    Ns_DStringFree(sPtr);
		}
            }
        }
    }

    /*
     * End of headers.
     */

    Ns_DStringNAppend(dsPtr, "\r\n", 2);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnQueueHeaders, Ns_ConnFlushHeaders, Ns_ConnSetRequiredHeaders --
 *
 *      Deprecated.
 *
 * Results:
 *      None / Number of bytes written.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnQueueHeaders(Ns_Conn *conn, int status)
{
    Ns_ConnSetResponseStatus(conn, status);
}

Tcl_WideInt
Ns_ConnFlushHeaders(Ns_Conn *conn, int status)
{
    Conn *connPtr = (Conn *) conn;

    Ns_ConnSetResponseStatus(conn, status);
    Ns_ConnWriteVData(conn, NULL, 0, 0);

    return connPtr->nContentSent;
}

void
Ns_ConnSetRequiredHeaders(Ns_Conn *conn, CONST char *type, size_t length)
{
    Ns_ConnSetTypeHeader(conn, type);
    Ns_ConnSetLengthHeader(conn, length);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResetReturn --
 *
 *      Deprecated
 *
 * Results:
 *      NS_OK
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResetReturn(Ns_Conn *conn)
{
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnAdminNotice --
 *
 *      Return a short notice to a client to contact system
 *      administrator.
 *
 * Results:
 *      See Ns_ConnReturnNotice
 *
 * Side effects:
 *      See Ns_ConnReturnNotice
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnAdminNotice(Ns_Conn *conn, int status,
                         CONST char *title, CONST char *notice)
{
    return Ns_ConnReturnNotice(conn, status, title, notice);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNotice --
 *
 *      Return a short notice to a client.
 *
 * Results:
 *      See Ns_ReturnHtml.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotice(Ns_Conn *conn, int status,
                    CONST char *title, CONST char *notice)
{
    Conn       *connPtr = (Conn *) conn;
    NsServer   *servPtr = connPtr->poolPtr->servPtr;
    Ns_DString  ds;
    int         result;

    Ns_DStringInit(&ds);
    if (title == NULL) {
        title = "Server Message";
    }
    Ns_DStringVarAppend(&ds,
            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
            "<HTML>\n<HEAD>\n"
            "<TITLE>", title, "</TITLE>\n"
            "</HEAD>\n<BODY>\n"
            "<H2>", title, "</H2>\n", NULL);
    if (notice != NULL) {
        Ns_DStringVarAppend(&ds, notice, "\n", NULL);
    }

    /*
     * Detailed server information at the bottom of the page.
     */

    if (servPtr->opts.noticedetail) {
        Ns_DStringVarAppend(&ds, "<P ALIGN=RIGHT><SMALL><I>",
                            Ns_InfoServerName(), "/",
                            Ns_InfoServerVersion(), " on ",
                            NULL);
        Ns_ConnLocationAppend(conn, &ds);
        Ns_DStringAppend(&ds, "</I></SMALL></P>\n");
    }

    /*
     * Padding that suppresses those horrible MSIE friendly errors.
     * NB: Because we pad inside the body we may pad more than needed.
     */

    if (status >= 400) {
        while (ds.length < servPtr->opts.errorminsize) {
            Ns_DStringAppend(&ds, "                    ");
        }
    }

    Ns_DStringVarAppend(&ds, "\n</BODY></HTML>\n", NULL);

    result = Ns_ConnReturnCharData(conn, status, ds.string, ds.length, "text/html");
    Ns_DStringFree(&ds);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnData --
 *
 *      Sets required headers, dumps them, and then writes your data.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      May set numerous headers, will close connection.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnData(Ns_Conn *conn, int status, CONST char *data, 
		  ssize_t len, CONST char *type)
{
    int result;

    if (type != NULL) {
        Ns_ConnSetTypeHeader(conn, type);
    }
    if (len < 0) {
        len = data ? strlen(data) : 0;
    }
    Ns_ConnSetResponseStatus(conn, status);
    result = ReturnRange(conn, type, -1, data, len);
    Ns_ConnClose(conn);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnCharData --
 *
 *      Sets required headers, dumps them, and then writes your
 *      data, translating from utf-8 to the correct character encoding.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      May set numerous headers, will close connection.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnCharData(Ns_Conn *conn, int status, CONST char *data, 
		      ssize_t len, CONST char *type)
{
    struct iovec sbuf;

    if (type != NULL) {
        Ns_ConnSetEncodedTypeHeader(conn, type);
    }

    sbuf.iov_base = (void *)data;
    sbuf.iov_len = len < 0 ? (data ? strlen(data) : 0) : len;

    Ns_ConnSetResponseStatus(conn, status);
    Ns_ConnWriteVChars(conn, &sbuf, 1, 0);

    return Ns_ConnClose(conn);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnHtml --
 *
 *      Return utf-8 character data as mime-type text/html to client.
 *
 * Results:
 *      NS_OK/NS_ERROR
 *
 * Side effects:
 *      See Ns_ConnReturnCharData
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnHtml(Ns_Conn *conn, int status, CONST char *html, ssize_t len)
{
    return Ns_ConnReturnCharData(conn, status, html, len, "text/html");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOpenChannel, FILE, fd --
 *
 *      Return an open channel, FILE, or fd out the conn.
 *
 * Results:
 *      NS_OK / NS_ERROR.
 *
 * Side effects:
 *      Will set a length header, so 'len' must describe the complete
 *      length of the entitiy.
 *
 *      May send various HTTP error responses.
 *
 *      May return before the content has been sent if the writer-queue
 *      is enabled.
 *
 *      Will close the connection.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnOpenChannel(Ns_Conn *conn, int status, CONST char *type,
                         Tcl_Channel chan, size_t len)
{
    return ReturnOpen(conn, status, type, chan, NULL, -1, len);
}

int
Ns_ConnReturnOpenFile(Ns_Conn *conn, int status, CONST char *type,
                      FILE *fp, size_t len)
{
    return ReturnOpen(conn, status, type, NULL, fp, -1, len);
}

int
Ns_ConnReturnOpenFd(Ns_Conn *conn, int status, CONST char *type,
                    int fd, size_t len)
{
    return ReturnOpen(conn, status, type, NULL, NULL, fd, len);
}

static int
ReturnOpen(Ns_Conn *conn, int status, CONST char *type, Tcl_Channel chan,
           FILE *fp, int fd, size_t len)
{
    int result;

    Ns_ConnSetTypeHeader(conn, type);
    Ns_ConnSetResponseStatus(conn, status);
    
    if ((chan != NULL || fp != NULL) 
	&& (NsWriterQueue(conn, len, chan, fp, fd, NULL, 0, 0) == NS_OK)) {
	return NS_OK;
    }

    if (chan != NULL) {
        Ns_ConnSetLengthHeader(conn, len);
        result = Ns_ConnSendChannel(conn, chan, len);
    } else if (fp != NULL) {
        Ns_ConnSetLengthHeader(conn, len);
        result = Ns_ConnSendFp(conn, fp, len);
    } else {
        result = ReturnRange(conn, type, fd, NULL, len);
    }

    Ns_ConnClose(conn);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnRange --
 *
 *      Return ranges from an open fd or buffer if specified by
 *      client, otherwise return the entire range.
 *
 * Results:
 *      NS_OK if all data sent, NS_ERROR otherwise
 *
 * Side effects:
 *      May send various HTTP error responses.
 *
 *      Will close the connection.
 *
 *----------------------------------------------------------------------
 */

static int
ReturnRange(Ns_Conn *conn, CONST char *type,
            int fd, CONST void *data, size_t len)
{
    Ns_DString  ds;
    Ns_FileVec  bufs[32];
    int         nbufs = sizeof(bufs) / sizeof(bufs[0]);
    int         rangeCount, result = NS_ERROR;

    Ns_DStringInit(&ds);
    rangeCount = NsConnParseRange(conn, type, fd, data, len,
                                  bufs, &nbufs, &ds);

    /*
     * We are able to handle the following cases via writer:
     * - iovec based requests: all range request up to 32 ranges.
     * - fd based requests: 0 or 1 range requests
     */
    if (fd == -1) {
	int nvbufs;
	struct iovec vbuf[32];

	if (rangeCount == 0) {
	    nvbufs = 1;
	    vbuf[0].iov_base = (void *)data;
	    vbuf[0].iov_len  = len;
	} else {
	    int i;

	    nvbufs = rangeCount;
	    len = 0;
	    for (i = 0; i < rangeCount; i++) {
		vbuf[0].iov_base = (void *)(intptr_t)bufs[0].offset;
		vbuf[0].iov_len  = bufs[0].length;
		len += bufs[0].length;
	    }
	}

	if (NsWriterQueue(conn, len, NULL, NULL, fd, &vbuf[0], nvbufs, 0) == NS_OK) {
	    Ns_DStringFree(&ds);
	    return NS_OK;
	}
    } else if (rangeCount < 2) {
	if (rangeCount == 1) {
	    lseek(fd, bufs[0].offset, SEEK_SET);
	    len = bufs[0].length;
	}
	if (NsWriterQueue(conn, len, NULL, NULL, fd, NULL, 0, 0) == NS_OK) {
	    Ns_DStringFree(&ds);
	    return NS_OK;
	}
    }
    
    if (rangeCount >= 0) {
	if (rangeCount == 0) {
            Ns_ConnSetLengthHeader(conn, len);
            Ns_SetFileVec(bufs, 0, fd, data, 0, len);
            nbufs = 1;
        }
        if ((result = Ns_ConnWriteVData(conn, NULL, 0, NS_CONN_STREAM)) == NS_OK) {
            result = Ns_ConnSendFileVec(conn, bufs, nbufs);
        }
    }
    Ns_DStringFree(&ds);

    return result;
}

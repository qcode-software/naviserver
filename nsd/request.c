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
 * request.c --
 *
 *      Functions that implement the Ns_Request type.
 *
 */

#include "nsd.h"

#define HTTP "HTTP/"

/*
 * Local functions defined in this file.
 */

static void SetUrl(Ns_Request *request, char *url)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static void FreeUrl(Ns_Request *request)
    NS_GNUC_NONNULL(1);

static const char *GetQvalue(const char *str, int *lenPtr)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2);

static const char *GetEncodingFormat(const char *encodingString,
                                     const char *encodingFormat, double *qValue)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(3);

static void RequestCleanupMembers(Ns_Request *request)
    NS_GNUC_NONNULL(1);

/*
 *----------------------------------------------------------------------
 *
 * RequestCleanupMembers --
 *
 *    Frees the members of the provided Ns_Request structure.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Freeing memory.
 *
 *----------------------------------------------------------------------
 */
static void
RequestCleanupMembers(Ns_Request *request)
{
    NS_NONNULL_ASSERT(request != NULL);

    if (request->line != NULL) {
        Ns_Log(Ns_LogRequestDebug, "end %s", request->line);
    }
    ns_free((char *)request->line);
    ns_free((char *)request->method);
    ns_free((char *)request->protocol);
    ns_free((char *)request->host);
    ns_free(request->query);
    FreeUrl(request);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ResetRequest --
 *
 *    Free the Ns_Request members. This function is usually called on
 *    embedded Ns_Request structures, such as these part of the Request
 *    structure.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ResetRequest(Ns_Request *request)
{
    NS_NONNULL_ASSERT(request != NULL);

    /*
     * There is no need to free the full structure, just clean the members and
     * reset it to NULL.
     */
    RequestCleanupMembers(request);
    memset(request, 0, sizeof(Ns_Request));
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_FreeRequest --
 *
 *    Free an Ns_Request structure and all its members.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_FreeRequest(Ns_Request *request)
{
    if (request != NULL) {
        RequestCleanupMembers(request);
        ns_free(request);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseRequests --
 *
 *    Parse a request from the client into an Ns_Request structure.
 *    On success, it fills the following Ns_Request members:
 *      - line
 *      - method
 *      - version
 *      - protocol
 *      - host
 *      - port
 *
 * Results:
 *    NS_OK on success, NS_ERROR on error
 *
 * Side effects:
 *    The request if not NULL is always zero-ed before filled with values
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
Ns_ParseRequest(Ns_Request *request, const char *line)
{
    char       *url, *l, *p;
    Ns_DString  ds;

    NS_NONNULL_ASSERT(line != NULL);

    if (request == NULL) {
        return NS_ERROR;
    }

#if !defined(NDEBUG)
    /*
     * The passed-in line must not contain a newline
     */
    assert(strrchr(line, INTCHAR('\n')) == NULL);
#endif

    memset(request, 0, sizeof(Ns_Request));
    Ns_DStringInit(&ds);

    /*
     * Make a copy of the line to chop up. Make sure it isn't blank.
     */

    Ns_DStringAppend(&ds, line);
    l = Ns_StrTrim(ds.string);
    if (*l == '\0') {
        goto done;
    }

    /*
     * Save the trimmed line for logging purposes.
     */
    request->line = ns_strdup(l);

    Ns_Log(Ns_LogRequestDebug, "begin %s", request->line);

    /*
     * Look for the minimum of method and url.
     *
     * Collect non-space characters as first token.
     */

    url = l;
    while (*url != '\0' && CHARTYPE(space, *url) == 0) {
        ++url;
    }
    if (*url == '\0') {
        goto done;
    }

    /*
     * Mark the end of the first token and remember it as HTTP-method.
     */
    *url++ = '\0';
    request->method = ns_strdup(l);

    /*
     * Skip spaces.
     */
    while (*url != '\0' && CHARTYPE(space, *url) != 0)  {
        ++url;
    }
    if (*url == '\0') {
        goto done;
    }


    /*
     * Look for a valid version. Typically, the HTTP-version number is of the
     * form "HTTP/1.0". However, in HTTP 0.9, the HTTP-version number was not
     * specified.
     */
    request->version = 0.0;

    /*
     * Search from the end for the last space.
     */
    p = strrchr(url, INTCHAR(' '));
    if (likely(p != NULL)) {
        /*
         * We have a final token. Let see, if this an HTTP-version string.
         */
        if (likely(strncmp(p + 1, HTTP, sizeof(HTTP) - 1u) == 0)) {
            /*
             * The HTTP-Version string starts really with HTTP/
             *
             * If strtod fails, version will be set to 0 and the server will
             * treat the connection as if it had no HTTP/n.n keyword.
             */
            *p = '\0';
            p += sizeof(HTTP);
            request->version = strtod(p, NULL);
        } else {
            /*
             * The last token does not have the form of an HTTP-version
             * string. Report result as invalid request.
             */
            goto done;
        }
    } else {
        /*
         * Let us assume, the request is HTTP 0.9, when the url starts with a
         * slash. HTTP 0.9 did not have proxy functionality.
         */
        if (*url != '/') {
            goto done;
        }
    }

    url = Ns_StrTrimRight(url);
    if (*url == '\0') {
        goto done;
    }

    /*
     * Look for a protocol in the URL.
     */
    request->protocol = NULL;
    request->host = NULL;
    request->port = 0u;

    if (*url != '/') {
        p = url;
        while (*p != '\0' && *p != '/' && *p != ':') {
            ++p;
        }
        if (*p == ':') {

            /*
             * Found a protocol - copy it and search for host:port.
             */

            *p++ = '\0';
            request->protocol = ns_strdup(url);
            url = p;
            if ((strlen(url) > 3u) && (*p++ == '/')
                && (*p++ == '/') && (*p != '\0') && (*p != '/')) {
                char *h = p;

                while ((*p != '\0') && (*p != '/')) {
                    p++;
                }
                if (*p == '/') {
                    *p++ = '\0';
                }
                url = p;

                /*
                 * Check for port
                 */
                Ns_HttpParseHost(h, NULL, &p);
                if (p != NULL) {
                    *p++ = '\0';
                    request->port = (unsigned short)strtol(p, NULL, 10);
                }
                request->host = ns_strdup(h);
            }
        }
    }

    SetUrl(request, url);
    Ns_DStringFree(&ds);
    return NS_OK;

 done:

    Ns_Log(Warning, "Ns_ParseRequest <%s> -> ERROR", line);
    Ns_DStringFree(&ds);
    return NS_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SkipUrl --
 *
 *    Return a pointer n elements into the request's url.
 *
 * Results:
 *    The url beginning n elements in.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

const char *
Ns_SkipUrl(const Ns_Request *request, int n)
{
    const char **elements, *result = NULL;
    int          length;

    NS_NONNULL_ASSERT(request != NULL);

    Tcl_SplitList(NULL, request->urlv, &length, &elements);

    if (n <= request->urlc) {
        size_t skip = 0u;

        while (--n >= 0) {
            skip += strlen(elements[n]) + 1u;
        }
        result = (request->url + skip);
    }
    Tcl_Free((char *)elements);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SetRequestUrl --
 *
 *    Set the url in a request structure.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Makes a copy of url.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SetRequestUrl(Ns_Request *request, const char *url)
{
    Ns_DString      ds;

    NS_NONNULL_ASSERT(request != NULL);
    NS_NONNULL_ASSERT(url != NULL);

    FreeUrl(request);
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, url);
    SetUrl(request, ds.string);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeUrl --
 *
 *    Free the url in a request.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeUrl(Ns_Request *request)
{
    NS_NONNULL_ASSERT(request != NULL);

    if (request->url != NULL) {
        ns_free((char *)request->url);
        request->url = NULL;
    }
    if (request->urlv != NULL) {
        ns_free((char *)request->urlv);
        request->urlv = NULL;
        request->urlc = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SetUrl --
 *
 *    Break up a URL and put it in the request.
 *
 * Results:
 *    None.
 *
 * Side effects:
 *    Memory allocated for members.
 *
 *----------------------------------------------------------------------
 */

static void
SetUrl(Ns_Request *request, char *url)
{
    Tcl_DString  ds1, ds2;
    char        *p;
    const char  *encodedPath;
    Tcl_Encoding encoding;

    NS_NONNULL_ASSERT(request != NULL);
    NS_NONNULL_ASSERT(url != NULL);

    Tcl_DStringInit(&ds1);
    Tcl_DStringInit(&ds2);

    /*
     * Look for a query string at the end of the URL.
     */

    p = strchr(url, INTCHAR('?'));
    if (p != NULL) {
        *p++ = '\0';
        if (request->query != NULL) {
            ns_free(request->query);
        }
        if (*p != '\0') {
            request->query = ns_strdup(p);
        }
    }

    /*
     * Decode and normalize the URL (remove ".", "..").
     */
    encodedPath = url;
    encoding = Ns_GetUrlEncoding(NULL);
    p = Ns_UrlPathDecode(&ds1, encodedPath, encoding);
    if (p == NULL) {
        p = url;
    }
    (void)Ns_NormalizeUrl(&ds2, p);
    Tcl_DStringSetLength(&ds1, 0);

    /*
     * Append a trailing slash to the normalized URL if the original URL
     * ended in slash that wasn't also the leading slash.
     */

    while (*url == '/') {
        ++url;
    }
    if (*url != '\0' && url[strlen(url) - 1u] == '/') {
        Tcl_DStringAppend(&ds2, "/", 1);
    }
    request->url = ns_strdup(ds2.string);
    request->url_len = ds2.length;
    Tcl_DStringFree(&ds2);

    /*
     * Build the urlv and set urlc.
     */
    {
        Tcl_Obj *listPtr, *segmentObj;

        listPtr = Tcl_NewListObj(0, NULL);
        Tcl_IncrRefCount(listPtr);
        /*
         * Skip the leading slash.
         */
        encodedPath++;

        while (*encodedPath != '\0') {
            p = strchr(encodedPath, INTCHAR('/'));
            if (p == NULL) {
                break;
            }
            *p = '\0';
            Ns_UrlPathDecode(&ds1, encodedPath, encoding);
            segmentObj = Tcl_NewStringObj(ds1.string, ds1.length);
            Tcl_ListObjAppendElement(NULL, listPtr, segmentObj);
            Tcl_DStringSetLength(&ds1, 0);
            encodedPath = p + 1;
        }
        /*
         * Append last segment if not empty (for compatibility with previous
         * versions).
         */
        if (*encodedPath != '\0') {
            Ns_UrlPathDecode(&ds1, encodedPath, encoding);
            segmentObj = Tcl_NewStringObj(ds1.string, ds1.length);
            Tcl_ListObjAppendElement(NULL, listPtr, segmentObj);
        }

        /*
         * Set request->urlc and request->urlv based on the listPtr.
         */
        Tcl_ListObjLength(NULL, listPtr, &request->urlc);
        request->urlv = ns_strdup(Tcl_GetString(listPtr));
        request->urlv_len = (int)strlen(request->urlv);

        Tcl_DecrRefCount(listPtr);
    }
    Tcl_DStringFree(&ds1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseHeader --
 *
 *    Consume a header line, handling header continuation, placing
 *    results in given set.
 *
 * Results:
 *    NS_OK/NS_ERROR
 *
 * Side effects:
 *    None
 *
 *----------------------------------------------------------------------
 */

Ns_ReturnCode
Ns_ParseHeader(Ns_Set *set, const char *line, Ns_HeaderCaseDisposition disp)
{
    Ns_ReturnCode   status = NS_OK;

    /*
     * Header lines are first checked if they continue a previous
     * header indicated by any preceding white space.  Otherwise,
     * they must be in well form key: value form.
     */

    NS_NONNULL_ASSERT(set != NULL);
    NS_NONNULL_ASSERT(line != NULL);

    if (CHARTYPE(space, *line) != 0) {
        if (Ns_SetSize(set) == 0u) {
            /*
             * Continue before first header.
             */
            status = NS_ERROR;

        } else {
            size_t idx = Ns_SetLast(set);
            while (CHARTYPE(space, *line) != 0) {
                ++line;
            }
            if (*line != '\0') {
                Ns_DString ds;
                char      *value = Ns_SetValue(set, idx);

                Ns_DStringInit(&ds);
                Ns_DStringVarAppend(&ds, value, " ", line, (char *)0L);
                Ns_SetPutValue(set, idx, ds.string);
                Ns_DStringFree(&ds);
            }
        }
    } else {
        char *sep;

        sep = strchr(line, INTCHAR(':'));
        if (sep == NULL) {
            /*
             * Malformed header.
             */
            status = NS_ERROR;

        } else {
            const char *value;
            char       *key;
            size_t      idx;

            *sep = '\0';
            for (value = sep + 1; (*value != '\0') && CHARTYPE(space, *value) != 0; value++) {
                ;
            }
            idx = Ns_SetPut(set, line, value);
            key = Ns_SetKey(set, idx);
            if (disp == ToLower) {
                while (*key != '\0') {
                    if (CHARTYPE(upper, *key) != 0) {
                        *key = CHARCONV(lower, *key);
                    }
                    ++key;
                }
            } else if (disp == ToUpper) {
                while (*key != '\0') {
                    if (CHARTYPE(lower, *key) != 0) {
                        *key = CHARCONV(upper, *key);
                    }
                    ++key;
                }
            }
            *sep = ':';
        }
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetQvalue --
 *
 *      Return the next qvalue string from accept encodings
 *
 * Results:
 *      string, setting lengthPtr; or NULL, if no or invalie
 *      qvalue provided
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */
static const char *
GetQvalue(const char *str, int *lenPtr) {
    const char *resultString = NULL;

    NS_NONNULL_ASSERT(str != NULL);
    NS_NONNULL_ASSERT(lenPtr != NULL);

    for (;;) {
        for (; *str == ' '; str++) {
            ;
        }
        if (*str != ';') {
            break;
        }
        for (str ++; *str == ' '; str++) {
            ;
        }
        if (*str != 'q') {
            break;
        }
        for (str ++; *str == ' '; str++) {
            ;
        }
        if (*str != '=') {
            break;
        }
        for (str ++; *str == ' '; str++) {
            ;
        }
        if (CHARTYPE(digit,*str) == 0) {
            break;
        }

        resultString = str;
        str++;
        if (*str == '.') {
            /*
             * Looks like a floating point number; RFC2612 allows up to
             * three digits after the comma.
             */
            str ++;
            if (CHARTYPE(digit, *str) != 0) {
                str++;
                if (CHARTYPE(digit, *str) != 0) {
                    str++;
                    if (CHARTYPE(digit, *str) != 0) {
                        str++;
                    }
                }
            }
        }
        /*
         * "str" should point to a valid terminator of the number.
         */
        if (*str == ' ' || *str == ',' || *str == ';' || *str == '\0') {
            *lenPtr = (int)(str - resultString);
        } else {
            resultString = NULL;
        }
        break;
    }
    return resultString;
}



/*
 *----------------------------------------------------------------------
 *
 * GetEncodingFormat --
 *
 *      Search on encodingString (header field accept-encodings) for
 *      encodingFormat (e.g. "gzip", "identy") and return its q value.
 *
 * Results:
 *      On success non-NULL value and the parsed qValue
 *      (when no qvalue is provided then assume qvalue as 1.0);
 *      On failure NULL value qValue set to -1;
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static const char *
GetEncodingFormat(const char *encodingString, const char *encodingFormat, double *qValue) {
    const char *encodingStr;

    NS_NONNULL_ASSERT(encodingString != NULL);
    NS_NONNULL_ASSERT(encodingFormat != NULL);
    NS_NONNULL_ASSERT(qValue != NULL);

    encodingStr = strstr(encodingString, encodingFormat);

    if (encodingStr != NULL) {
        int         len = 0;
        const char *qValueString = GetQvalue(encodingStr + strlen(encodingFormat), &len);

        if (qValueString != NULL) {
            *qValue = strtod(qValueString, NULL);
        } else {
            *qValue = 1.0;
        }

    } else {
        *qValue = -1.0;
    }
    return encodingStr;
}


/*
 *----------------------------------------------------------------------
 *
 * CompressAllow --
 *
 *      Handle quality values expressed expicitly (for gzip or brotli) in the
 *      header fields. Respect cases, where compression is forbidden via
 *      identy or default rules.
 *
 * Results:
 *      boolean
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static bool
CompressAllow(double compressQvalue, double identityQvalue, double starQvalue)
{
    bool result;

    if (compressQvalue > 0.999) {
        /*
         * Compress qvalue 1, use it, nothing else can be higher, so it is
         * allowed.
         */
        result = NS_TRUE;
    } else if (compressQvalue < 0.0009) {
        /*
         * Compress qvalue 0, forbid this kind of compressions
         */
        result = NS_FALSE;
    } else {
        /*
         * A middle compress qvalue was specified, compare it with identity
         * and default.
         */
        if (identityQvalue >=- 1.0) {
            /*
             * The compression format is allowed, when the compression qvalue
             * is larger than identity.
             */
            result = (compressQvalue >= identityQvalue);
        } else if (starQvalue >= -1.0) {
            /*
             * gzip is used, when gzip qvalue is larger than default
             */
            result = (compressQvalue >= starQvalue);
        } else {
            /*
             * Accept the low qvalue due to lack of alternatives
             */
            result = NS_TRUE;
        }
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * NsParseAcceptEncoding --
 *
 *      Parse the accept-encoding line and return whether gzip
 *      encoding is accepted or not.
 *
 * Results:
 *      The result is passed back in the last two arguments.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
void
NsParseAcceptEncoding(double version, const char *hdr, bool *gzipAcceptPtr, bool *brotliAcceptPtr)
{
    double      gzipQvalue = -1.0, brotliQvalue = -1, starQvalue = -1.0, identityQvalue = -1.0;
    bool        gzipAccept, brotliAccept;
    const char *gzipFormat, *brotliFormat, *starFormat;

    NS_NONNULL_ASSERT(hdr != NULL);
    NS_NONNULL_ASSERT(gzipAcceptPtr != NULL);
    NS_NONNULL_ASSERT(brotliAcceptPtr != NULL);

    gzipFormat    = GetEncodingFormat(hdr, "gzip", &gzipQvalue);
    brotliFormat  = GetEncodingFormat(hdr, "br", &brotliQvalue);
    starFormat    = GetEncodingFormat(hdr, "*", &starQvalue);
    (void)GetEncodingFormat(hdr, "identity", &identityQvalue);

    //fprintf(stderr, "hdr line <%s> gzipFormat <%s> brotliFormat <%s>\n", hdr, gzipFormat, brotliFormat);
    if ((gzipFormat != NULL) || (brotliFormat != NULL)) {
        gzipAccept   = CompressAllow(gzipQvalue, identityQvalue, starQvalue);
        brotliAccept = CompressAllow(brotliQvalue, identityQvalue, starQvalue);
    } else if (starFormat != NULL) {
        /*
         * No compress format was specified, star matches everything, so as
         * well the compression formats.
         */
        if (starQvalue < 0.0009) {
            /*
             * The low "*" qvalue forbids the compression formats.
             */
            gzipAccept = NS_FALSE;
        } else if (identityQvalue >= -1) {
            /*
             * Star qvalue allows gzip in HTTP/1.1, when it is larger
             * than identity.
             */
            gzipAccept = (starQvalue >= identityQvalue) && (version >= 1.1);
        } else {
            /*
             * No identity was specified, assume compression format is matched
             * with "*" in HTTP/1.1
             */
            gzipAccept = (version >= 1.1);
        }
        /*
         * The implicit rules are the same for gzip and brotli.
         */
        brotliAccept = gzipAccept;
    } else {
        gzipAccept   = NS_FALSE;
        brotliAccept = NS_FALSE;
    }
    *gzipAcceptPtr   = gzipAccept;
    *brotliAcceptPtr = brotliAccept;
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

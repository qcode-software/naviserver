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
 * sockfile.c --
 *
 *      Use the native OS sendfile-like implementation to send a file
 *      to an Ns_Sock if possible. Otherwise just use read/write etc.
 */

#include "nsd.h"


#ifdef _WIN32
#include <io.h>
ssize_t pread(int fd, char *buf, size_t count, off_t offset);
#endif

/*
 * Local functions defined in this file
 */

static ssize_t SendFd(Ns_Sock *sock, int fd, off_t offset, size_t length,
                      const Ns_Time *timeoutPtr, unsigned int flags,
                      Ns_DriverSendProc *sendProc);

static Ns_DriverSendProc SendBufs;



/*
 *----------------------------------------------------------------------
 *
 * Ns_SetFileVec --
 *
 *      Fill in the fields of an Ns_FileVec structure, handling both
 *      file-based and data-based offsets.
 *
 * Results:
 *      The given length.
 *
 * Side effects:
 *      None.
*
 *----------------------------------------------------------------------
 */

size_t
Ns_SetFileVec(Ns_FileVec *bufs, int i,  int fd, const void *data,
              off_t offset, size_t length)
{
    bufs[i].fd = fd;
    bufs[i].length = length;

    if (fd != NS_INVALID_FD) {
        bufs[i].offset = offset;
    } else {
        bufs[i].offset = ((off_t)(intptr_t) data) + offset;
    }
    return length;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ResetFileVec --
 *
 *      Zero the bufs which have had their data sent and adjust
 *      the remainder.
 *
 * Results:
 *      Index of first buf to send.
 *
 * Side effects:
 *      Updates offset and length members.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ResetFileVec(Ns_FileVec *bufs, int nbufs, size_t sent)
{
    int          i;

    for (i = 0; i < nbufs && sent > 0u; i++) {
        int    fd     = bufs[i].fd;
        size_t length = bufs[i].length;
        off_t  offset = bufs[i].offset;

        if (length > 0u) {
            if (sent >= length) {
                sent -= length;
                (void) Ns_SetFileVec(bufs, i, fd, NULL, 0, 0u);
            } else {
                (void) Ns_SetFileVec(bufs, i, fd, NULL,
                                     offset + (off_t)sent, length - sent);
                break;
            }
        }
    }

    return i;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockSendFileBufs --
 *
 *      Send a vector of buffers/files on a non-blocking socket.
 *      Not all data may be sent.
 *
 * Results:
 *      Number of bytes sent, -1 on error or timeout.
 *
 * Side effects:
 *      May block reading data from disk.
 *
 *      May wait for given timeout if first attempt to write to socket
 *      would block.
 *
 *----------------------------------------------------------------------
 */

#ifdef HAVE_SYS_SENDFILE_H  /* Linux */

#include <sys/sendfile.h>

static ssize_t
Sendfile(Ns_Sock *sock, int fd, off_t offset, size_t toSend, const Ns_Time *timeoutPtr);

ssize_t
Ns_SockSendFileBufs(Ns_Sock *sock, const Ns_FileVec *bufs, int nbufs,
                    const Ns_Time *timeoutPtr, unsigned int UNUSED(flags))
{

    ssize_t       sent, towrite, nwrote;
    struct iovec  sbufs[UIO_MAXIOV];
    int           nsbufs = 0, i;

    towrite = nwrote = 0;
    sent = -1;

    for (i = 0; i < nbufs; i++) {
        size_t   length = bufs[i].length;
        off_t    offset = bufs[i].offset;
        int      fd     = bufs[i].fd;

        if (length < 1) {
            continue;
        }

        towrite += (ssize_t)length;

        if (fd < 0) {
            /*
             * Coalesce runs of memory bufs into fixed-sized iovec.
             */
            (void) Ns_SetVec(sbufs, nsbufs++, INT2PTR(offset), length);
        }

        /* Flush pending memory bufs. */

        if ((fd < 0
             && (nsbufs == UIO_MAXIOV || i == nbufs -1))
            || (fd >= 0
                && nsbufs > 0)) {

            sent = NsDriverSend((Sock *)sock, sbufs, nsbufs, 0u);

            nsbufs = 0;
            if (sent > 0) {
                nwrote += sent;
            }
            if (sent < towrite) {
                break;
            }
        }

        /* Send a single file range. */

        if (fd >= 0) {
            sent = Sendfile(sock, fd, offset, length, timeoutPtr);
            if (sent > 0) {
                nwrote += sent;
            }
            if (sent < towrite) {
                break;
            }
            towrite = 0;
        }
    }

    return (nwrote != 0) ? nwrote : sent;
}

static ssize_t
Sendfile(Ns_Sock *sock, int fd, off_t offset, size_t toSend, const Ns_Time *timeoutPtr)
{
    ssize_t sent;

    sent = sendfile(sock->sock, fd, &offset, toSend);

    if (sent == -1) {
        if ((errno == EAGAIN) || (errno == NS_EWOULDBLOCK)) {
            if (Ns_SockTimedWait(sock->sock, NS_SOCK_WRITE, timeoutPtr) == NS_OK) {
                sent = sendfile(sock->sock, fd, &offset, toSend);
            }
        } else if ((errno == EINVAL) || (errno == ENOSYS)) {
            /*
             * File system does not support sendfile?
             */
            sent = SendFd(sock, fd, offset, toSend, timeoutPtr, 0, SendBufs);
        }
    }

    return sent;
}

#else /* Default implementation */

ssize_t
Ns_SockSendFileBufs(Ns_Sock *sock, const Ns_FileVec *bufs, int nbufs,
                    const Ns_Time *timeoutPtr, unsigned int flags)
{
    return NsSockSendFileBufsIndirect(sock, bufs, nbufs, timeoutPtr, flags,
                                      SendBufs);
}

#endif


/*
 *----------------------------------------------------------------------
 *
 * NsSockSendFileBufsIndirect --
 *
 *      Send a vector of buffers/files on a non-blocking socket using
 *      the given callback for socekt IO.  Not all data may be sent.
 *
 * Results:
 *      Number of bytes sent, -1 on error or timeout.
 *
 * Side effects:
 *      May block reading data from disk.
 *
 *      May wait for given timeout if first attempt to write to socket
 *      would block.
 *
 *----------------------------------------------------------------------
 */

ssize_t
NsSockSendFileBufsIndirect(Ns_Sock *sock, const Ns_FileVec *bufs, int nbufs,
                           const Ns_Time *timeoutPtr, unsigned int flags,
                           Ns_DriverSendProc *sendProc)
{
    ssize_t       sent, nwrote;
    struct iovec  iov;
    int           i;

    nwrote = 0;
    sent = -1;

    for (i = 0; i < nbufs; i++) {
        size_t  toSend = bufs[i].length;
        int     fd     = bufs[i].fd;
        off_t   offset = bufs[i].offset;

        if (toSend > 0u) {
            if (fd < 0) {
                (void) Ns_SetVec(&iov, 0, INT2PTR(offset), toSend);
                sent = (*sendProc)(sock, &iov, 1, timeoutPtr, flags);
            } else {
                sent = SendFd(sock, fd, offset, toSend,
                              timeoutPtr, flags, sendProc);
            }
            if (sent > 0) {
                nwrote += sent;
            }
            if (sent != (ssize_t)toSend) {
                break;
            }
        }
    }

    return nwrote > 0 ? nwrote : sent;
}


/*
 *----------------------------------------------------------------------
 *
 * pread --
 *
 *      The pread() function is used in SendFd to read N bytes from a
 *      stream/file from a given offset point. It is natively present
 *      on linux/unix but not on windows.
 *
 * Results:
 *      On success, number of bytes read, -1 on error
 *
 * Side effects:
 *      Advancing the file pointer.
 *
 *----------------------------------------------------------------------
 */
#ifdef _WIN32


ssize_t pread(int fd, char *buf, size_t count, off_t offset)
{
    HANDLE   fh = (HANDLE)_get_osfhandle(fd);
    ssize_t  result;

    if (fh == INVALID_HANDLE_VALUE) {
        errno = EBADF;
        result = -1;
    } else {
        DWORD      ret, c = (DWORD)count;
        OVERLAPPED overlapped = { 0u };

        overlapped.Offset = (DWORD)offset;
        overlapped.OffsetHigh = (DWORD)(((uint64_t)offset) >> 32);

        if (ReadFile(fh, buf, c, &ret, &overlapped) == FALSE) {
            result = -1;
        } else {
            result = (ssize_t)ret;
        }
    }

    return result;
}
#endif



/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCork --
 *
 *      Turn TCP_CORK state on or off, if supported by the OS. The
 *      function tracks the cork state in the socket structure to be
 *      able to handle nesting calls.
 *
 * Results:
 *      success (NS_TRUE or NS_FALSE)
 *
 * Side effects:
 *      Switch TCP send state, potentially update sockPtr->flags
 *
 *----------------------------------------------------------------------
 */
bool
Ns_SockCork(const Ns_Sock *sock, bool cork)
{
    bool success = NS_FALSE;
#if defined(TCP_CORK) || defined(UDP_CORK)
    Sock *sockPtr = (Sock *)sock;
    int corkInt = (int)cork;

    assert(sock != NULL);

    /* fprintf(stderr, "### Ns_SockCork sock %d %d\n", sockPtr->sock, cork); */

    if (cork && (sockPtr->flags & NS_CONN_SOCK_CORKED) != 0u) {
        /*
         * Don't cork an already corked connection.
         */
    } else if (!cork && (sockPtr->flags & NS_CONN_SOCK_CORKED) == 0u) {
        /*
         * Don't uncork an already uncorked connection.
         */
        Ns_Log(Error, "socket: trying to uncork an uncorked socket %d",
               sockPtr->sock);
    } else {
        /*
         * The cork state changes, try to alter the socket options, unless the
         * socket is already closed (don't complain in such cases to the
         * error.log).
         */
# if defined(TCP_CORK)
        if ((sockPtr->drvPtr->opts & NS_DRIVER_UDP) == 0) {
            if ((sockPtr->sock != NS_INVALID_SOCKET)
                && (setsockopt(sockPtr->sock, IPPROTO_TCP, TCP_CORK, &corkInt, sizeof(corkInt)) == -1)
                ) {
                Ns_Log(Error, "socket(%d): setsockopt(TCP_CORK) %d: %s",
                       sockPtr->sock, corkInt, ns_sockstrerror(ns_sockerrno));
            } else {
                success = NS_TRUE;
            }
        }
# endif
# if defined(UDP_CORK)
        if ((sockPtr->drvPtr->opts & NS_DRIVER_UDP) != 0) {
            if ((sockPtr->sock != NS_INVALID_SOCKET)
                && setsockopt(sockPtr->sock, IPPROTO_UDP, UDP_CORK, &corkInt, sizeof(corkInt)) == -1)
                ) {
                Ns_Log(Error, "socket(%d): setsockopt(UDP_CORK) %d: %s",
                       sockPtr->sock, corkInt, ns_sockstrerror(ns_sockerrno));
            } else {
                success = NS_TRUE;
            }
        }
# endif
        if (success) {
            /*
             * On success, update the corked flag.
             */
            if (cork) {
                sockPtr->flags |= NS_CONN_SOCK_CORKED;
            } else {
                sockPtr->flags &= ~NS_CONN_SOCK_CORKED;
            }
        }
    }
#else
    (void)cork;
    (void)sock;
#endif
    return success;
}


/*
 *----------------------------------------------------------------------
 *
 * SendFd --
 *
 *      Send the given range of bytes from fd to sock using the given
 *      callback. Not all data may be sent.
 *
 * Results:
 *      Number of bytes sent, -1 on error or timeout.
 *
 * Side effects:
 *      May block reading data from disk.
 *
 *      May wait for given timeout if first attempt to write to socket
 *      would block.
 *
 *----------------------------------------------------------------------
 */

static ssize_t
SendFd(Ns_Sock *sock, int fd, off_t offset, size_t length,
       const Ns_Time *timeoutPtr, unsigned int flags,
       Ns_DriverSendProc *sendProc)
{
    char          buf[16384];
    struct iovec  iov;
    ssize_t       nwrote = 0, toRead = (ssize_t)length, result;
    bool          decork;

    // Ns_Log(Notice, "SendFd offset %ld length %lu", offset, length);

    decork = Ns_SockCork(sock, NS_TRUE);
    while (toRead > 0) {
        ssize_t sent, nread;

        nread = pread(fd, buf, MIN((size_t)toRead, sizeof(buf)), offset);

        // Ns_Log(Notice, "... pread toread %lu offset %ld => read %ld", MIN((size_t)toRead, sizeof(buf)), offset, nread);
        if (nread <= 0) {
            break;
        }
        toRead -= nread;
        offset += (off_t)nread;

        (void) Ns_SetVec(&iov, 0, buf, (size_t)nread);
        sent = (*sendProc)(sock, &iov, 1, timeoutPtr, flags);
        if (sent > 0) {
            nwrote += sent;
        }

        if (sent != nread) {
            break;
        }
    }

    if (decork) {
        (void) Ns_SockCork(sock, NS_FALSE);
    }

    if (nwrote > 0) {
        result = nwrote;
    } else {
        result = -1;
    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * SendBufs --
 *
 *      Implements the driver send interface to send bufs directly
 *      to a OS socket.
 *
 * Results:
 *      Number of bytes sent, -1 on error or timeout.
 *
 * Side effects:
 *      See Ns_SockSendBufs.
 *
 *----------------------------------------------------------------------
 */

static ssize_t
SendBufs(Ns_Sock *sock, const struct iovec *bufs, int nbufs,
         const Ns_Time *timeoutPtr, unsigned int flags)
{
    return Ns_SockSendBufs(sock, bufs, nbufs, timeoutPtr, flags);
}

/*
 * Local Variables:
 * mode: c
 * c-basic-offset: 4
 * fill-column: 78
 * indent-tabs-mode: nil
 * End:
 */

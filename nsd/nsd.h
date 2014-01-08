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

#ifndef NSD_H
#define NSD_H

#include "ns.h"

/*
 * Constants
 */

#define NS_CONFIG_PARAMETERS           "ns/parameters"
#define NS_CONFIG_THREADS              "ns/threads"

/*
 * Various ADP option bits.
 */

#define ADP_SAFE                       0x01    /* Use Tcl_SafeEval for ADP */
#define ADP_SINGLE                     0x02    /* Combine blocks into a single script */
#define ADP_DEBUG                      0x04    /* Enable debugging */
#define ADP_EXPIRE                     0x08    /* Send Expires: now header on output */
#define ADP_CACHE                      0x10    /* Enable output caching */
#define ADP_TRACE                      0x20    /* Trace execution */
#define ADP_DETAIL                     0x80    /* Log connection details on error */
#define ADP_STRICT                     0x100   /* Strict error handling */
#define ADP_DISPLAY                    0x200   /* Display error messages in output stream */
#define ADP_TRIM                       0x400   /* Display error messages in output stream */
#define ADP_FLUSHED                    0x800   /* Some output has been sent */
#define ADP_ERRLOGGED                  0x1000  /* Error message has already been logged */
#define ADP_AUTOABORT                  0x2000  /* Raise abort on flush error */
#define ADP_ADPFILE                    0x4000  /* Object to evaluate is a file */
#define ADP_STREAM                     0x8000  /* Enable ADP streaming */
#define ADP_TCLFILE                    0x10000 /* Object to evaluate is a Tcl file */

#define ADP_OK                         0
#define ADP_BREAK                      1
#define ADP_ABORT                      2
#define ADP_RETURN                     3
#define ADP_TIMEOUT                    4

#define NSD_STRIP_WWW                  1
#define NSD_STRIP_PORT                 2

#define MAX_URLSPACES                  16

#define CONN_TCLFORM                   1  /* Query form set is registered for interp */
#define CONN_TCLHDRS                   2  /* Input headers set is registered for interp */
#define CONN_TCLOUTHDRS                4  /* Output headers set is registered for interp */
#define CONN_TCLAUTH                   8  /* 'auth' headers set is registered for interp */
#define CONN_TCLHTTP                   16  /* HTTP headers requested by ns_headers */


/*
 * For the time being, don't try to be very clever
 * and define (platform-neutral) just those two modes
 * for mapping the files.
 * Although the underlying implementation(s) can do
 * much more, we really need only one (read-maps) now.
 */

#ifdef _WIN32
#  define NS_MMAP_READ                 FILE_MAP_READ
#  define NS_MMAP_WRITE                FILE_MAP_WRITE
#else
#  define NS_MMAP_READ                 PROT_READ
#  define NS_MMAP_WRITE                PROT_WRITE
#endif

/*
 * The following is the default text/html content type
 * sent to the browser for html/adp etc. requests.
 */

#define NSD_TEXTHTML                   "text/html"

/*
 * Types definitions.
 */

typedef int bool;

struct Sock;
struct NsServer;

struct _nsconf {
    char *argv0;
    char *nsd;
    char *name;
    char *version;
    char *home;
    char *tmpDir;
    char *config;
    char *build;
    int pid;
    time_t boot_t;
    char hostname[255];
    char address[16];
    int shutdowntimeout;
    int backlog;

    /*
     * Slot IDs for socket local storage.
     */

    int nextSlsId;

    /*
     * The following table holds the configured virtual servers.
     * The dstring maintains a Tcl list of the names.
     */

    Tcl_HashTable servertable;
    Tcl_DString servers;
    char *defaultServer;

    /*
     * The following table holds config section sets from
     * the config file.
     */

    Tcl_HashTable sections;

    /*
     * The following struct maintains server state.
     */

    struct {
        Ns_Mutex lock;
        Ns_Cond cond;
        int started;
        int stopping;
    } state;

    struct {
        int jobsperthread;
        int maxelapsed;
    } sched;

#ifdef _WIN32
    struct {
        bool checkexit;
    } exec;
#endif

    struct {
        char *sharedlibrary;
        char *version;
        bool lockoninit;
    } tcl;

    struct {
        int jobsperthread;
        int timeout;
    } job;
};

NS_EXTERN struct _nsconf nsconf;

/*
 * The following structure tracks a memory-mapped file
 * in a platform-neutral way.
 */

typedef struct FileMap {
    char *addr;                 /* Mapped to this virtual address */
    int size;                   /* Size of the mapped region */
    int handle;                 /* OS handle of the opened/mapped file */
    void *mapobj;               /* Mapping object (Win32 only) */
} FileMap;

/*
 * The following structure maintains writer socket
 */
typedef struct WriterSock {
    struct WriterSock   *nextPtr;
    struct Sock         *sockPtr;
    struct SpoolerQueue *queuePtr; 
    struct Conn         *connPtr;
    int                  status;
    int                  err;
    int                  refCount;
    int                  keep;
    Tcl_WideInt          nsent;
    size_t               size;
    unsigned int         flags;
    int                  streaming;
    NS_SOCKET            fd;
    char                 *headerString;
    
    union {
    struct {
	struct iovec      *bufs;                 /* incoming bufs to be sent */
	int                nbufs;
	int                bufIdx; 
	struct iovec       sbufs[UIO_SMALLIOV];  /* scratch bufs for handling partial sends */
	int                nsbufs;
	int                sbufIdx;
	struct iovec       preallocated_bufs[UIO_SMALLIOV];
	struct FileMap     fmap;
    } mem;

    struct {
	size_t             maxsize;
	size_t             bufsize;
	off_t              bufoffset;
	Tcl_WideInt        toread;
	unsigned char     *buf;
	Ns_Mutex           fdlock;
    } file;
    };

    char              *clientData;
    Ns_Time            startTime;

} WriterSock;

/*
 * The following structure maintains a queue of sockets for
 * each writer or spooler thread
 */

typedef struct SpoolerQueue {
    struct SpoolerQueue *nextPtr;
    void                *sockPtr;     /* List of submitted socket structures */
    void                *curPtr;      /* List of processed socket structures */
    NS_SOCKET            pipe[2];     /* Trigger to wakeup WriterThread/SpoolerThread */
    Ns_Mutex             lock;        /* Lock around spooled list */
    Ns_Cond              cond;        /* Cond for stopped flag */
    Ns_Thread            thread;      /* Running WriterThread/Spoolerthread */
    int                  stopped;     /* Flag to indicate thread stopped */
    int                  shutdown;    /* Flag to indicate shutdown */
    int                  id;          /* Queue id */
    int                  queuesize;   /* Number of active sockets in the queue */
    CONST char          *threadname;  /* name of the thread working on this queue */
} SpoolerQueue;


/*
 * The following structure maintains an ADP call frame.
 */

typedef struct AdpFrame {
    struct AdpFrame   *prevPtr;
    unsigned short     line;
    unsigned short     objc;
    time_t    	       mtime;
    off_t     	       size;
    Tcl_Obj	      *ident;
    Tcl_Obj          **objv;
    char	      *savecwd;
    char	      *file;
    int                flags;
    Ns_DString         cwdbuf;
    Tcl_DString	      *outputPtr;
} AdpFrame;


/*
 * The following structure defines blocks of ADP.  The
 * len pointer is an array of ints with positive values
 * indicating text to copy and negative values indicating
 * scripts to evaluate.  The text and script chars are
 * packed together without null char separators starting
 * at base.  The len data is stored at the end of the
 * text dstring when parsing is complete.
 */

typedef struct AdpCode {
    int		nblocks;
    int		nscripts;
    int	       *len;
    int	       *line;
    Tcl_DString text;
} AdpCode;

#define AdpCodeLen(cp,i)	((cp)->len[(i)])
#define AdpCodeLine(cp,i)	((cp)->line[(i)])
#define AdpCodeText(cp)		((cp)->text.string)
#define AdpCodeBlocks(cp)	((cp)->nblocks)
#define AdpCodeScripts(cp)	((cp)->nscripts)


/*
 * The following structure defines the entire request
 * including HTTP request line, headers, and content.
 */

typedef struct Request {
    struct Request *nextPtr;    /* Next on free list */
    Ns_Request request;         /* Parsed request line */
    Ns_Set *headers;            /* Input headers */
    Ns_Set *auth;               /* Auth user/password and parameters */
    char peer[16];              /* Client peer address */
    int port;                   /* Client peer port */

    /*
     * The following pointers are used to access the
     * buffer contents after the read-ahead is complete.
     */

    char *next;                 /* Next read offset */
    char *content;              /* Start of content */
    size_t length;              /* Length of content */
    size_t contentLength;       /* Provided content length */
    size_t avail;               /* Bytes avail in buffer */
    int leadblanks;             /* Number of leading blank lines read */

   /*
    * The following block is for chunked encodings
    */
    Tcl_WideInt expectedLength; /* Provided expected length */
    Tcl_WideInt chunkStartOff;  /* Offset pointing to start of chunk to be parsed */
    Tcl_WideInt chunkWriteOff;  /* Offset pointing to position were to write chunk */

    /*
     * The following offsets are used to manage
     * the buffer read-ahead process.
     */

    off_t woff;                   /* Next write buffer offset */
    off_t roff;                   /* Next read buffer offset */
    off_t coff;                   /* Content buffer offset */
    Tcl_DString buffer;         /* Request and content buffer */

} Request;

/*
 * The following structure maitains data for each instance of
 * a driver initialized with Ns_DriverInit.
 */

typedef struct _DrvSpooler {
    int threads;               /* Number of spooler threads to run */
    Ns_Mutex lock;             /* Lock around spooler queue */
    SpoolerQueue *firstPtr;    /* Spooler thread queue */
    SpoolerQueue *curPtr;      /* Current spooler thread */
} DrvSpooler;

typedef struct _DrvWriter {
    int threads;               /* Number of writer threads to run */
    int maxsize;               /* Max content size to use writer thread */
    int bufsize;               /* Size of the output buffer */
    int streaming;             /* Activate writer for HTML streaming */
    Ns_Mutex lock;             /* Lock around writer queues */
    SpoolerQueue *firstPtr;    /* List of writer threads */
    SpoolerQueue *curPtr;      /* Current writer thread */
} DrvWriter;

typedef struct Driver {

    /*
     * Visible in Ns_Driver.
     */

    void *arg;                          /* Driver callback data */
    char *server;                       /* Virtual server name */
    char *module;                       /* Driver module */
    char *name;                         /* Driver name */
    char *location;                     /* Location, e.g, "http://foo:9090" */
    char *address;                      /* Address in location, e.g. "foo" */
    char *protocol;                     /* Protocol in location, e.g, "http" */
    int   sendwait;                     /* send() I/O timeout */
    int   recvwait;                     /* recv() I/O timeout */
    int   bufsize;                      /* Conn bufsize (0 for SSL) */

    /*
     * Private to Driver.
     */

    struct Driver         *nextPtr;     /* Next in list of drivers */
    struct NsServer       *servPtr;     /* Driver virtual server */
    Ns_DriverListenProc   *listenProc;
    Ns_DriverAcceptProc   *acceptProc;
    Ns_DriverRecvProc     *recvProc;
    Ns_DriverSendProc     *sendProc;
    Ns_DriverSendFileProc *sendFileProc; /* Optional - optimize direct file send. */
    Ns_DriverKeepProc     *keepProc;
    Ns_DriverRequestProc  *requestProc;
    Ns_DriverCloseProc    *closeProc;
    int opts;                           /* NS_DRIVER_* options */
    int closewait;                      /* Graceful close timeout */
    int keepwait;                       /* Keepalive timeout */
    int keepmaxdownloadsize;            /* When set, allow keepalive only for download requests up to this size */
    int keepmaxuploadsize;              /* When set, allow keepalive only for upload requests up to this size */
    NS_SOCKET sock;                     /* Listening socket */
    int pidx;                           /* poll() index */
    char *bindaddr;                     /* Numerical listen address */
    int port;                           /* Port in location */
    int backlog;                        /* listen() backlog */
    Tcl_WideInt maxinput;               /* Maximum request bytes to read */
    Tcl_WideInt maxupload;              /* Uploads that exceed will go into temp file without parsing */
    char *uploadpath;                   /* Path where uploaded files will be spooled */
    int maxline;                        /* Maximum request line size */
    int maxheaders;                     /* Maximum number of request headers */
    Tcl_WideInt readahead;              /* Maximum request size in memory */
    int queuesize;                      /* Current number of sockets in the queue */
    int maxqueuesize;                   /* Maximum number of sockets in the queue */
    int acceptsize;                     /* Number requests to accept at once */
    int loggingFlags;                   /* Logging control flags */

    int flags;                          /* Driver state flags. */
    Ns_Thread thread;                   /* Thread id to join on shutdown. */
    Ns_Mutex lock;                      /* Lock to protect lists below. */
    Ns_Cond cond;                       /* Cond to signal reader threads,
                                         * driver query, startup, and shutdown. */
    NS_SOCKET trigger[2];               /* Wakeup trigger pipe. */

    struct Sock *sockPtr;               /* Free list of Sock structures */
    struct Sock *closePtr;              /* First conn ready for graceful close */

    DrvSpooler spooler;                 /* Tracks upload spooler threads */
    DrvWriter  writer;                  /* Tracks writer threads */

} Driver;

/*
 * The following structure maintains a socket to a
 * connected client.  The socket is used to maintain state
 * during request read-ahead before connection processing
 * and keepalive after connection processing.
 */

typedef struct Sock {

    /*
     * Visible in Ns_Sock.
     */

    struct Driver      *drvPtr;
    NS_SOCKET           sock;
    struct sockaddr_in  sa;              /* Actual peer address */
    void               *arg;             /* Driver context. */

    /*
     * Private to Sock.
     */

    struct Sock        *nextPtr;
    struct NsServer    *servPtr;

    char               *location;
    int                 keep;
    int                 pidx;            /* poll() index */
    int                 flags;           /* state flags used by driver */
    Ns_Time             timeout;
    Request            *reqPtr;

    Ns_Time             acceptTime;

    int                 tfd;             /* file descriptor with request contents */
    char               *taddr;           /* mmap-ed temporary file */
    size_t              tsize;           /* size of mmap region */
    char               *tfile;           /* name of regular temporary file */

    void               *sls[1];          /* Slots for sls storage */

} Sock;

/*
 * The following structure maintains data from an
 * updated form file.
 */

typedef struct FormFile {
    Ns_Set *hdrs;
    off_t   off;
    size_t  len;
} FormFile;

/*
 * The following structure defines per-request limits.
 */

typedef struct NsLimits {
    char            *name;
    unsigned int     maxrun;    /* Max conns to run at once */
    unsigned int     maxwait;   /* Max conns waiting to run before being dropped */
    size_t	     maxupload; /* Max data accepted */
    int              timeout;   /* Seconds allowed for conn to complete */

    Ns_Mutex         lock;      /* Lock for state and stats */

    struct {
        unsigned int nrunning;  /* Conns currently running */
        unsigned int nwaiting;  /* Conns waiting to run */
    } state;

    struct {
        unsigned int ndropped;  /* Drops due to .. */
        unsigned int noverflow; /* Max upload exceeded */
        unsigned int ntimeout;  /* Timeout exceeded */
    } stats;

} NsLimits;

/*
 * The following structure maintains state for a connection
 * being processed.
 */

typedef struct Conn {

    /*
     * Visible in an Ns_Conn:
     */

    Ns_Request *request;

    Ns_Set *headers;
    Ns_Set *outputheaders;
    Ns_Set *auth;

    size_t contentLength;
    int flags;

    /*
     * Visible only in a Conn:
     */

    struct Conn *prevPtr;
    struct Conn *nextPtr;
    struct Sock *sockPtr;

    NsLimits *limitsPtr; /* Per-connection limits */
    Ns_Time   timeout;   /* Absolute timeout (startTime + limit) */

    /*
     * The following are copied from sockPtr so they're valid
     * after the connection is closed (e.g., within traces).
     */

    char *server;
    char *location;
    char *clientData;

    struct Request  *reqPtr;
    struct ConnPool *poolPtr;
    struct Driver   *drvPtr;

    int id;
    char idstr[16];

    Ns_Time acceptTime;          /* time stamp, when the request was accepted */
    Ns_Time requestQueueTime;    /* time stamp, when the request was queued */
    Ns_Time requestDequeueTime;  /* time stamp, when the request was dequeued */
    Ns_Time filterDoneTime;      /* time stamp, after filters */

    Ns_Time acceptTimeSpan;
    Ns_Time queueTimeSpan; 
    Ns_Time filterTimeSpan; 
    Ns_Time runTimeSpan;

    struct NsInterp *itPtr;
    struct stat fileInfo;
    struct FileMap fmap;

    Tcl_Encoding outputEncoding;
    Tcl_Encoding urlEncoding;

    Tcl_WideInt nContentSent;
    Tcl_WideInt responseLength;
    int responseStatus;
    int recursionCount;
    int keep;

    int fd;
    WriterSock *streamWriter;

    Ns_CompressStream  stream;
    int requestCompress;
    int compress;

    Ns_Set *query;
    Tcl_HashTable files;
    void *cls[NS_CONN_MAXCLS];

} Conn;


/*
 * The following structure is allocated for each connection thread.
 * The connPtr member is used for connecting threads with the request
 * info. The states if a conn thread are defined via enumeration.
 */
typedef enum {
  connThread_free,
  connThread_initial,
  connThread_warmup,
  connThread_ready,
  connThread_idle,
  connThread_busy,
  connThread_dead
} ConnThreadState;

typedef struct ConnThreadArg {
    struct ConnPool      *poolPtr;
    struct Conn          *connPtr;
    ConnThreadState       state;
    Ns_Cond               cond;        /* Cond for signaling this conn thread */
    Ns_Mutex              lock;
    struct ConnThreadArg *nextPtr;     /* used for the conn thread queue */
} ConnThreadArg;

/*
 * The following structure maintains a connection thread pool.
 */

typedef struct ConnPool {
    char *pool;
    struct ConnPool *nextPtr;
    struct NsServer *servPtr;

    /*
     * The following struct maintains the active and waiting connection
     * queues, the free conn list, the next conn id, and the number
     * of waiting connects.
     */

    struct {
        Conn *freePtr;
	int maxconns;

        struct {
            int   num;
            Conn *firstPtr;
            Conn *lastPtr;
        } wait;

        Ns_Cond  cond;
        Ns_Mutex lock;
        int      lowwatermark;
        int      highwatermark;

    } wqueue;

    /*
     * The following struct maintains the state of the threads.  Min and max
     * threads are determined at startup and then NsQueueConn ensures the
     * current number of threads remains within that range with individual
     * threads waiting no more than the timeout for a connection to
     * arrive.  The number of idle threads is maintained for the benefit of
     * the ns_server command.
     */

    struct {
        unsigned int nextid;
        int min;
        int max;
        int current;
        int idle;
        int timeout;
        int creating;
	Ns_Mutex lock;
    } threads;

    /*
     * The following struct maintains the state of the thread
     * connection queue.  "nextPtr" points to the next available
     * connection thread, "args" keeps the array of all configured
     * connection structs, and "lock" is used for locking this queue.
     */

    struct {
	ConnThreadArg *nextPtr;
	ConnThreadArg *args;
	Ns_Mutex       lock;
    } tqueue;
    
    /*
     * Track "statistics" such as counts or aggregated times.
     */

    struct {
        unsigned long spool;
        unsigned long queued;
	unsigned long processed;
        unsigned long connthreads;
        Ns_Time acceptTime;
	Ns_Time queueTime; 
	Ns_Time filterTime; 
	Ns_Time runTime;
    } stats;

} ConnPool;

/*
 * The following structure is allocated for each virtual server.
 */

typedef struct NsServer {

    char *server;

    /*
     * The following struct maintains the connection pool(s).
     */

    struct {
        Ns_Mutex lock;
        unsigned long nextconnid;
        bool shutdown;
        ConnPool *firstPtr;
        ConnPool *defaultPtr;
        Ns_Thread joinThread;
    } pools;

    /*
     * The following struct maintains various server options.
     */

    struct {
        bool flushcontent;
        bool modsince;
        bool noticedetail;
        int  errorminsize;
        CONST char *realm;
        Ns_HeaderCaseDisposition hdrcase;
    } opts;

    /*
     * Encoding defaults for the server
     */

    struct {

        CONST char    *urlCharset;
        Tcl_Encoding   urlEncoding;

        CONST char    *outputCharset;
        Tcl_Encoding   outputEncoding;

        bool           hackContentTypeP;

    } encoding;

    struct {
        CONST char *serverdir;  /* Virtual server files path */
        CONST char *pagedir;    /* Path to public pages */
        CONST char *pageroot;   /* Absolute path to public pages */
        CONST char **dirv;
        int dirc;
        CONST char *dirproc;
        CONST char *diradp;
        Ns_UrlToFileProc *url2file;
    } fastpath;

    /*
     * The following struct maintains virtual host config.
     */

    struct {
        bool enabled;
        int opts; /* NSD_STRIP_WWW | NSD_STRIP_PORT */
        CONST char *hostprefix;
        int hosthashlevel;
        Ns_ServerRootProc *serverRootProc;
        void *serverRootArg;
        Ns_ConnLocationProc *connLocationProc;
        void *connLocationArg;
        Ns_LocationProc *locationProc; /* Depreciated */
    } vhost;

    /*
     * The following struct maintains request tables.
     */

    struct {
        Ns_RequestAuthorizeProc *authProc;
        Tcl_HashTable redirect;
        Tcl_HashTable proxy;
        Ns_Mutex plock;
    } request;

    /*
     * The following struct maintains filters and traces.
     */

    struct {
        struct Filter *firstFilterPtr;
        struct Trace *firstTracePtr;
        struct Trace *firstCleanupPtr;
        Ns_Mutex lock;
    } filter;

    /*
     * The following array maintains url-specific data.
     */

    struct Junction *urlspace[MAX_URLSPACES];

    /*
     * The following struct maintains the core Tcl config.
     */

    struct {
        char *library;
        struct TclTrace *firstTracePtr;
        struct TclTrace *lastTracePtr;
        char *initfile;
        Ns_RWLock lock;
        char *script;
        int length;
        int epoch;
        Tcl_Obj *modules;
        Tcl_HashTable runTable;
        CONST char **errorLogHeaders;
        Tcl_HashTable caches;
        Ns_Mutex cachelock;

        /*
         * The following tracks synchronization
         * objects which are looked up by name.
         */

        struct {
            Tcl_HashTable mutexTable, csTable, semaTable, condTable, rwTable;
            unsigned int  mutexId, csId, semaId, condId, rwId;
            Ns_Mutex      lock;
        } synch;

    } tcl;

    /*
     * The following struct maintains ADP config,
     * registered tags, and read-only page text.
     */
    /*
     * The following struct maintains ADP config,
     * registered tags, and read-only page text.
     */

    struct {
        int flags;
        int tracesize;
        size_t bufsize;
        size_t cachesize;

        CONST char *errorpage;
        CONST char *startpage;
        CONST char *debuginit;

        Ns_Cond pagecond;
        Ns_Mutex pagelock;
        Tcl_HashTable pages;
        Ns_RWLock taglock;
        Tcl_HashTable tags;

    } adp;

    struct {
        bool enable;    /* on/off */
        int  level;     /* 1-9 */
        int  minsize;   /* min size of response to compress, in bytes */
    } compress;

    /*
     * The following struct maintains the arrays
     * for the nsv commands.
     */

    struct {
        struct Bucket *buckets;
        int nbuckets;
    } nsv;

    /*
     * The following struct maintains detached Tcl
     * channels for the benefit of the ns_chan command.
     */

    struct {
        Ns_Mutex lock;
        Tcl_HashTable table;
    } chans;

} NsServer;

/*
 * The following structure is allocated for each interp.
 */

typedef struct NsInterp {

    Tcl_Interp *interp;
    NsServer   *servPtr;
    int         deleteInterp;  /* Interp should be deleted on next deallocation */
    int         epoch;         /* Run the update script if != to server epoch */
    int         refcnt;        /* Counts recursive allocations of cached interp */

    /*
     * The following pointer maintains the first in
     * a FIFO list of callbacks to invoke at interp
     * de-allocate time.
     */

    struct Defer *firstDeferPtr;

    /*
     * The following pointer maintains the first in
     * a LIFO list of scripts to evaluate when a
     * connection closes.
     */

    struct AtClose *firstAtClosePtr;

    /*
     * The following pointer and struct maintain state for
     * the active connection, if any, and support the ns_conn
     * command.
     */

    Ns_Conn *conn;

    struct {
        int  flags;
        char auth[16];
        char form[16];
        char hdrs[16];
        char outhdrs[16];
    } nsconn;

    /*
     * The following struct maintains per-interp ADP
     * context including the private pages cache.
     */

    struct adp {
	int		   flags;
	int		   exception;
	int		   refresh;
	size_t		   bufsize;
	int                errorLevel;
	int                debugLevel;
	int                debugInit;
	char              *debugFile;
	Ns_Cache	  *cache;
	int                depth;
	char		  *cwd;
	struct AdpFrame	  *framePtr;
	Ns_Conn		  *conn;
	Tcl_Channel	   chan;
	Tcl_DString	   output;
    } adp;

    /*
     * The following table maintains private Ns_Set's
     * entered into this interp.
     */

    Tcl_HashTable sets;

    /*
     * The following table maintains shared channels
     * register with the ns_chan command.
     */

    Tcl_HashTable chans;

    /*
     * The following table maintains the Tcl HTTP requests.
     */

    Tcl_HashTable https;

} NsInterp;

/*
 * Libnsd initialization routines.
 */

NS_EXTERN void NsInitBinder(void);
NS_EXTERN void NsInitConf(void);
NS_EXTERN void NsInitFd(void);
NS_EXTERN void NsInitListen(void);
NS_EXTERN void NsInitLog(void);
NS_EXTERN void NsInitInfo(void);
NS_EXTERN void NsInitModLoad(void);
NS_EXTERN void NsInitProcInfo(void);
NS_EXTERN void NsInitQueue(void);
NS_EXTERN void NsInitLimits(void);
NS_EXTERN void NsInitDrivers(void);
NS_EXTERN void NsInitServers(void);
NS_EXTERN void NsInitSched(void);
NS_EXTERN void NsInitSls(void);
NS_EXTERN void NsInitTcl(void);
NS_EXTERN void NsInitRequests(void);
NS_EXTERN void NsInitUrl2File(void);

NS_EXTERN void NsConfigAdp(void);
NS_EXTERN void NsConfigLog(void);
NS_EXTERN void NsConfigFastpath(void);
NS_EXTERN void NsConfigMimeTypes(void);
NS_EXTERN void NsConfigDNS(void);
NS_EXTERN void NsConfigRedirects(void);
NS_EXTERN void NsConfigVhost(void);
NS_EXTERN void NsConfigEncodings(void);

/*
 * Virtual server management routines.
 */

NS_EXTERN void NsInitServer(char *server, Ns_ServerInitProc *initProc);
NS_EXTERN void NsRegisterServerInit(Ns_ServerInitProc *proc);
NS_EXTERN NsServer *NsGetInitServer(void);
NS_EXTERN NsServer *NsGetServer(CONST char *server);
NS_EXTERN void NsStartServers(void);
NS_EXTERN void NsStopServers(Ns_Time *toPtr);
NS_EXTERN void NsStartServer(NsServer *servPtr);
NS_EXTERN void NsStopServer(NsServer *servPtr);
NS_EXTERN void NsWaitServer(NsServer *servPtr, Ns_Time *toPtr);
NS_EXTERN void NsWakeupDriver(Driver *drvPtr);

/*
 * Url-specific data routines.
 */

NS_EXTERN void *NsUrlSpecificGet(NsServer *servPtr, CONST char *method,
                              CONST char *url, int id, int fast);

/*
 * Socket driver callbacks.
 */

NS_EXTERN ssize_t NsDriverSend(Sock *sockPtr, struct iovec *bufs, int nbufs, int flags);
NS_EXTERN ssize_t NsDriverSendFile(Sock *sockPtr, Ns_FileVec *bufs, int nbufs, int flags);

NS_EXTERN ssize_t
NsSockSendFileBufsIndirect(Ns_Sock *sock, CONST Ns_FileVec *bufs, int nbufs,
                           Ns_Time *timeoutPtr, int flags,
                           Ns_DriverSendProc *sendProc)
    NS_GNUC_NONNULL(1) NS_GNUC_NONNULL(2) NS_GNUC_NONNULL(6);



NS_EXTERN int  NsQueueConn(Sock *sockPtr, Ns_Time *nowPtr);
NS_EXTERN void NsEnsureRunningConnectionThreads(NsServer *servPtr, ConnPool *poolPtr);
NS_EXTERN void NsMapPool(ConnPool *poolPtr, char *map);
NS_EXTERN void NsSockClose(Sock *sockPtr, int keep);
NS_EXTERN int NsPoll(struct pollfd *pfds, int nfds, Ns_Time *timeoutPtr);

NS_EXTERN Request *NsGetRequest(Sock *sockPtr, Ns_Time *nowPtr);
NS_EXTERN void NsFreeRequest(Request *reqPtr);

NS_EXTERN void NsWriterLock(void);
NS_EXTERN void NsWriterUnlock(void);
NS_EXTERN void NsWriterFinish(WriterSock *wrSockPtr);
NS_EXTERN int  NsWriterQueue(Ns_Conn *conn, size_t nsend, Tcl_Channel chan,
			  FILE *fp, int fd, struct iovec *bufs, int nbufs, 
			  int everysize);

NS_EXTERN void NsFreeAdp(NsInterp *itPtr);
NS_EXTERN void NsTclRunAtClose(NsInterp *itPtr)
     NS_GNUC_NONNULL(1);

NS_EXTERN int NsUrlToFile(Ns_DString *dsPtr, NsServer *servPtr, CONST char *url);
NS_EXTERN char *NsPageRoot(Ns_DString *dest, NsServer *servPtr, CONST char *host);

NS_EXTERN void NsAsyncWriterQueueEnable();
NS_EXTERN void NsAsyncWriterQueueDisable();

/*
 * External callback functions.
 */

NS_EXTERN Ns_ConnLocationProc NsTclConnLocation;
NS_EXTERN Ns_SchedProc NsTclSchedProc;
NS_EXTERN Ns_ServerRootProc NsTclServerRoot;
NS_EXTERN Ns_ThreadProc NsTclThread;
NS_EXTERN Ns_ArgProc NsTclThreadArgProc;
NS_EXTERN Ns_SockProc NsTclSockProc;
NS_EXTERN Ns_ArgProc NsTclSockArgProc;
NS_EXTERN Ns_ThreadProc NsConnThread;
NS_EXTERN Ns_ArgProc NsConnArgProc;
NS_EXTERN Ns_FilterProc NsTclFilterProc;
NS_EXTERN Ns_FilterProc NsShortcutFilterProc;
NS_EXTERN Ns_OpProc NsTclRequestProc;
NS_EXTERN Ns_OpProc NsAdpPageProc;
NS_EXTERN Ns_ArgProc NsAdpPageArgProc;
NS_EXTERN Ns_TclTraceProc NsTclTraceProc;
NS_EXTERN Ns_UrlToFileProc NsUrlToFileProc;
NS_EXTERN Ns_Url2FileProc NsTclUrl2FileProc;
NS_EXTERN Ns_Url2FileProc NsMountUrl2FileProc;
NS_EXTERN Ns_ArgProc NsMountUrl2FileArgProc;

NS_EXTERN void NsGetCallbacks(Tcl_DString *dsPtr);
NS_EXTERN void NsGetSockCallbacks(Tcl_DString *dsPtr);
NS_EXTERN void NsGetScheduled(Tcl_DString *dsPtr);
NS_EXTERN void NsGetMimeTypes(Tcl_DString *dsPtr);
NS_EXTERN void NsGetTraces(Tcl_DString *dsPtr, char *server);
NS_EXTERN void NsGetFilters(Tcl_DString *dsPtr, char *server);
NS_EXTERN void NsGetRequestProcs(Tcl_DString *dsPtr, CONST char *server);
NS_EXTERN void NsGetUrl2FileProcs(Ns_DString *dsPtr, CONST char *server);

#ifdef _WIN32
NS_EXTERN int NsConnectService(void);
NS_EXTERN int NsInstallService(char *service);
NS_EXTERN int NsRemoveService(char *service);
#endif

NS_EXTERN void NsCreatePidFile(char *service);
NS_EXTERN void NsRemovePidFile(char *service);

NS_EXTERN void NsLogOpen(void);
NS_EXTERN void NsTclInitObjs(void);
NS_EXTERN void NsRunPreStartupProcs(void);
NS_EXTERN void NsBlockSignals(int debug);
NS_EXTERN void NsBlockSignal(int signal);
NS_EXTERN void NsUnblockSignal(int signal);
NS_EXTERN int  NsHandleSignals(void);
NS_EXTERN void NsStopDrivers(void);
NS_EXTERN void NsStopSpoolers(void);
NS_EXTERN void NsPreBind(char *bindargs, char *bindfile);
NS_EXTERN void NsClosePreBound(void);
NS_EXTERN char *NsConfigRead(CONST char *file);
NS_EXTERN void NsConfigEval(CONST char *config, int argc, char **argv, int optind);
NS_EXTERN void NsConfUpdate(void);
NS_EXTERN void NsEnableDNSCache(int maxsize, int ttl, int timeout);
NS_EXTERN void NsStartDrivers(void);
NS_EXTERN void NsWaitDriversShutdown(Ns_Time *toPtr);
NS_EXTERN void NsStartSchedShutdown(void);
NS_EXTERN void NsWaitSchedShutdown(Ns_Time *toPtr);
NS_EXTERN void NsStartSockShutdown(void);
NS_EXTERN void NsWaitSockShutdown(Ns_Time *toPtr);
NS_EXTERN void NsStartShutdownProcs(void);
NS_EXTERN void NsWaitShutdownProcs(Ns_Time *toPtr);
NS_EXTERN void NsStartTaskQueueShutdown(void);
NS_EXTERN void NsWaitTaskQueueShutdown(Ns_Time *toPtr);
NS_EXTERN void NsStartJobsShutdown(void);
NS_EXTERN void NsWaitJobsShutdown(Ns_Time *toPtr);

NS_EXTERN Tcl_AppInitProc NsTclAppInit;
NS_EXTERN void NsTclInitServer(CONST char *server)
     NS_GNUC_NONNULL(1);
NS_EXTERN void NsInitStaticModules(CONST char *server);
NS_EXTERN NsInterp *NsGetInterpData(Tcl_Interp *interp)
     NS_GNUC_NONNULL(1);
NS_EXTERN void NsFreeConnInterp(Conn *connPtr)
     NS_GNUC_NONNULL(1);

NS_EXTERN struct Bucket *NsTclCreateBuckets(CONST char *server, int nbuckets);

NS_EXTERN void NsSlsCleanup(Sock *sockPtr);
NS_EXTERN void NsClsCleanup(Conn *connPtr);
NS_EXTERN void NsTclAddBasicCmds(NsInterp *itPtr);
NS_EXTERN void NsTclAddServerCmds(NsInterp *itPtr);

NS_EXTERN void NsRestoreSignals(void);
NS_EXTERN void NsSendSignal(int sig);

/*
 * Conn routines.
 */

NS_EXTERN NsLimits *NsGetRequestLimits(NsServer *servPtr, char *method, char *url);

NS_EXTERN int NsMatchRange(Ns_Conn *conn, time_t mtime);

NS_EXTERN int NsConnParseRange(Ns_Conn *conn, CONST char *type,
                            int fd, CONST void *data, size_t length,
                            Ns_FileVec *bufs, int *nbufsPtr, Ns_DString *dsPtr);
/*
 * request parsing
 */
NS_EXTERN int NsParseAcceptEnconding(double version, CONST char *hdr);


/*
 * ADP routines.
 */

NS_EXTERN void NsAdpSetMimeType(NsInterp *itPtr, char *type);
NS_EXTERN void NsAdpSetCharSet(NsInterp *itPtr, char *charset);
NS_EXTERN int NsAdpGetBuf(NsInterp *itPtr, Tcl_DString **dsPtrPtr);
NS_EXTERN int NsAdpAppend(NsInterp *itPtr, CONST char *buf, int len);
NS_EXTERN int NsAdpFlush(NsInterp *itPtr, int stream);
NS_EXTERN int NsAdpDebug(NsInterp *itPtr, char *host, char *port, char *procs);
NS_EXTERN int NsAdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *resvar);
NS_EXTERN int NsAdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *resvar);
NS_EXTERN int NsAdpInclude(NsInterp *itPtr, int objc, Tcl_Obj *objv[],
			char *file, Ns_Time *ttlPtr);
NS_EXTERN void NsAdpParse(AdpCode *codePtr, NsServer *servPtr, char *utf,
		       int flags, CONST char* file);
NS_EXTERN void NsAdpFreeCode(AdpCode *codePtr);
NS_EXTERN void NsAdpLogError(NsInterp *itPtr);
NS_EXTERN void NsAdpInit(NsInterp *itPtr);
NS_EXTERN void NsAdpReset(NsInterp *itPtr);
NS_EXTERN void NsAdpFree(NsInterp *itPtr);

/*
 * Tcl support routines.
 */

NS_EXTERN void NsTclInitQueueType(void);
NS_EXTERN void NsTclInitAddrType(void);
NS_EXTERN void NsTclInitTimeType(void);
NS_EXTERN void NsTclInitKeylistType(void);
NS_EXTERN void NsTclInitSpecType(void);

/*
 * Callback routines.
 */

NS_EXTERN int NsRunFilters(Ns_Conn *conn, int why);
NS_EXTERN void NsRunCleanups(Ns_Conn *conn);
NS_EXTERN void NsRunTraces(Ns_Conn *conn);
NS_EXTERN void NsRunPreStartupProcs(void);
NS_EXTERN void NsRunSignalProcs(void);
NS_EXTERN void NsRunStartupProcs(void);
NS_EXTERN void NsRunAtReadyProcs(void);
NS_EXTERN void NsRunAtExitProcs(void);

/*
 * Upload progress routines.
 */

NS_EXTERN void NsConfigProgress(void);
NS_EXTERN void NsUpdateProgress(Ns_Sock *sock);

/*
 * watchdog.c
 */

NS_EXTERN int NsForkWatchedProcess(void);

/*
 * Utility functions.
 */

NS_EXTERN int NsCloseAllFiles(int errFd);
NS_EXTERN int NsMemMap(CONST char *path, int size, int mode, FileMap *mapPtr);
NS_EXTERN void NsMemUmap(FileMap *mapPtr);

NS_EXTERN void NsStopSockCallbacks(void);
NS_EXTERN void NsStopScheduledProcs(void);
NS_EXTERN void NsGetBuf(char **bufPtr, int *sizePtr);

NS_EXTERN char *NsFindCharset(CONST char *mimetype, size_t *lenPtr);
NS_EXTERN int NsEncodingIsUtf8(Tcl_Encoding encoding);

NS_EXTERN void NsUrlSpecificWalk(int id, CONST char *server, Ns_ArgProc func,
                              Tcl_DString *dsPtr);

void NsParseAuth(Conn *connPtr, char *auth);

NS_EXTERN int NsTclObjIsByteArray(Tcl_Obj *objPtr);

NS_EXTERN int NsTclTimeoutException(Tcl_Interp *interp);


/*
 * Proxy support
 */

NS_EXTERN int NsConnRunProxyRequest(Ns_Conn *conn);

#endif /* NSD_H */

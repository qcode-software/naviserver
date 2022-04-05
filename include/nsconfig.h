/* include/nsconfig.h.  Generated from nsconfig.h.in by configure.  */
/* include/nsconfig.h.in.  Generated from configure.ac by autoheader.  */

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Defined when cygwin/mingw does not support EXCEPTION DISPOSITION */
/* #undef EXCEPTION_DISPOSITION */

/* Define to 1 if arc4random is available. */
#define HAVE_ARC4RANDOM 1

/* Define to 1 for BSD-type sendfile */
/* #undef HAVE_BSD_SENDFILE */

/* Defined when compiler supports casting to union type. */
#define HAVE_CAST_TO_UNION 1

/* Define if you have support for BSD4.4 style msg passing. */
#define HAVE_CMMSG 1

/* Define to 1 if Tcl was compiled with Core Foundation support. */
#define HAVE_COREFOUNDATION 1

/* Define to 1 when crypt_r library function is available. */
/* #undef HAVE_CRYPT_R */

/* Define to 1 if you have the declaration of `tzname', and to 0 if you don't.
   */
/* #undef HAVE_DECL_TZNAME */

/* Is 'DIR64' in <sys/types.h>? */
/* #undef HAVE_DIR64 */

/* Define to 1 if you have the `drand48' function. */
#define HAVE_DRAND48 1

/* Define to 1 if you have the `fork1' function. */
/* #undef HAVE_FORK1 */

/* Define to 1 if you have the `getaddrinfo' function. */
#define HAVE_GETADDRINFO 1

/* Define to 1 if getgrgid_r is available. */
#define HAVE_GETGRGID_R 1

/* Define to 1 if getgrnam_r is available. */
#define HAVE_GETGRNAM_R 1

/* Define to 1 if gethostbyaddr_r is available. */
/* #undef HAVE_GETHOSTBYADDR_R */

/* Define to 1 if gethostbyaddr_r takes 7 args. */
/* #undef HAVE_GETHOSTBYADDR_R_7 */

/* Define to 1 if gethostbyname_r is available. */
/* #undef HAVE_GETHOSTBYNAME_R */

/* Define to 1 if gethostbyname_r takes 3 args. */
/* #undef HAVE_GETHOSTBYNAME_R_3 */

/* Define to 1 if gethostbyname_r takes 5 args. */
/* #undef HAVE_GETHOSTBYNAME_R_5 */

/* Define to 1 if gethostbyname_r takes 6 args. */
/* #undef HAVE_GETHOSTBYNAME_R_6 */

/* Define to 1 if you have the `getnameinfo' function. */
#define HAVE_GETNAMEINFO 1

/* Define to 1 if getpwnam_r is available. */
#define HAVE_GETPWNAM_R 1

/* Define to 1 if getpwuid_r is available. */
#define HAVE_GETPWUID_R 1

/* Define to 1 when gettid system call is available. */
/* #undef HAVE_GETTID */

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `gmtime_r' function. */
#define HAVE_GMTIME_R 1

/* Compiler support for module scope symbols */
#define HAVE_HIDDEN 1

/* Define to 1 if you have the `inet_ntop' function. */
#define HAVE_INET_NTOP 1

/* Define to 1 if you have the `inet_pton' function. */
#define HAVE_INET_PTON 1

/* Define to 1 if the system has the type `intmax_t'. */
#define HAVE_INTMAX_T 1

/* Define to 1 if the system has the type `intptr_t'. */
#define HAVE_INTPTR_T 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Are we building NaviServer with IPv6 support? */
#define HAVE_IPV6 1

/* Define to 1 if you have the `crypto' library (-lcrypto). */
#define HAVE_LIBCRYPTO 1

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* Define to 1 if you have the `z' library (-lz). */
#define HAVE_LIBZ 1

/* Define to 1 for Linux-type sendfile */
/* #undef HAVE_LINUX_SENDFILE */

/* Define to 1 if you have the `localtime_r' function. */
#define HAVE_LOCALTIME_R 1

/* Define to 1 if the system has the type `long long int'. */
#define HAVE_LONG_LONG_INT 1

/* Define to 1 if you have the `lseek64' function. */
/* #undef HAVE_LSEEK64 */

/* Define to 1 if DNS calls are MT-safe */
#define HAVE_MTSAFE_DNS 1

/* Define to 1 if you have the <netinet/tcp.h> header file. */
#define HAVE_NETINET_TCP_H 1

/* Do we have <net/errno.h>? */
/* #undef HAVE_NET_ERRNO_H */

/* Defined when mingw does not support SEH */
/* #undef HAVE_NO_SEH */

/* Define to 1 if you have the `open64' function. */
/* #undef HAVE_OPEN64 */

/* Define to 1 if you have the <openssl/evp.h> header file. */
#define HAVE_OPENSSL_EVP_H 1

/* Define to 1 if you have the `poll' function. */
#define HAVE_POLL 1

/* Define if you have POSIX threads libraries and header files */
#define HAVE_PTHREAD 1

/* Have PTHREAD_PRIO_INHERIT. */
#define HAVE_PTHREAD_PRIO_INHERIT 1

/* Define to 1 if you have the `random' function. */
#define HAVE_RANDOM 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Is 'struct dirent64' in <sys/types.h>? */
/* #undef HAVE_STRUCT_DIRENT64 */

/* Define to 1 if `sin_len' is a member of `struct sockaddr_in'. */
#define HAVE_STRUCT_SOCKADDR_IN_SIN_LEN 1

/* Is 'struct stat64' in <sys/stat.h>? */
/* #undef HAVE_STRUCT_STAT64 */

/* Define to 1 if `tm_zone' is a member of `struct tm'. */
#define HAVE_STRUCT_TM_TM_ZONE 1

/* Define to 1 if you have the <sys/sendfile.h> header file. */
/* #undef HAVE_SYS_SENDFILE_H */

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if Tcl exports the Tcl_GetMemoryInfo function. */
#define HAVE_TCL_GETMEMORYINFO 1

/* Define to 1 when TCP_FASTOPEN is available. */
/* #undef HAVE_TCP_FASTOPEN */

/* Define to 1 if you have the `timegm' function. */
#define HAVE_TIMEGM 1

/* Should we use the global timezone variable? */
#define HAVE_TIMEZONE_VAR 1

/* Should we use the tm_gmtoff field of struct tm? */
#define HAVE_TM_GMTOFF 1

/* Should we use the tm_tzadj field of struct tm? */
/* #undef HAVE_TM_TZADJ */

/* Define to 1 if your `struct tm' has `tm_zone'. Deprecated, use
   `HAVE_STRUCT_TM_TM_ZONE' instead. */
#define HAVE_TM_ZONE 1

/* Is off64_t in <sys/types.h>? */
/* #undef HAVE_TYPE_OFF64_T */

/* Define to 1 if you don't have `tm_zone' but do have the external array
   `tzname'. */
/* #undef HAVE_TZNAME */

/* Define to 1 if the system has the type `uintmax_t'. */
#define HAVE_UINTMAX_T 1

/* Define to 1 if the system has the type `uintptr_t'. */
#define HAVE_UINTPTR_T 1

/* Define to 1 if you have the <uio.h> header file. */
/* #undef HAVE_UIO_H */

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `unsetenv' function. */
#define HAVE_UNSETENV 1

/* Define to 1 if the system has the type `unsigned long long int'. */
#define HAVE_UNSIGNED_LONG_LONG_INT 1

/* Defined when cygwin/mingw ignores VOID define in winnt.h */
/* #undef HAVE_WINNT_IGNORE_VOID */

/* Define to X509_STORE_CTX_get_obj_by_subject */
#define HAVE_X509_STORE_CTX_GET_OBJ_BY_SUBJECT 1

/* Define to 1 if you have the <xlocale.h> header file. */
#define HAVE_XLOCALE_H 1

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* Define to 1 if you have the `_NSGetEnviron' function. */
#define HAVE__NSGETENVIRON 1

/* No Compiler support for module scope symbols */
#define MODULE_SCOPE extern __attribute__((__visibility__("hidden")))

/* Define to 1 if the native crypt library should be used */
/* #undef NS_HAVE_CRYPT */

/* type of second ns_poll() argument */
#define NS_POLL_NFDS_TYPE nfds_t

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "naviserver-devel@lists.sourceforge.net"

/* Define to the full name of this package. */
#define PACKAGE_NAME "NaviServer"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "NaviServer 4.99.23"

/* Revision number form source code management system, or a constant from tar
   distribution */
#define PACKAGE_TAG "naviserver-4.99.22-43-g95b92c051ac2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "naviserver"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "4.99.23"

/* Define to necessary symbol if this constant uses a non-standard name on
   your system. */
/* #undef PTHREAD_CREATE_JOINABLE */

/* The size of `time_t', as computed by sizeof. */
#define SIZEOF_TIME_T 8

/* Is this a static build? */
/* #undef STATIC_BUILD */

/* Define to 1 if all of the C90 standard headers exist (not just the ones
   required in a freestanding environment). This macro is provided for
   backward compatibility; new code need not use it. */
#define STDC_HEADERS 1

/* Is memory debugging enabled? */
/* #undef TCL_MEM_DEBUG */

/* Are we building with threads enabled? */
#define TCL_THREADS 1

/* Do 'long' and 'long long' have the same size (64-bit)? */
#define TCL_WIDE_INT_IS_LONG 1

/* What type should be used to define wide integers? */
/* #undef TCL_WIDE_INT_TYPE */

/* Maximum value for time_t */
#define TIME_T_MAX LONG_MAX

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* UNDER_CE version */
/* #undef UNDER_CE */

/* Define to 1 if the <dl.h> header should be used. */
/* #undef USE_DLSHL */

/* need for dup high */
/* #undef USE_DUPHIGH */

/* Define to 1 if the <mach-o/dyld.h> header should be used. */
#define USE_DYLD 1

/* Do we want to use the threaded memory allocator? */
#define USE_THREAD_ALLOC 1

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Add the _ISOC99_SOURCE flag when building */
/* #undef _ISOC99_SOURCE */

/* Add the _LARGEFILE64_SOURCE flag when building */
/* #undef _LARGEFILE64_SOURCE */

/* Add the _LARGEFILE_SOURCE64 flag when building */
/* #undef _LARGEFILE_SOURCE64 */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* # needed in sys/socket.h Should OS/390 do the right thing with sockets? */
/* #undef _OE_SOCKETS */

/* Do we really want to follow the standard? Yes we do! */
/* #undef _POSIX_PTHREAD_SEMANTICS */

/* Do we want the reentrant OS API? */
#define _REENTRANT 1

/* Do we want the thread-safe OS API? */
#define _THREAD_SAFE 1

/* Define for Solaris 2.5.1 so the uint32_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT32_T */

/* Define for Solaris 2.5.1 so the uint64_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT64_T */

/* Define for Solaris 2.5.1 so the uint8_t typedef from <sys/synch.h>,
   <pthread.h>, or <semaphore.h> is not used. If the typedef were allowed, the
   #define below would cause a syntax error. */
/* #undef _UINT8_T */

/* _WIN32_WCE version */
/* #undef _WIN32_WCE */

/* Do we want to use the XOPEN network library? */
/* #undef _XOPEN_SOURCE_EXTENDED */

/* Define to the type of a signed integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int16_t */

/* Define to the type of a signed integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int32_t */

/* Define to the type of a signed integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef int64_t */

/* Define to the type of a signed integer type of width exactly 8 bits if such
   a type exists and the standard includes do not define it. */
/* #undef int8_t */

/* Define to the widest signed integer type if <stdint.h> and <inttypes.h> do
   not define. */
/* #undef intmax_t */

/* Define to the type of a signed integer type wide enough to hold a pointer,
   if such a type exists, and if the system does not define it. */
/* #undef intptr_t */

/* Define to the type of an unsigned integer type of width exactly 16 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint16_t */

/* Define to the type of an unsigned integer type of width exactly 32 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint32_t */

/* Define to the type of an unsigned integer type of width exactly 64 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint64_t */

/* Define to the type of an unsigned integer type of width exactly 8 bits if
   such a type exists and the standard includes do not define it. */
/* #undef uint8_t */

/* Define to the widest unsigned integer type if <stdint.h> and <inttypes.h>
   do not define. */
/* #undef uintmax_t */

/* Define to the type of an unsigned integer type wide enough to hold a
   pointer, if such a type exists, and if the system does not define it. */
/* #undef uintptr_t */

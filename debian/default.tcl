#
# The contents of this file are subject to the Mozilla Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://www.mozilla.org/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
#
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
#

#
# Set some Tcl variables that are commonly used throughout this file.
# Where a daemon is started from the config file:
# /etc/naviserver/srv1.tcl
# the init script will expect to find the corresponding pid file in:
# /run/naviserver/srv1.pid
# If you change the instance name please keep in mind the pidfile is 
# based on it.
#

set	package			naviserver
set	instance		[string map {".tcl" ""} [file tail [ns_info config]]]

# Absolute path to the installation directory
set 	homedir			/usr/lib/${package}

# Log directories (access log, error log, pidfile)
set	logdir 			/var/log/${package}
set	piddir 			/run/${package}

# Name/description of the virtual server
set	servername		"$instance"
set	serverdesc		"Naviserver $instance"

# The hostname, address and port for nssock should be set to actual values.
set	hostname		"$instance"
set	address			0.0.0.0
set	port			80
set	controlport		9999

# nsssl options (if applicable)
set     ssl_port                443     
set     ssl_cert                /etc/naviserver/${instance}.pem

# html/adp files
set 	pageroot 		/usr/share/doc/naviserver
#set	pageroot		/var/www

# Absolute path to pages directory
set	pagedir			${pageroot}



########################################################################
# Modules to load
########################################################################

ns_section	"ns/server/${servername}/modules"
   ns_param	nssock			${homedir}/bin/nssock.so
   ns_param	nslog 			${homedir}/bin/nslog.so
   ns_param	nscgi 			${homedir}/bin/nscgi.so
   ns_param	nsperm 			${homedir}/bin/nsperm.so
   #ns_param 	nscp 			${homedir}/bin/nscp.so
   ns_param	nsproxy			${homedir}/bin/nsproxy.so
   ns_param  nsssl                   ${homedir}/bin/nsssl.so

   # Tcl modules are loaded here as well, they should be put
   # under tcl/ in separate directory each
   #ns_param	nstk			Tcl


########################################################################
# Global server parameters
########################################################################

ns_section	"ns/parameters"
   # Home directory for the server, it will be resolved automaticlaly if not specified
   ns_param	home			$homedir

   # Output debug log messages in the log
   ns_param	logdebug		false

   # Where all shared Tcl modules are located
   ns_param	tcllibrary      	${homedir}/tcl

   # Main server log file
   ns_param	serverlog       	${logdir}/${instance}.log
   ns_param	logroll  	     	false

   # Pid file of the server process
   ns_param	pidfile         	${piddir}/${instance}.pid

   # Min size of the uploaded file to enable progress tracking
   ns_param	progressminsize		0

   # How many Tcl jobs to run in any job thread before thread exit, this will alow
   # to reclaim thread memory back to the system if memory allocator supports it
   ns_param	jobsperthread		0

   # How many Tcl tasks to run in any schedule thread before thread exit
   ns_param	schedsperthread		0

   # Write asynchronously to log files (access log and error log)
   ns_param	asynlogcwriter		true ;# default: false

   #
   # I18N Parameters
   #

   # Automatic adjustment of response content-type header to include charset
   # This defaults to True.
   ns_param	hackcontenttype		true

   # Default output charset.  When none specified, no character encoding of
   # output is performed.
   ns_param	outputcharset		iso8859-1

   # Default Charset for Url Encode/Decode. When none specified, no character
   # set encoding is performed.
   ns_param	urlcharset		iso8859-1

   # This parameter supports output encoding arbitration.
   ns_param	preferredcharsets	{ utf-8 iso8859-1 }


########################################################################
# MIME types.
#
#  Note: naviserver already has an exhaustive list of MIME types, but in
#  case something is missing you can add it here.
########################################################################

ns_section	"ns/mimetypes"

   # MIME type for unknown extension.
   ns_param	default			"*/*"

   # MIME type for missing extension.
   ns_param	noextension		"*/*"

   #
   # I18N Mime-types
   #
   #  Define content-type header values to be mapped from these file-types.
   #
   #  Note that you can map file-types of adp files to control
   #  the output encoding through mime-type specificaion.
   #  Remember to add an adp mapping for that extension.
   #
   ns_param	.adp			"text/html; charset=iso-8859-1"
   ns_param	.u_adp          	"text/html; charset=UTF-8"
   ns_param	.gb_adp         	"text/html; charset=GB2312"
   ns_param	.sjis_html      	"text/html; charset=shift_jis"
   ns_param	.sjis_adp       	"text/html; charset=shift_jis"
   ns_param	.gb_html        	"text/html; charset=GB2312"


########################################################################
#   I18N File-type to Encoding mappings
########################################################################

ns_section	"ns/encodings"
   ns_param   	.utf_html       	"utf-8"
   ns_param   	.sjis_html      	"shiftjis"
   ns_param   	.gb_html        	"gb2312"
   ns_param   	.big5_html      	"big5"
   ns_param   	.euc-cn_html    	"euc-cn"
   #
   # Note: you will need to include file-type to encoding mappings
   # for ANY source files that are to be used, to allow the
   # server to handle them properly.  E.g., the following
   # asserts that the GB-producing .adp files are themselves
   # encoded in GB2312 (this is not simply assumed).
   #
   ns_param   	.gb_adp         	"gb2312"


########################################################################
# Thread library (nsthread) parameters
########################################################################

ns_section 	"ns/threads"

   # Global thread stacksize configuration deprecated. Server now uses OS
   # default. Only explicitly set the stacksize to some smaller than default
   # value if you need to save memory because your server has a lot of
   # threads, and you know for certain this won't cause problems with deeply
   # nested Tcl or ADP scripts.
   ns_param   	stacksize		[expr 128*1024]


########################################################################
# Server-level configuration
#
#  There is only one server in naviserver, but this is helpful when multiple
#  servers share the same configuration file.  This file assumes that only
#  one server is in use so it is set at the top in the "server" Tcl variable.
#  Other host-specific values are set up above as Tcl variables, too.
########################################################################

ns_section	"ns/servers"
   ns_param	$servername     	$serverdesc

########################################################################
# Server parameters
#
#  Server-level I18N Parameters can be specified here, to override
#  the global ones for this server.
#  These are: hackcontenttype outputcharset urlcharset
#  See the global parameter I18N section for a description of these.
########################################################################

ns_section 	"ns/server/${servername}"

   #
   # Scaling and Tuning Options
   #

   # Maximum number of connection structures
   ns_param   	maxconnections  	100	;# 100; determines queue size as well

   # Minimal and maximal number of connection threads
   ns_param   	maxthreads      	10
   ns_param   	minthreads      	1

   # Connection thread lifetime management
   ns_param	connsperthread  10000   ;# Number of connections (requests) handled per thread
   ns_param	threadtimeout   120     ;# Timeout for idle threads

   # Connection thread creation eagerness
   #ns_param	lowwatermark	10      ;# 10; create additional threads above this queue-full percentage
   #ns_param	highwatermark	100     ;# 80; allow concurrent creates above this queue-is percentage
                                        ;# 100 means to disable concurrent creates

   # Compress response character data: ns_return, ADP etc.
   ns_param	compressenable		off     ;# Default; use ns_conn compress to overide
   ns_param	compresslevel		4       ;# 1-9 where 9 is high compression, high overhead
   ns_param	compressminsize		512     ;# Compress responses larger than this


########################################################################
# ADP (AOLserver Dynamic Page) configuration
########################################################################

ns_section 	"ns/server/${servername}/adp"

   # Extensions to parse as ADP's.
   ns_param   	map             	"/*.adp"

   # Set "Expires: now" on all ADP's.
   ns_param   	enableexpire    	false

   # Allow Tclpro debugging with "?debug".
   ns_param   	enabledebug     	false

   # Parse *.tcl files in pageroot.
   ns_param   	enabletclpages  	true

   # I18N Note: will need to define I18N specifying mappings of ADP's here as well.
   ns_param   	map             	"/*.u_adp"
   ns_param   	map             	"/*.gb_adp"
   ns_param   	map             	"/*.sjis_adp"

   # ADP start page to use for empty ADP requests
   #ns_param   	startpage      		$pagedir/index.adp

   # ADP error page.
   #ns_param   	errorpage      		$pagedir/errorpage.adp


########################################################################
# Server specific Tcl setup
########################################################################

ns_section      "ns/server/${servername}/tcl"

   # Number of buckets in Tcl hash table for nsv vars
   ns_param        nsvbuckets              16

   # Path to private Tcl modules
   ns_param        library                 ${homedir}/servers/${servername}/modules/tcl

   # Set to "true" to use Tcl-trace based interp initialization.
   ns_param 	lazyloader		false


########################################################################
# Fast Path --
#
#  Fast path configuration is used to configure options used for serving
#  static content, and also provides options to automatically display
#  directory listings.
########################################################################

ns_section	"ns/server/${servername}/fastpath"

   # Defines absolute or relative to serverdir directory where all
   # html/adp pages are located
   ns_param    	pagedir               ${pageroot}

   # Directory index/default page to look for.
   ns_param	directoryfile           "index.adp index.tcl index.html index.htm"

   # Directory listing style. Optional, Can be "fancy" or "simple".
   ns_param	directorylisting	fancy

   # Name of Tcl proc to use to display directory listings. Optional, default is to use
   # _ns_dirlist. You can either specify directoryproc, or directoryadp - not both.
   ns_param	directoryproc           _ns_dirlist

   # Name of ADP page to use to display directory listings. Optional. You can either
   # specify directoryadp or directoryproc - not both.
   #ns_param	directoryadp		""


########################################################################
# Global FastPath settings
########################################################################

ns_section	"ns/fastpath"

   # Enable cache for normal URLs. Optional, default is false.
   ns_param	cache			false

   # Size of fast path cache. Optional, default is ~10M.
   ns_param	cachemaxsize		[expr 1024*10000]

   # Largest file size allowed in cache. Optional, default is 8K
   ns_param	cachemaxentry		8192

   # Use mmap() for cache. Optional, default is false.
   ns_param	mmap			false


########################################################################
# Socket driver module (HTTP)  -- nssock
########################################################################

ns_section 	"ns/server/${servername}/module/nssock"

   # TCP port server will listen on
   ns_param   	port           		$port

   # IP address for listener to bind on
   ns_param   	address        		$address

   # Hostname to use in redirects
   ns_param   	hostname       		$hostname

   # Max upload size
   ns_param  	maxinput	  	1024000

   # Max line size
   ns_param  	maxline	  		8192

   # Read-ahead buffer size
   ns_param  	bufsize        		16384

   # Max upload size when to use spooler
   ns_param  	readahead      		16384

   # Number of requests to accept at once
   ns_param	acceptsize		1

   # Max numbrer of sockets in the driver queue
   ns_param	maxqueuesize            256

   # Performance optimization, may cause recvwait to be ignored if no data sent (default false)
   ns_param        deferaccept             false

   # Spooling Threads
   #ns_param	spoolerthreads	1	;# 0, number of upload spooler threads
   #ns_param	maxupload	0	;# 0, when specified, spool uploads larger than this value to a temp file
   #ns_param	writerthreads	1	;# 0, number of writer threads
   #ns_param	writersize	1048576	;# 1024*1024, use writer threads for files larger than this value
   #ns_param	writerbufsize	8192	;# 8192, buffer (chunk) size for writer threads
   #ns_param	writerstreaming	true	;# false;  activate writer for streaming HTML output (e.g. ns_writer)

   # Tuning of keep-alives
   ns_param	keepwait		 5	 ;# timeout in seconds for keep-alive
   ns_param	keepalivemaxuploadsize	 500000	 ;# don't allow keep-alive for upload content larger than this
   ns_param	keepalivemaxdownloadsize 1000000 ;# don't allow keep-alive for download content larger than this


########################################################################
# SSL module (HTTPS)  -- nsssl
########################################################################

ns_section 	"ns/server/${servername}/module/nsssl"

   ns_param   port            $ssl_port
   ns_param   hostname        $hostname
   ns_param   address         $address

   # Certificate is required and will not run without it, it should contain cert and privkey
   ns_param   certificate     $ssl_cert

   # Ciphers parameter defines which ciphers will be used, by default nsssl uses all ciphers
   ns_param   ciphers         "RC4:HIGH:!aNULL:!MD5;"

   # Protocols parameter defines which protocols can be disabled, options are:
   #         !SSLv2 !SSLv3 !TLSv1, by default all protocosl are enabled
   ns_param   protocols       "!SSLv2"

   # Verify if set to 1 will reject any connections without valid ceritificate
   ns_param   verify          0
   ns_param   maxpost         10485760
   ns_param   maxinput        10485760

   # All other driver related parameters can be specified, see nssock for more details


########################################################################
# Access log -- nslog
########################################################################

ns_section 	"ns/server/${servername}/module/nslog"

   # Name to the log file
   ns_param   	file            	${logdir}/${instance}-access.log

   # If true then use common log format
   ns_param   	formattedtime   	true

   # If true then use NCSA combined format
   ns_param   	logcombined     	false

   # Put in the log request elapsed time
   ns_param	logreqtime		false

   # Include high-res start time and partial request durations (accept, queue, filter, run)
   ns_param	logpartialtimes		false

   # Max # of lines in the buffer, 0 ni limit
   ns_param   	maxbuffer       	0

   # Max # of files to keep when rolling
   ns_param   	maxbackup       	5

   # Time to roll log
   ns_param   	rollhour        	0

   # If true then do the log rolling
   ns_param   	rolllog         	false

   # If true then roll the log on SIGHUP
   ns_param	rollonsignal    	false

   # If true then don't show query string in the log
   ns_param	suppressquery   	false

   # If true ten check for X-Forwarded-For header
   ns_param   	checkforproxy   	false

   # List of additional headers to put in the log
   #ns_param   	extendedheaders 	"Referer X-Forwarded-For"


########################################################################
# CGI interface -- nscgi
#
#  WARNING: These directories must not live under pageroot.
########################################################################

ns_section 	"ns/server/${servername}/module/nscgi"

   # CGI script file dir (GET).
   ns_param   	map 			"GET  /cgi-bin /usr/lib/cgi-bin"

   # CGI script file dir (POST).
   ns_param   	map 			"POST /cgi-bin /usr/lib/cgi-bin"


########################################################################
# Example: Control port configuration.
#
#  To enable:
#
#  1. Define an address and port to listen on. For security
#     reasons listening on any port other then 127.0.0.1 is
#     not recommended.
#
#  2. Decided whether or not you wish to enable features such
#     as password echoing at login time, and command logging.
#
#  3. Add a list of authorized users and passwords. The entires
#     take the following format:
#
#     <user>:<encryptedPassword>:
#
#     You can use the ns_crypt Tcl command to generate an encrypted
#     password. The ns_crypt command uses the same algorithm as the
#     Unix crypt(3) command. You could also use passwords from the
#     /etc/passwd file.
#
#     The first two characters of the password are the salt - they can be
#     anything since the salt is used to simply introduce disorder into
#     the encoding algorithm.
#
#     ns_crypt <key> <salt>
#     ns_crypt x t2
#
#     The configuration example below adds the user "nsadmin" with a
#     password of "x".
#
#  4. Make sure the nscp.so module is loaded in the modules section.
########################################################################

ns_section 	"ns/server/${servername}/module/nscp"
   ns_param 	address 		127.0.0.1
   ns_param 	port 			$controlport
   ns_param 	echopassword    	true
   ns_param 	cpcmdlogging    	false

ns_section 	"ns/server/${servername}/module/nscp/users"
   ns_param 	user 			"nsadmin:t2GqvvaiIUbF2:"


########################################################################
# Example: Host headers based virtual servers.
#
# To enable:
#
# 1. Load comm driver(s) globally.
# 2. Configure drivers as in a virtual server.
# 3. Add a "servers" section to map virtual servers to Host headers.
# 4. Ensure "defaultserver" in comm driver refers to a defined
#    virtual server.
#
########################################################################

ns_section 	"ns/module/nssock"
   ns_param   	port            	$port
   ns_param   	hostname        	$hostname
   ns_param   	address         	$address
   ns_param   	defaultserver   	$servername

ns_section 	"ns/module/nssock/servers"
   ns_param   	$servername         	${hostname}:${port}


########################################################################
# Example: Dynamic Host headers based virtual servers.
#
#  To enable:
#
#  1. Enable by setting enabled to true.
#  2. For each hosted name create directory under ${serverdir}
#  3. Each virtual host directory should have ${pageroot} subdirectory
#
#  /usr/local/ns/
#        servers/${servername}
#                        host.com/
#                               pages
#                        domain.net/
#                               pages
#
########################################################################

ns_section	"ns/server/${servername}/vhost"

   # Enable or disable virtual hosting
   ns_param	enabled                 false

   # Prefix between serverdir and host name
   ns_param	hostprefix              ""

   # Remove :port in the Host: header when building pageroot path so Host: www.host.com:80
   # will result in pageroot ${serverdir}/www.host.com
   ns_param	stripport               true

   # Remove www. prefix from Host: header when building pageroot path so Host: www.host.com
   # will result in pageroot ${serverdir}/host.com
   ns_param	stripwww                true

   # Hash the leading characters of string into a path, skipping periods and slashes.
   # If string contains less characters than levels requested, '_' characters are used as padding.
   # For example, given the string 'foo' and the levels 2, 3:
   #   foo, 2 -> /f/o
   #   foo, 3 -> /f/o/o
   ns_param	hosthashlevel           0

########################################################################
# Example:  Multiple connection thread pools.
#
#  To enable:
#
#  1. Define one or more thread pools.
#  2. Configure pools as with the default server pool.
#  3. Map method/URL combinations to the pools
#
#  All unmapped method/URL's will go to the default server pool.
########################################################################

ns_section 	"ns/server/server1/pools"
   ns_param 	slow			"Slow requests here."
   ns_param 	fast 			"Fast requests here."

ns_section 	"ns/server/server1/pool/slow"
   ns_param 	map 			"POST /slowupload.adp"
   ns_param 	maxthreads      	20
   ns_param 	minthreads      	1

ns_section 	"ns/server/server1/pool/fast"
   ns_param 	map 			"GET /faststuff.adp"
   ns_param 	maxthreads 		10


########################################################################
# Tcl Proxy module -- nsproxy
#
# Below is the list of all supported configuration options
# for the nsproxy module filled in with their default values.
# This list of default values is also compiled in the code
# in case you ommit the ns_param lines.
########################################################################

ns_section  "ns/server/${servername}/module/nsproxy"

   # Proxy program to start
   ns_param	exec            	${homedir}/bin/nsproxy

   # Timeout (ms) when evaluating scripts
   ns_param    	evaltimeout     	0       

   # Timeout (ms) when getting proxy handles
   ns_param    	gettimeout      	0       

   # Timeout (ms) to send data
   ns_param    	sendtimeout     	5000    

   # Timeout (ms) to receive results
   ns_param    	recvtimeout     	5000    

   # Timeout (ms) to wait for slaveis to die
   ns_param    	waittimeout     	1000    

   # Timeout (ms) for a slave to live idle
   ns_param    	idletimeout     	300000  

   # Max number of allowed slaves alive
   ns_param    	maxslaves       	8       


########################################################################
# Limits support
#
# Connection limits can be bundled together into a
# named set of limits and then applied to a subset of the URL
# hierarchy. The max number of connection threads running and waiting to
# run a URL, the max upload file size, and the max time a connection
# should wait to run are all configurable.
########################################################################

ns_section "ns/limits"
   ns_param 	default         	"Default Limits"


ns_section "ns/limit/default"

   # Conn threads running for limit.
   ns_param 	maxrun          	100      

   # Conn threads waiting for limit.
   ns_param 	maxwait         	100      

   # Total seconds to wait for resources.
   ns_param 	timeout         	60       


ns_section "ns/server/server1/limits"

   # Map default limit to URL.
   ns_param 	default         	"GET  /*"
   ns_param 	default         	"POST /*"
   ns_param 	default         	"HEAD /*"

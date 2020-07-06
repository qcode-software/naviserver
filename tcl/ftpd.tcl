#
# FTPD for NaviServer
#
# This is a (partial) implementation of an FTP server for NaviServer
# based on the "ns_connchan" machinery, supporting UTF-8 file names
# and IPv6. It was tested so far with the OS X command line FTP
# client, with Chrome and Safari and with FileZilla.
#
# Be aware, this is no full implementation and so far just a proof of
# concept. What's missing
#
#  - no password checking
#  - no permission checking (aside of server-specific tests)
#  - no destructive commands (overwriting files on the server)
#  - no SFTP
#  - not all FTP commands/extension are implemented with all
#    possible features
#  - more flexible configuration
#
# Some of these items will be implemented as "abstract filesystem" on
# top of OpenACS packages such as xowiki and file-storage, some
# commands will be in corporate versions here. The intention is to
# provide a plain-filesystem based access supporting sftp as a
# NaviServer module and to provide the OpenACS code via OpenACS modules.
#
# For the time being, the configuration happens via this file (see
# end of this file). As long, this file is not a module, copy it to
#
#      naviserver/tcl/ftpd.tcl
#
# The Server requires at least NaviServer 4.99.16d8 to be able to open
# passive connection to the server (recommended configuration).
#
# Gustaf Neumann          June 2017
#

# package req nx
if {[info commands ::nx::Class] eq ""} {
    ns_log warning "ftpd is not available"
    return
}

namespace eval ftpd {
    #
    # Basic infrastructure
    #
    nx::Class create Infrastructure {
        :method log {level msg} {
            if {$level eq "debug"} {set level notice}
            ns_log $level "[self] $msg"
        }
    }

    #############################################################################
    #
    # Ftpd Server:
    #
    # - Listen on a port
    # - Wait for incoming requests,
    # - spawn a session, when a requests comes in.
    #
    #############################################################################
    nx::Class create Server -superclass Infrastructure {
        :property {host ::1}
        :property {port 21}
        :property {rootdir:substdefault "[file normalize [ns_server pagedir]/]"}

        :variable listen

        :public method listen {} {
            set dict [ns_connchan listen ${:host} ${:port} [list [self] listen_handler]]
            if {[dict exists $dict channel]} {
                set :listen [dict get $dict channel]
                :log notice "Ftpd listening on ftp://\[${:host}\]:${:port}"
            } else {
                :log warning "Ftpd can't listen on ftp://\[${:host}\]:${:port}"
            }
        }

        :public method listen_handler {channel} {
            :log debug "listen_handler $channel"
            Session new -channel $channel -host ${:host} -rootdir ${:rootdir}
            return 1
        }
        :public method destroy {} {
            if {[info exists :listen]} {
                ns_connchan close ${:listen}
            }
            next
        }
    }

    #
    # Ftpd Session:
    #
    # - Started on an incoming request
    # - Proof of Concept implementation
    # - Handles the protocol methods (all FTP cmds are implemented as
    #   methods using just capital characters)
    #

    nx::Class create Session -superclass Infrastructure {
        :property channel  ;# command channel
        :property host
        :property rootdir
        :property {currentdir:substdefault "[file normalize [ns_server pagedir]/]"}

        :variable type L8  ;# for encoding; minimal, just handling "binary" or "ascii"
        :variable listen   ;# for passive data channels
        :variable data     ;# data channel

        :variable permission_bits {0 --- 1 --x 2 -w- 3 -wx 4 r-- 5 r-x 6 rw- 7 rwx}

        :method reply {msg} {
            :log debug "${:channel} >>> $msg"
            ns_connchan write ${:channel} "$msg\r\n"
        }

        :method init {} {
            :log debug "session starting"
            ns_connchan callback ${:channel} [list [self] read_handler] rex
            :reply "220- NaviServer FTP access. Access is available as anonymous."
            :reply "220 NaviServer FTP server [ns_info patchlevel] ready."
            next
        }
        :method destroy {} {
            :log debug "session stopping"
            if {[ns_connchan exists ${:channel}]} {
                ns_connchan close ${:channel}
            }
            next
        }

        :method write_data {-plain:switch data} {
            if {$plain} {
                set prevType ${:type}
                set :type L8
            }
            if {${:type} eq "I"} {
                ns_connchan write ${:data} $data
            } else {
                :log debug "DATA\n$data"
                ns_connchan write ${:data} [encoding convertto utf-8 [string map [list \n \r\n] $data]]
            }
            if {$plain} {
                set :type $prevType
            }
        }

        :method require_data {arg cmd} {
            if {![info exists :data]} {
                :log notice ":data is not jet available, retry when connect from client finished"
                lappend :delayed_cmds [list arg $cmd] $arg
            } else {
                apply [list arg $cmd] $arg
            }
        }

        :public method listen_data {channel} {
            ns_connchan close ${:listen}
            unset :listen
            :log debug "listen_data sets :data <$channel>"
            set :data $channel
            :log debug "have delayed cmds [info exists :delayed_cmds]"
            if {[info exists :delayed_cmds]} {
                foreach {lambda arg} ${:delayed_cmds} {
                    apply $lambda $arg
                }
                unset :delayed_cmds
            }
        }
        :public method close_data {} {
            if {[info exists :data]} {
                :log debug "closing data channel ${:data}"
                ns_connchan close ${:data}
                unset :data
            }
        }

        :public method read_handler {condition} {
            set bytes [encoding convertfrom utf-8 [ns_connchan read ${:channel}]]
            set rlen [string length $bytes]
            #:log debug "read_handler: ${:channel} $rlen <[string trim $bytes]>"
            set result 0
            if {0 && $rlen == 0} {
                :log debug "client has closed connection"
            } else {
                #
                # Some clients send multiple lines/commands in one transmission
                #
                foreach line [split [string trim $bytes] \n] {
                    set line [string trim $line]
                    :log debug "${:channel} <<< $line"
                    if {$line eq ""} continue
                    if {[regexp {^([A-Za-z]+)\s?(.*)$} $line . cmd arg]} {
                        set cmd [string toupper $cmd]
                        if {[:info lookup method $cmd] ne ""} {
                            :$cmd $arg
                            set result 1
                        } else {
                            :log warning "502 Requested action <$cmd> not taken"
                            :reply "502 Requested action <$cmd> not taken"
                        }
                    } else {
                        :log warning "line <$line> does not look like a valid command"
                    }
                }
            }
            return $result
        }

        #############################################################################
        #
        # File system interface methods
        #
        #############################################################################
        :method "file size" {filename} {
            # minimal implementation, should return different results based on TYPE"
            return [file size ${:currentdir}/$filename]
        }

        :method "file exists" {filename} {
            return [file exists ${:currentdir}/$filename]
        }

        :method "file lastmodified" {filename} {
            return [clock format [file mtime ${:currentdir}/$filename] -format "%Y%m%d%H%M%S"]
        }

        :method "file open" {filename mode} {
            return [open ${:currentdir}/$filename $mode]
        }

        :method "file glob" {pattern} {
            # Some clients send a "-a" or a "-l", Skip it.
            regexp {^-[al]\s*(.*)$} $pattern . pattern
            if {$pattern eq ""} {
                set pattern "*"
            }
            return [lsort [glob -directory ${:currentdir}/ -nocomplain -- $pattern]]
        }

        :method "file dir" {type arg} {
            set files [:file glob $arg]
            set result ""
            foreach file $files {
                append result [:file format $type $file] \n
            }
            return $result
        }


        :method "file format permissions" {type mode} {
            if {$type eq "file"} {
                set permissions "-"
            } else {
                set permissions [string index $type 0]
            }
            foreach j [split [format %03o [expr {$mode & 0777}]] {}] {
                append permissions [dict get ${:permission_bits} $j]
            }
            return $permissions
        }

        :method "file format date" {seconds} {
            set currentTime [clock seconds]
            set oldTime [clock scan "6 months ago" -base $currentTime]
            if {$seconds <= $oldTime} {
                set time [clock format $seconds -format "%Y"]
            } else {
                set time [clock format $seconds -format "%H:%M"]
            }
            set day [string trimleft [clock format $seconds -format "%d"] 0]
            set month [clock format $seconds -format "%b"]
            return [format "%3s %2s %5s" $month $day $time]
        }

        :method "file format name" {file} {
            return [file tail $file]
        }

        :method "file format list" {file} {
            file stat $file stat
            if {$::tcl_platform(platform) eq "unix"} {
                set user [file attributes $file -owner]
                set group [file attributes $file -group]
            } else {
                set user owner
                set group
            }
            return [format "%s %3d %8s %8s %11s %s %s" \
                        [:file format permissions [file type $file] $stat(mode)] \
                        $stat(nlink) \
                        $user \
                        $group \
                        $stat(size) \
                        [:file format date $stat(mtime)] \
                        [file tail $file]]
        }

        :method "file format mlst" {file} {
            #  MLST size*;type*;perm*;create*;modify*;
            set perm ""
            file stat $file stat

            if {[file isdirectory $file]} {
                # maybe distinguish between dir/cdir/pdir rfc3659 7.5.1
                set type dir
                if {[file readable $file]} {append perm "l"}
                if {[file executable $file]} {append perm "e"}
                if {[file writable $file]} {append perm "cdm"}

            } else {
                set type file
                if {[file readable $file]} {append perm "r"}
                if {[file writable $file]} {append perm "fwd"}
            }
            set ctime [clock format $stat(ctime) -format "%Y%m%d%H%M%S"]
            set mtime [clock format $stat(mtime) -format "%Y%m%d%H%M%S"]
            append result \
                "Size=[file size $file];" \
                "Type=$type;" \
                "Perm=$perm;" \
                "Create=$ctime;" \
                "Modify=$mtime;" \
                " " [file tail $file]
            return $result
        }

        #############################################################################
        #
        # FTP methods
        #
        #############################################################################

        :method AUTH {option} {
            if {$option eq "TLS"} {
                :reply "534 not accepting TLS"
                #:reply "234 not yet done"
            } else {
                :reply "504 unrecognized security mechanism $option"
            }
        }

        :method CDUP {args} {
            :CWD ..
        }

        :method CWD {directory} {
            #
            # Check if path is absolute or relative
            #
            if {[string match "/*" $directory]} {
                set newdir ${:rootdir}$directory
            } else {
                set newdir ${:currentdir}/$directory
            }

            #
            # Normalize the path and check validity
            #
            set newdir [file normalize $newdir]
            :log debug "currentdir <${:currentdir}> rootdir <${:rootdir}> newdir <$newdir>"

            if {[string length $newdir] < [string length ${:rootdir}]
                || ![file readable $newdir]
                || ![file isdirectory $newdir]
            } {
                :reply "550 cannot change to directory $directory"
            } else {
                set :currentdir $newdir
                :reply "250 Okay."
            }
        }

        :method EPSV {args} {
            # RFC2428: entering extended passive mode. It accepts an
            # optional argument.  When the EPSV command is issued with
            # no argument, the server will choose the network protocol
            # for the data connection based on the protocol used for
            # the control connection.
            set dict [ns_connchan listen -bind ${:host} 0 [list [self] listen_data]]
            #:log debug "EPSV $dict"
            :reply "229 Entering Extended Passive Mode (|||[dict get $dict port]|)"
            set :listen [dict get $dict channel]
        }

        :method EPRT {arg} {
            # This method depends on "ns_connchan opensocket", which
            # is not implemented yet.
            if {![regexp {^[|]([^|]+)[|]([^|]+)[|]([^|]+)[|]$} $arg . proto host port]} {
                :reply "501 Bad Argument"
                :log warning "EPRT -> bad arg"
            } else {
                :log debug "EPRT: call open to host <$host> port <$port>"
                #set :data [ns_connchan opensocket $host $port]
                :log debug "EPRT: listen on fresh port $port DONE"
                :reply "200 Command OK"
            }
        }

        :method FEAT {args} {
            :reply "211-Extensions:"
            :reply " UTF8"
            :reply " LANG EN"
            #:reply " AUTH TLS"
            :reply " MDTM"
            :reply " MLST size*;type*;perm*;create*;modify*;"
            :reply "211 End"
        }

        :method MLSD {arg} {
            :require_data $arg {
                :reply "150 Opening ASCII mode data connection for /bin/ls."
                :write_data -plain [:file dir mlst $arg]
                :close_data
                :reply "226 Transfer complete."
            }
        }

        :method LIST {arg} {
            :require_data $arg {
                :reply "150 Opening ASCII mode data connection for /bin/ls."
                :write_data -plain [:file dir list $arg]
                :close_data
                :reply "226 Transfer complete."
            }
        }

        :method MDTM {filename} {
            if {[:file exists $filename]} {
                :reply "213 [:file lastmodified $filename]"
            } else {
                :reply "550 File not found"
            }
        }

        :method NLST {arg} {
            :require_data $arg {
                :reply "150 Opening ASCII mode data connection for NLST."
                :write_data -plain [:file dir name $arg]
                :close_data
                :reply "226 Transfer complete."
            }
        }

        :method OPTS {arg} {
            :log warning "opts $arg acknowledged, but ignored"
            :reply "200 Ok"
        }

        :method PASS {pass} {
            :reply "230 User ${:user} logged in.  Access restrictions apply."
        }

        :method PASV {args} {
            # Old style passive mode, just for IPv4. New clients should
            # use EPSV. However, PASV is still used by many clients.
            set dict [ns_connchan listen -bind ${:host} 0 [list [self] listen_data]]
            set port [dict get $dict port]
            :reply "227 Entering Passive Mode ([string map {. ,} [dict get $dict address]],[expr {$port/256}],[expr {$port%256}])"
            set :listen [dict get $dict channel]
        }

        :method PWD {pass} {
            :reply "257 \"[string range ${:currentdir} [string length ${:rootdir}] end]/\""
        }
        :method QUIT {args} {
            :reply "221 Goodbye."
            :destroy
        }

        :method RETR {arg} {
            :require_data $arg {
                set filename $arg
                if {[:file exists $filename]} {
                    :reply "150 Opening data connection for returning data."
                    set F [:file open $filename r]; set data [read $F]; close $F
                    :write_data $data
                    :reply "226 Transfer complete."
                } else {
                    :reply "550 File not found."
                }
                :close_data
            }
        }

        :method SIZE {filename} {
            if {[:file exists $filename]} {
                :reply "215 [:file size $filename]"
            } else {
                :reply "550 File not found."
            }
        }

        :method SYST {args} {
            :reply "215 [string toupper $::tcl_platform(platform)] Type: ${:type}"
        }

        :method TYPE {kind} {
            set :type $kind
            :reply "200 Type set to $kind"
        }
        :method USER {name} {
            set :user $name
            :reply "331 Password required for $name"
        }
    }
}

#############################################################################
#
# Configuration of a sample FTP server; so far in this file, will be
# moved to the configuration file.
#
#############################################################################

#ftpd::Server create ftpd1 -port 2100 -host ::1
ftpd::Server create ftpd1 -port 2100 -host ::0

ns_atstartup {
    #
    # ns_startup is executed for every server. Just start listen for
    # the first one.
    #
    if {[ns_info server] eq [lindex [ns_info servers] 0]} {
        # ftpd1 listen
    }
}

# TODO
#
# REST ... Restart of Interrupted Transfer (rfc3659)
# SITE ... https://en.wikipedia.org/wiki/List_of_FTP_commands


# Local variables:
#    mode: tcl
#    tcl-indent-level: 4
#    indent-tabs-mode: nil
# End:

# -*- Tcl -*-
#
# The contents of this file are subject to the Mozilla Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
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
# all.tcl --
#
#       This file contains a top-level script to run all of the tests.
#       Execute it by invoking "source all.tcl" when running nsd in
#       command mode in this directory.
#

package require Tcl 8.4
package require tcltest 2.2
namespace import tcltest::*
eval configure $argv -singleproc true -testdir [file dirname [info script]]



rename tcltest::test tcltest::__test

proc tcltest::test args {

    ns_log dev >->-> \
        [format "%-16s" "[lindex $args 0]:"] ([string trim [lindex $args 1]])

    uplevel 1 tcltest::__test $args
}

ns_logctl severity DriverDebug true

runAllTests

#
# Shutdown the server to let the cleanup handlers run
#
#foreach s [ns_info servers] {puts stderr "$s: [ns_server -server $s stats]"}
ns_shutdown

#
# Wait until these are finished, ns_shutdown will terminate this script
#
after 2000 return
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All Rights Reserved.
Copyright (c) 2011 - 2012, Google, Inc. All Rights Reserved.

UDP-based Data Transfer (UDT) Library - version 4
Author: Yunhong Gu [yunhong.gu @ gmail.com]

UDT version 4 is free software under BSD License. See ./LICENSE.txt.

============================================================================

UDT Website:
http://udt.sf.net
http://sf.net/projects/udt/ 


CONTENT: 
./src:     UDT source code 
./app:     Example programs 
./doc:     UDT documentation (HTML)
./win:     Visual C++ project files for the Windows version of UDT 


To make: 
     make -e os=XXX arch=YYY 

XXX: [LINUX(default), BSD, OSX] 
YYY: [IA32(default), POWERPC, IA64, AMD64] 

For example, on OS X, you may need to do "make -e os=OSX arch=POWERPC"; 
on 32-bit i386 Linux system, simply use "make".

On Windows systems, use the Visual C++ project files in ./win directory.

Note for BSD users, please use GNU Make.

The build requires libnice and its GLib dependency. The Makefiles use
pkg-config to discover the necessary compiler and linker flags. If
pkg-config is not available, set the NICE_CFLAGS and NICE_LIBS
environment variables before invoking make, for example:

    export NICE_CFLAGS='-I/usr/include/nice -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include'
    export NICE_LIBS='-lnice -lglib-2.0'

Adjust the paths to match your installation locations.

When using the libnice-based transport, applications must exchange ICE
parameters out-of-band before a connection can be established. Call
`UDT::getICEInfo()` to obtain the local username fragment, password, and
candidate list and signal these to the remote peer. Before initiating
connectivity checks, supply the remote values with `UDT::setICEInfo()`. The
sample `appniceclient` and `appniceserver` programs output these fields on a
single line by concatenating length-prefixed strings (for example,
`4:abcd10:passphrase...`). Copy the full line and provide the remote peer's line
when prompted. Applications may configure optional STUN and TURN services via
`UDT::setICESTUNServer()` and `UDT::setICETURNServer()` prior to requesting ICE
information. Passing an empty server string disables the associated relay.
Call `UDT::setICEPortRange(min_port, max_port)` before binding or requesting ICE
information to constrain the local UDP ports used for gathered candidates;
specify `(0, 0)` to return to the default libnice behavior. The `appnice*` and
`appgst*` samples expose these hooks through the `--stun=HOST[:PORT]` and
`--turn=HOST[:PORT],USERNAME,PASSWORD` command-line options so you can test
against public STUN/TURN infrastructure if desired.

To use UDT in your application:
Read index.htm in ./doc. The documentation is in HTML format and requires your
browser to support JavaScript.


Debug logging
-------------
Set the `LIBNICE_DEBUG` environment variable to a non-empty value (for example,
`export LIBNICE_DEBUG=1`) before starting an application to enable detailed
libnice integration logs. The library also honors the fallback variables
`POLEIS_LIBNICE_DEBUG` and `POLEIS_DEBUG` for the same purpose.

To surface additional diagnostics from the UDT stack, set `UDT_DEBUG` (or the
fallback variables `POLEIS_UDT_DEBUG` and `POLEIS_DEBUG`) to any value other
than `0`, `false`, `off`, or `no`. When enabled, the library writes verbose
status updates and error reports to standard error with a `[UDT]` prefix.


Questions? please post to the UDT project forum:
https://sourceforge.net/projects/udt/forums

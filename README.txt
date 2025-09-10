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
single line in the format `ufrag pwd cand1 cand2 ...`; provide the remote
peer's line when prompted.

To use UDT in your application:
Read index.htm in ./doc. The documentation is in HTML format and requires your
browser to support JavaScript.


Questions? please post to the UDT project forum:
https://sourceforge.net/projects/udt/forums

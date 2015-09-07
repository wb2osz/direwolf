
Files in this directory fill in the gaps missing for some operating systems.


--------------------------------------

These are part of the standard C library for Linux and similar operating systems.
For the Windows version we need to include our own copy.

They were copied from Cygwin source.
/usr/src/cygwin-1.7.10-1/newlib/libc/string/...

	strsep.c
	strtok_r.c

--------------------------------------

This was also missing on Windows but available everywhere else.

	strcasestr.c

--------------------------------------


The are used for the Linux and Windows versions.
They should be part of the standard C library for OpenBSD, FreeBSD, Mac OS X.
These are from OpenBSD.
http://ftp.netbsd.org/pub/pkgsrc/current/pkgsrc/net/tnftp/files/libnetbsd/strlcpy.c
http://ftp.netbsd.org/pub/pkgsrc/current/pkgsrc/net/tnftp/files/libnetbsd/strlcat.c


	strlcpy.c
	strlcat.c
	
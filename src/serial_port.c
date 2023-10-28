//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015, 2017  John Langner, WB2OSZ
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//


//#define DEBUG 1

/*------------------------------------------------------------------
 *
 * Module:      serial.c
 *
 * Purpose:   	Interface to serial port, hiding operating system differences.
 *		
 *---------------------------------------------------------------*/

#include "direwolf.h"		// should be first

#include <stdio.h>

#if __WIN32__

#include <stdlib.h>

#else

#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#endif

#include <assert.h>
#include <string.h>


#include "textcolor.h"
#include "serial_port.h"




/*-------------------------------------------------------------------
 *
 * Name:	serial_port_open
 *
 * Purpose:	Open serial port.
 *
 * Inputs:	devicename	- For Windows, usually like COM5.
 *				  For Linux, usually /dev/tty...
 *				  "COMn" also allowed and converted to /dev/ttyS(n-1)
 *				  Could be /dev/rfcomm0 for Bluetooth.
 *
 *		baud		- Speed.  1200, 4800, 9600 bps, etc.
 *				  If 0, leave it alone.
 *
 * Returns 	Handle for serial port or MYFDERROR for error.
 *
 *---------------------------------------------------------------*/


MYFDTYPE serial_port_open (char *devicename, int baud)
{

#if __WIN32__

	MYFDTYPE fd;
	DCB dcb;
	int ok;
	char bettername[50];

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("serial_port_open ( '%s', %d )\n", devicename, baud);
#endif

	
// Reference:	http://www.robbayer.com/files/serial-win.pdf

// Need to use FILE_FLAG_OVERLAPPED for full duplex operation.
// Without it, write blocks when waiting on read.

// Read http://support.microsoft.com/kb/156932 

// Bug fix in release 1.1 - Need to munge name for COM10 and up.
// http://support.microsoft.com/kb/115831

	strlcpy (bettername, devicename, sizeof(bettername));
	if (strncasecmp(devicename, "COM", 3) == 0) {
	  int n;
	  n = atoi(devicename+3);
	  if (n >= 10) {
	    strlcpy (bettername, "\\\\.\\", sizeof(bettername));
	    strlcat (bettername, devicename, sizeof(bettername));
	  }
	}

	fd = CreateFile(bettername, GENERIC_READ | GENERIC_WRITE, 
			0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (fd == MYFDERROR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not open serial port %s.\n", devicename);
	  return (MYFDERROR);
	}

	/* Reference: http://msdn.microsoft.com/en-us/library/windows/desktop/aa363201(v=vs.85).aspx */

	memset (&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(DCB);

	ok = GetCommState (fd, &dcb);
	if (! ok) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("serial_port_open: GetCommState failed.\n");
	}

	/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa363214(v=vs.85).aspx */

	dcb.DCBlength = sizeof(DCB);

	switch (baud) {

	  case 0:	/* Leave it alone. */		break;
	  case 1200:	dcb.BaudRate = CBR_1200;	break;
	  case 2400:	dcb.BaudRate = CBR_2400;	break;
	  case 4800:	dcb.BaudRate = CBR_4800;	break;
	  case 9600:	dcb.BaudRate = CBR_9600;	break;
	  case 19200:	dcb.BaudRate = CBR_19200;	break;
	  case 38400:	dcb.BaudRate = CBR_38400;	break;
	  case 57600:	dcb.BaudRate = CBR_57600;	break;
	  case 115200:	dcb.BaudRate = CBR_115200;	break;

	  default:	text_color_set(DW_COLOR_ERROR);
	  		dw_printf ("serial_port_open: Unsupported speed %d.  Using 4800.\n", baud);
			dcb.BaudRate = CBR_4800;	
			break;
	}

	dcb.fBinary = 1;
	dcb.fParity = 0;
	dcb.fOutxCtsFlow = 0;
	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fDsrSensitivity = 0;
	dcb.fOutX = 0;
	dcb.fInX = 0;
	dcb.fErrorChar = 0;
	dcb.fNull = 0;		/* Don't drop nul characters! */
	dcb.fRtsControl = 0;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	ok = SetCommState (fd, &dcb);
	if (! ok) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("serial_port_open: SetCommState failed.\n");
	}

	//text_color_set(DW_COLOR_INFO);
	//dw_printf("Successful serial port open on %s.\n", devicename);

	// Some devices, e.g. KPC-3+, can't turn off hardware flow control and need RTS.

	EscapeCommFunction(fd,SETRTS);
	EscapeCommFunction(fd,SETDTR);
#else

/* Linux version. */

	int fd;
	struct termios ts;
	int e;
	char linuxname[50];


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("serial_port_open ( '%s' )\n", devicename);
#endif

	/* Translate Windows device name into Linux name. */
	/* COM1 -> /dev/ttyS0, etc. */
	
	strlcpy (linuxname, devicename, sizeof(linuxname));

	if (strncasecmp(devicename, "COM", 3) == 0) {
	  int n = atoi (devicename + 3);
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Converted serial port name '%s'", devicename);
	  if (n < 1) n = 1;
	  snprintf (linuxname, sizeof(linuxname), "/dev/ttyS%d", n-1);
	  dw_printf (" to Linux equivalent '%s'\n", linuxname);
	}

	fd = open (linuxname, O_RDWR);

	if (fd == MYFDERROR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not open serial port %s.\n", linuxname);
	  return (MYFDERROR);
	}

	e = tcgetattr (fd, &ts);
	if (e != 0) { perror ("tcgetattr"); }

	cfmakeraw (&ts);
	
	ts.c_cc[VMIN] = 1;	/* wait for at least one character */
	ts.c_cc[VTIME] = 0;	/* no fancy timing. */

	switch (baud) {

	  case 0:	/* Leave it alone. */						break;
	  case 1200:	cfsetispeed (&ts, B1200);	cfsetospeed (&ts, B1200); 	break;
	  case 2400:	cfsetispeed (&ts, B2400);	cfsetospeed (&ts, B2400); 	break;
	  case 4800:	cfsetispeed (&ts, B4800);	cfsetospeed (&ts, B4800); 	break;
	  case 9600:	cfsetispeed (&ts, B9600);	cfsetospeed (&ts, B9600); 	break;
	  case 19200:	cfsetispeed (&ts, B19200);	cfsetospeed (&ts, B19200); 	break;
	  case 38400:	cfsetispeed (&ts, B38400);	cfsetospeed (&ts, B38400); 	break;
// This does not seem to be a problem anymore.
// Leaving traces behind, as clue, in case failure is encountered in some older version.
//#ifndef __APPLE__
	  // Not defined for Mac OSX.
	  // https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/messages/2072
	  case 57600:	cfsetispeed (&ts, B57600);	cfsetospeed (&ts, B57600); 	break;
	  case 115200:	cfsetispeed (&ts, B115200);	cfsetospeed (&ts, B115200); 	break;
//#endif
	  default:	text_color_set(DW_COLOR_ERROR);
	  		dw_printf ("serial_port_open: Unsupported speed %d.  Using 4800.\n", baud);
			cfsetispeed (&ts, B4800);	cfsetospeed (&ts, B4800);
		 	break;
	}

	e = tcsetattr (fd, TCSANOW, &ts);
	if (e != 0) { perror ("tcsetattr"); }

	//text_color_set(DW_COLOR_INFO);
	//dw_printf("Successfully opened serial port %s.\n", devicename);

#endif

	return (fd);
}




/*-------------------------------------------------------------------
 *
 * Name:	serial_port_write
 *
 * Purpose:	Send characters to serial port.
 *
 * Inputs:	fd	- Handle from open.
 *		str	- Pointer to array of bytes.
 *		len	- Number of bytes to write.
 *
 * Returns 	Number of bytes written.  Should be the same as len.
 *		-1 if error.
 *
 *---------------------------------------------------------------*/


int serial_port_write (MYFDTYPE fd, char *str, int len)
{

	if (fd == MYFDERROR) {
	  return (-1);
	}

#if __WIN32__

	DWORD nwritten; 

	/* Without this, write blocks while we are waiting on a read. */
	static OVERLAPPED ov_wr;
	memset (&ov_wr, 0, sizeof(ov_wr));

        if ( ! WriteFile (fd, str, len, &nwritten, &ov_wr))
	{
	  int err = GetLastError();

	  if (err != ERROR_IO_PENDING) 
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Error writing to serial port.  Error %d.\n\n", err);
	    return (-1);
	  }
	}

	// nwritten is 0 for asynchronous write, at this point, so just return the requested len.
	return (len);

#else
	int written;

        written = write (fd, str, (size_t)len);
	if (written != len)
	{
	  // Do we want this message here?
	  // Or rely on caller to check and provide something more meaningful for the usage?
	  //text_color_set(DW_COLOR_ERROR);
	  //dw_printf ("Error writing to serial port. err=%d\n\n", written);
	  return (-1);
	}

	return (written);
#endif

	
} /* serial_port_write */



/*-------------------------------------------------------------------
 *
 * Name:        serial_port_get1
 *
 * Purpose:     Get one byte from the serial port.  Wait if not ready.
 *
 * Inputs:	fd	- Handle from open.
 *
 * Returns:	Value of byte in range of 0 to 255.
 *		-1 if error.
 *
 *--------------------------------------------------------------------*/

int serial_port_get1 (MYFDTYPE fd)
{
	unsigned char ch;

#if __WIN32__		/* Native Windows version. */

	DWORD n;	
	static OVERLAPPED ov_rd;

	memset (&ov_rd, 0, sizeof(ov_rd));
	ov_rd.hEvent = CreateEvent (NULL, TRUE, FALSE, NULL);

	
	/* Overlapped I/O makes reading rather complicated. */
	/* See:  http://msdn.microsoft.com/en-us/library/ms810467.aspx */

	/* It seems that the read completes OK with a count */
	/* of 0 every time we send a message to the serial port. */

	n = 0;	/* Number of characters read. */

  	while (n == 0) {

	  if ( ! ReadFile (fd, &ch, 1, &n, &ov_rd)) 
	  {
	    int err1 = GetLastError();

	    if (err1 == ERROR_IO_PENDING) 
	    {
	      /* Wait for completion. */

	      if (WaitForSingleObject (ov_rd.hEvent, INFINITE) == WAIT_OBJECT_0) 
	      {
	        if ( ! GetOverlappedResult (fd, &ov_rd, &n, 1))
	        {
	          int err3 = GetLastError();

	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Serial Port GetOverlappedResult error %d.\n\n", err3);
	        }
	        else 
	        {
		  /* Success!  n should be 1 */
	        }
	      }
	    }
	    else
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Serial port read error %d.\n", err1);
	      return (-1);
	    }
	  }

	}	/* end while n==0 */

	CloseHandle(ov_rd.hEvent); 

	if (n != 1) {
	  //text_color_set(DW_COLOR_ERROR);
	  //dw_printf ("Serial port failed to get one byte. n=%d.\n\n", (int)n);
	  return (-1);
	}


#else		/* Linux version */

	int n;

	n = read(fd, &ch, (size_t)1);

	if (n != 1) {
	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("serial_port_get1(%d) returns -1 for error.\n", fd);
	  return (-1);
	}

#endif

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	if (isprint(ch)) {
	  dw_printf ("serial_port_get1(%d) returns 0x%02x = '%c'\n", fd, ch, ch);
	}
	else {
	  dw_printf ("serial_port_get1(%d) returns 0x%02x\n", fd, ch);
	}
#endif

	return (ch);
}


/*-------------------------------------------------------------------
 *
 * Name:        serial_port_close
 *
 * Purpose:     Close the device.
 *
 * Inputs:	fd	- Handle from open.
 *
 * Returns:	None.
 *
 *--------------------------------------------------------------------*/

void serial_port_close (MYFDTYPE fd)
{
#if __WIN32__
	CloseHandle (fd);
#else
	close (fd);
#endif
}


/* end serial_port.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2016  John Langner, WB2OSZ
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



/*------------------------------------------------------------------
 *
 * Module:      kiss.c
 *
 * Purpose:   	Act as a virtual KISS TNC for use by other packet radio applications.
 *		On Windows, it is a serial port.  On Linux, a pseudo terminal.
 *		
 * Input:	
 *
 * Outputs:	  
 *
 * Description:	It implements the KISS TNC protocol as described in:
 *		http://www.ka9q.net/papers/kiss.html
 *
 * 		Briefly, a frame is composed of 
 *
 *			* FEND (0xC0)
 *			* Contents - with special escape sequences so a 0xc0
 *				byte in the data is not taken as end of frame.
 *				as part of the data.
 *			* FEND
 *
 *		The first byte of the frame contains:
 *	
 *			* port number in upper nybble.
 *			* command in lower nybble.
 *
 *	
 *		Commands from application recognized:
 *
 *			0	Data Frame	AX.25 frame in raw format.
 *
 *			1	TXDELAY		See explanation in xmit.c.
 *
 *			2	Persistence	"	"
 *
 *			3 	SlotTime	"	"
 *
 *			4	TXtail		"	"
 *						Spec says it is obsolete but Xastir
 *						sends it and we respect it.
 *
 *			5	FullDuplex	Ignored.  Always full duplex.
 *		
 *			6	SetHardware	TNC specific.  Ignored.
 *			
 *			FF	Return		Exit KISS mode.  Ignored.
 *
 *
 *		Messages sent to client application:
 *
 *			0	Data Frame	Received AX.25 frame in raw format.
 *
 *
 *		
 * Platform differences:
 *
 *		We can use a pseudo terminal for Linux or Cygwin applications.
 *		However, Microsoft Windows doesn't seem to have similar functionality.
 *		Native Windows applications expect to see a device named COM1,
 *		COM2, COM3, or COM4.  Some might offer more flexibility but others
 *		might be limited to these four choices.
 *
 *		The documentation instucts the user to install the com0com 
 *		"Null-modem emulator" from http://sourceforge.net/projects/com0com/   
 *		and configure it for COM3 & COM4.
 *
 *		By default Dire Wolf will use COM3 (/dev/ttyS2 or /dev/com3 - lower case!)
 *		and the client application will use COM4 (available as /dev/ttyS or
 *		/dev/com4 for Cygwin applications).
 *
 *
 *		This can get confusing.
 *
 *		If __WIN32__ is defined, 
 *			We use the Windows interface to the specfied serial port.
 *			This could be a real serial port or the nullmodem driver
 *			connected to another application.
 *		
 *		If __CYGWIN__ is defined,
 *			We connect to a serial port as in the previous case but
 *			use the Linux I/O interface.
 *			We also supply a pseudo terminal for any Cygwin applications 
 *			such as Xastir so the null modem is not needed.
 *
 *		For the Linux case,
 *			We supply a pseudo terminal for use by other applications.
 *
 *
 * Reference:	http://www.robbayer.com/files/serial-win.pdf
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>

#if __WIN32__
#include <stdlib.h>
#else
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef __OpenBSD__
#include <errno.h>
#else
#include <sys/errno.h>
#endif
#endif

#include <assert.h>
#include <string.h>


#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "kiss.h"
#include "kiss_frame.h"
#include "xmit.h"


#if __WIN32__
typedef HANDLE MYFDTYPE;
#define MYFDERROR INVALID_HANDLE_VALUE
#else
typedef int MYFDTYPE;
#define MYFDERROR (-1)
#endif


static kiss_frame_t kf;		/* Accumulated KISS frame and state of decoder. */


/*
 * These are for a Linux/Cygwin pseudo terminal.
 */

#if ! __WIN32__

static MYFDTYPE pt_master_fd = MYFDERROR;	/* File descriptor for my end. */

static char pt_slave_name[32];			/* Pseudo terminal slave name  */
						/* like /dev/pts/999 */



/*
 * Symlink to pseudo terminal name which changes.
 */

#define TMP_KISSTNC_SYMLINK "/tmp/kisstnc"

#endif

/*
 * This is for native Windows applications and a virtual null modem.
 */

#if __CYGWIN__ || __WIN32__

static MYFDTYPE nullmodem_fd = MYFDERROR;

#endif


// TODO:  define in one place, use everywhere.
#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

static THREAD_F kiss_listen_thread (void *arg);



#if DEBUG9
static FILE *log_fp;
#endif


static int kiss_debug = 0;		/* Print information flowing from and to client. */

void kiss_serial_set_debug (int n) 
{	
	kiss_debug = n;
}


/* In server.c.  Should probably move to some misc. function file. */

void hex_dump (unsigned char *p, int len);





/*-------------------------------------------------------------------
 *
 * Name:        kiss_init
 *
 * Purpose:     Set up a pseudo terminal acting as a virtual KISS TNC.
 *		
 *
 * Inputs:	mc->nullmodem	- name of device for our end of nullmodem.
 *
 * Outputs:	
 *
 * Description:	(1) Create a pseudo terminal for the client to use.
 *		(2) Start a new thread to listen for commands from client app
 *		    so the main application doesn't block while we wait.
 *
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static MYFDTYPE kiss_open_nullmodem (char *device);
#else
static MYFDTYPE kiss_open_pt (void);
#endif


void kiss_init (struct misc_config_s *mc)
{

#if __WIN32__
	HANDLE kiss_nullmodem_listen_th;
#else
	pthread_t kiss_pterm_listen_tid;
	//pthread_t kiss_nullmodem_listen_tid;
	int e;
#endif

	memset (&kf, 0, sizeof(kf));

/*
 * This reads messages from client.
 */

#if ! __WIN32__

/*
 * Pseudo terminal for Cygwin and Linux versions.
 */
	pt_master_fd = MYFDERROR;

	if (mc->enable_kiss_pt) {

	  pt_master_fd = kiss_open_pt ();

	  if (pt_master_fd != MYFDERROR) {
	    e = pthread_create (&kiss_pterm_listen_tid, (pthread_attr_t*)NULL, kiss_listen_thread, NULL);
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create kiss listening thread for Linux pseudo terminal");
	    }
	  }
	}
	else {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Use -p command line option to enable KISS pseudo terminal.\n");
	}
#endif

#if __CYGWIN__ || __WIN32

/*
 * Cygwin and native Windows versions have serial port connection.
 */
	if (strlen(mc->nullmodem) > 0) {

#if ! __WIN32__

	  /* Translate Windows device name into Linux name. */
	  /* COM1 -> /dev/ttyS0, etc. */

	  if (strncasecmp(mc->nullmodem, "COM", 3) == 0) {
	    int n = atoi (mc->nullmodem + 3);
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Converted nullmodem device '%s'", mc->nullmodem);
	    if (n < 1) n = 1;
	    snprintf (mc->nullmodem, sizeof(mc->nullmodem), "/dev/ttyS%d", n-1);
	    dw_printf (" to Linux equivalent '%s'\n", mc->nullmodem);
	  }
#endif
	  nullmodem_fd = kiss_open_nullmodem (mc->nullmodem);

	  if (nullmodem_fd != MYFDERROR) {
#if __WIN32__
	    kiss_nullmodem_listen_th = (HANDLE)_beginthreadex (NULL, 0, kiss_listen_thread, NULL, 0, NULL);
	    if (kiss_nullmodem_listen_th == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not create kiss nullmodem thread\n");
	      return;
	    }
#else
	    e = pthread_create (&kiss_nullmodem_listen_tid, NULL, kiss_listen_thread, NULL);
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create kiss listening thread for Windows virtual COM port.");
	    
	    }
#endif
	  }
	}
#endif


#if DEBUG
	text_color_set (DW_COLOR_DEBUG);
#if ! __WIN32__
	dw_printf ("end of kiss_init: pt_master_fd = %d\n", pt_master_fd);
#endif
#if __CYGWIN__ || __WIN32__
	dw_printf ("end of kiss_init: nullmodem_fd = %d\n", nullmodem_fd);
#endif

#endif
}


/*
 * Returns fd for master side of pseudo terminal or MYFDERROR for error.
 */

#if ! __WIN32__

static MYFDTYPE kiss_open_pt (void)
{
	int fd;
	char *pts;
	struct termios ts;
	int e;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kiss_open_pt (  )\n");
#endif
	

	fd = posix_openpt(O_RDWR|O_NOCTTY);

	if (fd == MYFDERROR
	    || grantpt (fd) == MYFDERROR
	    || unlockpt (fd) == MYFDERROR
	    || (pts = ptsname (fd)) == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not create pseudo terminal for KISS TNC.\n");
	  return (MYFDERROR);
	}

	strlcpy (pt_slave_name, pts, sizeof(pt_slave_name));

	e = tcgetattr (fd, &ts);
	if (e != 0) { 
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Can't get pseudo terminal attributes, err=%d\n", e);
	  perror ("pt tcgetattr"); 
	}

	cfmakeraw (&ts);
	
	ts.c_cc[VMIN] = 1;	/* wait for at least one character */
	ts.c_cc[VTIME] = 0;	/* no fancy timing. */
				

	e = tcsetattr (fd, TCSANOW, &ts);
	if (e != 0) { 
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Can't set pseudo terminal attributes, err=%d\n", e);
	  perror ("pt tcsetattr"); 
	}

/*
 * We had a problem here since the beginning.
 * If no one was reading from the other end of the pseudo
 * terminal, the buffer space would eventually fill up,
 * the write here would block, and the receive decode
 * thread would get stuck.
 *
 * March 2016 - A "select" was put before the read to
 * solve a different problem.  With that in place, we can
 * now use non-blocking I/O and detect the buffer full
 * condition here.
 */

	// text_color_set(DW_COLOR_DEBUG);
	// dw_printf("Debug: Try using non-blocking mode for pseudo terminal.\n");

	int flags = fcntl(fd, F_GETFL, 0);
	e = fcntl (fd, F_SETFL, flags | O_NONBLOCK);
	if (e != 0) { 
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Can't set pseudo terminal to nonblocking, fcntl returns %d, errno = %d\n", e, errno);
	  perror ("pt fcntl"); 
	}

	text_color_set(DW_COLOR_INFO);
	dw_printf("Virtual KISS TNC is available on %s\n", pt_slave_name);
	

#if 1
	// Sample code shows this. Why would we open it here?
	// On Ubuntu, the slave side disappears after a few
	// seconds if no one opens it.  Same on Raspian which
	// is also based on Debian.
	// Need to revisit this.  

	MYFDTYPE pt_slave_fd;

	pt_slave_fd = open(pt_slave_name, O_RDWR|O_NOCTTY);

	if (pt_slave_fd < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Can't open %s\n", pt_slave_name);	
	    perror ("");
	    return MYFDERROR;
	}
#endif

/*
 * The device name is not the same every time.
 * This is inconvenient for the application because it might
 * be necessary to change the device name in the configuration.
 * Create a symlink, /tmp/kisstnc, so the application configuration
 * does not need to change when the pseudo terminal name changes.
 */

	unlink (TMP_KISSTNC_SYMLINK);


// TODO: Is this removed when application exits?

	if (symlink (pt_slave_name, TMP_KISSTNC_SYMLINK) == 0) {
	    dw_printf ("Created symlink %s -> %s\n", TMP_KISSTNC_SYMLINK, pt_slave_name);
	}
	else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to create symlink %s\n", TMP_KISSTNC_SYMLINK);	
	    perror ("");
	}

	return (fd);
}

#endif

/*
 * Returns fd for our side of null modem or MYFDERROR for error.
 */


#if __CYGWIN__ || __WIN32__

static MYFDTYPE kiss_open_nullmodem (char *devicename)
{

#if __WIN32__

	MYFDTYPE fd;
	DCB dcb;
	int ok;	
	char bettername[50];

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kiss_open_nullmodem ( '%s' )\n", devicename);
#endif
	
#if DEBUG9
	log_fp = fopen ("kiss-debug.txt", "w");
#endif

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
	  dw_printf ("ERROR - Could not connect to %s side of null modem for Windows KISS TNC.\n", devicename);
	  return (MYFDERROR);
	}

	/* Reference: http://msdn.microsoft.com/en-us/library/windows/desktop/aa363201(v=vs.85).aspx */

	memset (&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(DCB);

	ok = GetCommState (fd, &dcb);
	if (! ok) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("kiss_open_nullmodem: GetCommState failed.\n");
	}

	/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa363214(v=vs.85).aspx */

	dcb.DCBlength = sizeof(DCB);
	dcb.BaudRate = CBR_9600;	// shouldn't matter 
	dcb.fBinary = 1;
	dcb.fParity = 0;
	dcb.fOutxCtsFlow = 0;
	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = 0;
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
	  dw_printf ("kiss_open_nullmodem: SetCommState failed.\n");
	}

	text_color_set(DW_COLOR_INFO);
	dw_printf("Virtual KISS TNC is connected to %s side of null modem.\n", devicename);

#else

/* Cygwin version. */

	int fd;
	struct termios ts;
	int e;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kiss_open_nullmodem ( '%s' )\n", devicename);
#endif

	fd = open (devicename, O_RDWR);

	if (fd == MYFDERROR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not connect to %s side of null modem for Windows KISS TNC.\n", devicename);
	  return (MYFDERROR);
	}

	e = tcgetattr (fd, &ts);
	if (e != 0) { perror ("nm tcgetattr"); }

	cfmakeraw (&ts);
	
	ts.c_cc[VMIN] = 1;	/* wait for at least one character */
	ts.c_cc[VTIME] = 0;	/* no fancy timing. */

	e = tcsetattr (fd, TCSANOW, &ts);
	if (e != 0) { perror ("nm tcsetattr"); }

	text_color_set(DW_COLOR_INFO);
	dw_printf("Virtual KISS TNC is connected to %s side of null modem.\n", devicename);

#endif

	return (fd);
}

#endif




/*-------------------------------------------------------------------
 *
 * Name:        kiss_send_rec_packet
 *
 * Purpose:     Send a received packet or text string to the client app.
 *
 * Inputs:	chan		- Channel number where packet was received.
 *				  0 = first, 1 = second if any.
 *
 *		pp		- Identifier for packet object.
 *
 *		fbuf		- Address of raw received frame buffer
 *				  or a text string.
 *
 *		flen		- Length of raw received frame not including the FCS
 *				  or -1 for a text string.
 *
 * Description:	Send message to client.
 *		We really don't care if anyone is listening or not.
 *		I don't even know if we can find out.
 *
 *--------------------------------------------------------------------*/


void kiss_send_rec_packet (int chan, unsigned char *fbuf,  int flen)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN + 2];
	int kiss_len;
	int err;

#if ! __WIN32__
	if (pt_master_fd == MYFDERROR) {
	  return;
	}
#endif

#if __CYGWIN__ || __WIN32__

	if (nullmodem_fd == MYFDERROR) {
	  return;
	}
#endif
	
	if (flen < 0) {
	  flen = strlen((char*)fbuf);
	  if (kiss_debug) {
	    kiss_debug_print (TO_CLIENT, "Fake command prompt", fbuf, flen);
	  }
	  strlcpy ((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
	  kiss_len = strlen((char *)kiss_buff);
	}
	else {


	  unsigned char stemp[AX25_MAX_PACKET_LEN + 1];
	 
	  assert (flen < (int)(sizeof(stemp)));

	  stemp[0] = (chan << 4) + 0;
	  memcpy (stemp+1, fbuf, flen);

	  if (kiss_debug >= 2) {
	    /* AX.25 frame with the CRC removed. */
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("\n");
	    dw_printf ("Packet content before adding KISS framing and any escapes:\n");
	    hex_dump (fbuf, flen);
	  }

	  kiss_len = kiss_encapsulate (stemp, flen+1, kiss_buff);

	  /* This has KISS framing and escapes for sending to client app. */

	  if (kiss_debug) {
	    kiss_debug_print (TO_CLIENT, NULL, kiss_buff, kiss_len);
	  }

	}

#if ! __WIN32__

/* Pseudo terminal for Cygwin and Linux. */

        err = write (pt_master_fd, kiss_buff, (size_t)kiss_len);

	if (err == -1 && errno == EWOULDBLOCK) {
	  text_color_set (DW_COLOR_INFO);
	  dw_printf ("KISS SEND - Discarding message because no one is listening.\n");
	  dw_printf ("This happens when you use the -p option and don't read from the pseudo terminal.\n");
	}
	else if (err != kiss_len)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError sending KISS message to client application on pseudo terminal.  fd=%d, len=%d, write returned %d, errno = %d\n\n",
		pt_master_fd, kiss_len, err, errno);
	  perror ("pt write"); 
	}

#endif

#if __CYGWIN__ || __WIN32__


/*
 * This write can block if nothing is connected to the other end.
 * The solution is found in the com0com ReadMe file:
 *
 *	Q. My application hangs during its startup when it sends anything to one paired
 *	   COM port. The only way to unhang it is to start HyperTerminal, which is connected
 *	   to the other paired COM port. I didn't have this problem with physical serial
 *	   ports.
 *	A. Your application can hang because receive buffer overrun is disabled by
 *	   default. You can fix the problem by enabling receive buffer overrun for the
 *	   receiving port. Also, to prevent some flow control issues you need to enable
 *	   baud rate emulation for the sending port. So, if your application use port CNCA0
 *	   and other paired port is CNCB0, then:
 *	
 *	   1. Launch the Setup Command Prompt shortcut.
 *	   2. Enter the change commands, for example:
 *	
 *	      command> change CNCB0 EmuOverrun=yes
 *	      command> change CNCA0 EmuBR=yes
 */

#if __WIN32__

	  DWORD nwritten; 

	  /* Without this, write blocks while we are waiting on a read. */
	  static OVERLAPPED ov_wr;
	  memset (&ov_wr, 0, sizeof(ov_wr));

          if ( ! WriteFile (nullmodem_fd, kiss_buff, kiss_len, &nwritten, &ov_wr))
	  {
	    err = GetLastError();
	    if (err != ERROR_IO_PENDING) 
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError sending KISS message to client application thru null modem.  Error %d.\n\n", (int)GetLastError());
	      //CloseHandle (nullmodem_fd);
	      //nullmodem_fd = MYFDERROR;
	    }
	  }
	  else if ((int)nwritten != kiss_len)
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nError sending KISS message to client application thru null modem.  Only %d of %d written.\n\n", (int)nwritten, kiss_len);
	    //CloseHandle (nullmodem_fd);
	    //nullmodem_fd = MYFDERROR;
	  }

#else
          err = write (nullmodem_fd, kiss_buf, (size_t)kiss_len);
	  if (err != len)
	  {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nError sending KISS message to client application thru null modem. err=%d\n\n", err);
	    //close (nullmodem_fd);
	    //nullmodem_fd = MYFDERROR;
	  }
#endif

#endif

} /* kiss_send_rec_packet */




/*-------------------------------------------------------------------
 *
 * Name:        kiss_get
 *
 * Purpose:     Read one byte from the KISS client app.
 *
 * Global In:	nullmodem_fd  (Windows)  or pt_master_fd  (Linux)
 *
 * Returns:	one byte (value 0 - 255) or terminate thread on error.
 *
 * Description:	There is room for improvment here.  Reading one byte
 *		at a time is inefficient.  We could read a large block
 *		into a local buffer and return a byte from that most of the time.
 *		Is it worth the effort?  I don't know.  With GHz processors and
 *		the low data rate here it might not make a noticable difference.
 *
 *--------------------------------------------------------------------*/


static int kiss_get (/* MYFDTYPE fd*/ void )
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

	  if ( ! ReadFile (nullmodem_fd, &ch, 1, &n, &ov_rd)) 
	  {
	    int err1 = GetLastError();

	    if (err1 == ERROR_IO_PENDING) 
	    {
	      /* Wait for completion. */

	      if (WaitForSingleObject (ov_rd.hEvent, INFINITE) == WAIT_OBJECT_0) 
	      {
	        if ( ! GetOverlappedResult (nullmodem_fd, &ov_rd, &n, 1))
	        {
	          int err3 = GetLastError();

	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("\nKISS GetOverlappedResult error %d.\n\n", err3);
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
	      dw_printf ("\nKISS ReadFile error %d. Closing connection.\n\n", err1);
	      CloseHandle (nullmodem_fd);
	      nullmodem_fd = MYFDERROR;
	      //pthread_exit (NULL);
	    }
	  }

	}	/* end while n==0 */

	CloseHandle(ov_rd.hEvent); 

	if (n != 1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nKISS failed to get one byte. n=%d.\n\n", (int)n);

#if DEBUG9
	  fprintf (log_fp, "n=%d\n", n);
#endif
	}


#else		/* Linux/Cygwin version */

	int n = 0;
	fd_set fd_in, fd_ex;
	int rc;

	while ( n == 0 ) {

/*
 * Since the beginning we've always had a couple annoying problems with
 * the pseudo terminal KISS interface.
 * When using "kissattach" we would sometimes get the error message:
 *
 *	kissattach: Error setting line discipline: TIOCSETD: Device or resource busy
 *	Are you sure you have enabled MKISS support in the kernel
 *	or, if you made it a module, that the module is loaded?
 *
 * martinhpedersen came up with the interesting idea of putting in a "select"
 * before the "read" and explained it like this:
 *
 *	"Reading from master fd of the pty before the client has connected leads
 *	 to trouble with kissattach.  Use select to check if the slave has sent
 *	 any data before trying to read from it."
 *
 *	"This fix resolves the issue by not reading from the pty's master fd, until
 *	 kissattach has opened and configured the slave. This is implemented using
 *	 select() to wait for data before reading from the master fd."
 *
 * The submitted code looked like this:
 *
 *	FD_ZERO(&fd_in);
 *	rc = select(pt_master_fd + 1, &fd_in, NULL, &fd_in, NULL);
 *
 * That doesn't look right to me for a couple reasons.
 * First, I would expect to use FD_SET for the fd.
 * Second, using the same bit mask for two arguments doesn't seem
 * like a good idea because select modifies them.
 * When I tried running it, we don't get the failure message
 * anymore but the select never returns so we can't read data from
 * the KISS client app.
 *
 * I think this is what we want.
 *
 * Tested on Raspian (ARM) and Ubuntu (x86_64).
 * We don't get the error from kissattach anymore.
 */

	  FD_ZERO(&fd_in);
	  FD_SET(pt_master_fd, &fd_in);

	  FD_ZERO(&fd_ex);
	  FD_SET(pt_master_fd, &fd_ex);

	  rc = select(pt_master_fd + 1, &fd_in, NULL, &fd_ex, NULL);

#if 0
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("select returns %d, errno=%d, fd=%d, fd_in=%08x, fd_ex=%08x\n", rc, errno, pt_master_fd, *((int*)(&fd_in)), *((int*)(&fd_in)));
#endif

	  if (rc == 0)
	  {
	    continue;		// When could we get a 0?
	  }

	  if (rc == MYFDERROR
	      || (n = read(pt_master_fd, &ch, (size_t)1)) != 1)
	  {

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nError receiving KISS message from client application.  Closing %s.\n\n", pt_slave_name);
	    perror ("");

	    close (pt_master_fd);

	    pt_master_fd = MYFDERROR;
	    unlink (TMP_KISSTNC_SYMLINK);
	    pthread_exit (NULL);
	  }
	}

#endif

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kiss_get(%d) returns 0x%02x\n", fd, ch);
#endif

#if DEBUG9
	fprintf (log_fp, "%02x %c %c", ch, 
			isprint(ch) ? ch : '.' , 
			(isupper(ch>>1) || isdigit(ch>>1) || (ch>>1) == ' ') ? (ch>>1) : '.');
	if (ch == FEND) fprintf (log_fp, "  FEND");
	if (ch == FESC) fprintf (log_fp, "  FESC");
	if (ch == TFEND) fprintf (log_fp, "  TFEND");
	if (ch == TFESC) fprintf (log_fp, "  TFESC");
	if (ch == '\r') fprintf (log_fp, "  CR");
	if (ch == '\n') fprintf (log_fp, "  LF");
	fprintf (log_fp, "\n");
	if (ch == FEND) fflush (log_fp);
#endif
	return (ch);
}


/*-------------------------------------------------------------------
 *
 * Name:        kiss_listen_thread
 *
 * Purpose:     Read messages from serial port KISS client application.
 *
 * Global In:	nullmodem_fd  (Windows)  or pt_master_fd  (Linux)
 *
 * Description:	Reads bytes from the KISS client app and
 *		sends them to kiss_rec_byte for processing.
 *
 *--------------------------------------------------------------------*/


static THREAD_F kiss_listen_thread (void *arg)
{
	unsigned char ch;
			
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kiss_listen_thread ( %d )\n", fd);
#endif


	while (1) {
	  ch = kiss_get();
	  kiss_rec_byte (&kf, ch, kiss_debug, kiss_send_rec_packet);
	}

#if __WIN32__
	return(0);
#else
	return (THREAD_F) 0;	/* Unreachable but avoids compiler warning. */
#endif
}

/* end kiss.c */

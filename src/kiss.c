//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2016, 2017  John Langner, WB2OSZ
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
 *		This file implements it with a pseudo terminal for Linux only.
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
 *			_0	Data Frame	AX.25 frame in raw format.
 *
 *			_1	TXDELAY		See explanation in xmit.c.
 *
 *			_2	Persistence	"	"
 *
 *			_3 	SlotTime	"	"
 *
 *			_4	TXtail		"	"
 *						Spec says it is obsolete but Xastir
 *						sends it and we respect it.
 *
 *			_5	FullDuplex	Ignored.
 *		
 *			_6	SetHardware	TNC specific.
 *			
 *			FF	Return		Exit KISS mode.  Ignored.
 *
 *
 *		Messages sent to client application:
 *
 *			_0	Data Frame	Received AX.25 frame in raw format.
 *
 *		
 * Platform differences:
 *
 *		For the Linux case,
 *			We supply a pseudo terminal for use by other applications.
 *
 * Version 1.5:	Split serial port version off into its own file.
 *
 *---------------------------------------------------------------*/


#if __WIN32__			// Stub for Windows.

#include "direwolf.h"
#include "kiss.h"

void kisspt_init (struct misc_config_s *mc)
{
	return;
}

void kisspt_set_debug (int n)
{
	return;
}

void kisspt_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen, struct kissport_status_s *kps, int client)
{
	return;
}


#else				// Rest of file is for Linux only.


#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>


#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "kiss.h"
#include "kiss_frame.h"
#include "xmit.h"


/*
 * Accumulated KISS frame and state of decoder.
 */

static kiss_frame_t kf;


/*
 * These are for a Linux pseudo terminal.
 */

static int pt_master_fd = -1;		/* File descriptor for my end. */

static char pt_slave_name[32];		/* Pseudo terminal slave name  */
					/* like /dev/pts/999 */


/*
 * Symlink to pseudo terminal name which changes.
 */

#define TMP_KISSTNC_SYMLINK "/tmp/kisstnc"


static void * kisspt_listen_thread (void *arg);


static int kisspt_debug = 0;		/* Print information flowing from and to client. */

void kisspt_set_debug (int n)
{	
	kisspt_debug = n;
}


/* In server.c.  Should probably move to some misc. function file. */

void hex_dump (unsigned char *p, int len);





/*-------------------------------------------------------------------
 *
 * Name:        kisspt_init
 *
 * Purpose:     Set up a pseudo terminal acting as a virtual KISS TNC.
 *		
 *
 * Inputs:
 *
 * Outputs:	
 *
 * Description:	(1) Create a pseudo terminal for the client to use.
 *		(2) Start a new thread to listen for commands from client app
 *		    so the main application doesn't block while we wait.
 *
 *
 *--------------------------------------------------------------------*/

static int kisspt_open_pt (void);


void kisspt_init (struct misc_config_s *mc)
{

	pthread_t kiss_pterm_listen_tid;
	int e;

	memset (&kf, 0, sizeof(kf));

/*
 * This reads messages from client.
 */
	pt_master_fd = -1;

	if (mc->enable_kiss_pt) {

	  pt_master_fd = kisspt_open_pt ();

	  if (pt_master_fd != -1) {
	    e = pthread_create (&kiss_pterm_listen_tid, (pthread_attr_t*)NULL, kisspt_listen_thread, NULL);
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create kiss listening thread for Linux pseudo terminal");
	    }
	  }
	}
	else {
	  //text_color_set(DW_COLOR_INFO);
	  //dw_printf ("Use -p command line option to enable KISS pseudo terminal.\n");
	}


#if DEBUG
	text_color_set (DW_COLOR_DEBUG);

	dw_printf ("end of kisspt_init: pt_master_fd = %d\n", pt_master_fd);
#endif

}


/*
 * Returns fd for master side of pseudo terminal or -1 for error.
 */

static int kisspt_open_pt (void)
{
	int fd;
	char *pts;
	struct termios ts;
	int e;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kisspt_open_pt (  )\n");
#endif

	fd = posix_openpt(O_RDWR|O_NOCTTY);

	if (fd == -1
	    || grantpt (fd) == -1
	    || unlockpt (fd) == -1
	    || (pts = ptsname (fd)) == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Could not create pseudo terminal for KISS TNC.\n");
	  return (-1);
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
	// seconds if no one opens it.  Same on Raspbian which
	// is also based on Debian.
	// Need to revisit this.  

	int pt_slave_fd;

	pt_slave_fd = open(pt_slave_name, O_RDWR|O_NOCTTY);

	if (pt_slave_fd < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Can't open %s\n", pt_slave_name);	
	    perror ("");
	    return -1;
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



/*-------------------------------------------------------------------
 *
 * Name:        kisspt_send_rec_packet
 *
 * Purpose:     Send a received packet or text string to the client app.
 *
 * Inputs:	chan		- Channel number where packet was received.
 *				  0 = first, 1 = second if any.
 *
 *		kiss_cmd	- Usually KISS_CMD_DATA_FRAME but we can also have
 *				  KISS_CMD_SET_HARDWARE when responding to a query.
 *
 *		pp		- Identifier for packet object.
 *
 *		fbuf		- Address of raw received frame buffer
 *				  or a text string.
 *
 *		flen		- Length of raw received frame not including the FCS
 *				  or -1 for a text string.
 *
 *		kps, client	- Not used for pseudo terminal.
 *				  Here so that 3 related functions all have
 *				  the same parameter list.
 *
 * Description:	Send message to client.
 *		We really don't care if anyone is listening or not.
 *		I don't even know if we can find out.
 *
 *--------------------------------------------------------------------*/


void kisspt_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen, struct kissport_status_s *kps, int client)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN + 2];
	int kiss_len;
	int err;


	if (pt_master_fd == -1) {
	  return;
	}

	if (flen < 0) {
	  flen = strlen((char*)fbuf);
	  if (kisspt_debug) {
	    kiss_debug_print (TO_CLIENT, "Fake command prompt", fbuf, flen);
	  }
	  strlcpy ((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
	  kiss_len = strlen((char *)kiss_buff);
	}
	else {

	  unsigned char stemp[AX25_MAX_PACKET_LEN + 1];
	 
	  if (flen > (int)(sizeof(stemp)) - 1) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nPseudo Terminal KISS buffer too small.  Truncated.\n\n");
	    flen = (int)(sizeof(stemp)) - 1;
	  }

	  stemp[0] = (chan << 4) | kiss_cmd;
	  memcpy (stemp+1, fbuf, flen);

	  if (kisspt_debug >= 2) {
	    /* AX.25 frame with the CRC removed. */
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("\n");
	    dw_printf ("Packet content before adding KISS framing and any escapes:\n");
	    hex_dump (fbuf, flen);
	  }

	  kiss_len = kiss_encapsulate (stemp, flen+1, kiss_buff);

	  /* This has KISS framing and escapes for sending to client app. */

	  if (kisspt_debug) {
	    kiss_debug_print (TO_CLIENT, NULL, kiss_buff, kiss_len);
	  }

	}

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

} /* kisspt_send_rec_packet */




/*-------------------------------------------------------------------
 *
 * Name:        kisspt_get
 *
 * Purpose:     Read one byte from the KISS client app.
 *
 * Global In:	pt_master_fd
 *
 * Returns:	one byte (value 0 - 255) or terminate thread on error.
 *
 * Description:	There is room for improvement here.  Reading one byte
 *		at a time is inefficient.  We could read a large block
 *		into a local buffer and return a byte from that most of the time.
 *		Is it worth the effort?  I don't know.  With GHz processors and
 *		the low data rate here it might not make a noticeable difference.
 *
 *--------------------------------------------------------------------*/


static int kisspt_get (void)
{
	unsigned char ch;

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

	  if (rc == -1
	      || (n = read(pt_master_fd, &ch, (size_t)1)) != 1)
	  {

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nError receiving KISS message from client application.  Closing %s.\n\n", pt_slave_name);
	    perror ("");

	    close (pt_master_fd);

	    pt_master_fd = -1;
	    unlink (TMP_KISSTNC_SYMLINK);
	    pthread_exit (NULL);
	  }
	}

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kisspt_get(%d) returns 0x%02x\n", fd, ch);
#endif

	return (ch);
}


/*-------------------------------------------------------------------
 *
 * Name:        kisspt_listen_thread
 *
 * Purpose:     Read messages from serial port KISS client application.
 *
 * Global In:
 *
 * Description:	Reads bytes from the KISS client app and
 *		sends them to kiss_rec_byte for processing.
 *
 *--------------------------------------------------------------------*/

static void * kisspt_listen_thread (void *arg)
{
	unsigned char ch;
			
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kisspt_listen_thread ( %d )\n", fd);
#endif


	while (1) {
	  ch = kisspt_get();
	  kiss_rec_byte (&kf, ch, kisspt_debug, NULL, -1, kisspt_send_rec_packet);
	}

	return (void *) 0;	/* Unreachable but avoids compiler warning. */
}

#endif		// Linux version

/* end kiss.c */

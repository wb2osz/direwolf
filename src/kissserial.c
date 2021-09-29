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
 * Module:      kissserial.c
 *
 * Purpose:   	Act as a virtual KISS TNC for use by other packet radio applications.
 *		This file provides the service by good old fashioned serial port.
 *		Other files implement a pseudo terminal or TCP KISS interface.
 *		
 * Description:	This implements the KISS TNC protocol as described in:
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
 *		This file implements KISS over a serial port.
 *		It should behave pretty much the same for both Windows and Linux.
 *
 *		When running a client application on Windows, two applications
 *		can be connected together using a a "Null-modem emulator"
 *		such as com0com from http://sourceforge.net/projects/com0com/
 *
 *		(When running a client application, on the same host, with Linux,
 *		a pseudo terminal can be used for old applications.  More modern
 *		applications will generally have AGW and/or KISS over TCP.)
 *
 *
 * version 1.5:	Split out from kiss.c, simplified, consistent for Windows and Linux.
 *		Add polling option for use with Bluetooth.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "ax25_pad.h"
#include "textcolor.h"
#include "serial_port.h"
#include "kissserial.h"
#include "kiss_frame.h"
#include "xmit.h"


/*
 * Save Configuration for later use.
 */

static struct misc_config_s  *g_misc_config_p;

/*
 * Accumulated KISS frame and state of decoder.
 */

static kiss_frame_t kf;		


/*
 * The serial port device handle.
 * MYFD... are defined in kissserial.h
 */

static MYFDTYPE serialport_fd = MYFDERROR;




// TODO:  define in one place, use everywhere.
#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

static THREAD_F kissserial_listen_thread (void *arg);


static int kissserial_debug = 0;		/* Print information flowing from and to client. */

void kissserial_set_debug (int n) 
{	
	kissserial_debug = n;
}


/* In server.c.  Should probably move to some misc. function file. */

void hex_dump (unsigned char *p, int len);




/*-------------------------------------------------------------------
 *
 * Name:        kissserial_init
 *
 * Purpose:     Set up a serial port acting as a virtual KISS TNC.
 *		
 * Inputs:	mc->
 *		    kiss_serial_port	- Name of device for real or virtual serial port.
 *		    kiss_serial_speed	- Speed, bps, or 0 meaning leave it alone.
 *		    kiss_serial_poll	- When non-zero, poll each n seconds to see if
 *					  device has appeared.
 *
 * Outputs:	
 *
 * Description:	(1) Open file descriptor for the device.
 *		(2) Start a new thread to listen for commands from client app
 *		    so the main application doesn't block while we wait.
 *
 *--------------------------------------------------------------------*/


void kissserial_init (struct misc_config_s *mc)
{

#if __WIN32__
	HANDLE kissserial_listen_th;
#else
	pthread_t kissserial_listen_tid;
	int e;
#endif

	g_misc_config_p = mc;

	memset (&kf, 0, sizeof(kf));


	if (strlen(g_misc_config_p->kiss_serial_port) > 0) {

	  if (g_misc_config_p->kiss_serial_poll == 0) {

	    // Normal case, try to open the serial port at start up time.

	    serialport_fd = serial_port_open (g_misc_config_p->kiss_serial_port, g_misc_config_p->kiss_serial_speed);

	    if (serialport_fd != MYFDERROR) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Opened %s for serial port KISS.\n", g_misc_config_p->kiss_serial_port);
	    }
	    else {
	      // An error message was already displayed.
	    }
	  }
	  else {

	    // Polling case.   Defer until read and device not opened.
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Will be checking periodically for %s\n", g_misc_config_p->kiss_serial_port);
	  }

	  if (g_misc_config_p->kiss_serial_poll != 0 || serialport_fd != MYFDERROR) {
#if __WIN32__
	    kissserial_listen_th = (HANDLE)_beginthreadex (NULL, 0, kissserial_listen_thread, NULL, 0, NULL);
	    if (kissserial_listen_th == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not create kiss serial thread\n");
	      return;
	    }
#else
	    e = pthread_create (&kissserial_listen_tid, NULL, kissserial_listen_thread, NULL);
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create kiss serial thread.");
	    }
#endif
	  }
	}


#if DEBUG
	text_color_set (DW_COLOR_DEBUG);

	dw_printf ("end of kiss_init: serialport_fd = %d, polling = %d\n", serialport_fd, g_misc_config_p->kiss_serial_poll);
#endif
}




/*-------------------------------------------------------------------
 *
 * Name:        kissserial_send_rec_packet
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
 *		kps
 *		client		- Not used for serial port version.
 *				  Here so that 3 related functions all have
 *				  the same parameter list.
 *
 * Description:	Send message to client.
 *		We really don't care if anyone is listening or not.
 *		I don't even know if we can find out.
 *
 *--------------------------------------------------------------------*/


void kissserial_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf,  int flen,
		struct kissport_status_s *notused1, int notused2)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN + 2];
	int kiss_len;
	int err;


/*
 * Quietly discard if we don't have open connection.
 */
	if (serialport_fd == MYFDERROR) {
	  return;
	}
	
	if (flen < 0) {
	  flen = strlen((char*)fbuf);
	  if (kissserial_debug) {
	    kiss_debug_print (TO_CLIENT, "Fake command prompt", fbuf, flen);
	  }
	  strlcpy ((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
	  kiss_len = strlen((char *)kiss_buff);
	}
	else {

	  unsigned char stemp[AX25_MAX_PACKET_LEN + 1];
	 
	  if (flen > (int)(sizeof(stemp)) - 1) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nSerial Port KISS buffer too small.  Truncated.\n\n");
	    flen = (int)(sizeof(stemp)) - 1;
	  }

	  stemp[0] = (chan << 4) | kiss_cmd;
	  memcpy (stemp+1, fbuf, flen);

	  if (kissserial_debug >= 2) {
	    /* AX.25 frame with the CRC removed. */
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("\n");
	    dw_printf ("Packet content before adding KISS framing and any escapes:\n");
	    hex_dump (fbuf, flen);
	  }

	  kiss_len = kiss_encapsulate (stemp, flen+1, kiss_buff);

	  /* This has KISS framing and escapes for sending to client app. */

	  if (kissserial_debug) {
	    kiss_debug_print (TO_CLIENT, NULL, kiss_buff, kiss_len);
	  }
	}

/*
 * This write can block on Windows if using the virtual null modem
 * and nothing is connected to the other end.
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

	err = serial_port_write (serialport_fd, (char*)kiss_buff, kiss_len);

	if (err != kiss_len)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError sending KISS message to client application thru serial port.\n\n");
	  serial_port_close (serialport_fd);
	  serialport_fd = MYFDERROR;
	}

} /* kissserial_send_rec_packet */



/*-------------------------------------------------------------------
 *
 * Name:        kissserial_get
 *
 * Purpose:     Read one byte from the KISS client app.
 *
 * Global In:	serialport_fd
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


static int kissserial_get (void)
{
	int ch;		// normally 0-255 but -1 for error.


	if (g_misc_config_p->kiss_serial_poll == 0) {
/*
 * Normal case, was opened at start up time.
 */  
	  ch = serial_port_get1 (serialport_fd);

	  if (ch < 0) {

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nSerial Port KISS read error. Closing connection.\n\n");
	    serial_port_close (serialport_fd);
	    serialport_fd = MYFDERROR;
#if __WIN32__
	    ExitThread (0);
#else
	    pthread_exit (NULL);
#endif
	  }

#if DEBUGx
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("kissserial_get(%d) returns 0x%02x\n", fd, ch);
#endif
	  return (ch);
	}

/*
 * Polling case.  Wait until device is present and open.
 */
	while (1) {

	  if (serialport_fd != MYFDERROR) {

	    // Open, try to read.

	    ch = serial_port_get1 (serialport_fd);

	    if (ch >= 0) {
	       return (ch);
	    }

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nSerial Port KISS read error. Closing connection.\n\n");
	    serial_port_close (serialport_fd);
	    serialport_fd = MYFDERROR;
	  }
	  else {

	    // Not open.  Wait for it to appear and try opening.

	    struct stat buf;

	    SLEEP_SEC (g_misc_config_p->kiss_serial_poll);

	    if (stat(g_misc_config_p->kiss_serial_port, &buf) == 0) {

	      // It's there now.  Try to open.

	      serialport_fd = serial_port_open (g_misc_config_p->kiss_serial_port, g_misc_config_p->kiss_serial_speed);

	      if (serialport_fd != MYFDERROR) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("\nOpened %s for serial port KISS.\n\n", g_misc_config_p->kiss_serial_port);

	        memset (&kf, 0, sizeof(kf));		// Start with clean state.
	      }
	      else {
	        // An error message was already displayed.
	      }
	    }
	  }
	}

} /* end kissserial_get */



/*-------------------------------------------------------------------
 *
 * Name:        kissserial_listen_thread
 *
 * Purpose:     Read messages from serial port KISS client application.
 *
 * Global In:	serialport_fd  
 *
 * Description:	Reads bytes from the serial port KISS client app and
 *		sends them to kiss_rec_byte for processing.
 *		kiss_rec_byte is a common function used by all 3 KISS
 *		interfaces: serial port, pseudo terminal, and TCP.
 *
 *--------------------------------------------------------------------*/


static THREAD_F kissserial_listen_thread (void *arg)
{
	unsigned char ch;
			
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissserial_listen_thread ( %d )\n", fd);
#endif

	while (1) {
	  ch = kissserial_get();
	  kiss_rec_byte (&kf, ch, kissserial_debug, NULL, -1, kissserial_send_rec_packet);
	}

#if __WIN32__
	return(0);
#else
	return (THREAD_F) 0;	/* Unreachable but avoids compiler warning. */
#endif
}

/* end kissserial.c */

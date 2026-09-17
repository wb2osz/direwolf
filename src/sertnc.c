
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2024  John Langner, WB2OSZ
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
 * Module:      nettnc.c
 *
 * Purpose:   	Attach to Network KISS TNC(s) for NCHANNEL config file item(s).
 *		
 * Description:	Called once at application start up.
 *
 *---------------------------------------------------------------*/


#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this
#else 
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stddef.h>

#include "textcolor.h"
#include "audio.h"		// configuration.
#include "kiss.h"
#include "ax25_pad.h"		// for AX25_MAX_PACKET_LEN
#include "dlq.h"		// received packet queue

#include "serial_port.h"
#include "sertnc.h"
#include "tnc_common.h"



// TODO: define macros in common locaation to hide platform specifics.

#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

#if __WIN32__
static HANDLE sertnc_listen_th[MAX_TOTAL_CHANS];
static THREAD_F sertnc_listen_thread (void *arg);
#else
static pthread_t sertnc_listen_tid[MAX_TOTAL_CHANS];
static THREAD_F sertnc_listen_thread (void *arg);	
#endif

static int s_kiss_debug = 0;


/*-------------------------------------------------------------------
 *
 * Name:        sertnc_init
 *
 * Purpose:      Attach to Serial KISS TNC(s) for SCHANNEL config file item(s).
 *
 * Inputs:	pa              - Address of structure of type audio_s.
 *
 *		debug ? TBD
 *
 *
 * Returns:	0 for success, -1 for failure.
 *
 * Description:	Called once at direwolf application start up time.
 *		Calls sertnc_attach for each SCHANNEL configuration item.
 *
 *--------------------------------------------------------------------*/

void sertnc_init (struct audio_s *pa)
{
	for (int i = 0; i < MAX_TOTAL_CHANS; i++) {

	  if (pa->chan_medium[i] == MEDIUM_SERTNC) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Channel %d: Serial TNC %s %d\n", i, pa->sertnc_device[i], pa->sertnc_baud[i]);
	    int e = sertnc_attach (i, pa->sertnc_device[i], pa->sertnc_baud[i]);
	    if (e < 0) {
	      exit (1);
	    }
	  }
	}

}  // end nettnc_init



/*-------------------------------------------------------------------
 *
 * Name:        sertnc_attach
 *
 * Purpose:      Attach to one Serial KISS TNC.
 *
 * Inputs:	chan	- channel number from SCHANNEL configuration.
 *
 *		device	- Serial device name.  Something like "/dev/ttyS0" or "COM4".
 *
 *		baud	- Serial baud rate.  Typically 9600.
 *
 * Returns:	0 for success, -1 for failure.
 *
 * Description:	This starts up a thread, for each device, which listens to the port and
 *		dispatches the messages to the corresponding callback functions.
 *		It will also attempt to re-establish communication with the
 *		device if it goes away.
 *
 *--------------------------------------------------------------------*/

static char s_tnc_device[MAX_TOTAL_CHANS][80];
static int s_tnc_baud[MAX_TOTAL_CHANS];
static volatile MYFDTYPE s_tnc_fd[MAX_TOTAL_CHANS];	// File descriptor. MYFDERROR for invalid.


int sertnc_attach (int chan, char *device, int baud)
{
	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);

	strlcpy (s_tnc_device[chan], device, sizeof(s_tnc_device[chan]));
	s_tnc_baud[chan] = baud;
	s_tnc_fd[chan] = MYFDERROR;

	s_tnc_fd[chan] = serial_port_open (s_tnc_device[chan], s_tnc_baud[chan]);

	if (s_tnc_fd[chan] == MYFDERROR) {
	  return (-1);
	}


/*
 * Read frames from the serial TNC.
 * If the TNC disappears, try to reestablish communication.
 */


#if __WIN32__
	sertnc_listen_th[chan] = (HANDLE)_beginthreadex (NULL, 0, sertnc_listen_thread, (void *)(ptrdiff_t)chan, 0, NULL);
	if (sertnc_listen_th[chan] == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: Could not create serial TNC listening thread\n");
	  return (-1);
	}
#else
	int e = pthread_create (&sertnc_listen_tid[chan], NULL, sertnc_listen_thread, (void *)(ptrdiff_t)chan);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Internal error: Could not create serial TNC listening thread");
	  return (-1);
	}
#endif

// TNC initialization if specified.

//	if (s_tnc_init_func != NULL) {
//	  e = (*s_tnc_init_func)();
//	  return (e);
//	}

	return (0);

}  // end sertnc_attach



/*-------------------------------------------------------------------
 *
 * Name:        sertnc_listen_thread
 *
 * Purpose:     Listen for anything from TNC and process it.
 *		Reconnect if something goes wrong and we got disconnected.
 *
 * Inputs:	arg			- Channel number.
 *		s_tnc_device[chan]	- Device & baud rate for re-connection.
 *		s_tnc_baud[chan]
 *
 * Outputs:	s_tnc_fd[chan] - File descriptor for communicating with TNC.
 *				  Will be MYFDERROR if not connected.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall sertnc_listen_thread (void *arg)
#else
static void * sertnc_listen_thread (void *arg)	
#endif	
{
	int chan = (int)(ptrdiff_t)arg;
	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);

	kiss_frame_t kstate;	 // State machine to gather a KISS frame.
	memset (&kstate, 0, sizeof(kstate));

	int ch;		// normally 0-255 but -1 for error.

	int reattach_delay = 30;	// Could happen if USB serial device is unplugged.

	while (1) {
/*
 * Re-attach to TNC if not currently attached.
 */
	  if (s_tnc_fd[chan] == MYFDERROR) {
	    text_color_set(DW_COLOR_ERROR);
	    // I'm using the term "attach" here, in an attempt to
	    // avoid confusion with the AX.25 connect.
	    dw_printf ("Attempting to reattach to serial TNC...\n");

	    s_tnc_fd[chan] = serial_port_open (s_tnc_device[chan], s_tnc_baud[chan]);

	    if (s_tnc_fd[chan] != MYFDERROR) {
	      dw_printf ("Successfully reattached to serial TNC.\n");
	    }
	    else {
	      dw_printf ("Failed to reattach.  Try again in %d seconds.\n", reattach_delay);
	      serial_port_close (s_tnc_fd[chan]);
	      s_tnc_fd[chan] = MYFDERROR;
	      SLEEP_SEC (reattach_delay);
	      continue;
	    }
	  }
	  else {
	    ch = serial_port_get1 (s_tnc_fd[chan]);

	    if (ch == -1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Lost communication with serial TNC. Will try to reattach in %d seconds.\n", reattach_delay);
	      serial_port_close (s_tnc_fd[chan]);
	      s_tnc_fd[chan] = MYFDERROR;
	      SLEEP_SEC (reattach_delay);
	      continue;
	    }

#if 0
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("TEMP DEBUG:  byte received from channel %d serial TNC.\n", chan);
#endif
	    // Separate the byte stream into KISS frame(s) and make it
	    // look like this came from a radio channel.
	    my_kiss_rec_byte (&kstate, ch, s_kiss_debug, chan, SUBCHAN_SERTNC);
	  } // s_tnc_fd != MYFDERROR
	} // while (1)

	return (0);	// unreachable but shutup warning.

} // end sertnc_listen_thread



/*-------------------------------------------------------------------
 *
 * Name:	sertnc_send_packet
 *
 * Purpose:	Send packet to a KISS serial TNC.
 *
 * Inputs:	chan	- Channel number from SCHANNEL configuration.
 *		pp	- Packet object.
 *
 * Outputs:	Packet is converted to KISS and send to serial TNC.
 *
 * Returns:	none.
 *
 * Description:	This does not free the packet object; caller is responsible.
 *
 *-----------------------------------------------------------------*/

void sertnc_send_packet (int chan, packet_t pp)
{

// First, get the on-air frame format from packet object.
// Prepend 0 byte for KISS command and channel.

	unsigned char frame_buff[AX25_MAX_PACKET_LEN + 2];	// One byte for channel/command,
							// followed by the AX.25 on-air format frame.
	frame_buff[0] = 0;	// For now, set channel to 0.

	unsigned char *fbuf = ax25_get_frame_data_ptr (pp);
	int flen = ax25_get_frame_len (pp); 

	memcpy (frame_buff+1, fbuf, flen);

// Next, encapsulate into KISS frame with surrounding FENDs and any escapes.

	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN];
	int kiss_len = kiss_encapsulate (frame_buff, flen+1, kiss_buff);

	int err = serial_port_write (s_tnc_fd[chan], (char*) kiss_buff, kiss_len);
	if (err <= 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending packet to KISS Serial TNC for channel %d.  Closing connection.\n\n", err, chan);
	  serial_port_close (s_tnc_fd[chan]);
	  s_tnc_fd[chan] = MYFDERROR;
	}
	
	// Do not free packet object;  caller will take care of it.

} /* end nettnc_send_packet */


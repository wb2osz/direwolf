
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
#include "dwsock.h"		// socket helper functions.
#include "ax25_pad.h"		// for AX25_MAX_PACKET_LEN
#include "dlq.h"		// received packet queue

#include "nettnc.h"



void hex_dump (unsigned char *p, int len);


// TODO: define macros in common locaation to hide platform specifics.

#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

#if __WIN32__
static HANDLE nettnc_listen_th[MAX_TOTAL_CHANS];
static THREAD_F nettnc_listen_thread (void *arg);
#else
static pthread_t nettnc_listen_tid[MAX_TOTAL_CHANS];
static THREAD_F nettnc_listen_thread (void *arg);	
#endif

static void my_kiss_rec_byte (kiss_frame_t *kf, unsigned char b, int debug, int channel_override);

int s_kiss_debug = 0;


/*-------------------------------------------------------------------
 *
 * Name:        nettnc_init
 *
 * Purpose:      Attach to Network KISS TNC(s) for NCHANNEL config file item(s).
 *
 * Inputs:	pa              - Address of structure of type audio_s.
 *
 *		debug ? TBD
 *
 *
 * Returns:	0 for success, -1 for failure.
 *
 * Description:	Called once at direwolf application start up time.
 *		Calls nettnc_attach for each NCHANNEL configuration item.
 *
 *--------------------------------------------------------------------*/

void nettnc_init (struct audio_s *pa)
{
	for (int i = 0; i < MAX_TOTAL_CHANS; i++) {

	  if (pa->chan_medium[i] == MEDIUM_NETTNC) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Channel %d: Network TNC %s %d\n", i, pa->nettnc_addr[i], pa->nettnc_port[i]);
	    int e = nettnc_attach (i, pa->nettnc_addr[i], pa->nettnc_port[i]);
	    if (e < 0) {
	      exit (1);
	    }
	  }
	}

}  // end nettnc_init



/*-------------------------------------------------------------------
 *
 * Name:        nettnc_attach
 *
 * Purpose:      Attach to one Network KISS TNC.
 *
 * Inputs:	chan	- channel number from NCHANNEL configuration.
 *
 *		host	- Host name or IP address.  Often "localhost".
 *
 *		port	- TCP port number.  Typically 8001.
 *
 *		init_func - Call this function after establishing communication //
 *			with the TNC.  We put it here, so that it can be done//
 *			again automatically if the TNC disappears and we//
 *			reattach to it.//
 *			It must return 0 for success.//
 *			Can be NULL if not needed.//
 *
 * Returns:	0 for success, -1 for failure.
 *
 * Description:	This starts up a thread, for each socket, which listens to the socket and
 *		dispatches the messages to the corresponding callback functions.
 *		It will also attempt to re-establish communication with the
 *		TNC if it goes away.
 *
 *--------------------------------------------------------------------*/

static char s_tnc_host[MAX_TOTAL_CHANS][80];
static char s_tnc_port[MAX_TOTAL_CHANS][20];
static volatile int s_tnc_sock[MAX_TOTAL_CHANS];	// Socket handle or file descriptor. -1 for invalid.


int nettnc_attach (int chan, char *host, int port)
{
	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);

	char tncaddr[DWSOCK_IPADDR_LEN];

	char sport[20];		// need port as text string later.
	snprintf (sport, sizeof(sport), "%d", port);

	strlcpy (s_tnc_host[chan], host, sizeof(s_tnc_host[chan]));
	strlcpy (s_tnc_port[chan], sport, sizeof(s_tnc_port[chan]));
	s_tnc_sock[chan] = -1;

	dwsock_init();

	s_tnc_sock[chan] = dwsock_connect (s_tnc_host[chan], s_tnc_port[chan], "Network TNC", 0, 0, tncaddr);

	if (s_tnc_sock[chan] == -1) {
	  return (-1);
	}


/*
 * Read frames from the network TNC.
 * If the TNC disappears, try to reestablish communication.
 */


#if __WIN32__
	nettnc_listen_th[chan] = (HANDLE)_beginthreadex (NULL, 0, nettnc_listen_thread, (void *)(ptrdiff_t)chan, 0, NULL);
	if (nettnc_listen_th[chan] == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: Could not create remore TNC listening thread\n");
	  return (-1);
	}
#else
	int e = pthread_create (&nettnc_listen_tid[chan], NULL, nettnc_listen_thread, (void *)(ptrdiff_t)chan);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Internal error: Could not create network TNC listening thread");
	  return (-1);
	}
#endif

// TNC initialization if specified.

//	if (s_tnc_init_func != NULL) {
//	  e = (*s_tnc_init_func)();
//	  return (e);
//	}

	return (0);

}  // end nettnc_attach



/*-------------------------------------------------------------------
 *
 * Name:        nettnc_listen_thread
 *
 * Purpose:     Listen for anything from TNC and process it.
 *		Reconnect if something goes wrong and we got disconnected.
 *
 * Inputs:	arg			- Channel number.
 *		s_tnc_host[chan]	- Host & port for re-connection.
 *		s_tnc_port[chan]
 *
 * Outputs:	s_tnc_sock[chan] - File descriptor for communicating with TNC.
 *				  Will be -1 if not connected.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall nettnc_listen_thread (void *arg)
#else
static void * nettnc_listen_thread (void *arg)	
#endif	
{
	int chan = (int)(ptrdiff_t)arg;
	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);

	kiss_frame_t kstate;	 // State machine to gather a KISS frame.
	memset (&kstate, 0, sizeof(kstate));

	char tncaddr[DWSOCK_IPADDR_LEN];	// IP address used by dwsock_connect.
						// Useful when rotate addresses used.

// Set up buffer for collecting a KISS frame.$CC exttnc.c

	while (1) {
/*
 * Re-attach to TNC if not currently attached.
 */
	  if (s_tnc_sock[chan] == -1) {

	    text_color_set(DW_COLOR_ERROR);
	    // I'm using the term "attach" here, in an attempt to
	    // avoid confusion with the AX.25 connect.
	    dw_printf ("Attempting to reattach to network TNC...\n");

	    s_tnc_sock[chan] = dwsock_connect (s_tnc_host[chan], s_tnc_port[chan], "Network TNC", 0, 0, tncaddr);

	    if (s_tnc_sock[chan] != -1) {
	      dw_printf ("Successfully reattached to network TNC.\n");
	    }
	  }
	  else {
#define NETTNCBUFSIZ 2048
	    unsigned char buf[NETTNCBUFSIZ];
	    int n = SOCK_RECV (s_tnc_sock[chan], (char *)buf, sizeof(buf));

	    if (n == -1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Lost communication with network TNC. Will try to reattach.\n");
	      dwsock_close (s_tnc_sock[chan]);
	      s_tnc_sock[chan] = -1;
	      SLEEP_SEC(5);
	      continue;
	    }

#if 0
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("TEMP DEBUG:  %d bytes received from channel %d network TNC.\n", n, chan);
#endif
	    for (int j = 0; j < n; j++) {
	      // Separate the byte stream into KISS frame(s) and make it
	      // look like this came from a radio channel.
	      my_kiss_rec_byte (&kstate, buf[j], s_kiss_debug, chan);
	    }
	  } // s_tnc_sock != -1
	} // while (1)

	return (0);	// unreachable but shutup warning.

} // end nettnc_listen_thread



/*-------------------------------------------------------------------
 *
 * Name:        my_kiss_rec_byte 
 *
 * Purpose:     Process one byte from a KISS network TNC.
 *
 * Inputs:	kf	- Current state of building a frame.
 *		b	- A byte from the input stream.
 *		debug	- Activates debug output.
 *		channel_overide - Set incoming channel number to the NCHANNEL
 *				number rather than the channel in the KISS frame.
 *
 * Outputs:	kf	- Current state is updated.
 *
 * Returns:	none.
 *
 * Description:	This is a simplified version of kiss_rec_byte used
 *		for talking to KISS client applications.  It already has
 *		too many special cases and I don't want to make it worse.
 *		This also needs to make the packet look like it came from
 *		a radio channel, not from a client app.
 *
 *-----------------------------------------------------------------*/

static void my_kiss_rec_byte (kiss_frame_t *kf, unsigned char b, int debug, int channel_override)
{

	//dw_printf ("my_kiss_rec_byte ( %c %02x ) \n", b, b);
	
	switch (kf->state) {
	 
  	  case KS_SEARCHING:		/* Searching for starting FEND. */
	  default:

	    if (b == FEND) {
	      
	      /* Start of frame.  */
	      
	      kf->kiss_len = 0;
	      kf->kiss_msg[kf->kiss_len++] = b;
	      kf->state = KS_COLLECTING;
	      return;
	    }
	    return;
	    break;

	  case KS_COLLECTING:		/* Frame collection in progress. */

     
	    if (b == FEND) {
	      
	      unsigned char unwrapped[AX25_MAX_PACKET_LEN];
	      int ulen;

	      /* End of frame. */

	      if (kf->kiss_len == 0) {
		/* Empty frame.  Starting a new one. */
	        kf->kiss_msg[kf->kiss_len++] = b;
	        return;
	      }
	      if (kf->kiss_len == 1 && kf->kiss_msg[0] == FEND) {
		/* Empty frame.  Just go on collecting. */
	        return;
	      }

	      kf->kiss_msg[kf->kiss_len++] = b;
	      if (debug) {
		/* As received over the wire from network TNC. */
		// May include escapted characters.  What about FEND?
// FIXME: make it say Network TNC.
	        kiss_debug_print (FROM_CLIENT, NULL, kf->kiss_msg, kf->kiss_len);
	      }

	      ulen = kiss_unwrap (kf->kiss_msg, kf->kiss_len, unwrapped);

	      if (debug >= 2) {
	        /* Append CRC to this and it goes out over the radio. */
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("\n");
	        dw_printf ("Frame content after removing KISS framing and any escapes:\n");
	        /* Don't include the "type" indicator. */
		/* It contains the radio channel and type should always be 0 here. */
	        hex_dump (unwrapped+1, ulen-1);
	      }

	      // Convert to packet object and send to received packet queue.
	      // Note that we use channel associated with the network TNC, not channel in KISS frame.

	      int subchan = -3;
	      int slice = 0;
	      alevel_t alevel;  
	      memset(&alevel, 0, sizeof(alevel));
	      packet_t pp = ax25_from_frame (unwrapped+1, ulen-1, alevel);
	      if (pp != NULL) {
	        fec_type_t fec_type = fec_type_none;
	        retry_t retries;
	        memset (&retries, 0, sizeof(retries));
	        char spectrum[] = "Network TNC";
	        dlq_rec_frame (channel_override, subchan, slice, pp, alevel, fec_type, retries, spectrum);
	      }
	      else {
	   	text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Failed to create packet object for KISS frame from channel %d network TNC.\n", channel_override);
	      }
     
	      kf->state = KS_SEARCHING;
	      return;
	    }

	    if (kf->kiss_len < MAX_KISS_LEN) {
	      kf->kiss_msg[kf->kiss_len++] = b;
	    }
	    else {	    
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS frame from network TNC exceeded maximum length.\n");
	    }	      
	    return;
	    break;
	}
	
	return;	/* unreachable but suppress compiler warning. */

} /* end my_kiss_rec_byte */   
	      	    








/*-------------------------------------------------------------------
 *
 * Name:	nettnc_send_packet
 *
 * Purpose:	Send packet to a KISS network TNC.
 *
 * Inputs:	chan	- Channel number from NCHANNEL configuration.
 *		pp	- Packet object.
 *		b	- A byte from the input stream.
 *
 * Outputs:	Packet is converted to KISS and send to network TNC.
 *
 * Returns:	none.
 *
 * Description:	This does not free the packet object; caller is responsible.
 *
 *-----------------------------------------------------------------*/

void nettnc_send_packet (int chan, packet_t pp)
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

#if __WIN32__	
	int err = SOCK_SEND(s_tnc_sock[chan], (char*)kiss_buff, kiss_len);
	if (err == SOCKET_ERROR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending packet to KISS Network TNC for channel %d.  Closing connection.\n\n", WSAGetLastError(), chan);
	  closesocket (s_tnc_sock[chan]);
	  s_tnc_sock[chan] = -1;
	}
#else
	int err = SOCK_SEND (s_tnc_sock[chan], kiss_buff, kiss_len);
	if (err <= 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending packet to KISS Network TNC for channel %d.  Closing connection.\n\n", err, chan);
	  close (s_tnc_sock[chan]);
	  s_tnc_sock[chan] = -1;
	}
#endif
	
	// Do not free packet object;  caller will take care of it.

} /* end nettnc_send_packet */


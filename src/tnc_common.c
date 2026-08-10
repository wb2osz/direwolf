
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
 * Module:      tnc_common.c
 *
 * Purpose:   	Functions common to both network and serial TNCs.
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

#include <string.h>

#include "textcolor.h"
#include "kiss.h"
#include "dlq.h"		// received packet queue



void hex_dump (unsigned char *p, int len);


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
 *				or SCHANNEL number rather than the channel in
 *				the KISS frame.
 *		subchan	- Sub-channel type, used here for identifying the frame
 *			as associated with either a network or a serial TNC.
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

void my_kiss_rec_byte (kiss_frame_t *kf, unsigned char b, int debug, int channel_override, int subchan)
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
		/* As received over the wire from network or serial TNC. */
		// May include escapted characters.  What about FEND?
// FIXME: make it say Network TNC or Serial TNC.
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
	      // Note that we use channel associated with the network or serial TNC, not channel in KISS frame.

	      int slice = 0;
	      alevel_t alevel;  
	      memset(&alevel, 0, sizeof(alevel));
	      packet_t pp = ax25_from_frame (unwrapped+1, ulen-1, alevel);
	      if (pp != NULL) {
	        fec_type_t fec_type = fec_type_none;
	        retry_t retries;
	        memset (&retries, 0, sizeof(retries));
	        char *spectrum = (subchan == SUBCHAN_NETTNC ? "Network TNC" : "Serial TNC");
	        dlq_rec_frame (channel_override, subchan, slice, pp, alevel, fec_type, retries, spectrum);
	      }
	      else {
	   	text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Failed to create packet object for KISS frame from channel %d %s TNC.\n",
	          channel_override, subchan == SUBCHAN_NETTNC ? "network" : "serial");
	      }
     
	      kf->state = KS_SEARCHING;
	      return;
	    }

	    if (kf->kiss_len < MAX_KISS_LEN) {
	      kf->kiss_msg[kf->kiss_len++] = b;
	    }
	    else {	    
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS frame from %s TNC exceeded maximum length.\n",
	        subchan == SUBCHAN_NETTNC ? "network" : "serial");
	    }	      
	    return;
	    break;
	}
	
	return;	/* unreachable but suppress compiler warning. */

} /* end my_kiss_rec_byte */   
	      	    

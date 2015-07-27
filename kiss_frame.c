//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013  John Langner, WB2OSZ
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
 * Module:      kiss_frame.c
 *
 * Purpose:   	Common code used by Serial port and network versions of KISS protocol.
 *		
 * Description: The KISS TNS protocol is described in http://www.ka9q.net/papers/kiss.html
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
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>

#include <stdlib.h>
#include <ctype.h>

#include <assert.h>
#include <string.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "kiss_frame.h"
#include "tq.h"
#include "xmit.h"



/*-------------------------------------------------------------------
 *
 * Name:        kiss_frame 
 *
 * Purpose:     Extract a KISS frame from byte stream.
 *
 * Inputs:	kf	- Current state of building a frame.
 *		ch	- A byte from the input stream.
 *		debug	- Activates debug output.
 *		sendfun	- Function to send something to the client application.
 *
 * Outputs:	kf	- Current state is updated.
 *
 * Returns:	TRUE when a complete frame is ready for processing.
 *
 * Bug:		For send, the debug output shows exactly what is
 *		being sent including the surrounding FEND and any
 *		escapes.  For receive, we don't show those.
 *
 *-----------------------------------------------------------------*/

/*
 * Application might send some commands to put TNC into KISS mode.  
 * For example, APRSIS32 sends something like:
 *
 *	<0x0d>
 *	<0x0d>
 *	XFLOW OFF<0x0d>
 *	FULLDUP OFF<0x0d>
 *	KISS ON<0x0d>
 *	RESTART<0x0d>
 *	<0x03><0x03><0x03>
 *	TC 1<0x0d>
 *	TN 2,0<0x0d><0x0d><0x0d>
 *	XFLOW OFF<0x0d>
 *	FULLDUP OFF<0x0d>
 *	KISS ON<0x0d>
 *	RESTART<0x0d>
 *
 * This keeps repeating over and over and over and over again if
 * it doesn't get any sort of response.
 *
 * Let's try to keep it happy by sending back a command prompt.
 */

int kiss_frame (kiss_frame_t *kf, unsigned char ch, int debug, void (*sendfun)(int,unsigned char*,int)) 
{
	
	switch (kf->state) {
	 
  	  case KS_SEARCHING:		/* Searching for starting FEND. */

	    if (ch == FEND) {
	      
	      /* Start of frame.  But first print any collected noise for debugging. */

	      if (kf->noise_len > 0) {
		if (debug) {
		  kiss_debug_print (FROM_CLIENT, "Rejected Noise", kf->noise, kf->noise_len);
	        }
		kf->noise_len = 0;
	      }
	      
	      kf->kiss_len = 0;
	      kf->state = KS_COLLECTING;
	      return 0;
	    }

	    /* Noise to be rejected. */

	    if (kf->noise_len < MAX_NOISE_LEN) {
	      kf->noise[kf->noise_len++] = ch;
	    }
	    if (ch == '\r') {
	      if (debug) {
		kiss_debug_print (FROM_CLIENT, "Rejected Noise", kf->noise, kf->noise_len);
	   	kf->noise[kf->noise_len] = '\0';
	      }

	      /* Try to appease it by sending something back. */
	      if (strcasecmp("restart\r", (char*)(kf->noise)) == 0 ||
		    strcasecmp("reset\r", (char*)(kf->noise)) == 0) {
	   	  (*sendfun) (0, (unsigned char *)"\xc0\xc0", -1);
	      }
	      else {
	   	  (*sendfun) (0, (unsigned char *)"\r\ncmd:", -1);
	      }
	      kf->noise_len = 0;
	    }
	    return 0;

	  case KS_COLLECTING:		/* Frame collection in progress. */

	    if (ch == FEND) {
	      
	      /* End of frame. */

	      if (kf->kiss_len == 0) {
		/* Empty frame.  Just go on collecting. */
	        return 0;
	      }

	      if (debug) {
	        kiss_debug_print (FROM_CLIENT, NULL, kf->kiss_msg, kf->kiss_len);
	      }
	      kf->state = KS_SEARCHING;
	      return 1;
	    }

	    if (kf->kiss_len < MAX_KISS_LEN) {
	      kf->kiss_msg[kf->kiss_len++] = ch;
	    }
	    else {	    
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS message exceeded maximum length.\n");
	    }	      
	    return 0;

	  case KS_ESCAPE:		/* Expecting TFESC or TFEND. */

	    if (kf->kiss_len >= MAX_KISS_LEN) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS message exceeded maximum length.\n");
	      kf->state = KS_COLLECTING;
	      return 0;
	    }	      

	    if (ch == TFESC) {
	      kf->kiss_msg[kf->kiss_len++] = FESC;
	    }
	    else if (ch == TFEND) {
	      kf->kiss_msg[kf->kiss_len++] = FEND;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS protocol error.  TFESC or TFEND expected.\n");
	    }
	    
	    kf->state = KS_COLLECTING;
	    return 0;
	}
	
	return 0;	/* unreachable but suppress compiler warning. */

} /* end kiss_frame */   
	      	    

/*-------------------------------------------------------------------
 *
 * Name:        kiss_process_msg 
 *
 * Purpose:     Process a message from the KISS client.
 *
 * Inputs:	kf	- Current state of building a frame.
 *			  Should be complete.
 *
 *		debug	- Debug option is selected.
 *
 *-----------------------------------------------------------------*/

void kiss_process_msg (kiss_frame_t *kf, int debug)
{
	int port;
	int cmd;
	packet_t pp;

	port = (kf->kiss_msg[0] >> 4) & 0xf;
	cmd = kf->kiss_msg[0] & 0xf;

	switch (cmd) 
	{
	  case 0:				/* Data Frame */

	    /* Special hack - Discard apparently bad data from Linux AX25. */

	    if ((port == 2 || port == 8) && 
		 kf->kiss_msg[1] == 'Q' << 1 &&
		 kf->kiss_msg[2] == 'S' << 1 &&
		 kf->kiss_msg[3] == 'T' << 1 &&
		 kf->kiss_msg[4] == ' ' << 1 &&
		 kf->kiss_msg[15] == 3 &&
		 kf->kiss_msg[16] == 0xcd) {
	        
	      if (debug) {
	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Special case - Drop packets which appear to be in error.\n");
	      }
	      return;
	    }
	
	    pp = ax25_from_frame (kf->kiss_msg+1, kf->kiss_len-1, -1);
	    if (pp == NULL) {
	       text_color_set(DW_COLOR_ERROR);
	       dw_printf ("ERROR - Invalid KISS data frame from client app.\n");
	    }
	    else {

	    /* How can we determine if it is an original or repeated message? */
	    /* If there is at least one digipeater in the frame, AND */
	    /* that digipeater has been used, it should go out quickly thru */
	    /* the high priority queue. */
	    /* Otherwise, it is an original for the low priority queue. */

	    if (ax25_get_num_repeaters(pp) >= 1 &&
	      ax25_get_h(pp,AX25_REPEATER_1)) {
	      tq_append (port, TQ_PRIO_0_HI, pp);
	    }
	    else {
	      tq_append (port, TQ_PRIO_1_LO, pp);
	    }
	  }
	  break;

        case 1:				/* TXDELAY */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set TXDELAY = %d, port %d\n", kf->kiss_msg[1], port);
	  xmit_set_txdelay (port, kf->kiss_msg[1]);
	  break;

        case 2:				/* Persistence */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set Persistence = %d, port %d\n", kf->kiss_msg[1], port);
	  xmit_set_persist (port, kf->kiss_msg[1]);
	  break;

        case 3:				/* SlotTime */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set SlotTime = %d, port %d\n", kf->kiss_msg[1], port);
	  xmit_set_slottime (port, kf->kiss_msg[1]);
	  break;

        case 4:				/* TXtail */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set TXtail = %d, port %d\n", kf->kiss_msg[1], port);
	  xmit_set_txtail (port, kf->kiss_msg[1]);
	  break;

        case 5:				/* FullDuplex */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set FullDuplex = %d, port %d\n", kf->kiss_msg[1], port);
	  break;

        case 6:				/* TNC specific */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set hardware - ignored.\n");
	  break;

        case 15:				/* End KISS mode, port should be 15. */
						/* Ignore it. */
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol end KISS mode\n");
	  break;

        default:			
          text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("KISS Invalid command %d\n", cmd);
          kiss_debug_print (FROM_CLIENT, NULL, kf->kiss_msg, kf->kiss_len);
	  break;
	}

} /* end kiss_process_msg */


/*-------------------------------------------------------------------
 *
 * Name:        kiss_debug_print 
 *
 * Purpose:     Print message to/from client for debugging.
 *
 * Inputs:	fromto		- Direction of message.
 *		special		- Comment if not a KISS frame.
 *		pmsg		- Address of the message block.
 *		msg_len		- Length of the message.
 *
 *--------------------------------------------------------------------*/


/* In server.c.  Should probably move to some misc. function file. */

void hex_dump (unsigned char *p, int len);



void kiss_debug_print (fromto_t fromto, char *special, unsigned char *pmsg, int msg_len)
{
	const char *direction [2] = { "from", "to" };
	const char *prefix [2] = { "<<<", ">>>" };
	const char *function[16] = { 
		"Data frame",	"TXDELAY",	"P",		"SlotTime",
		"TXtail",	"FullDuplex",	"SetHardware",	"Invalid 7",
		"Invalid 8", 	"Invalid 9",	"Invalid 10",	"Invalid 11",
		"Invalid 12", 	"Invalid 13",	"Invalid 14",	"Return" };


	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n");

	if (special == NULL) {
	  dw_printf ("%s %s %s KISS client application, port %d, total length = %d\n",
			prefix[(int)fromto], function[pmsg[0] & 0xf], direction[(int)fromto], 
			(pmsg[0] >> 4) & 0xf, msg_len);
	}
	else {
	  dw_printf ("%s %s %s KISS client application, total length = %d\n",
			prefix[(int)fromto], special, direction[(int)fromto], 
			msg_len);
	}
	hex_dump ((char*)pmsg, msg_len);

} /* end kiss_debug_print */


/* end kiss_frame.c */

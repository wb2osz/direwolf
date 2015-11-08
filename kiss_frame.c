//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014  John Langner, WB2OSZ
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

/* In server.c.  Should probably move to some misc. function file. */
void hex_dump (unsigned char *p, int len);


static void kiss_process_msg (unsigned char *kiss_msg, int kiss_len, int debug);


#if KISSTEST

#define dw_printf printf

void text_color_set (dw_color_t c)
{
	return;
}

#endif



/*-------------------------------------------------------------------
 *
 * Name:        kiss_frame_init 
 *
 * Purpose:     Save information about valid channels for later error checking.
 *
 * Inputs:      pa		- Address of structure of type audio_s.
 *
 *-----------------------------------------------------------------*/

static struct audio_s *save_audio_config_p;

void kiss_frame_init (struct audio_s *pa)
{
	save_audio_config_p = pa;
}


/*-------------------------------------------------------------------
 *
 * Name:        kiss_encapsulate 
 *
 * Purpose:     Ecapsulate a frame into KISS format.
 *
 * Inputs:	in	- Address of input block.
 *			  First byte is the "type indicator" with type and 
 *			  channel but we don't care about that here.
 *
 *			  This seems cumbersome and confusing to have this
 *			  one byte offset when encapsulating an AX.25 frame.
 *			  Maybe the type/channel byte should be passed in 
 *			  as a separate argument.
 *
 *			  Note that this is "binary" data and can contain
 *			  nul (0x00) values.   Don't treat it like a text string!
 *
 *		ilen	- Number of bytes in input block.
 *
 * Outputs:	out	- Address where to place the KISS encoded representation.
 *			  The sequence is:
 *				FEND		- Magic frame separator.
 *				data		- with certain byte values replaced so
 *						  FEND will never occur here.
 *				FEND		- Magic frame separator.
 *
 * Returns:	Number of bytes in the output.
 *		Absolute max length will be twice input plus 2.
 *
 *-----------------------------------------------------------------*/

int kiss_encapsulate (unsigned char *in, int ilen, unsigned char *out)
{
	int olen;
	int j;

	olen = 0;
	out[olen++] = FEND;
	for (j=0; j<ilen; j++) {

	  if (in[j] == FEND) {
	    out[olen++] = FESC;
	    out[olen++] = TFEND;
	  }
	  else if (in[j] == FESC) {
	    out[olen++] = FESC;
	    out[olen++] = TFESC;
	  }
	  else {
	    out[olen++] = in[j];
	  }
	}
	out[olen++] = FEND;
	
	return (olen);

}  /* end kiss_encapsulate */


#ifndef WALK96

/*-------------------------------------------------------------------
 *
 * Name:        kiss_unwrap 
 *
 * Purpose:     Extract original data from a KISS frame.
 *
 * Inputs:	in	- Address of the received the KISS encoded representation.
 *			  The sequence is:
 *				FEND		- Magic frame separator, optional.
 *				data		- with certain byte values replaced so
 *						  FEND will never occur here.
 *				FEND		- Magic frame separator.
 *		ilen	- Number of bytes in input block.
 *
 * Inputs:	out	- Where to put the resulting frame without
 *			  the escapes or FEND.
 *			  First byte is the "type indicator" with type and 
 *			  channel but we don't care about that here.
 *			  Note that this is "binary" data and can contain
 *			  nul (0x00) values.   Don't treat it like a text string!
 *
 * Returns:	Number of bytes in the output.
 *
 *-----------------------------------------------------------------*/

static int kiss_unwrap (unsigned char *in, int ilen, unsigned char *out)
{
	int olen;
	int j;
	int escaped_mode;

	olen = 0;
	escaped_mode = 0;

	if (ilen < 2) {
	  /* Need at least the "type indicator" byte and FEND. */
	  /* Probably more. */
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("KISS message less than minimum length.\n");
	  return (0);
	}

	if (in[ilen-1] == FEND) {
	  ilen--;	/* Don't try to process below. */
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("KISS frame should end with FEND.\n");
	}

	if (in[0] == FEND) {
	  j = 1;	/* skip over optional leading FEND. */
	}
	else {
	  j = 0;
	}

	for ( ; j<ilen; j++) {

	  if (in[j] == FEND) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS frame should not have FEND in the middle.\n");
	  }

	  if (escaped_mode) {

	    if (in[j] == TFESC) {
	      out[olen++] = FESC;
	    }
	    else if (in[j] == TFEND) {
	      out[olen++] = FEND;
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS protocol error.  Found 0x%02x after FESC.\n", in[j]);
	    }
	    escaped_mode = 0;
	  }
	  else if (in[j] == FESC) {
	    escaped_mode = 1;
	  }
	  else {
	    out[olen++] = in[j];
	  }
	}
	
	return (olen);

}  /* end kiss_unwrap */


#ifndef KISSTEST



/*-------------------------------------------------------------------
 *
 * Name:        kiss_rec_byte 
 *
 * Purpose:     Process one byte from a KISS client app.
 *
 * Inputs:	kf	- Current state of building a frame.
 *		ch	- A byte from the input stream.
 *		debug	- Activates debug output.
 *		sendfun	- Function to send something to the client application.
 *
 * Outputs:	kf	- Current state is updated.
 *
 * Returns:	none.
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




void kiss_rec_byte (kiss_frame_t *kf, unsigned char ch, int debug, void (*sendfun)(int,unsigned char*,int)) 
{

	//dw_printf ("kiss_frame ( %c %02x ) \n", ch, ch);
	
	switch (kf->state) {
	 
  	  case KS_SEARCHING:		/* Searching for starting FEND. */
	  default:

	    if (ch == FEND) {
	      
	      /* Start of frame.  But first print any collected noise for debugging. */

	      if (kf->noise_len > 0) {
		if (debug) {
		  kiss_debug_print (FROM_CLIENT, "Rejected Noise", kf->noise, kf->noise_len);
	        }
		kf->noise_len = 0;
	      }
	      
	      kf->kiss_len = 0;
	      kf->kiss_msg[kf->kiss_len++] = ch;
	      kf->state = KS_COLLECTING;
	      return;
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

	      /* Try to appease client app by sending something back. */
	      if (strcasecmp("restart\r", (char*)(kf->noise)) == 0 ||
		    strcasecmp("reset\r", (char*)(kf->noise)) == 0) {
	   	  (*sendfun) (0, (unsigned char *)"\xc0\xc0", -1);
	      }
	      else {
	   	  (*sendfun) (0, (unsigned char *)"\r\ncmd:", -1);
	      }
	      kf->noise_len = 0;
	    }
	    return;
	    break;

	  case KS_COLLECTING:		/* Frame collection in progress. */

     
	    if (ch == FEND) {
	      
	      unsigned char unwrapped[AX25_MAX_PACKET_LEN];
	      int ulen;

	      /* End of frame. */

	      if (kf->kiss_len == 0) {
		/* Empty frame.  Starting a new one. */
	        kf->kiss_msg[kf->kiss_len++] = ch;
	        return;
	      }
	      if (kf->kiss_len == 1 && kf->kiss_msg[0] == FEND) {
		/* Empty frame.  Just go on collecting. */
	        return;
	      }

	      kf->kiss_msg[kf->kiss_len++] = ch;
	      if (debug) {
		/* As received over the wire from client app. */
	        kiss_debug_print (FROM_CLIENT, NULL, kf->kiss_msg, kf->kiss_len);
	      }

	      ulen = kiss_unwrap (kf->kiss_msg, kf->kiss_len, unwrapped);

	      if (debug >= 2) {
	        /* Append CRC to this and it goes out over the radio. */
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("\n");
	        dw_printf ("Packet content after removing KISS framing and any escapes:\n");
	        /* Don't include the "type" indicator. */
		/* It contains the radio channel and type should always be 0 here. */
	        hex_dump (unwrapped+1, ulen-1);
	      }

	      kiss_process_msg (unwrapped, ulen, debug);

	      kf->state = KS_SEARCHING;
	      return;
	    }

	    if (kf->kiss_len < MAX_KISS_LEN) {
	      kf->kiss_msg[kf->kiss_len++] = ch;
	    }
	    else {	    
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS message exceeded maximum length.\n");
	    }	      
	    return;
	    break;
	}
	
	return;	/* unreachable but suppress compiler warning. */

} /* end kiss_rec_byte */   
	      	    



/*-------------------------------------------------------------------
 *
 * Name:        kiss_process_msg 
 *
 * Purpose:     Process a message from the KISS client.
 *
 * Inputs:	kiss_msg	- Kiss frame with FEND and escapes removed.
 *				  The first byte contains channel and command.
 *
 *		kiss_len	- Number of bytes including the command.
 *
 *		debug	- Debug option is selected.
 *
 *-----------------------------------------------------------------*/

static void kiss_process_msg (unsigned char *kiss_msg, int kiss_len, int debug)
{
	int port;
	int cmd;
	packet_t pp;
	alevel_t alevel;

	port = (kiss_msg[0] >> 4) & 0xf;
	cmd = kiss_msg[0] & 0xf;

	switch (cmd) 
	{
	  case 0:				/* Data Frame */

	    /* Special hack - Discard apparently bad data from Linux AX25. */

	    if ((port == 2 || port == 8) && 
		 kiss_msg[1] == 'Q' << 1 &&
		 kiss_msg[2] == 'S' << 1 &&
		 kiss_msg[3] == 'T' << 1 &&
		 kiss_msg[4] == ' ' << 1 &&
		 kiss_msg[15] == 3 &&
		 kiss_msg[16] == 0xcd) {
	        
	      if (debug) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Special case - Drop packets which appear to be in error.\n");
	      }
	      return;
	    }
	
	    /* Verify that the port (channel) number is valid. */

	    if (port < 0 || port >= MAX_CHANS || ! save_audio_config_p->achan[port].valid) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid transmit channel %d from KISS client app.\n", port);
              text_color_set(DW_COLOR_DEBUG);
	      kiss_debug_print (FROM_CLIENT, NULL, kiss_msg, kiss_len);
	      return;
	    }

	    memset (&alevel, 0xff, sizeof(alevel));
	    pp = ax25_from_frame (kiss_msg+1, kiss_len-1, alevel);
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
	  dw_printf ("KISS protocol set TXDELAY = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  xmit_set_txdelay (port, kiss_msg[1]);
	  break;

        case 2:				/* Persistence */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set Persistence = %d, port %d\n", kiss_msg[1], port);
	  xmit_set_persist (port, kiss_msg[1]);
	  break;

        case 3:				/* SlotTime */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set SlotTime = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  xmit_set_slottime (port, kiss_msg[1]);
	  break;

        case 4:				/* TXtail */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set TXtail = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  xmit_set_txtail (port, kiss_msg[1]);
	  break;

        case 5:				/* FullDuplex */

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set FullDuplex = %d, port %d\n", kiss_msg[1], port);
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
          kiss_debug_print (FROM_CLIENT, NULL, kiss_msg, kiss_len);
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
	  unsigned char *p;	/* to skip over FEND if present. */

	  p = pmsg;
	  if (*p == FEND) p++;

	  dw_printf ("%s %s %s KISS client application, port %d, total length = %d\n",
			prefix[(int)fromto], function[p[0] & 0xf], direction[(int)fromto], 
			(p[0] >> 4) & 0xf, msg_len);
	}
	else {
	  dw_printf ("%s %s %s KISS client application, total length = %d\n",
			prefix[(int)fromto], special, direction[(int)fromto], 
			msg_len);
	}
	hex_dump (pmsg, msg_len);

} /* end kiss_debug_print */


#endif


/* Quick unit test for encapsulate & unwrap */

// $ gcc -DKISSTEST kiss_frame.c ; ./a
// Quick KISS test passed OK.


#if KISSTEST


main ()
{
	unsigned char din[512];
	unsigned char kissed[520];
	unsigned char dout[520];
	int klen;
	int dlen;
	int k;

	for (k = 0; k < 512; k++) {
	  if (k < 256) {
	    din[k] = k;
	  }
	  else {
	    din[k] = 511 - k;
	  }
	}

	klen = kiss_encapsulate (din, 512, kissed);
	assert (klen == 512 + 6);

	dlen = kiss_unwrap (kissed, klen, dout);
	assert (dlen == 512);
	assert (memcmp(din, dout, 512) == 0);

	dlen = kiss_unwrap (kissed+1, klen-1, dout);
	assert (dlen == 512);
	assert (memcmp(din, dout, 512) == 0);

	dw_printf ("Quick KISS test passed OK.\n");
	exit (EXIT_SUCCESS);
}

#endif

#endif /* WALK96 */

/* end kiss_frame.c */

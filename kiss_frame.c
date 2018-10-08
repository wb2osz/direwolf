//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2017  John Langner, WB2OSZ
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
 * Description: The KISS TNC protocol is described in http://www.ka9q.net/papers/kiss.html
 *
 *		( An extended form, to handle multiple TNCs on a single serial port.
 *		  Not applicable for our situation.  http://he.fi/pub/oh7lzb/bpq/multi-kiss.pdf )
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
 *			* port number (radio channel) in upper nybble.
 *			* command in lower nybble.
 *
 *	
 *		Commands from application tp TNC:
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
 *			_5	FullDuplex	Full Duplex.  Transmit immediately without
 *						waiting for channel to be clear.
 *		
 *			_6	SetHardware	TNC specific.
 *
 *			_C	XKISS extension - not supported.
 *			_E	XKISS extention - not supported.
 *			
 *			FF	Return		Exit KISS mode.  Ignored.
 *
 *
 *		Messages sent to client application:
 *
 *			_0	Data Frame	Received AX.25 frame in raw format.
 *
 *			_6	SetHardware	TNC specific.
 *						Usually a response to a query.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>

#include "ax25_pad.h"
#include "textcolor.h"
#include "kiss_frame.h"
#include "tq.h"
#include "xmit.h"
#include "version.h"


/* In server.c.  Should probably move to some misc. function file. */
void hex_dump (unsigned char *p, int len);

#ifdef KISSUTIL
void hex_dump (unsigned char *p, int len)
{
	int n, i, offset;

	offset = 0;
	while (len > 0) {
	  n = len < 16 ? len : 16;
	  printf ("  %03x: ", offset);
	  for (i=0; i<n; i++) {
	    printf (" %02x", p[i]);
	  }
	  for (i=n; i<16; i++) {
	    printf ("   ");
	  }
	  printf ("  ");
	  for (i=0; i<n; i++) {
	    printf ("%c", isprint(p[i]) ? p[i] : '.');
	  }
	  printf ("\n");
	  p += 16;
	  offset += 16;
	  len -= 16;
	}
}
#endif

#if KISSTEST

#define dw_printf printf

void text_color_set (dw_color_t c)
{
	return;
}

#else

#ifndef DECAMAIN
#ifndef KISSUTIL
static void kiss_set_hardware (int chan, char *command, int debug, int client, void (*sendfun)(int,int,unsigned char*,int,int));
#endif
#endif

#endif

//#if KISSUTIL
//#define text_color_set(x)   ;
//#define dw_printf printf
//#endif


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
 *			  If it happens to be FEND or FESC, it is escaped, like any other byte.
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
 *		Absolute max length (extremely unlikely) will be twice input plus 2.
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
 *			  We treat it like any other byte with special handling
 *			  if it happens to be FESC.
 *			  Note that this is "binary" data and can contain
 *			  nul (0x00) values.   Don't treat it like a text string!
 *
 * Returns:	Number of bytes in the output.
 *
 *-----------------------------------------------------------------*/

int kiss_unwrap (unsigned char *in, int ilen, unsigned char *out)
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


#ifndef DECAMAIN

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
 *		client	- Client app number for TCP KISS.
 *		          Ignored for pseudo termal and serial port.
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




void kiss_rec_byte (kiss_frame_t *kf, unsigned char ch, int debug, int client, void (*sendfun)(int,int,unsigned char*,int,int))
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

#ifndef KISSUTIL
	      /* Try to appease client app by sending something back. */
	      if (strcasecmp("restart\r", (char*)(kf->noise)) == 0 ||
		    strcasecmp("reset\r", (char*)(kf->noise)) == 0) {
		// first 2 parameters don't matter when length is -1 indicating text.
	        (*sendfun) (0, 0, (unsigned char *)"\xc0\xc0", -1, client);
	      }
	      else {
	        (*sendfun) (0, 0, (unsigned char *)"\r\ncmd:", -1, client);
	      }
#endif
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

	      kiss_process_msg (unwrapped, ulen, debug, client, sendfun);

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
 *		debug		- Debug option is selected.
 *
 *		client		- Client app number for TCP KISS.
 *				  Ignored for pseudo termal and serial port.
 *
 *		sendfun		- Function to send something to the client application.
 *				  "Set Hardware" can send a response.
 *
 *-----------------------------------------------------------------*/

#ifndef KISSUTIL	// All these ifdefs in here are a sign that this should be refactored.
			// Should split this into multiple files.
			// Some functions are only for the TNC end.
			// Other functions are suitble for both TNC and client app.

// This is used only by the TNC sided.

void kiss_process_msg (unsigned char *kiss_msg, int kiss_len, int debug, int client, void (*sendfun)(int,int,unsigned char*,int,int))
{
	int port;
	int cmd;
	packet_t pp;
	alevel_t alevel;

	port = (kiss_msg[0] >> 4) & 0xf;
	cmd = kiss_msg[0] & 0xf;

	switch (cmd) 
	{
	  case KISS_CMD_DATA_FRAME:				/* 0 = Data Frame */

	    /* Special hack - Discard apparently bad data from Linux AX25. */

	    /* Note July 2017: There is a variant of of KISS, called SMACK, that assumes */
	    /* a TNC can never have more than 8 ports.  http://symek.de/g/smack.html */
	    /* It uses the MSB to indicate that a checksum is added.  I wonder if this */
	    /* is why we sometimes hear about a request to transmit on channel 8.  */
	    /* Should we have a message that asks the user if SMACK is being used, */
	    /* and if so, turn it off in the application configuration? */
	    /* Our current default is a maximum of 6 channels but it is easily */
	    /* increased by changing one number and recompiling. */

	    if (kiss_len > 16 &&
	        (port == 2 || port == 8) &&
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

        case KISS_CMD_TXDELAY:				/* 1 = TXDELAY */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for TXDELAY command.\n");
	    return;
	  }
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set TXDELAY = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  if (kiss_msg[1] < 4 || kiss_msg[1] > 100) {
            text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Are you sure you want such an extreme value for TXDELAY?\n");
	    dw_printf ("See \"Radio Channel - Transmit Timing\" section of User Guide for explanation.\n");
	  }
	  xmit_set_txdelay (port, kiss_msg[1]);
	  break;

        case KISS_CMD_PERSISTENCE:			/* 2 = Persistence */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for PERSISTENCE command.\n");
	    return;
	  }
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set Persistence = %d, port %d\n", kiss_msg[1], port);
	  if (kiss_msg[1] < 5 || kiss_msg[1] > 250) {
            text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Are you sure you want such an extreme value for PERSIST?\n");
	    dw_printf ("See \"Radio Channel - Transmit Timing\" section of User Guide for explanation.\n");
	  }
	  xmit_set_persist (port, kiss_msg[1]);
	  break;

        case KISS_CMD_SLOTTIME:				/* 3 = SlotTime */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for SLOTTIME command.\n");
	    return;
	  }
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set SlotTime = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  if (kiss_msg[1] < 2 || kiss_msg[1] > 50) {
            text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Are you sure you want such an extreme value for SLOTTIME?\n");
	    dw_printf ("See \"Radio Channel - Transmit Timing\" section of User Guide for explanation.\n");
	  }
	  xmit_set_slottime (port, kiss_msg[1]);
	  break;

        case KISS_CMD_TXTAIL:				/* 4 = TXtail */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for TXTAIL command.\n");
	    return;
	  }
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set TXtail = %d (*10mS units = %d mS), port %d\n", kiss_msg[1], kiss_msg[1] * 10, port);
	  if (kiss_msg[1] < 2) {
            text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Setting TXTAIL so low is asking for trouble.  You probably don't want to do this.\n");
	    dw_printf ("See \"Radio Channel - Transmit Timing\" section of User Guide for explanation.\n");
	  }
	  xmit_set_txtail (port, kiss_msg[1]);
	  break;

        case KISS_CMD_FULLDUPLEX:			/* 5 = FullDuplex */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for FULLDUPLEX command.\n");
	    return;
	  }
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set FullDuplex = %d, port %d\n", kiss_msg[1], port);
	  xmit_set_fulldup (port, kiss_msg[1]);
	  break;

        case KISS_CMD_SET_HARDWARE:			/* 6 = TNC specific */

	  if (kiss_len < 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS ERROR: Missing value for SET HARDWARE command.\n");
	    return;
	  }
	  kiss_msg[kiss_len] = '\0';
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol set hardware \"%s\", port %d\n", (char*)(kiss_msg+1), port);
	  kiss_set_hardware (port, (char*)(kiss_msg+1), debug, client, sendfun);
	  break;

        case KISS_CMD_END_KISS:			/* 15 = End KISS mode, port should be 15. */
						/* Ignore it. */
          text_color_set(DW_COLOR_INFO);
	  dw_printf ("KISS protocol end KISS mode - Ignored.\n");
	  break;

        default:			
          text_color_set(DW_COLOR_ERROR);
	  dw_printf ("KISS Invalid command %d\n", cmd);
          kiss_debug_print (FROM_CLIENT, NULL, kiss_msg, kiss_len);

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("Troubleshooting tip:\n");
	  dw_printf ("Use \"-d kn\" option on direwolf command line to observe\n");
	  dw_printf ("all communication with the client application.\n");

	  if (cmd == XKISS_CMD_DATA || cmd == XKISS_CMD_POLL) {
	    dw_printf ("\n");
	    dw_printf ("It looks like you are trying to use the \"XKISS\" protocol which is not supported.\n");
	    dw_printf ("Change your application settings to use standard \"KISS\" rather than some other variant.\n");
	    dw_printf ("If you are using Winlink Express, configure like this:\n");
	    dw_printf ("    Packet TNC Type:  KISS\n");
	    dw_printf ("    Packet TNC Model:  NORMAL      -- Using ACKMODE will cause this error.\n");
	    dw_printf ("\n");
	  }
	  break;
	}

} /* end kiss_process_msg */

#endif  // ifndef KISSUTIL


/*-------------------------------------------------------------------
 *
 * Name:        kiss_set_hardware
 *
 * Purpose:     Process the "set hardware" command.
 *
 * Inputs:	chan		- channel, 0 - 15.
 *
 *		command		- All but the first byte.  e.g.  "TXBUF:99"
 *				  Case sensitive.
 *				  Will be modified so be sure caller doesn't care.
 *
 *		debug		- debug level.
 *
 *		client		- Client app number for TCP KISS.
 *				  Needed so we can send any response to the right client app.
 *				  Ignored for pseudo terminal and serial port.
 *
 *		sendfun		- Function to send something to the client application.
 *
 *				  This is the tricky part.  We can have any combination of
 *				  serial port, pseudo terminal, and multiple TCP clients.
 *				  We need to send the response to same place where query came
 *				  from.  The function is different for each class of device
 *				  and we need a client number for the TCP case because we
 *				  can have multiple TCP KISS clients at the same time.
 *
 *
 * Description:	This is new in version 1.5.  "Set hardware" was previously ignored.
 *
 *		There are times when the client app might want to send configuration
 *		commands, such as modem speed, to the KISS TNC or inquire about its
 *		current state.
 *
 *		The immediate motivation for adding this is that one application wants
 *		to know how many frames are currently in the transmit queue.  This can
 *		be used for throttling of large transmissions and performing some action
 *		after the last frame has been sent.
 *
 *		The original KISS protocol spec offers no guidance on what "Set Hardware" might look
 *		like.  I'm aware of only two, drastically different, implementations:
 *
 *		fldigi - http://www.w1hkj.com/FldigiHelp-3.22/kiss_command_page.html
 *
 *			Everything is in human readable in both directions:
 *
 *			COMMAND: [ parameter [ , parameter ... ] ]
 *
 *			Lack of a parameter, in the client to TNC direction, is a query
 *			which should generate a response in the same format.
 *
 *		    Used by applications, http://www.w1hkj.com/FldigiHelp/kiss_host_prgs_page.html
 *			- BPQ32
 *			- UIChar
 *			- YAAC
 *
 *		mobilinkd - https://raw.githubusercontent.com/mobilinkd/tnc1/tnc2/bertos/net/kiss.c
 *
 *			Single byte with the command / response code, followed by
 *			zero or more value bytes.
 *
 *		    Used by applications:
 *			- APRSdroid
 *
 *		It would be beneficial to adopt one of them rather than doing something
 *		completely different.  It might even be possible to recognize both.
 *		This might allow leveraging of other existing applications.
 *
 *		Let's start with the easy to understand human readable format.
 *
 * Commands:	(Client to TNC, with parameter(s) to set something.)
 *
 *			none yet
 *
 * Queries:	(Client to TNC, no parameters, generate a response.)
 *
 *			Query		Response		Comment
 *			-----		--------		-------
 *
 *			TNC:		TNC:DIREWOLF 9.9	9.9 represents current version.
 *
 *			TXBUF:		TXBUF:999		Number of bytes (not frames) in transmit queue.
 *
 *--------------------------------------------------------------------*/

#ifndef KISSUTIL

static void kiss_set_hardware (int chan, char *command, int debug, int client, void (*sendfun)(int,int,unsigned char*,int,int))
{
	char *param;
	char response[100];

	param = strchr (command, ':');
	if (param != NULL) {
	  *param = '\0';
	  param++;

	  if (strcmp(command, "TNC") == 0) {		/* TNC - Identify software version. */

	    if (strlen(param) > 0) {
              text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS Set Hardware TNC: Did not expect a parameter.\n");
	    }

	    snprintf (response, sizeof(response), "DIREWOLF %d.%d", MAJOR_VERSION, MINOR_VERSION);
	    (*sendfun) (chan, KISS_CMD_SET_HARDWARE, (unsigned char *)response, strlen(response), client);
	  }

	  else if (strcmp(command, "TXBUF") == 0) {	/* TXBUF - Number of bytes in transmit queue. */

	    if (strlen(param) > 0) {
              text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS Set Hardware TXBUF: Did not expect a parameter.\n");
	    }

	    int n = tq_count (chan, -1, "", "", 1);
	    snprintf (response, sizeof(response), "TXBUF:%d", n);
	    (*sendfun) (chan, KISS_CMD_SET_HARDWARE, (unsigned char *)response, strlen(response), client);
	  }

	  else {
            text_color_set(DW_COLOR_ERROR);
	    dw_printf ("KISS Set Hardware unrecognized command: %s.\n", command);
	  }
	}
	else {
          text_color_set(DW_COLOR_ERROR);
	  dw_printf ("KISS Set Hardware \"%s\" expected the form COMMAND:[parameter[,parameter...]]\n", command);
	}
	return;

} /* end kiss_set_hardware */

#endif 	// ifndef KISSUTIL


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
#ifndef KISSUTIL
	const char *direction [2] = { "from", "to" };
	const char *prefix [2] = { "<<<", ">>>" };
	const char *function[16] = { 
		"Data frame",	"TXDELAY",	"P",		"SlotTime",
		"TXtail",	"FullDuplex",	"SetHardware",	"Invalid 7",
		"Invalid 8", 	"Invalid 9",	"Invalid 10",	"Invalid 11",
		"Invalid 12", 	"Invalid 13",	"Invalid 14",	"Return" };
#endif

	text_color_set(DW_COLOR_DEBUG);

#ifdef KISSUTIL
	dw_printf ("From KISS TNC:\n");
#else
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
#endif
	hex_dump (pmsg, msg_len);

} /* end kiss_debug_print */


#endif

#endif /* DECAMAIN */

/* Quick unit test for encapsulate & unwrap */

// $ gcc -DKISSTEST kiss_frame.c ; ./a
// Quick KISS test passed OK.


#if KISSTEST


int main ()
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

#endif  /* KISSTEST */

#endif /* WALK96 */

/* end kiss_frame.c */

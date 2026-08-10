//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016, 2017  John Langner, WB2OSZ
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
 * Name:	cdigipeater.c
 *
 * Purpose:	Act as an digital repeater for connected AX.25 mode.
 *		Similar digipeater.c is for APRS.
 *
 *
 * Description:	Decide whether the specified packet should
 *		be digipeated.  Put my callsign in the digipeater field used.
 *
 *		APRS and connected mode were two split into two
 *		separate files.  Yes, there is duplicate code but they
 *		are significantly different and I thought it would be
 *		too confusing to munge them together.
 *
 * References:	The Ax.25 protocol barely mentions digipeaters and
 *		and doesn't describe how they should work.
 *		
 *------------------------------------------------------------------*/

#define CDIGIPEATER_C

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>	/* for isdigit, isupper */
#include "regex.h"
#include <unistd.h>

#include "ax25_pad.h"
#include "cdigipeater.h"
#include "textcolor.h"
#include "tq.h"
#include "pfilter.h"


static packet_t cdigipeat_match (int from_chan, packet_t pp, char *mycall_rec, char *mycall_xmit, 
				int has_alias, regex_t *alias, int to_chan, char *cfilter_str);


/*
 * Keep pointer to configuration options.
 * Set by cdigipeater_init and used later.
 */


static struct audio_s	    *save_audio_config_p;
static struct cdigi_config_s *save_cdigi_config_p;


/*
 * Maintain count of packets digipeated for each combination of from/to channel.
 */

static int cdigi_count[MAX_RADIO_CHANS][MAX_RADIO_CHANS];

int cdigipeater_get_count (int from_chan, int to_chan) {
	return (cdigi_count[from_chan][to_chan]);
}



/*------------------------------------------------------------------------------
 *
 * Name:	cdigipeater_init
 * 
 * Purpose:	Initialize with stuff from configuration file.
 *
 * Inputs:	p_audio_config	- Configuration for audio channels.
 *
 *		p_cdigi_config	- Connected Digipeater configuration details.
 *		
 * Outputs:	Save pointers to configuration for later use.
 *		
 * Description:	Called once at application startup time.
 *
 *------------------------------------------------------------------------------*/

void cdigipeater_init (struct audio_s *p_audio_config, struct cdigi_config_s *p_cdigi_config) 
{
	save_audio_config_p = p_audio_config;
	save_cdigi_config_p = p_cdigi_config;
}




/*------------------------------------------------------------------------------
 *
 * Name:	cdigipeater
 * 
 * Purpose:	Re-transmit packet if it matches the rules.
 *
 * Inputs:	chan	- Radio channel where it was received.
 *		
 * 		pp	- Packet object.
 *		
 * Returns:	None.
 *		
 *------------------------------------------------------------------------------*/



void cdigipeater (int from_chan, packet_t pp)
{
	int to_chan;

	// Connected mode is allowed only for channels with internal modem.
	// It probably wouldn't matter for digipeating but let's keep that rule simple and consistent.

	if ( from_chan < 0 || from_chan >= MAX_RADIO_CHANS ||
		(save_audio_config_p->chan_medium[from_chan] != MEDIUM_RADIO &&
		save_audio_config_p->chan_medium[from_chan] != MEDIUM_NETTNC &&
		save_audio_config_p->chan_medium[from_chan] != MEDIUM_SERTNC)  ) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("cdigipeater: Did not expect to receive on invalid channel %d.\n", from_chan);
	  return;
	}

/*
 * First pass:  Look at packets being digipeated to same channel.
 *
 * There was a reason for two passes for APRS.
 * Might not have a benefit here.
 */

	for (to_chan=0; to_chan<MAX_RADIO_CHANS; to_chan++) {
	  if (save_cdigi_config_p->enabled[from_chan][to_chan]) {
	    if (to_chan == from_chan) {
	      packet_t result;

	      result = cdigipeat_match (from_chan, pp, save_audio_config_p->mycall[from_chan],
					   save_audio_config_p->mycall[to_chan],
			save_cdigi_config_p->has_alias[from_chan][to_chan],
			&(save_cdigi_config_p->alias[from_chan][to_chan]), to_chan,
				save_cdigi_config_p->cfilter_str[from_chan][to_chan]);
	      if (result != NULL) {
	        tq_append (to_chan, TQ_PRIO_0_HI, result);
	        cdigi_count[from_chan][to_chan]++;
	      }
	    }
	  }
	}


/*
 * Second pass:  Look at packets being digipeated to different channel.
 */

	for (to_chan=0; to_chan<MAX_RADIO_CHANS; to_chan++) {
	  if (save_cdigi_config_p->enabled[from_chan][to_chan]) {
	    if (to_chan != from_chan) {
	      packet_t result;

	      result = cdigipeat_match (from_chan, pp, save_audio_config_p->mycall[from_chan],
					   save_audio_config_p->mycall[to_chan],
	                save_cdigi_config_p->has_alias[from_chan][to_chan],
			&(save_cdigi_config_p->alias[from_chan][to_chan]), to_chan,
				save_cdigi_config_p->cfilter_str[from_chan][to_chan]);
	      if (result != NULL) {
	        tq_append (to_chan, TQ_PRIO_0_HI, result);
	        cdigi_count[from_chan][to_chan]++;
	      }
	    }
	  }
	}

} /* end cdigipeater */



/*------------------------------------------------------------------------------
 *
 * Name:	cdigipeat_match
 * 
 * Purpose:	A simple digipeater for connected mode AX.25.
 *
 * Input:	pp		- Pointer to a packet object.
 *	
 *		mycall_rec	- Call of my station, with optional SSID,
 *				  associated with the radio channel where the 
 *				  packet was received.
 *
 *		mycall_xmit	- Call of my station, with optional SSID,
 *				  associated with the radio channel where the 
 *				  packet is to be transmitted.  Could be the same as
 *				  mycall_rec or different.
 *
 *		has_alias	- True if we have an alias.
 *
 *		alias		- Optional compiled pattern for my station aliases.
 *				  Do NOT attempt to use this if 'has_alias' is false.
 *
 *		to_chan		- Channel number that we are transmitting to.
 *
 *		cfilter_str	- Filter expression string for the from/to channel pair or NULL.
 *				  Note that only a subset of the APRS filters are applicable here.
 *		
 * Returns:	Packet object for transmission or NULL.
 *		The original packet is not modified.  The caller is responsible for freeing it.
 *		We make a copy and return that modified copy!
 *		This is very important because we could digipeat from one channel to many.
 *
 * Description:	The packet will be digipeated if the next unused digipeater
 *		field matches one of the following:
 *
 *			- mycall_rec
 *			- alias list
 *
 *		APRS digipeating drops duplicates within 30 seconds but we don't do that here.
 *
 *------------------------------------------------------------------------------*/


static packet_t cdigipeat_match (int from_chan, packet_t pp, char *mycall_rec, char *mycall_xmit, 
				int has_alias, regex_t *alias, int to_chan, char *cfilter_str)
{
	int r;
	char repeater[AX25_MAX_ADDR_LEN];
	int err;
	char err_msg[100];

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("cdigipeat_match (from_chan=%d, pp=%p, mycall_rec=%s, mycall_xmit=%s, has_alias=%d, alias=%p, to_chan=%d, cfilter_str=%s\n",
			from_chan, pp, mycall_rec, mycall_xmit, has_alias, alias, to_chan, cfilter_str);
#endif

/*
 * First check if filtering has been configured.
 * Note that we have three different config file filter commands:
 *
 *	FILTER		- APRS digipeating and IGate client side.
 *				Originally this was the only one.
 *				Should we change it to AFILTER to make it clearer?
 *	CFILTER		- Similar for connected moded digipeater.
 *	IGFILTER	- APRS-IS (IGate) server side - completely different.
 *				Confusing with similar name but much different idea.
 *				Maybe this should be renamed to SUBSCRIBE or something like that.
 *
 * Logically this should come later, after an address/alias match.
 * But here we only have to do it once.
 */

	if (cfilter_str != NULL) {

	  if (pfilter(from_chan, to_chan, cfilter_str, pp, 0) != 1) {
	    return(NULL);
	  }
	}


/* 
 * Find the first repeater station which doesn't have "has been repeated" set.
 *
 * r = index of the address position in the frame.
 */
	r = ax25_get_first_not_repeated(pp);

	if (r < AX25_REPEATER_1) {
	  return (NULL);		// Nothing to do.
	}

	ax25_get_addr_with_ssid(pp, r, repeater);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("First unused digipeater is %s\n", repeater);
#endif


/*
 * First check for explicit use of my call.
 * Note that receive and transmit channels could have different callsigns.
 */
	
	if (strcmp(repeater, mycall_rec) == 0) {
	  packet_t result;

	  result = ax25_dup (pp);
	  assert (result != NULL);

	  /* If using multiple radio channels, they could have different calls. */

	  ax25_set_addr (result, r, mycall_xmit);	
	  ax25_set_h (result, r);
	  return (result);
	}

/*
 * If we have an alias match, substitute MYCALL.
 */
	if (has_alias) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Checking %s for alias match.\n", repeater);
#endif
	  err = regexec(alias,repeater,0,NULL,0);
	  if (err == 0) {
	    packet_t result;

	    result = ax25_dup (pp);
	    assert (result != NULL);

	    ax25_set_addr (result, r, mycall_xmit);	
	    ax25_set_h (result, r);
	    return (result);
	  }
	  else if (err != REG_NOMATCH) {
	    regerror(err, alias, err_msg, sizeof(err_msg));
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("%s\n", err_msg);
	  }
	}
	else {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("No alias was specified.\n");
#endif
	}

/*
 * Don't repeat it if we get here.
 */
	return (NULL);

} /* end cdigipeat_match */



/* end cdigipeater.c */

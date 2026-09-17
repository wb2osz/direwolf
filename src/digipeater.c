//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Name:	digipeater.c
 *
 * Purpose:	Act as an APRS digital repeater.
 *		Similar cdigipeater.c is for connected mode.
 *
 *
 * Description:	Decide whether the specified packet should
 *		be digipeated and make necessary modifications.
 *
 *
 * References:	APRS Protocol Reference, document version 1.0.1
 *
 *			http://www.aprs.org/doc/APRS101.PDF
 *
 *		APRS SPEC Addendum 1.1
 *
 *			http://www.aprs.org/aprs11.html
 *
 *		APRS SPEC Addendum 1.2
 *
 *			http://www.aprs.org/aprs12.html
 *
 *		"The New n-N Paradigm"
 *
 *			http://www.aprs.org/fix14439.html
 *
 *		Preemptive Digipeating  (new in version 0.8)
 *
 *			http://www.aprs.org/aprs12/preemptive-digipeating.txt
 *			I ignored the part about the RR bits.
 *
 *------------------------------------------------------------------*/

#define DIGIPEATER_C

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>	/* for isdigit, isupper */
#include "regex.h"
#include <unistd.h>

#include "ax25_pad.h"
#include "digipeater.h"
#include "textcolor.h"
#include "dedupe.h"
#include "tq.h"
#include "pfilter.h"


static packet_t digipeat_match (int from_chan, packet_t pp, char *mycall_rec, char *mycall_xmit, 
				regex_t *uidigi, regex_t *uitrace, int to_chan, enum preempt_e preempt, char *atgp, char *type_filter);


/*
 * Keep pointer to configuration options.
 * Set by digipeater_init and used later.
 */


static struct audio_s	    *save_audio_config_p;
static struct digi_config_s *save_digi_config_p;


/*
 * Maintain count of packets digipeated for each combination of from/to channel.
 */

static int digi_count[MAX_TOTAL_CHANS][MAX_TOTAL_CHANS];

int digipeater_get_count (int from_chan, int to_chan) {
	return (digi_count[from_chan][to_chan]);
}



/*------------------------------------------------------------------------------
 *
 * Name:	digipeater_init
 * 
 * Purpose:	Initialize with stuff from configuration file.
 *
 * Inputs:	p_audio_config	- Configuration for audio channels.
 *
 *		p_digi_config	- Digipeater configuration details.
 *		
 * Outputs:	Save pointers to configuration for later use.
 *		
 * Description:	Called once at application startup time.
 *
 *------------------------------------------------------------------------------*/

void digipeater_init (struct audio_s *p_audio_config, struct digi_config_s *p_digi_config) 
{
	save_audio_config_p = p_audio_config;
	save_digi_config_p = p_digi_config;
	
	dedupe_init (p_digi_config->dedupe_time);
}




/*------------------------------------------------------------------------------
 *
 * Name:	digipeater
 * 
 * Purpose:	Re-transmit packet if it matches the rules.
 *
 * Inputs:	chan	- Radio channel where it was received.
 *		
 * 		pp	- Packet object.
 *		
 * Returns:	None.
 *		
 *
 *------------------------------------------------------------------------------*/



void digipeater (int from_chan, packet_t pp)
{
	int to_chan;


	// dw_printf ("digipeater()\n");
	


	// Network TNC is OK for UI frames where we don't care about timing.

	if ( from_chan < 0 || from_chan >= MAX_TOTAL_CHANS ||
	     (save_audio_config_p->chan_medium[from_chan] != MEDIUM_RADIO &&
	      save_audio_config_p->chan_medium[from_chan] != MEDIUM_NETTNC &&
	      save_audio_config_p->chan_medium[from_chan] != MEDIUM_SERTNC)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("APRS digipeater: Did not expect to receive on invalid channel %d.\n", from_chan);
	}


/*
 * First pass:  Look at packets being digipeated to same channel.
 *
 * We want these to get out quickly, bypassing the usual random wait time.
 *
 * Some may disagree but I followed what WB4APR had to say about it.   
 *
 *	http://www.aprs.org/balloons.html
 *
 *		APRS NETWORK FRATRICIDE: Generally, all APRS digipeaters are supposed to transmit
 *		immediately and all at the same time. They should NOT wait long enough for each
 *		one to QRM the channel with the same copy of each packet. NO, APRS digipeaters
 *		are all supposed to STEP ON EACH OTHER with every packet. This makes sure that
 *		everyone in range of a digi will hear one and only one copy of each packet.
 *		and that the packet will digipeat OUTWARD and not backward. The goal is that a
 *		digipeated packet is cleared out of the local area in ONE packet time and not
 *		N packet times for every N digipeaters that heard the packet. This means no
 *		PERSIST times, no DWAIT times and no UIDWAIT times. Notice, this is contrary 
 *		to other packet systems that might want to guarantee delivery (but at the
 *		expense of throughput). APRS wants to clear the channel quickly to maximize throughput.
 *
 *	http://www.aprs.org/kpc3/kpc3+WIDEn.txt
 *
 *		THIRD:  Eliminate the settings that are detrimental to the network.
 *
 *		* UIDWAIT should be OFF. (the default).  With it on, your digi is not doing the
 *		fundamental APRS fratricide that is the primary mechanism for minimizing channel
 *		loading.  All digis that hear the same packet are supposed to DIGI it at the SAME
 *		time so that all those copies only take up one additional time slot. (but outward
 *		located digs will hear it without collision (and continue outward propagation)
 *
 */

	for (to_chan=0; to_chan<MAX_TOTAL_CHANS; to_chan++) {
	  if (save_digi_config_p->enabled[from_chan][to_chan]) {
	    if (to_chan == from_chan) {
	      packet_t result;

	      result = digipeat_match (from_chan, pp, save_audio_config_p->mycall[from_chan],
					   save_audio_config_p->mycall[to_chan],
			&save_digi_config_p->alias[from_chan][to_chan], &save_digi_config_p->wide[from_chan][to_chan],
			to_chan, save_digi_config_p->preempt[from_chan][to_chan],
				save_digi_config_p->atgp[from_chan][to_chan],
				save_digi_config_p->filter_str[from_chan][to_chan]);
	      if (result != NULL) {
		dedupe_remember (pp, to_chan);
	        tq_append (to_chan, TQ_PRIO_0_HI, result);		//  High priority queue.
	        digi_count[from_chan][to_chan]++;
	      }
	    }
	  }
	}


/*
 * Second pass:  Look at packets being digipeated to different channel.
 *
 * These are lower priority
 */

	for (to_chan=0; to_chan<MAX_TOTAL_CHANS; to_chan++) {
	  if (save_digi_config_p->enabled[from_chan][to_chan]) {
	    if (to_chan != from_chan) {
	      packet_t result;

	      result = digipeat_match (from_chan, pp, save_audio_config_p->mycall[from_chan],
					   save_audio_config_p->mycall[to_chan],
			&save_digi_config_p->alias[from_chan][to_chan], &save_digi_config_p->wide[from_chan][to_chan],
			to_chan, save_digi_config_p->preempt[from_chan][to_chan],
				save_digi_config_p->atgp[from_chan][to_chan],
				save_digi_config_p->filter_str[from_chan][to_chan]);
	      if (result != NULL) {
		dedupe_remember (pp, to_chan);
	        tq_append (to_chan, TQ_PRIO_1_LO, result);		// Low priority queue.
	        digi_count[from_chan][to_chan]++;
	      }
	    }
	  }
	}

} /* end digipeater */



/*------------------------------------------------------------------------------
 *
 * Name:	digipeat_match
 * 
 * Purpose:	A simple digipeater for APRS.
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
 *		alias		- Compiled pattern for my station aliases or 
 *				  "trapping" (repeating only once).
 *
 *		wide		- Compiled pattern for normal WIDEn-n digipeating.
 *
 *		to_chan		- Channel number that we are transmitting to.
 *				  This is needed to maintain a history for 
 *			 	  removing duplicates during specified time period.
 *
 *		preempt		- Option for "preemptive" digipeating.
 *
 *		atgp		- No tracing if this matches alias prefix.
 *				  Hack added for special needs of ATGP.
 *
 *		filter_str	- Filter expression string or NULL.
 *		
 * Returns:	Packet object for transmission or NULL.
 *		The original packet is not modified.  (with one exception, probably obsolete)
 *		We make a copy and return that modified copy!
 *		This is very important because we could digipeat from one channel to many.
 *
 * Description:	The packet will be digipeated if the next unused digipeater
 *		field matches one of the following:
 *
 *			- mycall_rec
 *			- udigi list (only once)
 *			- wide list (usual wideN-N rules)
 *
 *------------------------------------------------------------------------------*/


static packet_t digipeat_match (int from_chan, packet_t pp, char *mycall_rec, char *mycall_xmit, 
				regex_t *alias, regex_t *wide, int to_chan, enum preempt_e preempt, char *atgp, char *filter_str)
{
	char source[AX25_MAX_ADDR_LEN];
	int ssid;
	int r;
	char repeater[AX25_MAX_ADDR_LEN];
	int err;
	char err_msg[100];

/*
 * First check if filtering has been configured.
 */
	if (filter_str != NULL) {

	  if (pfilter(from_chan, to_chan, filter_str, pp, 1) != 1) {
	    return(NULL);
	  }
	}

/*
 * The spec says:
 *
 * 	The SSID in the Destination Address field of all packets is coded to specify
 * 	the APRS digipeater path.
 * 	If the Destination Address SSID is -0, the packet follows the standard AX.25
 * 	digipeater ("VIA") path contained in the Digipeater Addresses field of the
 * 	AX.25 frame.
 * 	If the Destination Address SSID is non-zero, the packet follows one of 15
 * 	generic APRS digipeater paths.
 * 
 *
 * What if this is non-zero but there is also a digipeater path?
 * I will ignore this if there is an explicit path.
 *
 * Note that this modifies the input.  But only once!
 * Otherwise we don't want to modify the input because this could be called multiple times.
 */


/* 
 * Find the first repeater station which doesn't have "has been repeated" set.
 *
 * r = index of the address position in the frame.
 */
	r = ax25_get_first_not_repeated(pp);

	if (r < AX25_REPEATER_1) {
	  return (NULL);
	}

	ax25_get_addr_with_ssid(pp, r, repeater);
	ssid = ax25_get_ssid(pp, r);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("First unused digipeater is %s, ssid=%d\n", repeater, ssid);
#endif


/*
 * First check for explicit use of my call, including SSID.
 * Someone might explicitly specify a particular path for testing purposes.
 * This will bypass the usual checks for duplicates and my call in the source.
 *
 * In this case, we don't check the history so it would be possible
 * to have a loop (of limited size) if someone constructed the digipeater paths
 * correctly.  I would expect it only for testing purposes.
 */
	
	if (strcmp(repeater, mycall_rec) == 0) {
	  packet_t result;

	  result = ax25_dup (pp);
	  assert (result != NULL);

	  /* If using multiple radio channels, they */
	  /* could have different calls. */
	  ax25_set_addr (result, r, mycall_xmit);	
	  ax25_set_h (result, r);
	  return (result);
	}

/*
 * Don't digipeat my own.  Fixed in 1.4 dev H.
 * Alternatively we might feed everything transmitted into
 * dedupe_remember rather than only frames out of digipeater.
 */
	ax25_get_addr_with_ssid(pp, AX25_SOURCE, source);
	if (strcmp(source, mycall_rec) == 0) {
	  return (NULL);
	}


/*
 * Next try to avoid retransmitting redundant information.
 * Duplicates are detected by comparing only:
 *	- source
 *	- destination
 *	- info part
 *	- but not the via path.  (digipeater addresses)
 * A history is kept for some amount of time, typically 30 seconds.
 * For efficiency, only a checksum, rather than the complete fields
 * might be kept but the result is the same.
 * Packets transmitted recently will not be transmitted again during
 * the specified time period.
 *
 */

	if (dedupe_check(pp, to_chan)) {
//#if DEBUG
	  /* Might be useful if people are wondering why */
	  /* some are not repeated.  Might also cause confusion. */

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Digipeater: Drop redundant packet to channel %d.\n", to_chan);
//#endif
	  return NULL;
	}

/*
 * For the alias pattern, we unconditionally digipeat it once.
 * i.e.  Just replace it with MYCALL.
 *
 * My call should be an implied member of this set.
 * In this implementation, we already caught it further up.
 */
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

/* 
 * If preemptive digipeating is enabled, try matching my call 
 * and aliases against all remaining unused digipeaters.
 *
 * Bob says: "GENERIC XXXXn-N DIGIPEATING should not do preemptive digipeating."
 *
 * But consider this case:  https://github.com/wb2osz/direwolf/issues/488
 */

	if (preempt != PREEMPT_OFF) {
	  int r2;

	  for (r2 = r+1; r2 < ax25_get_num_addr(pp); r2++) {
	    char repeater2[AX25_MAX_ADDR_LEN];

	    ax25_get_addr_with_ssid(pp, r2, repeater2);

	    //text_color_set (DW_COLOR_DEBUG);
	    //dw_printf ("test match %d %s\n", r2, repeater2);

	    if (strcmp(repeater2, mycall_rec) == 0 ||
	        regexec(alias,repeater2,0,NULL,0) == 0) {
	      packet_t result;

	      result = ax25_dup (pp);
	      assert (result != NULL);

	      ax25_set_addr (result, r2, mycall_xmit);	
	      ax25_set_h (result, r2);

	      switch (preempt) {
	        case PREEMPT_DROP:	/* remove all prior */
					// TODO: deprecate this option.  Result is misleading.

		  text_color_set (DW_COLOR_ERROR);
		  dw_printf ("The digipeat DROP option will be removed in a future release.  Use PREEMPT for preemptive digipeating.\n");

	          while (r2 > AX25_REPEATER_1) {
	            ax25_remove_addr (result, r2-1);
 		    r2--;
	          }
	          break;

	        case PREEMPT_MARK:	// TODO: deprecate this option.  Result is misleading.

		  text_color_set (DW_COLOR_ERROR);
		  dw_printf ("The digipeat MARK option will be removed in a future release.  Use PREEMPT for preemptive digipeating.\n");

	          r2--;
	          while (r2 >= AX25_REPEATER_1 && ax25_get_h(result,r2) == 0) {
	            ax25_set_h (result, r2);
 		    r2--;
	          }
	          break;

		case PREEMPT_TRACE:	/* My enhancement - remove prior unused digis. */
					/* this provides an accurate path of where packet traveled. */

					// Uh oh.  It looks like sample config files went out
					// with this option.  Should it be renamed as
					// PREEMPT which is more descriptive?
	        default:
	          while (r2 > AX25_REPEATER_1 && ax25_get_h(result,r2-1) == 0) {
	            ax25_remove_addr (result, r2-1);
 		    r2--;
	          }
	          break;
	      }

// Idea: Here is an interesting idea for a new option.  REORDER?
// The preemptive digipeater could move its call after the (formerly) last used digi field
// and preserve all the unused fields after that.  The list of used addresses would
// accurately record the journey taken by the packet.

// https://groups.yahoo.com/neo/groups/aprsisce/conversations/topics/31935

// >  I was wishing for a non-marking preemptive digipeat so that the original packet would be left intact
// >  or maybe something like WIDE1-1,WIDE2-1,KJ4OVQ-9 becoming KJ4OVQ-9*,WIDE1-1,WIDE2-1.

	      return (result);
	    }
 	  }
	}

/*
 * For the wide pattern, we check the ssid and decrement it.
 */

	err = regexec(wide,repeater,0,NULL,0);
	if (err == 0) {

// Special hack added for ATGP to behave like some combination of options in some old TNC
// so the via path does not continue to grow and exceed the 8 available positions.
// The strange thing about this is that the used up digipeater is left there but
// removed by the next digipeater.

	  if (strlen(atgp) > 0 && strncasecmp(repeater, atgp, strlen(atgp)) == 0) {

	    if (ssid >= 1 && ssid <= 7) {
	      packet_t result;

	      result = ax25_dup (pp);
	      assert (result != NULL);

	      // First, remove any already used digipeaters.

	      while (ax25_get_num_addr(result) >= 3 && ax25_get_h(result,AX25_REPEATER_1) == 1) {
	        ax25_remove_addr (result, AX25_REPEATER_1);
	        r--;
	      }

	      ssid = ssid - 1;
	      ax25_set_ssid(result, r, ssid);	// could be zero.
	      if (ssid == 0) {
	        ax25_set_h (result, r);
	      }

	      // Insert own call at beginning and mark it used.

	      ax25_insert_addr (result, AX25_REPEATER_1, mycall_xmit);
	      ax25_set_h (result, AX25_REPEATER_1);
	      return (result);
	    }
	  }

/*
 * If ssid == 1, we simply replace the repeater with my call and
 *	mark it as being used.
 *
 * Otherwise, if ssid in range of 2 to 7, 
 *	Decrement y and don't mark repeater as being used.
 * 	Insert own call ahead of this one for tracing if we don't already have the 
 *	maximum number of repeaters.
 */

	  if (ssid == 1) {
	    packet_t result;

	    result = ax25_dup (pp);
	    assert (result != NULL);

 	    ax25_set_addr (result, r, mycall_xmit);	
	    ax25_set_h (result, r);
	    return (result);
	  }

	  if (ssid >= 2 && ssid <= 7) {
	    packet_t result;

	    result = ax25_dup (pp);
	    assert (result != NULL);

	    ax25_set_ssid(result, r, ssid-1);	// should be at least 1

	    if (ax25_get_num_repeaters(pp) < AX25_MAX_REPEATERS) {
	      ax25_insert_addr (result, r, mycall_xmit);	
	      ax25_set_h (result, r);
	    }
	    return (result);
	  }
	} 
	else if (err != REG_NOMATCH) {
	  regerror(err, wide, err_msg, sizeof(err_msg));
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("%s\n", err_msg);
	}


/*
 * Don't repeat it if we get here.
 */

	return (NULL);
}



/*------------------------------------------------------------------------------
 *
 * Name:	digi_regen
 * 
 * Purpose:	Send regenerated copy of what we received.
 *
 * Inputs:	chan	- Radio channel where it was received.
 *		
 * 		pp	- Packet object.
 *		
 * Returns:	None.
 *
 * Description:	TODO...
 *
 *		Initial reports were favorable.
 *		Should document what this is all about if there is still interest...
 *		
 *------------------------------------------------------------------------------*/

void digi_regen (int from_chan, packet_t pp)
{
	int to_chan;
	packet_t result;

	// dw_printf ("digi_regen()\n");
	
	assert (from_chan >= 0 && from_chan < MAX_TOTAL_CHANS);

	for (to_chan=0; to_chan<MAX_TOTAL_CHANS; to_chan++) {
	  if (save_digi_config_p->regen[from_chan][to_chan]) {
	    result = ax25_dup (pp); 
	    if (result != NULL) {
	      // TODO:  if AX.25 and has been digipeated, put in HI queue?
	      tq_append (to_chan, TQ_PRIO_1_LO, result);
	    }
	  }
	}

} /* end dig_regen */



/*-------------------------------------------------------------------------
 *
 * Name:	main
 * 
 * Purpose:	Standalone test case for this functionality.
 *
 * Usage:	make -f Makefile.<platform> dtest
 *		./dtest 
 *
 *------------------------------------------------------------------------*/

#if DIGITEST

static char mycall[12];

static regex_t alias_re;     

static regex_t wide_re;   

static int failed;

static enum preempt_e preempt = PREEMPT_OFF;

static 	char config_atgp[AX25_MAX_ADDR_LEN] = "HOP";


static void test (char *in, char *out)
{
	packet_t pp, result;
	char rec[256];
	char xmit[256];
	unsigned char *pinfo;
	int info_len;
	unsigned char frame[AX25_MAX_PACKET_LEN];
	int frame_len;
	alevel_t alevel;


	dw_printf ("\n");

/*
 * As an extra test, change text to internal format back to 
 * text again to make sure it comes out the same.
 */
	pp = ax25_from_text (in, 1);
	assert (pp != NULL);

	ax25_format_addrs (pp, rec);
	info_len = ax25_get_info (pp, &pinfo);
	(void)info_len;
	strlcat (rec, (char*)pinfo, sizeof(rec));

	if (strcmp(in, rec) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Text/internal/text error-1 %s -> %s\n", in, rec);
	}

/*
 * Just for more fun, write as the frame format, read it back
 * again, and make sure it is still the same.
 */

	frame_len = ax25_pack (pp, frame);
	ax25_delete (pp);

	alevel.rec = 50;
	alevel.mark = 50;
	alevel.space = 50;

	pp = ax25_from_frame (frame, frame_len, alevel);
	assert (pp != NULL);
	ax25_format_addrs (pp, rec);
	info_len = ax25_get_info (pp, &pinfo);
	strlcat (rec, (char*)pinfo, sizeof(rec));

	if (strcmp(in, rec) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("internal/frame/internal/text error-2 %s -> %s\n", in, rec);
	}

/*
 * On with the digipeater test.
 */	
	
	text_color_set(DW_COLOR_REC);
	dw_printf ("Rec\t%s\n", rec);

//TODO:										  	             Add filtering to test.
//											             V
	result = digipeat_match (0, pp, mycall, mycall, &alias_re, &wide_re, 0, preempt, config_atgp, NULL);
	
	if (result != NULL) {

	  dedupe_remember (result, 0);
	  ax25_format_addrs (result, xmit);
	  info_len = ax25_get_info (result, &pinfo);
	  strlcat (xmit, (char*)pinfo, sizeof(xmit));
	  ax25_delete (result);
	}
	else {
	  strlcpy (xmit, "", sizeof(xmit));
	}

	text_color_set(DW_COLOR_XMIT);
	dw_printf ("Xmit\t%s\n", xmit);
	
	if (strcmp(xmit, out) == 0) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("OK\n");
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Expect\t%s\n", out);
 	  failed++;
	}

	dw_printf ("\n");
}

int main (int argc, char *argv[])
{
	int e;
	failed = 0;
	char message[256];
	strlcpy(mycall, "WB2OSZ-9", sizeof(mycall));

	dedupe_init (4);

/* 
 * Compile the patterns. 
 */
	e = regcomp (&alias_re, "^WIDE[4-7]-[1-7]|CITYD$", REG_EXTENDED|REG_NOSUB);
	if (e != 0) {
	  regerror (e, &alias_re, message, sizeof(message));
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n%s\n\n", message);
	  exit (1);
	}

	e = regcomp (&wide_re, "^WIDE[1-7]-[1-7]$|^TRACE[1-7]-[1-7]$|^MA[1-7]-[1-7]$|^HOP[1-7]-[1-7]$", REG_EXTENDED|REG_NOSUB);
	if (e != 0) {
	  regerror (e, &wide_re, message, sizeof(message));
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n%s\n\n", message);
	  exit (1);
	}

/*
 * Let's start with the most basic cases.
 */

	test (	"W1ABC>TEST01,TRACE3-3:",
		"W1ABC>TEST01,WB2OSZ-9*,TRACE3-2:");

	test (	"W1ABC>TEST02,WIDE3-3:",
		"W1ABC>TEST02,WB2OSZ-9*,WIDE3-2:");

	test (	"W1ABC>TEST03,WIDE3-2:",
		"W1ABC>TEST03,WB2OSZ-9*,WIDE3-1:");

	test (	"W1ABC>TEST04,WIDE3-1:",
		"W1ABC>TEST04,WB2OSZ-9*:");

/*
 * Look at edge case of maximum number of digipeaters.
 */
	test (	"W1ABC>TEST11,R1,R2,R3,R4,R5,R6*,WIDE3-3:",
		"W1ABC>TEST11,R1,R2,R3,R4,R5,R6,WB2OSZ-9*,WIDE3-2:");

	test (	"W1ABC>TEST12,R1,R2,R3,R4,R5,R6,R7*,WIDE3-3:",
		"W1ABC>TEST12,R1,R2,R3,R4,R5,R6,R7*,WIDE3-2:");

	test (	"W1ABC>TEST13,R1,R2,R3,R4,R5,R6,R7*,WIDE3-1:",
		"W1ABC>TEST13,R1,R2,R3,R4,R5,R6,R7,WB2OSZ-9*:");

/*
 * "Trap" large values of "N" by repeating only once.
 */
	test (	"W1ABC>TEST21,WIDE4-4:",
		"W1ABC>TEST21,WB2OSZ-9*:");

	test (	"W1ABC>TEST22,WIDE7-7:",
		"W1ABC>TEST22,WB2OSZ-9*:");

/*
 * Only values in range of 1 thru 7 are valid.
 */
	test (	"W1ABC>TEST31,WIDE0-4:",
		"");

	test (	"W1ABC>TEST32,WIDE8-4:",
		"");

	test (	"W1ABC>TEST33,WIDE2:",
		"");


/*
 * and a few cases actually heard.
 */

	test (	"WA1ENO>FN42ND,W1MV-1*,WIDE3-2:",
		"WA1ENO>FN42ND,W1MV-1,WB2OSZ-9*,WIDE3-1:");

	test (	"W1ON-3>BEACON:",
		"");

	test (	"W1CMD-9>TQ3Y8P,N1RCW-2,W1CLA-1,N8VIM,WIDE2*:",
		"");

	test (	"W1CLA-1>APX192,W1GLO-1,WIDE2*:",
		"");

	test (	"AC1U-9>T2TX4S,AC1U,WIDE1,N8VIM*,WIDE2-1:",
		"AC1U-9>T2TX4S,AC1U,WIDE1,N8VIM,WB2OSZ-9*:");

/*
 * Someone is still using the old style and will probably be disappointed.
 */

	test (	"K1CPD-1>T2SR5R,RELAY*,WIDE,WIDE,SGATE,WIDE:",
		"");


/* 
 * Change destination SSID to normal digipeater if none specified.  (Obsolete, removed.)
 */

	test (	"W1ABC>TEST-3:",
		"");

	test (	"W1DEF>TEST-3,WIDE2-2:",
		"W1DEF>TEST-3,WB2OSZ-9*,WIDE2-1:");

/*
 * Drop duplicates within specified time interval.
 * Only the first 1 of 3 should be retransmitted.
 * The 4th case might be controversial.
 */

	test (	"W1XYZ>TESTD,R1*,WIDE3-2:info1",
		"W1XYZ>TESTD,R1,WB2OSZ-9*,WIDE3-1:info1");

	test (	"W1XYZ>TESTD,R2*,WIDE3-2:info1",
		"");

	test (	"W1XYZ>TESTD,R3*,WIDE3-2:info1",
		"");

	test (	"W1XYZ>TESTD,R1*,WB2OSZ-9:has explicit routing",
		"W1XYZ>TESTD,R1,WB2OSZ-9*:has explicit routing");


/*
 * Allow same thing after adequate time.
 */
	SLEEP_SEC (5);

	test (	"W1XYZ>TEST,R3*,WIDE3-2:info1",
		"W1XYZ>TEST,R3,WB2OSZ-9*,WIDE3-1:info1");

/*
 * Although source and destination match, the info field is different.
 */

	test (	"W1XYZ>TEST,R1*,WIDE3-2:info4",
		"W1XYZ>TEST,R1,WB2OSZ-9*,WIDE3-1:info4");

	test (	"W1XYZ>TEST,R1*,WIDE3-2:info5",
		"W1XYZ>TEST,R1,WB2OSZ-9*,WIDE3-1:info5");

	test (	"W1XYZ>TEST,R1*,WIDE3-2:info6",
		"W1XYZ>TEST,R1,WB2OSZ-9*,WIDE3-1:info6");

/*
 * New in version 0.8.
 * "Preemptive" digipeating looks ahead beyond the first unused digipeater.
 */

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,CITYD,CITYE:off",
		"");

	preempt = PREEMPT_DROP;

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,CITYD,CITYE:drop",
		"W1ABC>TEST11,WB2OSZ-9*,CITYE:drop");

	preempt = PREEMPT_MARK;

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,CITYD,CITYE:mark1",
		"W1ABC>TEST11,CITYA,CITYB,CITYC,WB2OSZ-9*,CITYE:mark1");

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,WB2OSZ-9,CITYE:mark2",
		"W1ABC>TEST11,CITYA,CITYB,CITYC,WB2OSZ-9*,CITYE:mark2");

	preempt = PREEMPT_TRACE;

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,CITYD,CITYE:trace1",
		"W1ABC>TEST11,CITYA,WB2OSZ-9*,CITYE:trace1");

	test (	"W1ABC>TEST11,CITYA*,CITYB,CITYC,CITYD:trace2",
		"W1ABC>TEST11,CITYA,WB2OSZ-9*:trace2");

	test (	"W1ABC>TEST11,CITYB,CITYC,CITYD:trace3",
		"W1ABC>TEST11,WB2OSZ-9*:trace3");

	test (	"W1ABC>TEST11,CITYA*,CITYW,CITYX,CITYY,CITYZ:nomatch",
		"");


/* 
 * Did I miss any cases?
 * Yes.  Don't retransmit my own.  1.4H
 */

	test (	"WB2OSZ-7>TEST14,WIDE1-1,WIDE1-1:stuff",
		"WB2OSZ-7>TEST14,WB2OSZ-9*,WIDE1-1:stuff");

	test (	"WB2OSZ-9>TEST14,WIDE1-1,WIDE1-1:from myself",
		"");

	test (	"WB2OSZ-9>TEST14,WIDE1-1*,WB2OSZ-9:from myself but explicit routing",
		"WB2OSZ-9>TEST14,WIDE1-1,WB2OSZ-9*:from myself but explicit routing");

	test (	"WB2OSZ-15>TEST14,WIDE1-1,WIDE1-1:stuff",
		"WB2OSZ-15>TEST14,WB2OSZ-9*,WIDE1-1:stuff");

// New in 1.7 - ATGP Hack

	preempt = PREEMPT_OFF;	// Shouldn't make a difference here.

	test (	"W1ABC>TEST51,HOP7-7,HOP7-7:stuff1",
		"W1ABC>TEST51,WB2OSZ-9*,HOP7-6,HOP7-7:stuff1");

	test (	"W1ABC>TEST52,ABCD*,HOP7-1,HOP7-7:stuff2",
		"W1ABC>TEST52,WB2OSZ-9,HOP7*,HOP7-7:stuff2");  // Used up address remains.

	test (	"W1ABC>TEST53,HOP7*,HOP7-7:stuff3",
		"W1ABC>TEST53,WB2OSZ-9*,HOP7-6:stuff3");	// But it gets removed here.

	test (	"W1ABC>TEST54,HOP7*,HOP7-1:stuff4",
		"W1ABC>TEST54,WB2OSZ-9,HOP7*:stuff4");		// Remains again here.

	test (	"W1ABC>TEST55,HOP7,HOP7*:stuff5",
		"");

// Examples given for desired result.

	strlcpy (mycall, "CLNGMN-1", sizeof(mycall));
	test (	"W1ABC>TEST60,HOP7-7,HOP7-7:",
		"W1ABC>TEST60,CLNGMN-1*,HOP7-6,HOP7-7:");
	test (	"W1ABC>TEST61,ROAN-3*,HOP7-6,HOP7-7:",
		"W1ABC>TEST61,CLNGMN-1*,HOP7-5,HOP7-7:");

	strlcpy (mycall, "GDHILL-8", sizeof(mycall));
	test (	"W1ABC>TEST62,MDMTNS-7*,HOP7-1,HOP7-7:",
		"W1ABC>TEST62,GDHILL-8,HOP7*,HOP7-7:");
	test (	"W1ABC>TEST63,CAMLBK-9*,HOP7-1,HOP7-7:",
		"W1ABC>TEST63,GDHILL-8,HOP7*,HOP7-7:");

	strlcpy (mycall, "MDMTNS-7", sizeof(mycall));
	test (	"W1ABC>TEST64,GDHILL-8*,HOP7*,HOP7-7:",
		"W1ABC>TEST64,MDMTNS-7*,HOP7-6:");

	strlcpy (mycall, "CAMLBK-9", sizeof(mycall));
	test (	"W1ABC>TEST65,GDHILL-8,HOP7*,HOP7-7:",
		"W1ABC>TEST65,CAMLBK-9*,HOP7-6:");

	strlcpy (mycall, "KATHDN-15", sizeof(mycall));
	test (	"W1ABC>TEST66,MTWASH-14*,HOP7-1:",
		"W1ABC>TEST66,KATHDN-15,HOP7*:");

	strlcpy (mycall, "SPRNGR-1", sizeof(mycall));
	test (	"W1ABC>TEST67,CLNGMN-1*,HOP7-1:",
		"W1ABC>TEST67,SPRNGR-1,HOP7*:");


	if (failed == 0) {
	  dw_printf ("SUCCESS -- All digipeater tests passed.\n");
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - %d digipeater tests failed.\n", failed);
	}

	return ( failed != 0 ); 

} /* end main */

#endif  /* if DIGITEST */

/* end digipeater.c */

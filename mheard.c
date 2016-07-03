//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016  John Langner, WB2OSZ
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
 * File:	mheard.c
 *
 * Purpose:	Maintain a list of all stations heard.
 *
 * Description: This was added for IGate statistics but would also be
 *		useful for the AGW network protocol 'H' request.
 *
 *		This application has no GUI and is not interactive so
 *		I'm not sure what else we might do with the information.
 *
 * Future Ideas: Someone suggested using SQLite to store the information
 *		so other applications could access it.
 *
 *------------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <time.h>
#include <assert.h>
#include <stdlib.h>	
#include <string.h>	
#include <ctype.h>	

#include "textcolor.h"
#include "decode_aprs.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"		// for retry_t
#include "mheard.h"


// I think we can get away without a critical region if we follow certain rules.
//
// (1) All updates are from a single thread.  Although there are multiple receive
//	threads, all received packets go into a single queue for serial processing.
// (2) When adding a new node, make sure it is complete, including next ptr,
//	before adding it to the list.
// (3) Nothing gets deleted.
//
// It shouldn't be a problem if the data readers are from other threads.


/*
 * Information for each station heard over the radio.
 */

typedef struct mheard_s {

	struct mheard_s *pnext;			// Pointer to next in list.

	char callsign[AX25_MAX_ADDR_LEN];	// Callsign from the AX.25 source field.

	int num_digi_hops;			// Number of digipeater hops before we heard it.
						// Zero when heard directly.

	time_t last_heard;			// Timestamp when last heard.

						// What else would be useful?
						// The AGW protocol is by channel and returns
						// first heard in addition to last heard.
} mheard_t;

/*
 * The list could be quite long and we hit this a lot so use a hash table.
 */

#define MHEARD_HASH_SIZE 73	// Best if prime number.

static mheard_t *mheard_hash[MHEARD_HASH_SIZE];

static inline int hash_index(char *callsign) {
	int n = 0;
	char *p = callsign;

	while (*p != '\0') {
	  n += *p++;
	}
	return (n % MHEARD_HASH_SIZE);
}

static mheard_t *mheard_ptr(char *callsign) {
	int n = hash_index(callsign);
	mheard_t *p = mheard_hash[n];

	while (p != NULL) {
	  if (strcmp(callsign,p->callsign) == 0) return (p);
	  p = p->pnext;
	}
	return (NULL);
}
	  
	
static int mheard_debug;


/*------------------------------------------------------------------
 *
 * Function:	mheard_init
 *
 * Purpose:	Initialization at start of application.
 *
 * Inputs:	debug		- Debug level.
 *
 * Description:	Clear pointer table.
 *		Save debug level for later use.
 *
 *------------------------------------------------------------------*/


void mheard_init (int debug) 
{
	int i;

	mheard_debug = debug;

	for (i = 0; i < MHEARD_HASH_SIZE; i++) {
	  mheard_hash[i] = NULL;
	}

} /* end mheard_init */



/*------------------------------------------------------------------
 *
 * Function:	mheard_save
 *
 * Purpose:	Save information about station heard.
 *
 * Inputs:	chan	- Radio channel where heard.
 *
 *		A	- Exploded information from APRS packet.
 *
 *		pp	- Received packet object.
 *
 * 		alevel	- audio level.
 *
 *		retries	- Amount of effort to get a good CRC.
 *
 * Description:	Calling sequence was copied from "log_write."
 *		It has a lot more than what we currently keep but the
 *		hooks are there so it will be easy to capture additional
 *		information when the need arises.
 *
 *------------------------------------------------------------------*/

void mheard_save (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries)
{
	time_t now = time(NULL);
	char source[AX25_MAX_ADDR_LEN];
	int hops;
	mheard_t *mptr;

	ax25_get_addr_with_ssid (pp, AX25_SOURCE, source);

/*
 * How many digipeaters has it gone thru before we hear it?
 * We can count the number of digi addresses that are marked as "has been used."
 * This is not always accurate because there is inconsistency in digipeater behavior.
 * The base AX.25 spec seems clear in this regard.  The used digipeaters should
 * should accurately reflict the path taken by the packet.  Sometimes we see excess
 * stuff in there.  Even when you understand what is going on, it is still an ambiguous
 * situation.  Look for my rant in the User Guide.
 */

	hops = ax25_get_heard(pp) - AX25_SOURCE;
	
	mptr = mheard_ptr(source);
	if (mptr == NULL) {
	  int i;
/*
 * Not heard before.  Add it.
 */

	  if (mheard_debug) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("mheard_save: %s %d - added new\n", source, hops);
	  }

	  mptr = calloc(sizeof(mheard_t),1);
	  strlcpy (mptr->callsign, source, sizeof(mptr->callsign));
	  mptr->num_digi_hops = hops;
	  mptr->last_heard = now;
	  
	  i = hash_index(source);

	  mptr->pnext = mheard_hash[i];	// before inserting into list.
	  mheard_hash[i] = mptr;
	}
	else {

/*
 * Update existing entry.
 * The only tricky part here is that we might hear the same transmission
 * several times.  First direct, then thru various digipeater paths.
 * We are interested in the shortest path if heard very recently.
 */

	  if (hops > mptr->num_digi_hops && (int)(now - mptr->last_heard) < 15) {

	    if (mheard_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("mheard_save: %s %d - skip because hops was %d %d seconds ago.\n", source, hops, mptr->num_digi_hops, (int)(now - mptr->last_heard) );
	    }
	  }
	  else {

	    if (mheard_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("mheard_save: %s %d - update time, was %d hops %d seconds ago.\n", source, hops, mptr->num_digi_hops, (int)(now - mptr->last_heard));
	    }

	    mptr->num_digi_hops = hops;
	    mptr->last_heard = now;
	  }
	}

	if (mheard_debug >= 2) {
	  int limit = 10;		// normally 30 or 60
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("mheard debug, %d min, DIR_CNT=%d,LOC_CNT=%d,RF_CNT=%d\n", limit, mheard_count(0,limit), mheard_count(2,limit), mheard_count(8,limit));
	}

} /* end mheard_save */


/*------------------------------------------------------------------
 *
 * Function:	mheard_count
 *
 * Purpose:	Count local stations for IGate statistics report like this:
 *
 *			<IGATE,MSG_CNT=1,LOC_CNT=25
 *
 * Inputs:	max_hops	- Include only stations heard with this number of 
 *				  digipeater hops or less.  For reporting, we might use:
 *
 *					0 for DIR_CNT (heard directly)
 *					IGate transmit path for LOC_CNT.
 *						e.g. 3 for WIDE1-1,WIDE2-2
 *					8 for RF_CNT.
 *
 *		time_limit	- Include only stations heard within this many minutes.
 *				  Typically 30 or 60.
 *
 * Returns:	Number to be used in the statistics report.
 *
 * Description:	Look for discussion here:  http://www.tapr.org/pipermail/aprssig/2016-June/045837.html
 *
 *		Lynn KJ4ERJ:
 *	
 *			For APRSISCE/32, "Local" is defined as those stations to which messages 
 *			would be gated if any are received from the APRS-IS.  This currently 
 *			means unique stations heard within the past 30 minutes with at most two 
 *			used path hops.
 *
 *			I added DIR_CNT and RF_CNT with comma delimiters to APRSISCE/32's IGate 
 *			status.  DIR_CNT is the count of unique stations received on RF in the 
 *			past 30 minutes with no used hops.  RF_CNT is the total count of unique 
 *			stations received on RF in the past 30 minutes.
 *
 *		Steve K4HG:
 *
 *			The number of hops defining local should match the number of hops of the
 *			outgoing packets from the IGate. So if the path is only WIDE, then local
 *			should only be stations heard direct or through one hop. From the beginning
 *			I was very much against on a standardization of the outgoing IGate path,
 *			hams should be free to manage their local RF network in a way that works
 *			for them. Busy areas one hop may be best, I lived in an area where three was
 *			a much better choice. I avoided as much as possible prescribing anything
 *			that might change between locations.
 *
 *			The intent was how many stations are there for which messages could be IGated.
 *			IGate software keeps an internal list of the 'local' stations so it knows
 *			when to IGate a message, and this number should be the length of that list.
 *			Some IGates have a parameter for local timeout, 1 hour was the original default,
 *			so if in an hour the IGate has not heard another local packet the station is
 *			dropped from the local list. Messages will no longer be IGated to that station
 *			and the station count would drop by one. The number should not just continue to rise.
 *
 *
 *------------------------------------------------------------------*/

int mheard_count (int max_hops, int time_limit)
{
	time_t since = time(NULL) - time_limit * 60;
	int count = 0;
	int i;
	mheard_t *p;

	for (i = 0; i < MHEARD_HASH_SIZE; i++) {
	  for (p = mheard_hash[i]; p != NULL; p = p->pnext) {
	    if (p->last_heard >= since && p->num_digi_hops <= max_hops) {
	      count++;
	    }
	  }
	}

	if (mheard_debug == 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("mheard_count(<= %d digi hops, last %d minutes) returns %d\n", max_hops, time_limit, count);
	}

	return (count);

} /* end mheard_count */


/* end mheard.c */

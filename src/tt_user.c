//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Module:      tt-user.c
 *
 * Purpose:   	Keep track of the APRStt users.
 *		
 * Description: This maintains a list of recently heard APRStt users
 *		and prepares "object" format packets for transmission.
 *
 * References:	This is based upon APRStt (TM) documents but not 100%
 *		compliant due to ambiguities and inconsistencies in
 *		the specifications.
 *
 *		http://www.aprs.org/aprstt.html
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>

#include "version.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "aprs_tt.h"
#include "tt_text.h"
#include "dedupe.h"
#include "tq.h"
#include "igate.h"
#include "tt_user.h"
#include "encode_aprs.h"
#include "latlong.h"

#include "server.h"
#include "kiss.h"
#include "kissserial.h"
#include "kissnet.h"
#include "kiss_frame.h"

/* 
 * Information kept about local APRStt users.
 *
 * For now, just use a fixed size array for simplicity.
 */

#if TT_MAIN
#define MAX_TT_USERS 3	
#else
#define MAX_TT_USERS 100
#endif

#define MAX_CALLSIGN_LEN 9	/* "Object Report" names can be up to 9 characters. */
				
#define MAX_COMMENT_LEN 43	/* Max length of comment in "Object Report." */

//#define G_UNKNOWN -999999	/* Should be in one place. */

#define NUM_XMITS 3
#define XMIT_DELAY_1 5
#define XMIT_DELAY_2 8
#define XMIT_DELAY_3 13


static struct tt_user_s {

	char callsign[MAX_CALLSIGN_LEN+1];	/* Callsign of station heard. */
						/* Does not include the "-12" SSID added later. */
						/* Possibly other tactical call / object label. */
						/* Null string indicates table position is not used. */

	int count;				/* Number of times we received information for this object. */
						/* Value 1 means first time and could be used to send */
						/* a welcome greeting. */

	int ssid;				/* SSID to add. */	
						/* Default of 12 but not always. */			
		
	char overlay;				/* Overlay character. Should be 0-9, A-Z. */
						/* Could be / or \ for general object. */

	char symbol;				/* 'A' for traditional.  */
						/* Can be any symbol for extended objects. */

	char digit_suffix[3+1];			/* Suffix abbreviation as 3 digits. */

	time_t last_heard;			/* Timestamp when last heard.  */
						/* User information will be deleted at some */
						/* point after last time being heard. */

	int xmits;				/* Number of remaining times to transmit info */
						/* about the user.   This is set to 3 when */
						/* a station is heard and decremented each time */
						/* an object packet is sent.  The idea is to send */
						/* 3 within 30 seconds to improve chances of */
						/* being heard while using digipeater duplicate */
						/* removal. */
						// TODO:  I think implementation is different.

	time_t next_xmit;			/* Time for next transmit.  Meaningful only */
						/* if xmits > 0. */

	int corral_slot;			/* If location is known, set this to 0. */
						/* Otherwise, this is a display offset position */
						/* from the gateway. */

	char loc_text[24];			/* Text representation of location when a single */
						/* lat/lon point would be deceptive.  e.g.  */
						/* 32TPP8049 */
						/* 32TPP8179549363 */
						/* 32T 681795 4849363 */
						/* EM29QE78 */

	double latitude, longitude;		/* Location either from user or generated */		
						/* position in the corral. */

	int ambiguity;				/* Number of digits to omit from location. */
						/* Default 0, max. 4. */

	char freq[12];				/* Frequency in format 999.999MHz */

	char ctcss[5];				/* CTCSS tone.  Exactly 3 digits for integer part. */
						/* For example 74.4 Hz becomes "074". */

	char comment[MAX_COMMENT_LEN+1];	/* Free form comment from user. */
						/* Comment sent in final object report includes */
						/* other information besides this. */

	char mic_e;				/* Position status. */
						/* Should be a character in range of '1' to '9' for */
						/* the predefined status strings or '0' for none. */

	char dao[8];				/* Enhanced position information. */
						

} tt_user[MAX_TT_USERS];


static void clear_user(int i);

static void xmit_object_report (int i, int first_time);

static void tt_setenv (int i);


#if __WIN32__

// setenv is missing on Windows!

int setenv(const char *name, const char *value, int overwrite)
{
	char etemp[1000];

	snprintf (etemp, sizeof(etemp), "%s=%s", name, value);
	putenv (etemp);
	return (0);
}

#endif


/*------------------------------------------------------------------
 *
 * Name:        tt_user_init
 *
 * Purpose:     Initialize the APRStt gateway at system startup time.
 *
 * Inputs:      Configuration options gathered by config.c.
 *
 * Global out:	Make our own local copy of the structure here.
 *
 * Returns:     None
 *
 * Description:	The main program needs to call this at application
 *		start up time after reading the configuration file.
 *
 *		TT_MAIN is defined for unit testing.
 *
 *----------------------------------------------------------------*/

static struct audio_s *save_audio_config_p;

static struct tt_config_s *save_tt_config_p;


void tt_user_init (struct audio_s *p_audio_config, struct tt_config_s *p_tt_config)
{
	int i;

	save_audio_config_p = p_audio_config;

	save_tt_config_p = p_tt_config;

	for (i=0; i<MAX_TT_USERS; i++) {
	  clear_user (i);
	}
}


/*------------------------------------------------------------------
 *
 * Name:        tt_user_search
 *
 * Purpose:     Search for user in recent history.
 *
 * Inputs:      callsign	- full or a old style 3 DIGIT suffix abbreviation
 *		overlay
 *
 * Returns:     Handle for referring to table position or -1 if not found.
 *		This happens to be an index into an array but
 *		the implementation could change so the caller should
 *		not make any assumptions.
 *
 *----------------------------------------------------------------*/

int tt_user_search (char *callsign, char overlay)
{
	int i;
/*
 * First, look for exact match to full call and overlay.
 */
	for (i=0; i<MAX_TT_USERS; i++) {
	  if (strcmp(callsign, tt_user[i].callsign) == 0 && 
		overlay == tt_user[i].overlay) {
	    return (i);
	  }
	}

/*
 * Look for digits only suffix plus overlay.
 */
	for (i=0; i<MAX_TT_USERS; i++) {
	  if (strcmp(callsign, tt_user[i].digit_suffix) == 0 && 
		overlay != ' ' &&
		overlay == tt_user[i].overlay) {
	    return (i);
	  }
	}

/*
 * Look for digits only suffix if no overlay was specified.
 */
	for (i=0; i<MAX_TT_USERS; i++) {
	  if (strcmp(callsign, tt_user[i].digit_suffix) == 0 && 
		overlay == ' ') {
	    return (i);
	  }
	}

/*
 * Not sure about the new spelled suffix yet...
 */
	return (-1);

}  /* end tt_user_search */



/*------------------------------------------------------------------
 *
 * Name:        tt_3char_suffix_search
 *
 * Purpose:     Search for new style 3 CHARACTER (vs. 3 digit) suffix in recent history.
 *
 * Inputs:      suffix	- full or a old style 3 DIGIT suffix abbreviation
 *
 * Outputs:	callsign - corresponding full callsign or empty string.
 *
 * Returns:     Handle for referring to table position (>= 0) or -1 if not found.
 *		This happens to be an index into an array but
 *		the implementation could change so the caller should
 *		not make any assumptions.
 *
 *----------------------------------------------------------------*/

int tt_3char_suffix_search (char *suffix, char *callsign)
{
	int i;


/*
 * Look for suffix in list of known calls.
 */
	for (i=0; i<MAX_TT_USERS; i++) {
	  int len = strlen(tt_user[i].callsign);

	  if (len >= 3 && len <= 6 && strcmp(tt_user[i].callsign + len - 3, suffix) == 0) {
	    strlcpy (callsign, tt_user[i].callsign, MAX_CALLSIGN_LEN+1);
	    return (i);
	  }
	}

/*
 * Not found.
 */
	strlcpy (callsign, "", MAX_CALLSIGN_LEN+1);
	return (-1);

}  /* end tt_3char_suffix_search */



/*------------------------------------------------------------------
 *
 * Name:        clear_user
 *
 * Purpose:     Clear specified user table entry.
 *
 * Inputs:      handle for user table entry.
 *
 *----------------------------------------------------------------*/

static void clear_user(int i)
{
	assert (i >= 0 && i < MAX_TT_USERS);

	memset (&(tt_user[i]), 0, sizeof (struct tt_user_s));

} /* end clear_user */


/*------------------------------------------------------------------
 *
 * Name:        find_avail
 *
 * Purpose:     Find an available user table location.
 *
 * Inputs:      none
 *
 * Returns:     Handle for referring to table position.
 *
 * Description:	If table is already full, this should delete the 
 *		least recently heard user to make room.		
 *
 *----------------------------------------------------------------*/

static int find_avail (void)
{
	int i;
	int i_oldest;

	for (i=0; i<MAX_TT_USERS; i++) {
	  if (tt_user[i].callsign[0] == '\0') {
	    clear_user (i);
	    return (i);
	  }
	}

/* Remove least recently heard. */

	i_oldest = 0;

	for (i=1; i<MAX_TT_USERS; i++) {
	  if (tt_user[i].last_heard < tt_user[i_oldest].last_heard) {
	    i_oldest = i;
	  }
	}

	clear_user (i_oldest);
	return (i_oldest);

} /* end find_avail */


/*------------------------------------------------------------------
 *
 * Name:        corral_slot
 *
 * Purpose:     Find an available position in the corral.
 *
 * Inputs:      none
 *
 * Returns:     Small integer >= 1 not already in use.
 *
 *----------------------------------------------------------------*/

static int corral_slot (void)
{
	int slot, i, used;

	for (slot=1; ; slot++) {
	  used = 0;;
	  for (i=0; i<MAX_TT_USERS && ! used; i++) {
	    if (tt_user[i].callsign[0] != '\0' && tt_user[i].corral_slot == slot) {
	      used = 1;
	    }
	  }
	  if (!used) {
	    return (slot);
	  }
	}

} /* end corral_slot */


/*------------------------------------------------------------------
 *
 * Name:        digit_suffix
 *
 * Purpose:     Find 3 digit only suffix code for given call.
 *
 * Inputs:      callsign
 *
 * Outputs:	3 digit suffix
 *
 *----------------------------------------------------------------*/

static void digit_suffix (char *callsign, char *suffix)
{
	char two_key[50];
	char *t;


	strlcpy (suffix, "000", 5);			// TODO: should have proper size
	tt_text_to_two_key (callsign, 0, two_key);
	for (t = two_key; *t != '\0'; t++) {
	  if (isdigit(*t)) {
	    suffix[0] = suffix[1];
	    suffix[1] = suffix[2];
	    suffix[2] = *t;
	  }
	}


} /* end digit_suffix */


/*------------------------------------------------------------------
 *
 * Name:        tt_user_heard
 *
 * Purpose:     Record information from an APRStt transmission.
 *
 * Inputs:      callsign	- full or an abbreviation
 *		ssid
 *		overlay		- or symbol table identifier
 *		symbol
 *		loc_text	- Original text for non lat/lon location
 *		latitude
 *		longitude
 *		ambiguity
 *		freq
 *		ctcss
 *		comment
 *		mic_e
 *		dao
 *
 * Outputs:	Information is stored in table above.
 *		Last heard time is updated.
 *		Object Report transmission is scheduled.
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 *----------------------------------------------------------------*/

int tt_user_heard (char *callsign, int ssid, char overlay, char symbol, char *loc_text, double latitude, 
		double longitude, int ambiguity, char *freq, char *ctcss, char *comment, char mic_e, char *dao)
{
	int i;



	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("tt_user_heard (%s, %d, %c, %c, %s, ...)\n", callsign, ssid, overlay, symbol, loc_text);

/*
 * At this time all messages are expected to contain a callsign.
 * Other types of messages, not related to a particular person/object
 * are a future possibility. 
 */
	if (callsign[0] == '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  printf ("APRStt tone sequence did not include callsign / object name.\n");
	  return (TT_ERROR_NO_CALL);
	}

/*
 * Is it someone new or a returning user?
 */
	i = tt_user_search (callsign, overlay);
	if (i == -1) {

/*
 * New person.  Create new table entry with all available information.
 */
	  i = find_avail ();

	  assert (i >= 0 && i < MAX_TT_USERS);
	  strlcpy (tt_user[i].callsign, callsign, sizeof(tt_user[i].callsign));
	  tt_user[i].count = 1;
	  tt_user[i].ssid = ssid;
	  tt_user[i].overlay = overlay;
	  tt_user[i].symbol = symbol;
	  digit_suffix(tt_user[i].callsign, tt_user[i].digit_suffix);
	  strlcpy (tt_user[i].loc_text, loc_text, sizeof(tt_user[i].loc_text));

	  if (latitude != G_UNKNOWN && longitude != G_UNKNOWN) {
	    /* We have specific location. */
	    tt_user[i].corral_slot = 0;
	    tt_user[i].latitude = latitude;
	    tt_user[i].longitude = longitude;
	  }
	  else {
	    /* Unknown location, put it in the corral. */
	    tt_user[i].corral_slot = corral_slot();
	  }

	  tt_user[i].ambiguity = ambiguity;

	  strlcpy (tt_user[i].freq, freq, sizeof(tt_user[i].freq));
	  strlcpy (tt_user[i].ctcss, ctcss, sizeof(tt_user[i].ctcss));
	  strlcpy (tt_user[i].comment, comment, sizeof(tt_user[i].comment));
	  tt_user[i].mic_e = mic_e;
	  strlcpy(tt_user[i].dao, dao, sizeof(tt_user[i].dao));
	}
	else {
/*
 * Known user.  Update with any new information.
 * Keep any old values where not being updated.
 */
	  assert (i >= 0 && i < MAX_TT_USERS);

	  tt_user[i].count++;

	  /* Any reason to look at ssid here? */

	  /* Update the symbol if not the default. */

	  if (overlay != APRSTT_DEFAULT_SYMTAB || symbol != APRSTT_DEFAULT_SYMBOL) {
	    tt_user[i].overlay = overlay;
	    tt_user[i].symbol = symbol;
	  }

	  if (strlen(loc_text) > 0) {
	    strlcpy (tt_user[i].loc_text, loc_text, sizeof(tt_user[i].loc_text));
	  }

	  if (latitude != G_UNKNOWN && longitude != G_UNKNOWN) {
	    /* We have specific location. */
	    tt_user[i].corral_slot = 0;
	    tt_user[i].latitude = latitude;
	    tt_user[i].longitude = longitude;
	  }

	  if (ambiguity != G_UNKNOWN) {
	    tt_user[i].ambiguity = ambiguity;
	  }

	  if (freq[0] != '\0') {
	    strlcpy (tt_user[i].freq, freq, sizeof(tt_user[i].freq));
	  }

	  if (ctcss[0] != '\0') {
	    strlcpy (tt_user[i].ctcss, ctcss, sizeof(tt_user[i].ctcss));
	  }

	  if (comment[0] != '\0') {
	    strlcpy (tt_user[i].comment, comment, MAX_COMMENT_LEN);
	    tt_user[i].comment[MAX_COMMENT_LEN] = '\0';
	  }

	  if (mic_e != ' ') {
	    tt_user[i].mic_e = mic_e;
	  }

	  if (strlen(dao) > 0) {
	    strlcpy(tt_user[i].dao, dao, sizeof(tt_user[i].dao));
	  }
	}

/*
 * In both cases, note last time heard and schedule object report transmission. 
 */
	tt_user[i].last_heard = time(NULL);
	tt_user[i].xmits = 0;
	tt_user[i].next_xmit = tt_user[i].last_heard + save_tt_config_p->xmit_delay[0];

/*
 * Send to applications and IGate immediately.
 */

	xmit_object_report (i, 1);	

/*
 * Put properties into environment variables in preparation
 * for calling a user-specified script.
 */

	tt_setenv (i);

	return (0);	/* Success! */

} /* end tt_user_heard */


/*------------------------------------------------------------------
 *
 * Name:        tt_user_background
 *
 * Purpose:     
 *
 * Inputs:      
 *
 * Outputs:	Append to transmit queue.
 *
 * Returns:     None
 *
 * Description:	...... TBD
 *
 *----------------------------------------------------------------*/

void tt_user_background (void)
{
	time_t now = time(NULL);
	int i;

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("tt_user_background()  now = %d\n", (int)now);


	for (i=0; i<MAX_TT_USERS; i++) {

	  assert (i >= 0 && i < MAX_TT_USERS);

	  if (tt_user[i].callsign[0] != '\0') {
	    if (tt_user[i].xmits < save_tt_config_p->num_xmits && tt_user[i].next_xmit <= now) {


	      //text_color_set(DW_COLOR_DEBUG);
	      //dw_printf ("tt_user_background()  now = %d\n", (int)now);
	      //tt_user_dump ();

	      xmit_object_report (i, 0);	
 
	      /* Increase count of number times this one was sent. */
	      tt_user[i].xmits++;
	      if (tt_user[i].xmits < save_tt_config_p->num_xmits) {
	        /* Schedule next one. */
	        tt_user[i].next_xmit += save_tt_config_p->xmit_delay[tt_user[i].xmits];    
	      }

	      //tt_user_dump ();
	    }
	  }
	}

/*
 * Purge if too old.
 */
	for (i=0; i<MAX_TT_USERS; i++) {
	  if (tt_user[i].callsign[0] != '\0') {
	    if (tt_user[i].last_heard + save_tt_config_p->retain_time < now) {

	     //dw_printf ("debug: purging expired user %d\n", i);

	      clear_user (i);
	    }
	  }
	}
}


/*------------------------------------------------------------------
 *
 * Name:        xmit_object_report
 *
 * Purpose:     Create object report packet and put into transmit queue.
 *
 * Inputs:      i	   - Index into user table.
 *
 *		first_time - Is this being called immediately after the tone sequence
 *			 	was received or after some delay?
 *				For the former, we send to any attached applications
 *				and the IGate.
 *				For the latter, we transmit over radio.
 *
 * Outputs:	Append to transmit queue.
 *
 * Returns:     None
 *
 * Description:	Details for specified user are converted to
 *		"Object Report Format" and added to the transmit queue.
 *
 *		If the user did not report a position, we have to make 
 *		up something so the corresponding object will show up on
 *		the map or other list of nearby stations.
 *
 *		The traditional approach is to put them in different 
 *		positions in the "corral" by applying increments of an
 *		offset from the starting position.  This has two 
 *		unfortunate properties.  It gives the illusion we know
 *		where the person is located.   Being in the ,,,
 *
 *----------------------------------------------------------------*/

static void xmit_object_report (int i, int first_time)
{
	char object_name[20];		// xxxxxxxxx or xxxxxx-nn
	char info_comment[200];		// usercomment [locationtext] /status !DAO!
	char object_info[250];		// info part of Object Report packet
	char stemp[300];		// src>dest,path:object_info

	double olat, olong;
	int oambig;			// Position ambiguity.
	packet_t pp;
	char c4[4];

	//text_color_set(DW_COLOR_DEBUG);
	//printf ("xmit_object_report (index = %d, first_time = %d) rx = %d, tx = %d\n", i, first_time, 
	//			save_tt_config_p->obj_recv_chan, save_tt_config_p->obj_xmit_chan);

	assert (i >= 0 && i < MAX_TT_USERS);

/*
 * Prepare the object name.  
 * Tack on "-12" if it is a callsign.
 */
	strlcpy (object_name, tt_user[i].callsign, sizeof(object_name));

	if (strlen(object_name) <= 6 && tt_user[i].ssid != 0) {
	  char stemp8[8];
	  snprintf (stemp8, sizeof(stemp8), "-%d", tt_user[i].ssid);
	  strlcat (object_name, stemp8, sizeof(object_name));
	}

	if (tt_user[i].corral_slot == 0) {
/* 
 * Known location.
 */
	  olat = tt_user[i].latitude;
	  olong = tt_user[i].longitude;
	  oambig = tt_user[i].ambiguity;
	  if (oambig == G_UNKNOWN) oambig = 0;
	}
	else {
/*
 * Use made up position in the corral.
 */
	  double c_lat = save_tt_config_p->corral_lat;		// Corral latitude.
	  double c_long = save_tt_config_p->corral_lon;		// Corral longitude.
	  double c_offs =  save_tt_config_p->corral_offset;	// Corral (latitude) offset.

	  olat = c_lat - (tt_user[i].corral_slot - 1) * c_offs;
	  olong = c_long;
	  oambig = 0;
	}

/*
 * Build comment field from various information.
 *
 * 	usercomment [locationtext] /status !DAO!
 *
 * Any frequency is inserted at beginning later.
 */
	strlcpy (info_comment, "", sizeof(info_comment));

	if (strlen(tt_user[i].comment) != 0) {
	  strlcat (info_comment, tt_user[i].comment, sizeof(info_comment));
	}

	if (strlen(tt_user[i].loc_text) > 0) {
	  if (strlen(info_comment) > 0) {
	    strlcat (info_comment, " ", sizeof(info_comment));
	  }
	  strlcat (info_comment, "[", sizeof(info_comment));
	  strlcat (info_comment, tt_user[i].loc_text, sizeof(info_comment));
	  strlcat (info_comment, "]", sizeof(info_comment));
	}

	if (tt_user[i].mic_e >= '1' && tt_user[i].mic_e <= '9') {
	  
	  if (strlen(info_comment) > 0) {
	    strlcat (info_comment, " ", sizeof(info_comment));
	  }

	  // Insert "/" if status does not already begin with it.
	  if (save_tt_config_p->status[tt_user[i].mic_e - '0'][0] != '/') {
	    strlcat (info_comment, "/", sizeof(info_comment));
	  }
	  strlcat (info_comment, save_tt_config_p->status[tt_user[i].mic_e - '0'], sizeof(info_comment));
	}

	if (strlen(tt_user[i].dao) > 0) {
	  if (strlen(info_comment) > 0) {
	    strlcat (info_comment, " ", sizeof(info_comment));
	  }
	  strlcat (info_comment, tt_user[i].dao, sizeof(info_comment));
	}

	/* Official limit is 43 characters. */
	//info_comment[MAX_COMMENT_LEN] = '\0';
	
/*
 * Packet header is built from mycall (of transmit channel) and software version.
 */

	if (save_tt_config_p->obj_xmit_chan >= 0) {
	  strlcpy (stemp, save_audio_config_p->achan[save_tt_config_p->obj_xmit_chan].mycall, sizeof(stemp));
	}
	else {
	  strlcpy (stemp, save_audio_config_p->achan[save_tt_config_p->obj_recv_chan].mycall, sizeof(stemp));
	}
	strlcat (stemp, ">", sizeof(stemp));
	strlcat (stemp, APP_TOCALL, sizeof(stemp));
	c4[0] = '0' + MAJOR_VERSION;
	c4[1] = '0' + MINOR_VERSION;
	c4[2] = '\0';
	strlcat (stemp, c4, sizeof(stemp));

/*
 * Append via path, for transmission, if specified. 
 */

	if ( ! first_time && save_tt_config_p->obj_xmit_via[0] != '\0') {
	  strlcat (stemp, ",", sizeof(stemp));
	  strlcat (stemp, save_tt_config_p->obj_xmit_via, sizeof(stemp));
	}

	strlcat (stemp, ":", sizeof(stemp));

	encode_object (object_name, 0, tt_user[i].last_heard, olat, olong, oambig,
		tt_user[i].overlay, tt_user[i].symbol, 
		0,0,0,NULL, G_UNKNOWN, G_UNKNOWN,	/* PHGD, Course/Speed */
		strlen(tt_user[i].freq) > 0 ? atof(tt_user[i].freq) : G_UNKNOWN,
		strlen(tt_user[i].ctcss) > 0 ? atof(tt_user[i].ctcss) : G_UNKNOWN,
		G_UNKNOWN,	/* CTCSS */
		info_comment, object_info, sizeof(object_info));

	strlcat (stemp, object_info, sizeof(stemp));

#if TT_MAIN

	printf ("---> %s\n\n", stemp);

#else

	if (first_time) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("[APRStt] %s\n", stemp);
	}

/*
 * Convert text to packet.
 */
	pp = ax25_from_text (stemp, 1);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\"%s\"\n", stemp);
	  return;
	}


/* 
 * Send to one or more of the following depending on configuration:
 *	Transmit queue.
 *	Any attached application(s).
 * 	IGate.
 *
 * When transmitting over the radio, it gets sent multiple times, to help
 * probability of being heard, with increasing delays between.
 *
 * The other methods are reliable so we only want to send it once.
 */

	if (first_time && save_tt_config_p->obj_send_to_app)  {
	  unsigned char fbuf[AX25_MAX_PACKET_LEN];
	  int flen;

 	  // TODO1.3:  Put a wrapper around this so we only call one function to send by all methods.
	  // We see the same sequence in direwolf.c.

	  flen = ax25_pack(pp, fbuf);

	  server_send_rec_packet (save_tt_config_p->obj_recv_chan, pp, fbuf, flen);
	  kissnet_send_rec_packet (save_tt_config_p->obj_recv_chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);
	  kissserial_send_rec_packet (save_tt_config_p->obj_recv_chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);
	  kisspt_send_rec_packet (save_tt_config_p->obj_recv_chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);
	}

	if (first_time && save_tt_config_p->obj_send_to_ig)  {

	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("xmit_object_report (): send to IGate\n");

          igate_send_rec_packet (save_tt_config_p->obj_recv_chan, pp);

	}

	if ( ! first_time && save_tt_config_p->obj_xmit_chan >= 0) {

	  /* Remember it so we don't digipeat our own. */

	  dedupe_remember (pp, save_tt_config_p->obj_xmit_chan);

	  tq_append (save_tt_config_p->obj_xmit_chan, TQ_PRIO_1_LO, pp);
	}
	else {
	  ax25_delete (pp);
	}

#endif 
	

}

static const char *letters[26] = {
        "Alpha",
        "Bravo",
        "Charlie",
        "Delta",
        "Echo",
        "Foxtrot",
        "Golf",
        "Hotel",
        "India",
        "Juliet",
        "Kilo",
        "Lima",
        "Mike",
        "November",
        "Oscar",
        "Papa",
        "Quebec",
        "Romeo",
        "Sierra",
        "Tango",
        "Uniform",
        "Victor",
        "Whiskey",
        "X-ray",
        "Yankee",
        "Zulu"
};

static const char *digits[10] = {
	"Zero",
	"One",
	"Two",
	"Three",
	"Four",
	"Five",
	"Six",
	"Seven",
	"Eight",
	"Nine"		
};


/*------------------------------------------------------------------
 *
 * Name:        tt_setenv
 *
 * Purpose:     Put information in environment variables in preparation
 *		for calling a user-supplied script for custom processing.
 *
 * Inputs:      i	- Index into tt_user table.
 *
 * Description:	Timestamps displayed relative to current time.
 *
 *----------------------------------------------------------------*/


static void tt_setenv (int i)
{
	char stemp[256];
	char t2[2];
	char *p;

	assert (i >= 0 && i < MAX_TT_USERS);

	setenv ("TTCALL", tt_user[i].callsign, 1);

	strlcpy (stemp, "", sizeof(stemp));
	t2[1] = '\0';
	for (p = tt_user[i].callsign; *p != '\0'; p++) {
	  t2[0] = *p;
	  strlcat (stemp, t2, sizeof(stemp));
	  if (p[1] != '\0') strlcat (stemp, " ", sizeof(stemp));
	}
	setenv ("TTCALLSP", stemp, 1);

	strlcpy (stemp, "", sizeof(stemp));
	for (p = tt_user[i].callsign; *p != '\0'; p++) {
	  if (isupper(*p)) {
	    strlcat (stemp, letters[*p - 'A'], sizeof(stemp));
	  }
	  else if (islower(*p)) {
	    strlcat (stemp, letters[*p - 'a'], sizeof(stemp));
	  }
	  else if (isdigit(*p)) {
	    strlcat (stemp, digits[*p - '0'], sizeof(stemp));
	  }
	  else {
	    t2[0] = *p;
	    strlcat (stemp, t2, sizeof(stemp));
	  }
	  if (p[1] != '\0') strlcat (stemp, " ", sizeof(stemp));
	}
	setenv ("TTCALLPH", stemp, 1);

	snprintf (stemp, sizeof(stemp), "%d", tt_user[i].ssid);
	setenv ("TTSSID",stemp , 1);

	snprintf (stemp, sizeof(stemp), "%d", tt_user[i].count);
	setenv ("TTCOUNT",stemp , 1);

	snprintf (stemp, sizeof(stemp), "%c%c", tt_user[i].overlay, tt_user[i].symbol);
	setenv ("TTSYMBOL",stemp , 1);

	snprintf (stemp, sizeof(stemp), "%.6f", tt_user[i].latitude);
	setenv ("TTLAT",stemp , 1);

	snprintf (stemp, sizeof(stemp), "%.6f", tt_user[i].longitude);
	setenv ("TTLON",stemp , 1);

	setenv ("TTFREQ", tt_user[i].freq, 1);

	// TODO: Should convert to actual frequency. e.g.  074 becomes 74.4
	// There is some code for this in decode_aprs.c but not broken out
	// into a function that we could use from here.
	// TODO: Document this environment variable after converting.

	setenv ("TTCTCSS", tt_user[i].ctcss, 1);

	setenv ("TTCOMMENT", tt_user[i].comment, 1);

	setenv ("TTLOC", tt_user[i].loc_text, 1);

	if (tt_user[i].mic_e >= '1' && tt_user[i].mic_e <= '9') {
	  setenv ("TTSTATUS", save_tt_config_p->status[tt_user[i].mic_e - '0'], 1);
	}
	else {
	  setenv ("TTSTATUS", "", 1);
	}

	setenv ("TTDAO", tt_user[i].dao, 1);

} /* end tt_setenv */



/*------------------------------------------------------------------
 *
 * Name:        tt_user_dump
 *
 * Purpose:     Print information about known users for debugging.
 *
 * Inputs:      None.
 *
 * Description:	Timestamps displayed relative to current time.
 *
 *----------------------------------------------------------------*/

void tt_user_dump (void)
{
	int i;
	time_t now = time(NULL);
	
	printf ("call   ov suf lsthrd xmit nxt cor  lat    long freq     ctcss m comment\n");
	for (i=0; i<MAX_TT_USERS; i++) {
	  if (tt_user[i].callsign[0] != '\0') {
	    printf ("%-6s %c%c %-3s %6d %d %+6d %d %6.2f %7.2f %-10s %-3s %c %s\n",
	    	tt_user[i].callsign,
	    	tt_user[i].overlay,
	    	tt_user[i].symbol,
	    	tt_user[i].digit_suffix,
	    	(int)(tt_user[i].last_heard - now),
	    	tt_user[i].xmits,
	    	(int)(tt_user[i].next_xmit - now),
	    	tt_user[i].corral_slot,
	    	tt_user[i].latitude,
	    	tt_user[i].longitude,
	    	tt_user[i].freq,
		tt_user[i].ctcss,
	    	tt_user[i].mic_e,
	    	tt_user[i].comment);
	  }
	}
			
}


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test for some functions in this file.
 *
 * Description:	Just a smattering, not an organized test.
 *
 * 		$ rm a.exe ; gcc -DTT_MAIN -Iregex tt_user.c tt_text.c encode_aprs.c latlong.c textcolor.c misc.a ; ./a.exe
 *
 *----------------------------------------------------------------*/


#if TT_MAIN


static struct audio_s my_audio_config;

static struct tt_config_s my_tt_config;


int main (int argc, char *argv[])
{
	int n;

/* Fake audio config - All we care about is mycall for constructing object report packet. */

	memset (&my_audio_config, 0, sizeof(my_audio_config));

	strlcpy (my_audio_config.achan[0].mycall, "WB2OSZ-15", sizeof(my_audio_config.achan[0].mycall));

/* Fake TT gateway config. */

	memset (&my_tt_config, 0, sizeof(my_tt_config));	

	/* Don't care about the location translation here. */

	my_tt_config.retain_time = 20;		/* Normally 80 minutes. */
	my_tt_config.num_xmits = 3;
	assert (my_tt_config.num_xmits <= TT_MAX_XMITS);
	my_tt_config.xmit_delay[0] = 3;		/* Before initial transmission. */
	my_tt_config.xmit_delay[1] = 5;
	my_tt_config.xmit_delay[2] = 5;

	my_tt_config.corral_lat = 42.61900;
	my_tt_config.corral_lon = -71.34717;
	my_tt_config.corral_offset = 0.02 / 60;
	my_tt_config.corral_ambiguity = 0;


	tt_user_init(&my_audio_config, &my_tt_config);

// tt_user_heard (char *callsign, int ssid, char overlay, char symbol, char *loc_text, double latitude,
//              double longitude, int ambiguity, char *freq, char *ctcss, char *comment, char mic_e, char *dao);

	tt_user_heard ("TEST1",  12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	SLEEP_SEC (1);
	tt_user_heard ("TEST2",  12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	SLEEP_SEC (1);
	tt_user_heard ("TEST3",  12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	SLEEP_SEC (1);
	tt_user_heard ("TEST4",  12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	SLEEP_SEC (1);
	tt_user_heard ("WB2OSZ", 12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	tt_user_heard ("K2H",    12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "", ' ', "!T99!");
	tt_user_dump ();

	tt_user_heard ("679",    12, 'J', 'A', "", 37.25,     -71.75,    0, "", " ", " ", ' ', "!T99!");
	tt_user_heard ("WB2OSZ", 12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "146.520MHz", "", "", ' ', "!T99!");
	tt_user_heard ("WB1GOF", 12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "146.955MHz", "074", "", ' ', "!T99!");
	tt_user_heard ("679",    12, 'J', 'A', "", G_UNKNOWN, G_UNKNOWN, 0, "", "", "Hello, world", '9', "!T99!");
	tt_user_dump ();
	
	for (n=0; n<30; n++) {
	  SLEEP_SEC(1);
	  tt_user_background ();
	}

	return(0);

}  /* end main */

#endif		/* unit test */


/* end tt-user.c */


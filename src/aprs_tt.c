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
 * Module:      aprs_tt.c
 *
 * Purpose:   	First half of APRStt gateway.
 *
 * Description: This file contains functions to parse the tone sequences
 *		and extract meaning from them.
 *
 *		tt_user.c maintains information about users and
 *		generates the APRS Object Reports.
 *		
 *
 * References:	This is based upon APRStt (TM) documents with some
 *		artistic freedom.
 *
 *		http://www.aprs.org/aprstt.html
 *
 *---------------------------------------------------------------*/

#define APRS_TT_C 1

#include "direwolf.h"


// TODO:  clean up terminolgy.  
// "Message" has a specific meaning in APRS and this is not it.  
// Touch Tone sequence should be appropriate.
// What do we call the parts separated by * key?  Field.


#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <ctype.h>
#include <assert.h>

#include "version.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"		/* for process_rec_frame */
#include "textcolor.h"
#include "aprs_tt.h"
#include "tt_text.h"
#include "tt_user.h"
#include "symbols.h"
#include "latlong.h"
#include "dlq.h"
#include "demod.h"          /* for alevel_t & demod_get_audio_level() */
#include "tq.h"




// geotranz

#include "utm.h"
#include "mgrs.h"
#include "usng.h"
#include "error_string.h"

/* Convert between degrees and radians. */

#define D2R(d) ((d) * M_PI / 180.)
#define R2D(r) ((r) * 180. / M_PI)


/*
 * Touch Tone sequences are accumulated here until # terminator found.
 * Kept separate for each audio channel so the gateway CAN be listening
 * on multiple channels at the same time.
 */

#define MAX_MSG_LEN 100

static char msg_str[MAX_CHANS][MAX_MSG_LEN+1];
static int msg_len[MAX_CHANS];

static int parse_fields (char *msg);
static int parse_callsign (char *e);
static int parse_object_name (char *e);
static int parse_symbol (char *e);
static int parse_aprstt3_call (char *e);
static int parse_location (char *e);
static int parse_comment (char *e);
static int expand_macro (char *e);
#ifndef TT_MAIN
static void raw_tt_data_to_app (int chan, char *msg);
#endif
static int find_ttloc_match (char *e, char *xstr, char *ystr, char *zstr, char *bstr, char *dstr, size_t valstrsize);

#if TT_MAIN
static void check_result (void);
#endif

static int tt_debug = 0;


/*------------------------------------------------------------------
 *
 * Name:        aprs_tt_init
 *
 * Purpose:     Initialize the APRStt gateway at system startup time.
 *
 * Inputs:      P	- Pointer to configuration options gathered by config.c.
 *		debug	- Debug printing control.
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

static struct tt_config_s tt_config;

#if TT_MAIN
#define NUM_TEST_CONFIG (sizeof(test_config) / sizeof (struct ttloc_s))
static struct ttloc_s test_config[] = {

	{ TTLOC_POINT, "B01", .point.lat = 12.25, .point.lon = 56.25 },
	{ TTLOC_POINT, "B988", .point.lat = 12.50, .point.lon = 56.50 },

	{ TTLOC_VECTOR, "B5bbbdddd", .vector.lat = 53., .vector.lon = -1., .vector.scale = 1000. },  /* km units */

	/* Hilltop Tower http://www.aprs.org/aprs-jamboree-2013.html */
	{ TTLOC_VECTOR, "B5bbbddd", .vector.lat = 37+55.37/60., .vector.lon = -(81+7.86/60.), .vector.scale = 16.09344 },   /* .01 mile units */

	{ TTLOC_GRID, "B2xxyy", .grid.lat0 = 12.00, .grid.lon0 = 56.00, 
				.grid.lat9 = 12.99, .grid.lon9 = 56.99 },
	{ TTLOC_GRID, "Byyyxxx", .grid.lat0 = 37 + 50./60.0, .grid.lon0 = 81, 
				.grid.lat9 = 37 + 59.99/60.0, .grid.lon9 = 81 + 9.99/60.0 },

	{ TTLOC_MHEAD, "BAxxxxxx", .mhead.prefix = "326129" },

	{ TTLOC_SATSQ, "BAxxxx" },

	{ TTLOC_MACRO, "xxyyy", .macro.definition = "B9xx*AB166*AA2B4C5B3B0Ayyy" },
	{ TTLOC_MACRO, "xxxxzzzzzzzzzz", .macro.definition = "BAxxxx*ACzzzzzzzzzz" },
}; 
#endif


void aprs_tt_init (struct tt_config_s *p, int debug)
{
	int c;
	tt_debug = debug;

#if TT_MAIN
	/* For unit testing. */

	memset (&tt_config, 0, sizeof(struct tt_config_s));	
	tt_config.ttloc_size = NUM_TEST_CONFIG;
	tt_config.ttloc_ptr = test_config;
	tt_config.ttloc_len = NUM_TEST_CONFIG;

	/* Don't care about xmit timing or corral here. */
#else
	// TODO: Keep ptr instead of making a copy.
	memcpy (&tt_config, p, sizeof(struct tt_config_s));
#endif
	for (c=0; c<MAX_CHANS; c++) {	
	  msg_len[c] = 0;
	  msg_str[c][0] = '\0';
	}

}


/*------------------------------------------------------------------
 *
 * Name:        aprs_tt_button
 *
 * Purpose:     Process one received button press.
 *
 * Inputs:      chan		- Audio channel it came from.
 *
 *		button		0123456789ABCD*#	- Received button press.
 *				$			- No activity timeout.
 *				space			- Quiet time filler.
 *
 * Returns:     None
 *
 * Description:	Individual key presses are accumulated here until
 *		the # message terminator is found.
 *		The complete message is then processed.
 *		The touch tone decoder sends $ if no activity
 *		for some amount of time, perhaps 5 seconds.
 *		A partially accumulated message is discarded if
 *		there is a long gap.
 *
 *		'.' means no activity during processing period.
 *		space, between blocks, shouldn't get here.
 *
 *----------------------------------------------------------------*/

#ifndef TT_MAIN

void aprs_tt_button (int chan, char button)
{
	static int poll_period = 0;

	assert (chan >= 0 && chan < MAX_CHANS);


	//if (button != '.') {
	//  dw_printf ("aprs_tt_button (%d, '%c')\n", chan, button);
	//}


// TODO:  Might make more sense to put timeout here rather in the dtmf decoder.

	if (button == '$') {

/* Timeout reset. */

	  msg_len[chan] = 0;
	  msg_str[chan][0] = '\0';
	}
	else if (button != '.' && button != ' ') {
	  if (msg_len[chan] < MAX_MSG_LEN) {
	    msg_str[chan][msg_len[chan]++] = button;
	    msg_str[chan][msg_len[chan]] = '\0';
	  }
	  if (button == '#') {

/* 
 * Put into the receive queue like any other packet.
 * This way they are all processed by the common receive thread
 * rather than the thread associated with the particular audio device.
 */
	    raw_tt_data_to_app (chan, msg_str[chan]);

	    msg_len[chan] = 0;
	    msg_str[chan][0] = '\0';
	  }
	}
	else {

/* 
 * Idle time. Poll occasionally for processing. 
 * Timing would be off we we are listening to more than
 * one channel so do this only for the one specified
 * in the TTOBJ command. 
 */

	  if (chan == tt_config.obj_recv_chan) {	  
	    poll_period++;
	    if (poll_period >= 39) {
	      poll_period = 0;
	      tt_user_background ();
	    }
	  }
	}	
  
} /* end aprs_tt_button */

#endif

/*------------------------------------------------------------------
 *
 * Name:        aprs_tt_sequence
 *
 * Purpose:     Process complete received touch tone sequence
 *		terminated by #.
 *
 * Inputs:      chan		- Audio channel it came from.
 *
 *		msg		- String of DTMF buttons.
 *				  # should be the final character.
 *
 * Returns:     None
 *
 * Description:	Process a complete tone sequence.
 *		It should have one or more fields separated by *
 *		and terminated by a final # like these:
 *
 *		callsign #
 *		entry1 * callsign #
 *		entry1 * entry * callsign #
 *
 * Limitation:	Has one set of static data for communication among
 *		group of functions.  This shouldn't be a problem
 *		when receiving on multiple channels at once 
 *		because they get serialized thru the receive packet queue.
 *
 *----------------------------------------------------------------*/

static char m_callsign[20];	/* really object name */

/*
 * Standard APRStt has symbol code 'A' (box) with overlay of 0-9, A-Z. 
 *
 * Dire Wolf extension allows:
 *	Symbol table '/' (primary), any symbol code.
 *	Symbol table '\' (alternate), any symbol code.
 *	Alternate table symbol code, overlay of 0-9, A-Z.
 */

static char m_symtab_or_overlay;
static char m_symbol_code;		// Default 'A'

static char m_loc_text[24];
static double m_longitude;		// Set to G_UNKNOWN if not defined.
static double m_latitude;		// Set to G_UNKNOWN if not defined.
static int m_ambiguity;
static char m_comment[200];
static char m_freq[12];
static char m_ctcss[8];
static char m_mic_e;
static char m_dao[6];
static int m_ssid;			// Default 12 for APRStt user.



void aprs_tt_sequence (int chan, char *msg)
{
	int err;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n\"%s\"\n", msg);
#endif

/* 
 * Discard empty message. 
 * In case # is there as optional start. 
 */

	if (msg[0] == '#') return;

/*
 * The parse functions will fill these in. 
 */
	strlcpy (m_callsign, "", sizeof(m_callsign));
	m_symtab_or_overlay = APRSTT_DEFAULT_SYMTAB;
	m_symbol_code = APRSTT_DEFAULT_SYMBOL;
	strlcpy (m_loc_text, "", sizeof(m_loc_text));
	m_longitude = G_UNKNOWN;
	m_latitude = G_UNKNOWN;
	m_ambiguity = 0;
	strlcpy (m_comment, "", sizeof(m_comment));
	strlcpy (m_freq, "", sizeof(m_freq));
	strlcpy (m_ctcss, "", sizeof(m_ctcss));
	m_mic_e = ' ';
	strlcpy (m_dao, "!T  !", sizeof(m_dao));	/* start out unknown */
	m_ssid = 12;

/*
 * Parse the touch tone sequence.
 */
	err = parse_fields (msg);

#if defined(DEBUG)
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("callsign=\"%s\", ssid=%d, symbol=\"%c%c\", freq=\"%s\", ctcss=\"%s\", comment=\"%s\", lat=%.4f, lon=%.4f, dao=\"%s\"\n",
		m_callsign, m_ssid, m_symtab_or_overlay, m_symbol_code, m_freq, m_ctcss, m_comment, m_latitude, m_longitude, m_dao);
#endif

#if TT_MAIN
	(void)err;		// suppress variable set but not used warning.
	check_result ();	// for unit testing.
#else

/*
 * If digested successfully.  Add to our list of users and schedule transmissions.
 */

	if (err == 0) {

	  err = tt_user_heard (m_callsign, m_ssid, m_symtab_or_overlay, m_symbol_code, 
		m_loc_text, m_latitude, m_longitude, m_ambiguity,
		m_freq, m_ctcss, m_comment, m_mic_e, m_dao);
	}


/*
 * If a command / script was supplied, run it now.
 * This can do additional processing and provide a custom audible response.
 * This is done only for the success case.
 * It might be useful to run it for error cases as well but we currently
 * don't pass in the success / failure code to know the difference.
 */
	char script_response[1000];

	strlcpy (script_response, "", sizeof(script_response));

	if (err == 0 && strlen(tt_config.ttcmd) > 0) {

	  dw_run_cmd (tt_config.ttcmd, 1, script_response, sizeof(script_response));

	}

/*
 * Send response to user by constructing packet with SPEECH or MORSE as destination.
 * Source shouldn't matter because it doesn't get transmitted as AX.25 frame.
 * Use high priority queue for consistent timing.
 *
 * Anything from script, above, will override other predefined responses.
 */

	char audible_response[sizeof(script_response) + 16];

	snprintf (audible_response, sizeof(audible_response), 
					"APRSTT>%s:%s", 
					tt_config.response[err].method,
					(strlen(script_response) > 0) ? script_response : tt_config.response[err].mtext);

	packet_t pp;

	pp = ax25_from_text (audible_response, 0);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error. Couldn't make frame from \"%s\"\n", audible_response);
	  return;
	}

	tq_append (chan, TQ_PRIO_0_HI, pp);

#endif  /* ifndef TT_MAIN */

} /* end aprs_tt_sequence */


/*------------------------------------------------------------------
 *
 * Name:        parse_fields
 *
 * Purpose:     Separate the complete string of touch tone characters
 *		into fields, delimited by *, and process each.
 *
 * Inputs:      msg		- String of DTMF buttons.
 *
 * Returns:     None
 *
 * Description:	It should have one or more fields separated by *.
 *
 *		callsign #
 *		entry1 * callsign #
 *		entry1 * entry * callsign #
 *
 *		Note that this will be used recursively when macros
 *		are expanded.
 *
 *		"To iterate is human, to recurse divine."
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 *----------------------------------------------------------------*/

static int parse_fields (char *msg)
{
	char stemp[MAX_MSG_LEN+1];
	char *e;
	char *save;
	int err;


	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("parse_fields (%s).\n", msg);

// Make a copy of msg because strtok corrupts the original.
// While we are at it, remove any blanks.
// This should not happen with DTMF reception but could happen
// in manually crafted strings for testing.

	int n = 0;
	for (char *m = msg; *m != '\0' && n < sizeof(stemp)-1; m++) {
	  if (*m != ' ') {
	    stemp[n++] = *m;
	  }
	}
	stemp[n] = '\0';

	e = strtok_r (stemp, "*#", &save);
	while (e != NULL) {

	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("parse_fields () field = %s\n", e);

	  switch (*e) {

	    case 'A': 
	      
	      switch (e[1]) {

	        case 'A':			/* AA object-name */
	          err = parse_object_name (e);
	          if (err != 0) return (err);
	          break;

	        case 'B':			/* AB symbol */
	          err = parse_symbol (e);
	          if (err != 0) return (err);
	          break;

	        case 'C':			/* AC new-style-callsign */

	          err = parse_aprstt3_call (e);
	          if (err != 0) return (err);
	          break;

	        default:			/* Traditional style call or suffix */
	          err = parse_callsign (e);
	          if (err != 0) return (err);
	          break;
	      }
	      break;

	    case 'B': 
	      err = parse_location (e);
	      if (err != 0) return (err);
	      break;

	    case 'C': 
	      err = parse_comment (e);
	      if (err != 0) return (err);
	      break;

	    case '0': 
	    case '1': 
	    case '2': 
	    case '3': 
	    case '4': 
	    case '5': 
	    case '6': 
	    case '7': 
	    case '8': 
	    case '9': 
	      err = expand_macro (e);
	      if (err != 0) return (err);
	      break;

	    case '\0':
	      /* Empty field.  Just ignore it. */
	      /* This would happen if someone uses a leading *. */
	      break;

	    default:

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Field does not start with A, B, C, or digit: \"%s\"\n", e);
	      return (TT_ERROR_D_MSG);

	  }
	
	  e = strtok_r (NULL, "*#", &save);
	}

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("parse_fields () normal return\n");

	return (0);

} /* end parse_fields */


/*------------------------------------------------------------------
 *
 * Name:        expand_macro
 *
 * Purpose:     Expand compact form "macro" to full format then process.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should contain only digits.
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	Separate out the fields, perform substitution, 
 *		call parse_fields for processing.
 *
 *
 * Future:	Generalize this to allow any lower case letter for substitution?
 *
 *----------------------------------------------------------------*/

#define VALSTRSIZE 20

static int expand_macro (char *e) 
{
	//int len;
	int ipat;
	char xstr[VALSTRSIZE], ystr[VALSTRSIZE], zstr[VALSTRSIZE], bstr[VALSTRSIZE], dstr[VALSTRSIZE];
	char stemp[MAX_MSG_LEN+1];
	char *d;


	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("Macro tone sequence: '%s'\n", e);

	//len = strlen(e);

	ipat = find_ttloc_match (e, xstr, ystr, zstr, bstr, dstr, VALSTRSIZE);

	if (ipat >= 0) {

	  // Why did we print b & d here?
	  // Documentation says only x, y, z can be used with macros.
	  // Only those 3 are processed below.

	  //dw_printf ("Matched pattern %3d: '%s', x=%s, y=%s, z=%s, b=%s, d=%s\n", ipat, tt_config.ttloc_ptr[ipat].pattern, xstr, ystr, zstr, bstr, dstr);
	  dw_printf ("Matched pattern %3d: '%s', x=%s, y=%s, z=%s\n", ipat, tt_config.ttloc_ptr[ipat].pattern, xstr, ystr, zstr);

	  dw_printf ("Replace with:        '%s'\n", tt_config.ttloc_ptr[ipat].macro.definition);

	  if (tt_config.ttloc_ptr[ipat].type != TTLOC_MACRO) {

	     /* Found match to a different type.  Really shouldn't be here. */
	     /* Print internal error message... */
	     dw_printf ("expand_macro: type != TTLOC_MACRO\n");
	     return (TT_ERROR_INTERNAL);
	  }

/*
 * We found a match for the length and any fixed digits.
 * Substitute values in to the definition.
 */		
	  
	  strlcpy (stemp, "", sizeof(stemp));

	  for (d = tt_config.ttloc_ptr[ipat].macro.definition; *d != '\0'; d++) {

	    while (( *d == 'x' || *d == 'y' || *d == 'z') && *d == d[1]) {
	      /* Collapse adjacent matching substitution characters. */
	      d++;
	    }

	    switch (*d) {
	      case 'x':
		strlcat (stemp, xstr, sizeof(stemp));
	        break;
	      case 'y':
		strlcat (stemp, ystr, sizeof(stemp));
	        break;
	      case 'z':
		strlcat (stemp, zstr, sizeof(stemp));
	        break;
	      default:
		{
	        char c1[2];
	        c1[0] = *d;
	        c1[1] = '\0';
	        strlcat (stemp, c1, sizeof(stemp)); 
		}
		break;
	    }
	  }
/*
 * Process as if we heard this over the air.
 */

	  dw_printf ("After substitution:  '%s'\n", stemp);
	  return (parse_fields (stemp));
	}

	else {


	  /* Send reject sound. */
	  /* Does not match any macro definitions. */
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tone sequence did not match any pattern\n");
	  return (TT_ERROR_MACRO_NOMATCH);
	}
	
	/* should be unreachable */
	return (0);
}



/*------------------------------------------------------------------
 *
 * Name:        parse_callsign
 *
 * Purpose:     Extract traditional format callsign or object name from touch tone sequence.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "A" then a digit.
 *
 * Outputs:	m_callsign
 *
 *		m_symtab_or_overlay - Set to 0-9 or A-Z if specified.
 *
 *		m_symbol_code	- Always set to 'A' (Box, DTMF or RFID)
 *					If you want a different symbol, use the new
 *					object name format and separate symbol specification.
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	We recognize 3 different formats:
 *
 *		Annn		- 3 digits are a tactical callsign.  No overlay.
 *
 *		Annnvk		- Abbreviation with 3 digits, numeric overlay, checksum.
 *		Annnvvk		- Abbreviation with 3 digits, letter overlay, checksum.
 *
 *		Att...ttvk	- Full callsign in two key method, numeric overlay, checksum.
 *		Att...ttvvk	- Full callsign in two key method, letter overlay, checksum.
 *		
 *
 *----------------------------------------------------------------*/

static int checksum_not_ok (char *str, int len, char found)
{
	int i;
	int sum;
	char expected;

	sum = 0;
	for (i=0; i<len; i++) {
	  if (isdigit(str[i])) {
	    sum += str[i] - '0';
	  }
	  else if (str[i] >= 'A' && str[i] <= 'D') {
	    sum += str[i] - 'A' + 10;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("aprs_tt: checksum: bad character \"%c\" in checksum calculation!\n", str[i]);
	  }
	}
	expected =  '0' + (sum % 10);

	if (expected != found) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Bad checksum for \"%.*s\".  Expected %c but received %c.\n", len, str, expected, found);
	    return (TT_ERROR_BAD_CHECKSUM);
	}
	return (0);
}


static int parse_callsign (char *e)
{
	int len;
	char tttemp[40], stemp[30];

	if (tt_debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("APRStt parse callsign (starts with A then digit): \"%s\"\n", e);
	}

	assert (*e == 'A');

	len = strlen(e);

/*
 * special case: 3 digit tactical call.
 */

	if (len == 4 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3])) {
	  strlcpy (m_callsign, e+1, sizeof(m_callsign));
	  if (tt_debug) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Special case, 3 digit tactical call: \"%s\"\n", m_callsign);
	  }
	  return (0);
	}

/* 
 * 3 digit abbreviation:  We only do the parsing here.
 * Another part of application will try to find corresponding full call.
 */

	if ((len == 6 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3]) && isdigit(e[4]) && isdigit(e[5])) ||
	    (len == 7 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3]) && isdigit(e[4]) && isupper(e[5]) && isdigit(e[6]))) {

	  int cs_err = checksum_not_ok (e+1, len-2, e[len-1]);

	  if (cs_err != 0) {
	    return (cs_err);
	  }

	  memcpy (m_callsign, e+1, 3);
	  m_callsign[3] = '\0';
	
	  if (len == 7) {
	    tttemp[0] = e[len-3];
	    tttemp[1] = e[len-2];
	    tttemp[2] = '\0';
	    tt_two_key_to_text (tttemp, 0, stemp);
	    m_symbol_code = APRSTT_DEFAULT_SYMBOL;
	    m_symtab_or_overlay = stemp[0];
	    if (tt_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Three digit abbreviation1: callsign \"%s\", symbol code '%c (Box DTMF)', overlay '%c', checksum %c\n",
				m_callsign, m_symbol_code, m_symtab_or_overlay, e[len-1]);
	    }
	  }
	  else {
	    m_symbol_code = APRSTT_DEFAULT_SYMBOL;
	    m_symtab_or_overlay = e[len-2];
	    if (tt_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Three digit abbreviation2: callsign \"%s\", symbol code '%c' (Box DTMF), overlay '%c', checksum %c\n",
				m_callsign, m_symbol_code, m_symtab_or_overlay, e[len-1]);
	    }
	  }
	  return (0);
	}

/* 
 * Callsign in two key format.
 */

	if (len >= 7 && len <= 24) {

	  int cs_err = checksum_not_ok (e+1, len-2, e[len-1]);

	  if (cs_err != 0) {
	    return (cs_err);
	  }
	
	  if (isupper(e[len-2])) {
	    memcpy (tttemp, e+1, len-4);
	    tttemp[len-4] = '\0';
	    tt_two_key_to_text (tttemp, 0, m_callsign);

	    tttemp[0] = e[len-3];
	    tttemp[1] = e[len-2];
	    tttemp[2] = '\0';
	    tt_two_key_to_text (tttemp, 0, stemp);
	    m_symbol_code = APRSTT_DEFAULT_SYMBOL;
	    m_symtab_or_overlay = stemp[0];
	    if (tt_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Callsign in two key format1: callsign \"%s\", symbol code '%c' (Box DTMF), overlay '%c', checksum %c\n",
				m_callsign, m_symbol_code, m_symtab_or_overlay, e[len-1]);
	    }
	  }
	  else {
	    memcpy (tttemp, e+1, len-3);
	    tttemp[len-3] = '\0';
	    tt_two_key_to_text (tttemp, 0, m_callsign);

	    m_symbol_code = APRSTT_DEFAULT_SYMBOL;
	    m_symtab_or_overlay = e[len-2];
	    if (tt_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Callsign in two key format2: callsign \"%s\", symbol code '%c' (Box DTMF), overlay '%c', checksum %c\n",
				m_callsign, m_symbol_code, m_symtab_or_overlay, e[len-1]);
	    }
	  }
	  return (0);
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Touch tone callsign not valid: \"%s\"\n", e);
	return (TT_ERROR_INVALID_CALL);
}


/*------------------------------------------------------------------
 *
 * Name:        parse_object_name 
 *
 * Purpose:     Extract object name from touch tone sequence.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "AA".
 *
 * Outputs:	m_callsign
 *
 *		m_ssid		- Cleared to remove the default of 12.
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	Data format
 *
 *		AAtt...tt	- Symbol name, two key method, up to 9 characters.
 *
 *----------------------------------------------------------------*/


static int parse_object_name (char *e)
{
	int len;

	if (tt_debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("APRStt parse object name (starts with AA): \"%s\"\n", e);
	}

	assert (e[0] == 'A');
	assert (e[1] == 'A');

	len = strlen(e);

/* 
 * Object name in two key format.
 */

	if (len >= 2 + 1 && len <= 30) {

	  if (tt_two_key_to_text (e+2, 0, m_callsign) == 0) {
	    m_callsign[9] = '\0';  /* truncate to 9 */
	    m_ssid = 0;		/* No ssid for object name */
	    if (tt_debug) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Object name in two key format: \"%s\"\n", m_callsign);
	    }
	    return (0);
	  }
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Touch tone object name not valid: \"%s\"\n", e);

	return (TT_ERROR_INVALID_OBJNAME);

}  /* end parse_oject_name */


/*------------------------------------------------------------------
 *
 * Name:        parse_symbol 
 *
 * Purpose:     Extract symbol from touch tone sequence.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "AB".
 *
 * Outputs:	m_symtab_or_overlay
 *
 * 		m_symbol_code
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	Data format
 *
 *		AB1nn		- Symbol from primary symbol table.  
 *				  Two digits nn are the same as in the GPSCnn 
 *				  generic address used as a destination. 
 *
 *		AB2nn		- Symbol from alternate symbol table.  
 *				  Two digits nn are the same as in the GPSEnn 
 *				  generic address used as a destination. 
 *
 *		AB0nnvv		- Symbol from alternate symbol table.  
 *				  Two digits nn are the same as in the GPSEnn 
 *				  generic address used as a destination.
 *	 			  vv is an overlay digit or letter in two key method.
 *
 *----------------------------------------------------------------*/


static int parse_symbol (char *e)
{
	int len;
	char nstr[3];
	int nn;
	char stemp[10];

	if (tt_debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("APRStt parse symbol (starts with AB): \"%s\"\n", e);
	}

	assert (e[0] == 'A');
	assert (e[1] == 'B');

	len = strlen(e);

	if (len >= 4 && len <= 10) {

	  nstr[0] = e[3];
	  nstr[1] = e[4];
	  nstr[2] = '\0';

	  nn = atoi (nstr);
	  if (nn < 1) {
	    nn = 1;
	  }
	  else if (nn > 94) {
	    nn = 94;
	  }

	  switch (e[2]) {

	    case '1':
	      m_symtab_or_overlay = '/';
	      m_symbol_code = 32 + nn;
	      if (tt_debug) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("symbol code '%c', primary symbol table '%c'\n",
				m_symbol_code, m_symtab_or_overlay);
	      }
	      return (0);
	      break;

	    case '2':
	      m_symtab_or_overlay = '\\';
	      m_symbol_code = 32 + nn;
	      if (tt_debug) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("symbol code '%c', alternate symbol table '%c'\n",
				m_symbol_code, m_symtab_or_overlay);
	      }
	      return (0);
	      break;

	    case '0':
	      if (len >= 6) {
	        if (tt_two_key_to_text (e+5, 0, stemp) == 0) {
	          m_symbol_code = 32 + nn;
	          m_symtab_or_overlay = stemp[0];
	          if (tt_debug) {
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("symbol code '%c', alternate symbol table with overlay '%c'\n",
				m_symbol_code, m_symtab_or_overlay);
	          }
	          return (0);
	        }
	      }
	      break;
	  }
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Touch tone symbol not valid: \"%s\"\n", e);

	return (TT_ERROR_INVALID_SYMBOL);

}  /* end parse_oject_name */


/*------------------------------------------------------------------
 *
 * Name:        parse_aprstt3_call
 *
 * Purpose:     Extract QIKcom-2 / APRStt 3 ten digit call or five digit suffix.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "AC".
 *
 * Outputs:	m_callsign
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	We recognize 3 different formats:
 *
 *		ACxxxxxxxxxx	- 10 digit full callsign.
 *
 *		ACxxxxx		- 5 digit suffix.   If we can find a corresponding full
 *				  callsign, that will be substituted.
 *				  Error condition is returned if we can't find one.
 *
 *----------------------------------------------------------------*/

static int parse_aprstt3_call (char *e)
{

	assert (e[0] == 'A');
	assert (e[1] == 'C');

	if (tt_debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("APRStt parse QIKcom-2 / APRStt 3 ten digit call or five digit suffix (starts with AC): \"%s\"\n", e);
	}

	if (strlen(e) == 2+10) {
	  char call[12];

	  if (tt_call10_to_text(e+2,1,call) == 0) {
	    strlcpy(m_callsign, call, sizeof(m_callsign));
	  }
	  else {
	    return (TT_ERROR_INVALID_CALL);		/* Could not convert to text */
	  }
	}
	else if (strlen(e) == 2+5) {
	  char suffix[8];
          if (tt_call5_suffix_to_text(e+2,1,suffix) == 0) {

#if TT_MAIN
	    /* For unit test, use suffix rather than trying lookup. */
	    strlcpy (m_callsign, suffix, sizeof(m_callsign));
#else
	    char call[12];

	    /* In normal operation, try to find full callsign for the suffix received. */

	    if (tt_3char_suffix_search (suffix, call) >= 0) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Suffix \"%s\" was converted to full callsign \"%s\"\n", suffix, call);

	      strlcpy(m_callsign, call, sizeof(m_callsign));
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Couldn't find full callsign for suffix \"%s\"\n", suffix);
	      return (TT_ERROR_SUFFIX_NO_CALL);	/* Don't know this user. */
	    }
#endif
	  }
	  else {
	    return (TT_ERROR_INVALID_CALL);	/* Could not convert to text */
	  }
	}
	else {
	  return (TT_ERROR_INVALID_CALL);	/* Invalid length, not 2+ (10 ir 5) */
	}

	return (0);

}  /* end parse_aprstt3_call */


/*------------------------------------------------------------------
 *
 * Name:        parse_location
 *
 * Purpose:     Extract location from touch tone sequence.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "B".
 *
 * Outputs:	m_latitude
 *		m_longitude
 *
 *		m_dao		It should previously be "!T  !" to mean unknown or none.
 *				We generally take the first two tones of the field.
 *				For example, "!TB5!" for the standard bearing & range.
 *				The point type is an exception where we use "!Tn !" for
 *				one of ten positions or "!Tnn" for one of a hundred.
 *				If this ever changes, be sure to update corresponding
 *				section in process_comment() in decode_aprs.c
 *
 *		m_ambiguity
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	There are many different formats recognizable
 *		by total number of digits and sometimes the first digit.
 *
 *		We handle most of them in a general way, processing
 *		them in 5 groups:
 *
 *		* points
 *		* vector
 *		* grid
 *		* utm
 *		* usng / mgrs
 *
 *		Position ambiguity is also handled here.
 *			Latitude, Longitude, and DAO should not be touched in this case.
 *		 	We only record a position ambiguity value.
 *
 *----------------------------------------------------------------*/

/* Average radius of earth in meters. */
#define R 6371000.


static int parse_location (char *e)
{
	int ipat;
	char xstr[VALSTRSIZE], ystr[VALSTRSIZE], zstr[VALSTRSIZE], bstr[VALSTRSIZE], dstr[VALSTRSIZE];
	double x, y, dist, bearing;
	double lat0, lon0;
	double lat9, lon9;
	long lerr;	
	double easting, northing;
	char mh[20];	
	char stemp[32];

	if (tt_debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("APRStt parse location (starts with B): \"%s\"\n", e);
	  // TODO: more detail later...
	}

	assert (*e == 'B');


	ipat = find_ttloc_match (e, xstr, ystr, zstr, bstr, dstr, VALSTRSIZE);
	if (ipat >= 0) {

	  //dw_printf ("ipat=%d, x=%s, y=%s, b=%s, d=%s\n", ipat, xstr, ystr, bstr, dstr);

	  switch (tt_config.ttloc_ptr[ipat].type) {
	    case TTLOC_POINT:
		
	      m_latitude = tt_config.ttloc_ptr[ipat].point.lat;
	      m_longitude = tt_config.ttloc_ptr[ipat].point.lon;

	      /* Is it one of ten or a hundred positions? */
	      /* It's not hardwired to always be B0n or B9nn.  */
	      /* This is a pretty good approximation. */

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      if (strlen(e) == 3) {	/* probably B0n -->  !Tn ! */
		m_dao[2] = e[2];
	        m_dao[3] = ' ';
	      }
	      if (strlen(e) == 4) {	/* probably B9nn -->  !Tnn! */
		m_dao[2] = e[2];
	        m_dao[3] = e[3];
	      }
	      break;

	    case TTLOC_VECTOR:

	      if (strlen(bstr) != 3) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Bearing \"%s\" should be 3 digits.\n", bstr);
	        // return error code?
	      }
	      if (strlen(dstr) < 1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Distance \"%s\" should 1 or more digits.\n", dstr);
	        // return error code?
	      }

	      lat0 = D2R(tt_config.ttloc_ptr[ipat].vector.lat);
	      lon0 = D2R(tt_config.ttloc_ptr[ipat].vector.lon);
	      dist = atof(dstr) * tt_config.ttloc_ptr[ipat].vector.scale;
	      bearing = D2R(atof(bstr));

	      /* Equations and caluculators found here: */
	      /* http://movable-type.co.uk/scripts/latlong.html */
	      /* This should probably be a function in latlong.c in case we have another use for it someday. */

	      m_latitude = R2D(asin(sin(lat0) * cos(dist/R) + cos(lat0) * sin(dist/R) * cos(bearing)));

	      m_longitude = R2D(lon0 + atan2(sin(bearing) * sin(dist/R) * cos(lat0),
				  cos(dist/R) - sin(lat0) * sin(D2R(m_latitude))));

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;

	    case TTLOC_GRID:

	      if (strlen(xstr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing X coordinate.\n");
		strlcpy (xstr, "0", sizeof(xstr));
	      }
	      if (strlen(ystr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing Y coordinate.\n");
		strlcpy (ystr, "0", sizeof(ystr));
	      }

	      lat0 = tt_config.ttloc_ptr[ipat].grid.lat0;
	      lat9 = tt_config.ttloc_ptr[ipat].grid.lat9;
	      double yrange = lat9 - lat0;
	      y = atof(ystr);
	      double user_y_max = round(pow(10., strlen(ystr)) - 1.);	// e.g. 999 for 3 digits
	      m_latitude = lat0 + yrange * y / user_y_max;
#if 0
	      dw_printf ("TTLOC_GRID LAT min=%f, max=%f, range=%f\n", lat0, lat9, yrange);
	      dw_printf ("TTLOC_GRID LAT user_y=%f, user_y_max=%f\n", y, user_y_max);
	      dw_printf ("TTLOC_GRID LAT min + yrange * user_y / user_y_range = %f\n", m_latitude);
#endif
	      lon0 = tt_config.ttloc_ptr[ipat].grid.lon0;
	      lon9 = tt_config.ttloc_ptr[ipat].grid.lon9;
	      double xrange = lon9 - lon0;
	      x = atof(xstr);
	      double user_x_max = round(pow(10., strlen(xstr)) - 1.);
	      m_longitude = lon0 + xrange * x / user_x_max;
#if 0
	      dw_printf ("TTLOC_GRID LON min=%f, max=%f, range=%f\n", lon0, lon9, xrange);
	      dw_printf ("TTLOC_GRID LON user_x=%f, user_x_max=%f\n", x, user_x_max);
	      dw_printf ("TTLOC_GRID LON min + xrange * user_x / user_x_range = %f\n", m_longitude);
#endif

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;

	    case TTLOC_UTM:

	      if (strlen(xstr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing X coordinate.\n");
	        /* Avoid divide by zero later.  Put in middle of range. */
		strlcpy (xstr, "5", sizeof(xstr));
	      }
	      if (strlen(ystr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing Y coordinate.\n");
	        /* Avoid divide by zero later.  Put in middle of range. */
		strlcpy (ystr, "5", sizeof(ystr));
	      }

	      x = atof(xstr);
	      easting = x * tt_config.ttloc_ptr[ipat].utm.scale + tt_config.ttloc_ptr[ipat].utm.x_offset;

	      y = atof(ystr);
	      northing = y * tt_config.ttloc_ptr[ipat].utm.scale + tt_config.ttloc_ptr[ipat].utm.y_offset;
	
	      if (isalpha(tt_config.ttloc_ptr[ipat].utm.latband)) {
	        snprintf (m_loc_text, sizeof(m_loc_text), "%d%c %.0f %.0f", (int)(tt_config.ttloc_ptr[ipat].utm.lzone), tt_config.ttloc_ptr[ipat].utm.latband, easting, northing);
	      }
	      else if (tt_config.ttloc_ptr[ipat].utm.latband == '-') {
	        snprintf (m_loc_text, sizeof(m_loc_text), "%d %.0f %.0f", (int)(- tt_config.ttloc_ptr[ipat].utm.lzone), easting, northing);
	      }
	      else {
	        snprintf (m_loc_text, sizeof(m_loc_text), "%d %.0f %.0f", (int)(tt_config.ttloc_ptr[ipat].utm.lzone), easting, northing);
	      }

              lerr = Convert_UTM_To_Geodetic(tt_config.ttloc_ptr[ipat].utm.lzone, 
			tt_config.ttloc_ptr[ipat].utm.hemi, easting, northing, &lat0, &lon0);

              if (lerr == 0) {
                m_latitude = R2D(lat0);
                m_longitude = R2D(lon0);

                //dw_printf ("DEBUG: from UTM, latitude = %.6f, longitude = %.6f\n", m_latitude, m_longitude);
              }
              else {
	        char message[300];

		text_color_set(DW_COLOR_ERROR);
	        utm_error_string (lerr, message);
                dw_printf ("Conversion from UTM failed:\n%s\n\n", message);
              }

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;


	    case TTLOC_MGRS:
	    case TTLOC_USNG:

	      if (strlen(xstr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("MGRS/USNG: Missing X (easting) coordinate.\n");
	        /* Should not be possible to get here. Fake it and carry on. */
		strlcpy (xstr, "5", sizeof(xstr));
	      }
	      if (strlen(ystr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("MGRS/USNG: Missing Y (northing) coordinate.\n");
	        /* Should not be possible to get here. Fake it and carry on. */
		strlcpy (ystr, "5", sizeof(ystr));
	      }

	      char loc[40];
	
	      strlcpy (loc, tt_config.ttloc_ptr[ipat].mgrs.zone, sizeof(loc));
	      strlcat (loc, xstr, sizeof(loc));
	      strlcat (loc, ystr, sizeof(loc));

	      //text_color_set(DW_COLOR_DEBUG);
	      //dw_printf ("MGRS/USNG location debug:  %s\n", loc);

	      strlcpy (m_loc_text, loc, sizeof(m_loc_text));

	      if (tt_config.ttloc_ptr[ipat].type == TTLOC_MGRS)
                lerr = Convert_MGRS_To_Geodetic(loc, &lat0, &lon0);
	      else
                lerr = Convert_USNG_To_Geodetic(loc, &lat0, &lon0);


              if (lerr == 0) {
                m_latitude = R2D(lat0);
                m_longitude = R2D(lon0);

                //dw_printf ("DEBUG: from MGRS/USNG, latitude = %.6f, longitude = %.6f\n", m_latitude, m_longitude);
              }
              else {
	        char message[300];

		text_color_set(DW_COLOR_ERROR);
	        mgrs_error_string (lerr, message);
                dw_printf ("Conversion from MGRS/USNG failed:\n%s\n\n", message);
              }

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;

	    case TTLOC_MHEAD:


	      /* Combine prefix from configuration and digits from user. */
	
  	      strlcpy (stemp, tt_config.ttloc_ptr[ipat].mhead.prefix, sizeof(stemp));
	      strlcat (stemp, xstr, sizeof(stemp));

	      if (strlen(stemp) != 4 && strlen(stemp) != 6 && strlen(stemp) != 10 && strlen(stemp) != 12) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Expected total of 4, 6, 10, or 12 digits for the Maidenhead Locator \"%s\" + \"%s\"\n", 
							tt_config.ttloc_ptr[ipat].mhead.prefix, xstr);
	        return (TT_ERROR_INVALID_MHEAD);
	      }

	      //text_color_set(DW_COLOR_DEBUG);
	      //dw_printf ("Case MHEAD: Convert to text \"%s\".\n", stemp);

	      if (tt_mhead_to_text (stemp, 0, mh, sizeof(mh))  == 0) {
	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("Case MHEAD: Resulting text \"%s\".\n", mh);

		strlcpy (m_loc_text, mh, sizeof(m_loc_text));

		ll_from_grid_square (mh, &m_latitude, &m_longitude);
	      }

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;

	    case TTLOC_SATSQ:

	      if (strlen(xstr) != 4) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Expected 4 digits for the Satellite Square.\n");
	        return (TT_ERROR_INVALID_SATSQ);
	      }

	      /* Convert 4 digits to usual AA99 form, then to location. */

	      if (tt_satsq_to_text (xstr, 0, mh)  == 0) {

	        strlcpy (m_loc_text, mh, sizeof(m_loc_text));

		ll_from_grid_square (mh, &m_latitude, &m_longitude);
	      }

	      m_dao[2] = e[0];
	      m_dao[3] = e[1];

	      break;

	    case TTLOC_AMBIG:

	      if (strlen(xstr) != 1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Expected 1 digits for the position ambiguity.\n");
	        return (TT_ERROR_INVALID_LOC);
	      }

	      m_ambiguity = atoi(xstr);

	      break;

	    default:
	      assert (0);
	  }
	  return (0);
	}

	/* Does not match any location specification. */

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Received location \"%s\" does not match any definitions.\n", e);

	/* Send reject sound. */

	return (TT_ERROR_INVALID_LOC);

} /* end parse_location */


/*------------------------------------------------------------------
 *
 * Name:        find_ttloc_match
 *
 * Purpose:     Try to match the received position report to a pattern
 *		defined in the configuration file.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "B".
 *
 *		valstrsize	- size of the outputs so we can check for buffer overflow.
 *
 * Outputs:	xstr		- All digits matching x positions in configuration.
 *		ystr		-                     y 
 *		zstr		-                     z 
 *		bstr		-                     b
 * 		dstr		-                     d
 *
 * Returns:     >= 0 for index into table if found.
 *		-1 if not found.
 *
 * Description:	
 *
 *----------------------------------------------------------------*/

static int find_ttloc_match (char *e, char *xstr, char *ystr, char *zstr, char *bstr, char *dstr, size_t valstrsize)
{
	int ipat;	/* Index into patterns from configuration file */
	int len;	/* Length of pattern we are trying to match. */
	int match;
	char mc;
	int k;

	// debug dw_printf ("find_ttloc_match: e=%s\n", e);

	for (ipat=0; ipat<tt_config.ttloc_len; ipat++) {
	  
	  len = strlen(tt_config.ttloc_ptr[ipat].pattern);

	  if ((int)(strlen(e)) == len) {

	    match = 1;
	    strlcpy (xstr, "", valstrsize);
	    strlcpy (ystr, "", valstrsize);
	    strlcpy (zstr, "", valstrsize);
	    strlcpy (bstr, "", valstrsize);
	    strlcpy (dstr, "", valstrsize);

	    for (k=0; k<len; k++) {
	      mc = tt_config.ttloc_ptr[ipat].pattern[k];
	      switch (mc) {

	        case 'B':
	        case '0':
	        case '1':
	        case '2':
	        case '3':
	        case '4':
	        case '5':
	        case '6':
	        case '7':
	        case '8':
	        case '9':
	        case 'A':	/* Allow A,C,D after the B? */
	        case 'C':
	        case 'D':

		  if (e[k] != mc) {
		    match = 0;
		  }
		  break;

		case 'x':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strlcat (xstr, stemp, valstrsize);
		   }
		   else {
		     match = 0;
	           }
		  break;

		case 'y':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strlcat (ystr, stemp, valstrsize);
		   }
		   else {
		     match = 0;
	           }
		  break;

		case 'z':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strlcat (zstr, stemp, valstrsize);
		   }
		   else {
		     match = 0;
	           }
		  break;

		case 'b':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strlcat (bstr, stemp, valstrsize);
		   }
		   else {
		     match = 0;
	           }
		  break;

		case 'd':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strlcat (dstr, stemp, valstrsize);
		   }
		   else {
		     match = 0;
	           }
		  break;

		default:
		  dw_printf ("find_ttloc_match: shouldn't be here.\n");
		  /* Shouldn't be here. */
		  match = 0;
		  break;

	      } /* switch */
	    } /* for k */
	  
	    if (match) {
	      return (ipat);
	    }
	  } /* if strlen */
	}
	return (-1);

} /* end find_ttloc_match */


/*------------------------------------------------------------------
 *
 * Name:        parse_comment
 *
 * Purpose:     Extract comment / status or other special information from touch tone message. 
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt message.
 *				  In this case, it should start with "C".
 *
 * Outputs:	m_comment
 *		m_mic_e
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	We recognize these different formats:
 *
 *		Cn		- One digit (1-9) predefined status.  0 is reserved for none.
 *				  The defaults are derived from the MIC-E position comments
 *				  which were always "/" plus exactly 10 characters.
 *				  Users can override the defaults with configuration options.
 *	
 *		Cnnnnnn		- Six digit frequency reformatted as nnn.nnnMHz
 *
 *		Cnnn		- Three digit are for CTCSS tone.  Use only integer part
 *				  and leading 0 if necessary to make exactly 3 digits.
 *
 *		Cttt...tttt	- General comment in Multi-press encoding.
 *
 *		CAttt...tttt	- New enhanced comment format that can handle all ASCII characters.
 *
 *----------------------------------------------------------------*/

static int parse_comment (char *e)
{
	int len;

	assert (*e == 'C');

	len = strlen(e);

	if (e[1] == 'A') {
	  tt_ascii2d_to_text (e+2, 0, m_comment);
	  return (0);
	}

	if (len == 2 && isdigit(e[1])) {
	  m_mic_e = e[1];
	  return (0);
	}

	if (len == 7 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3]) && isdigit(e[4]) && isdigit(e[5]) && isdigit(e[6])) {
	  m_freq[0] = e[1];
	  m_freq[1] = e[2];
	  m_freq[2] = e[3];
	  m_freq[3] = '.';
	  m_freq[4] = e[4];
	  m_freq[5] = e[5];
	  m_freq[6] = e[6];
	  m_freq[7] = 'M';
	  m_freq[8] = 'H';
	  m_freq[9] = 'z';
	  m_freq[10] = '\0';
	  return (0);
	}

	if (len == 4 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3])) {
	  strlcpy (m_ctcss, e+1, sizeof(m_ctcss));
	  return (0);
	}

	tt_multipress_to_text (e+1, 0, m_comment);
	return (0);
}


/*------------------------------------------------------------------
 *
 * Name:        raw_tt_data_to_app
 *
 * Purpose:     Send raw touch tone data to application. 
 *
 * Inputs:      chan		- Channel where touch tone data heard.
 *		msg		- String of button pushes.
 *				  Normally ends with #.
 *
 * Global In:	m_callsign
 *		m_symtab_or_overlay
 *		m_symbol_code
 *
 * Returns:     None
 *
 * Description:	
 * 		Put raw touch tone message in a packet and send to application.
 * 		The APRS protocol does not have provision for this.
 * 		For now, use the unused "t" message type.
 * 		TODO:  Get an officially sanctioned method.
 *
 * 		Use callsign for source if available.
 * 		Encode the overlay in the destination. 
 *
 *----------------------------------------------------------------*/

#ifndef TT_MAIN

static void raw_tt_data_to_app (int chan, char *msg)
{

#if TT_MAIN
	return ;
#else
	char src[10], dest[10];
	char raw_tt_msg[256];
	packet_t pp;
	alevel_t alevel;



// Set source and dest to something valid to keep rest of processing happy.
// For lack of a better idea, make source "DTMF" to indicate where it came from.
// Application version might be useful in case we end up using different
// message formats in later versions.

	strlcpy (src, "DTMF", sizeof(src));
	snprintf (dest, sizeof(dest), "%s%d%d", APP_TOCALL, MAJOR_VERSION, MINOR_VERSION);

	snprintf (raw_tt_msg, sizeof(raw_tt_msg), "%s>%s:t%s", src, dest, msg);

	pp = ax25_from_text (raw_tt_msg, 1);

/*
 * Process like a normal received frame.
 * NOTE:  This goes directly to application rather than
 * thru the multi modem duplicate processing.
 *
 * Should we use a different type so it can be easily
 * distinguished later?
 *
 * We try to capture an overall audio level here.
 * Mark and space do not apply in this case.
 * This currently doesn't get displayed but we might want it someday.
 */

	if (pp != NULL) {

	  alevel = demod_get_audio_level (chan, 0);
	  alevel.mark = -2;
	  alevel.space = -2;

	  dlq_rec_frame (chan, -1, 0, pp, alevel, 0, RETRY_NONE, "tt");
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not convert \"%s\" into APRS packet.\n", raw_tt_msg);
	}

#endif
}

#endif


/*------------------------------------------------------------------
 *
 * Name:        dw_run_cmd
 *
 * Purpose:     Run a command and capture the output. 
 *
 * Inputs:      cmd		- The command.
 *
 *		oneline		- 0 = Keep original line separators. Caller
 *					must deal with operating system differences.
 *				  1 = Change CR, LF, TAB to space so result
 *					is one line of text.
 *				  2 = Also remove any trailing whitespace.
 *
 *		resultsiz	- Amount of space available for result.
 *
 * Outputs:	result		- Output captured from running command.
 *
 * Returns:     -1 for any sort of error.
 *		>0 for number of characters returned (= strlen(result))
 *
 * Description:	This is currently used for running a user-specified
 *		script to generate a custom speech response.
 *
 * Future:	There are potential other uses so it should probably
 *		be relocated to a file of other misc. utilities.
 *
 *----------------------------------------------------------------*/

int dw_run_cmd (char *cmd, int oneline, char *result, size_t resultsiz) 
{
	FILE *fp;

	strlcpy (result, "", resultsiz);

	fp = popen (cmd, "r");
	if (fp != NULL) {
	  int remaining = (int)resultsiz;
	  char *pr = result;
	  int err;

	  while (remaining > 2 && fgets(pr, remaining, fp) != NULL) {
	    pr = result + strlen(result);
	    remaining = (int)resultsiz - strlen(result);
	  }

	  if ((err = pclose(fp)) != 0) {	 
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR: Unable to run \"%s\"\n", cmd);
	    // On Windows, non-existent file produces "Operation not permitted"
	    // Maybe we should put in a test for whether file exists.
	    dw_printf ("%s\n", strerror(err));

	    return (-1);
	  }

	  // take out any newline characters.
	
	  if (oneline) {
	    for (pr = result; *pr != '\0'; pr++) {
	      if (*pr == '\r' || *pr == '\n' || *pr == '\t') {
	        *pr = ' ';
	      }
	    }

	    if (oneline > 1) {
	      pr = result + strlen(result) - 1;
	      while (pr >= result && *pr == ' ') {
	        *pr = '\0';
	        pr--;
	      }
	    }
	  }

	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("%s returns \"%s\"\n", cmd, result);
	  
	  return (strlen(result));
	}

	else {
	  // explain_popen() would be nice but doesn't seem to be commonly available.
	  
	  // We get here only if fork or pipe fails.
	  // The command not existing must be caught above.

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Unable to run \"%s\"\n", cmd);
	  dw_printf ("%s\n", strerror(errno));
	  
	  return (-1);
	}


} /* end dw_run_cmd */


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Unit test for this file.
 *
 * Description:	Run unit test like this:
 *
 *			rm a.exe ; gcc tt_text.c -DTT_MAIN -Igeotranz aprs_tt.c latlong.o textcolor.o geotranz.a misc.a  ; ./a.exe
 *		or
 *			make ttest
 *
 *----------------------------------------------------------------*/


#if TT_MAIN

/*
 * Regression test for the parsing.
 * It does not maintain any history so abbreviation will not invoke previous full call.
 */

/* Some examples are derived from http://www.aprs.org/aprstt/aprstt-coding24.txt */


static const struct {
	char *toneseq;		/* Tone sequence in. */
	
	char *callsign;		/* Expected results... */
	char *ssid;
	char *symbol;
	char *freq;
	char *comment;
	char *lat;
	char *lon;
	char *dao;
} testcases[] = {

  /* Callsigns & abbreviations, traditional */

	{ "A9A2B42A7A7C71#",	"WB4APR", "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" }, 	/* WB4APR/7 */
	{ "A27773#",		"277",    "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" }, 	/* abbreviated form */

	/* Intentionally wrong - Has 6 for checksum when it should be 3. */
	{ "A27776#",		"",       "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* Expect error message. */
	
	/* Example in spec is wrong.  checksum should be 5 in this case. */
	{ "A2A7A7C71#",		"",       "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* Spelled suffix, overlay, checksum */
	{ "A2A7A7C75#",		"APR",    "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* Spelled suffix, overlay, checksum */
	{ "A27773#",		"277",    "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* Suffix digits, overlay, checksum */

	{ "A9A2B26C7D9D71#",	"WB2OSZ", "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* WB2OSZ/7 numeric overlay */
	{ "A67979#",		"679",    "12", "7A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* abbreviated form */

	{ "A9A2B26C7D9D5A9#",	"WB2OSZ", "12", "JA", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* WB2OSZ/J letter overlay */
	{ "A6795A7#",		"679",    "12", "JA", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* abbreviated form */

	{ "A277#",		"277",    "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },	/* Tactical call "277" no overlay and no checksum */

  /* QIKcom-2 style 10 digit call & 5 digit suffix */

	{ "AC9242771558#", 	"WB4APR", "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },
	{ "AC27722#",		"APR",    "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },

  /* Locations */

	{ "B01*A67979#",	"679",    "12", "7A", "", "", "12.2500", "56.2500", "!T1 !" },
	{ "B988*A67979#",	"679",    "12", "7A", "", "", "12.5000", "56.5000", "!T88!" },

	{ "B51000125*A67979#",	"679",    "12", "7A", "", "", "52.7907", "0.8309", "!TB5!" },		/* expect about 52.79  +0.83 */

	{ "B5206070*A67979#",	"679",    "12", "7A", "", "", "37.9137", "-81.1366", "!TB5!" },		/* Try to get from Hilltop Tower to Archery & Target Range. */
													/* Latitude comes out ok, 37.9137 -> 55.82 min. */
													/* Longitude -81.1254 -> 8.20 min */
	{ "B21234*A67979#",	"679",    "12", "7A", "", "", "12.3400", "56.1200", "!TB2!" },

	{ "B533686*A67979#",	"679",    "12", "7A", "", "", "37.9222", "81.1143", "!TB5!" },

// TODO: should test other coordinate systems.

  /* Comments */

	{ "C1",			"",       "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },
	{ "C2",			"",       "12", "\\A", "", "", "-999999.0000", "-999999.0000", "!T  !" },
	{ "C146520",		"",       "12", "\\A", "146.520MHz", "", "-999999.0000", "-999999.0000", "!T  !" },
	{ "C7788444222550227776669660333666990122223333",
	                        "",       "12", "\\A", "", "QUICK BROWN FOX 123", "-999999.0000", "-999999.0000", "!T  !" },
  /* Macros */

	{ "88345",		"BIKE 345", "0", "/b", "", "", "12.5000", "56.5000", "!T88!" },

  /* 10 digit representation for callsign & satellite grid. WB4APR near 39.5, -77   */

	{ "AC9242771558*BA1819", "WB4APR", "12", "\\A", "", "", "39.5000", "-77.0000", "!TBA!" },
	{ "18199242771558",	 "WB4APR", "12", "\\A", "", "", "39.5000", "-77.0000", "!TBA!" },
};


static int test_num;
static int error_count;

static void check_result (void)
{
	char stemp[32];

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("callsign=\"%s\", ssid=%d, symbol=\"%c%c\", freq=\"%s\", comment=\"%s\", lat=%.4f, lon=%.4f, dao=\"%s\"\n", 
		m_callsign, m_ssid, m_symtab_or_overlay, m_symbol_code, m_freq, m_comment, m_latitude, m_longitude, m_dao);


	if (strcmp(m_callsign, testcases[test_num].callsign) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for callsign.\n", testcases[test_num].callsign);
	  error_count++;
	}

	snprintf (stemp, sizeof(stemp), "%d", m_ssid);
	if (strcmp(stemp, testcases[test_num].ssid) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for SSID.\n", testcases[test_num].ssid);
	  error_count++;
	}

	stemp[0] = m_symtab_or_overlay;
	stemp[1] = m_symbol_code;
	stemp[2] = '\0';
	if (strcmp(stemp, testcases[test_num].symbol) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for Symbol.\n", testcases[test_num].symbol);
	  error_count++;
	}

	if (strcmp(m_freq, testcases[test_num].freq) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for Freq.\n", testcases[test_num].freq);
	  error_count++;
	}

	if (strcmp(m_comment, testcases[test_num].comment) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for Comment.\n", testcases[test_num].comment);
	  error_count++;
	}

	snprintf (stemp, sizeof(stemp), "%.4f", m_latitude);
	if (strcmp(stemp, testcases[test_num].lat) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for Latitude.\n", testcases[test_num].lat);
	  error_count++;
	}

	snprintf (stemp, sizeof(stemp), "%.4f", m_longitude);
	if (strcmp(stemp, testcases[test_num].lon) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for Longitude.\n", testcases[test_num].lon);
	  error_count++;
	}

	if (strcmp(m_dao, testcases[test_num].dao) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR: Expected \"%s\" for DAO.\n", testcases[test_num].dao);
	  error_count++;
	}
}


int main (int argc, char *argv[])
{
	aprs_tt_init (NULL, 0);

	error_count = 0;

	for (test_num = 0; test_num < sizeof(testcases) / sizeof(testcases[0]); test_num++) {

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("\nTest case %d: %s\n", test_num, testcases[test_num].toneseq);

	  aprs_tt_sequence (0, testcases[test_num].toneseq);
	}

	if (error_count != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n\nTEST FAILED, Total of %d errors.\n", error_count);
	  return (EXIT_FAILURE);
	}

	text_color_set(DW_COLOR_REC);
	dw_printf ("\n\nAll tests passed.\n");
	return (EXIT_SUCCESS);

}  /* end main */


#endif		

/* end aprs_tt.c */


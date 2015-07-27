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
 * Module:      aprs_tt.c
 *
 * Purpose:   	APRStt gateway.
 *		
 * Description: Transfer touch tone messages into the APRS network.
 *
 * References:	This is based upon APRStt (TM) documents with some
 *		artistic freedom.
 *
 *		http://www.aprs.org/aprstt.html
 *
 *---------------------------------------------------------------*/

#define APRS_TT_C 1


// TODO:  clean up terminolgy.  
// "Message" has a specific meaning in APRS and this is not it.  
// Touch Tone sequence might be appropriate.
// What do we call the parts separated by * key?   Entry?  Field?


#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>


#include <ctype.h>
#include <assert.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"		/* for process_rec_frame */
#include "textcolor.h"
#include "aprs_tt.h"
#include "tt_text.h"
#include "tt_user.h"
#include "symbols.h"
#include "latlong.h"


#if __WIN32__
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

#include "utm/LatLong-UTMconversion.h"


//TODO: #include "tt_user.h"



/*
 * Touch Tone sequences are accumulated here until # terminator found.
 * Kept separate for each audio channel.
 */

#define MAX_MSG_LEN 100

static char msg_str[MAX_CHANS][MAX_MSG_LEN+1];
static int msg_len[MAX_CHANS];

static void aprs_tt_message (int chan, char *msg);
static int parse_fields (char *msg);
static int parse_callsign (char *e);
static int parse_object_name (char *e);
static int parse_symbol (char *e);
static int parse_location (char *e);
static int parse_comment (char *e);
static int expand_macro (char *e);
static void raw_tt_data_to_app (int chan, char *msg);
static int find_ttloc_match (char *e, char *xstr, char *ystr, char *zstr, char *bstr, char *dstr);




/*------------------------------------------------------------------
 *
 * Name:        aprs_tt_init
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

	{ TTLOC_MACRO, "xxyyy", .macro.definition = "B9xx*AB166*AA2B4C5B3B0Ayyy" },
}; 
#endif


void aprs_tt_init (struct tt_config_s *p)
{
	int c;

#if TT_MAIN
	/* For unit testing. */

	memset (&tt_config, 0, sizeof(struct tt_config_s));	
	tt_config.ttloc_size = NUM_TEST_CONFIG;
	tt_config.ttloc_ptr = test_config;
	tt_config.ttloc_len = NUM_TEST_CONFIG;

	/* Don't care about xmit timing or corral here. */
#else
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
 *		A partially accumulated messge is discarded if
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

/* Process complete message. */
	
	    aprs_tt_message (chan, msg_str[chan]);
	    msg_len[chan] = 0;
	    msg_str[chan][0] = '\0';
	  }
	}
	else {

/* Idle time. Poll occasionally for processing. */
	  
	  poll_period++;
	  if (poll_period >= 39) {
	    poll_period = 0;
	    tt_user_background ();
	  }
	}	
  
} /* end aprs_tt_button */

#endif

/*------------------------------------------------------------------
 *
 * Name:        aprs_tt_message
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
 * Description:	Process a complete message.
 *		It should have one or more fields separatedy by *
 *		and terminated by a final # like these:
 *
 *		callsign #
 *		entry1 * callsign #
 *		entry1 * entry * callsign #
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
static char m_symbol_code;

static double m_longitude;
static double m_latitude;
static char m_comment[200];
static char m_freq[12];
static char m_mic_e;
static char m_dao[6];
static int m_ssid;

//#define G_UNKNOWN -999999



void aprs_tt_message (int chan, char *msg)
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
 * This takes the place of the usual line with audio level.
 * Do it here, rather than in process_rec_frame, so any
 * error messages are associated with the DTMF message
 * rather than the most recent regular AX.25 frame.
 */

#ifndef TT_MAIN
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\nDTMF message\n");
#endif

/*
 * The parse functions will fill these in. 
 */
	strcpy (m_callsign, "");
	m_symtab_or_overlay = '\\';
	m_symbol_code = 'A';
	m_longitude = G_UNKNOWN; 
	m_latitude = G_UNKNOWN; 
	strcpy (m_comment, "");
	strcpy (m_freq, "");
	m_mic_e = ' ';
	strcpy (m_dao, "!T  !");	/* start out unknown */
	m_ssid = 12;

/* 
 * Send raw touch tone data to application. 
 */
	raw_tt_data_to_app (chan, msg);

/*
 * Parse the touch tone sequence.
 */
	err = parse_fields (msg);

#if defined(DEBUG) || defined(TT_MAIN)
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("callsign=\"%s\", ssid=%d, symbol=\"%c%c\", freq=\"%s\", comment=\"%s\", lat=%.4f, lon=%.4f, dao=\"%s\"\n", 
		m_callsign, m_ssid, m_symtab_or_overlay, m_symbol_code, m_freq, m_comment, m_latitude, m_longitude, m_dao);
#endif


	if (err == 0) {

/*
 * Digested successfully.  Add to our list of users and schedule transmissions.
 */

#ifndef TT_MAIN
	  err = tt_user_heard (m_callsign, m_ssid, m_symtab_or_overlay, m_symbol_code, m_latitude, m_longitude,
		m_freq, m_comment, m_mic_e, m_dao);
#endif
	}

	// TODO send response to user.
	// err == 0 OK, others, suitable error response.


} /* end aprs_tt_message */


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
 * Description:	It should have one or more fields separatedy by *.
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

	strcpy (stemp, msg);
	e = strtok_r (stemp, "*#", &save);
	while (e != NULL) {

	  switch (*e) {

	    case 'A': 
	      
	      switch (e[1]) {
	        case 'A':
	          parse_object_name (e);
	          break;
	        case 'B':
	          parse_symbol (e);
	          break;
	        default:
	          parse_callsign (e);
	          break;
	      }
	      break;

	    case 'B': 
	      parse_location (e);
	      break;

	    case 'C': 
	      parse_comment (e);
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
	      expand_macro (e);
	      break;

	    case '\0':
	      /* Empty field.  Just ignore it. */
	      /* This would happen if someone uses a leading *. */
	      break;

	    default:

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Entry does not start with A, B, C, or digit: \"%s\"\n", msg);
	      return (TT_ERROR_D_MSG);

	  }
	
	  e = strtok_r (NULL, "*#", &save);
	}

	return (0);

} /* end parse_fields */


/*------------------------------------------------------------------
 *
 * Name:        expand_macro
 *
 * Purpose:     Expand compact form "macro" to full format then process.
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt messsage.
 *				  In this case, it should contain only digits.
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	Separate out the fields, perform substitution, 
 *		call parse_fields for processing.
 *
 *----------------------------------------------------------------*/

static int expand_macro (char *e) 
{
	int len;
	int ipat;
	char xstr[20], ystr[20], zstr[20], bstr[20], dstr[20];
	char stemp[MAX_MSG_LEN+1];
	char *d, *s;


	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("Macro tone sequence: '%s'\n", e);

	len = strlen(e);

	ipat = find_ttloc_match (e, xstr, ystr, zstr, bstr, dstr);

	if (ipat >= 0) {

	  dw_printf ("Matched pattern %3d: '%s', x=%s, y=%s, z=%s, b=%s, d=%s\n", ipat, tt_config.ttloc_ptr[ipat].pattern, xstr, ystr, zstr, bstr, dstr);

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
	  
	  strcpy (stemp, "");

	  for (d = tt_config.ttloc_ptr[ipat].macro.definition; *d != '\0'; d++) {

	    while (( *d == 'x' || *d == 'y' || *d == 'z') && *d == d[1]) {
	      /* Collapse adjacent matching substitution characters. */
	      d++;
	    }

	    switch (*d) {
	      case 'x':
		strcat (stemp, xstr);
	        break;
	      case 'y':
		strcat (stemp, ystr);
	        break;
	      case 'z':
		strcat (stemp, zstr);
	        break;
	      default:
		{
	        char c1[2];
	        c1[0] = *d;
	        c1[1] = '\0';
	        strcat (stemp, c1); 
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
 * Purpose:     Extract callsign or object name from touch tone message. 
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt messsage.
 *				  In this case, it should start with "A".
 *
 * Outputs:	m_callsign
 *
 *		m_symtab_or_overlay - Set to 0-9 or A-Z if specified.
 *
 *		m_symbol_code	- Always set to 'A'.
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
	int c_length;
	char tttemp[40], stemp[30];

	assert (*e == 'A');

	len = strlen(e);

/*
 * special case: 3 digit tactical call.
 */

	if (len == 4 && isdigit(e[1]) && isdigit(e[2]) && isdigit(e[3])) {
	  strcpy (m_callsign, e+1);
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

	  strncpy (m_callsign, e+1, 3);
	  m_callsign[3] = '\0';
	
	  if (len == 7) {
	    tttemp[0] = e[len-3];
	    tttemp[1] = e[len-2];
	    tttemp[2] = '\0';
	    tt_two_key_to_text (tttemp, 0, stemp);
	    m_symbol_code = 'A';
	    m_symtab_or_overlay = stemp[0];
	  }
	  else {
	    m_symbol_code = 'A';
	    m_symtab_or_overlay = e[len-2];
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
	    strncpy (tttemp, e+1, len-4);
	    tttemp[len-4] = '\0';
	    tt_two_key_to_text (tttemp, 0, m_callsign);

	    tttemp[0] = e[len-3];
	    tttemp[1] = e[len-2];
	    tttemp[2] = '\0';
	    tt_two_key_to_text (tttemp, 0, stemp);
	    m_symbol_code = 'A';
	    m_symtab_or_overlay = stemp[0];
	  }
	  else {
	    strncpy (tttemp, e+1, len-3);
	    tttemp[len-3] = '\0';
	    tt_two_key_to_text (tttemp, 0, m_callsign);

	    m_symbol_code = 'A';
	    m_symtab_or_overlay = e[len-2];
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
 * Purpose:     Extract object name from touch tone message. 
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt messsage.
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
	int c_length;
	char tttemp[40], stemp[30];

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
 * Purpose:     Extract symbol from touch tone message. 
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt messsage.
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
	char nstr[4];
	int nn;
	char stemp[10];

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
	      return (0);
	      break;

	    case '2':
	      m_symtab_or_overlay = '\\';
	      m_symbol_code = 32 + nn;
	      return (0);
	      break;

	    case '0':
	      if (len >= 6) {
	        if (tt_two_key_to_text (e+5, 0, stemp) == 0) {
	          m_symbol_code = 32 + nn;
	          m_symtab_or_overlay = stemp[0];
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
 * Name:        parse_location
 *
 * Purpose:     Extract location from touch tone message. 
 *
 * Inputs:      e		- An "entry" extracted from a complete
 *				  APRStt messsage.
 *				  In this case, it should start with "B".
 *
 * Outputs:	m_latitude
 *		m_longitude
 *		m_dao
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	There are many different formats recognizable
 *		by total number of digits and sometimes the first digit.
 *
 *		We handle most of them in a general way, processing
 *		them in 4 groups:
 *
 *		* points
 *		* vector
 *		* grid
 *		* utm
 *
 *----------------------------------------------------------------*/



/* Average radius of earth in meters. */
#define R 6371000.

/* Convert between degrees and radians. */

#define D2R(a) ((a) * 2. * M_PI / 360.)
#define R2D(a) ((a) * 360. / (2*M_PI))


static int parse_location (char *e)
{
	int len;
	int ipat;
	char xstr[20], ystr[20], zstr[20], bstr[20], dstr[20];
	double x, y, dist, bearing;
	double lat0, lon0;
	double lat9, lon9;	
	double easting, northing;	

	assert (*e == 'B');

	m_dao[2] = e[0];
	m_dao[3] = e[1];	/* Type of location.  e.g.  !TB6! */
				/* Will be changed by point types. */

				/* If this ever changes, be sure to update corresponding */
				/* section in process_comment() in decode_aprs.c */

	len = strlen(e);

	ipat = find_ttloc_match (e, xstr, ystr, zstr, bstr, dstr);
	if (ipat >= 0) {

	  //dw_printf ("ipat=%d, x=%s, y=%s, b=%s, d=%s\n", ipat, xstr, ystr, bstr, dstr);

	  switch (tt_config.ttloc_ptr[ipat].type) {
	    case TTLOC_POINT:
		
	      m_latitude = tt_config.ttloc_ptr[ipat].point.lat;
	      m_longitude = tt_config.ttloc_ptr[ipat].point.lon;

	      /* Is it one of ten or a hundred positions? */
	      /* It's not hardwired to always be B0n or B9nn.  */
	      /* This is a pretty good approximation. */

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

	      m_latitude = R2D(asin(sin(lat0) * cos(dist/R) + cos(lat0) * sin(dist/R) * cos(bearing)));

	      m_longitude = R2D(lon0 + atan2(sin(bearing) * sin(dist/R) * cos(lat0),
				  cos(dist/R) - sin(lat0) * sin(D2R(m_latitude))));
	      break;

	    case TTLOC_GRID:

	      if (strlen(xstr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing X coordinate.\n");
		strcpy (xstr, "0");
	      }
	      if (strlen(ystr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing Y coordinate.\n");
		strcpy (ystr, "0");
	      }

	      lat0 = tt_config.ttloc_ptr[ipat].grid.lat0;
	      lat9 = tt_config.ttloc_ptr[ipat].grid.lat9;
	      y = atof(ystr);
	      m_latitude = lat0 + y * (lat9-lat0) / (pow(10., strlen(ystr)) - 1.);

	      lon0 = tt_config.ttloc_ptr[ipat].grid.lon0;
	      lon9 = tt_config.ttloc_ptr[ipat].grid.lon9;
	      x = atof(xstr);
	      m_longitude = lon0 + x * (lon9-lon0) / (pow(10., strlen(xstr)) - 1.);

	      break;

	    case TTLOC_UTM:

	      if (strlen(xstr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing X coordinate.\n");
	        /* Avoid divide by zero later.  Put in middle of range. */
		strcpy (xstr, "5");
	      }
	      if (strlen(ystr) == 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Missing Y coordinate.\n");
	        /* Avoid divide by zero later.  Put in middle of range. */
		strcpy (ystr, "5");
	      }

	      x = atof(xstr);
	      easting = x * tt_config.ttloc_ptr[ipat].utm.scale + tt_config.ttloc_ptr[ipat].utm.x_offset;

	      y = atof(ystr);
	      northing = y * tt_config.ttloc_ptr[ipat].utm.scale + tt_config.ttloc_ptr[ipat].utm.y_offset;

	      UTMtoLL (WSG84, northing, easting, tt_config.ttloc_ptr[ipat].utm.zone, 
					&m_latitude, &m_longitude);
	      break;

	    default:
	      assert (0);
	  }
	  return (0);
	}

	/* Send reject sound. */
	/* Does not match any location specification. */

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
 *				  APRStt messsage.
 *				  In this case, it should start with "B".
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

static int find_ttloc_match (char *e, char *xstr, char *ystr, char *zstr, char *bstr, char *dstr)
{
	int ipat;	/* Index into patterns from configuration file */
	int len;	/* Length of pattern we are trying to match. */
	int match;
	char mc;
	int k;

//TODO: remove dw_printf ("find_ttloc_match: e=%s\n", e);

	for (ipat=0; ipat<tt_config.ttloc_len; ipat++) {
	  
	  len = strlen(tt_config.ttloc_ptr[ipat].pattern);

	  if (strlen(e) == len) {

	    match = 1;
	    strcpy (xstr, "");
	    strcpy (ystr, "");
	    strcpy (zstr, "");
	    strcpy (bstr, "");
	    strcpy (dstr, "");

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
		  if (e[k] != mc) {
		    match = 0;
		  }
		  break;

		case 'x':
		   if (isdigit(e[k])) {
	             char stemp[2];
		     stemp[0] = e[k];
		     stemp[1] = '\0';
		     strcat (xstr, stemp);
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
		     strcat (ystr, stemp);
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
		     strcat (zstr, stemp);
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
		     strcat (bstr, stemp);
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
		     strcat (dstr, stemp);
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
 *				  APRStt messsage.
 *				  In this case, it should start with "C".
 *
 * Outputs:	m_comment
 *		m_mic_e
 *
 * Returns:	0 for success or one of the TT_ERROR_... codes.
 *
 * Description:	We recognize these different formats:
 *
 *		Cn		- One digit for MIC-E position comment.
 *				  Always / plus exactly 10 characters.
 *	
 *		Cnnnnnn		- Six digit frequency reformatted as nnn.nnnMHz
 *
 *		Cttt...tttt	- General comment in Multi-press encoding.
 *
 *----------------------------------------------------------------*/

static int parse_comment (char *e)
{
	int len;
	int n;

	assert (*e == 'C');

	len = strlen(e);

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


static void raw_tt_data_to_app (int chan, char *msg)
{

#if TT_MAIN
	return ;
#else
	char src[10], dest[10];
	char raw_tt_msg[200];
	packet_t pp;
	char *c, *s;
	int i;
	int err;

/* 
 * "callsign" could be a general object name up to
 * 9 characters and a space which will get ax25_from_text upset.
 */

	strcpy (src, "");
	if (strlen(m_callsign) > 0) {
	  for (c=m_callsign, s=src; *c != '\0' && strlen(src) < 6; c++) {
	    if (isupper(*c) || isdigit(*c)) {
	      *s++ = *c;
	      *s = '\0';
	    }
	  }
	}
	else {
	  strcpy (src, "APRSTT");
	}


	// TODO: test this.

	err= symbols_into_dest (m_symtab_or_overlay, m_symbol_code, dest);
	if (err) {
	  /* Error message was already printed. */
	  /* Set reasonable default rather than keeping "GPS???" which */
	  /* is invalid and causes trouble later. */
	
	  strcpy (dest, "GPSAA");    
	}

	sprintf (raw_tt_msg, "%s>%s:t%s", src, dest, msg);

	pp = ax25_from_text (raw_tt_msg, 1);

/*
 * Process like a normal received frame.
 * NOTE:  This goes directly to application rather than
 * thru the multi modem duplicate processing.
 */

	if (pp != NULL) {
	  app_process_rec_packet (chan, -1, pp, -2, RETRY_NONE, "tt");
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not convert \"%s\" into APRS packet.\n", raw_tt_msg);
	}

#endif
}



/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Unit test for this file.
 *
 * Description:	Run unit test like this:
 *
 *		rm a.exe ; gcc tt_text.c -DTT_MAIN -DDEBUG aprs_tt.c strtok_r.o utm/LatLong-UTMconversion.c ; ./a.exe 
 *
 *
 * Bugs:	No automatic checking.
 *		Just eyeball it to see if things look right.
 *
 *----------------------------------------------------------------*/


#if TT_MAIN


void text_color_set (dw_color_t c) { return; }

int dw_printf (const char *fmt, ...) 
{
	va_list args;
	int len;
	
	va_start (args, fmt);
	len = vprintf (fmt, args);
	va_end (args);
	return (len);
}



int main (int argc, char *argv[])
{
	char text[256], buttons[256];
	int n;

	dw_printf ("Hello, world!\n");

	aprs_tt_init (NULL);

	//if (argc < 2) {
	  //dw_printf ("Supply text string on command line.\n");
	  //exit (1);
	//}

/* Callsigns & abbreviations. */

	aprs_tt_message (0, "A9A2B42A7A7C71#");		/* WB4APR/7 */
	aprs_tt_message (0, "A27773#");			/* abbreviated form */
	/* Example in http://www.aprs.org/aprstt/aprstt-coding24.txt has a bad checksum! */
	aprs_tt_message (0, "A27776#");			/* Expect error message. */

	aprs_tt_message (0, "A2A7A7C71#");		/* Spelled suffix, overlay, checksum */
	aprs_tt_message (0, "A27773#");			/* Suffix digits, overlay, checksum */

	aprs_tt_message (0, "A9A2B26C7D9D71#");		/* WB2OSZ/7 numeric overlay */
	aprs_tt_message (0, "A67979#");			/* abbreviated form */

	aprs_tt_message (0, "A9A2B26C7D9D5A9#");	/* WB2OSZ/J letter overlay */
	aprs_tt_message (0, "A6795A7#");		/* abbreviated form */

	aprs_tt_message (0, "A277#");			/* Tactical call "277" no overlay and no checksum */

/* Locations */

	aprs_tt_message (0, "B01*A67979#"); 
	aprs_tt_message (0, "B988*A67979#"); 

	/* expect about 52.79  +0.83 */
	aprs_tt_message (0, "B51000125*A67979#"); 

	/* Try to get from Hilltop Tower to Archery & Target Range. */
	/* Latitude comes out ok, 37.9137 -> 55.82 min. */
	/* Longitude -81.1254 -> 8.20 min */

	aprs_tt_message (0, "B5206070*A67979#"); 

	aprs_tt_message (0, "B21234*A67979#"); 
	aprs_tt_message (0, "B533686*A67979#"); 


/* Comments */

	aprs_tt_message (0, "C1");
	aprs_tt_message (0, "C2");
	aprs_tt_message (0, "C146520");
	aprs_tt_message (0, "C7788444222550227776669660333666990122223333");

/* Macros */

	aprs_tt_message (0, "88345");

	return(0);

}  /* end main */




#endif		

/* end aprs_tt.c */


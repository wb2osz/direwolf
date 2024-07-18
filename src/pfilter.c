//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2015, 2016, 2023  John Langner, WB2OSZ
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
 * Module:      pfilter.c
 *
 * Purpose:   	Packet filtering based on characteristics.
 *		
 * Description:	Sometimes it is desirable to digipeat or drop packets based on rules.
 *		For example, you might want to pass only weather information thru
 *		a cross band digipeater or you might want to drop all packets from
 *		an abusive user that is overloading the channel.
 *
 *		The filter specifications are loosely modeled after the IGate Server-side Filter
 *		Commands:   http://www.aprs-is.net/javaprsfilter.aspx
 *
 *		We add AND, OR, NOT, and ( ) to allow very flexible control.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "ax25_pad.h"
#include "textcolor.h"
#include "decode_aprs.h"
#include "latlong.h"
#include "pfilter.h"
#include "mheard.h"



/*
 * Global stuff (to this file)
 *
 * These are set by init function.
 */

static struct igate_config_s	*save_igate_config_p;
static int 			s_debug = 0;



/*-------------------------------------------------------------------
 *
 * Name:        pfilter_init
 *
 * Purpose:     One time initialization when main application starts up.
 *
 * Inputs:	p_igate_config	- IGate configuration.
 *  
 *		debug_level	- 0	no debug output.
 *				  1	single summary line with final result. Indent by 1.
 *				  2	details from each filter specification.  Indent by 3.
 *				  3	Logical operators.  Indent by 2.
 *
 *--------------------------------------------------------------------*/


void pfilter_init (struct igate_config_s *p_igate_config, int debug_level)
{
	s_debug = debug_level;
	save_igate_config_p = p_igate_config;
}




typedef enum token_type_e { TOKEN_AND, TOKEN_OR, TOKEN_NOT, TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_FILTER_SPEC, TOKEN_EOL } token_type_t;


#define MAX_FILTER_LEN 1024
#define MAX_TOKEN_LEN 1024

typedef struct pfstate_s {

	int from_chan;				/* From and to channels.   MAX_TOTAL_CHANS is used for IGate. */
	int to_chan;				/* Used only for debug and error messages. */

/*
 * Original filter string from config file.
 * All control characters should be replaced by spaces.
 */
	char filter_str[MAX_FILTER_LEN];
	int nexti;				/* Next available character index. */

/*
 * Packet object.
 */
	packet_t pp;

/*
 * Are we processing APRS or connected mode?
 * This determines which types of filters are available.
 */
	int is_aprs;

/*
 * Packet split into separate parts if APRS.
 * Most interesting fields are:
 *
 *		g_symbol_table	- / \ or overlay
 *		g_symbol_code
 *		g_lat, g_lon	- Location
 *		g_name		- for object or item
 *		g_comment
 */
	decode_aprs_t decoded;

/*
 * These are set by next_token.
 */
	token_type_t token_type;
	char token_str[MAX_TOKEN_LEN];		/* Printable string representation for use in error messages. */
	int tokeni;				/* Index in original string for enhanced error messages. */

} pfstate_t;



static int parse_expr (pfstate_t *pf);
static int parse_or_expr (pfstate_t *pf);
static int parse_and_expr (pfstate_t *pf);
static int parse_primary (pfstate_t *pf);
static int parse_filter_spec (pfstate_t *pf);

static void next_token (pfstate_t *pf);
static void print_error (pfstate_t *pf, char *msg);

static int filt_bodgu (pfstate_t *pf, char *pattern);
static int filt_t (pfstate_t *pf);
static int filt_r (pfstate_t *pf, char *sdist);
static int filt_s (pfstate_t *pf);
static int filt_i (pfstate_t *pf);

static char *bool2text (int val)
{
	if (val == 1) return "TRUE";
	if (val == 0) return "FALSE";
	if (val == -1) return "ERROR";
	return "OOPS!";
}


/*-------------------------------------------------------------------
 *
 * Name:        pfilter.c
 *
 * Purpose:     Decide whether a packet should be allowed thru.
 *
 * Inputs:	from_chan - Channel packet is coming from.  
 *		to_chan	  - Channel packet is going to.
 *				Both are 0 .. MAX_TOTAL_CHANS-1 or MAX_TOTAL_CHANS for IGate.
 *			 	For debug/error messages only.
 *
 *		filter	- String of filter specs and logical operators to combine them.
 *
 *		pp	- Packet object handle.
 *
 *		is_aprs	- True for APRS, false for connected mode digipeater.
 *			  Connected mode allows a subset of the filter types, only
 *			  looking at the addresses, not information part contents.
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	This might be running in multiple threads at the same time so
 *		no static data allowed and take other thread-safe precautions.
 *
 *--------------------------------------------------------------------*/

int pfilter (int from_chan, int to_chan, char *filter, packet_t pp, int is_aprs)
{
	pfstate_t pfstate;
	char *p;
	int result;

	assert (from_chan >= 0 && from_chan <= MAX_TOTAL_CHANS);
	assert (to_chan >= 0 && to_chan <= MAX_TOTAL_CHANS);

	memset (&pfstate, 0, sizeof(pfstate));

	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR in pfilter: NULL packet pointer. Please report this!\n");
	  return (-1);
	}
	if (filter == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR in pfilter: NULL filter string pointer. Please report this!\n");
	  return (-1);
	}

	pfstate.from_chan = from_chan;
	pfstate.to_chan = to_chan;

	/* Copy filter string, changing any control characters to spaces. */

	strlcpy (pfstate.filter_str, filter, sizeof(pfstate.filter_str));

	pfstate.nexti = 0;
	for (p = pfstate.filter_str; *p != '\0'; p++) {
	  if (iscntrl(*p)) {
	    *p = ' ';
	  }
	}

	pfstate.pp = pp;
	pfstate.is_aprs = is_aprs;

	if (is_aprs) {
	  decode_aprs (&pfstate.decoded, pp, 1, NULL);
	}

	next_token(&pfstate);
	
	if (pfstate.token_type == TOKEN_EOL) {
	  /* Empty filter means reject all. */
	  result = 0;
	}
	else {
	  result = parse_expr (&pfstate);

	  if (pfstate.token_type != TOKEN_AND && 
		pfstate.token_type != TOKEN_OR && 
		pfstate.token_type != TOKEN_EOL) {

	    print_error (&pfstate, "Expected logical operator or end of line here.");
	    result = -1;
	  }
	}

	if (s_debug >= 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  if (from_chan == MAX_TOTAL_CHANS) {
	    dw_printf (" Packet filter from IGate to radio channel %d returns %s\n", to_chan, bool2text(result));
	  }
	  else if (to_chan == MAX_TOTAL_CHANS) {
	    dw_printf (" Packet filter from radio channel %d to IGate returns %s\n", from_chan, bool2text(result));
	  }
	  else if (is_aprs) {
	    dw_printf (" Packet filter for APRS digipeater from radio channel %d to %d returns %s\n", from_chan, to_chan, bool2text(result));
	  }
	  else {
	    dw_printf (" Packet filter for traditional digipeater from radio channel %d to %d returns %s\n", from_chan, to_chan, bool2text(result));
	  }
	}

	return (result);

} /* end pfilter */



/*-------------------------------------------------------------------
 *
 * Name:   	next_token     
 *
 * Purpose:     Extract the next token from input string.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *
 * Outputs:	See definition of the structure.
 *
 * Description:	Look for these special operators:   & | ! ( ) end-of-line
 *		Anything else is considered a filter specification.
 *		Note that a filter-spec must be followed by space or 
 *		end of line.  This is so the magic characters can appear in one.
 *
 * Future:	Maybe allow words like 'OR' as alternatives to symbols like '|'.
 *
 * Unresolved Issue:
 *
 *		Adding the special operators adds a new complication.
 *		How do we handle the case where we want those characters in
 *		a filter specification?   For example how do we know if the
 *		last character of /#& means HF gateway or AND the next part
 *		of the expression.
 *		
 *		Approach 1:  Require white space after all filter specifications.
 *			     Currently implemented.
 *			     Simple. Easy to explain. 
 *			     More readable than having everything squashed together.
 *		
 *		Approach 2:  Use escape character to get literal value.  e.g.  s/#\&
 *			     Linux people would be comfortable with this but 
 *			     others might have a problem with it.
 *		
 *		Approach 3:  use quotation marks if it contains special characters or space.
 *			     "s/#&"  Simple.  Allows embedded space but I'm not sure
 *		 	     that's useful.  Doesn't hurt to always put the quotes there
 *			     if you can't remember which characters are special.
 *
 *--------------------------------------------------------------------*/

static void next_token (pfstate_t *pf) 
{
	while (pf->filter_str[pf->nexti] ==  ' ') {
	  pf->nexti++;
	}

	pf->tokeni = pf->nexti;

	if (pf->filter_str[pf->nexti] == '\0') {
	  pf->token_type = TOKEN_EOL;
	  strlcpy (pf->token_str, "end-of-line", sizeof(pf->token_str));
	}
	else if (pf->filter_str[pf->nexti] == '&') {
	  pf->nexti++;
	  pf->token_type = TOKEN_AND;
	  strlcpy (pf->token_str, "\"&\"", sizeof(pf->token_str));
	}
	else if (pf->filter_str[pf->nexti] == '|') {
	  pf->nexti++;
	  pf->token_type = TOKEN_OR;
	  strlcpy (pf->token_str, "\"|\"", sizeof(pf->token_str));
	}
	else if (pf->filter_str[pf->nexti] == '!') {
	  pf->nexti++;
	  pf->token_type = TOKEN_NOT;
	  strlcpy (pf->token_str, "\"!\"", sizeof(pf->token_str));
	}
	else if (pf->filter_str[pf->nexti] == '(') {
	  pf->nexti++;
	  pf->token_type = TOKEN_LPAREN;
	  strlcpy (pf->token_str, "\"(\"", sizeof(pf->token_str));
	}
	else if (pf->filter_str[pf->nexti] == ')') {
	  pf->nexti++;
	  pf->token_type = TOKEN_RPAREN;
	  strlcpy (pf->token_str, "\")\"", sizeof(pf->token_str));
	}
	else {
	  char *p = pf->token_str;
	  pf->token_type = TOKEN_FILTER_SPEC;
	  do {
	    *p++ = pf->filter_str[pf->nexti++];
	  } while (pf->filter_str[pf->nexti] != ' ' && pf->filter_str[pf->nexti] != '\0');
	  *p = '\0';
	}

} /* end next_token */


/*-------------------------------------------------------------------
 *
 * Name:   	parse_expr
 *		parse_or_expr
 *		parse_and_expr
 *		parse_primary
 *    
 * Purpose:     Recursive descent parser to evaluate filter specifications
 *		contained within expressions with & | ! ( ).
 *
 * Inputs:	pf	- Pointer to current state information.	
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 *--------------------------------------------------------------------*/


static int parse_expr (pfstate_t *pf)
{
	int result;

	result = parse_or_expr (pf);

	return (result);
}

/* or_expr::	and_expr [ | and_expr ] ... */

static int parse_or_expr (pfstate_t *pf)
{
	int result;

	result = parse_and_expr (pf);
	if (result < 0) return (-1);
	
	while (pf->token_type == TOKEN_OR) {
	  int e;

	  next_token (pf);
	  e = parse_and_expr (pf);

	  if (s_debug >= 3) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("  %s | %s\n", bool2text(result), bool2text(e));
	  }

	  if (e < 0) return (-1);
	  result |= e;
	}

	return (result);
}

/* and_expr::	primary [ & primary ] ... */
 
static int parse_and_expr (pfstate_t *pf)
{
	int result;

	result = parse_primary (pf);
	if (result < 0) return (-1);

	while (pf->token_type == TOKEN_AND) {
	  int e;

	  next_token (pf);
	  e = parse_primary (pf);

	  if (s_debug >= 3) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("  %s & %s\n", bool2text(result), bool2text(e));
	  }

	  if (e < 0) return (-1);
	  result &= e;
	}

	return (result);
}

/* primary::	( expr )	*/
/* 		! primary	*/
/*		filter_spec	*/

static int parse_primary (pfstate_t *pf)
{
	int result;

	if (pf->token_type == TOKEN_LPAREN) {

	  next_token (pf);
	  result = parse_expr (pf);
	  	  
	  if (pf->token_type == TOKEN_RPAREN) {
	    next_token (pf);
	  }
	  else {
	    print_error (pf, "Expected \")\" here.\n");
	    result = -1;
	  }
	}
	else if (pf->token_type == TOKEN_NOT) {
	  int e;

	  next_token (pf);
	  e = parse_primary (pf);

	  if (s_debug >= 3) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("  ! %s\n", bool2text(e));
	  }

	  if (e < 0) result = -1;
	  else result = ! e;
	}
	else if (pf->token_type == TOKEN_FILTER_SPEC) {
	  result = parse_filter_spec (pf);
	}
	else {
	  print_error (pf, "Expected filter specification, (, or ! here.");
	  result = -1;
	}

	return (result);
}

/*-------------------------------------------------------------------
 *
 * Name:   	parse_filter_spec
 *    
 * Purpose:     Parse and evaluate filter specification.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	All filter specifications are allowed for APRS.
 *		Only those dealing with addresses are allowed for connected digipeater.
 *
 *		b	- budlist (source)
 *		d	- digipeaters used
 *		v	- digipeaters not used
 *		u	- unproto (destination)
 *
 *--------------------------------------------------------------------*/

static int parse_filter_spec (pfstate_t *pf)
{
	int result = -1;


	if ( ( ! pf->is_aprs) && strchr ("01bdvu", pf->token_str[0]) == NULL) {

	  print_error (pf, "Only b, d, v, and u specifications are allowed for connected mode digipeater filtering.");
	  result = -1;
	  next_token (pf);
	  return (result);
	}


/* undocumented: can use 0 or 1 for testing. */

	if (strcmp(pf->token_str, "0") == 0) {
	  result = 0;
	}
	else if (strcmp(pf->token_str, "1") == 0) {
	  result = 1;
	}

/* simple string matching */

/* b - budlist */

	else if (pf->token_str[0] == 'b' && ispunct(pf->token_str[1])) {
	  /* Budlist - AX.25 source address */
	  /* Could be different than source encapsulated by 3rd party header. */
	  char addr[AX25_MAX_ADDR_LEN];
	  ax25_get_addr_with_ssid (pf->pp, AX25_SOURCE, addr);
	  result = filt_bodgu (pf, addr);

	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), addr);
	  }
	}

/* o - object or item name */

	else if (pf->token_str[0] == 'o' && ispunct(pf->token_str[1])) {
	  result = filt_bodgu (pf, pf->decoded.g_name);

	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), pf->decoded.g_name);
	  }
	}

/* d - was digipeated by */

	else if (pf->token_str[0] == 'd' && ispunct(pf->token_str[1])) {
	  int n;
	  // Loop on all AX.25 digipeaters.
	  result = 0;
	  for (n = AX25_REPEATER_1; result == 0 && n < ax25_get_num_addr (pf->pp); n++) {
	    // Consider only those with the H (has-been-used) bit set.
	    if (ax25_get_h (pf->pp, n)) {
	      char addr[AX25_MAX_ADDR_LEN];
	      ax25_get_addr_with_ssid (pf->pp, n, addr);
	      result = filt_bodgu (pf, addr);
	    }
	  }

	  if (s_debug >= 2) {
	    char path[100];

	    ax25_format_via_path (pf->pp, path, sizeof(path));
	    if (strlen(path) == 0) {
	      strcpy (path, "no digipeater path");
	    }
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), path);
	  }
	}

/* v - via not used */

	else if (pf->token_str[0] == 'v' && ispunct(pf->token_str[1])) {
	  int n;
	  // loop on all AX.25 digipeaters (mnemonic Via)
	  result = 0;
	  for (n = AX25_REPEATER_1; result == 0 && n < ax25_get_num_addr (pf->pp); n++) {
	    // This is different than the previous "d" filter.
	    // Consider only those where the the H (has-been-used) bit is NOT set.
	    if ( ! ax25_get_h (pf->pp, n)) {
	      char addr[AX25_MAX_ADDR_LEN];
	      ax25_get_addr_with_ssid (pf->pp, n, addr);
	      result = filt_bodgu (pf, addr);
	    }
	  }

	  if (s_debug >= 2) {
	    char path[100];

	    ax25_format_via_path (pf->pp, path, sizeof(path));
	    if (strlen(path) == 0) {
	      strcpy (path, "no digipeater path");
	    }
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), path);
	  }
	}

/* g - Addressee of message. e.g. "BLN*" for bulletins. */

	else if (pf->token_str[0] == 'g' && ispunct(pf->token_str[1])) {
	  if (pf->decoded.g_message_subtype == message_subtype_message ||
	      pf->decoded.g_message_subtype == message_subtype_ack ||
	      pf->decoded.g_message_subtype == message_subtype_rej ||
	      pf->decoded.g_message_subtype == message_subtype_bulletin ||
	      pf->decoded.g_message_subtype == message_subtype_nws ||
	      pf->decoded.g_message_subtype == message_subtype_directed_query) {
	    result = filt_bodgu (pf, pf->decoded.g_addressee);

	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), pf->decoded.g_addressee);
	    }
	  }
	  else {
	    result = 0;
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), "not a message");
	    }
	  }
	}

/* u - unproto (AX.25 destination) */

	else if (pf->token_str[0] == 'u' && ispunct(pf->token_str[1])) {
	  /* Probably want to exclude mic-e types */
	  /* because destination is used for part of location. */

	  if (ax25_get_dti(pf->pp) != '\'' && ax25_get_dti(pf->pp) != '`') {
	    char addr[AX25_MAX_ADDR_LEN];
	    ax25_get_addr_with_ssid (pf->pp, AX25_DESTINATION, addr);
	    result = filt_bodgu (pf, addr);

	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), addr);
	    }
	  }
	  else {
	    result = 0;
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), "MIC-E packet type");
	    }
	  }
	}

/* t - packet type: position, weather, telemetry, etc. */

	else if (pf->token_str[0] == 't' && ispunct(pf->token_str[1])) {
	  
	  result = filt_t (pf);

	  if (s_debug >= 2) {
	    char *infop = NULL;
	    (void) ax25_get_info (pf->pp, (unsigned char **)(&infop));

	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %c data type indicator\n", pf->token_str, bool2text(result), *infop);
	  }
	}

/* r - range */

	else if (pf->token_str[0] == 'r' && ispunct(pf->token_str[1])) {
	  /* range */
	  char sdist[30];
	  strcpy (sdist, "unknown distance");
	  result = filt_r (pf, sdist);

	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("   %s returns %s for %s\n", pf->token_str, bool2text(result), sdist);
	  }
	}

/* s - symbol */

	else if (pf->token_str[0] == 's' && ispunct(pf->token_str[1])) {
	  /* symbol */
	  result = filt_s (pf);

	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    if (pf->decoded.g_symbol_table == '/') {
	      dw_printf ("   %s returns %s for symbol %c in primary table\n", pf->token_str, bool2text(result), pf->decoded.g_symbol_code);
	    }
	    else if (pf->decoded.g_symbol_table == '\\') {
	      dw_printf ("   %s returns %s for symbol %c in alternate table\n", pf->token_str, bool2text(result), pf->decoded.g_symbol_code);
	    }
	    else {
	      dw_printf ("   %s returns %s for symbol %c with overlay %c\n", pf->token_str, bool2text(result), pf->decoded.g_symbol_code, pf->decoded.g_symbol_table);
	    }
	  }
	}

/* i - IGate messaging default */

	else if (pf->token_str[0] == 'i' && ispunct(pf->token_str[1])) {
	  /* IGatge messaging */
	  result = filt_i (pf);

	  if (s_debug >= 2) {
	    char *infop = NULL;
	    (void) ax25_get_info (pf->pp, (unsigned char **)(&infop));

	    text_color_set(DW_COLOR_DEBUG);
	    if (pf->decoded.g_packet_type == packet_type_message) {
	      dw_printf ("   %s returns %s for message to %s\n", pf->token_str, bool2text(result), pf->decoded.g_addressee);
	    }
	    else {
	      dw_printf ("   %s returns %s for not an APRS 'message'\n", pf->token_str, bool2text(result));
	    }
	  }
	}

/* unrecognized filter type */

	else  {
	  char stemp[80];
	  snprintf (stemp, sizeof(stemp), "Unrecognized filter type '%c'", pf->token_str[0]);
	  print_error (pf, stemp);
	  result = -1;
	}

	next_token (pf);

	return (result);
}


/*------------------------------------------------------------------------------
 *
 * Name:	filt_bodgu
 * 
 * Purpose:	Filter with text pattern matching
 *
 * Inputs:	pf	- Pointer to current state information.	
 *			  token_str should have one of these filter specs:
 *
 * 				Budlist		b/call1/call2...  
 * 				Object		o/obj1/obj2...  
 * 				Digipeater	d/digi1/digi2...  
 * 				Group Msg	g/call1/call2...  
 * 				Unproto		u/unproto1/unproto2...
 *				Via-not-yet	v/digi1/digi2...noteapd
 *
 *		arg	- Value to match from source addr, destination,
 *			  used digipeater, object name, etc.
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	Same function is used for all of these because they are so similar.
 *		Look for exact match to any of the specified strings.
 *		All of them allow wildcarding with single * at the end.
 *
 *------------------------------------------------------------------------------*/

static int filt_bodgu (pfstate_t *pf, char *arg)
{
	char str[MAX_TOKEN_LEN];
	char *cp;
	char sep[2];
	char *v;
	int result = 0;

	strlcpy (str, pf->token_str, sizeof(str));
	sep[0] = str[1];
	sep[1] = '\0';
	cp = str + 2;

	while (result == 0 && (v = strsep (&cp, sep)) != NULL) {

	  int mlen;
	  char *w;

	  if ((w = strchr(v,'*')) != NULL) {
	    /* Wildcarding.  Should have single * on end. */

	    mlen = w - v;
	    if (mlen != (int)(strlen(v) - 1)) {
	      print_error (pf, "Any wildcard * must be at the end of pattern.\n");
	      return (-1);
	    }
	    if (strncmp(v,arg,mlen) == 0) result = 1;
	  } 
	  else {
	    /* Try for exact match. */
	    if (strcmp(v,arg) == 0) result = 1;
	  }
	}

	return (result);
}



/*------------------------------------------------------------------------------
 *
 * Name:	filt_t
 * 
 * Purpose:	Filter by packet type.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	The filter is loosely based the type filtering described here:
 *		http://www.aprs-is.net/javAPRSFilter.aspx
 *
 *		Mostly use g_packet_type and g_message_subtype from decode_aprs.
 *
 * References:
 *		http://www.aprs-is.net/WX/
 *		http://wxsvr.aprs.net.au/protocol-new.html	(has disappeared)
 *		
 *------------------------------------------------------------------------------*/

static int filt_t (pfstate_t *pf) 
{
	char src[AX25_MAX_ADDR_LEN];
	char *infop = NULL;
	char *f;

	memset (src, 0, sizeof(src));
	ax25_get_addr_with_ssid (pf->pp, AX25_SOURCE, src);
	(void) ax25_get_info (pf->pp, (unsigned char **)(&infop));

	assert (infop != NULL);

	for (f = pf->token_str + 2; *f != '\0'; f++) {
	  switch (*f) {
	
	    case 'p':				/* Position */
	      if (pf->decoded.g_packet_type == packet_type_position) return(1);
	      break;

	    case 'o':				/* Object */
	      if (pf->decoded.g_packet_type == packet_type_object) return(1);
	      break;

	    case 'i':				/* Item */
	      if (pf->decoded.g_packet_type == packet_type_item) return(1);
	      break;

	    case 'm':				// Any "message."
	      if (pf->decoded.g_packet_type == packet_type_message) return(1);
	      break;

	    case 'q':				/* Query */
	      if (pf->decoded.g_packet_type == packet_type_query) return(1);
	      break;

	    case 'c':				/* station Capabilities - my extension */
						/* Most often used for IGate statistics. */
	      if (pf->decoded.g_packet_type == packet_type_capabilities) return(1);
	      break;

	    case 's':				/* Status */
	      if (pf->decoded.g_packet_type == packet_type_status) return(1);
	      break;

	    case 't':				/* Telemetry data or metadata */
	      if (pf->decoded.g_packet_type == packet_type_telemetry) return(1);
	      break;

	    case 'u':				/* User-defined */
	      if (pf->decoded.g_packet_type == packet_type_userdefined) return(1);
	      break;

	    case 'h':				/* has third party Header - my extension */
	      if (pf->decoded.g_has_thirdparty_header) return (1);
	      break;

	    case 'w':				/* Weather */

	      if (pf->decoded.g_packet_type == packet_type_weather) return(1);

	      /* Positions !=/@  with symbol code _ are weather. */
	      /* Object with _ symbol is also weather.  APRS protocol spec page 66. */
	      // Can't use *infop because it would not work with 3rd party header.

	      if ((pf->decoded.g_packet_type == packet_type_position ||
	           pf->decoded.g_packet_type == packet_type_object) && pf->decoded.g_symbol_code == '_') return (1);
	      break;

	    case 'n':				/* NWS format */
	      if (pf->decoded.g_packet_type == packet_type_nws) return(1);
	      break;

	    default:

	      print_error (pf, "Invalid letter in t/ filter.\n");
	      return (-1);
	      break;
	  }
	}
	return (0);			/* Didn't match anything.  Reject */

} /* end filt_t */




/*------------------------------------------------------------------------------
 *
 * Name:	filt_r
 * 
 * Purpose:	Is it in range (kilometers) of given location.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *			  token_str should contain something of format:
 *
 *				r/lat/lon/dist
 *
 *			  We also need to know the location (if any) from the packet.
 *
 *				decoded.g_lat & decoded.g_lon
 *
 * Outputs:	sdist	- Distance as a string for troubleshooting.
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	
 *
 *------------------------------------------------------------------------------*/

static int filt_r (pfstate_t *pf, char *sdist)
{
	char str[MAX_TOKEN_LEN];
	char *cp;
	char sep[2];
	char *v;
	double dlat, dlon, ddist, km;


	strlcpy (str, pf->token_str, sizeof(str));
	sep[0] = str[1];
	sep[1] = '\0';
	cp = str + 2;

	if (pf->decoded.g_lat == G_UNKNOWN || pf->decoded.g_lon == G_UNKNOWN) {
	  return (0);
	}

	v = strsep (&cp, sep);
	if (v == NULL) {
	  print_error (pf, "Missing latitude for Range filter.");
	  return (-1);
	}
	dlat = atof(v);

	v = strsep (&cp, sep);
	if (v == NULL) {
	  print_error (pf, "Missing longitude for Range filter.");
	  return (-1);
	}
	dlon = atof(v);

	v = strsep (&cp, sep);
	if (v == NULL) {
	  print_error (pf, "Missing distance for Range filter.");
	  return (-1);
	}
	ddist = atof(v);

	km = ll_distance_km (dlat, dlon, pf->decoded.g_lat, pf->decoded.g_lon);

	sprintf (sdist, "%.2f km", km);

	if (km <= ddist) {
	  return (1);
	}

	return (0);
}



/*------------------------------------------------------------------------------
 *
 * Name:	filt_s
 * 
 * Purpose:	Filter by symbol.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *			  token_str should contain something of format:
 *
 *				s/pri/alt/over
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	
 *		  
 *		s/pri
 *		s/pri/alt
 *		s/pri/alt/
 *		s/pri/alt/over
 *
 *		"pri" is zero or more symbols from the primary symbol set.
 *			Symbol codes are any printable ASCII character other than | or ~.
 *			(Zero symbols here would be sensible only if later alt part is specified.)
 *		"alt" is one or more symbols from the alternate symbol set.
 *		"over" is overlay characters for the alternate symbol set.
 *			Only upper case letters, digits, and \ are allowed here.
 *			If the last part is not specified, any overlay or lack of overlay, is ignored.
 *			If the last part is specified, only the listed overlays will match.
 *			An explicit lack of overlay is represented by the \ character.
 *		
 *		Examples:
 *			s/O		Balloon.
 *			s/->		House or car from primary symbol table.
 *
 *			s//#		Alternate table digipeater, with or without overlay.
 *			s//#/\		Alternate table digipeater, only if no overlay.
 *			s//#/SL1	Alternate table digipeater, with overlay S, L, or 1.
 *			s//#/SL\	Alternate table digipeater, with S, L, or no overlay.
 *
 *			s/s/s		Any variation of watercraft.  Either symbol table.  With or without overlay.
 *			s/s/s/		Ship or ship sideview, only if no overlay.
 *			s//s/J		Jet Ski.
 *
 *		What if you want to use the / symbol when / is being used as a delimiter here?  Recall that you
 *		can use some other special character after the initial lower case letter and this becomes the
 *		delimiter for the rest of the specification.
 *
 *		Examples:
 *
 *			s:/		Red Dot.
 *			s::/		Waypoint Destination, with or without overlay.
 *			s:/:/		Either Red Dot or Waypoint Destination.
 *			s:/:/:		Either Red Dot or Waypoint Destination, no overlay.
 *
 *		Bad example:
 *
 *			Someone tried using this to include ballons:   s/'/O/-/#/_
 *			probably following the buddy filter pattern of / between each alternative.
 *			There should be an error message because it has more than 3 delimiter characters.
 *
 * 
 *------------------------------------------------------------------------------*/

static int filt_s (pfstate_t *pf)
{
	char str[MAX_TOKEN_LEN];
	char *cp;
	char sep[2];		// Delimiter character.  Typically / but it could be different.
	char *pri = NULL, *alt = NULL, *over = NULL, *extra = NULL;
	char *x;


	strlcpy (str, pf->token_str, sizeof(str));
	sep[0] = str[1];
	sep[1] = '\0';
	cp = str + 2;


// First, separate the parts and do a strict syntax check.

	pri = strsep (&cp, sep);

	if (pri != NULL) {

	  // Zero length is acceptable if alternate symbol(s) specified.  Will check that later.

	  for (x = pri; *x != '\0'; x++) {
	    if ( ! isprint(*x) || *x == '|' || *x == '~') {
	      print_error (pf, "Symbol filter, primary must be printable ASCII character(s) other than | or ~.");
	      return (-1);
	    }
	  }

	  alt = strsep (&cp, sep);

	  if (alt != NULL) {

	    // Zero length after second / would be pointless.

	    if (strlen(alt) == 0) {
	      print_error (pf, "Nothing specified for alternate symbol table.");
	      return (-1);
	    }

	    for (x = alt; *x != '\0'; x++) {
	      if ( ! isprint(*x) || *x == '|' || *x == '~') {
	        print_error (pf, "Symbol filter, alternate must be printable ASCII character(s) other than | or ~.");
	        return (-1);
	      }
	    }

	    over = strsep (&cp, sep);

	    if (over != NULL) {

	      // Zero length is acceptable and is not the same as missing.

	      for (x = over; *x != '\0'; x++) {
	        if ( (! isupper(*x)) && (! isdigit(*x)) && *x != '\\') {
	          print_error (pf, "Symbol filter, overlay must be upper case letter, digit, or \\.");
	          return (-1);
	        }
	      }

	      extra = strsep (&cp, sep);

	      if (extra != NULL) {
	        print_error (pf, "More than 3 delimiter characters in Symbol filter.");
	        return (-1);
	      }
	    }
	  }
	  else {
	    // No alt part is OK if at least one primary symbol was specified.
	    if (strlen(pri) == 0) {
	      print_error (pf, "No symbols specified for Symbol filter.");
	      return (-1);
	    }
	  }
	}
	else {
	  print_error (pf, "Missing arguments for Symbol filter.");
	  return (-1);
	}


// This applies only for Position, Object, Item.
// decode_aprs() should set symbol code to space to mean undefined.

	if (pf->decoded.g_symbol_code == ' ') {
	  return (0);
	}


// Look for Primary symbols.

	if (pf->decoded.g_symbol_table == '/') {
	  if (pri != NULL && strlen(pri) > 0) {
	    return (strchr(pri, pf->decoded.g_symbol_code) != NULL);
	  }
	}

	if (alt == NULL) {
	  return (0);
	}

	//printf ("alt=\"%s\"  sym='%c'\n", alt, pf->decoded.g_symbol_code);

// Look for Alternate symbols.

	if (strchr(alt, pf->decoded.g_symbol_code) != NULL) {

	  // We have a match but that might not be enough.
	  // We must see if there was an overlay part specified.

	  if (over != NULL) {

	    if (strlen(over) > 0) {

	      // Non-zero length overlay part was specified.
	      // Need to match one of them.

	      return (strchr(over, pf->decoded.g_symbol_table) != NULL);
	    }
	    else {

	      // Zero length overlay part was specified.
	      // We must have no overlay, i.e.  table is \.

	      return (pf->decoded.g_symbol_table == '\\');
	    }
	  }
	  else {

	    // No check of overlay part.  Just make sure it is not primary table.

	    return (pf->decoded.g_symbol_table != '/');
	  }
	}

	return (0);

} /* end filt_s */


/*------------------------------------------------------------------------------
 *
 * Name:	filt_i
 *
 * Purpose:	IGate messaging filter.
 *		This would make sense only for IS>RF direction.
 *
 * Inputs:	pf	- Pointer to current state information.
 *			  token_str should contain something of format:
 *
 *				i/time/hops/lat/lon/km
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description: Selection is based on time since last heard on RF, and distance
 *		in terms of digipeater hops and/or physical location.
 *
 *		i/time
 *		i/time/hops
 *		i/time/hops/lat/lon/km
 *
 *
 *		"time" is maximum number of minutes since message addressee was last heard.
 *			This is required.  APRS-IS uses 3 hours so that would be a good value here.
 *
 *		"hops" is maximum number of digpeater hops.  (i.e. 0 for heard directly).
 * 			If hops is not specified, the maximum transmit digipeater hop count,
 *			from the IGTXVIA configuration will be used.

 *		The rest is distanced, in kilometers, from given point.
 *		
 *		Examples:
 *			i/180/0		Heard in past 3 hours directly.
 *			i/45		Past 45 minutes, default max digi hops.
 *			i/180/3		Default time (3 hours), max 3 digi hops.
 *			i/180/8/42.6/-71.3/50.
 *
 *
 *		It only makes sense to use this for the IS>RF direction.
 *		The basic idea is that we want to transmit a "message" only if the
 *		addressee has been heard recently and is not too far away.
 *
 *		That is so we can distinguish messages addressed to a specific
 *		station, and other sundry uses of the addressee field.
 *
 *		After passing along a "message" we will also allow the next
 *		position report from the sender of the "message."
 *		That is done somewhere else.  We are not concerned with it here.
 *
 *		IMHO, the rules here are too restrictive.
 *
 *		    The APRS-IS would send a "message" to my IGate only if the addressee
 *		    has been heard nearby recently.  180 minutes, I believe.
 *		    Why would I not want to transmit it?
 *
 * Discussion:	In retrospect, I think this is far too complicated.
 *		In a future release, I think at options other than time should be removed.
 *		Messages have more value than most packets.  Why reduce the chance of successful delivery?
 *
 *		Consider the following scenario:
 *
 *		(1) We hear AA1PR-9 by a path of 4 digipeaters.
 *		    Looking closer, it's probably only two because there are left over WIDE1-0 and WIDE2-0.
 *
 *			Digipeater WIDE2 (probably N3LLO-3) audio level = 72(19/15)   [NONE]   _|||||___
 *			[0.3] AA1PR-9>APY300,K1EQX-7,WIDE1,N3LLO-3,WIDE2*,ARISS::ANSRVR   :cq hotg vt aprsthursday{01<0x0d>
 *
 *		(2) APRS-IS sends a response to us.
 *
 *			[ig>tx] ANSRVR>APWW11,KJ4ERJ-15*,TCPIP*,qAS,KJ4ERJ-15::AA1PR-9  :N:HOTG 161 Messages Sent{JL}
 *
 *		(3) Here is our analysis of whether it should be sent to RF.
 *
 *			Was message addressee AA1PR-9 heard in the past 180 minutes, with 2 or fewer digipeater hops?
 *			No, AA1PR-9 was last heard over the radio with 4 digipeater hops 0 minutes ago.
 *
 *		The wrong hop count caused us to drop a packet that should have been transmitted.
 *		We could put in a hack to not count the "WIDE*-0"  addresses.
 *		That is not correct because other prefixes could be used and we don't know
 *		what they are for other digipeaters.
 *		I think the best solution is to simply ignore the hop count.
 *
 * Release 1.7:	I got overly ambitious and now realize this is just giving people too much
 *		"rope to hang themselves," drop messages unexpectedly, and accidentally break messaging.
 *		Change documentation to mention only the time limit.
 *		The other functionality will be undocumented and maybe disappear over time.
 *
 *------------------------------------------------------------------------------*/

static int filt_i (pfstate_t *pf)
{
	char str[MAX_TOKEN_LEN];
	char *cp;
	char sep[2];
	char *v;

// http://lists.tapr.org/pipermail/aprssig_lists.tapr.org/2020-July/048656.html
// Default of 3 hours should be good.
// One might question why to have a time limit at all.  Messages are very rare
// the the APRS-IS wouldn't be sending it to me unless the addressee was in the
// vicinity recently.
// TODO: Should produce a warning if a user specified filter does not include "i".

	int heardtime = 180;	// 3 hours * 60 min/hr = 180 minutes
#if PFTEST
	int maxhops = 2;
#else
	int maxhops = save_igate_config_p->max_digi_hops;	// from IGTXVIA config.
#endif
	double dlat = G_UNKNOWN;
	double dlon = G_UNKNOWN;
	double km = G_UNKNOWN;


	//char src[AX25_MAX_ADDR_LEN];
	//char *infop = NULL;
	//int info_len;
	//char *f;
	//char addressee[AX25_MAX_ADDR_LEN];


	strlcpy (str, pf->token_str, sizeof(str));
	sep[0] = str[1];
	sep[1] = '\0';
	cp = str + 2;

// Get parameters or defaults.

	v = strsep (&cp, sep);

	if (v != NULL && strlen(v) > 0) {
	  heardtime = atoi(v);
	}
	else {
	  print_error (pf, "Missing time limit for IGate message filter.");
	  return (-1);
	}

	v = strsep (&cp, sep);

	if (v != NULL) {
	  if (strlen(v) > 0) {
	    maxhops = atoi(v);
	  }
	  else {
	    print_error (pf, "Missing max digipeater hops for IGate message filter.");
	    return (-1);
	  }

	  v = strsep (&cp, sep);
	  if (v != NULL && strlen(v) > 0) {
	    dlat = atof(v);

	    v = strsep (&cp, sep);
	    if (v != NULL && strlen(v) > 0) {
	      dlon = atof(v);
	    }
	    else {
	      print_error (pf, "Missing longitude for IGate message filter.");
	      return (-1);
	    }

	    v = strsep (&cp, sep);
	    if (v != NULL && strlen(v) > 0) {
	      km = atof(v);
	    }
	    else {
	      print_error (pf, "Missing distance, in km, for IGate message filter.");
	      return (-1);
	    }
	  }

	  v = strsep (&cp, sep);
	  if (v != NULL) {
	    print_error (pf, "Something unexpected after distance for IGate message filter.");
	    return (-1);
	  }
	}

#if PFTEST
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("debug: IGate message filter, %d minutes, %d hops, %.2f %.2f %.2f km\n",
		heardtime, maxhops, dlat, dlon, km);
#endif


/*
 * Get source address and info part.
 * Addressee has already been extracted into pf->decoded.g_addressee.
 */
	if (pf->decoded.g_packet_type != packet_type_message) return(0);

#if defined(PFTEST) || defined(DIGITEST)	// TODO: test functionality too, not just syntax.

	(void)dlat;	// Suppress set and not used warning.
	(void)dlon;
	(void)km;
	(void)maxhops;
	(void)heardtime;

	return (1);
#else

/*
 * Condition 1:
 *	"the receiving station has been heard within range within a predefined time
 *	 period (range defined as digi hops, distance, or both)."
 */

	int was_heard = mheard_was_recently_nearby ("addressee", pf->decoded.g_addressee, heardtime, maxhops, dlat, dlon, km);

	if ( ! was_heard) return (0);

/*
 * Condition 2:
 *	"the sending station has not been heard via RF within a predefined time period
 *	 (packets gated from the Internet by other stations are excluded from this test)."
 *
 * This is the part I'm not so sure about.
 * I guess the intention is that if the sender can be heard over RF, then the addressee
 * might hear the sender without the help of Igate stations.
 * Suppose the sender was 1 digipeater hop to the west and the addressee was 1 digipeater hop to the east.
 * I can communicate with each of them with 1 digipeater hop but for them to reach each other, they
 * might need 3 hops and using that many is generally frowned upon and rare.
 *
 * Maybe we could compromise here and say the sender must have been heard directly.
 * It sent the message currently being processed so we must have heard it very recently, i.e. in
 * the past minute, rather than the usual 180 minutes for the addressee.
 */

	was_heard = mheard_was_recently_nearby ("source", pf->decoded.g_src, 1, 0, G_UNKNOWN, G_UNKNOWN, G_UNKNOWN);

	if (was_heard) return (0);

	return (1);

#endif

} /* end filt_i */


/*-------------------------------------------------------------------
 *
 * Name:   	print_error
 *    
 * Purpose:     Print error message with context so someone can figure out what caused it.
 *
 * Inputs:	pf	- Pointer to current state information.	
 *
 *		str	- Specific error message.
 *
 *--------------------------------------------------------------------*/

static void print_error (pfstate_t *pf, char *msg)
{
	char intro[50];

	if (pf->from_chan == MAX_TOTAL_CHANS) {

	  if (pf->to_chan == MAX_TOTAL_CHANS) {
	    snprintf (intro, sizeof(intro), "filter[IG,IG]: ");
	  }
	  else {
	    snprintf (intro, sizeof(intro), "filter[IG,%d]: ", pf->to_chan);
	  }
	}
	else {

	  if (pf->to_chan == MAX_TOTAL_CHANS) {
	    snprintf (intro, sizeof(intro), "filter[%d,IG]: ", pf->from_chan);
	  }
	  else {
	    snprintf (intro, sizeof(intro), "filter[%d,%d]: ", pf->from_chan, pf->to_chan);
	  }
	}

	text_color_set (DW_COLOR_ERROR);

	dw_printf ("%s%s\n", intro, pf->filter_str);
	dw_printf ("%*s\n", (int)(strlen(intro) + pf->tokeni + 1), "^");
	dw_printf ("%s\n", msg);
}



#if PFTEST


/*-------------------------------------------------------------------
 *
 * Name:   	main & pftest
 *    
 * Purpose:     Unit test for packet filtering.
 *
 * Usage:	gcc -Wall -o pftest -DPFTEST pfilter.c ax25_pad.o textcolor.o fcs_calc.o decode_aprs.o latlong.o symbols.o telemetry.o tt_text.c misc.a regex.a && ./pftest
 *		
 *
 *--------------------------------------------------------------------*/


static int error_count = 0;
static void pftest (int test_num, char *filter, char *packet, int expected);

int main ()
{

	dw_printf ("Quick test for packet filtering.\n");
	dw_printf ("Some error messages are normal.  Look at the final success/fail message.\n");

	pftest (1, "", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (2, "0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (3, "1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);

	pftest (10, "0 | 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (11, "0 | 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (12, "1 | 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (13, "1 | 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (14, "0 | 0 | 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);

	pftest (20, "0 & 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (21, "0 & 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (22, "1 & 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (23, "1 & 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (24, "1 & 1 & 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (24, "1 & 0 & 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (24, "1 & 1 & 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	pftest (30, "0 | ! 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (31, "! 1 | ! 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (32, "! ! 1 | 0", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (33, "1 | ! ! 1", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);

	pftest (40, "1 &(!0 |0 )", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (41, "0 |(!0 )", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (42, "1 |(!!0 )", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (42, "(!(1 ) & (1 ))", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	pftest (50, "b/W2UB/WB2OSZ-5/N2GH", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (51, "b/W2UB/WB2OSZ-14/N2GH", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (52, "b#W2UB#WB2OSZ-5#N2GH", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (53, "b#W2UB#WB2OSZ-14#N2GH", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	pftest (60, "o/HOME", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 0);
	pftest (61, "o/home", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 1);
	pftest (62, "o/HOME", "HOME>APDW12,WIDE1-1,WIDE2-1:;AWAY     *111111z4237.14N/07120.83W-Chelmsford MA", 0);
	pftest (63, "o/WB2OSZ-5", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (64, "o/HOME", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:)home!4237.14N/07120.83W-Chelmsford MA", 0);
	pftest (65, "o/home", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:)home!4237.14N/07120.83W-Chelmsford MA", 1);

	pftest (70, "d/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (71, "d/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1*,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (72, "d/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (73, "d/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3*,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (74, "d/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4*:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (75, "d/DIGI9/DIGI2", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);

	pftest (80, "g/W2UB",        "WB2OSZ-5>APDW12::W2UB     :text", 1);
	pftest (81, "g/W2UB/W2UB-*", "WB2OSZ-5>APDW12::W2UB-9   :text", 1);
	pftest (82, "g/W2UB/*",      "WB2OSZ-5>APDW12::XXX      :text", 1);
	pftest (83, "g/W2UB/W*UB",   "WB2OSZ-5>APDW12::W2UB-9   :text", -1);
	pftest (84, "g/W2UB*",       "WB2OSZ-5>APDW12::W2UB-9   :text", 1);
	pftest (85, "g/W2UB*",       "WB2OSZ-5>APDW12::W2UBZZ   :text", 1);
	pftest (86, "g/W2UB",        "WB2OSZ-5>APDW12::W2UB-9   :text", 0);
	pftest (87, "g/*",           "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (88, "g/W*",          "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	pftest (90, "u/APWW10", "WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", 1);
	pftest (91, "u/TRSY3T", "W1WRA-7>TRSY3T,WIDE1-1,WIDE2-1:`c-:l!hK\\>\"4b}=<0x0d>", 0);
	pftest (92, "u/APDW11/APDW12", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (93, "u/APDW", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	// rather sparse coverage of the cases
	pftest (100, "t/mqt", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (101, "t/mqtp", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (102, "t/mqtp", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 0);
	pftest (103, "t/mqop", "WB2OSZ>APDW12,WIDE1-1,WIDE2-1:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 1);
	pftest (104, "t/p", "W1WRA-7>TRSY3T,WIDE1-1,WIDE2-1:`c-:l!hK\\>\"4b}=<0x0d>", 1);
	pftest (104, "t/s", "KB1CHU-13>APWW10,W1CLA-1*,WIDE2-1:>FN42pb/_DX: W1MHL 36.0mi 306<0xb0> 13:24 4223.32N 07115.23W", 1);

	pftest (110, "t/p", "N8VIM>APN391,AB1OC-10,W1MRA*,WIDE2:$ULTW0000000001110B6E27F4FFF3897B0001035E004E04DD00030000<0x0d><0x0a>", 0);
	pftest (111, "t/w", "N8VIM>APN391,AB1OC-10,W1MRA*,WIDE2:$ULTW0000000001110B6E27F4FFF3897B0001035E004E04DD00030000<0x0d><0x0a>", 1);
	pftest (112, "t/t", "WM1X>APU25N:@210147z4235.39N/07106.58W_359/000g000t027r000P000p000h89b10234/WX REPORT {UIV32N}<0x0d>", 0);
	pftest (113, "t/w", "WM1X>APU25N:@210147z4235.39N/07106.58W_359/000g000t027r000P000p000h89b10234/WX REPORT {UIV32N}<0x0d>", 1);

	/* Telemetry metadata should not be classified as message. */
	pftest (114, "t/t", "KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 1);
	pftest (115, "t/m", "KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 0);
	pftest (116, "t/t", "KB1GKN-10>APRX27,UNCAN,WIDE1*:T#491,4.9,0.3,25.0,0.0,1.0,00000000", 1);

	/* Bulletins should not be considered to be messages.  Was bug in 1.6. */
	pftest (117, "t/m", "A>B::W1AW     :test", 1);
	pftest (118, "t/m", "A>B::BLN      :test", 0);
	pftest (119, "t/m", "A>B::NWS      :test", 0);

	// https://www.aprs-is.net/WX/
	pftest (121, "t/p", "CWAPID>APRS::NWS-TTTTT:DDHHMMz,ADVISETYPE,zcs{seq#", 0);
	pftest (122, "t/p", "CWAPID>APRS::SKYCWA   :DDHHMMz,ADVISETYPE,zcs{seq#", 0);
	pftest (123, "t/p", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 0);
	pftest (124, "t/n", "CWAPID>APRS::NWS-TTTTT:DDHHMMz,ADVISETYPE,zcs{seq#", 1);
	pftest (125, "t/n", "CWAPID>APRS::SKYCWA   :DDHHMMz,ADVISETYPE,zcs{seq#", 1);
	//pftest (126, "t/n", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 1);
	pftest (127, "t/",  "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 0);

	pftest (128, "t/c",  "S0RCE>DEST:<stationcapabilities", 1);
	pftest (129, "t/h",  "S0RCE>DEST:<stationcapabilities", 0);
	pftest (130, "t/h",  "S0RCE>DEST:}WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (131, "t/c",  "S0RCE>DEST:}WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	pftest (140, "r/42.6/-71.3/10", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (141, "r/42.6/-71.3/10", "WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", 0);

	pftest (145, "( t/t & b/WB2OSZ ) | ( t/o & ! r/42.6/-71.3/1 )", "WB2OSZ>APDW12:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 1);

	pftest (150, "s/->", "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (151, "s/->", "WB2OSZ-5>APDW12:!4237.14N/07120.83W-PHG7140Chelmsford MA", 1);
	pftest (152, "s/->", "WB2OSZ-5>APDW12:!4237.14N/07120.83W>PHG7140Chelmsford MA", 1);
	pftest (153, "s/->", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W>PHG7140Chelmsford MA", 0);

	pftest (154, "s//#", "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (155, "s//#", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W#PHG7140Chelmsford MA", 1);
	pftest (156, "s//#", "WB2OSZ-5>APDW12:!4237.14N/07120.83W#PHG7140Chelmsford MA", 0);

	pftest (157, "s//#/\\", "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (158, "s//#/\\", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W#PHG7140Chelmsford MA", 1);
	pftest (159, "s//#/\\", "WB2OSZ-5>APDW12:!4237.14N/07120.83W#PHG7140Chelmsford MA", 0);

	pftest (160, "s//#/LS1", "WB2OSZ-5>APDW12:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (161, "s//#/LS1", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W#PHG7140Chelmsford MA", 0);
	pftest (162, "s//#/LS1", "WB2OSZ-5>APDW12:!4237.14N/07120.83W#PHG7140Chelmsford MA", 0);
	pftest (163, "s//#/LS\\", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W#PHG7140Chelmsford MA", 1);

	pftest (170, "s:/", "WB2OSZ-5>APDW12:!4237.14N/07120.83W/PHG7140Chelmsford MA", 1);
	pftest (171, "s:/", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W/PHG7140Chelmsford MA", 0);
	pftest (172, "s::/", "WB2OSZ-5>APDW12:!4237.14N/07120.83W/PHG7140Chelmsford MA", 0);
	pftest (173, "s::/", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W/PHG7140Chelmsford MA", 1);
	pftest (174, "s:/:/", "WB2OSZ-5>APDW12:!4237.14N/07120.83W/PHG7140Chelmsford MA", 1);
	pftest (175, "s:/:/", "WB2OSZ-5>APDW12:!4237.14N\\07120.83W/PHG7140Chelmsford MA", 1);
	pftest (176, "s:/:/", "WB2OSZ-5>APDW12:!4237.14NX07120.83W/PHG7140Chelmsford MA", 1);
	pftest (177, "s:/:/:X", "WB2OSZ-5>APDW12:!4237.14NX07120.83W/PHG7140Chelmsford MA", 1);

	// FIXME: Different on Windows and  64 bit Linux.
	//pftest (178, "s:/:/:", "WB2OSZ-5>APDW12:!4237.14NX07120.83W/PHG7140Chelmsford MA", 1);

	pftest (179, "s:/:/:\\", "WB2OSZ-5>APDW12:!4237.14NX07120.83W/PHG7140Chelmsford MA", 0);

	pftest (180, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (181, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1*,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (182, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (183, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3*,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (184, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4*:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (185, "v/DIGI9/DIGI2", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	/* Test error reporting. */

	pftest (200, "x/", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (201, "t/w & ( t/w | t/w ", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (202, "t/w ) ", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (203, "!", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (203, "t/w t/w", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (204, "r/42.6/-71.3", "WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", -1);

	pftest (210, "i/30/8/42.6/-71.3/50", "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (212, "i/30/8/42.6/-71.3/",   "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", -1);
	pftest (213, "i/30/8/42.6/-71.3",    "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", -1);
	pftest (214, "i/30/8/42.6/",         "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", -1);
	pftest (215, "i/30/8/42.6",          "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", -1);
	pftest (216, "i/30/8/",              "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (217, "i/30/8",               "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);

	// FIXME: behaves differently on Windows and Linux.  Why?
	// On Windows we have our own version of strsep because it's not in the MS library.
	// It must behave differently than the Linux version when nothing follows the last separator.
	//pftest (228, "i/30/",                "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);

	pftest (229, "i/30",                 "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (230, "i/30",                 "X>X:}WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (231, "i/",                   "WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", -1);

	// Besure bulletins and telemetry metadata don't get included.
	pftest (234, "i/30", "KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 0);
	pftest (235, "i/30", "A>B::BLN      :test", 0);

	pftest (240, "s/", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (241, "s/'/O/-/#/_", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (242, "s/O/O/c", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (243, "s/O/O/1/2", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (244, "s/O/|/1", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (245, "s//", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);
	pftest (246, "s///", "WB2OSZ-5>APDW12:!4237.14N/07120.83WOPHG7140Chelmsford MA", -1);

	// Third party header - done properly in 1.7.
	// Packet filter t/h is no longer a mutually exclusive packet type.
	// Now it is an independent attribute and the encapsulated part is evaluated.

	pftest (250, "o/home", "A>B:}WB2OSZ>APDW12,WIDE1-1,WIDE2-1:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 1);
	pftest (251, "t/p",    "A>B:}W1WRA-7>TRSY3T,WIDE1-1,WIDE2-1:`c-:l!hK\\>\"4b}=<0x0d>", 1);
	pftest (252, "i/180",  "A>B:}WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (253, "t/m",  "A>B:}WB2OSZ-5>APDW14::W2UB     :Happy Birthday{001", 1);
	pftest (254, "r/42.6/-71.3/10", "A>B:}WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (254, "r/42.6/-71.3/10", "A>B:}WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", 0);
	pftest (255, "t/h",  "KB1GKN-10>APRX27,UNCAN,WIDE1*:T#491,4.9,0.3,25.0,0.0,1.0,00000000", 0);
	pftest (256, "t/h",  "A>B:}KB1GKN-10>APRX27,UNCAN,WIDE1*:T#491,4.9,0.3,25.0,0.0,1.0,00000000", 1);
	pftest (258, "t/t",  "A>B:}KB1GKN-10>APRX27,UNCAN,WIDE1*:T#491,4.9,0.3,25.0,0.0,1.0,00000000", 1);
	pftest (259, "t/t",  "A>B:}KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 1);

	pftest (270, "g/BLN*", "WB2OSZ>APDW17::BLN1xxxxx:bulletin text", 1);
	pftest (271, "g/BLN*", "A>B:}WB2OSZ>APDW17::BLN1xxxxx:bulletin text", 1);
	pftest (272, "g/BLN*", "A>B:}WB2OSZ>APDW17::W1AW     :xxxx", 0);

	pftest (273, "g/NWS*", "WB2OSZ>APDW17::NWS-xxxxx:weather bulletin", 1);
	pftest (274, "g/NWS*", "A>B:}WB2OSZ>APDW17::NWS-xxxxx:weather bulletin", 1);
	pftest (275, "g/NWS*", "A>B:}WB2OSZ>APDW17::W1AW     :xxxx", 0);

// TODO: add b/ with 3rd party header.

// TODO: to be continued...  directed query ...

	if (error_count > 0) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("\nPacket Filtering Test - FAILED!     %d errors\n", error_count);
	  exit (EXIT_FAILURE);
	}
	text_color_set (DW_COLOR_REC);
	dw_printf ("\nPacket Filtering Test - SUCCESS!\n");
	exit (EXIT_SUCCESS);

}

static void pftest (int test_num, char *filter, char *monitor, int expected)
{
	int result;
	packet_t pp;

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("test number %d\n", test_num);
	
	pp = ax25_from_text (monitor, 1);
	assert (pp != NULL);

	result = pfilter (0, 0, filter, pp, 1);
	if (result != expected) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Unexpected result for test number %d\n", test_num);
	  error_count++;
	}

	ax25_delete (pp);
}

#endif /* if TEST */

/* end pfilter.c */



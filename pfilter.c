//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2015, 2016  John Langner, WB2OSZ
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


#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#if __WIN32__
char *strsep(char **stringp, const char *delim);
#endif

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "decode_aprs.h"
#include "latlong.h"
#include "pfilter.h"



typedef enum token_type_e { TOKEN_AND, TOKEN_OR, TOKEN_NOT, TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_FILTER_SPEC, TOKEN_EOL } token_type_t;


#define MAX_FILTER_LEN 1024
#define MAX_TOKEN_LEN 1024

typedef struct pfstate_s {

	int from_chan;				/* From and to channels.   MAX_CHANS is used for IGate. */
	int to_chan;				/* Used only for debug and error messages. */


// TODO: might want to put channels and packet here so we only pass one thing around.

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
 * Packet split into separate parts.
 * Most interesting fields are:
 *		g_src		- source address
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
static int filt_r (pfstate_t *pf);
static int filt_s (pfstate_t *pf);


/*-------------------------------------------------------------------
 *
 * Name:        pfilter.c
 *
 * Purpose:     Decide whether a packet should be allowed thru.
 *
 * Inputs:	from_chan - Channel packet is coming from.  
 *		to_chan	  - Channel packet is going to.
 *				Both are 0 .. MAX_CHANS-1 or MAX_CHANS for IGate.  
 *			 	For debug/error messages only.
 *
 *		filter	- String of filter specs and logical operators to combine them.
 *
 *		pp	- Packet object handle.
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	This might be running in multiple threads at the same time so
 *		no static data allowed and take other thread-safe precautions.
 *
 *--------------------------------------------------------------------*/

int pfilter (int from_chan, int to_chan, char *filter, packet_t pp)
{
	pfstate_t pfstate;
	char *p;
	int result;

	assert (from_chan >= 0 && from_chan <= MAX_CHANS);
	assert (to_chan >= 0 && to_chan <= MAX_CHANS);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR in pfilter: NULL packet pointer. Please report this!\n");
	  return (-1);
	}
	if (filter == NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR in pfilter: NULL filter string pointer. Please report this!\n");
	  return (-1);
	}

	pfstate.from_chan = from_chan;
	pfstate.to_chan = to_chan;

	/* Copy filter string, removing any control characters. */

	memset (pfstate.filter_str, 0, sizeof(pfstate.filter_str));
	strncpy (pfstate.filter_str, filter, MAX_FILTER_LEN-1);

	pfstate.nexti = 0;
	for (p = pfstate.filter_str; *p != '\0'; p++) {
	  if (iscntrl(*p)) {
	    *p = ' ';
	  }
	}

	pfstate.pp = pp;
	decode_aprs (&pfstate.decoded, pp, 1);	

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
 *--------------------------------------------------------------------*/



static int parse_filter_spec (pfstate_t *pf)
{
	int result = -1;
	char addr[AX25_MAX_ADDR_LEN];

/* undocumented: can use 0 or 1 for testing. */

	if (strcmp(pf->token_str, "0") == 0) {
	  result = 0;
	}
	else if (strcmp(pf->token_str, "1") == 0) {
	  result = 1;
	}

/* simple string matching */

	else if (pf->token_str[0] == 'b' && ispunct(pf->token_str[1])) {
	  /* Budlist - source address */
	  result = filt_bodgu (pf, pf->decoded.g_src);
	}
	else if (pf->token_str[0] == 'o' && ispunct(pf->token_str[1])) {
	  /* Object or item name */
	  result = filt_bodgu (pf, pf->decoded.g_name);
	}
	else if (pf->token_str[0] == 'd' && ispunct(pf->token_str[1])) {
	  int n;
	  // loop on all digipeaters
	  result = 0;
	  for (n = AX25_REPEATER_1; result == 0 && n < ax25_get_num_addr (pf->pp); n++) {
	    // Consider only those with the H (has-been-used) bit set.
	    if (ax25_get_h (pf->pp, n)) {
	      ax25_get_addr_with_ssid (pf->pp, n, addr);
	      result = filt_bodgu (pf, addr);
	    }
	  }
	}
	else if (pf->token_str[0] == 'v' && ispunct(pf->token_str[1])) {
	  int n;
	  // loop on all digipeaters (mnemonic Via)
	  result = 0;
	  for (n = AX25_REPEATER_1; result == 0 && n < ax25_get_num_addr (pf->pp); n++) {
	    // This is different than the previous "d" filter.
	    // Consider only those where the the H (has-been-used) bit is NOT set.
	    if ( ! ax25_get_h (pf->pp, n)) {
	      ax25_get_addr_with_ssid (pf->pp, n, addr);
	      result = filt_bodgu (pf, addr);
	    }
	  }
	}
	else if (pf->token_str[0] == 'g' && ispunct(pf->token_str[1])) {
	  /* Addressee of message. */
	  if (ax25_get_dti(pf->pp) == ':') {
	    result = filt_bodgu (pf, pf->decoded.g_addressee);
	  }
	  else {
	    result = 0;
	  }
	}
	else if (pf->token_str[0] == 'u' && ispunct(pf->token_str[1])) {
	  /* Unproto (destination) - probably want to exclude mic-e types */
	  /* because destintation is used for part of location. */

	  if (ax25_get_dti(pf->pp) != '\'' && ax25_get_dti(pf->pp) != '`') {
	    ax25_get_addr_with_ssid (pf->pp, AX25_DESTINATION, addr);
	    result = filt_bodgu (pf, addr);
	  }
	  else {
	    result = 0;
	  }
	}

/* type: position, weather, etc. */

	else if (pf->token_str[0] == 't' && ispunct(pf->token_str[1])) {
	  
	  ax25_get_addr_with_ssid (pf->pp, AX25_DESTINATION, addr);
	  result = filt_t (pf);
	}

/* range */

	else if (pf->token_str[0] == 'r' && ispunct(pf->token_str[1])) {
	  /* range */
	  result = filt_r (pf);
	}

/* symbol */

	else if (pf->token_str[0] == 's' && ispunct(pf->token_str[1])) {
	  /* symbol */
	  result = filt_s (pf);
	}

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
 *				Via-not-yet	v/digi1/digi2...
 *
 *		arg	- Value to match from source addr, destination,
 *			  used digipeater, object name, etc.
 *
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	Same function is used for all of these because they are so similar.
 *		Look for exact match to any of the specifed strings.
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
	    if (mlen != strlen(v) - 1) {
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
 * Description:	The filter is based the type filtering described here:
 *		http://www.aprs-is.net/javAPRSFilter.aspx
 *
 *		Most of these simply check the first byte of the information part.
 *		Trying to detect NWS information is a little trickier.
 *		http://www.aprs-is.net/WX/
 *		http://wxsvr.aprs.net.au/protocol-new.html	
 *		
 *------------------------------------------------------------------------------*/

/* Telemetry metadata is a special case of message. */
/* We want to categorize it as telemetry rather than message. */

static int is_telem_metadata (char *infop)
{
	if (*infop != ':') return (0);
	if (strlen(infop) < 16) return (0);
	if (strncmp(infop+10, ":PARM.", 6) == 0) return (1);
	if (strncmp(infop+10, ":UNIT.", 6) == 0) return (1);
	if (strncmp(infop+10, ":EQNS.", 6) == 0) return (1);
	if (strncmp(infop+10, ":BITS.", 6) == 0) return (1);
	return (0);
}


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
	      if (*infop == '!') return (1);
	      if (*infop == '\'') return (1);
	      if (*infop == '/') return (1);
	      if (*infop == '=') return (1);
	      if (*infop == '@') return (1);
	      if (*infop == '`') return (1);
	      break;

	    case 'o':				/* Object */
	      if (*infop == ';') return (1);
	      break;

	    case 'i':				/* Item */
	      if (*infop == ')') return (1);
	      break;

	    case 'm':				/* Message */
	      if (*infop == ':' && ! is_telem_metadata(infop)) return (1);
	      break;

	    case 'q':				/* Query */
	      if (*infop == '?') return (1);
	      break;

	    case 's':				/* Status */
	      if (*infop == '>') return (1);
	      break;

	    case 't':				/* Telemetry */
	      if (*infop == 'T') return (1);
	      if (is_telem_metadata(infop)) return (1);
	      break;

	    case 'u':				/* User-defined */
	      if (*infop == '{') return (1);
	      break;

	    case 'w':				/* Weather */
	      if (*infop == '@') return (1);
	      if (*infop == '*') return (1);
	      if (*infop == '_') return (1);

	      /* '$' is normally raw GPS. Check for special case. */
	      if (strncmp(infop, "$ULTW", 5) == 0) return (1);

	      /* TODO: Positions !=/@ can be weather. */
	      /* Need to check for _ symbol. */
	      break;

	    case 'n':				/* NWS format */
/*
 * This is the interesting case.
 * The source must be exactly 6 upper case letters, no SSID.
 */
	      if (strlen(src) != 6) break;
	      if (! isupper(src[0])) break;
	      if (! isupper(src[1])) break;
	      if (! isupper(src[2])) break;
	      if (! isupper(src[3])) break;
	      if (! isupper(src[4])) break;
	      if (! isupper(src[5])) break;
/*
 * We can have a "message" with addressee starting with NWS, SKY, or BOM (Australian version.)
 */
	      if (strncmp(infop, ":NWS", 4) == 0) return (1);
	      if (strncmp(infop, ":SKY", 4) == 0) return (1);
	      if (strncmp(infop, ":BOM", 4) == 0) return (1);
/*
 * Or we can have an object.
 * It's not exactly clear how to distiguish this from other objects.
 * It looks like the first 3 characters of the source should be the same
 * as the first 3 characters of the addressee.
 */
	      if (infop[0] == ';' &&
		  infop[1] == src[0] &&
		  infop[2] == src[1] &&
		  infop[3] == src[2]) return (1);
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
 * Returns:	 1 = yes
 *		 0 = no
 *		-1 = error detected
 *
 * Description:	
 *
 *------------------------------------------------------------------------------*/

static int filt_r (pfstate_t *pf)
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


	text_color_set (DW_COLOR_DEBUG);

	dw_printf ("Calculated distance = %.3f km\n", km);

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
 *		"pri" is zero or more symbols from the primary symbol set.
 *		"alt" is one or more symbols from the alternate symbol set.
 *		"over" is overlay characters.  Overlays apply only to the alternate symbol set.
 *		
 *		Examples:
 *			s/->		Allow house and car from primary symbol table.
 *			s//#		Allow alternate table digipeater, with or without overlay.
 *			s//#/\		Allow alternate table digipeater, only if no overlay.
 *			s//#/SL1	Allow alternate table digipeater, with overlay S, L, or 1
 * 
 *------------------------------------------------------------------------------*/

static int filt_s (pfstate_t *pf)
{
	char str[MAX_TOKEN_LEN];
	char *cp;
	char sep[2];
	char *pri, *alt, *over;


	strlcpy (str, pf->token_str, sizeof(str));
	sep[0] = str[1];
	sep[1] = '\0';
	cp = str + 2;

	pri = strsep (&cp, sep);
	if (pri == NULL) {
	  print_error (pf, "Missing arguments for Symbol filter.");
	  return (-1);
	}

	if (pf->decoded.g_symbol_table == '/' && strchr(pri, pf->decoded.g_symbol_code) != NULL) {
	  /* Found in primary symbols. All done. */
	  return (1);
	}

	alt = strsep (&cp, sep);
	if (alt == NULL) {
	  return (0);
	}
	if (strlen(alt) == 0) {
	  /* We have s/.../ */
	  print_error (pf, "Missing alternate symbols for Symbol filter.");
	  return (-1);
	}

	//printf ("alt=\"%s\"  sym='%c'\n", alt, pf->decoded.g_symbol_code);

	if (strchr(alt, pf->decoded.g_symbol_code) == NULL) {
	  /* Not found in alternate symbols. Reject. */
	  return (0);
	}

	over = strsep (&cp, sep);
	if (over == NULL) {
	  /* alternate, with or without overlay. */
	  return (pf->decoded.g_symbol_table != '/');
	}

	// printf ("over=\"%s\"  table='%c'\n", over, pf->decoded.g_symbol_table);

	if (strlen(over) == 0) {
	  return (pf->decoded.g_symbol_table == '\\');
	}

	return (strchr(over, pf->decoded.g_symbol_table) != NULL);
}


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

	if (pf->from_chan == MAX_CHANS) {

	  if (pf->to_chan == MAX_CHANS) {
	    snprintf (intro, sizeof(intro), "filter[IG,IG]: ");
	  }
	  else {
	    snprintf (intro, sizeof(intro), "filter[IG,%d]: ", pf->to_chan);
	  }
	}
	else {

	  if (pf->to_chan == MAX_CHANS) {
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

	/* Telemetry metadata is a special case of message. */
	pftest (114, "t/t", "KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 1);
	pftest (115, "t/m", "KJ4SNT>APMI04::KJ4SNT   :PARM.Vin,Rx1h,Dg1h,Eff1h,Rx10m,O1,O2,O3,O4,I1,I2,I3,I4", 0);
	pftest (116, "t/t", "KB1GKN-10>APRX27,UNCAN,WIDE1*:T#491,4.9,0.3,25.0,0.0,1.0,00000000", 1);


	pftest (120, "t/p", "CWAPID>APRS::NWS-TTTTT:DDHHMMz,ADVISETYPE,zcs{seq#", 0);
	pftest (122, "t/p", "CWAPID>APRS::SKYCWA   :DDHHMMz,ADVISETYPE,zcs{seq#", 0);
	pftest (123, "t/p", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 0);
	pftest (124, "t/n", "CWAPID>APRS::NWS-TTTTT:DDHHMMz,ADVISETYPE,zcs{seq#", 1);
	pftest (125, "t/n", "CWAPID>APRS::SKYCWA   :DDHHMMz,ADVISETYPE,zcs{seq#", 1);
	pftest (126, "t/n", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 1);
	pftest (127, "t/",  "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", 0);

	pftest (130, "r/42.6/-71.3/10", "WB2OSZ-5>APDW12,WIDE1-1,WIDE2-1:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (131, "r/42.6/-71.3/10", "WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", 0);

	pftest (140, "( t/t & b/WB2OSZ ) | ( t/o & ! r/42.6/-71.3/1 )", "WB2OSZ>APDW12:;home     *111111z4237.14N/07120.83W-Chelmsford MA", 1);

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

	pftest (170, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (171, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1*,DIGI2,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (172, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 1);
	pftest (173, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3*,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (174, "v/DIGI2/DIGI3", "WB2OSZ-5>APDW12,DIGI1,DIGI2,DIGI3,DIGI4*:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);
	pftest (175, "v/DIGI9/DIGI2", "WB2OSZ-5>APDW12,DIGI1,DIGI2*,DIGI3,DIGI4:!4237.14NS07120.83W#PHG7140Chelmsford MA", 0);

	/* Test error reporting. */

	pftest (200, "x/", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (201, "t/w & ( t/w | t/w ", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (202, "t/w ) ", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (203, "!", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (203, "t/w t/w", "CWAPID>APRS:;CWAttttz *DDHHMMzLATLONICONADVISETYPE{seq#", -1);
	pftest (204, "r/42.6/-71.3", "WA1PLE-5>APWW10,W1MHL,N8VIM,WIDE2*:@022301h4208.75N/07115.16WoAPRS-IS for Win32", -1);


	if (error_count > 0) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("\nPacket Filtering Test - FAILED!\n");
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

	result = pfilter (0, 0, filter, pp);
	if (result != expected) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Unexpected result for test number %d\n", test_num);
	  error_count++;
	}

	ax25_delete (pp);
}

#endif /* if TEST */

/* end pfilter.c */




//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011 , 2013, 2014  John Langner, WB2OSZ
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
 * Name:	ax25_pad
 *
 * Purpose:	Packet assembler and disasembler.
 *
 *   		We can obtain AX.25 packets from different sources:
 *		
 *		(a) from an HDLC frame.
 *		(b) from text representation.
 *		(c) built up piece by piece.
 *
 *		We also want to use a packet in different ways:
 *
 *		(a) transmit as an HDLC frame.
 *		(b) print in human-readable text.
 *		(c) take it apart piece by piece.
 *
 *		Looking at the more general case, we also want to modify
 *		an existing packet.  For instance an APRS repeater might 
 *		want to change "WIDE2-2" to "WIDE2-1" and retransmit it.
 *
 *
 * Description:	
 *
 *
 *	A UI frame starts with 2-10 addressses (14-70 octets):
 *
 *	* Destination Address
 *	* Source Address
 *	* 0-8 Digipeater Addresses  (Could there ever be more as a result of 
 *					digipeaters inserting their own call
 *					and decrementing the remaining count in
 *					WIDEn-n, TRACEn-n, etc.?   
 *					NO.  The limit is 8 when transmitting AX.25 over the radio.
 *					However, communication with an IGate server could have 
 *					a longer VIA path but that is only in text form, not here.)
 *
 *	Each address is composed of:
 *
 *	* 6 upper case letters or digits, blank padded.
 *		These are shifted left one bit, leaving the the LSB always 0.
 *	* a 7th octet containing the SSID and flags.
 *		The LSB is always 0 except for the last octet of the address field.
 *
 *	The final octet of the Destination has the form:
 *
 *		C R R SSID 0, where,
 *
 *			C = command/response = 1
 *			R R = Reserved = 1 1
 *			SSID = substation ID
 *			0 = zero
 *
 *	The final octet of the Source has the form:
 *
 *		C R R SSID 0, where,
 *
 *			C = command/response = 1
 *			R R = Reserved = 1 1
 *			SSID = substation ID
 *			0 = zero (or 1 if no repeaters)
 *
 *	The final octet of each repeater has the form:
 *
 *		H R R SSID 0, where,
 *
 *			H = has-been-repeated = 0 initially.  
 *				Set to 1 after this address has been used.
 *			R R = Reserved = 1 1
 *			SSID = substation ID
 *			0 = zero (or 1 if last repeater in list)
 *
 *		A digipeater would repeat this frame if it finds its address
 *		with the "H" bit set to 0 and all earlier repeater addresses
 *		have the "H" bit set to 1.  
 *		The "H" bit would be set to 1 in the repeated frame.
 *
 *	When monitoring, an asterisk is displayed after the last digipeater with 
 *	the "H" bit set.  No asterisk means the source is being heard directly.
 *
 *	Example, if we can hear all stations involved,
 *
 *		SRC>DST,RPT1,RPT2,RPT3:		-- we heard SRC
 *		SRC>DST,RPT1*,RPT2,RPT3:	-- we heard RPT1
 *		SRC>DST,RPT1,RPT2*,RPT3:	-- we heard RPT2
 *		SRC>DST,RPT1,RPT2,RPT3*:	-- we heard RPT3
 *
 *	
 *	Next we have:
 *
 *	* One byte Control Field 	- APRS uses 3 for UI frame
 *					   The more general AX.25 frame can have two.
 *
 *	* One byte Protocol ID 		- APRS uses 0xf0 for no layer 3
 *
 *	Finally the Information Field of 1-256 bytes.
 *
 *	And, of course, the 2 byte CRC.
 *
 *
 * Constructors: ax25_init		- Clear everything.
 *		ax25_from_text		- Tear apart a text string
 *		ax25_from_frame		- Tear apart an AX.25 frame.  
 *					  Must be called before any other function.
 *
 * Get methods:	....			- Extract destination, source, or digipeater
 *					  address from frame.
 *
 * Assumptions:	CRC has already been verified to be correct.
 *
 *------------------------------------------------------------------*/

#define AX25_PAD_C		/* this will affect behavior of ax25_pad.h */


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#ifndef _POSIX_C_SOURCE

#define _POSIX_C_SOURCE 1
#endif

#include "regex.h"

#if __WIN32__
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif

#include "ax25_pad.h"
#include "textcolor.h"
#include "fcs_calc.h"

/*
 * Accumulate statistics.
 * If new_count gets more than a few larger than delete_count plus the size of 
 * the transmit queue we have a memory leak.
 */

static int new_count = 0;
static int delete_count = 0;

#define CLEAR_LAST_ADDR_FLAG  this_p->frame_data[this_p->num_addr*7-1] &= ~ SSID_LAST_MASK
#define SET_LAST_ADDR_FLAG  this_p->frame_data[this_p->num_addr*7-1] |= SSID_LAST_MASK


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_new
 * 
 * Purpose:	Allocate memory for a new packet object.
 *
 * Returns:	Identifier for a new packet object.
 *		In the current implementation this happens to be a pointer.
 *
 *------------------------------------------------------------------------------*/


static packet_t ax25_new (void) 
{
	struct packet_s *this_p;


#if DEBUG
        text_color_set(DW_COLOR_DEBUG);
        dw_printf ("ax25_new(): before alloc, new=%d, delete=%d\n", new_count, delete_count);
#endif

	new_count++;

/*
 * check for memory leak.
 */
	if (new_count > delete_count + 100) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Memory leak for packet objects.  new=%d, delete=%d\n", new_count, delete_count);
	}

	this_p = calloc(sizeof (struct packet_s), (size_t)1);
	this_p->magic1 = MAGIC;
	this_p->magic2 = MAGIC;
	this_p->num_addr = (-1);
	return (this_p);
}

/*------------------------------------------------------------------------------
 *
 * Name:	ax25_delete
 * 
 * Purpose:	Destroy a packet object, freeing up memory it was using.
 *
 *------------------------------------------------------------------------------*/

void ax25_delete (packet_t this_p)
{
#if DEBUG
        text_color_set(DW_COLOR_DEBUG);
        dw_printf ("ax25_delete(): before free, new=%d, delete=%d\n", new_count, delete_count);
#endif
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	memset (this_p, 0, sizeof (struct packet_s));
	delete_count++;
	free (this_p);
}


		
/*------------------------------------------------------------------------------
 *
 * Name:	ax25_from_text
 * 
 * Purpose:	Parse a frame in human-readable monitoring format and change
 *		to internal representation.
 *
 * Input:	monitor	- "TNC-2" format of a monitored packet.  i.e.
 *				source>dest[,repeater1,repeater2,...]:information
 *
 *		strict	- True to enforce rules for packets sent over the air.
 *			  False to be more lenient for packets from IGate server.
 *
 *			  Messages from an IGate server can have longer 
 *		 	  addresses after qAC.  Up to 9 observed so far. 
 *
 *			  We can just truncate the name because we will only
 *			  end up discarding it.    TODO:  check on this.
 *
 * Returns:	Pointer to new packet object in the current implementation.
 *
 * Outputs:	Use the "get" functions to retrieve information in different ways.
 *
 *------------------------------------------------------------------------------*/

packet_t ax25_from_text (char *monitor, int strict)
{

/*
 * Tearing it apart is destructive so make our own copy first.
 */
	char stuff[512];

	char *pinfo;
	char *pa;
	char *saveptr;		/* Used with strtok_r because strtok is not thread safe. */

	static int first_time = 1;
	static regex_t unhex_re;
	int e;
	char emsg[100];
#define MAXMATCH 1
	regmatch_t match[MAXMATCH];
	int keep_going;
	char temp[512];
	int ssid_temp, heard_temp;
	char atemp[AX25_MAX_ADDR_LEN];

	
	packet_t this_p = ax25_new ();

	/* Is it possible to have a nul character (zero byte) in the */
	/* information field of an AX.25 frame? */
	/* Yes, but it would be difficult in the from-text case. */

	strcpy (stuff, monitor);

/* 
 * Translate hexadecimal values like <0xff> to non-printing characters.
 * MIC-E message type uses 5 different non-printing characters.
 */

	if (first_time) 
	{
	  e = regcomp (&unhex_re, "<0x[0-9a-fA-F][0-9a-fA-F]>", 0);
	  if (e) {
	    regerror (e, &unhex_re, emsg, sizeof(emsg));
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%s:%d: %s\n", __FILE__, __LINE__, emsg);
	  }

	  first_time = 0;
	}

#if 0
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("BEFORE: %s\n", stuff);
	ax25_safe_print (stuff, -1, 0);
	dw_printf ("\n");
#endif
	keep_going = 1;
	while (keep_going) {
	  if (regexec (&unhex_re, stuff, MAXMATCH, match, 0) == 0) {
	    int n;
	    char *p;
  
	    stuff[match[0].rm_so + 5] = '\0';
	    n = strtol (stuff + match[0].rm_so + 3, &p, 16);
	    stuff[match[0].rm_so] = n;
	    strcpy (temp, stuff + match[0].rm_eo);
	    strcpy (stuff + match[0].rm_so + 1, temp);
	  }
	  else {
	    keep_going = 0;
	  }
	}
#if 0
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("AFTER:  %s\n", stuff);
	ax25_safe_print (stuff, -1, 0);
	dw_printf ("\n");
#endif

/*
 * Initialize the packet with two addresses and control/pid
 * for APRS.
 */
	memset (this_p->frame_data + AX25_DESTINATION*7, ' ' << 1, 6);
	this_p->frame_data[AX25_DESTINATION*7+6] = SSID_H_MASK | SSID_RR_MASK;
 
	memset (this_p->frame_data + AX25_SOURCE*7, ' ' << 1, 6);
	this_p->frame_data[AX25_SOURCE*7+6] = SSID_H_MASK | SSID_RR_MASK | SSID_LAST_MASK;

	this_p->frame_data[14] = AX25_UI_FRAME;
	this_p->frame_data[15] = AX25_NO_LAYER_3;

	this_p->frame_len = 7 + 7 + 1 + 1;
	this_p->num_addr = (-1);
	assert (ax25_get_num_addr(this_p) == 2);


/*
 * Separate the addresses from the rest.
 */
	pinfo = strchr (stuff, ':');

	if (pinfo == NULL) {
	  ax25_delete (this_p);
	  return (NULL);
	}

	*pinfo = '\0';
	pinfo++;

	if (strlen(pinfo) > AX25_MAX_INFO_LEN) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Warning: Information part truncated to %d characters.\n", AX25_MAX_INFO_LEN);
	  pinfo[AX25_MAX_INFO_LEN] = '\0';
	}
	
/*
 * Separate the addresses.
 * Note that source and destination order is swappped.
 */

/*
 * Source address.
 * Don't use traditional strtok because it is not thread safe.
 */
	pa = strtok_r (stuff, ">", &saveptr);
	if (pa == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  No source address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	if ( ! ax25_parse_addr (pa, strict, atemp, &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad source address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	ax25_set_addr (this_p, AX25_SOURCE, pa);
	ax25_set_h (this_p, AX25_SOURCE);	// c/r in this position

	ax25_set_ssid (this_p, AX25_SOURCE, ssid_temp);

/*
 * Destination address.
 */
 
	pa = strtok_r (NULL, ",", &saveptr);
	if (pa == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  No destination address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	if ( ! ax25_parse_addr (pa, strict, atemp, &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad destination address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	ax25_set_addr (this_p, AX25_DESTINATION, pa);
	ax25_set_h (this_p, AX25_DESTINATION);	// c/r in this position

	ax25_set_ssid (this_p, AX25_DESTINATION, ssid_temp);

/*
 * VIA path.
 */
	while (( pa = strtok_r (NULL, ",", &saveptr)) != NULL && this_p->num_addr < AX25_MAX_ADDRS ) {

	  //char *last;
	  int k;

	  k = this_p->num_addr;

	  // JWL 10:38 this_p->num_addr++;

	  if ( ! ax25_parse_addr (pa, strict, atemp, &ssid_temp, &heard_temp)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to create packet from text.  Bad digipeater address\n");
	    ax25_delete (this_p);
	    return (NULL);
	  }

	  ax25_set_addr (this_p, k, pa);
	  
	  ax25_set_ssid (this_p, k, ssid_temp);

	  // Does it have an "*" at the end? 
	  // TODO: Complain if more than one "*".
	  // Could also check for all has been repeated bits are adjacent.
	
          if (heard_temp) {
	    for ( ; k >= AX25_REPEATER_1; k--) {
	      ax25_set_h (this_p, k);
	    }
	  }
        }

/*
 * Append the info part.  
 */
	strcpy ((char*)(this_p->frame_data+this_p->frame_len), pinfo);
	this_p->frame_len += strlen(pinfo);


	return (this_p);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_from_frame
 * 
 * Purpose:	Split apart an HDLC frame to components.
 *
 * Inputs:	fbuf	- Pointer to beginning of frame.
 *
 *		flen	- Length excluding the two FCS bytes.
 *
 *		alevel	- Audio level of received signal.  
 *			  Maximum range 0 - 100.
 *			  -1 might be used when not applicable.
 *
 * Returns:	Pointer to new packet object or NULL if error.
 *
 * Outputs:	Use the "get" functions to retrieve information in different ways.
 *
 *------------------------------------------------------------------------------*/


packet_t ax25_from_frame (unsigned char *fbuf, int flen, int alevel)
{
	unsigned char *pf;
	//int found_last;
	packet_t this_p;

	int a;
	int addr_bytes;
	
/*
 * First make sure we have an acceptable length:
 *
 *	We are not concerned with the FCS (CRC) because someone else checked it.
 *
 * Is is possible to have zero length for info?  
 *
 * In the original version, assuming APRS, the answer was no.
 * We always had at least 3 octets after the address part:
 * control, protocol, and first byte of info part for data type.
 *
 * In later versions, this restriction was relaxed so other
 * variations of AX.25 could be used.  Now the minimum length
 * is 7+7 for addresses plus 1 for control.
 *
 */

	if (flen < AX25_MIN_PACKET_LEN || flen > AX25_MAX_PACKET_LEN)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Frame length %d not in allowable range of %d to %d.\n", flen, AX25_MIN_PACKET_LEN, AX25_MAX_PACKET_LEN);
	  return (NULL);
	}

	this_p = ax25_new ();

/* Copy the whole thing intact. */

	memcpy (this_p->frame_data, fbuf, flen);
	this_p->frame_data[flen] = 0;
	this_p->frame_len = flen;

/* Find number of addresses. */
	
	this_p->num_addr = (-1);
	(void) ax25_get_num_addr (this_p);

	return (this_p);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_dup
 * 
 * Purpose:	Make a copy of given packet object.
 *
 * Inputs:	copy_from	- Existing packet object.
 *
 * Returns:	Pointer to new packet object or NULL if error.
 *
 *
 *------------------------------------------------------------------------------*/


packet_t ax25_dup (packet_t copy_from)
{

	packet_t this_p;

	
	this_p = ax25_new ();

	memcpy (this_p, copy_from, sizeof (struct packet_s));

	return (this_p);

}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_parse_addr
 * 
 * Purpose:	Parse address with optional ssid.
 *
 * Inputs:	in_addr		- Input such as "WB2OSZ-15*"
 *
 * 		strict		- TRUE for strict checking (6 characters, no lower case,
 *				  SSID must be in range of 0 to 15).
 *				  Strict is appropriate for packets sent
 *				  over the radio.  Communication with IGate
 *				  allows lower case (e.g. "qAR") and two 
 *				  alphanumeric characters for the SSID.
 *				  We also get messages like this from a server.
 *					KB1POR>APU25N,TCPIP*,qAC,T2NUENGLD:...
 *
 * Outputs:	out_addr	- Address without any SSID.
 *				  Must be at least AX25_MAX_ADDR_LEN bytes.
 *
 *		out_ssid	- Numeric value of SSID.
 *
 *		out_heard	- True if "*" found.
 *
 * Returns:	True (1) if OK, false (0) if any error.
 *
 *
 *------------------------------------------------------------------------------*/


int ax25_parse_addr (char *in_addr, int strict, char *out_addr, int *out_ssid, int *out_heard)
{
	char *p;
	char sstr[4];
	int i, j, k;
	int maxlen;

	strcpy (out_addr, "");
	*out_ssid = 0;
	*out_heard = 0;

	//dw_printf ("ax25_parse_addr in: %s\n", in_addr);

	maxlen = strict ? 6 : (AX25_MAX_ADDR_LEN-1);
	p = in_addr;
	i = 0;
	for (p = in_addr; isalnum(*p); p++) {
	  if (i >= maxlen) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Address is too long. \"%s\" has more than %d characters.\n", in_addr, maxlen);
	    return 0;
	  }
	  out_addr[i++] = *p;
	  out_addr[i] = '\0';
	  if (strict && islower(*p)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Address has lower case letters. \"%s\" must be all upper case.\n", in_addr);
	    return 0;
	  }
	}
	
	strcpy (sstr, "");
	j = 0;
	if (*p == '-') {
	  for (p++; isalnum(*p); p++) {
	    if (j >= 2) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("SSID is too long. SSID part of \"%s\" has more than 2 characters.\n", in_addr);
	      return 0;
	    }
	    sstr[j++] = *p;
	    sstr[j] = '\0';
	    if (strict && ! isdigit(*p)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("SSID must be digits. \"%s\" has letters in SSID.\n", in_addr);
	      return 0;
	    }
	  }
	  k = atoi(sstr);
	  if (k < 0 || k > 15) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("SSID out of range. SSID of \"%s\" not in range of 0 to 15.\n", in_addr);
	    return 0;
	  }
	  *out_ssid = k;
	}

	if (*p == '*') {
	  *out_heard = 1;
	  p++;
	}

	if (*p != '\0') {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Invalid character \"%c\" found in address \"%s\".\n", *p, in_addr);
	  return 0;
	}

	//dw_printf ("ax25_parse_addr out: %s %d %d\n", out_addr, *out_ssid, *out_heard);

	return (1);

} /* end ax25_parse_addr */


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_unwrap_third_party
 * 
 * Purpose:	Unwrap a third party messge from the header.
 *
 * Inputs:	copy_from	- Existing packet object.
 *
 * Returns:	Pointer to new packet object or NULL if error.
 *
 * Example:	Input:		A>B,C:}D>E,F:info
 *		Output:		D>E,F:info
 *
 *------------------------------------------------------------------------------*/

packet_t ax25_unwrap_third_party (packet_t from_pp)
{
	unsigned char *info_p;
	packet_t result_pp;

	if (ax25_get_dti(from_pp) != '}') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_unwrap_third_party: wrong data type.\n");
	  return (NULL);
	}

	(void) ax25_get_info (from_pp, &info_p);

	result_pp = ax25_from_text((char *)info_p + 1, 0);

	return (result_pp);
}



/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_addr
 * 
 * Purpose:	Add or change an address.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 *			  Must be either an existing address or one greater
 *			  than the final which causes a new one to be added.
 *
 *		ad	- Address with optional dash and substation id.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * TODO:  	ax25_from_text could use this.
 *
 * Returns:	None.
 *		
 *------------------------------------------------------------------------------*/

void ax25_set_addr (packet_t this_p, int n, char *ad)
{
	int ssid_temp, heard_temp;
	char atemp[AX25_MAX_ADDR_LEN];
	int i;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= 0 && n < AX25_MAX_ADDRS);
	assert (strlen(ad) < AX25_MAX_ADDR_LEN);

	//dw_printf ("ax25_set_addr (%d, %s) num_addr=%d\n", n, ad, this_p->num_addr);

	if (n >= 0 && n < this_p->num_addr) {

	  //dw_printf ("ax25_set_addr , existing case\n");
/* 
 * Set existing address position. 
 */
	  ax25_parse_addr (ad, 0, atemp, &ssid_temp, &heard_temp);

	  memset (this_p->frame_data + n*7, ' ' << 1, 6);

	  for (i=0; i<6 && atemp[i] != '\0'; i++) {
	    this_p->frame_data[n*7+i] = atemp[i] << 1;
	  }
	  ax25_set_ssid (this_p, n, ssid_temp);
	}
	else if (n == this_p->num_addr) {		

	  //dw_printf ("ax25_set_addr , appending case\n");
/* 
 * One beyond last position, process as insert.
 */

	  ax25_insert_addr (this_p, n, ad);
	}
	else { 
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error, ax25_set_addr, bad position %d for '%s'\n", n, ad);
	}

	//dw_printf ("------\n");
	//dw_printf ("dump after ax25_set_addr (%d, %s)\n", n, ad);
	//ax25_hex_dump (this_p);
	//dw_printf ("------\n");
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_insert_addr
 * 
 * Purpose:	Insert address at specified position, shifting others up one
 *		position.
 *		This is used when a digipeater wants to insert its own call
 *		for tracing purposes.
 *		For example:
 *			W1ABC>TEST,WIDE3-3
 *		Would become:
 *			W1ABC>TEST,WB2OSZ-1*,WIDE3-2
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 *		ad	- Address with optional dash and substation id.
 *
 * Bugs:	Little validity or bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	None.
 *		
 *
 *------------------------------------------------------------------------------*/

void ax25_insert_addr (packet_t this_p, int n, char *ad)
{
	int k;
	int ssid_temp, heard_temp;
	char atemp[AX25_MAX_ADDR_LEN];
	int i;
	int expect;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= AX25_REPEATER_1 && n < AX25_MAX_ADDRS);
	assert (strlen(ad) < AX25_MAX_ADDR_LEN);

	//dw_printf ("ax25_insert_addr (%d, %s)\n", n, ad);

	/* Don't do it if we already have the maximum number. */
	/* Should probably return success/fail code but currently the caller doesn't care. */

	if ( this_p->num_addr >= AX25_MAX_ADDRS) {
	  return;
	}

	CLEAR_LAST_ADDR_FLAG;

	this_p->num_addr++;

	memmove (this_p->frame_data + (n+1)*7, this_p->frame_data + n*7, this_p->frame_len - (n*7));
	memset (this_p->frame_data + n*7, ' ' << 1, 6);
	this_p->frame_len += 7;
	this_p->frame_data[n*7+6] = SSID_RR_MASK;

	SET_LAST_ADDR_FLAG;

	ax25_parse_addr (ad, 0, atemp, &ssid_temp, &heard_temp);
	memset (this_p->frame_data + n*7, ' ' << 1, 6);
	for (i=0; i<6 && atemp[i] != '\0'; i++) {
	  this_p->frame_data[n*7+i] = atemp[i] << 1;
	}
	
	ax25_set_ssid (this_p, n, ssid_temp);

	// Sanity check after messing with number of addresses.

	expect = this_p->num_addr;
	this_p->num_addr = (-1);
	if (expect != ax25_get_num_addr (this_p)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error ax25_remove_addr expect %d, actual %d\n", expect, this_p->num_addr);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_remove_addr
 * 
 * Purpose:	Remove address at specified position, shifting others down one position.
 *		This is used when we want to remove something from the digipeater list.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_REPEATER1, AX25_REPEATER2, etc.
 *
 * Bugs:	Little validity or bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	None.
 *		
 *
 *------------------------------------------------------------------------------*/

void ax25_remove_addr (packet_t this_p, int n)
{
	int k;
	int expect; 

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= AX25_REPEATER_1 && n < AX25_MAX_ADDRS);

	/* Shift those beyond to fill this position. */

	CLEAR_LAST_ADDR_FLAG;

	this_p->num_addr--;

	memmove (this_p->frame_data + n*7, this_p->frame_data + (n+1)*7, this_p->frame_len - ((n+1)*7));
	this_p->frame_len -= 7;
	SET_LAST_ADDR_FLAG;

	// Sanity check after messing with number of addresses.

	expect = this_p->num_addr;
	this_p->num_addr = (-1);
	if (expect != ax25_get_num_addr (this_p)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error ax25_remove_addr expect %d, actual %d\n", expect, this_p->num_addr);
	}

}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_num_addr
 * 
 * Purpose:	Return number of addresses in current packet.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Number of addresses in the current packet.
 *		Should be in the range of 2 .. AX25_MAX_ADDRS.
 *
 * Version 0.9:	Could be zero for a non AX.25 frame in KISS mode.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_num_addr (packet_t this_p)
{
	unsigned char *pf;
	int a;
	int addr_bytes;


	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

/* Use cached value if already set. */

	if (this_p->num_addr >= 0) {
	  return (this_p->num_addr);
	}

/* Otherwise, determine the number ofaddresses. */

	this_p->num_addr = 0;		/* Number of addresses extracted. */
	
	addr_bytes = 0;
	for (a = 0; a < this_p->frame_len && addr_bytes == 0; a++) {
	  if (this_p->frame_data[a] & SSID_LAST_MASK) {
	    addr_bytes = a + 1;
	  }
	}

	if (addr_bytes % 7 == 0) {
	  int addrs = addr_bytes / 7;
	  if (addrs >= AX25_MIN_ADDRS && addrs <= AX25_MAX_ADDRS) {
	    this_p->num_addr = addrs;
	  }
	}
	
	return (this_p->num_addr);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_num_repeaters
 * 
 * Purpose:	Return number of repeater addresses in current packet.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Number of addresses in the current packet - 2.
 *		Should be in the range of 0 .. AX25_MAX_ADDRS - 2.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_num_repeaters (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  return (this_p->num_addr - 2);
	}

	return (0);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_addr_with_ssid
 * 
 * Purpose:	Return specified address with any SSID in current packet.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 * Outputs:	station - String representation of the station, including the SSID.
 *			e.g.  "WB2OSZ-15"
 *
 * Bugs:	No bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Character string in usual human readable format,
 *		
 *
 *------------------------------------------------------------------------------*/

void ax25_get_addr_with_ssid (packet_t this_p, int n, char *station)
{	
	int ssid;
	char sstr[4];
	int i;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);


	if (n < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_addr_with_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is less than zero.\n", n);
	  strcpy (station, "??????");
	  return;
	}

	if (n >= this_p->num_addr) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_addr_with_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is too large for number of addresses, %d.\n", n, this_p->num_addr);
	  strcpy (station, "??????");
	  return;
	}

	memset (station, 0, 7);
	for (i=0; i<6; i++) {
	  unsigned char ch;

	  ch = (this_p->frame_data[n*7+i] >> 1) & 0x7f;
	  if (ch <= ' ') break;
	  station[i] = ch;
	}

	ssid = ax25_get_ssid (this_p, n);
	if (ssid != 0) {
	  sprintf (sstr, "-%d", ssid);
	  strcat (station, sstr);
	}   
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_ssid
 * 
 * Purpose:	Return SSID of specified address in current packet.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Substation id, as integer 0 .. 15.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_ssid (packet_t this_p, int n)
{
	
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	
	if (n >= 0 && n < this_p->num_addr) {
	  return ((this_p->frame_data[n*7+6] & SSID_SSID_MASK) >> SSID_SSID_SHIFT);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_get_ssid(%d), num_addr=%d\n", n, this_p->num_addr);
	  return (0);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_ssid
 * 
 * Purpose:	Set the SSID of specified address in current packet.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 *		ssid	- New SSID.  Must be in range of 0 to 15.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Bugs:	Rewrite to keep call and SSID separate internally.
 *
 *------------------------------------------------------------------------------*/

void ax25_set_ssid (packet_t this_p, int n, int ssid)
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);


	if (n >= 0 && n < this_p->num_addr) {
	  this_p->frame_data[n*7+6] =   (this_p->frame_data[n*7+6] & ~ SSID_SSID_MASK) |
		((ssid << SSID_SSID_SHIFT) & SSID_SSID_MASK) ;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_set_ssid(%d,%d), num_addr=%d\n", n, ssid, this_p->num_addr);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_h
 * 
 * Purpose:	Return "has been repeated" flag of specified address in current packet.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 * Bugs:	No bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	True or false.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_h (packet_t this_p, int n)
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= 0 && n < this_p->num_addr);

	if (n >= 0 && n < this_p->num_addr) {
	  return ((this_p->frame_data[n*7+6] & SSID_H_MASK) >> SSID_H_SHIFT);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_get_h(%d), num_addr=%d\n", n, this_p->num_addr);
	  return (0);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_h
 * 
 * Purpose:	Set the "has been repeated" flag of specified address in current packet.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			 Should be in range of AX25_REPEATER_1 .. AX25_REPEATER_8.
 *
 * Bugs:	No bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	None
 *
 *------------------------------------------------------------------------------*/

void ax25_set_h (packet_t this_p, int n)
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (n >= 0 && n < this_p->num_addr) {
	  this_p->frame_data[n*7+6] |= SSID_H_MASK;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_set_hd(%d), num_addr=%d\n", n, this_p->num_addr);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_heard
 * 
 * Purpose:	Return index of the station that we heard.
 *		
 * Inputs:	none
 *
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	If any of the digipeaters have the has-been-repeated bit set, 
 *		return the index of the last one.  Otherwise return index for source.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_heard(packet_t this_p)
{
	int i;
	int result;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	result = AX25_SOURCE;

	for (i = AX25_REPEATER_1; i < ax25_get_num_addr(this_p); i++) {
	
	  if (ax25_get_h(this_p,i)) {
	    result = i;
	  }
	}
	return (result);
}



/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_first_not_repeated
 * 
 * Purpose:	Return index of the first repeater that does NOT have the 
 *		"has been repeated" flag set or -1 if none.
 *
 * Inputs:	none
 *
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	In range of X25_REPEATER_1 .. X25_REPEATER_8 or -1 if none.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_first_not_repeated(packet_t this_p)
{
	int i;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	for (i = AX25_REPEATER_1; i < ax25_get_num_addr(this_p); i++) {
	
	  if ( ! ax25_get_h(this_p,i)) {
	    return (i);
	  }
	}
	return (-1);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_info
 * 
 * Purpose:	Obtain Information part of current packet.
 *
 * Inputs:	None.
 *
 * Outputs:	paddr	- Starting address is returned here.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Number of octets in the Information part.
 *		Should be in the range of AX25_MIN_INFO_LEN .. AX25_MAX_INFO_LEN.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_info (packet_t this_p, unsigned char **paddr)
{
	unsigned char *info_ptr;
	int info_len;


	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {

	  /* AX.25 */

	  info_ptr = this_p->frame_data + ax25_get_info_offset(this_p);
	  info_len = ax25_get_num_info(this_p);
	}
	else {

	  /* Not AX.25.  Treat Whole packet as info. */

	  info_ptr = this_p->frame_data;
	  info_len = this_p->frame_len;
	}

	/* Add nul character in case caller treats as printable string. */
	
	assert (info_len >= 0);

	info_ptr[info_len] = '\0';

	*paddr = info_ptr;
	return (info_len);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_dti
 * 
 * Purpose:	Get Data Type Identifier from Information part.
 *
 * Inputs:	None.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	First byte from the information part.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_dti (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  return (this_p->frame_data[ax25_get_info_offset(this_p)]);
	}
	return (' ');
}

/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_nextp
 * 
 * Purpose:	Set next packet object in queue.
 *
 * Inputs:	this_p		- Current packet object.
 *
 *		next_p		- pointer to next one
 *
 * Description:	This is used to build a linked list for a queue.
 *
 *------------------------------------------------------------------------------*/

void ax25_set_nextp (packet_t this_p, packet_t next_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	
	this_p->nextp = next_p;
}



/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_nextp
 * 
 * Purpose:	Obtain next packet object in queue.
 *
 * Inputs:	Packet object.
 *
 * Returns:	Following object in queue or NULL.
 *
 *------------------------------------------------------------------------------*/

packet_t ax25_get_nextp (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	
	return (this_p->nextp);
}



/*------------------------------------------------------------------
 *
 * Function:	ax25_format_addrs
 *
 * Purpose:	Format all the addresses suitable for printing.
 *
 *		The AX.25 spec refers to this as "Source Path Header" - "TNC-2" Format
 *
 * Inputs:	Current packet.
 *		
 * Outputs:	result	- All addresses combined into a single string of the form:
 *
 *				"Source > Destination [ , repeater ... ] :"
 *
 *			An asterisk is displayed after the last digipeater 
 *			with the "H" bit set.  e.g.  If we hear RPT2, 
 *
 *			SRC>DST,RPT1,RPT2*,RPT3:
 *
 *			No asterisk means the source is being heard directly.
 *			Needs to be 101 characters to avoid overflowing.
 *			(Up to 100 characters + \0)
 *
 * Errors:	No error checking so caller needs to be careful.
 *
 *
 *------------------------------------------------------------------*/

void ax25_format_addrs (packet_t this_p, char *result)
{
	int i;
	int heard;
	char stemp[AX25_MAX_ADDR_LEN];

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	*result = '\0';

	/* New in 0.9. */
	/* Don't get upset if no addresses.  */
	/* This will allow packets that do not comply to AX.25 format. */

	if (this_p->num_addr == 0) {
	  return;
	}

	ax25_get_addr_with_ssid (this_p, AX25_SOURCE, stemp);
	strcat (result, stemp);
	strcat (result, ">");

	ax25_get_addr_with_ssid (this_p, AX25_DESTINATION, stemp);
	strcat (result, stemp);

	heard = ax25_get_heard(this_p);

	for (i=(int)AX25_REPEATER_1; i<this_p->num_addr; i++) {
	  ax25_get_addr_with_ssid (this_p, i, stemp);
	  strcat (result, ",");
	  strcat (result, stemp);
	  if (i == heard) {
	    strcat (result, "*");
	  }
	}
	
	strcat (result, ":");
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_pack
 *
 * Purpose:	Put all the pieces into format ready for transmission.
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Outputs:	result		- Frame buffer, AX25_MAX_PACKET_LEN bytes.
 *				Should also have two extra for FCS to be
 *				added later.
 *
 * Returns:	Number of octets in the frame buffer.  
 *		Does NOT include the extra 2 for FCS.
 *
 * Errors:	Returns -1.
 *
 *------------------------------------------------------------------*/

int ax25_pack (packet_t this_p, unsigned char result[AX25_MAX_PACKET_LEN]) 
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	assert (this_p->frame_len > 0 && this_p->frame_len <= AX25_MAX_PACKET_LEN);

	memcpy (result, this_p->frame_data, this_p->frame_len);

	return (this_p->frame_len);
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_hex_dump
 *
 * Purpose:	Print out packet in hexadecimal for debugging.
 *
 * Inputs:	fptr		- Pointer to frame data.
 *
 *		flen		- Frame length, bytes.  Does not include CRC.
 *		
 *------------------------------------------------------------------*/

static void hex_dump (unsigned char *p, int len) 
{
	int n, i, offset;

	offset = 0;
	while (len > 0) {
	  n = len < 16 ? len : 16; 
	  dw_printf ("  %03x: ", offset);
	  for (i=0; i<n; i++) {
	    dw_printf (" %02x", p[i]);
	  }
	  for (i=n; i<16; i++) {
	    dw_printf ("   ");
	  }
	  dw_printf ("  ");
	  for (i=0; i<n; i++) {
	    dw_printf ("%c", isprint(p[i]) ? p[i] : '.');
	  }
	  dw_printf ("\n");
	  p += 16;
	  offset += 16;
	  len -= 16;
	}
}

/* Text description of control octet. */

static void ctrl_to_text (int c, char *out)
{
	if      ((c & 1) == 0)       { sprintf (out, "I frame: n(r)=%d, p=%d, n(s)=%d",  (c>>5)&7, (c>>4)&1, (c>>1)&7); }
	else if ((c & 0xf) == 0x01)  { sprintf (out, "S frame RR: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x05)  { sprintf (out, "S frame RNR: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x09)  { sprintf (out, "S frame REJ: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x0D)  { sprintf (out, "S frame sREJ: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xef) == 0x6f) { sprintf (out, "U frame SABME: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x2f) { sprintf (out, "U frame SABM: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x43) { sprintf (out, "U frame DISC: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x0f) { sprintf (out, "U frame DM: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x63) { sprintf (out, "U frame UA: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x87) { sprintf (out, "U frame FRMR: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x03) { sprintf (out, "U frame UI: p/f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0xAF) { sprintf (out, "U frame XID: p/f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0xe3) { sprintf (out, "U frame TEST: p/f=%d", (c>>4)&1); }
	else                         { sprintf (out, "Unknown frame type for control = 0x%02x", c); }
}

/* Text description of protocol id octet. */

static void pid_to_text (int p, char *out)
{

	if      ((p & 0x30) == 0x10) { sprintf (out, "AX.25 layer 3 implemented."); }
	else if ((p & 0x30) == 0x20) { sprintf (out, "AX.25 layer 3 implemented."); }
	else if (p == 0x01)          { sprintf (out, "ISO 8208/CCITT X.25 PLP"); }
	else if (p == 0x06)          { sprintf (out, "Compressed TCP/IP packet. Van Jacobson (RFC 1144)"); }
	else if (p == 0x07)          { sprintf (out, "Uncompressed TCP/IP packet. Van Jacobson (RFC 1144)"); }
	else if (p == 0x08)          { sprintf (out, "Segmentation fragment"); }
	else if (p == 0xC3)          { sprintf (out, "TEXNET datagram protocol"); }
	else if (p == 0xC4)          { sprintf (out, "Link Quality Protocol"); }
	else if (p == 0xCA)          { sprintf (out, "Appletalk"); }
	else if (p == 0xCB)          { sprintf (out, "Appletalk ARP"); }
	else if (p == 0xCC)          { sprintf (out, "ARPA Internet Protocol"); }
	else if (p == 0xCD)          { sprintf (out, "ARPA Address resolution"); }
	else if (p == 0xCE)          { sprintf (out, "FlexNet"); }
	else if (p == 0xCF)          { sprintf (out, "NET/ROM"); }
	else if (p == 0xF0)          { sprintf (out, "No layer 3 protocol implemented."); }
	else if (p == 0xFF)          { sprintf (out, "Escape character. Next octet contains more Level 3 protocol information."); }
	else                         { sprintf (out, "Unknown protocol id = 0x%02x", p); }
}



void ax25_hex_dump (packet_t this_p) 
{
	int n;
	unsigned char *fptr = this_p->frame_data;
	int flen = this_p->frame_len;


	
	if (this_p->num_addr >= AX25_MIN_ADDRS && this_p->num_addr <= AX25_MAX_ADDRS) {
	  int c, p;
	  char cp_text[120];
	  char l_text[20];

	  c = fptr[this_p->num_addr*7];
	  p = fptr[this_p->num_addr*7+1];

	  ctrl_to_text (c, cp_text);

	  if ( (c & 0x01) == 0 ||				/* I   xxxx xxx0 */
	     	c == 0x03 || c == 0x13) {			/* UI  000x 0011 */

	    char p_text[100];

	    pid_to_text (p, p_text);

	    strcat (cp_text, ", ");
	    strcat (cp_text, p_text);

	  }

	  sprintf (l_text, ", length = %d", flen);
	  strcat (cp_text, l_text);

	  dw_printf ("%s\n", cp_text);
	}


	dw_printf (" dest    %c%c%c%c%c%c %2d c/r=%d res=%d last=%d\n", 
				fptr[0]>>1, fptr[1]>>1, fptr[2]>>1, fptr[3]>>1, fptr[4]>>1, fptr[5]>>1,
				(fptr[6]&SSID_SSID_MASK)>>SSID_SSID_SHIFT,
				(fptr[6]&SSID_H_MASK)>>SSID_H_SHIFT, 
				(fptr[6]&SSID_RR_MASK)>>SSID_RR_SHIFT,
				fptr[6]&SSID_LAST_MASK);

	dw_printf (" source  %c%c%c%c%c%c %2d c/r=%d res=%d last=%d\n", 
				fptr[7]>>1, fptr[8]>>1, fptr[9]>>1, fptr[10]>>1, fptr[11]>>1, fptr[12]>>1,
				(fptr[13]&SSID_SSID_MASK)>>SSID_SSID_SHIFT,
				(fptr[13]&SSID_H_MASK)>>SSID_H_SHIFT, 
				(fptr[13]&SSID_RR_MASK)>>SSID_RR_SHIFT,
				fptr[13]&SSID_LAST_MASK);

	for (n=2; n<this_p->num_addr; n++) {	

	  dw_printf (" digi %d  %c%c%c%c%c%c %2d   h=%d res=%d last=%d\n", 
				n - 1,
				fptr[n*7+0]>>1, fptr[n*7+1]>>1, fptr[n*7+2]>>1, fptr[n*7+3]>>1, fptr[n*7+4]>>1, fptr[n*7+5]>>1,
				(fptr[n*7+6]&SSID_SSID_MASK)>>SSID_SSID_SHIFT,
				(fptr[n*7+6]&SSID_H_MASK)>>SSID_H_SHIFT, 
				(fptr[n*7+6]&SSID_RR_MASK)>>SSID_RR_SHIFT,
				fptr[n*7+6]&SSID_LAST_MASK);

	}

	hex_dump (fptr, flen);

} /* end ax25_hex_dump */



/*------------------------------------------------------------------
 *
 * Function:	ax25_is_aprs
 *
 * Purpose:	Is this packet APRS format?
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Returns:	True if this frame has the proper control
 *		octets for an APRS packet.
 *			control		3 for UI frame
 *			protocol id	0xf0 for no layer 3
 *
 *
 * Description:	Dire Wolf should be able to act as a KISS TNC for
 *		any type of AX.25 activity.  However, there are other
 *		places where we want to process only APRS.
 *		(e.g. digipeating and IGate.)
 *
 *------------------------------------------------------------------*/


int ax25_is_aprs (packet_t this_p) 
{
	int ctrl, pid, is_aprs;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	ctrl = ax25_get_control(this_p);
	pid = ax25_get_pid(this_p);

	is_aprs = this_p->num_addr >= 2 && ctrl == AX25_UI_FRAME && pid == AX25_NO_LAYER_3;

#if 0 
        text_color_set(DW_COLOR_ERROR);
        dw_printf ("ax25_is_aprs(): ctrl=%02x, pid=%02x, is_aprs=%d\n", ctrl, pid, is_aprs);
#endif
	return (is_aprs);
}

/*------------------------------------------------------------------
 *
 * Function:	ax25_get_control
 *
 * Purpose:	Get Control field from packet.
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Returns:	APRS uses AX25_UI_FRAME.
 *		This could also be used in other situations.
 *
 *------------------------------------------------------------------*/


int ax25_get_control (packet_t this_p) 
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  return (this_p->frame_data[ax25_get_control_offset(this_p)]);
	}
	return (-1);
}

/*------------------------------------------------------------------
 *
 * Function:	ax25_get_pid
 *
 * Purpose:	Get protocol ID from packet.
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Returns:	APRS uses 0xf0 for no layer 3.
 *		This could also be used in other situations.
 *
 * AX.25:	"The Protocol Identifier (PID) field appears in information
 *		 frames (I and UI) only. It identifies which kind of
 *		 Layer 3 protocol, if any, is in use."
 *
 *------------------------------------------------------------------*/


int ax25_get_pid (packet_t this_p) 
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  return (this_p->frame_data[ax25_get_pid_offset(this_p)]);
	}
	return (-1);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_dedupe_crc 
 * 
 * Purpose:	Calculate a checksum for the packet source, destination, and
 *		information but NOT the digipeaters.
 *		This is used for duplicate detection in the digipeater 
 *		and IGate algorithms.
 *
 * Input:	pp	- Pointer to packet object.
 *		
 * Returns:	Value which will be the same for a duplicate but very unlikely 
 *		to match a non-duplicate packet.
 *
 * Description:	For detecting duplicates, we need to look
 *			+ source station
 *			+ destination 
 *			+ information field
 *		but NOT the changing list of digipeaters.
 *
 *		Typically, only a checksum is kept to reduce memory 
 *		requirements and amount of compution for comparisons.
 *		There is a very very small probability that two unrelated 
 *		packets will result in the same checksum, and the
 *		undesired dropping of the packet.
 *		
 *------------------------------------------------------------------------------*/

unsigned short ax25_dedupe_crc (packet_t pp)
{
	unsigned short crc;
	char src[AX25_MAX_ADDR_LEN];
	char dest[AX25_MAX_ADDR_LEN];
	unsigned char *pinfo;
	int info_len;

	ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);
	info_len = ax25_get_info (pp, &pinfo);

	crc = 0xffff;
	crc = crc16((unsigned char *)src, strlen(src), crc);
	crc = crc16((unsigned char *)dest, strlen(dest), crc);
	crc = crc16(pinfo, info_len, crc);

	return (crc);
}

/*------------------------------------------------------------------------------
 *
 * Name:	ax25_m_m_crc 
 * 
 * Purpose:	Calculate a checksum for the packet.
 *		This is used for the multimodem duplicate detection.
 *
 * Input:	pp	- Pointer to packet object.
 *		
 * Returns:	Value which will be the same for a duplicate but very unlikely 
 *		to match a non-duplicate packet.
 *
 * Description:	For detecting duplicates, we need to look the entire packet.
 *
 *		Typically, only a checksum is kept to reduce memory 
 *		requirements and amount of compution for comparisons.
 *		There is a very very small probability that two unrelated 
 *		packets will result in the same checksum, and the
 *		undesired dropping of the packet.
		
 *------------------------------------------------------------------------------*/

unsigned short ax25_m_m_crc (packet_t pp)
{
	unsigned short crc;
	unsigned char fbuf[AX25_MAX_PACKET_LEN];
	int flen;

	flen = ax25_pack (pp, fbuf); 

	crc = 0xffff;
	crc = crc16(fbuf, flen, crc);

	return (crc);
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_safe_print
 *
 * Purpose:	Print given string, changing non printable characters to 
 *		hexadecimal notation.   Note that character values
 *		<DEL>, 28, 29, 30, and 31 can appear in MIC-E message.
 *
 * Inputs:	pstr	- Pointer to string.
 *
 *		len	- Maximum length if not -1.
 *
 *		ascii_only	- Restrict output to only ASCII.
 *				  Normally we allow UTF-8.
 *		
 *		Stops after non-zero len characters or at nul.
 *
 * Returns:	none
 *
 * Description:	Print a string in a "safe" manner.
 *		Anything that is not a printable character
 *		will be converted to a hexadecimal representation.
 *		For example, a Line Feed character will appear as <0x0a>
 *		rather than dropping down to the next line on the screen.
 *
 *		ax25_from_text can accept this format.
 *
 *
 * Example:	W1MED-1>T2QP0S,N1OHZ,N8VIM*,WIDE1-1:'cQBl <0x1c>-/]<0x0d>
 *		                                          ------   ------
 *
 * Questions:	What should we do about UTF-8?  Should that be displayed
 *		as hexadecimal for troubleshooting? Maybe an option so the
 *		packet raw data is in hexadecimal but an extracted 
 *		comment displays UTF-8?  Or a command line option for only ASCII?
 *			
 *------------------------------------------------------------------*/

#define MAXSAFE 500

void ax25_safe_print (char *pstr, int len, int ascii_only)
{
	int ch;
	char safe_str[MAXSAFE*6+1];
	int safe_len;

	safe_len = 0;
	safe_str[safe_len] = '\0';


	if (len < 0) 
	  len = strlen(pstr);

	if (len > MAXSAFE)
	  len = MAXSAFE;

	while (len > 0 && *pstr != '\0')
	{
	  ch = *((unsigned char *)pstr);

	  if (ch < ' ' || ch == 0x7f || ch == 0xfe || ch == 0xff ||
			(ascii_only && ch >= 0x80) ) {

	      /* Control codes and delete. */
	      /* UTF-8 does not use fe and ff except in a possible */
	      /* "Byte Order Mark" (BOM) at the beginning. */

	      sprintf (safe_str + safe_len, "<0x%02x>", ch);
	      safe_len += 6;	      
	    }
	  else {
	    /* Let everything else thru so we can handle UTF-8 */
	    /* Maybe we should have an option to display 0x80 */
	    /* and above as hexadecimal. */

	    safe_str[safe_len++] = ch;
	    safe_str[safe_len] = '\0';
	  }

	  pstr++;
	  len--;
	}

	dw_printf ("%s", safe_str);

} /* end ax25_safe_print */



/* end ax25_pad.c */

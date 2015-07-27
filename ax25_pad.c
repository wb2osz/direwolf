// TODO:  Shouldn't this be using dw_printf???


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2013  John Langner, WB2OSZ
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
 *		Looking at the more general case, we might want to modify
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
 *					However, communication with an IGate server
 *					could have a longer VIA path but that is in text form.)
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


	
	packet_t this_p = ax25_new ();

	/* Is it possible to have a nul character (zero byte) in the */
	/* information field of an AX.25 frame? */

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
	
	strcpy ((char*)(this_p->the_rest + 2), pinfo);
	this_p->the_rest_len = strlen(pinfo) + 2;

/*
 * Now separate the addresses.
 * Note that source and destination order is swappped.
 */

	this_p->num_addr = 2;

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

	if ( ! ax25_parse_addr (pa, strict, this_p->addrs[AX25_SOURCE], &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad source address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	this_p->ssid_etc[AX25_SOURCE] = SSID_H_MASK | SSID_RR_MASK;
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

	if ( ! ax25_parse_addr (pa, strict, this_p->addrs[AX25_DESTINATION], &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad destination address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	this_p->ssid_etc[AX25_DESTINATION] = SSID_H_MASK | SSID_RR_MASK;
	ax25_set_ssid (this_p, AX25_DESTINATION, ssid_temp);

/*
 * VIA path.
 */
	while (( pa = strtok_r (NULL, ",", &saveptr)) != NULL && this_p->num_addr < AX25_MAX_ADDRS ) {

	  //char *last;
	  int k;

	  k = this_p->num_addr;

	  this_p->num_addr++;

	  if ( ! ax25_parse_addr (pa, strict, this_p->addrs[k], &ssid_temp, &heard_temp)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to create packet from text.  Bad digipeater address\n");
	    ax25_delete (this_p);
	    return (NULL);
	  }

	  this_p->ssid_etc[k] = SSID_RR_MASK;
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
	
	this_p->the_rest[0] = AX25_UI_FRAME;
	this_p->the_rest[1] = AX25_NO_LAYER_3;

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
 * Is is possible to have zero length for info?  No.
 */

	if (flen < AX25_MIN_PACKET_LEN || flen > AX25_MAX_PACKET_LEN)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Frame length %d not in allowable range of %d to %d.\n", flen, AX25_MIN_PACKET_LEN, AX25_MAX_PACKET_LEN);
	  return (NULL);
	}

	this_p = ax25_new ();

/*
 * Extract the addresses.
 * The last one has '1' in the LSB of the last byte.
 */

#if 1

/* 
 * 0.9 - Try new strategy that will allow KISS mode 
 * to handle non AX.25 frame. 
 */

	this_p->num_addr = 0;		/* Number of addresses extracted. */
	
	addr_bytes = 0;
	for (a = 0; a < flen && addr_bytes == 0; a++) {
	  if (fbuf[a] & 0x01) {
	    addr_bytes = a + 1;
	  }
	}

	if (addr_bytes % 7 == 0) {
	  int addrs = addr_bytes / 7;
	  if (addrs >= AX25_MIN_ADDRS && addrs <= AX25_MAX_ADDRS) {
	    this_p->num_addr = addrs;
	    
	    for (a = 0; a < addrs; a++){ 
	      unsigned char *pin;
	      char *pout;
	      int j;
	      char ch;
	
	      pin = fbuf + a * 7;
              pout = & this_p->addrs[a][0];

	      for (j=0; j<6; j++) 
	      {
	        ch = *pin++ >> 1;
	        if (ch != ' ')
	        {
	          *pout++ = ch;
	        }
	      }
	      *pout = '\0';

	      this_p->ssid_etc[a] = *pin & ~ SSID_LAST_MASK;
	    }
	  }
	}
	
	pf = fbuf + this_p->num_addr * 7;

#else 

	pf = fbuf;		/* Transmitted form from here. */

	this_p->num_addr = 0;		/* Number of addresses extracted. */
	found_last = 0;

	while (this_p->num_addr < AX25_MAX_ADDRS && ! found_last) {

	  unsigned char *pin;
	  char *pout;
	  int j;
	  char ch;
	
	  pin = pf;
          pout = & this_p->addrs[this_p->num_addr][0];

	  for (j=0; j<6; j++) 
	  {
	    ch = *pin++ >> 1;
	    if (ch != ' ')
	    {
	      *pout++ = ch;
	    }
	  }
	  *pout = '\0';

	  this_p->ssid_etc[this_p->num_addr] = pf[6] & ~ SSID_LAST_MASK;
	
  	  this_p->num_addr++;

	  if (pf[6] & SSID_LAST_MASK) {	/* Is this the last one? */
	    found_last = 1;
          }
	  else {
	    pf += 7;			/* Get ready for next one. */
	  }
	}

	if (this_p->num_addr < 2) {
	  int k;
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Frame format error detected in ax25_from_frame, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Did not find a minimum of two addresses at beginning of AX.25 frame.\n");
	  for (k=0; k<14; k++) {
	    dw_printf (" %02x", fbuf[k]);
	  }
	  dw_printf ("\n");
	  /* Should we keep going or delete the packet? */
	}

/*
 * pf still points to the last address (repeater or source).
 *
 * Verify that it has the last address bit set.
 */
	if ((pf[6] & SSID_LAST_MASK) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Last address in header does not have LSB set.\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	pf += 7;

#endif

	if (this_p->num_addr * 7 > flen - 1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Frame too short to include control field.\n");
	  ax25_delete (this_p);
	  return (NULL);
	}




/*
 * pf should now point to control field.
 * Previously we separated out control, PID, and Info here.
 *
 * Now (version 0.8) we save control, PID, and info together.
 * This makes it easier to act as a dumb KISS TNC
 * for AX.25-based protocols other than APRS.
 */
	this_p->the_rest_len = flen - (pf - fbuf);

	assert (this_p->the_rest_len >= 1);

	memcpy (this_p->the_rest, pf, (size_t)this_p->the_rest_len);
	this_p->the_rest[this_p->the_rest_len] = '\0';

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
 *		ad	- Address with optional dash and substation id.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * TODO:  ax25_from_text could use this.
 *
 * Returns:	None.
 *		
 *
 *------------------------------------------------------------------------------*/

void ax25_set_addr (packet_t this_p, int n, char *ad)
{
	int ssid_temp, heard_temp;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= 0 && n < AX25_MAX_ADDRS);
	assert (strlen(ad) < AX25_MAX_ADDR_LEN);

	if (n+1 > this_p->num_addr) {
	  this_p->num_addr = n+1;
	  this_p->ssid_etc[n] = SSID_RR_MASK;
	}

	ax25_parse_addr (ad, 0, this_p->addrs[n], &ssid_temp, &heard_temp);
	ax25_set_ssid (this_p, n, ssid_temp);
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

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= AX25_REPEATER_1 && n < AX25_MAX_ADDRS);
	assert (strlen(ad) < AX25_MAX_ADDR_LEN);

	/* Don't do it if we already have the maximum number. */
	/* Should probably return success/fail code but currently the caller doesn't care. */

	if ( this_p->num_addr >= AX25_MAX_ADDRS) {
	  return;
	}

	/* Shift the current occupant and others up. */

	for (k=this_p->num_addr; k>n; k--) {
	  strcpy (this_p->addrs[k], this_p->addrs[k-1]);
	  this_p->ssid_etc[k] = this_p->ssid_etc[k-1];
	}

	this_p->num_addr++;

	ax25_parse_addr (ad, 0, this_p->addrs[n], &ssid_temp, &heard_temp);
	this_p->ssid_etc[n] = SSID_RR_MASK;
	ax25_set_ssid (this_p, n, ssid_temp);
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

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= AX25_REPEATER_1 && n < AX25_MAX_ADDRS);

	/* Shift those beyond to fill this position. */

	this_p->num_addr--;

	for (k = n; k < this_p->num_addr; k++) {
	  strcpy (this_p->addrs[k], this_p->addrs[k+1]);
	  this_p->ssid_etc[k] = this_p->ssid_etc[k+1];
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
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
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

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

/*
 * This assert failure popped up once and it is not clear why.
 * Let's print out more information about the situation so we 
 * might have a clue about the root cause.
 * Try to keep going instead of dying at this point.
 */
	//assert (n >= 0 && n < this_p->num_addr);

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

	strcpy (station, this_p->addrs[n]);

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
 * Warning:	No bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Substation id, as integer 0 .. 15.
 *
 * Bugs:	Rewrite to keep call and SSID separate internally.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_ssid (packet_t this_p, int n)
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= 0 && n < this_p->num_addr);

	return ((this_p->ssid_etc[n] & SSID_SSID_MASK) >> SSID_SSID_SHIFT);
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
 * Warning:	No bounds checking is performed.  Be careful.
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
	assert (n >= 0 && n < this_p->num_addr);

	this_p->ssid_etc[n] =   (this_p->ssid_etc[n] & ~ SSID_SSID_MASK) |
		((ssid << SSID_SSID_SHIFT) & SSID_SSID_MASK) ;
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

	return ((this_p->ssid_etc[n] & SSID_H_MASK) >> SSID_H_SHIFT);
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
	assert (n >= 0 && n < this_p->num_addr);

	this_p->ssid_etc[n] |= SSID_H_MASK;
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
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  *paddr = this_p->the_rest + ax25_get_info_offset(this_p);
	  return (ax25_get_num_info(this_p));
	}

	/* Not AX.25.  Whole packet is info. */

	*paddr = this_p->the_rest;
	return (this_p->the_rest_len);
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
	  return (this_p->the_rest[ax25_get_info_offset(this_p)]);
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
 *
 *------------------------------------------------------------------*/

int ax25_pack (packet_t this_p, unsigned char result[AX25_MAX_PACKET_LEN]) 
{
	int j, k;
	unsigned char *pout;
	int len;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	pout = result;

	for (j=0; j<this_p->num_addr; j++) {

	  char *s;

	  memset (pout, ' ' << 1, (size_t)6);

	  s = this_p->addrs[j];
	  for (k=0; *s != '\0'; k++, s++) {
	    pout[k] = *s << 1;	    
	  }

	  if (j == this_p->num_addr - 1) {
	    pout[6] = this_p->ssid_etc[j] | SSID_LAST_MASK;
	  }
	  else {
	   pout[6] = this_p->ssid_etc[j] & ~ SSID_LAST_MASK;
	  }
	  pout += 7;
	}

	memcpy (pout, this_p->the_rest, (size_t)this_p->the_rest_len);
	pout += this_p->the_rest_len;

	len = pout - result;

	assert (len <= AX25_MAX_PACKET_LEN);

	return (len);
}


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
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	return (this_p->num_addr >= 2 &&
		ax25_get_control(this_p) == AX25_UI_FRAME && 
		ax25_get_pid(this_p) == AX25_NO_LAYER_3);
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
	  return (this_p->the_rest[ax25_get_control_offset(this_p)]);
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
 *------------------------------------------------------------------*/


int ax25_get_pid (packet_t this_p) 
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->num_addr >= 2) {
	  return (this_p->the_rest[ax25_get_pid_offset(this_p)]);
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



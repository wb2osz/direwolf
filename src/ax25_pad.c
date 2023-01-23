//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011 , 2013, 2014, 2015, 2019  John Langner, WB2OSZ
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
 *		This was written when I was only concerned about APRS which
 *		uses only UI frames.  ax25_pad2.c, added years later, has
 *		functions for dealing with other types of frames.
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
 *	APRS uses only UI frames.
 *	Each starts with 2-10 addresses (14-70 octets):
 *
 *	* Destination Address  (note: opposite order in printed format)
 *
 *	* Source Address
 *
 *	* 0-8 Digipeater Addresses  (Could there ever be more as a result of
 *					digipeaters inserting their own call for
 *					the tracing feature?
 *					NO.  The limit is 8 when transmitting AX.25 over the
 *					radio.
 *					Communication with an IGate server could
 *					have a longer VIA path but that is only in text form,
 *					not as an AX.25 frame.)
 *
 *	Each address is composed of:
 *
 *	* 6 upper case letters or digits, blank padded.
 *		These are shifted left one bit, leaving the LSB always 0.
 *
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
 *		The AX.25 spec states that the RR bits should be 11 if not used.
 *		There are a couple documents talking about possible uses for APRS.
 *		I'm ignoring them for now.
 *		http://www.aprs.org/aprs12/preemptive-digipeating.txt
 *		http://www.aprs.org/aprs12/RR-bits.txt
 *
 *		I don't recall why I originally set the source & destination C bits both to 1.
 *		Reviewing this 5 years later, after spending more time delving into the
 *		AX.25 spec, I think it should be 1 for destination and 0 for source.
 *		In practice you see all four combinations being used by APRS stations
 *		and everyone apparently ignores them for APRS.  They do make a big
 *		difference for connected mode.
 *
 *	The final octet of the Source has the form:
 *
 *		C R R SSID 0, where,
 *
 *			C = command/response = 0
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
 *	In standard monitoring format, an asterisk is displayed after the last
 *	digipeater with the "H" bit set.  That indicates who you are hearing
 *	over the radio.
 *	(That is if digipeaters update the via path properly.  Some don't so
 *	we don't know who we are hearing.  This is discussed in the User Guide.)
 *	No asterisk means the source is being heard directly.
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
 * 	The descriptions above, for the C, H, and RR bits, are for APRS usage.
 *	When operating as a KISS TNC we just pass everything along and don't
 *	interpret or change them.
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

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>

#include "regex.h"

#if __WIN32__
char *strtok_r(char *str, const char *delim, char **saveptr);
#endif


#include "textcolor.h"
#include "ax25_pad.h"
#include "fcs_calc.h"

/*
 * Accumulate statistics.
 * If new_count gets much larger than delete_count plus the size of 
 * the transmit queue we have a memory leak.
 */

static volatile int new_count = 0;
static volatile int delete_count = 0;
static volatile int last_seq_num = 0;

#if AX25MEMDEBUG

int ax25memdebug = 0;


void ax25memdebug_set(void) 
{
	ax25memdebug = 1;
}

int ax25memdebug_get (void)
{
	return (ax25memdebug);
}

int ax25memdebug_seq (packet_t this_p)
{
	return (this_p->seq);
}


#endif



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


packet_t ax25_new (void)
{
	struct packet_s *this_p;


#if DEBUG 
        text_color_set(DW_COLOR_DEBUG);
        dw_printf ("ax25_new(): before alloc, new=%d, delete=%d\n", new_count, delete_count);
#endif

	last_seq_num++;
	new_count++;

/*
 * check for memory leak.
 */

// version 1.4 push up the threshold.   We could have considerably more with connected mode.

	//if (new_count > delete_count + 100) {
	if (new_count > delete_count + 256) {


	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Report to WB2OSZ - Memory leak for packet objects.  new=%d, delete=%d\n", new_count, delete_count);
#if AX25MEMDEBUG
	  // Force on debug option to gather evidence.
	  ax25memdebug_set();
#endif
	}

	this_p = calloc(sizeof (struct packet_s), (size_t)1);

	if (this_p == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - can't allocate memory in ax25_new.\n");
	}

	assert (this_p != NULL);

	this_p->magic1 = MAGIC;
	this_p->seq = last_seq_num;
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

#if AX25MEMDEBUG
void ax25_delete_debug (packet_t this_p, char *src_file, int src_line)
#else
void ax25_delete (packet_t this_p)
#endif
{
#if DEBUG
        text_color_set(DW_COLOR_DEBUG);
        dw_printf ("ax25_delete(): before free, new=%d, delete=%d\n", new_count, delete_count);
#endif

	if (this_p == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - NULL pointer passed to ax25_delete.\n");
	  return;
	}


	delete_count++;

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_delete, seq=%d, called from %s %d, new_count=%d, delete_count=%d\n", this_p->seq, src_file, src_line, new_count, delete_count);
	}
#endif

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	
	this_p->magic1 = 0;
	this_p->magic1 = 0;

	//memset (this_p, 0, sizeof (struct packet_s));
	free (this_p);
}


		
/*------------------------------------------------------------------------------
 *
 * Name:	ax25_from_text
 * 
 * Purpose:	Parse a frame in human-readable monitoring format and change
 *		to internal representation.
 *
 * Input:	monitor	- "TNC-2" monitor format for packet.  i.e.
 *				source>dest[,repeater1,repeater2,...]:information
 *
 *			The information part can have non-printable characters
 *			in the form of <0xff>.  This will be converted to single
 *			bytes.  e.g.  <0x0d> is carriage return.
 *			In version 1.4H we will allow nul characters which means
 *			we have to maintain a length rather than using strlen().
 *			I maintain that it violates the spec but want to handle it
 *			because it does happen and we want to preserve it when
 *			acting as an IGate rather than corrupting it.
 *
 *		strict	- True to enforce rules for packets sent over the air.
 *			  False to be more lenient for packets from IGate server.
 *
 *			  Packets from an IGate server can have longer
 *		 	  addresses after qAC.  Up to 9 observed so far.
 *			  The SSID can be 2 alphanumeric characters, not just 1 to 15.
 *
 *			  We can just truncate the name because we will only
 *			  end up discarding it.    TODO:  check on this.
 *
 * Returns:	Pointer to new packet object in the current implementation.
 *
 * Outputs:	Use the "get" functions to retrieve information in different ways.
 *
 *------------------------------------------------------------------------------*/

#if AX25MEMDEBUG
packet_t ax25_from_text_debug (char *monitor, int strict, char *src_file, int src_line)
#else
packet_t ax25_from_text (char *monitor, int strict)
#endif
{

/*
 * Tearing it apart is destructive so make our own copy first.
 */
	char stuff[AX25_MAX_PACKET_LEN+1];
	char *pinfo;

	int ssid_temp, heard_temp;
	char atemp[AX25_MAX_ADDR_LEN];

	char info_part[AX25_MAX_INFO_LEN+1];
	int info_len;

	// text_color_set(DW_COLOR_DEBUG);
	// dw_printf ("DEBUG: ax25_from_text ('%s', %d)\n", monitor, strict);
	// fflush(stdout); sleep(1);

	packet_t this_p = ax25_new ();

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_from_text, seq=%d, called from %s %d\n", this_p->seq, src_file, src_line);
	}
#endif

	/* Is it possible to have a nul character (zero byte) in the */
	/* information field of an AX.25 frame? */
	/* At this point, we have a normal C string. */
	/* It is possible that will convert <0x00> to a nul character later. */
	/* There we need to maintain a separate length and not use normal C string functions. */

	strlcpy (stuff, monitor, sizeof(stuff));


/*
 * Initialize the packet structure with two addresses and control/pid
 * for APRS.
 */
	memset (this_p->frame_data + AX25_DESTINATION*7, ' ' << 1, 6);
	this_p->frame_data[AX25_DESTINATION*7+6] = SSID_H_MASK | SSID_RR_MASK;
 
	memset (this_p->frame_data + AX25_SOURCE*7, ' ' << 1, 6);
	this_p->frame_data[AX25_SOURCE*7+6] = SSID_RR_MASK | SSID_LAST_MASK;

	this_p->frame_data[14] = AX25_UI_FRAME;
	this_p->frame_data[15] = AX25_PID_NO_LAYER_3;

	this_p->frame_len = 7 + 7 + 1 + 1;
	this_p->num_addr = (-1);
	(void) ax25_get_num_addr(this_p);  // when num_addr is -1, this sets it properly.
	assert (this_p->num_addr == 2);


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

/*
 * Separate the addresses.
 * Note that source and destination order is swappped.
 */

/*
 * Source address.
 */

	char *pnxt = stuff;
	char *pa = strsep (&pnxt, ">");
	if (pa == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  No source address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	if ( ! ax25_parse_addr (AX25_SOURCE, pa, strict, atemp, &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad source address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	ax25_set_addr (this_p, AX25_SOURCE, atemp);
	ax25_set_h (this_p, AX25_SOURCE);	// c/r in this position
	ax25_set_ssid (this_p, AX25_SOURCE, ssid_temp);

/*
 * Destination address.
 */
 
	pa = strsep (&pnxt, ",");
	if (pa == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  No destination address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	if ( ! ax25_parse_addr (AX25_DESTINATION, pa, strict, atemp, &ssid_temp, &heard_temp)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to create packet from text.  Bad destination address\n");
	  ax25_delete (this_p);
	  return (NULL);
	}

	ax25_set_addr (this_p, AX25_DESTINATION, atemp);
	ax25_set_h (this_p, AX25_DESTINATION);	// c/r in this position
	ax25_set_ssid (this_p, AX25_DESTINATION, ssid_temp);

/*
 * VIA path.
 */

// Originally this used strtok_r.
// strtok considers all adjacent delimiters to be a single delimiter.
// This is handy for varying amounts of whitespace.
// It will never return a zero length string.
// All was good until this bizarre case came along:

//	AISAT-1>CQ,,::CQ-0     :From  AMSAT INDIA & Exseed Space |114304|48|45|42{962

// Apparently there are two digipeater fields but they are empty.
// When we parsed this text representation, the extra commas were ignored rather
// than pointed out as being invalid.

// Use strsep instead.  This does not collapse adjacent delimiters.

	while (( pa = strsep (&pnxt, ",")) != NULL && this_p->num_addr < AX25_MAX_ADDRS ) {

	  int k = this_p->num_addr;

	  // printf ("DEBUG: get digi loop, num addr = %d, address = '%s'\n", k, pa);// FIXME

	  // Hack for q construct, from APRS-IS, so it does not cause panic later.

	  if ( ! strict && pa[0] == 'q' && pa[1] == 'A') {
	    pa[0] = 'Q';
	    pa[2] = toupper(pa[2]);
	  }

	  if ( ! ax25_parse_addr (k, pa, strict, atemp, &ssid_temp, &heard_temp)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Failed to create packet from text.  Bad digipeater address\n");
	    ax25_delete (this_p);
	    return (NULL);
	  }

	  ax25_set_addr (this_p, k, atemp);
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
 * Finally, process the information part.
 *
 * Translate hexadecimal values like <0xff> to single bytes.
 * MIC-E format uses 5 different non-printing characters.
 * We might want to manually generate UTF-8 characters such as degree.
 */

//#define DEBUG14H 1

#if DEBUG14H
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("BEFORE: %s\nSAFE:   ", pinfo);
	ax25_safe_print (pinfo, -1, 0);
	dw_printf ("\n");
#endif

	info_len = 0;
	while (*pinfo != '\0' && info_len < AX25_MAX_INFO_LEN) {

	  if (strlen(pinfo) >= 6 &&
		pinfo[0] == '<' &&
		pinfo[1] == '0' &&
		pinfo[2] == 'x' &&
		isxdigit(pinfo[3]) &&
		isxdigit(pinfo[4]) &&
		pinfo[5] == '>') {

	    char *p;

	    info_part[info_len] = strtol (pinfo + 3, &p, 16);
	    info_len++;
	    pinfo += 6;
	  }
	  else {
	    info_part[info_len] = *pinfo;
	    info_len++;
	    pinfo++;
	  }
	}
	info_part[info_len] = '\0';

#if DEBUG14H
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("AFTER:  %s\nSAFE:   ", info_part);
	ax25_safe_print (info_part, info_len, 0);
	dw_printf ("\n");
#endif

/*
 * Append the info part.  
 */
	memcpy ((char*)(this_p->frame_data+this_p->frame_len), info_part, info_len);
	this_p->frame_len += info_len;

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

#if AX25MEMDEBUG
packet_t ax25_from_frame_debug (unsigned char *fbuf, int flen, alevel_t alevel, char *src_file, int src_line)
#else
packet_t ax25_from_frame (unsigned char *fbuf, int flen, alevel_t alevel)
#endif
{
	packet_t this_p;


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

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_from_frame, seq=%d, called from %s %d\n", this_p->seq, src_file, src_line);
	}
#endif

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


#if AX25MEMDEBUG
packet_t ax25_dup_debug (packet_t copy_from, char *src_file, int src_line)
#else
packet_t ax25_dup (packet_t copy_from)
#endif
{
	int save_seq;
	packet_t this_p;

	
	this_p = ax25_new ();
	assert (this_p != NULL);

	save_seq = this_p->seq;

	memcpy (this_p, copy_from, sizeof (struct packet_s));
	this_p->seq = save_seq;

#if AX25MEMDEBUG
	if (ax25memdebug) {	
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_dup, seq=%d, called from %s %d, clone of seq %d\n", this_p->seq, src_file, src_line, copy_from->seq);
	}
#endif

	return (this_p);

}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_parse_addr
 * 
 * Purpose:	Parse address with optional ssid.
 *
 * Inputs:	position	- AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER_1...
 *				  Used for more specific error message.  -1 if not used.
 *
 *		in_addr		- Input such as "WB2OSZ-15*"
 *
 * 		strict		- 1 (true) for strict checking (6 characters, no lower case,
 *				  SSID must be in range of 0 to 15).
 *				  Strict is appropriate for packets sent
 *				  over the radio.  Communication with IGate
 *				  allows lower case (e.g. "qAR") and two 
 *				  alphanumeric characters for the SSID.
 *				  We also get messages like this from a server.
 *					KB1POR>APU25N,TCPIP*,qAC,T2NUENGLD:...
 *					K1BOS-B>APOSB,TCPIP,WR2X-2*:...
 *
 *				  2 (extra true) will complain if * is found at end.
 *
 * Outputs:	out_addr	- Address without any SSID.
 *				  Must be at least AX25_MAX_ADDR_LEN bytes.
 *
 *		out_ssid	- Numeric value of SSID.
 *
 *		out_heard	- True if "*" found.
 *
 * Returns:	True (1) if OK, false (0) if any error.
 *		When 0, out_addr, out_ssid, and out_heard are undefined.
 *
 *
 *------------------------------------------------------------------------------*/

static const char *position_name[1 + AX25_MAX_ADDRS] = {
	"", "Destination ", "Source ",
	"Digi1 ", "Digi2 ", "Digi3 ", "Digi4 ",
	"Digi5 ", "Digi6 ", "Digi7 ", "Digi8 " };

int ax25_parse_addr (int position, char *in_addr, int strict, char *out_addr, int *out_ssid, int *out_heard)
{
	char *p;
	char sstr[8];		/* Should be 1 or 2 digits for SSID. */
	int i, j, k;
	int maxlen;

	*out_addr = '\0';
	*out_ssid = 0;
	*out_heard = 0;

	// dw_printf ("ax25_parse_addr in: position=%d, '%s', strict=%d\n", position, in_addr, strict);

	if (position < -1) position = -1;
	if (position > AX25_REPEATER_8) position = AX25_REPEATER_8;
	position++;	/* Adjust for position_name above. */

	if (strlen(in_addr) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("%sAddress \"%s\" is empty.\n", position_name[position], in_addr);
	  return 0;
	}

	if (strict && strlen(in_addr) >= 2 && strncmp(in_addr, "qA", 2) == 0) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("%sAddress \"%s\" is a \"q-construct\" used for communicating with\n", position_name[position], in_addr);
	  dw_printf ("APRS Internet Servers.  It should never appear when going over the radio.\n");
	}

	// dw_printf ("ax25_parse_addr in: %s\n", in_addr);

	maxlen = strict ? 6 : (AX25_MAX_ADDR_LEN-1);
	p = in_addr;
	i = 0;
	for (p = in_addr; *p != '\0' && *p != '-' && *p != '*'; p++) {
	  if (i >= maxlen) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%sAddress is too long. \"%s\" has more than %d characters.\n", position_name[position], in_addr, maxlen);
	    return 0;
	  }
	  if ( ! isalnum(*p)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%sAddress, \"%s\" contains character other than letter or digit in character position %d.\n", position_name[position], in_addr, (int)(long)(p-in_addr)+1);
	    return 0;
	  }

	  out_addr[i++] = *p;
	  out_addr[i] = '\0';

#if DECAMAIN	// Hack when running in decode_aprs utility.
		// Exempt the "qA..." case because it was already mentioned.

	  if (strict && islower(*p) && strncmp(in_addr, "qA", 2) != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%sAddress has lower case letters. \"%s\" must be all upper case.\n", position_name[position], in_addr);
	  }
#else
	  if (strict && islower(*p)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%sAddress has lower case letters. \"%s\" must be all upper case.\n", position_name[position], in_addr);
	    return 0;
	  }
#endif
	}
	
	j = 0;
	sstr[j] = '\0';
	if (*p == '-') {
	  for (p++; isalnum(*p); p++) {
	    if (j >= 2) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("%sSSID is too long. SSID part of \"%s\" has more than 2 characters.\n", position_name[position], in_addr);
	      return 0;
	    }
	    sstr[j++] = *p;
	    sstr[j] = '\0';
	    if (strict && ! isdigit(*p)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("%sSSID must be digits. \"%s\" has letters in SSID.\n", position_name[position], in_addr);
	      return 0;
	    }
	  }
	  k = atoi(sstr);
	  if (k < 0 || k > 15) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("%sSSID out of range. SSID of \"%s\" not in range of 0 to 15.\n", position_name[position], in_addr);
	    return 0;
	  }
	  *out_ssid = k;
	}

	if (*p == '*') {
	  *out_heard = 1;
	  p++;
	  if (strict == 2) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\"*\" is not allowed at end of address \"%s\" here.\n", in_addr);
	    return 0;
	  }
	}

	if (*p != '\0') {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid character \"%c\" found in %saddress \"%s\".\n", *p, position_name[position], in_addr);
	  return 0;
	}

	// dw_printf ("ax25_parse_addr out: '%s' %d %d\n", out_addr, *out_ssid, *out_heard);

	return (1);

} /* end ax25_parse_addr */


/*-------------------------------------------------------------------
 *
 * Name:        ax25_check_addresses
 *
 * Purpose:     Check addresses of given packet and print message if any issues.
 *		We call this when receiving and transmitting.
 *
 * Inputs:	pp	- packet object pointer.
 *
 * Errors:	Print error message.
 *
 * Returns:	1 for all valid.  0 if not.
 *
 * Examples:	I was surprised to get this from an APRS-IS server with
 *		a lower case source address.
 *
 *			n1otx>APRS,TCPIP*,qAC,THIRD:@141335z4227.48N/07111.73W_348/005g014t044r000p000h60b10075.wview_5_20_2
 *
 *		I haven't gotten to the bottom of this yet but it sounds
 *		like "q constructs" are somehow getting on to the air when
 *		they should only appear in conversations with IGate servers.
 *
 *			https://groups.yahoo.com/neo/groups/direwolf_packet/conversations/topics/678
 *
 *			WB0VGI-7>APDW12,W0YC-5*,qAR,AE0RF-10:}N0DZQ-10>APWW10,TCPIP,WB0VGI-7*:;145.230MN*080306z4607.62N/09230.58WrKE0ACL/R 145.230- T146.2 (Pine County ARES)	
 *
 * Typical result:
 *
 *			Digipeater WIDE2 (probably N3LEE-4) audio level = 28(10/6)   [NONE]   __|||||||
 *			[0.5] VE2DJE-9>P_0_P?,VE2PCQ-3,K1DF-7,N3LEE-4,WIDE2*:'{S+l <0x1c>>/
 *			Invalid character "_" in MIC-E destination/latitude.
 *			Invalid character "_" in MIC-E destination/latitude.
 *			Invalid character "?" in MIC-E destination/latitude.
 *			Invalid MIC-E N/S encoding in 4th character of destination.
 *			Invalid MIC-E E/W encoding in 6th character of destination.
 *			MIC-E, normal car (side view), Unknown manufacturer, Returning
 *			N 00 00.0000, E 005 55.1500, 0 MPH
 *			Invalid character "_" found in Destination address "P_0_P?".
 *
 *			*** The origin and journey of this packet should receive some scrutiny. ***
 *
 *--------------------------------------------------------------------*/

int ax25_check_addresses (packet_t pp)
{
	int n;
	char addr[AX25_MAX_ADDR_LEN];
	char ignore1[AX25_MAX_ADDR_LEN];
	int ignore2, ignore3;
	int all_ok = 1;

	for (n = 0; n < ax25_get_num_addr(pp); n++) {
	  ax25_get_addr_with_ssid (pp, n, addr);
	  all_ok &= ax25_parse_addr (n, addr, 1, ignore1, &ignore2, &ignore3);
	}

	if (! all_ok) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n");
	  dw_printf ("*** The origin and journey of this packet should receive some scrutiny. ***\n");
	  dw_printf ("\n");
	}

	return (all_ok);
} /* end ax25_check_addresses */


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_unwrap_third_party
 * 
 * Purpose:	Unwrap a third party message from the header.
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

	// Want strict because addresses should conform to AX.25 here.
	// That's not the case for something from an Internet Server.

	result_pp = ax25_from_text((char *)info_p + 1, 1);

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

	//dw_printf ("ax25_set_addr (%d, %s) num_addr=%d\n", n, ad, this_p->num_addr);

	if (strlen(ad) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Set address error!  Station address for position %d is empty!\n", n);
	}

	if (n >= 0 && n < this_p->num_addr) {

	  //dw_printf ("ax25_set_addr , existing case\n");
/* 
 * Set existing address position. 
 */

	  // Why aren't we setting 'strict' here?
	  // Messages from IGate have q-constructs.
	  // We use this to parse it and later remove unwanted parts.

	  ax25_parse_addr (n, ad, 0, atemp, &ssid_temp, &heard_temp);

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
	int ssid_temp, heard_temp;
	char atemp[AX25_MAX_ADDR_LEN];
	int i;
	int expect;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= AX25_REPEATER_1 && n < AX25_MAX_ADDRS);

	//dw_printf ("ax25_insert_addr (%d, %s)\n", n, ad);

	if (strlen(ad) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Set address error!  Station address for position %d is empty!\n", n);
	}

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

	// Why aren't we setting 'strict' here?
	// Messages from IGate have q-constructs.
	// We use this to parse it and later remove unwanted parts.

	ax25_parse_addr (n, ad, 0, atemp, &ssid_temp, &heard_temp);
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
	//unsigned char *pf;
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
 *			  Usually variables will be AX25_MAX_ADDR_LEN bytes
 *			  but 10 would be adequate.
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
	char sstr[8];		/* Should be 1 or 2 digits for SSID. */
	int i;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);


	if (n < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_addr_with_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is less than zero.\n", n);
	  strlcpy (station, "??????", 10);
	  return;
	}

	if (n >= this_p->num_addr) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_addr_with_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is too large for number of addresses, %d.\n", n, this_p->num_addr);
	  strlcpy (station, "??????", 10);
	  return;
	}

	// At one time this would stop at the first space, on the assumption we would have only trailing spaces.
	// Then there was a forum discussion where someone encountered the address " WIDE2" with a leading space.
	// In that case, we would have returned a zero length string here.
	// Now we return exactly what is in the address field and trim trailing spaces.
	// This will provide better information for troubleshooting.

	for (i=0; i<6; i++) {
	  station[i] = (this_p->frame_data[n*7+i] >> 1) & 0x7f;
	}
	station[6] = '\0';

	for (i=5; i>=0; i--) {
	  if (station[i] == '\0') {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Station address \"%s\" contains nul character.  AX.25 protocol requires trailing ASCII spaces when less than 6 characters.\n", station);
	  }
	  else if (station[i] == ' ')
	    station[i] = '\0';
	  else
	    break;
	}

	if (strlen(station) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Station address, in position %d, is empty!  This is not a valid AX.25 frame.\n", n);
	}

	ssid = ax25_get_ssid (this_p, n);
	if (ssid != 0) {
	  snprintf (sstr, sizeof(sstr), "-%d", ssid);
	  strlcat (station, sstr, 10);
	}

} /* end ax25_get_addr_with_ssid */


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_addr_no_ssid
 * 
 * Purpose:	Return specified address WITHOUT any SSID.
 *
 * Inputs:	n	- Index of address.   Use the symbols 
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 * Outputs:	station - String representation of the station, WITHOUT the SSID.
 *			e.g.  "WB2OSZ"
 *			  Usually variables will be AX25_MAX_ADDR_LEN bytes
 *			  but 7 would be adequate.
 *
 * Bugs:	No bounds checking is performed.  Be careful.
 *		  
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 * Returns:	Character string in usual human readable format,
 *		
 *
 *------------------------------------------------------------------------------*/

void ax25_get_addr_no_ssid (packet_t this_p, int n, char *station)
{	
	int i;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);


	if (n < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_addr_no_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is less than zero.\n", n);
	  strlcpy (station, "??????", 7);
	  return;
	}

	if (n >= this_p->num_addr) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error detected in ax25_get_no_with_ssid, %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("Address index, %d, is too large for number of addresses, %d.\n", n, this_p->num_addr);
	  strlcpy (station, "??????", 7);
	  return;
	}

	// At one time this would stop at the first space, on the assumption we would have only trailing spaces.
	// Then there was a forum discussion where someone encountered the address " WIDE2" with a leading space.
	// In that case, we would have returned a zero length string here.
	// Now we return exactly what is in the address field and trim trailing spaces.
	// This will provide better information for troubleshooting.

	for (i=0; i<6; i++) {
	  station[i] = (this_p->frame_data[n*7+i] >> 1) & 0x7f;
	}
	station[6] = '\0';

	for (i=5; i>=0; i--) {
	  if (station[i] == ' ')
	    station[i] = '\0';
	  else
	    break;
	}

	if (strlen(station) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Station address, in position %d, is empty!  This is not a valid AX.25 frame.\n", n);
	}

} /* end ax25_get_addr_no_ssid */


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
 * Name:	ax25_get_rr
 *
 * Purpose:	Return the two reserved "RR" bits in the specified address field.
 *
 * Inputs:	pp	- Packet object.
 *
 *		n	- Index of address.   Use the symbols
 *			  AX25_DESTINATION, AX25_SOURCE, AX25_REPEATER1, etc.
 *
 * Returns:	0, 1, 2, or 3.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_rr (packet_t this_p, int n)
{

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	assert (n >= 0 && n < this_p->num_addr);

	if (n >= 0 && n < this_p->num_addr) {
	  return ((this_p->frame_data[n*7+6] & SSID_RR_MASK) >> SSID_RR_SHIFT);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: ax25_get_rr(%d), num_addr=%d\n", n, this_p->num_addr);
	  return (0);
	}
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_info
 * 
 * Purpose:	Obtain Information part of current packet.
 *
 * Inputs:	this_p	- Packet object pointer.
 *
 * Outputs:	paddr	- Starting address of information part is returned here.
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

} /* end ax25_get_info */


void ax25_set_info (packet_t this_p, unsigned char *new_info_ptr, int new_info_len)
{
	unsigned char *old_info_ptr;
	int old_info_len = ax25_get_info (this_p, &old_info_ptr);
	this_p->frame_len -= old_info_len;

	if (new_info_len < 0) new_info_len = 0;
	if (new_info_len > AX25_MAX_INFO_LEN) new_info_len = AX25_MAX_INFO_LEN;
	memcpy (old_info_ptr, new_info_ptr, new_info_len);
	this_p->frame_len += new_info_len;
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_cut_at_crlf
 *
 * Purpose:	Truncate the information part at the first CR or LF.
 *		This is used for the RF>IS IGate function.
 *		CR/LF is used as record separator so we must remove it
 *		before packaging up packet to sending to server.
 *
 * Inputs:	this_p	- Packet object pointer.
 *
 * Outputs:	Packet is modified in place.
 *
 * Returns:	Number of characters removed from the end.
 *		0 if not changed.
 *
 * Assumption:	ax25_from_text or ax25_from_frame was called first.
 *
 *------------------------------------------------------------------------------*/

int ax25_cut_at_crlf (packet_t this_p)
{
	unsigned char *info_ptr;
	int info_len;
	int j;


	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	info_len = ax25_get_info (this_p, &info_ptr);

	// Can't use strchr because there is potential of nul character.

	for (j = 0; j < info_len; j++) {

	  if (info_ptr[j] == '\r' || info_ptr[j] == '\n') {

	    int chop = info_len - j;

	    this_p->frame_len -= chop;
	    return (chop);
	  }
	}

	return (0);
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


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_release_time
 *
 * Purpose:	Set release time
 *
 * Inputs:	this_p		- Current packet object.
 *
 *		release_time	- Time as returned by dtime_monotonic().
 *
 *------------------------------------------------------------------------------*/

void ax25_set_release_time (packet_t this_p, double release_time)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	
	this_p->release_time = release_time;
}



/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_release_time
 *
 * Purpose:	Get release time.
 *
 *------------------------------------------------------------------------------*/

double ax25_get_release_time (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	return (this_p->release_time);
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_set_modulo
 *
 * Purpose:	Set modulo value for I and S frame sequence numbers.
 *
 *------------------------------------------------------------------------------*/

void ax25_set_modulo (packet_t this_p, int modulo)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	this_p->modulo = modulo;
}


/*------------------------------------------------------------------------------
 *
 * Name:	ax25_get_modulo
 *
 * Purpose:	Get modulo value for I and S frame sequence numbers.
 *
 * Returns:	8 or 128 if known.
 *		0 if unknown.
 *
 *------------------------------------------------------------------------------*/

int ax25_get_modulo (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	return (this_p->modulo);
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

// TODO: max len for result.  buffer overflow?

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
	// FIXME:  For ALL strcat: Pass in sizeof result and use strlcat.
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

	// dw_printf ("DEBUG ax25_format_addrs, num_addr = %d, result = '%s'\n", this_p->num_addr, result);
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_format_via_path
 *
 * Purpose:	Format via path addresses suitable for printing.
 *
 * Inputs:	Current packet.
 *
 *		result_size	- Number of bytes available for result.
 *				  We can have up to 8 addresses x 9 characters
 *				  plus 7 commas, possible *, and nul = 81 minimum.
 *
 * Outputs:	result	- Digipeater field addresses combined into a single string of the form:
 *
 *				"repeater, repeater ..."
 *
 *			An asterisk is displayed after the last digipeater
 *			with the "H" bit set.  e.g.  If we hear RPT2,
 *
 *			RPT1,RPT2*,RPT3
 *
 *			No asterisk means the source is being heard directly.
 *
 *------------------------------------------------------------------*/

void ax25_format_via_path (packet_t this_p, char *result, size_t result_size)
{
	int i;
	int heard;
	char stemp[AX25_MAX_ADDR_LEN];

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);
	*result = '\0';

	/* Don't get upset if no addresses.  */
	/* This will allow packets that do not comply to AX.25 format. */

	if (this_p->num_addr == 0) {
	  return;
	}

	heard = ax25_get_heard(this_p);

	for (i=(int)AX25_REPEATER_1; i<this_p->num_addr; i++) {
	  if (i > (int)AX25_REPEATER_1) {
	    strlcat (result, ",", result_size);
	  }
	  ax25_get_addr_with_ssid (this_p, i, stemp);
	  strlcat (result, stemp, result_size);
	  if (i == heard) {
	    strlcat (result, "*", result_size);
	  }
	}

} /* end ax25_format_via_path */


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

	assert (this_p->frame_len >= 0 && this_p->frame_len <= AX25_MAX_PACKET_LEN);

	memcpy (result, this_p->frame_data, this_p->frame_len);

	return (this_p->frame_len);
}



/*------------------------------------------------------------------
 *
 * Function:	ax25_frame_type
 *
 * Purpose:	Extract the type of frame.
 *		This is derived from the control byte(s) but
 *		is an enumerated type for easier handling.
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Outputs:	desc	- Text description such as "I frame" or
 *			  "U frame SABME".   
 *			  Supply 56 bytes to be safe.
 *
 *		cr	- Command or response?
 *
 *		pf	- P/F - Poll/Final or -1 if not applicable
 *
 *		nr	- N(R) - receive sequence or -1 if not applicable.
 *
 *		ns	- N(S) - send sequence or -1 if not applicable.
 *	
 * Returns:	Frame type from  enum ax25_frame_type_e.
 *
 *------------------------------------------------------------------*/

// TODO: need someway to ensure caller allocated enough space.
// Should pass in as parameter.
#define DESC_SIZ 56


ax25_frame_type_t ax25_frame_type (packet_t this_p, cmdres_t *cr, char *desc, int *pf, int *nr, int *ns) 
{
	int c;		// U frames are always one control byte.
	int c2 = 0;	// I & S frames can have second Control byte.

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	strlcpy (desc, "????", DESC_SIZ);
	*cr = cr_11;
	*pf = -1;
	*nr = -1;
	*ns = -1;

	c = ax25_get_control(this_p);
	if (c < 0) {
	  strlcpy (desc, "Not AX.25", DESC_SIZ);
	  return (frame_not_AX25);
	}

/*
 * TERRIBLE HACK :-(  for display purposes.
 *
 * I and S frames can have 1 or 2 control bytes but there is
 * no good way to determine this without dipping into the data
 * link state machine.  Can we guess?
 *
 * S frames have no protocol id or information so if there is one
 * more byte beyond the control field, we could assume there are
 * two control bytes.
 *
 * For I frames, the protocol id will usually be 0xf0.  If we find
 * that as the first byte of the information field, it is probably
 * the pid and not part of the information.  Ditto for segments 0x08.
 * Not fool proof but good enough for troubleshooting text out.
 *
 * If we have a link to the peer station, this will be set properly
 * before it needs to be used for other reasons.
 *
 * Setting one of the RR bits (find reference!) is sounding better and better.
 * It's in common usage so I should lobby to get that in the official protocol spec.
 */

	if (this_p->modulo == 0 && (c & 3) == 1 && ax25_get_c2(this_p) != -1) {
	  this_p->modulo = modulo_128;
	}
	else if (this_p->modulo == 0 && (c & 1) == 0 && this_p->frame_data[ax25_get_info_offset(this_p)] == 0xF0) {
	  this_p->modulo = modulo_128;
	}
	else if (this_p->modulo == 0 && (c & 1) == 0 && this_p->frame_data[ax25_get_info_offset(this_p)] == 0x08) {	// same for segments
	  this_p->modulo = modulo_128;
	}


	if (this_p->modulo == modulo_128) {
	  c2 = ax25_get_c2 (this_p);
	}

	int dst_c = this_p->frame_data[AX25_DESTINATION * 7 + 6] & SSID_H_MASK;
	int src_c = this_p->frame_data[AX25_SOURCE * 7 + 6] & SSID_H_MASK;

	char cr_text[8];
	char pf_text[8];

	if (dst_c) {
	  if (src_c) { *cr = cr_11;  strcpy(cr_text,"cc=11"); strcpy(pf_text,"p/f"); }
	  else       { *cr = cr_cmd; strcpy(cr_text,"cmd");   strcpy(pf_text,"p"); }
	}
	else {
	  if (src_c) { *cr = cr_res; strcpy(cr_text,"res");   strcpy(pf_text,"f"); }
	  else       { *cr = cr_00;  strcpy(cr_text,"cc=00"); strcpy(pf_text,"p/f"); }
	}

	if ((c & 1) == 0) {

// Information 			rrr p sss 0		or	sssssss 0  rrrrrrr p

	  if (this_p->modulo == modulo_128) {
	    *ns = (c >> 1) & 0x7f;
	    *pf = c2 & 1;
	    *nr = (c2 >> 1) & 0x7f;
	  }
	  else {
	    *ns = (c >> 1) & 7;
	    *pf = (c >> 4) & 1;
	    *nr = (c >> 5) & 7;
	  }

	  //snprintf (desc, DESC_SIZ, "I %s, n(s)=%d, n(r)=%d, %s=%d", cr_text, *ns, *nr, pf_text, *pf);
	  snprintf (desc, DESC_SIZ, "I %s, n(s)=%d, n(r)=%d, %s=%d, pid=0x%02x", cr_text, *ns, *nr, pf_text, *pf, ax25_get_pid(this_p));
	  return (frame_type_I);
	}
	else if ((c & 2) == 0) {

// Supervisory			rrr p/f ss 0 1		or	0000 ss 0 1  rrrrrrr p/f

	  if (this_p->modulo == modulo_128) {
	    *pf = c2 & 1;
	    *nr = (c2 >> 1) & 0x7f;
	  }
	  else {
	    *pf = (c >> 4) & 1;
	    *nr = (c >> 5) & 7;
	  }

 
	  switch ((c >> 2) & 3) {
	    case 0: snprintf (desc, DESC_SIZ, "RR %s, n(r)=%d, %s=%d", cr_text, *nr, pf_text, *pf);   return (frame_type_S_RR);   break;
	    case 1: snprintf (desc, DESC_SIZ, "RNR %s, n(r)=%d, %s=%d", cr_text, *nr, pf_text, *pf);  return (frame_type_S_RNR);  break;
	    case 2: snprintf (desc, DESC_SIZ, "REJ %s, n(r)=%d, %s=%d", cr_text, *nr, pf_text, *pf);  return (frame_type_S_REJ);  break;
	    case 3: snprintf (desc, DESC_SIZ, "SREJ %s, n(r)=%d, %s=%d", cr_text, *nr, pf_text, *pf); return (frame_type_S_SREJ); break;
	 } 
	}
	else {

// Unnumbered			mmm p/f mm 1 1

	  *pf = (c >> 4) & 1;
	  
	  switch (c & 0xef) {
	
	    case 0x6f: snprintf (desc, DESC_SIZ, "SABME %s, %s=%d",	cr_text, pf_text, *pf);  return (frame_type_U_SABME); break;
	    case 0x2f: snprintf (desc, DESC_SIZ, "SABM %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_SABM);  break;
	    case 0x43: snprintf (desc, DESC_SIZ, "DISC %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_DISC);  break;
	    case 0x0f: snprintf (desc, DESC_SIZ, "DM %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_DM);    break;
	    case 0x63: snprintf (desc, DESC_SIZ, "UA %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_UA);    break;
	    case 0x87: snprintf (desc, DESC_SIZ, "FRMR %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_FRMR);  break;
	    case 0x03: snprintf (desc, DESC_SIZ, "UI %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_UI);    break;
	    case 0xaf: snprintf (desc, DESC_SIZ, "XID %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_XID);   break;
	    case 0xe3: snprintf (desc, DESC_SIZ, "TEST %s, %s=%d", 	cr_text, pf_text, *pf);  return (frame_type_U_TEST);  break;
	    default:   snprintf (desc, DESC_SIZ, "U other???");        				 return (frame_type_U);       break;
	  }
	}

	// Should be unreachable but compiler doesn't realize that.
	// Here only to suppress "warning: control reaches end of non-void function"

	return (frame_not_AX25);

} /* end ax25_frame_type */



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
// FIXME:  this is wrong.  It doesn't handle modulo 128.

// TODO: use ax25_frame_type() instead.

static void ctrl_to_text (int c, char *out, size_t outsiz)
{
	if      ((c & 1) == 0)       { snprintf (out, outsiz, "I frame: n(r)=%d, p=%d, n(s)=%d",  (c>>5)&7, (c>>4)&1, (c>>1)&7); }
	else if ((c & 0xf) == 0x01)  { snprintf (out, outsiz, "S frame RR: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x05)  { snprintf (out, outsiz, "S frame RNR: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x09)  { snprintf (out, outsiz, "S frame REJ: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xf) == 0x0D)  { snprintf (out, outsiz, "S frame sREJ: n(r)=%d, p/f=%d",  (c>>5)&7, (c>>4)&1); }
	else if ((c & 0xef) == 0x6f) { snprintf (out, outsiz, "U frame SABME: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x2f) { snprintf (out, outsiz, "U frame SABM: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x43) { snprintf (out, outsiz, "U frame DISC: p=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x0f) { snprintf (out, outsiz, "U frame DM: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x63) { snprintf (out, outsiz, "U frame UA: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x87) { snprintf (out, outsiz, "U frame FRMR: f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0x03) { snprintf (out, outsiz, "U frame UI: p/f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0xAF) { snprintf (out, outsiz, "U frame XID: p/f=%d", (c>>4)&1); }
	else if ((c & 0xef) == 0xe3) { snprintf (out, outsiz, "U frame TEST: p/f=%d", (c>>4)&1); }
	else                         { snprintf (out, outsiz, "Unknown frame type for control = 0x%02x", c); }
}

/* Text description of protocol id octet. */

#define PID_TEXT_SIZE 80

static void pid_to_text (int p, char out[PID_TEXT_SIZE])
{

	if      ((p & 0x30) == 0x10) { snprintf (out, PID_TEXT_SIZE, "AX.25 layer 3 implemented."); }
	else if ((p & 0x30) == 0x20) { snprintf (out, PID_TEXT_SIZE, "AX.25 layer 3 implemented."); }
	else if (p == 0x01)          { snprintf (out, PID_TEXT_SIZE, "ISO 8208/CCITT X.25 PLP"); }
	else if (p == 0x06)          { snprintf (out, PID_TEXT_SIZE, "Compressed TCP/IP packet. Van Jacobson (RFC 1144)"); }
	else if (p == 0x07)          { snprintf (out, PID_TEXT_SIZE, "Uncompressed TCP/IP packet. Van Jacobson (RFC 1144)"); }
	else if (p == 0x08)          { snprintf (out, PID_TEXT_SIZE, "Segmentation fragment"); }
	else if (p == 0xC3)          { snprintf (out, PID_TEXT_SIZE, "TEXNET datagram protocol"); }
	else if (p == 0xC4)          { snprintf (out, PID_TEXT_SIZE, "Link Quality Protocol"); }
	else if (p == 0xCA)          { snprintf (out, PID_TEXT_SIZE, "Appletalk"); }
	else if (p == 0xCB)          { snprintf (out, PID_TEXT_SIZE, "Appletalk ARP"); }
	else if (p == 0xCC)          { snprintf (out, PID_TEXT_SIZE, "ARPA Internet Protocol"); }
	else if (p == 0xCD)          { snprintf (out, PID_TEXT_SIZE, "ARPA Address resolution"); }
	else if (p == 0xCE)          { snprintf (out, PID_TEXT_SIZE, "FlexNet"); }
	else if (p == 0xCF)          { snprintf (out, PID_TEXT_SIZE, "NET/ROM"); }
	else if (p == 0xF0)          { snprintf (out, PID_TEXT_SIZE, "No layer 3 protocol implemented."); }
	else if (p == 0xFF)          { snprintf (out, PID_TEXT_SIZE, "Escape character. Next octet contains more Level 3 protocol information."); }
	else                         { snprintf (out, PID_TEXT_SIZE, "Unknown protocol id = 0x%02x", p); }
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

	  ctrl_to_text (c, cp_text, sizeof(cp_text)); // TODO: use ax25_frame_type() instead.

	  if ( (c & 0x01) == 0 ||				/* I   xxxx xxx0 */
	     	c == 0x03 || c == 0x13) {			/* UI  000x 0011 */

	    char pid_text[PID_TEXT_SIZE];

	    pid_to_text (p, pid_text);

	    strlcat (cp_text, ", ", sizeof(cp_text));
	    strlcat (cp_text, pid_text, sizeof(cp_text));

	  }

	  snprintf (l_text, sizeof(l_text), ", length = %d", flen);
	  strlcat (cp_text, l_text, sizeof(cp_text));

	  dw_printf ("%s\n", cp_text);
	}

	// Address fields must be only upper case letters and digits.
	// If less than 6 characters, trailing positions are filled with ASCII space.
	// Using all zero bits in one of these 6 positions is wrong.
	// Any non printable characters will be printed as "." here.

	dw_printf (" dest    %c%c%c%c%c%c %2d c/r=%d res=%d last=%d\n",
				isprint(fptr[0]>>1) ? fptr[0]>>1 : '.',
				isprint(fptr[1]>>1) ? fptr[1]>>1 : '.',
				isprint(fptr[2]>>1) ? fptr[2]>>1 : '.',
				isprint(fptr[3]>>1) ? fptr[3]>>1 : '.',
				isprint(fptr[4]>>1) ? fptr[4]>>1 : '.',
				isprint(fptr[5]>>1) ? fptr[5]>>1 : '.',
				(fptr[6]&SSID_SSID_MASK)>>SSID_SSID_SHIFT,
				(fptr[6]&SSID_H_MASK)>>SSID_H_SHIFT, 
				(fptr[6]&SSID_RR_MASK)>>SSID_RR_SHIFT,
				fptr[6]&SSID_LAST_MASK);

	dw_printf (" source  %c%c%c%c%c%c %2d c/r=%d res=%d last=%d\n", 
				isprint(fptr[7]>>1) ? fptr[7]>>1 : '.',
				isprint(fptr[8]>>1) ? fptr[8]>>1 : '.',
				isprint(fptr[9]>>1) ? fptr[9]>>1 : '.',
				isprint(fptr[10]>>1) ? fptr[10]>>1 : '.',
				isprint(fptr[11]>>1) ? fptr[11]>>1 : '.',
				isprint(fptr[12]>>1) ? fptr[12]>>1 : '.',
				(fptr[13]&SSID_SSID_MASK)>>SSID_SSID_SHIFT,
				(fptr[13]&SSID_H_MASK)>>SSID_H_SHIFT, 
				(fptr[13]&SSID_RR_MASK)>>SSID_RR_SHIFT,
				fptr[13]&SSID_LAST_MASK);

	for (n=2; n<this_p->num_addr; n++) {	

	  dw_printf (" digi %d  %c%c%c%c%c%c %2d   h=%d res=%d last=%d\n", 
				n - 1,
				isprint(fptr[n*7+0]>>1) ? fptr[n*7+0]>>1 : '.',
				isprint(fptr[n*7+1]>>1) ? fptr[n*7+1]>>1 : '.',
				isprint(fptr[n*7+2]>>1) ? fptr[n*7+2]>>1 : '.',
				isprint(fptr[n*7+3]>>1) ? fptr[n*7+3]>>1 : '.',
				isprint(fptr[n*7+4]>>1) ? fptr[n*7+4]>>1 : '.',
				isprint(fptr[n*7+5]>>1) ? fptr[n*7+5]>>1 : '.',
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

	if (this_p->frame_len == 0) return(0);

	ctrl = ax25_get_control(this_p);
	pid = ax25_get_pid(this_p);

	is_aprs = this_p->num_addr >= 2 && ctrl == AX25_UI_FRAME && pid == AX25_PID_NO_LAYER_3;

#if 0 
        text_color_set(DW_COLOR_ERROR);
        dw_printf ("ax25_is_aprs(): ctrl=%02x, pid=%02x, is_aprs=%d\n", ctrl, pid, is_aprs);
#endif
	return (is_aprs);
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_is_null_frame
 *
 * Purpose:	Is this packet structure empty?
 *
 * Inputs:	this_p	- pointer to packet object.
 *
 * Returns:	True if frame data length is 0.
 *
 * Description:	This is used when we want to wake up the
 *		transmit queue processing thread but don't
 *		want to transmit a frame.
 *
 *------------------------------------------------------------------*/


int ax25_is_null_frame (packet_t this_p)
{
	int is_null;

	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	is_null = this_p->frame_len == 0;

#if 0
        text_color_set(DW_COLOR_ERROR);
        dw_printf ("ax25_is_null_frame(): is_null=%d\n", is_null);
#endif
	return (is_null);
}


/*------------------------------------------------------------------
 *
 * Function:	ax25_get_control
 		ax25_get_c2
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

	if (this_p->frame_len == 0) return(-1);

	if (this_p->num_addr >= 2) {
	  return (this_p->frame_data[ax25_get_control_offset(this_p)]);
	}
	return (-1);
}

int ax25_get_c2 (packet_t this_p) 
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	if (this_p->frame_len == 0) return(-1);

	if (this_p->num_addr >= 2) {
	  int offset2 = ax25_get_control_offset(this_p)+1;

	  if (offset2 < this_p->frame_len) {
	    return (this_p->frame_data[offset2]);
	  }
	  else {
	    return (-1);	/* attempt to go beyond the end of frame. */
	  }
	}
	return (-1);		/* not AX.25 */
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

	// TODO: handle 2 control byte case.
	// TODO: sanity check: is it I or UI frame?

	if (this_p->frame_len == 0) return(-1);

	if (this_p->num_addr >= 2) {
	  return (this_p->frame_data[ax25_get_pid_offset(this_p)]);
	}
	return (-1);
}



/*------------------------------------------------------------------
 *
 * Function:	ax25_get_frame_len
 *
 * Purpose:	Get length of frame.
 *
 * Inputs:	this_p	- pointer to packet object.
 *		
 * Returns:	Number of octets in the frame buffer.  
 *		Does NOT include the extra 2 for FCS.
 *
 *------------------------------------------------------------------*/

int ax25_get_frame_len (packet_t this_p) 
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	assert (this_p->frame_len >= 0 && this_p->frame_len <= AX25_MAX_PACKET_LEN);

	return (this_p->frame_len);

} /* end ax25_get_frame_len */


unsigned char *ax25_get_frame_data_ptr (packet_t this_p)
{
	assert (this_p->magic1 == MAGIC);
	assert (this_p->magic2 == MAGIC);

	return (this_p->frame_data);

} /* end ax25_get_frame_data_ptr */


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
 *		There is a 1 / 65536 chance of getting a false positive match
 *		which is good enough for this application.
 *		We could reduce that with a 32 bit CRC instead of reusing
 *		code from the AX.25 frame CRC calculation.
 *
 * Version 1.3:	We exclude any trailing CR/LF at the end of the info part
 *		so we can detect duplicates that are received only over the
 *		air and those which have gone thru an IGate where the process
 *		removes any trailing CR/LF.   Example:
 *
 *		Original via RF only:
 *		W1TG-1>APU25N,N3LEE-10*,WIDE2-1:<IGATE,MSG_CNT=30,LOC_CNT=61<0x0d>
 *
 *		When we get the same thing via APRS-IS:
 *		W1TG-1>APU25N,K1FFK,WIDE2*,qAR,WB2ZII-15:<IGATE,MSG_CNT=30,LOC_CNT=61
 *
 *		(Actually there is a trailing space.  Maybe some systems
 *		change control characters to space???)
 *		Hmmmm.  I guess we should ignore trailing space as well for 
 *		duplicate detection and suppression.
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

	while (info_len >= 1 && (pinfo[info_len-1] == '\r' ||
	                         pinfo[info_len-1] == '\n' ||
	                         pinfo[info_len-1] == ' ')) {

	// Temporary for debugging!

	//  if (pinfo[info_len-1] == ' ') {
	//    text_color_set(DW_COLOR_ERROR);
	//    dw_printf ("DEBUG:  ax25_dedupe_crc ignoring trailing space.\n");
	//  }

	  info_len--;
	}

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

	// TODO: I think this can be more efficient by getting the packet content pointer instead of copying.
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
 *		len	- Number of bytes.  If < 0 we use strlen().
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
 * Trailing space:
 *		I recently noticed a case where a packet has space character
 *		at the end.  If the last character of the line is a space,
 *		this will be displayed in hexadecimal to make it obvious.
 *			
 *------------------------------------------------------------------*/

//#define MAXSAFE 500
#define MAXSAFE AX25_MAX_INFO_LEN

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

	while (len > 0)
	{
	  ch = *((unsigned char *)pstr);

	  if (ch == ' ' && (len == 1 || pstr[1] == '\0')) {

	      snprintf (safe_str + safe_len, sizeof(safe_str)-safe_len, "<0x%02x>", ch);
	      safe_len += 6;
	  }
	  else if (ch < ' ' || ch == 0x7f || ch == 0xfe || ch == 0xff ||
			(ascii_only && ch >= 0x80) ) {

	      /* Control codes and delete. */
	      /* UTF-8 does not use fe and ff except in a possible */
	      /* "Byte Order Mark" (BOM) at the beginning. */

	      snprintf (safe_str + safe_len, sizeof(safe_str)-safe_len, "<0x%02x>", ch);
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

// TODO1.2: should return string rather printing to remove a race condition.

	dw_printf ("%s", safe_str);

} /* end ax25_safe_print */



/*------------------------------------------------------------------
 *
 * Function:	ax25_alevel_to_text
 *
 * Purpose:	Convert audio level to text representation.
 *
 * Inputs:	alevel	- Audio levels collected from demodulator.
 *
 * Outputs:	text	- Text representation for presentation to user.  
 *			  Currently it will look something like this:
 *
 *				r(m/s)
 *
 *			  With n,m,s corresponding to received, mark, and space.
 *			  Comma is to be avoided because one place this 
 *			  ends up is in a CSV format file.
 *
 *			  size should be AX25_ALEVEL_TO_TEXT_SIZE.
 *
 * Returns:	True if something to print.  (currently if alevel.original >= 0)
 *		False if not.
 *
 * Description:	Audio level used to be simple; it was a single number.
 *		In version 1.2, we start collecting more details.
 *		At the moment, it includes:
 *
 *		- Received level from new method.  
 *		- Levels from mark & space filters to examine the ratio.
 *
 *		We print this in multiple places so put it into a function.
 *			
 *------------------------------------------------------------------*/


int ax25_alevel_to_text (alevel_t alevel, char text[AX25_ALEVEL_TO_TEXT_SIZE])
{
	if (alevel.rec < 0) {
	  strlcpy (text, "", AX25_ALEVEL_TO_TEXT_SIZE);
	  return (0);
	}

// TODO1.2: haven't thought much about non-AFSK cases yet.
// What should we do for 9600 baud?

// For DTMF omit the two extra numbers.

	if (alevel.mark >= 0 &&  alevel.space < 0) {		/* baseband */

	  snprintf (text, AX25_ALEVEL_TO_TEXT_SIZE, "%d(%+d/%+d)", alevel.rec, alevel.mark, alevel.space);
	}
	else if ((alevel.mark == -1 &&  alevel.space == -1) ||		/* PSK */
		(alevel.mark == -99 &&  alevel.space == -99)) {		/* v. 1.7 "B" FM demodulator. */
									// ?? Where does -99 come from?

	  snprintf (text, AX25_ALEVEL_TO_TEXT_SIZE, "%d", alevel.rec);
	}
	else if (alevel.mark == -2 &&  alevel.space == -2) {		/* DTMF - single number. */

	  snprintf (text, AX25_ALEVEL_TO_TEXT_SIZE, "%d", alevel.rec);
	}
	else {		/* AFSK */

	  //snprintf (text, AX25_ALEVEL_TO_TEXT_SIZE, "%d:%d(%d/%d=%05.3f=)", alevel.original, alevel.rec, alevel.mark, alevel.space, alevel.ms_ratio);
	  snprintf (text, AX25_ALEVEL_TO_TEXT_SIZE, "%d(%d/%d)", alevel.rec, alevel.mark, alevel.space);
	}
	return (1);	

} /* end ax25_alevel_to_text */


/* end ax25_pad.c */


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2016, 2017  John Langner, WB2OSZ
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



/*------------------------------------------------------------------
 *
 * Module:      xid.c
 *
 * Purpose:   	Encode and decode the info field of XID frames.
 *		
 * Description:	If we originate the connection, and the other end is
 *		capable of AX.25 version 2.2,
 *
 *		 - We send an XID command frame with our capabilities.
 *		 - the other sends back an XID response, possibly
 *			reducing some values to be acceptable there.
 *		 - Both ends use the values in that response.
 *
 *		If the other end originates the connection,
 *
 *		  - It sends XID command frame with its capabilities.
 *		  - We might decrease some of them to be acceptable.
 *		  - Send XID response.
 *		  - Both ends use values in my response.
 *
 * References:	AX.25 Protocol Spec, sections 4.3.3.7 & 6.3.2.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "textcolor.h"
#include "xid.h"



#define FI_Format_Indicator	0x82	
#define GI_Group_Identifier	0x80

#define PI_Classes_of_Procedures	2	
#define PI_HDLC_Optional_Functions	3	
#define PI_I_Field_Length_Rx		6	
#define PI_Window_Size_Rx		8	
#define PI_Ack_Timer			9	
#define PI_Retries			10	

// Forget about the bit order at the physical layer (e.g. HDLC).
// It doesn't matter at all here.  We are dealing with bytes.
// A different encoding could send the bits in the opposite order.

// The bit numbers are confusing because this one table (Fig. 4.5) starts
// with 1 for the LSB when everywhere else refers to the LSB as bit 0.

// The first byte must be of the form	0xx0 0001
// The second byte must be of the form	0000 0000
// If we process the two byte "Classes of Procedures" like
// the other multibyte numeric fields, with the more significant
// byte first, we end up with the bit masks below.
// The bit order would be  8 7 6 5 4 3 2 1   16 15 14 13 12 11 10 9

// (This has nothing to do with the HDLC serializing order.
// I'm talking about the way we would normally write binary numbers.)

#define PV_Classes_Procedures_Balanced_ABM	0x0100
#define PV_Classes_Procedures_Half_Duplex	0x2000
#define PV_Classes_Procedures_Full_Duplex	0x4000


// The first byte must be of the form	1000 0xx0
// The second byte must be of the form	1010 xx00
// The third byte must be of the form	0000 0010
// If we process the three byte "HDLC Optional Parameters" like
// the other multibyte numeric fields, with the most significant
// byte first, we end up with bit masks like this.
// The bit order would be  8 7 6 5 4 3 2 1   16 15 14 13 12 11 10 9   24 23 22 21 20 19 18 17

#define PV_HDLC_Optional_Functions_REJ_cmd_resp		0x020000
#define PV_HDLC_Optional_Functions_SREJ_cmd_resp	0x040000
#define PV_HDLC_Optional_Functions_Extended_Address 	0x800000

#define PV_HDLC_Optional_Functions_Modulo_8		0x000400
#define PV_HDLC_Optional_Functions_Modulo_128		0x000800
#define PV_HDLC_Optional_Functions_TEST_cmd_resp	0x002000
#define PV_HDLC_Optional_Functions_16_bit_FCS		0x008000

#define PV_HDLC_Optional_Functions_Multi_SREJ_cmd_resp 	0x000020
#define PV_HDLC_Optional_Functions_Segmenter	 	0x000040

#define PV_HDLC_Optional_Functions_Synchronous_Tx	0x000002


/*-------------------------------------------------------------------
 *
 * Name:        xid_parse
 *
 * Purpose:    	Decode information part of XID frame into individual values.
 *
 * Inputs:	info		- pointer to information part of frame.
 *
 *		info_len	- Number of bytes in information part of frame.
 *				  Could be 0.
 *
 *		desc_size	- Size of desc.  100 is good.
 *
 * Outputs:	result		- Structure with extracted values.
 *
 *		desc		- Text description for troubleshooting.
 *
 * Returns:	1 for mostly successful (with possible error messages), 0 for failure.
 *
 * Description:	6.3.2 "The receipt of an XID response from the other station
 *		establishes that both stations are using AX.25 version
 *		2.2 or higher and enables the use of the segmenter/reassembler
 *		and selective reject."
 *
 *--------------------------------------------------------------------*/


int xid_parse (unsigned char *info, int info_len, struct xid_param_s *result, char *desc, int desc_size)
{
	unsigned char *p;
	int group_len;
	char stemp[64];


	strlcpy (desc, "", desc_size);

// What should we do when some fields are missing?

// The  AX.25 v2.2 protocol spec says, for most of these,
//	"If this field is not present, the current values are retained."

// We set the numeric values to our usual G_UNKNOWN to mean undefined and let the caller deal with it.
// rej and modulo are enum so we can't use G_UNKNOWN there.

	result->full_duplex = G_UNKNOWN;
	result->srej = srej_not_specified;
	result->modulo = modulo_unknown;
	result->i_field_length_rx = G_UNKNOWN;
	result->window_size_rx = G_UNKNOWN;
	result->ack_timer = G_UNKNOWN;
	result->retries = G_UNKNOWN;


/* Information field is optional but that seems pretty lame. */

	if (info_len == 0) {
	  return (1);
	}

	p = info;

	if (*p != FI_Format_Indicator) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("XID error: First byte of info field should be Format Indicator, %02x.\n", FI_Format_Indicator);
	  dw_printf ("XID info part: %02x %02x %02x %02x %02x ... length=%d\n", info[0], info[1], info[2], info[3], info[4], info_len);
	  return 0;
	}
	p++;

	if (*p != GI_Group_Identifier) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("XID error: Second byte of info field should be Group Indicator, %d.\n", GI_Group_Identifier);
	  return 0;
	}
	p++;

	group_len = *p++;
	group_len = (group_len << 8) + *p++;
	
	while (p < info + 4 + group_len) {

	  int pind, plen, pval, j;

	  pind = *p++;
	  plen = *p++;		// should have sanity checking
	  if (plen < 1 || plen > 4) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("XID error: Length ?????   TODO   ????  %d.\n", plen);
	    return (1);		// got this far.
	  }
	  pval = 0;
	  for (j=0; j<plen; j++) {
	    pval = (pval << 8) + *p++;
	  }

	  switch (pind) {

	    case PI_Classes_of_Procedures:
	      
	      if ( ! (pval & PV_Classes_Procedures_Balanced_ABM)) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected Balanced ABM to be set.\n");	
	      }

	      if (pval & PV_Classes_Procedures_Half_Duplex && ! (pval & PV_Classes_Procedures_Full_Duplex)) {
	        result->full_duplex = 0;
	        strlcat (desc, "Half-Duplex ", desc_size);
	      }
	      else if (pval & PV_Classes_Procedures_Full_Duplex && ! (pval & PV_Classes_Procedures_Half_Duplex)) {
	        result->full_duplex = 1;
	        strlcat (desc, "Full-Duplex ", desc_size);
	      }
	      else {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected one of Half or Full Duplex be set.\n");	
	        result->full_duplex = 0;
	      }

	      break;

	    case PI_HDLC_Optional_Functions:	

	      // Pick highest of those offered.

	      if (pval & PV_HDLC_Optional_Functions_REJ_cmd_resp) {
	        strlcat (desc, "REJ ", desc_size);
	      }
	      if (pval & PV_HDLC_Optional_Functions_SREJ_cmd_resp) {
	        strlcat (desc, "SREJ ", desc_size);
	      }
	      if (pval & PV_HDLC_Optional_Functions_Multi_SREJ_cmd_resp) {
	        strlcat (desc, "Multi-SREJ ", desc_size);
	      }

	      if (pval & PV_HDLC_Optional_Functions_Multi_SREJ_cmd_resp) {
	        result->srej = srej_multi;
	      }
	      else if (pval & PV_HDLC_Optional_Functions_SREJ_cmd_resp) {
	        result->srej = srej_single;
	      }
	      else if (pval & PV_HDLC_Optional_Functions_REJ_cmd_resp) {
	        result->srej = srej_none;
	      }
	      else {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected at least one of REJ, SREJ, Multi-SREJ to be set.\n");
	        result->srej = srej_none;
	      }

	      if ((pval & PV_HDLC_Optional_Functions_Modulo_8) && ! (pval & PV_HDLC_Optional_Functions_Modulo_128)) {
	        result->modulo = modulo_8;
	        strlcat (desc, "modulo-8 ", desc_size);
	      }
	      else if ((pval & PV_HDLC_Optional_Functions_Modulo_128) && ! (pval & PV_HDLC_Optional_Functions_Modulo_8)) {
	        result->modulo = modulo_128;
	        strlcat (desc, "modulo-128 ", desc_size);
	      }
	      else {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected one of Modulo 8 or 128 be set.\n");	
	      }

	      if ( ! (pval & PV_HDLC_Optional_Functions_Extended_Address)) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected Extended Address to be set.\n");	
	      }

	      if ( ! (pval & PV_HDLC_Optional_Functions_TEST_cmd_resp)) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected TEST cmd/resp to be set.\n");	
	      }

	      if ( ! (pval & PV_HDLC_Optional_Functions_16_bit_FCS)) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected 16 bit FCS to be set.\n");	
	      }

	      if ( ! (pval & PV_HDLC_Optional_Functions_Synchronous_Tx)) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected Synchronous Tx to be set.\n");	
	      }

	      break;

	    case PI_I_Field_Length_Rx:	
	      
	      result->i_field_length_rx = pval / 8;

	      snprintf (stemp, sizeof(stemp), "I-Field-Length-Rx=%d ", result->i_field_length_rx);
	      strlcat (desc, stemp, desc_size);

	      if (pval & 0x7) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: I Field Length Rx, %d, is not a whole number of bytes.\n", pval);
	      }

	      break;

	    case PI_Window_Size_Rx:	

	      result->window_size_rx = pval;

	      snprintf (stemp, sizeof(stemp), "Window-Size-Rx=%d ", result->window_size_rx);
	      strlcat (desc, stemp, desc_size);

	      if (pval < 1 || pval > 127) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Window Size Rx, %d, is not in range of 1 thru 127.\n", pval);
	        result->window_size_rx = 127;
		// Let the caller deal with modulo 8 consideration.
	      }

//continue here with more error checking.

	      break;

	    case PI_Ack_Timer:	
	      result->ack_timer = pval;

	      snprintf (stemp, sizeof(stemp), "Ack-Timer=%d ", result->ack_timer);
	      strlcat (desc, stemp, desc_size);

	      break;

	    case PI_Retries:			// Is it retrys or retries?
	      result->retries = pval;

	      snprintf (stemp, sizeof(stemp), "Retries=%d ", result->retries);
	      strlcat (desc, stemp, desc_size);

	      break;

	    default:		
	      break;	// Ignore anything we don't recognize.
	  }
	}

	if (p != info + info_len) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("XID error: Frame / Group Length mismatch.\n");	
	}

	return (1);

} /* end xid_parse */


/*-------------------------------------------------------------------
 *
 * Name:        xid_encode
 *
 * Purpose:    	Encode the information part of an XID frame.
 *
 * Inputs:	param->
 *			full_duplex	- As command, am I capable of full duplex operation?
 *					  When a response, are we both?
 *					  0 = half duplex.
 *					  1 = full duplex.
 *
 * 			srej		- Level of selective reject.
 *					  srej_none (use REJ), srej_single, srej_multi
 *					  As command, offer a menu of what I can handle.  (i.e. perhaps multiple bits set)
 *					  As response, take minimum of what is offered and what I can handle. (one bit set)
 *
 *			modulo	- 8 or 128.
 *
 *			i_field_length_rx - Maximum number of bytes I can handle in info part.
 *					    Default is 256.
 *					    Up to 8191 will fit into the field.
 *					    Use G_UNKNOWN to omit this.
 *
 *			window_size_rx 	- Maximum window size ("k") that I can handle.
 *				   Defaults are are 4 for modulo 8 and 32 for modulo 128.
 *
 *			ack_timer	- Acknowledge timer in milliseconds.
 *					*** describe meaning.  ***
 *				  Default is 3000.
 *				  Use G_UNKNOWN to omit this.
 *
 *			retries		- Allows negotiation of retries.
 *				  Default is 10.
 *				  Use G_UNKNOWN to omit this.
 *
 *		cr	- Is it a command or response?
 *
 * Outputs:	info	- Information part of XID frame.
 *			  Does not include the control byte.
 *			  Use buffer of 40 bytes just to be safe.
 *
 * Returns:	Number of bytes in the info part.  Should be at most 27.
 *		Again, provide a larger space just to be safe in case this ever changes.
 *
 * Description:	6.3.2  "Parameter negotiation occurs at any time. It is accomplished by sending
 *		the XID command frame and receiving the XID response frame. Implementations of
 *		AX.25 prior to version 2.2 respond to an XID command frame with a FRMR response
 *		frame. The TNC receiving the FRMR uses a default set of parameters compatible
 *		with previous versions of AX.25."
 *
 *		"This version of AX.25 implements the negotiation or notification of six AX.25
 *		parameters. Notification simply tells the distant TNC some limit that cannot be exceeded.
 *		The distant TNC can choose to use the limit or some other value that is within the
 *		limits. Notification is used with the Window Size Receive (k) and Information
 *		Field Length Receive (N1) parameters. Negotiation involves both TNCs choosing a
 *		value that is mutually acceptable. The XID command frame contains a set of values
 *		acceptable to the originating TNC. The distant TNC chooses to accept the values
 *		offered, or other acceptable values, and places these values in the XID response.
 *		Both TNCs set themselves up based on the values used in the XID response. Negotiation
 *		is used by Classes of Procedures, HDLC Optional Functions, Acknowledge Timer and Retries."
 *
 * Comment:	I have a problem with "... occurs at any time."  What if we were in the middle
 *		of transferring a large file with k=32 then along comes XID which says switch to modulo 8?
 *
 * Insight:	Or is it Erratum?
 *		After reading the base standards documents, it seems that the XID command should offer
 *		up a menu of all the acceptable choices.  e.g.  REJ, SREJ, Multi-SREJ.  One or more bits
 *		can be set.  The XID response, would set a single bit which is the desired choice from
 *		among those offered.
 *		Should go back and review half/full duplex and modulo.
 *
 *--------------------------------------------------------------------*/


int xid_encode (struct xid_param_s *param, unsigned char *info, cmdres_t cr)
{
	unsigned char *p;
	int len;
	int x;
	int m = 0;


	p = info;

	*p++ = FI_Format_Indicator;
	*p++ = GI_Group_Identifier;
	*p++ = 0;

	m = 4;		// classes of procedures
	m += 5;		// HDLC optional features
	if (param->i_field_length_rx != G_UNKNOWN) m += 4;
	if (param->window_size_rx != G_UNKNOWN) m += 3;
	if (param->ack_timer != G_UNKNOWN) m += 4;
	if (param->retries != G_UNKNOWN) m += 3;

	*p++ = m;		// 0x17 if all present.

// "Classes of Procedures" has half / full duplex.

// We always send this.

	*p++ = PI_Classes_of_Procedures;
	*p++ = 2;
	
	x = PV_Classes_Procedures_Balanced_ABM;

	if (param->full_duplex == 1)
	  x |= PV_Classes_Procedures_Full_Duplex;
	else	// includes G_UNKNOWN
	  x |= PV_Classes_Procedures_Half_Duplex;

	*p++ = (x >> 8) & 0xff;
	*p++ = x & 0xff;

// "HDLC Optional Functions" contains REJ/SREJ & modulo 8/128.

// We always send this.
// Watch out for unknown values and do something reasonable.

	*p++ = PI_HDLC_Optional_Functions;
	*p++ = 3;

	x = PV_HDLC_Optional_Functions_Extended_Address |
		PV_HDLC_Optional_Functions_TEST_cmd_resp |
		PV_HDLC_Optional_Functions_16_bit_FCS | 
		PV_HDLC_Optional_Functions_Synchronous_Tx;

	//text_color_set (DW_COLOR_ERROR);
	//dw_printf ("******      XID temp hack - test no SREJ      ******\n");
	// param->srej = srej_none;

	if (cr == cr_cmd) {
	  // offer a "menu" of acceptable choices.  i.e. 1, 2 or 3 bits set.
	  switch (param->srej) {
	    case srej_none:
	    default:
	      x |= PV_HDLC_Optional_Functions_REJ_cmd_resp;
	      break;
	    case srej_single:
	      x |= PV_HDLC_Optional_Functions_REJ_cmd_resp |
	           PV_HDLC_Optional_Functions_SREJ_cmd_resp;
	      break;
	    case srej_multi:
	      x |= PV_HDLC_Optional_Functions_REJ_cmd_resp |
	           PV_HDLC_Optional_Functions_SREJ_cmd_resp |
	           PV_HDLC_Optional_Functions_Multi_SREJ_cmd_resp;
	      break;
	  }
	}
	else {
	  // for response, set only a single bit.
	  switch (param->srej) {
	    case srej_none:
	    default:
	      x |= PV_HDLC_Optional_Functions_REJ_cmd_resp;
	      break;
	    case srej_single:
	      x |= PV_HDLC_Optional_Functions_SREJ_cmd_resp;
	      break;
	    case srej_multi:
	      x |= PV_HDLC_Optional_Functions_Multi_SREJ_cmd_resp;
	      break;
	  }
	}

	if (param->modulo == modulo_128)
	  x |= PV_HDLC_Optional_Functions_Modulo_128;
	else	// includes modulo_8 and modulo_unknown
	  x |= PV_HDLC_Optional_Functions_Modulo_8;

	*p++ = (x >> 16) & 0xff;
	*p++ = (x >> 8) & 0xff;
	*p++ = x & 0xff;

// The rest are skipped if undefined values.

// "I Field Length Rx" - max I field length acceptable to me.
// This is in bits.  8191 would be max number of bytes to fit in field.

	if (param->i_field_length_rx != G_UNKNOWN) {
	  *p++ = PI_I_Field_Length_Rx;
	  *p++ = 2;
	  x = param->i_field_length_rx * 8;
	  *p++ = (x >> 8) & 0xff;
	  *p++ = x & 0xff;
	}

// "Window Size Rx"

	if (param->window_size_rx != G_UNKNOWN) {
	  *p++ = PI_Window_Size_Rx;
	  *p++ = 1;
	  *p++ = param->window_size_rx;
	}

// "Ack Timer" milliseconds.  We could handle up to 65535 here.

	if (param->ack_timer != G_UNKNOWN) {
	  *p++ = PI_Ack_Timer;
	  *p++ = 2;
	  *p++ = (param->ack_timer >> 8) & 0xff;
	  *p++ = param->ack_timer & 0xff;
	}

// "Retries."

	if (param->retries != G_UNKNOWN) {
	  *p++ = PI_Retries;
	  *p++ = 1;
	  *p++ = param->retries;
	}

	len = p - info;

	return (len);

} /* end xid_encode */



/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:    	Unit test for other functions here.
 *
 * Description:	Run with:
 *
 *			gcc -DXIDTEST -g xid.c textcolor.o && ./a
 *
 *		Result should be:
 *
 *			XID test:  Success.
 *
 *		with no error messages.
 *
 *--------------------------------------------------------------------*/


#if XIDTEST

/* From Figure 4.6. Typical XID frame, from AX.25 protocol spec, v. 2.2 */
/* This is the info part after a control byte of 0xAF. */

static unsigned char example[27] = {

	/* FI */	0x82,	/* Format indicator */
	/* GI */	0x80,	/* Group Identifier - parameter negotiation */
	/* GL */	0x00,	/* Group length - all of the PI/PL/PV fields */
	/* GL */	0x17,	/* (2 bytes) */
	/* PI */	0x02,	/* Parameter Indicator - classes of procedures */
	/* PL */	0x02,	/* Parameter Length */
#if 0 // Erratum: Example in the protocol spec looks wrong.
	/* PV */	0x00,	/* Parameter Variable - Half Duplex, Async, Balanced Mode */
	/* PV */	0x20,	/*  */
#else  // I think it should be like this instead.
	/* PV */	0x21,	/* Parameter Variable - Half Duplex, Async, Balanced Mode */
	/* PV */	0x00,	/* Reserved */
#endif
	/* PI */	0x03,	/* Parameter Indicator - optional functions */
	/* PL */	0x03,	/* Parameter Length */
	/* PV */	0x86,	/* Parameter Variable - SREJ/REJ, extended addr */ 
	/* PV */	0xA8,	/* 16-bit FCS, TEST cmd/resp, Modulo 128 */
	/* PV */	0x02,	/* synchronous transmit */
	/* PI */	0x06,	/* Parameter Indicator - Rx I field length (bits) */
	/* PL */	0x02,	/* Parameter Length */

// Erratum: The text does not say anything about the byte order for multibyte
// numeric values.  In the example, we have two cases where 16 bit numbers are
// sent with the more significant byte first.

	/* PV */	0x04,	/* Parameter Variable - 1024 bits (128 octets) */
	/* PV */	0x00,	/* */
	/* PI */	0x08,	/* Parameter Indicator - Rx window size */
	/* PL */	0x01,	/* Parameter length */
	/* PV */	0x02,	/* Parameter Variable - 2 frames */
	/* PI */	0x09,	/* Parameter Indicator - Timer T1 */
	/* PL */	0x02,	/* Parameter Length */
	/* PV */	0x10,	/* Parameter Variable - 4096 MSec */
	/* PV */	0x00,	/* */
	/* PI */	0x0A,	/* Parameter Indicator - Retries (N1) */
	/* PL */	0x01,	/* Parameter Length */
	/* PV */	0x03 	/* Parameter Variable - 3 retries */
};

int main (int argc, char *argv[]) {

	struct xid_param_s param;
	struct xid_param_s param2;
	int n;
	unsigned char info[40];	// Currently max of 27 but things can change.
	char desc[150];		// I've seen 109.


/* parse example. */

	n = xid_parse (example, sizeof(example), &param, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

#ifdef NDEBUG
#error	"This won't work properly if NDEBUG is defined.  It should be undefined in direwolf.h"
#endif
	assert (n==1);
	assert (param.full_duplex == 0);
	assert (param.srej == srej_single);
	assert (param.modulo == modulo_128);
	assert (param.i_field_length_rx == 128);	
	assert (param.window_size_rx == 2);
	assert (param.ack_timer == 4096);	
	assert (param.retries == 3);	

/* encode and verify it comes out the same. */

	n = xid_encode (&param, info, cr_cmd);

	assert (n == sizeof(example));
	n = memcmp(info, example, 27);

	//for (n=0; n<27; n++) {
	//  dw_printf ("%2d  %02x  %02x\n", n, example[n], info[n]);
	//}

	assert (n == 0);
	
/* try a couple different values, no srej. */

	param.full_duplex = 1;
	param.srej = srej_none;
	param.modulo = modulo_8;
	param.i_field_length_rx = 2048;
	param.window_size_rx = 3;
	param.ack_timer = 1234;
	param.retries = 12;

	n = xid_encode (&param, info, cr_cmd);
	n = xid_parse (info, n, &param2, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

	assert (param2.full_duplex == 1);
	assert (param2.srej == srej_none);
	assert (param2.modulo == modulo_8);
	assert (param2.i_field_length_rx == 2048);
	assert (param2.window_size_rx == 3);
	assert (param2.ack_timer == 1234);
	assert (param2.retries == 12);

/* Other values, single srej. */

	param.full_duplex = 0;
	param.srej = srej_single;
	param.modulo = modulo_8;
	param.i_field_length_rx = 61;
	param.window_size_rx = 4;
	param.ack_timer = 5555;
	param.retries = 9;

	n = xid_encode (&param, info, cr_cmd);
	n = xid_parse (info, n, &param2, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

	assert (param2.full_duplex == 0);
	assert (param2.srej == srej_single);
	assert (param2.modulo == modulo_8);
	assert (param2.i_field_length_rx == 61);
	assert (param2.window_size_rx == 4);
	assert (param2.ack_timer == 5555);
	assert (param2.retries == 9);


/* Other values, multi srej. */

	param.full_duplex = 0;
	param.srej = srej_multi;
	param.modulo = modulo_128;
	param.i_field_length_rx = 61;
	param.window_size_rx = 4;
	param.ack_timer = 5555;
	param.retries = 9;

	n = xid_encode (&param, info, cr_cmd);
	n = xid_parse (info, n, &param2, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

	assert (param2.full_duplex == 0);
	assert (param2.srej == srej_multi);
	assert (param2.modulo == modulo_128);
	assert (param2.i_field_length_rx == 61);
	assert (param2.window_size_rx == 4);
	assert (param2.ack_timer == 5555);
	assert (param2.retries == 9);


/* Specify some and not others. */

	param.full_duplex = 0;
	param.srej = srej_single;
	param.modulo = modulo_8;
	param.i_field_length_rx = G_UNKNOWN;
	param.window_size_rx = G_UNKNOWN;
	param.ack_timer = 999;
	param.retries = G_UNKNOWN;

	n = xid_encode (&param, info, cr_cmd);
	n = xid_parse (info, n, &param2, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

	assert (param2.full_duplex == 0);
	assert (param2.srej == srej_single);
	assert (param2.modulo == modulo_8);
	assert (param2.i_field_length_rx == G_UNKNOWN);
	assert (param2.window_size_rx == G_UNKNOWN);
	assert (param2.ack_timer == 999);
	assert (param2.retries == G_UNKNOWN);

/* Default values for empty info field. */

	n = 0;
	n = xid_parse (info, n, &param2, desc, sizeof(desc));

	text_color_set (DW_COLOR_DEBUG);
	dw_printf ("%d: %s\n", __LINE__, desc);
	fflush (stdout);
	SLEEP_SEC (1);

	text_color_set (DW_COLOR_ERROR);

	assert (param2.full_duplex == G_UNKNOWN);
	assert (param2.srej == srej_not_specified);
	assert (param2.modulo == modulo_unknown);
	assert (param2.i_field_length_rx == G_UNKNOWN);
	assert (param2.window_size_rx == G_UNKNOWN);
	assert (param2.ack_timer == G_UNKNOWN);
	assert (param2.retries == G_UNKNOWN);


	text_color_set (DW_COLOR_REC);
	dw_printf ("XID test:  Success.\n");	
	
	exit (0);
	
}

#endif

/* end xid.c */

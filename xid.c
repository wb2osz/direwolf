
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014  John Langner, WB2OSZ
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
 * Purpose:   	....
 *		
 * Description:	
 *
 * References:	...
 *
 *		
 *
 *---------------------------------------------------------------*/


#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "textcolor.h"
//#include "xid.h"


struct ax25_param_s {

	int full_duplex;
	
	// Order is important because negotiation keeps the lower.  
	enum rej_e {implicit_reject, selective_reject, selective_reject_reject } rej;

	enum modulo_e {modulo_8 = 8, modulo_128 = 128} modulo;

	int i_field_length_rx;	/* In bytes.  XID has it in bits. */

	int window_size;	

	int ack_timer;		/* "T1" in mSec. */

	int retries;		/* Inconsistently refered to as "N1" or "N2" */
};



#define FI_Format_Indicator	0x82	
#define GI_Group_Identifier	0x80

#define PI_Classes_of_Procedures	2	
#define PI_HDLC_Optional_Functions	3	
#define PI_I_Field_Length_Rx		6	
#define PI_Window_Size_Rx		8	
#define PI_Ack_Timer			9	
#define PI_Retries			10	

#define PV_Classes_Procedures_Balanced_ABM	0x0100
#define PV_Classes_Procedures_Half_Duplex	0x2000
#define PV_Classes_Procedures_Full_Duplex	0x4000

#define PV_HDLC_Optional_Functions_REJ_cmd_resp		0x020000
#define PV_HDLC_Optional_Functions_SREJ_cmd_resp	0x040000
#define PV_HDLC_Optional_Functions_Extended_Address 	0x800000

#define PV_HDLC_Optional_Functions_Modulo_8		0x000400
#define PV_HDLC_Optional_Functions_Modulo_128		0x000800
#define PV_HDLC_Optional_Functions_TEST_cmd_resp	0x002000
#define PV_HDLC_Optional_Functions_16_bit_FCS		0x008000

#define PV_HDLC_Optional_Functions_Synchronous_Tx	0x000002


/*-------------------------------------------------------------------
 *
 * Name:        ...
 *
 * Purpose:    	...
 *
 * Inputs:	...
 *
 * Outputs:	...
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/


//Returns:	1 for mostly successful (with possible error messages), 0 for failure.


int xid_parse (unsigned char *info, int info_len, struct ax25_param_s *result)
{
	unsigned char *p;
	int group_len;


	result->full_duplex = 0;
	result->rej = selective_reject;
	result->modulo = modulo_8;
	result->i_field_length_rx = 256;
	result->window_size = result->modulo == modulo_128 ? 32 : 4;
	result->ack_timer = 3000;
	result->retries = 10;

	p = info;

	if (*p != FI_Format_Indicator) {
	  return 0;
	}
	p++;

	if (*p != GI_Group_Identifier) {
	  return 0;
	}
	p++;

	group_len = *p++;
	group_len = (group_len << 8) + *p++;
	
	while (p < info + 4 + group_len) {

	  int pind, plen, pval, j;

	  pind = *p++;
	  plen = *p++;		// should have sanity checking
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
	      }
	      else if (pval & PV_Classes_Procedures_Full_Duplex && ! (pval & PV_Classes_Procedures_Half_Duplex)) {
	        result->full_duplex = 1;
	      }
	      else {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected one of Half or Full Duplex be set.\n");	
	      }

	      break;

	    case PI_HDLC_Optional_Functions:	

	      if (pval & PV_HDLC_Optional_Functions_REJ_cmd_resp && pval & PV_HDLC_Optional_Functions_SREJ_cmd_resp) {
	        result->rej = selective_reject_reject;		/* Both bits set */
	      }
	      else if (pval & PV_HDLC_Optional_Functions_REJ_cmd_resp && ! (pval & PV_HDLC_Optional_Functions_SREJ_cmd_resp)) {
	        result->rej = implicit_reject;			/* Only REJ is set */
	      }
	      else if ( ! (pval & PV_HDLC_Optional_Functions_REJ_cmd_resp) && pval & PV_HDLC_Optional_Functions_SREJ_cmd_resp) {
	        result->rej = selective_reject;			/* Only SREJ is set */
	      }
	      else {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: Expected one or both of REJ, SREJ to be set.\n");	
	      }

	      if (pval & PV_HDLC_Optional_Functions_Modulo_8 && ! (pval & PV_HDLC_Optional_Functions_Modulo_128)) {
	        result->modulo = modulo_8;
	      }
	      else if (pval & PV_HDLC_Optional_Functions_Modulo_128 && ! (pval & PV_HDLC_Optional_Functions_Modulo_8)) {
	        result->modulo = modulo_128;
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

	      if (pval & 0x7) {
	        text_color_set (DW_COLOR_ERROR);
	        dw_printf ("XID error: I Field Length Rx is not a whole number of bytes.\n");	
	      }

	      break;

	    case PI_Window_Size_Rx:	

	      result->window_size = pval;

// TODO must be 1-7 for modulo 8 or 1-127 for modulo 128;

//	      if (pval & 0x7) {
//	        text_color_set (DW_COLOR_ERROR);
//	        dw_printf ("XID error: Window Size Rx is not in range of 1 thru ???\n");	
//	      }

//continue here.

	      break;

	    case PI_Ack_Timer:	
	      result->ack_timer = pval;
	      break;

	    case PI_Retries:
	      result->retries = pval;
	      break;

	    default:		
	      break;
	  }
	}

	if (p != info + info_len) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("XID error: Frame / Group Length mismatch.\n");	
	}

	return 1; 

} /* end xid_parse */


/*-------------------------------------------------------------------
 *
 * Name:        xid_encode
 *
 * Purpose:    	...
 *
 * Inputs:	param	- 
 *
 * Outputs:	info	- Information part of XID frame.
 *			  Does not include the control byte.
 *
 * Returns:	Number of bytes in the info part.
 *
 * Description:	
 *
 *--------------------------------------------------------------------*/


int xid_encode (struct ax25_param_s *param, unsigned char *info)
{
	unsigned char *p;
	int len;
	int x;

	p = info;

	*p++ = FI_Format_Indicator;
	*p++ = GI_Group_Identifier;
	*p++ = 0;
	*p++ = 0x17;

	*p++ = PI_Classes_of_Procedures;
	*p++ = 2;
	
	x = PV_Classes_Procedures_Balanced_ABM;

	if (param->full_duplex)
	  x |= PV_Classes_Procedures_Full_Duplex;
	else
	  x |= PV_Classes_Procedures_Half_Duplex;

	*p++ = (x >> 8) & 0xff;
	*p++ = x & 0xff;

	*p++ = PI_HDLC_Optional_Functions;
	*p++ = 3;

	x = PV_HDLC_Optional_Functions_Extended_Address |
		PV_HDLC_Optional_Functions_TEST_cmd_resp |
		PV_HDLC_Optional_Functions_16_bit_FCS | 
		PV_HDLC_Optional_Functions_Synchronous_Tx;

	if (param->rej == implicit_reject || param->rej == selective_reject_reject)
	  x |= PV_HDLC_Optional_Functions_REJ_cmd_resp;

	if (param->rej == selective_reject || param->rej == selective_reject_reject)	
	  x |= PV_HDLC_Optional_Functions_SREJ_cmd_resp;

	if (param->modulo == modulo_128)
	  x |= PV_HDLC_Optional_Functions_Modulo_128;
	else
	  x |= PV_HDLC_Optional_Functions_Modulo_8;

	*p++ = (x >> 16) & 0xff;
	*p++ = (x >> 8) & 0xff;
	*p++ = x & 0xff;

	*p++ = PI_I_Field_Length_Rx;
	*p++ = 2;
	x = param->i_field_length_rx * 8;
	*p++ = (x >> 8) & 0xff;
	*p++ = x & 0xff;

	*p++ = PI_Window_Size_Rx;
	*p++ = 1;
	*p++ = param->window_size;

	*p++ = PI_Ack_Timer;
	*p++ = 2;
	*p++ = param->ack_timer >> 8;
	*p++ = param->ack_timer & 0xff;

	*p++ = PI_Retries;
	*p++ = 1;
	*p++ = param->retries;

	len = p - info;
	assert (len == 27);
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
 *			gcc -DTEST -g xid.c textcolor.c ; ./a
 *
 *		Result should be:
 *
 *			XID test:  Success.
 *
 *		with no error messages.
 *
 *--------------------------------------------------------------------*/


#if TEST

/* From Figure 4.6. Typical XID frame, from AX.25 protocol spec, v. 2.2 */
/* This is the info part after a control byte of 0xAF. */

static unsigned char example[27] = {

	/* FI */	0x82,	/* Format indicator */
	/* GI */	0x80,	/* Group Identifier - parameter negotiation */
	/* GL */	0x00,	/* Group length - all of the PI/PL/PV fields */
	/* GL */	0x17,	/* (2 bytes) */
	/* PI */	0x02,	/* Parameter Indicator - classes of procedures */
	/* PL */	0x02,	/* Parameter Length */
#if 0 // Example in the protocol spec looks wrong.
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
	/* PV */	0x03 	/* Parameter Variable - 3 ret */
};

int main (int argc, char *argv[]) {

	struct ax25_param_s param;
	struct ax25_param_s param2;
	int n;
	unsigned char info[40];

/* parse example. */

	n = xid_parse (example, sizeof(example), &param);

	text_color_set (DW_COLOR_ERROR);

	assert (n==1);
	assert (param.full_duplex == 0);
	assert (param.rej == selective_reject_reject);
	assert (param.modulo == modulo_128);
	assert (param.i_field_length_rx == 128);	
	assert (param.window_size == 2);	
	assert (param.ack_timer == 4096);	
	assert (param.retries == 3);	

/* encode and verify it comes out the same. */

	n = xid_encode (&param, info);

	assert (n == sizeof(example));
	n = memcmp(info, example, 27);

	//for (n=0; n<27; n++) {
	//  dw_printf ("%2d  %02x  %02x\n", n, example[n], info[n]);
	//}

	assert (n == 0);
	
/* try a couple different values. */

	param.full_duplex = 1;
	param.rej = implicit_reject;
	param.modulo = modulo_8;
	param.i_field_length_rx = 2048;
	param.window_size = 3;
	param.ack_timer = 3000;
	param.retries = 10;

	n = xid_encode (&param, info);
	n = xid_parse (info, n, &param2);

	assert (param2.full_duplex == 1);
	assert (param2.rej == implicit_reject);
	assert (param2.modulo == modulo_8);
	assert (param2.i_field_length_rx == 2048);
	assert (param2.window_size == 3);
	assert (param2.ack_timer == 3000);
	assert (param2.retries == 10);

/* Finally the third possbility for rej. */

	param.full_duplex = 0;
	param.rej = selective_reject;
	param.modulo = modulo_8;
	param.i_field_length_rx = 256;
	param.window_size = 4;
	param.ack_timer = 3000;
	param.retries = 10;

	n = xid_encode (&param, info);
	n = xid_parse (info, n, &param2);

	assert (param2.full_duplex == 0);
	assert (param2.rej == selective_reject);
	assert (param2.modulo == modulo_8);
	assert (param2.i_field_length_rx == 256);
	assert (param2.window_size == 4);
	assert (param2.ack_timer == 3000);
	assert (param2.retries == 10);

	text_color_set (DW_COLOR_INFO);
	dw_printf ("XID test:  Success.\n");	
	
	exit (0);
	
}

#endif

/* end xid.c */

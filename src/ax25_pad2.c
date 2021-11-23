//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016  John Langner, WB2OSZ
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
 * Name:	ax25_pad2.c
 *
 * Purpose:	Packet assembler and disasembler, part 2.
 *
 * Description:	
 *
 *	The original ax25_pad.c was written with APRS in mind.
 *	It handles UI frames and transparency for a KISS TNC.
 *	Here we add new functions that can handle the
 *	more general cases of AX.25 frames.
 *
 *
 *	* Destination Address  (note: opposite order in printed format)
 *
 *	* Source Address
 *
 *	* 0-8 Digipeater Addresses
 *				(The AX.25 v2.2 spec reduced this number to
 *				a maximum of 2 but I allow the original 8.)
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
 *			C = command/response.   Set to 1 for command.
 *			R R = Reserved = 1 1	(See RR note, below)
 *			SSID = substation ID
 *			0 = zero
 *
 *	The final octet of the Source has the form:
 *
 *		C R R SSID 0, where,
 *
 *			C = command/response.   Must be inverse of destination C bit.
 *			R R = Reserved = 1 1	(See RR note, below)
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
 *
 *	
 *	Next we have:
 *
 *	* One or two byte Control Field - A U frame always has one control byte.
 *					When using modulo 128 sequence numbers, the
 *					I and S frames can have a second byte allowing
 *					7 bit fields instead of 3 bit fields.
 *					Unfortunately, we can't tell which we have by looking
 *					at a frame out of context.  :-(
 *					If we are one end of the link, we would know this
 *					from SABM/SABME and possible later negotiation
 *					with XID.  But if we start monitoring two other
 *					stations that are already conversing, we don't know.
 *
 *			RR note:	It seems that some implementations put a hint
 *					in the "RR" reserved bits.
 *					http://www.tapr.org/pipermail/ax25-layer2/2005-October/000297.html (now broken)
 *					https://elixir.bootlin.com/linux/latest/source/net/ax25/ax25_addr.c#L237
 *
 *					The RR bits can also be used for "DAMA" which is
 *					some sort of channel access coordination scheme.
 *					http://internet.freepage.de/cgi-bin/feets/freepage_ext/41030x030A/rewrite/hennig/afu/afudoc/afudama.html
 *					Neither is part of the official protocol spec.
 *
 *	* One byte Protocol ID 		- Only for I and UI frames.
 *					Normally we would use 0xf0 for no layer 3.
 *
 *	Finally the Information Field. The initial max size is 256 but it 
 *	can be negotiated higher if both ends agree.
 *
 *	Only these types of frames can have an information part:
 *		- I
 *		- UI
 *		- XID
 *		- TEST
 *		- FRMR
 *
 *	The 2 byte CRC is not stored here. 
 *
 *
 * Constructors:
 *		ax25_u_frame		- Construct a U frame.
 *		ax25_s_frame		- Construct a S frame.
 *		ax25_i_frame		- Construct a I frame.
 *
 * Get methods:	....			???
 *
 *------------------------------------------------------------------*/

#define AX25_PAD_C		/* this will affect behavior of ax25_pad.h */


#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>


#include "textcolor.h"
#include "ax25_pad.h"
#include "ax25_pad2.h"



extern int ax25memdebug;

static int set_addrs (packet_t pp, char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr);

//#if AX25MEMDEBUG
//#undef AX25MEMDEBUG
//#endif

		
/*------------------------------------------------------------------------------
 *
 * Name:	ax25_u_frame
 * 
 * Purpose:	Construct a U frame.
 *
 * Input:	addrs		- Array of addresses.
 *	
 *		num_addr	- Number of addresses, range 2 .. 10.
 *
 *		cr		- cr_cmd command frame, cr_res for a response frame.
 *
 *		ftype		- One of:
 *				        frame_type_U_SABME     // Set Async Balanced Mode, Extended
 *				        frame_type_U_SABM      // Set Async Balanced Mode
 *				        frame_type_U_DISC      // Disconnect
 *				        frame_type_U_DM        // Disconnect Mode
 *				        frame_type_U_UA        // Unnumbered Acknowledge
 *				        frame_type_U_FRMR      // Frame Reject
 *				        frame_type_U_UI        // Unnumbered Information
 *				        frame_type_U_XID       // Exchange Identification
 *				        frame_type_U_TEST      // Test
 *
 *		pf		- Poll/Final flag.
 *
 *		pid		- Protocol ID.  >>> Used ONLY for the UI type. <<<
 *				  Normally 0xf0 meaning no level 3.
 *				  Could be other values for NET/ROM, etc.
 *
 *		pinfo		- Pointer to data for Info field.  Allowed only for UI, XID, TEST, FRMR.
 *		
 *		info_len	- Length for Info field.
 *
 *
 * Returns:	Pointer to new packet object.
 *
 *------------------------------------------------------------------------------*/

#if AX25MEMDEBUG
packet_t ax25_u_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len, char *src_file, int src_line)
#else
packet_t ax25_u_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int pf, int pid, unsigned char *pinfo, int info_len)
#endif
{
	packet_t this_p;
	unsigned char *p;
	int ctrl = 0;
	unsigned int t = 999;	// 1 = must be cmd, 0 = must be response, 2 = can be either.
	int i = 0;		// Is Info part allowed?

	this_p = ax25_new ();

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_u_frame, seq=%d, called from %s %d\n", this_p->seq, src_file, src_line);
	}
#endif
	
	if (this_p == NULL) return (NULL);

	this_p->modulo = 0;

	if ( ! set_addrs (this_p, addrs, num_addr, cr)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Could not set addresses for U frame.\n", __func__);
	  ax25_delete (this_p);
	  return (NULL);
	}  

	switch (ftype) {
							// 1 = cmd only, 0 = res only, 2 = either
	  case frame_type_U_SABME:	ctrl = 0x6f;	t = 1;		break;
	  case frame_type_U_SABM:	ctrl = 0x2f;	t = 1;		break;
	  case frame_type_U_DISC:	ctrl = 0x43;	t = 1;		break;
	  case frame_type_U_DM:		ctrl = 0x0f;	t = 0;		break;
	  case frame_type_U_UA:		ctrl = 0x63;	t = 0;		break;
	  case frame_type_U_FRMR:	ctrl = 0x87;	t = 0;	i = 1;	break;
	  case frame_type_U_UI:		ctrl = 0x03;	t = 2;	i = 1;	break;
	  case frame_type_U_XID:	ctrl = 0xaf;	t = 2;	i = 1;	break;
	  case frame_type_U_TEST:	ctrl = 0xe3;	t = 2;	i = 1;	break;

	  default:
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: Invalid ftype %d for U frame.\n", __func__, ftype);
	    ax25_delete (this_p);
	    return (NULL);
	    break;
	}
	if (pf) ctrl |= 0x10;

	if (t != 2) {
	  if (cr != t) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: U frame, cr is %d but must be %d. ftype=%d\n", __func__, cr, t, ftype);
	  }
	}

	p = this_p->frame_data + this_p->frame_len;
	*p++ = ctrl;	
	this_p->frame_len++;

	if (ftype == frame_type_U_UI) {

	  // Definitely don't want pid value of 0 (not in valid list)
	  // or 0xff (which means more bytes follow).

	  if (pid < 0 || pid == 0 || pid == 0xff) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: U frame, Invalid pid value 0x%02x.\n", __func__, pid);
	    pid = AX25_PID_NO_LAYER_3;
	  }
	  *p++ = pid;
	  this_p->frame_len++;
	}

	if (i) {
	  if (pinfo != NULL && info_len > 0) {
	    if (info_len > AX25_MAX_INFO_LEN) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Internal error in %s: U frame, Invalid information field length %d.\n", __func__, info_len);
	      info_len = AX25_MAX_INFO_LEN;
	    }
	    memcpy (p, pinfo, info_len);
	    p += info_len;
	    this_p->frame_len += info_len;
	  }
	}
	else {
	  if (pinfo != NULL && info_len > 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: Info part not allowed for U frame type.\n", __func__);
	  }
	}
	*p = '\0';

	assert (p == this_p->frame_data + this_p->frame_len);
        assert (this_p->magic1 == MAGIC);
        assert (this_p->magic2 == MAGIC);

#if PAD2TEST
	ax25_frame_type_t check_ftype;
	cmdres_t check_cr;
	char check_desc[80];
	int check_pf;
	int check_nr;
	int check_ns;

	check_ftype = ax25_frame_type (this_p, &check_cr, check_desc, &check_pf, &check_nr, &check_ns);

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("check: ftype=%d, desc=\"%s\", pf=%d\n", check_ftype, check_desc, check_pf);

	assert (check_cr == cr);
	assert (check_ftype == ftype);
	assert (check_pf == pf);
	assert (check_nr == -1);
	assert (check_ns == -1);

#endif

	return (this_p);

} /* end ax25_u_frame */






		
/*------------------------------------------------------------------------------
 *
 * Name:	ax25_s_frame
 * 
 * Purpose:	Construct an S frame.
 *
 * Input:	addrs		- Array of addresses.
 *	
 *		num_addr	- Number of addresses, range 2 .. 10.
 *
 *		cr		- cr_cmd command frame, cr_res for a response frame.
 *
 *		ftype		- One of:
 *				        frame_type_S_RR,        // Receive Ready - System Ready To Receive
 *				        frame_type_S_RNR,       // Receive Not Ready - TNC Buffer Full
 *				        frame_type_S_REJ,       // Reject Frame - Out of Sequence or Duplicate
 *				        frame_type_S_SREJ,      // Selective Reject - Request single frame repeat
 *
 *		modulo		- 8 or 128.  Determines if we have 1 or 2 control bytes.
 *
 *		nr		- N(R) field --- describe.
 *
 *		pf		- Poll/Final flag.
 *
 *		pinfo		- Pointer to data for Info field.  Allowed only for SREJ.
 *
 *		info_len	- Length for Info field.
 *
 *
 * Returns:	Pointer to new packet object.
 *
 *------------------------------------------------------------------------------*/

#if AX25MEMDEBUG
packet_t ax25_s_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int modulo, int nr, int pf, unsigned char *pinfo, int info_len, char *src_file, int src_line)
#else
packet_t ax25_s_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, ax25_frame_type_t ftype, int modulo, int nr, int pf, unsigned char *pinfo, int info_len)
#endif
{
	packet_t this_p;
	unsigned char *p;
	int ctrl = 0;

	this_p = ax25_new ();

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_s_frame, seq=%d, called from %s %d\n", this_p->seq, src_file, src_line);
	}
#endif
	
	if (this_p == NULL) return (NULL);

	if ( ! set_addrs (this_p, addrs, num_addr, cr)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Could not set addresses for S frame.\n", __func__);
	  ax25_delete (this_p);
	  return (NULL);
	}  

	if (modulo != 8 && modulo != 128) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Invalid modulo %d for S frame.\n", __func__, modulo);
	  modulo = 8;
	}
	this_p->modulo = modulo;

	if (nr < 0 || nr >= modulo) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Invalid N(R) %d for S frame.\n", __func__, nr);
	  nr &= (modulo - 1);
	}

	// Erratum: The AX.25 spec is not clear about whether SREJ should be command, response, or both.
	// The underlying X.25 spec clearly says it is response only.  Let's go with that.

	if (ftype == frame_type_S_SREJ && cr != cr_res) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: SREJ must be response.\n", __func__);
	}

	switch (ftype) {

	  case frame_type_S_RR:		ctrl = 0x01;	break;
	  case frame_type_S_RNR:	ctrl = 0x05;	break;
	  case frame_type_S_REJ:	ctrl = 0x09;	break;
	  case frame_type_S_SREJ:	ctrl = 0x0d;	break;

	  default:
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: Invalid ftype %d for S frame.\n", __func__, ftype);
	    ax25_delete (this_p);
	    return (NULL);
	    break;
	}

	p = this_p->frame_data + this_p->frame_len;

	if (modulo == 8) {
	  if (pf) ctrl |= 0x10;
	  ctrl |= nr << 5;
	  *p++ = ctrl;	
	  this_p->frame_len++;
	}
	else {
	  *p++ = ctrl;	
	  this_p->frame_len++;
	  
	  ctrl = pf & 1;
	  ctrl |= nr << 1;
	  *p++ = ctrl;	
	  this_p->frame_len++;
	}

	if (ftype == frame_type_S_SREJ) {
	  if (pinfo != NULL && info_len > 0) {
	    if (info_len > AX25_MAX_INFO_LEN) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Internal error in %s: SREJ frame, Invalid information field length %d.\n", __func__, info_len);
	      info_len = AX25_MAX_INFO_LEN;
	    }
	    memcpy (p, pinfo, info_len);
	    p += info_len;
	    this_p->frame_len += info_len;
	  }
	}
	else {
	  if (pinfo != NULL || info_len != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: Info part not allowed for RR, RNR, REJ frame.\n", __func__);
	  }
	}
	*p = '\0';

	assert (p == this_p->frame_data + this_p->frame_len);
        assert (this_p->magic1 == MAGIC);
        assert (this_p->magic2 == MAGIC);

#if PAD2TEST

	ax25_frame_type_t check_ftype;
	cmdres_t check_cr;
	char check_desc[80];
	int check_pf;
	int check_nr;
	int check_ns;
	
	// todo modulo must be input.
	check_ftype = ax25_frame_type (this_p, &check_cr, check_desc, &check_pf, &check_nr, &check_ns);

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("check: ftype=%d, desc=\"%s\", pf=%d, nr=%d\n", check_ftype, check_desc, check_pf, check_nr);

	assert (check_cr == cr);
	assert (check_ftype == ftype);
	assert (check_pf == pf);
	assert (check_nr == nr);
	assert (check_ns == -1);

#endif
	return (this_p);

} /* end ax25_s_frame */




		
/*------------------------------------------------------------------------------
 *
 * Name:	ax25_i_frame
 * 
 * Purpose:	Construct an I frame.
 *
 * Input:	addrs		- Array of addresses.
 *	
 *		num_addr	- Number of addresses, range 2 .. 10.
 *
 *		cr		- cr_cmd command frame, cr_res for a response frame.
 *
 *		modulo		- 8 or 128.
 *
 *		nr		- N(R) field --- describe.
 *
 *		ns		- N(S) field --- describe.
 *
 *		pf		- Poll/Final flag.
 *
 *		pid		- Protocol ID.  
 *				  Normally 0xf0 meaning no level 3.
 *				  Could be other values for NET/ROM, etc.
 *
 *		pinfo		- Pointer to data for Info field.  
 *		
 *		info_len	- Length for Info field.
 *
 *
 * Returns:	Pointer to new packet object.
 *
 *------------------------------------------------------------------------------*/

#if AX25MEMDEBUG
packet_t ax25_i_frame_debug (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, int modulo, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len, char *src_file, int src_line)
#else
packet_t ax25_i_frame (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr, int modulo, int nr, int ns, int pf, int pid, unsigned char *pinfo, int info_len)
#endif
{
	packet_t this_p;
	unsigned char *p;
	int ctrl = 0;

	this_p = ax25_new ();

#if AX25MEMDEBUG	
	if (ax25memdebug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ax25_i_frame, seq=%d, called from %s %d\n", this_p->seq, src_file, src_line);
	}
#endif
	
	if (this_p == NULL) return (NULL);

	if ( ! set_addrs (this_p, addrs, num_addr, cr)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Could not set addresses for I frame.\n", __func__);
	  ax25_delete (this_p);
	  return (NULL);
	}  

	if (modulo != 8 && modulo != 128) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Invalid modulo %d for I frame.\n", __func__, modulo);
	  modulo = 8;
	}
	this_p->modulo = modulo;

	if (nr < 0 || nr >= modulo) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Invalid N(R) %d for I frame.\n", __func__, nr);
	  nr &= (modulo - 1);
	}

	if (ns < 0 || ns >= modulo) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error in %s: Invalid N(S) %d for I frame.\n", __func__, ns);
	  ns &= (modulo - 1);
	}

	p = this_p->frame_data + this_p->frame_len;

	if (modulo == 8) {
	  ctrl = (nr << 5) | (ns << 1);
	  if (pf) ctrl |= 0x10;
	  *p++ = ctrl;	
	  this_p->frame_len++;
	}
	else {
	  ctrl = ns << 1;
	  *p++ = ctrl;	
	  this_p->frame_len++;

	  ctrl = nr << 1;
	  if (pf) ctrl |= 0x01;
	  *p++ = ctrl;	
	  this_p->frame_len++;
	}

	// Definitely don't want pid value of 0 (not in valid list)
	// or 0xff (which means more bytes follow).

	if (pid < 0 || pid == 0 || pid == 0xff) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Warning: Client application provided invalid PID value, 0x%02x, for I frame.\n", pid);
	  pid = AX25_PID_NO_LAYER_3;
	}
	*p++ = pid;
	this_p->frame_len++;

	if (pinfo != NULL && info_len > 0) {
	  if (info_len > AX25_MAX_INFO_LEN) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error in %s: I frame, Invalid information field length %d.\n", __func__, info_len);
	    info_len = AX25_MAX_INFO_LEN;
	  }
	  memcpy (p, pinfo, info_len);
	  p += info_len;
	  this_p->frame_len += info_len;
	}

	*p = '\0';

	assert (p == this_p->frame_data + this_p->frame_len);
        assert (this_p->magic1 == MAGIC);
        assert (this_p->magic2 == MAGIC);

#if PAD2TEST

	ax25_frame_type_t check_ftype;
	cmdres_t check_cr;
	char check_desc[80];
	int check_pf;
	int check_nr;
	int check_ns;
	unsigned char *check_pinfo;
	int check_info_len;

	check_ftype = ax25_frame_type (this_p, &check_cr, check_desc, &check_pf, &check_nr, &check_ns);

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("check: ftype=%d, desc=\"%s\", pf=%d, nr=%d, ns=%d\n", check_ftype, check_desc, check_pf, check_nr, check_ns);

	check_info_len = ax25_get_info (this_p, &check_pinfo);

	assert (check_cr == cr);
	assert (check_ftype == frame_type_I);
	assert (check_pf == pf);
	assert (check_nr == nr);
	assert (check_ns == ns);

	assert (check_info_len == info_len);
	assert (strcmp((char*)check_pinfo,(char*)pinfo) == 0);
#endif

	return (this_p);

} /* end ax25_i_frame */





/*------------------------------------------------------------------------------
 *
 * Name:	set_addrs
 * 
 * Purpose:	Set address fields
 *
 * Input:	pp		- Packet object.
 *
 *		addrs		- Array of addresses.  Same order as in frame.
 *	
 *		num_addr	- Number of addresses, range 2 .. 10.
 *
 *		cr		- cr_cmd command frame, cr_res for a response frame.
 *
 * Output:	pp->frame_data 	- 7 bytes for each address.
 *
 *		pp->frame_len	- num_addr * 7
 *
 *		p->num_addr	- num_addr
 *
 * Returns:	1 for success.  0 for failure.
 *
 *------------------------------------------------------------------------------*/


static int set_addrs (packet_t pp, char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, cmdres_t cr)
{
	int n;

	assert (pp->frame_len == 0);
	assert (cr == cr_cmd || cr == cr_res);

	if (num_addr < AX25_MIN_ADDRS || num_addr > AX25_MAX_ADDRS) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR: %s %s %d, num_addr = %d\n", __FILE__, __func__, __LINE__, num_addr);
	  return (0);
	}

	for (n = 0; n < num_addr; n++) {

	  unsigned char *pa = pp->frame_data + n * 7;
	  int ok;
	  int strict = 1;
	  char oaddr[AX25_MAX_ADDR_LEN];
	  int ssid;
	  int heard;
	  int j;

	  ok = ax25_parse_addr (n, addrs[n], strict, oaddr, &ssid, &heard);

	  if (! ok) return (0);

	  // Fill in address.

	  memset (pa, ' ' << 1, 6);
	  for (j = 0; oaddr[j]; j++) {
	    pa[j] = oaddr[j] << 1;
	  }
	  pa += 6;

	  // Fill in SSID.

	  *pa = 0x60 | ((ssid & 0xf) << 1);

	  // Command / response flag.

	  switch (n) {
	   case AX25_DESTINATION:
	      if (cr == cr_cmd) *pa |= 0x80;
	      break;
	   case AX25_SOURCE:
	      if (cr == cr_res) *pa |= 0x80;
	      break;
	   default:
	    break;
	  }

	  // Is this the end of address field?

	  if (n == num_addr - 1) {
	    *pa |= 1;
	  }

	  pp->frame_len += 7;
	}

	pp->num_addr = num_addr;
	return (1);

} /* end set_addrs */




/*------------------------------------------------------------------------------
 *
 * Name:	main
 * 
 * Purpose:	Quick unit test for this file.
 *
 * Description:	Generate a variety of frames.
 *		Each function calls ax25_frame_type to verify results.
 *
 *		$ gcc -DPAD2TEST -DUSE_REGEX_STATIC -Iregex ax25_pad.c ax25_pad2.c fcs_calc.o textcolor.o regex.a misc.a
 *
 *------------------------------------------------------------------------------*/

#if PAD2TEST

int main ()
{
	char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN];
	int num_addr = 2;
	cmdres_t cr;
	ax25_frame_type_t ftype;
	int pf = 0;
	int pid = 0xf0;
	int modulo;
	int nr, ns;
	unsigned char *pinfo = NULL;
	int info_len = 0;
	packet_t pp;

	strcpy (addrs[0], "W2UB");
	strcpy (addrs[1], "WB2OSZ-15");
	num_addr = 2;

/* U frame */

	for (ftype = frame_type_U_SABME; ftype <= frame_type_U_TEST; ftype++) {

	  for (pf = 0; pf <= 1; pf++) {

 	    int cmin = 0, cmax = 1;

	    switch (ftype) {
					// 0 = response, 1 = command
	      case frame_type_U_SABME:	cmin = 1; cmax = 1; break;
	      case frame_type_U_SABM:	cmin = 1; cmax = 1; break;
	      case frame_type_U_DISC:	cmin = 1; cmax = 1; break;
	      case frame_type_U_DM:	cmin = 0; cmax = 0; break;
	      case frame_type_U_UA:	cmin = 0; cmax = 0; break;
	      case frame_type_U_FRMR:	cmin = 0; cmax = 0; break;
	      case frame_type_U_UI:	cmin = 0; cmax = 1; break;
	      case frame_type_U_XID:	cmin = 0; cmax = 1; break;
	      case frame_type_U_TEST:	cmin = 0; cmax = 1; break;
	      default:			break;	// avoid compiler warning.		
	    }
	  
	    for (cr = cmin; cr <= cmax; cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct U frame, cr=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_u_frame (addrs, num_addr, cr, ftype, pf, pid, pinfo, info_len);
	      ax25_hex_dump (pp);
	      ax25_delete (pp);
	    }
	  }
	}

	dw_printf ("\n----------\n\n");

/* S frame */

	strcpy (addrs[2], "DIGI1-1");
	num_addr = 3;

	for (ftype = frame_type_S_RR; ftype <= frame_type_S_SREJ; ftype++) {

	  for (pf = 0; pf <= 1; pf++) {

	    modulo = 8;
	    nr = modulo / 2 + 1;

 	    for (cr = 0; cr <= 1; cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, NULL, 0);

	      ax25_hex_dump (pp);
	      ax25_delete (pp);
	    }

	    modulo = 128;
	    nr = modulo / 2 + 1;

 	    for (cr = 0; cr <= 1; cr++) {

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("\nConstruct S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	      pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, NULL, 0);

	      ax25_hex_dump (pp);
	      ax25_delete (pp);
	    }
	  }
	}

/* SREJ is only S frame which can have information part. */

	static unsigned char srej_info[] = { 1<<1, 2<<1, 3<<1, 4<<1 };

	ftype = frame_type_S_SREJ;
	for (pf = 0; pf <= 1; pf++) {

	  modulo = 128;
	  nr = 127;
	  cr = cr_res;

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("\nConstruct Multi-SREJ S frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	  pp = ax25_s_frame (addrs, num_addr, cr, ftype, modulo, nr, pf, srej_info, (int)(sizeof(srej_info)));

	  ax25_hex_dump (pp);
	  ax25_delete (pp);
	}

	dw_printf ("\n----------\n\n");

/* I frame */

	pinfo = (unsigned char*)"The rain in Spain stays mainly on the plain.";
	info_len = strlen((char*)pinfo);

	for (pf = 0; pf <= 1; pf++) {

	  modulo = 8;
	  nr = 0x55 & (modulo - 1);
	  ns = 0xaa & (modulo - 1);

 	  for (cr = 0; cr <= 1; cr++) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("\nConstruct I frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	    pp = ax25_i_frame (addrs, num_addr, cr, modulo, nr, ns, pf, pid, pinfo, info_len);

	    ax25_hex_dump (pp);
	    ax25_delete (pp);
	  }

	  modulo = 128;
	  nr = 0x55 & (modulo - 1);
	  ns = 0xaa & (modulo - 1);

 	  for (cr = 0; cr <= 1; cr++) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("\nConstruct I frame, cmd=%d, ftype=%d, pid=0x%02x\n", cr, ftype, pid);

	    pp = ax25_i_frame (addrs, num_addr, cr, modulo, nr, ns, pf, pid, pinfo, info_len);

	    ax25_hex_dump (pp);
	    ax25_delete (pp);
	  }
	}

	text_color_set(DW_COLOR_REC);
	dw_printf ("\n----------\n\n");
	dw_printf ("\nSUCCESS!\n");

	exit (EXIT_SUCCESS);

} /* end main */

#endif


/* end ax25_pad2.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2019  John Langner, WB2OSZ
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

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "fx25.h"
#include "fcs_calc.h"
#include "textcolor.h"
#include "audio.h"
#include "gen_tone.h"


//#define FXTEST 1		// To build unit test application.


#ifndef FXTEST
static void send_bytes (int chan, unsigned char *b, int count);
static void send_bit (int chan, int b);
#endif
static int stuff_it (unsigned char *in, int ilen, unsigned char *out, int osize);


static int number_of_bits_sent[MAX_RADIO_CHANS];		// Count number of bits sent by "fx25_send_frame" or "???"


#if FXTEST
static unsigned char preload[] = {
	'T'<<1, 'E'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, 0x60,
	'W'<<1, 'B'<<1, '2'<<1, 'O'<<1, 'S'<<1, 'Z'<<1, 0x63,
	0x03, 0xf0,
	'F', 'o', 'o', '?' , 'B', 'a', 'r', '?' ,   //  '?' causes bit stuffing
	0, 0, 0		// Room for FCS + extra
}; 

int main ()
{
	text_color_set(DW_COLOR_ERROR);
	dw_printf("fxsend - FX.25 unit test.\n");
	dw_printf("This generates 11 files named fx01.dat, fx02.dat, ..., fx0b.dat\n");
	dw_printf("Run fxrec as second part of test.\n");

	fx25_init (3);
	for (int i = 100 + CTAG_MIN; i <= 100 + CTAG_MAX; i++) {
	  fx25_send_frame (0, preload, (int)sizeof(preload)-3, i);
	}
	exit(EXIT_SUCCESS);
} // end main
#endif


/*-------------------------------------------------------------
 *
 * Name:	fx25_send_frame
 *
 * Purpose:	Convert HDLC frames to a stream of bits.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		fbuf	- Frame buffer address.
 *
 *		flen	- Frame length, before bit-stuffing, not including the FCS.
 *
 *		fx_mode	- Normally, this would be 16, 32, or 64 for the desired number
 *			  of check bytes.  The shortest format, adequate for the 
 *			  required data length will be picked automatically.
 *			  0x01 thru 0x0b may also be specified for a specific format
 *			  but this is expected to be mostly for testing, not normal
 *			  operation.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent including "flags" and the
 *		stuffing bits.  
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *		-1 is returned for failure.
 *
 * Description:	Generate an AX.25 frame in the usual way then wrap
 *		it inside of the FX.25 correlation tag and check bytes.
 *
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 * Errors:	If something goes wrong, return -1 and the caller should
 *		fallback to sending normal AX.25.
 *
 *		This could happen if the frame is too large.
 *
 *--------------------------------------------------------------*/

int fx25_send_frame (int chan, unsigned char *fbuf, int flen, int fx_mode)
{
	if (fx25_get_debug() >= 3) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("------\n");
	  dw_printf ("FX.25[%d] send frame: FX.25 mode = %d\n", chan, fx_mode);
	  fx_hex_dump (fbuf, flen);
	}

	number_of_bits_sent[chan] = 0;

	// Append the FCS.

	int fcs = fcs_calc (fbuf, flen);
	fbuf[flen++] = fcs & 0xff;
	fbuf[flen++] = (fcs >> 8) & 0xff;

	// Add bit-stuffing.

	unsigned char data[FX25_MAX_DATA+1];
	const unsigned char fence = 0xaa;
	data[FX25_MAX_DATA] = fence;

	int dlen = stuff_it(fbuf, flen, data, FX25_MAX_DATA);

	assert (data[FX25_MAX_DATA] == fence);
	if (dlen < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FX.25[%d]: Frame length of %d + overhead is too large to encode.\n", chan, flen);
	  return (-1);
	}

	// Pick suitable correlation tag depending on
	// user's preference, for number of check bytes,
	// and the data size.

	int ctag_num = fx25_pick_mode (fx_mode, dlen);

	if (ctag_num < CTAG_MIN || ctag_num > CTAG_MAX) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FX.25[%d]: Could not find suitable format for requested %d and data length %d.\n", chan, fx_mode, dlen);
	  return (-1);
	}

	uint64_t ctag_value = fx25_get_ctag_value (ctag_num);

	// Zero out part of data which won't be transmitted.
	// It should all be filled by extra HDLC "flag" patterns.

	int k_data_radio = fx25_get_k_data_radio (ctag_num);
	int k_data_rs = fx25_get_k_data_rs (ctag_num);
	int shorten_by = FX25_MAX_DATA - k_data_radio;
	if (shorten_by > 0) {
	  memset (data + k_data_radio, 0, shorten_by);
	}

	// Compute the check bytes.

	unsigned char check[FX25_MAX_CHECK+1];
	check[FX25_MAX_CHECK] = fence;
	struct rs *rs = fx25_get_rs (ctag_num);

	assert (k_data_rs + NROOTS == NN);

	ENCODE_RS(rs, data, check);
	assert (check[FX25_MAX_CHECK] == fence);

	if (fx25_get_debug() >= 3) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("FX.25[%d]: transmit %d data bytes, ctag number 0x%02x\n", chan, k_data_radio, ctag_num);
	  fx_hex_dump (data, k_data_radio);
	  dw_printf ("FX.25[%d]: transmit %d check bytes:\n", chan, NROOTS);
	  fx_hex_dump (check, NROOTS);
	  dw_printf ("------\n");
	}

#if FXTEST
	// Standalone text application.

	unsigned char flags[16] = { 0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e ,0x7e };
	char fname[32];
	snprintf (fname, sizeof(fname), "fx%02x.dat", ctag_num);
	FILE *fp = fopen(fname, "wb");
	fwrite (flags, sizeof(flags), 1, fp);
	//fwrite ((unsigned char *)(&ctag_value), sizeof(ctag_value), 1, fp);	// No - assumes little endian.
	for (int k = 0; k < 8; k++) {
	  unsigned char b = (ctag_value >> (k * 8)) & 0xff;	// Should be portable to big endian too.
	  fwrite (&b, 1, 1, fp);
	}
#if 1
	for (int j = 8; j < 16; j++) {	// Introduce errors.
	  data[j] = ~ data[j];
	}
#endif
	fwrite (data, k_data_radio, 1, fp);
	fwrite (check, NROOTS, 1, fp);
	fwrite (flags, sizeof(flags), 1, fp);
	fflush(fp);
	fclose (fp);
#else
	// Normal usage.  Send bits to modulator.

// Temp hack for testing.  Corrupt first 8 bytes.
//	for (int j = 0; j < 16; j++) {
//	  data[j] = ~ data[j];
//	}

	for (int k = 0; k < 8; k++) {
	  unsigned char b = (ctag_value >> (k * 8)) & 0xff;
	  send_bytes (chan, &b, 1);
	}
	send_bytes (chan, data, k_data_radio);
	send_bytes (chan, check, NROOTS);
#endif
	
	return (number_of_bits_sent[chan]);
}


#ifndef FXTEST

static void send_bytes (int chan, unsigned char *b, int count)
{
	for (int j = 0; j < count; j++) {
	  unsigned char x = b[j];
	  for (int k = 0; k < 8; k++) {
	    send_bit (chan, x & 0x01);
	    x >>= 1;
	  }
	}
}

/*
 * NRZI encoding.
 * data 1 bit -> no change.
 * data 0 bit -> invert signal.
 */
static void send_bit (int chan, int b)
{
	static int output[MAX_RADIO_CHANS];

	if (b == 0) {
	  output[chan] = ! output[chan];
	}
	tone_gen_put_bit (chan, output[chan]);
	number_of_bits_sent[chan]++;
}
#endif  // FXTEST


/*-------------------------------------------------------------
 *
 * Name:	stuff_it
 *
 * Purpose:	Perform HDLC bit-stuffing and add "flag" octets in
 *		preparation for the RS encoding.
 *
 * Inputs:	in	- Frame, including FCS, in.
 *
 *		ilen	- Number of bytes in.
 *
 *		osize	- Size of out area.
 *
 * Outputs:	out	- Location to receive result.
 *
 * Returns:	Number of bytes needed in output area including one trailing flag.
 *		-1 if it won't fit.
 *
 * Description:	Convert to stream of bits including:
 *			start flag
 *			bit stuffed data, including FCS
 *			end flag
 *		Fill remainder with flag octets which might not be on byte boundaries.
 * 
 *--------------------------------------------------------------*/

#define put_bit(value)  {						\
			if (olen >= osize) return(-1);			\
			if (value) out[olen>>3] |= 1 << (olen & 0x7);	\
			olen++;						\
			}

static int stuff_it (unsigned char *in, int ilen, unsigned char *out, int osize)
{
	const unsigned char flag = 0x7e;
	int ret = -1;
	memset (out, 0, osize);
	out[0] = flag;
	int olen = 8;			// Number of bits in output.
	osize *= 8;			// Now in bits rather than bytes.
	int ones = 0;

	for (int i = 0; i < ilen; i++) {
	  for (unsigned char imask = 1; imask != 0; imask <<= 1) {
	    int v = in[i] & imask;
	    put_bit(v);
	    if (v) {
	      ones++;
	      if (ones == 5) {
	        put_bit(0);
	        ones = 0;
	      }
	    }
	    else {
	      ones = 0;
	    }
	  }
	}
	for (unsigned char imask = 1; imask != 0; imask <<= 1) {
	  put_bit(flag & imask);
	}
	ret = (olen + 7) / 8;		// Includes any partial byte.

	unsigned char imask = 1;
	while (olen < osize) {
	  put_bit( flag & imask);
	  imask = (imask << 1) | (imask >> 7);	// Rotate.
	}

	return (ret);

} // end stuff_it

// end fx25_send.c
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2021  John Langner, WB2OSZ
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

#include "il2p.h"
#include "textcolor.h"
#include "audio.h"
#include "gen_tone.h"


static int number_of_bits_sent[MAX_RADIO_CHANS];		// Count number of bits sent by "il2p_send_frame"

static void send_bytes (int chan, unsigned char *b, int count, int polarity);
static void send_bit (int chan, int b, int polarity);



/*-------------------------------------------------------------
 *
 * Name:	il2p_send_frame
 *
 * Purpose:	Convert frames to a stream of bits in IL2P format.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		pp	- Pointer to packet object.
 *
 *		max_fec	- 1 to force 16 parity symbols for each payload block.
 *			  0 for automatic depending on block size.
 *
 *		polarity - 0 for normal.  1 to invert signal.
 *			   2 special case for testing - introduce some errors to test FEC.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent including
 *		- Preamble   (01010101...)
 *		- 3 byte Sync Word.
 *		- 15 bytes for Header.
 *		- Optional payload.
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *		-1 is returned for failure.
 *
 * Description:	Generate an IL2P encoded frame.
 *
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 * Errors:	Return -1 for error.  Probably frame too large.
 *
 * Note:	Inconsistency here. ax25 version has just a byte array
 *		and length going in.  Here we need the full packet object.
 *
 *--------------------------------------------------------------*/

int il2p_send_frame (int chan, packet_t pp, int max_fec, int polarity)
{
	unsigned char encoded[IL2P_MAX_PACKET_SIZE];

	encoded[0] = ( IL2P_SYNC_WORD >> 16 ) & 0xff;
	encoded[1] = ( IL2P_SYNC_WORD >> 8  ) & 0xff;
	encoded[2] = ( IL2P_SYNC_WORD       ) & 0xff;

	int elen = il2p_encode_frame (pp, max_fec, encoded + IL2P_SYNC_WORD_SIZE);
	if (elen <= 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("IL2P: Unable to encode frame into IL2P.\n");
	  return (-1);
	}

	elen += IL2P_SYNC_WORD_SIZE;

	number_of_bits_sent[chan] = 0;

	if (il2p_get_debug() >= 1) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("IL2P frame, max_fec = %d, %d encoded bytes total\n", max_fec, elen);
	  fx_hex_dump (encoded, elen);
	}

	// Clobber some bytes for testing.
	if (polarity >= 2) {
	    for (int j = 10; j < elen; j+=100) {
	        encoded[j] = ~ encoded[j];
	    }
	}

	// Send bits to modulator.

	static unsigned char preamble = IL2P_PREAMBLE;

	send_bytes (chan, &preamble, 1, polarity);
	send_bytes (chan, encoded, elen, polarity);
	
	return (number_of_bits_sent[chan]);
}


static void send_bytes (int chan, unsigned char *b, int count, int polarity)
{
	for (int j = 0; j < count; j++) {
	  unsigned int x = b[j];
	  for (int k = 0; k < 8; k++) {
	    send_bit (chan, (x & 0x80) != 0, polarity);
	    x <<= 1;
	  }
	}
}

// NRZI would be applied for AX.25 but IL2P does not use it.
// However we do have an option to invert the signal.
// The direwolf receive implementation will automatically compensate
// for either polarity but other implementations might not.

static void send_bit (int chan, int b, int polarity)
{
	tone_gen_put_bit (chan, (b ^ polarity) & 1);
	number_of_bits_sent[chan]++;
}



// end il2p_send.c
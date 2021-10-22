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

/*--------------------------------------------------------------------------------
 *
 * File:	il2p_scramble.c
 *
 * Purpose:	Scramble / descramble data as specified in the IL2P protocol specification.
 *
 *--------------------------------------------------------------------------------*/


#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "il2p.h"


// Scramble bits for il2p transmit.

// Note that there is a delay of 5 until the first bit comes out.
// So we need to need to ignore the first 5 out and stick in
// an extra 5 filler bits to flush at the end.

#define INIT_TX_LSFR 0x00f

static inline int scramble_bit (int in, int *state)
{
	int out = ((*state >> 4) ^ *state) & 1;
	*state = (  (((in ^ *state) & 1) << 9) | (*state ^ ((*state & 1) << 4)) ) >> 1;
        return (out);
}


// Undo data scrambling for il2p receive.

#define INIT_RX_LSFR 0x1f0

static inline int descramble_bit (int in, int *state)
{
        int out = (in ^ *state) & 1;
	*state = ((*state >> 1) | ((in & 1) << 8)) ^ ((in & 1) << 3);
        return (out);
}


/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_scramble_block
 *
 * Purpose:	Scramble a block before adding RS parity.
 *
 * Inputs:	in		Array of bytes.
 *		len		Number of bytes both in and out.
 *
 * Outputs:	out		Array of bytes.
 *
 *--------------------------------------------------------------------------------*/

void il2p_scramble_block (unsigned char *in, unsigned char *out, int len)
{
	int tx_lfsr_state = INIT_TX_LSFR;

	memset (out, 0, len);

	int skipping = 1;	// Discard the first 5 out.
	int ob = 0;		// Index to output byte.
	int om = 0x80;		// Output bit mask;
	for (int ib = 0; ib < len; ib++) {
	    for (int im = 0x80; im != 0; im >>= 1) {
	        int s = scramble_bit((in[ib] & im) != 0, &tx_lfsr_state);
	        if (ib == 0 && im == 0x04) skipping = 0;
	        if ( ! skipping) {
	            if (s) {
	                out[ob] |= om;
	            }
	            om >>= 1;
	            if (om == 0) {
	                om = 0x80;
	                ob++;
	            }
	        }
	    }
	}
	// Flush it.

	// This is a relic from when I thought the state would need to
	// be passed along for the next block.
	// Preserve the LSFR state from before flushing.
	// This might be needed as the initial state for later payload blocks.
	int x = tx_lfsr_state;
	for (int n = 0; n < 5; n++) {
	    int s = scramble_bit(0, &x);
	    if (s) {
	        out[ob] |= om;
	    }
	    om >>=1;
	    if (om == 0) {
	        om = 0x80;
	        ob++;
	    }
	}

}  // end il2p_scramble_block



/*--------------------------------------------------------------------------------
 *
 * Function:	il2p_descramble_block
 *
 * Purpose:	Descramble a block after removing RS parity.
 *
 * Inputs:	in		Array of bytes.
 *		len		Number of bytes both in and out.
 *
 * Outputs:	out		Array of bytes.
 *
 *--------------------------------------------------------------------------------*/

void il2p_descramble_block (unsigned char *in, unsigned char *out, int len)
{
	int rx_lfsr_state = INIT_RX_LSFR;

	memset (out, 0, len);

	for (int b = 0; b < len; b++) {
	    for (int m = 0x80; m != 0; m >>= 1) {
	        int d = descramble_bit((in[b] & m) != 0, &rx_lfsr_state);
	        if (d) {
	            out[b] |= m;
	        }
	    }
	}
}

// end il2p_scramble.c

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


/********************************************************************************
 *
 * File:        il2p_rec.c
 *
 * Purpose:     Extract IL2P frames from a stream of bits and process them.
 *
 * References:	http://tarpn.net/t/il2p/il2p-specification0-4.pdf
 *
 *******************************************************************************/

#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "textcolor.h"
#include "il2p.h"
#include "multi_modem.h"
#include "demod.h"


struct il2p_context_s {

	enum { IL2P_SEARCHING=0, IL2P_HEADER, IL2P_PAYLOAD, IL2P_DECODE } state;

	unsigned int acc;	// Accumulate most recent 24 bits for sync word matching.
				// Lower 8 bits are also used for accumulating bytes for
				// the header and payload.

	int bc;			// Bit counter so we know when a complete byte has been accumulated.

	int polarity;		// 1 if opposite of expected polarity.

	unsigned char shdr[IL2P_HEADER_SIZE+IL2P_HEADER_PARITY];
				// Scrambled header as received over the radio.  Includes parity.
	int hc;			// Number if bytes placed in above.

	unsigned char uhdr[IL2P_HEADER_SIZE];  // Header after FEC and unscrambling.

	int eplen;		// Encoded payload length.  This is not the nuumber from
				// from the header but rather the number of encoded bytes to gather.

	unsigned char spayload[IL2P_MAX_ENCODED_PAYLOAD_SIZE];
				// Scrambled and encoded payload as received over the radio.
	int pc;			// Number of bytes placed in above.

	int corrected;		// Number of symbols corrected by RS FEC.
};

static struct il2p_context_s *il2p_context[MAX_CHANS][MAX_SUBCHANS][MAX_SLICERS];



/***********************************************************************************
 *
 * Name:        il2p_rec_bit
 *
 * Purpose:     Extract FX.25 packets from a stream of bits.
 *
 * Inputs:      chan    - Channel number.
 *
 *              subchan - This allows multiple demodulators per channel.
 *
 *              slice   - Allows multiple slicers per demodulator (subchannel).
 *
 *              dbit	- One bit from the received data stream.
 *
 * Description: This is called once for each received bit.
 *              For each valid packet, process_rec_frame() is called for further processing.
 *		It can gather multiple candidates from different parallel demodulators
 *		("subchannels") and slicers, then decide which one is the best.
 *
 ***********************************************************************************/

void il2p_rec_bit (int chan, int subchan, int slice, int dbit)
{

// Allocate context blocks only as needed.

	struct il2p_context_s *F = il2p_context[chan][subchan][slice];
	if (F == NULL) {
          assert (chan >= 0 && chan < MAX_CHANS);
          assert (subchan >= 0 && subchan < MAX_SUBCHANS);
          assert (slice >= 0 && slice < MAX_SLICERS);
	  F = il2p_context[chan][subchan][slice] = (struct il2p_context_s *)malloc(sizeof (struct il2p_context_s));
	  assert (F != NULL);
	  memset (F, 0, sizeof(struct il2p_context_s));
	}

// Accumulate most recent 24 bits received.  Most recent is LSB.

	F->acc = ((F->acc << 1) | (dbit & 1)) & 0x00ffffff;

// State machine to look for sync word then gather appropriate number of header and payload bytes.
	  
	switch (F->state) {

	  case IL2P_SEARCHING:		// Searching for the sync word.

	    if (__builtin_popcount (F->acc ^ IL2P_SYNC_WORD) <= 1) {	// allow single bit mismatch
	      //text_color_set (DW_COLOR_INFO);
	      //dw_printf ("IL2P header has normal polarity\n");
	      F->polarity = 0;
	      F->state = IL2P_HEADER;
	      F->bc = 0;
	      F->hc = 0;
	    }
	    else if (__builtin_popcount ((~F->acc & 0x00ffffff) ^ IL2P_SYNC_WORD) <= 1) {
	      text_color_set (DW_COLOR_INFO);
	      // FIXME - this pops up occasionally with random noise.  Find better way to convey information.
	      // This also happens for each slicer - to noisy.
	      //dw_printf ("IL2P header has reverse polarity\n");
	      F->polarity = 1;
	      F->state = IL2P_HEADER;
	      F->bc = 0;
	      F->hc = 0;
	    }
	    break;

	  case IL2P_HEADER:		// Gathering the header.

	    F->bc++;
	    if (F->bc == 8) {	// full byte has been collected.
	      F->bc = 0;
	      if ( ! F->polarity) {
	        F->shdr[F->hc++] = F->acc & 0xff;
	      }
	      else {
	        F->shdr[F->hc++] = (~ F->acc) & 0xff;
	      }
	      if (F->hc == IL2P_HEADER_SIZE+IL2P_HEADER_PARITY) {		// Have all of header

	        if (il2p_get_debug() >= 1) {
		  text_color_set (DW_COLOR_DEBUG);
	          dw_printf ("IL2P header as received [%d.%d.%d]:\n", chan, subchan, slice);
		  fx_hex_dump (F->shdr, IL2P_HEADER_SIZE+IL2P_HEADER_PARITY);
	        }

		// Fix any errors and descramble.
	        F->corrected = il2p_clarify_header(F->shdr, F->uhdr);

	        if (F->corrected >= 0) {	// Good header.
						// How much payload is expected?
	           il2p_payload_properties_t plprop;
		   int hdr_type, max_fec;
		   int len = il2p_get_header_attributes (F->uhdr, &hdr_type, &max_fec);

		   F->eplen = il2p_payload_compute (&plprop, len, max_fec);

	           if (il2p_get_debug() >= 1) {
		     text_color_set(DW_COLOR_DEBUG);
	             dw_printf ("IL2P header after correcting %d symbols and unscrambling [%d.%d.%d]:\n", F->corrected, chan, subchan, slice);
		     fx_hex_dump (F->uhdr, IL2P_HEADER_SIZE);
	             dw_printf ("Header type %d, max fec = %d\n", hdr_type, max_fec);
	             dw_printf ("Need to collect %d encoded bytes for %d byte payload.\n", F->eplen, len);
	             dw_printf ("%d small blocks of %d and %d large blocks of %d.  %d parity symbols per block\n", 
				plprop.small_block_count, plprop.small_block_size,
				plprop.large_block_count, plprop.large_block_size, plprop.parity_symbols_per_block);
	           }

	           if (F->eplen >= 1) {		// Need to gather payload.
	             F->pc = 0;
	             F->state = IL2P_PAYLOAD;
	           }
	           else if (F->eplen == 0) {	// No payload.
	             F->pc = 0;
	             F->state = IL2P_DECODE;
	           }
	           else {			// Error.

	             if (il2p_get_debug() >= 1) {
		       text_color_set (DW_COLOR_ERROR);
	               dw_printf ("IL2P header INVALID.\n");
	             }

	             F->state = IL2P_SEARCHING;
                   }
	        }  // good header after FEC.
	        else {
	           F->state = IL2P_SEARCHING;	// Header failed FEC check.
	        }   
	      }  // entire header has been collected.    
	    }  // full byte collected.
	    break;

	  case IL2P_PAYLOAD:		// Gathering the payload, if any.

	    F->bc++;
	    if (F->bc == 8) {	// full byte has been collected.
	      F->bc = 0;
	      if ( ! F->polarity) {
	        F->spayload[F->pc++] = F->acc & 0xff;
	      }
	      else {
	        F->spayload[F->pc++] = (~ F->acc) & 0xff;
	      }
	      if (F->pc == F->eplen) {

		 // TODO?: for symmetry it seems like we should clarify the payload before combining.

	         F->state = IL2P_DECODE;
	      }
	    }
	    break;

	  case IL2P_DECODE:
	    // We get here after a good header and any payload has been collected.
	    // Processing is delayed by one bit but I think it makes the logic cleaner.
	    // During unit testing be sure to send an extra bit to flush it out at the end.

	    // in uhdr[IL2P_HEADER_SIZE];  // Header after FEC and descrambling.

	    // TODO?:  for symmetry, we might decode the payload here and later build the frame.

	    {
	      packet_t pp = il2p_decode_header_payload (F->uhdr, F->spayload, &(F->corrected));

	      if (il2p_get_debug() >= 1) {
	          if (pp != NULL) {
	              ax25_hex_dump (pp);
	          }
	          else {
	              // Most likely too many FEC errors.
	              text_color_set(DW_COLOR_ERROR);
	              dw_printf ("FAILED to construct frame in %s.\n", __func__);
	          }
	      }

	      if (pp != NULL) {
	          alevel_t alevel = demod_get_audio_level (chan, subchan);
	          retry_t retries = F->corrected;
	          int is_fx25 = 1;		// FIXME: distinguish fx.25 and IL2P.
						// Currently this just means that a FEC mode was used.

	          // TODO: Could we put last 3 arguments in packet object rather than passing around separately?

	          multi_modem_process_rec_packet (chan, subchan, slice, pp, alevel, retries, is_fx25);
	      }
	    }   // end block for local variables.

	    if (il2p_get_debug() >= 1) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("-----\n");
	    }

	    F->state = IL2P_SEARCHING;
	    break;

	} // end of switch

} // end il2p_rec_bit


// end il2p_rec.c


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2019, 2021  John Langner, WB2OSZ
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

#include <stdio.h>

#include "hdlc_send.h"
#include "audio.h"
#include "gen_tone.h"
#include "textcolor.h"
#include "fcs_calc.h"
#include "ax25_pad.h"
#include "fx25.h"
#include "il2p.h"

static void send_byte_msb_first (int chan, int x, int polarity);

static void send_control_nrzi (int, int);
static void send_data_nrzi (int, int);
static void send_bit_nrzi (int, int);



static int number_of_bits_sent[MAX_RADIO_CHANS];	// Count number of bits sent by "hdlc_send_frame" or "hdlc_send_flags"



/*-------------------------------------------------------------
 *
 * Name:	layer2_send_frame
 *
 * Purpose:	Convert frames to a stream of bits.
 *		Originally this was for AX.25 only, hence the file name.
 *		Over time, FX.25 and IL2P were shoehorned in.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		pp	- Packet object.
 *
 *		bad_fcs	- Append an invalid FCS for testing purposes.
 *			  Applies only to regular AX.25.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent including "flags" and the
 *		stuffing bits.  
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *
 * Description:	For AX.25, send:
 *			start flag
 *			bit stuffed data
 *			calculated FCS
 *			end flag
 *		NRZI encoding for all but the "flags."
 *
 * 
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 *--------------------------------------------------------------*/

static int ax25_only_hdlc_send_frame (int chan, unsigned char *fbuf, int flen, int bad_fcs);


int layer2_send_frame (int chan, packet_t pp, int bad_fcs, struct audio_s *audio_config_p)
{
	if (audio_config_p->achan[chan].layer2_xmit == LAYER2_IL2P) {

	  int n = il2p_send_frame (chan, pp, audio_config_p->achan[chan].il2p_max_fec,
						audio_config_p->achan[chan].il2p_invert_polarity);
	  if (n > 0) {
	    return (n);
	  }
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to send IL2p frame.  Falling back to regular AX.25.\n");
	  // Not sure if we should fall back to AX.25 or not here.
	}
	else if (audio_config_p->achan[chan].layer2_xmit == LAYER2_FX25) {
	  unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
	  int flen = ax25_pack (pp, fbuf);
	  int n = fx25_send_frame (chan, fbuf, flen, audio_config_p->achan[chan].fx25_strength);
	  if (n > 0) {
	    return (n);
	  }
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unable to send FX.25.  Falling back to regular AX.25.\n");
	  // Definitely need to fall back to AX.25 here because
	  // the FX.25 frame length is so limited.
	}

	unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
	int flen = ax25_pack (pp, fbuf);
	return (ax25_only_hdlc_send_frame (chan, fbuf, flen, bad_fcs));
}



static int ax25_only_hdlc_send_frame (int chan, unsigned char *fbuf, int flen, int bad_fcs)
{
	int j, fcs;
	

	number_of_bits_sent[chan] = 0;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("hdlc_send_frame ( chan = %d, fbuf = %p, flen = %d, bad_fcs = %d)\n", chan, fbuf, flen, bad_fcs);
	fflush (stdout);
#endif

	send_control_nrzi (chan, 0x7e);	/* Start frame */
	
	for (j=0; j<flen; j++) {
	  send_data_nrzi (chan, fbuf[j]);
	}

	fcs = fcs_calc (fbuf, flen);

	if (bad_fcs) {
	  /* For testing only - Simulate a frame getting corrupted along the way. */
	  send_data_nrzi (chan, (~fcs) & 0xff);
	  send_data_nrzi (chan, ((~fcs) >> 8) & 0xff);
	}
	else {
	  send_data_nrzi (chan, fcs & 0xff);
	  send_data_nrzi (chan, (fcs >> 8) & 0xff);
	}

	send_control_nrzi (chan, 0x7e);	/* End frame */

	return (number_of_bits_sent[chan]);
}


/*-------------------------------------------------------------
 *
 * Name:	layer2_preamble_postamble
 *
 * Purpose:	Send filler pattern before and after the frame.
 *		For HDLC it is 01111110, for IL2P 01010101.
 *
 * Inputs:	chan	- Audio channel number, 0 = first.
 *
 *		nbytes	- Number of bytes to send.
 *
 *		finish	- True for end of transmission.
 *			  This causes the last audio buffer to be flushed.
 *
 *		audio_config_p - Configuration for audio and modems.
 *
 * Outputs:	Bits are shipped out by calling tone_gen_put_bit().
 *
 * Returns:	Number of bits sent.  
 *		There is no bit-stuffing so we would expect this to
 *		be 8 * nbytes.
 *		The required time can be calculated by dividing this
 *		number by the transmit rate of bits/sec.
 *
 * Assumptions:	It is assumed that the tone_gen module has been
 *		properly initialized so that bits sent with 
 *		tone_gen_put_bit() are processed correctly.
 *
 *--------------------------------------------------------------*/

int layer2_preamble_postamble (int chan, int nbytes, int finish, struct audio_s *audio_config_p)
{
	int j;
	
	number_of_bits_sent[chan] = 0;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("hdlc_send_flags ( chan = %d, nflags = %d, finish = %d )\n", chan, nflags, finish);
	fflush (stdout);
#endif

	// When the transmitter is on but not sending data, it should be sending
	// a stream of a filler pattern.
	// For AX.25, it is the 01111110 "flag" pattern with NRZI and no bit stuffing.
	// For IL2P, it is 01010101 without NRZI.

	for (j=0; j<nbytes; j++) {
	  if (audio_config_p->achan[chan].layer2_xmit == LAYER2_IL2P) {
	    send_byte_msb_first (chan, IL2P_PREAMBLE, audio_config_p->achan[chan].il2p_invert_polarity);
	  }
	  else {
	    send_control_nrzi (chan, 0x7e);
	  }
	}

/* Push out the final partial buffer! */

	if (finish) {
	  audio_flush(ACHAN2ADEV(chan));
	}

	return (number_of_bits_sent[chan]);
}



// The next one is only for IL2P.  No NRZI.
// MSB first, opposite of AX.25.

static void send_byte_msb_first (int chan, int x, int polarity)
{
	int i;

	for (i=0; i<8; i++) {
	  int dbit = (x & 0x80) != 0;
	  tone_gen_put_bit (chan, (dbit ^ polarity) & 1);
	  x <<= 1;
	  number_of_bits_sent[chan]++;
	}
}


// The following are only for HDLC.
// All bits are sent NRZI.
// Data (non flags) use bit stuffing.


static int stuff[MAX_RADIO_CHANS];		// Count number of "1" bits to keep track of when we
					// need to break up a long run by "bit stuffing."
					// Needs to be array because we could be transmitting
					// on multiple channels at the same time.

static void send_control_nrzi (int chan, int x)
{
	int i;

	for (i=0; i<8; i++) {
	  send_bit_nrzi (chan, x & 1);
	  x >>= 1;
	}
	
	stuff[chan] = 0;
}

static void send_data_nrzi (int chan, int x)
{
	int i;

	for (i=0; i<8; i++) {
	  send_bit_nrzi (chan, x & 1);
	  if (x & 1) {
	    stuff[chan]++;
	    if (stuff[chan] == 5) {
	      send_bit_nrzi (chan, 0);
	      stuff[chan] = 0;
	    }
	  } else {
	    stuff[chan] = 0;
          }
	  x >>= 1;
	}
}

/*
 * NRZI encoding.
 * data 1 bit -> no change.
 * data 0 bit -> invert signal.
 */

static void send_bit_nrzi (int chan, int b)
{
	static int output[MAX_RADIO_CHANS];

	if (b == 0) {
	  output[chan] = ! output[chan];
	}

	tone_gen_put_bit (chan, output[chan]);

	number_of_bits_sent[chan]++;
}


//  The rest of this is for EAS SAME.
//  This is sort of a logical place because it serializes a frame, but not in HDLC.
//  We have a parallel where SAME deserialization is in hdlc_rec.
//  Maybe both should be pulled out and moved to a same.c.


/*-------------------------------------------------------------------
 *
 * Name:        eas_send
 *
 * Purpose:    	Serialize EAS SAME for transmission.
 *
 * Inputs:	chan	- Radio channel number.
 *		str	- Character string to send.
 *		repeat	- Number of times to repeat with 1 sec quiet between.
 *		txdelay	- Delay (ms) from PTT to first preamble bit.
 *		txtail	- Delay (ms) from last data bit to PTT off.	
 *		
 *
 * Returns:	Total number of milliseconds to activate PTT.
 *		This includes delays before the first character
 *		and after the last to avoid chopping off part of it.
 *
 * Description:	xmit_thread calls this instead of the usual hdlc_send
 *		when we have a special packet that means send EAS SAME
 *		code.
 *
 *--------------------------------------------------------------------*/

static inline void eas_put_byte (int chan, unsigned char b)
{
	for (int n=0; n<8; n++) {
	  tone_gen_put_bit (chan, (b & 1));
	  b >>= 1;
	}
}

int eas_send (int chan, unsigned char *str, int repeat, int txdelay, int txtail)
{
	int bytes_sent = 0;
	const int gap = 1000;
	int gaps_sent = 0;

	gen_tone_put_quiet_ms (chan, txdelay);

	for (int r=0; r<repeat; r++ ) {
	  for (int j=0; j<16; j++) {
	    eas_put_byte (chan, 0xAB);
	    bytes_sent++;
	  }

	  for (unsigned char *p = str; *p != '\0'; p++) {
	    eas_put_byte (chan, *p);
	    bytes_sent++;
	  }

	  if (r < repeat-1) {
	    gen_tone_put_quiet_ms (chan, gap);
	    gaps_sent++;
	  }
	}

	gen_tone_put_quiet_ms (chan, txtail);

	audio_flush(ACHAN2ADEV(chan));

	int elapsed = txdelay + (int) (bytes_sent * 8 * 1.92) + (gaps_sent * gap) + txtail;

// dw_printf ("DEBUG:  EAS total time = %d ms\n", elapsed);

	return (elapsed);

}  /* end eas_send */




/* end hdlc_send.c */

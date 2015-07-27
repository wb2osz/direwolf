//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2012,2013  John Langner, WB2OSZ
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
 * File:	hdlc_rec.c
 *
 * Purpose:	Extract HDLC frames from a stream of bits.
 *
 *******************************************************************************/

#include <stdio.h>
#include <assert.h>

#include "direwolf.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "fcs_calc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "rrbb.h"
#include "multi_modem.h"


//#define TEST 1				/* Define for unit testing. */

//#define DEBUG3 1				/* monitor the data detect signal. */



/* 
 * Minimum & maximum sizes of an AX.25 frame including the 2 octet FCS. 
 */

#define MIN_FRAME_LEN ((AX25_MIN_PACKET_LEN) + 2)
				
#define MAX_FRAME_LEN ((AX25_MAX_PACKET_LEN) + 2)	

/*
 * This is the current state of the HDLC decoder.
 *
 * It is possible to run multiple decoders concurrently by
 * having a separate set of state variables for each.
 *
 * Should have a reset function instead of initializations here.
 */

struct hdlc_state_s {

	int prev_raw;			/* Keep track of previous bit so */
					/* we can look for transitions. */
					/* Should be only 0 or 1. */

	unsigned char pat_det; 		/* 8 bit pattern detector shift register. */
					/* See below for more details. */

	unsigned int flag4_det;		/* Last 32 raw bits to look for 4 */
					/* flag patterns in a row. */

	unsigned char oacc;		/* Accumulator for building up an octet. */

	int olen;			/* Number of bits in oacc. */
					/* When this reaches 8, oacc is copied */
					/* to the frame buffer and olen is zeroed. */
					/* The value of -1 is a special case meaning */
					/* bits should not be accumulated. */

	unsigned char frame_buf[MAX_FRAME_LEN];
					/* One frame is kept here. */

	int frame_len;			/* Number of octets in frame_buf. */
					/* Should be in range of 0 .. MAX_FRAME_LEN. */

	int data_detect;		/* True when HDLC data is detected. */
					/* This will not be triggered by voice or other */
					/* noise or even tones.  */

	enum retry_e fix_bits;		/* Level of effort to recover from */
					/* a bad FCS on the frame. */

	rrbb_t rrbb;			/* Handle for bit array for raw received bits. */
					
};


static struct hdlc_state_s hdlc_state[MAX_CHANS][MAX_SUBCHANS];

static int num_subchan[MAX_CHANS];


/***********************************************************************************
 *
 * Name:	hdlc_rec_init
 *
 * Purpose:	Call once at the beginning to initialize.
 *
 * Inputs:	None.
 *
 ***********************************************************************************/

static int was_init = 0;

void hdlc_rec_init (struct audio_s *pa)
{
	int j, k;
	struct hdlc_state_s *H;

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("hdlc_rec_init (%p) \n", pa);

	assert (pa != NULL);
	
	for (j=0; j<pa->num_channels; j++)
	{
	  num_subchan[j] = pa->num_subchan[j];

	  assert (num_subchan[j] >= 1 && num_subchan[j] < MAX_SUBCHANS);

	  for (k=0; k<MAX_SUBCHANS; k++) 
	  {
	    H = &hdlc_state[j][k];

	    H->prev_raw = 0;
	    H->pat_det = 0;
	    H->flag4_det = 0;
	    H->olen = -1;
	    H->frame_len = 0;
	    H->data_detect = 0;
	    H->fix_bits = pa->fix_bits;
	    H->rrbb = rrbb_new(j, k, pa->modem_type[j] == SCRAMBLE, -1);
	  }
	}

	was_init = 1;
}



/***********************************************************************************
 *
 * Name:	hdlc_rec_bit
 *
 * Purpose:	Extract HDLC frames from a stream of bits.
 *
 * Inputs:	chan	- Channel number.  
 *
 *		subchan	- This allows multiple decoders per channel.
 *
 *		raw 	- One bit from the demodulator.
 *			  should be 0 or 1.
 *	
 *		is_scrambled - Is the data scrambled?
 *
 *		descram_state - Current descrambler state.
 *					
 *		sam	- Possible future: Sample from demodulator output.
 *			  Should nominally be in roughly -1 to +1 range.
 *
 * Description:	This is called once for each received bit.
 *		For each valid frame, process_rec_frame()
 *		is called for further processing.
 *
 ***********************************************************************************/

#if SLICENDICE
void hdlc_rec_bit_sam (int chan, int subchan, int raw, float demod_out)
#else
void hdlc_rec_bit (int chan, int subchan, int raw, int is_scrambled, int descram_state)
#endif
{

	int dbit;			/* Data bit after undoing NRZI. */
					/* Should be only 0 or 1. */
	struct hdlc_state_s *H;

	assert (was_init == 1);

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

/*
 * Different state information for each channel.
 */
	H = &hdlc_state[chan][subchan];

/*
 * Using NRZI encoding,
 *   A '0' bit is represented by an inversion since previous bit.
 *   A '1' bit is represented by no change.
 */

	dbit = (raw == H->prev_raw);
	H->prev_raw = raw;

/*
 * Octets are sent LSB first.
 * Shift the most recent 8 bits thru the pattern detector.
 */
	H->pat_det >>= 1;
	if (dbit) {
	  H->pat_det |= 0x80;
	}

	H->flag4_det >>= 1;
	if (dbit) {
	  H->flag4_det |= 0x80000000;
	}


/*
 * "Data Carrier detect" function based on data rather than
 * tones from a modem.
 *
 * Idle time, at beginning of transmission should be filled
 * with the special "flag" characters.
 *
 * Idle time of all zero bits (alternating tones at maximum rate)
 * has also been observed. 
 */

	if (H->flag4_det == 0x7e7e7e7e) {	
	  

	  if ( ! H->data_detect) {
	    H->data_detect = 1;
#if DEBUG3
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("DCD%d = 1 flags\n", chan);
#endif
	  }
	}

	if (H->flag4_det == 0x7e000000) {	
	  

	  if ( ! H->data_detect) {
	    H->data_detect = 1;
#if DEBUG3
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("DCD%d = 1 zero fill\n", chan);
#endif
	  }
	}


/* 
 * Loss of signal should result in lack of transitions.
 * (all '1' bits) for at least a little while.
 */

  
	if (H->pat_det == 0xff) {	
	  
	  if ( H->data_detect ) {
	    H->data_detect = 0;
#if DEBUG3
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("DCD%d =   0\n", chan);
#endif
	  }
	}


/*
 * End of data carrier detect.  
 * 
 * The rest is concerned with framing.
 */

#if SLICENDICE
	rrbb2_append_bit (H->rrbb, demod_out);
#else
	rrbb_append_bit (H->rrbb, raw);
#endif
	if (H->pat_det == 0x7e) {

	  rrbb_chop8 (H->rrbb);

/*
 * The special pattern 01111110 indicates beginning and ending of a frame.  
 * If we have an adequate number of whole octets, it is a candidate for 
 * further processing.
 *
 * It might look odd that olen is being tested for 7 instead of 0.
 * This is because oacc would already have 7 bits from the special
 * "flag" pattern before it is detected here.
 */


#if OLD_WAY

#if TEST
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("\nfound flag, olen = %d, frame_len = %d\n", olen, frame_len);
#endif
	  if (H->olen == 7 && H->frame_len >= MIN_FRAME_LEN) {

	    unsigned short actual_fcs, expected_fcs;

#if TEST
	    int j;
	    dw_printf ("TRADITIONAL: frame len = %d\n", H->frame_len);
	    for (j=0; j<H->frame_len; j++) {
	      dw_printf ("  %02x", H->frame_buf[j]);
	    }
	    dw_printf ("\n");

#endif
	    /* Check FCS, low byte first, and process... */

	    /* Alternatively, it is possible to include the two FCS bytes */
	    /* in the CRC calculation and look for a magic constant.  */
	    /* That would be easier in the case where the CRC is being */
	    /* accumulated along the way as the octets are received. */
	    /* I think making a second pass over it and comparing is */
	    /* easier to understand. */

	    actual_fcs = H->frame_buf[H->frame_len-2] | (H->frame_buf[H->frame_len-1] << 8);

	    expected_fcs = fcs_calc (H->frame_buf, H->frame_len - 2);

	    if (actual_fcs == expected_fcs) {
	      int alevel = demod_get_audio_level (chan, subchan);

	      multi_modem_process_rec_frame (chan, subchan, H->frame_buf, H->frame_len - 2, alevel, RETRY_NONE);   /* len-2 to remove FCS. */
	    }
	    else {

#if TEST
	      dw_printf ("*** actual fcs = %04x, expected fcs = %04x ***\n", actual_fcs, expected_fcs);
#endif

	    }

	  }

#else

/*
 * New way - Decode the raw bits in later step.
 */

#if TEST
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("\nfound flag, %d bits in frame\n", rrbb_get_len(H->rrbb) - 1);
#endif
	  if (rrbb_get_len(H->rrbb) >= MIN_FRAME_LEN * 8) {
		
	    int alevel = demod_get_audio_level (chan, subchan);

	    rrbb_set_audio_level (H->rrbb, alevel);
	    rrbb_set_fix_bits (H->rrbb, H->fix_bits);
	    hdlc_rec2_block (H->rrbb, H->fix_bits);
	    	/* Now owned by someone else who will free it. */
	    H->rrbb = rrbb_new (chan, subchan, is_scrambled, descram_state); /* Allocate a new one. */
	  }
	  else {
	    rrbb_clear (H->rrbb, is_scrambled, descram_state); 
	  }

	  H->olen = 0;		/* Allow accumulation of octets. */
	  H->frame_len = 0;

#if SLICENDICE
	  rrbb2_append_bit (H->rrbb, H->prev_raw ? 1.0 : -1.0); /* Last bit of flag.  Needed to get first data bit. */
#else
	  rrbb_append_bit (H->rrbb, H->prev_raw); /* Last bit of flag.  Needed to get first data bit. */
#endif
#endif

	}
	else if (H->pat_det == 0xfe) {

/*
 * Valid data will never have 7 one bits in a row.
 *
 *	11111110
 *
 * This indicates loss of signal.
 */

	  H->olen = -1;		/* Stop accumulating octets. */
	  H->frame_len = 0;	/* Discard anything in progress. */

	  rrbb_clear (H->rrbb, is_scrambled, descram_state); 
#if SLICENDICE
	  rrbb2_append_bit (H->rrbb, H->prev_raw ? 1.0 : -1.0); /* Last bit of flag.  Needed to get first data bit. */
#else
	  rrbb_append_bit (H->rrbb, H->prev_raw); /* Last bit of flag.  Needed to get first data bit. */
#endif
	}
	else if ( (H->pat_det & 0xfc) == 0x7c ) {

/*
 * If we have five '1' bits in a row, followed by a '0' bit,
 *
 *	0111110xx
 *
 * the current '0' bit should be discarded because it was added for 
 * "bit stuffing."
 */
	  ;

	} else {

/*
 * In all other cases, accumulate bits into octets, and complete octets
 * into the frame buffer.
 */
	  if (H->olen >= 0) {

	    H->oacc >>= 1;
	    if (dbit) {
	      H->oacc |= 0x80;
	    }
	    H->olen++;

	    if (H->olen == 8) {
	      H->olen = 0;

	      if (H->frame_len < MAX_FRAME_LEN) {
		H->frame_buf[H->frame_len] = H->oacc;
		H->frame_len++;
	      }
	    }
	  }
	}
}



/*-------------------------------------------------------------------
 *
 * Name:        hdlc_rec_data_detect_1
 *		hdlc_rec_data_detect_any
 *
 * Purpose:     Determine if the radio channel is curently busy
 *		with packet data.
 *		This version doesn't care about voice or other sounds.
 *		This is used by the transmit logic to transmit only
 *		when the channel is clear.
 *
 * Inputs:	chan	- Audio channel.  0 for left, 1 for right.
 *
 * Returns:	True if channel is busy (data detected) or 
 *		false if OK to transmit. 
 *
 *
 * Description:	We have two different versions here.
 *
 *		hdlc_rec_data_detect_1 tests a single decoder (subchan)
 *		and is used by the DPLL to determine how much inertia
 *		to use when trying to follow the incoming signal.
 *
 *		hdlc_rec_data_detect_any sees if ANY of the decoders
 *		for this channel are receving a signal.   This is
 *		used to determine whether the channel is clear and
 *		we can transmit.  This would apply to the 300 baud
 *		HF SSB case where we have multiple decoders running
 *		at the same time.  The channel is busy if ANY of them
 *		thinks the channel is busy.
 *
 *--------------------------------------------------------------------*/

int hdlc_rec_data_detect_1 (int chan, int subchan)
{
	assert (chan >= 0 && chan < MAX_CHANS);

	return ( hdlc_state[chan][subchan].data_detect );

} /* end hdlc_rec_data_detect_1 */


int hdlc_rec_data_detect_any (int chan)
{
	int subchan;

	assert (chan >= 0 && chan < MAX_CHANS);

	for (subchan = 0; subchan < num_subchan[chan]; subchan++) {

	  assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	  if (hdlc_state[chan][subchan].data_detect) {
	    return (1);
	  }
	}
	return (0);


} /* end hdlc_rec_data_detect_any */


/* end hdlc_rec.c */



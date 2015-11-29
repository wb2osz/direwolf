//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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
#include <string.h>

#include "direwolf.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "fcs_calc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "rrbb.h"
#include "multi_modem.h"
#include "demod_9600.h"		/* for descramble() */
#include "ptt.h"


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

	int lfsr;			/* Descrambler shift register for 9600 baud. */

	int prev_descram;		/* Previous descrambled for 9600 baud. */

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

	rrbb_t rrbb;			/* Handle for bit array for raw received bits. */
					
};

static struct hdlc_state_s hdlc_state[MAX_CHANS][MAX_SUBCHANS][MAX_SLICERS];

static int num_subchan[MAX_CHANS];		//TODO1.2 use ptr rather than copy.

static int composite_dcd[MAX_CHANS][MAX_SUBCHANS+1];


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
	int ch, sub, slice;
	struct hdlc_state_s *H;

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("hdlc_rec_init (%p) \n", pa);

	assert (pa != NULL);
	
	memset (composite_dcd, 0, sizeof(composite_dcd));

	for (ch = 0; ch < MAX_CHANS; ch++)
	{

	  if (pa->achan[ch].valid) {

	    num_subchan[ch] = pa->achan[ch].num_subchan;

	    assert (num_subchan[ch] >= 1 && num_subchan[ch] <= MAX_SUBCHANS);

	    for (sub = 0; sub < num_subchan[ch]; sub++)
	    {
	      for (slice = 0; slice < MAX_SLICERS; slice++) {

	        H = &hdlc_state[ch][sub][slice];

	        H->olen = -1;

		// TODO: FIX13 wasteful if not needed.
		// Should loop on number of slicers, not max.

	        H->rrbb = rrbb_new(ch, sub, slice, pa->achan[ch].modem_type == MODEM_SCRAMBLE, H->lfsr, H->prev_descram);
	      }
	    }
	  }
	}
	hdlc_rec2_init (pa);
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
 *		subchan	- This allows multiple demodulators per channel.
 *
 *		slice	- Allows multiple slicers per demodulator (subchannel).
 *
 *		raw 	- One bit from the demodulator.
 *			  should be 0 or 1.
 *	
 *		is_scrambled - Is the data scrambled?
 *
 *		descram_state - Current descrambler state.  (not used - remove)
 *					
 *
 * Description:	This is called once for each received bit.
 *		For each valid frame, process_rec_frame()
 *		is called for further processing.
 *
 ***********************************************************************************/

// TODO: int not_used_remove

void hdlc_rec_bit (int chan, int subchan, int slice, int raw, int is_scrambled, int not_used_remove)
{

	int dbit;			/* Data bit after undoing NRZI. */
					/* Should be only 0 or 1. */
	struct hdlc_state_s *H;

	assert (was_init == 1);

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	assert (slice >= 0 && slice < MAX_SLICERS);

/*
 * Different state information for each channel / subchannel / slice.
 */
	H = &hdlc_state[chan][subchan][slice];

/*
 * Using NRZI encoding,
 *   A '0' bit is represented by an inversion since previous bit.
 *   A '1' bit is represented by no change.
 */

	if (is_scrambled) {
	  int descram;

	  descram = descramble(raw, &(H->lfsr));

	  dbit = (descram == H->prev_descram);
	  H->prev_descram = descram;			
	  H->prev_raw = raw;	}
	else {

	  dbit = (raw == H->prev_raw);
	  H->prev_raw = raw;
	}

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
 * "Data Carrier detect" function based on data patterns rather than
 * audio signal strength.
 *
 * Idle time, at beginning of transmission should be filled
 * with the special "flag" characters.
 *
 * Idle time of all zero bits (alternating tones at maximum rate)
 * has also been observed rarely. 
 * Recognize zero(s) followed by a flag even though it vilolates the spec.
 */

/*
 * Originally, this looked for 4 flags in a row or 3 zeros and a flag. 
 * Is that too fussy?
 * Here are the numbers of start of DCD for our favorite Track 2 test.
 *
 *	7e7e7e7e  504 	7e000000  32  	
 *	7e7e7e--  513   7e0000--  33	
 *	7e7e----  555   7e00----  42	
 *	7e------ 2088
 *					
 * I don't think we want to look for a single flag because that would
 * make DCD too sensitive to noise and it would interfere with waiting for a 
 * clear channel to transmit.  Even a two byte match causes a lot of flickering
 * when listening to live signals.  Let's try 3 and see how that works out.
 */

	//if (H->flag4_det == 0x7e7e7e7e) {
	if ((H->flag4_det & 0xffffff00) == 0x7e7e7e00) {	
	//if ((H->flag4_det & 0xffff0000) == 0x7e7e0000) {	

	  if ( ! H->data_detect) {
	    H->data_detect = 1;
	    dcd_change (chan, subchan, slice, 1);
	  }
	}
	//else if (H->flag4_det == 0x7e000000) {	
	else if ((H->flag4_det & 0xffffff00) == 0x7e000000) {	
	//else if ((H->flag4_det & 0xffff0000) == 0x7e000000) {	
	  
	  if ( ! H->data_detect) {
	    H->data_detect = 1;
	    dcd_change (chan, subchan, slice, 1);
	  }
	}


/* 
 * Loss of signal should result in lack of transitions.
 * (all '1' bits) for at least a little while.
 */

  
	if (H->pat_det == 0xff) {	
	  
	  if ( H->data_detect ) {
	    H->data_detect = 0;
	    dcd_change (chan, subchan, slice, 0);
	  }
	}


/*
 * End of data carrier detect.  
 * 
 * The rest is concerned with framing.
 */


	rrbb_append_bit (H->rrbb, raw);

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
	      alevel_t alevel = demod_get_audio_level (chan, subchan);

	      multi_modem_process_rec_frame (chan, subchan, slice, H->frame_buf, H->frame_len - 2, alevel, RETRY_NONE);   /* len-2 to remove FCS. */
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
	  dw_printf ("\nfound flag, channel %d.%d, %d bits in frame\n", chan, subchan, rrbb_get_len(H->rrbb) - 1);
#endif
	  if (rrbb_get_len(H->rrbb) >= MIN_FRAME_LEN * 8) {
		
	    alevel_t alevel = demod_get_audio_level (chan, subchan);

	    rrbb_set_audio_level (H->rrbb, alevel);
	    hdlc_rec2_block (H->rrbb);
	    	/* Now owned by someone else who will free it. */

	    H->rrbb = rrbb_new (chan, subchan, slice, is_scrambled, H->lfsr, H->prev_descram); /* Allocate a new one. */
	  }
	  else {
	    rrbb_clear (H->rrbb, is_scrambled, H->lfsr, H->prev_descram); 
	  }

	  H->olen = 0;		/* Allow accumulation of octets. */
	  H->frame_len = 0;


	  rrbb_append_bit (H->rrbb, H->prev_raw); /* Last bit of flag.  Needed to get first data bit. */
						/* Now that we are saving other initial state information, */
						/* it would be sensible to do the same for this instead */
						/* of lumping it in with the frame data bits. */
#endif

	}

//#define EXPERIMENT12B 1

#if EXPERIMENT12B

	else if (H->pat_det == 0xff) {

/*
 * Valid data will never have seven 1 bits in a row.
 *
 *	11111110
 *
 * This indicates loss of signal.
 * But we will let it slip thru because it might diminish
 * our single bit fixup effort.   Instead give up on frame
 * only when we see eight 1 bits in a row.
 *
 *	11111111
 *
 * What is the impact?  No difference.
 *
 *  Before:	atest -P E -F 1 ../02_Track_2.wav	= 1003
 *  After:	atest -P E -F 1 ../02_Track_2.wav	= 1003
 */

#else
	else if (H->pat_det == 0xfe) {

/*
 * Valid data will never have 7 one bits in a row.
 *
 *	11111110
 *
 * This indicates loss of signal.
 */

#endif

	  H->olen = -1;		/* Stop accumulating octets. */
	  H->frame_len = 0;	/* Discard anything in progress. */

	  rrbb_clear (H->rrbb, is_scrambled, H->lfsr, H->prev_descram); 

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
 * Name:        hdlc_rec_gathering
 *
 * Purpose:     Report whether bits are currently being gathered into a frame.
 *		This is used to influence the PLL inertia.
 *		The idea is that the PLL should be a little more agreeable to
 *		synchronize with the incoming data stream when not in a frame
 *		and resist changing a little more when capturing a frame.
 *
 * Inputs:	chan
 *		subchan
 *		slice
 *
 * Returns:	True if we are currently gathering bits.
 *		In this case we want the PLL to have more inertia.
 *
 * Discussion:	This simply returns the data carrier detect state.
 *		A couple other variations were tried but turned out to
 *		be slightly worse.
 *
 *--------------------------------------------------------------------*/

int hdlc_rec_gathering (int chan, int subchan, int slice)
{
	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SLICERS);

	// Counts from 	     Track 1 & Track 2
	// data_detect		992	988
	// olen>=0		992	985
	// OR-ed		992	985

	return ( hdlc_state[chan][subchan][slice].data_detect );

} /* end hdlc_rec_gathering */



/*-------------------------------------------------------------------
 *
 * Name:        dcd_change
 *
 * Purpose:     Combine DCD states of all subchannels/ into an overall
 *		state for the channel.
 *
 * Inputs:	chan	
 *
 *		subchan		0 to MAX_SUBCHANS-1 for HDLC.
 *				SPECIAL CASE --> MAX_SUBCHANS for DTMF decoder.
 *
 *		slice		slicer number, 0 .. MAX_SLICERS - 1.
 *
 *		state		1 for active, 0 for not.
 *
 * Returns:	None.  Use hdlc_rec_data_detect_any to retrieve result.
 *
 * Description:	DCD for the channel is active if ANY of the subchannels/slices
 *		are active.  Update the DCD indicator.
 *
 * version 1.3:	Add DTMF detection into the final result.
 *		This is now called from dtmf.c too.
 *
 *--------------------------------------------------------------------*/

void dcd_change (int chan, int subchan, int slice, int state)
{
	int old, new;

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan <= MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SLICERS);
	assert (state == 0 || state == 1);

#if DEBUG3
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("DCD %d.%d.%d = %d \n", chan, subchan, slice, state);
#endif

	old = hdlc_rec_data_detect_any(chan);

	if (state) {
	  composite_dcd[chan][subchan] |= (1 << slice);
	}
	else {
	  composite_dcd[chan][subchan] &=  ~ (1 << slice);
	}

	new = hdlc_rec_data_detect_any(chan);

	if (new != old) {
	  ptt_set (OCTYPE_DCD, chan, new);
	}
}


/*-------------------------------------------------------------------
 *
 * Name:        hdlc_rec_data_detect_any
 *
 * Purpose:     Determine if the radio channel is curently busy
 *		with packet data.
 *		This version doesn't care about voice or other sounds.
 *		This is used by the transmit logic to transmit only
 *		when the channel is clear.
 *
 * Inputs:	chan	- Audio channel. 
 *
 * Returns:	True if channel is busy (data detected) or 
 *		false if OK to transmit. 
 *
 *
 * Description:	We have two different versions here.
 *
 *		hdlc_rec_data_detect_any sees if ANY of the decoders
 *		for this channel are receving a signal.   This is
 *		used to determine whether the channel is clear and
 *		we can transmit.  This would apply to the 300 baud
 *		HF SSB case where we have multiple decoders running
 *		at the same time.  The channel is busy if ANY of them
 *		thinks the channel is busy.
 *
 * Version 1.3: New option for input signal to inhibit transmit.
 *
 *--------------------------------------------------------------------*/

int hdlc_rec_data_detect_any (int chan)
{

	int sc;
	assert (chan >= 0 && chan < MAX_CHANS);

	for (sc = 0; sc < num_subchan[chan]; sc++) {
	  if (composite_dcd[chan][sc] != 0)
	    return (1);
	}

	if (get_input(ICTYPE_TXINH, chan) == 1) return (1);

	return (0);

} /* end hdlc_rec_data_detect_any */

/* end hdlc_rec.c */



//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2014, 2015, 2016, 2019, 2023  John Langner, WB2OSZ
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
 * Module:      gen_tone.c
 *
 * Purpose:     Convert bits to AFSK for writing to .WAV sound file 
 *		or a sound device.
 *
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"


#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>


#include "audio.h"
#include "gen_tone.h"
#include "textcolor.h"

#include "fsk_demod_state.h"	/* for MAX_FILTER_SIZE which might be overly generous for here. */
				/* but safe if we use same size as for receive. */
#include "dsp.h"


// Properties of the digitized sound stream & modem.

static struct audio_s *save_audio_config_p = NULL;

/*
 * 8 bit samples are unsigned bytes in range of 0 .. 255.
 *
 * 16 bit samples are signed short in range of -32768 .. +32767.
 */


/* Constants after initialization. */

#define TICKS_PER_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

static int ticks_per_sample[MAX_CHANS];	/* Same for both channels of same soundcard */
					/* because they have same sample rate */
					/* but less confusing to have for each channel. */

static int ticks_per_bit[MAX_CHANS];
static int f1_change_per_sample[MAX_CHANS];
static int f2_change_per_sample[MAX_CHANS];
static float samples_per_symbol[MAX_CHANS];


static short sine_table[256];


/* Accumulators. */

static unsigned int tone_phase[MAX_CHANS]; // Phase accumulator for tone generation.
					   // Upper bits are used as index into sine table.

#define PHASE_SHIFT_180 ( 128u << 24 )
#define PHASE_SHIFT_90  (  64u << 24 )
#define PHASE_SHIFT_45  (  32u << 24 )


static int bit_len_acc[MAX_CHANS];	// To accumulate fractional samples per bit.

static int lfsr[MAX_CHANS];		// Shift register for scrambler.

static int bit_count[MAX_CHANS];	// Counter incremented for each bit transmitted
					// on the channel.   This is only used for QPSK.
					// The LSB determines if we save the bit until
					// next time, or send this one with the previously saved.
					// The LSB+1 position determines if we add an
					// extra 180 degrees to the phase to compensate
					// for having 1.5 carrier cycles per symbol time.

					// For 8PSK, it has a different meaning.  It is the
					// number of bits in 'save_bit' so we can accumulate
					// three for each symbol.
static int save_bit[MAX_CHANS];


static int prev_dat[MAX_CHANS];		// Previous data bit.  Used for G3RUH style.




/*------------------------------------------------------------------
 *
 * Name:        gen_tone_init
 *
 * Purpose:     Initialize for AFSK tone generation which might
 *		be used for RTTY or amateur packet radio.
 *
 * Inputs:      audio_config_p		- Pointer to modem parameter structure, modem_s.
 *
 *				The fields we care about are:
 *
 *					samples_per_sec
 *					baud
 *					mark_freq
 *					space_freq
 *					samples_per_sec
 *
 *		amp		- Signal amplitude on scale of 0 .. 100.
 *
 *				  100% uses the full 16 bit sample range of +-32k.
 *
 *		gen_packets	- True if being called from "gen_packets" utility
 *				  rather than the "direwolf" application.
 *
 * Returns:     0 for success.
 *              -1 for failure.
 *
 * Description:	 Calculate various constants for use by the direct digital synthesis
 * 		audio tone generation.
 *
 *----------------------------------------------------------------*/

static int amp16bit;	/* for 9600 baud */


int gen_tone_init (struct audio_s *audio_config_p, int amp, int gen_packets)
{
	int j;
	int chan = 0;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("gen_tone_init ( audio_config_p=%p, amp=%d, gen_packets=%d )\n",
			audio_config_p, amp, gen_packets);
#endif
	
/* 
 * Save away modem parameters for later use. 
 */

	save_audio_config_p = audio_config_p;
	
	amp16bit = (int)((32767 * amp) / 100);

	for (chan = 0; chan < MAX_CHANS; chan++) {

	  if (audio_config_p->chan_medium[chan] == MEDIUM_RADIO) {

	    int a = ACHAN2ADEV(chan);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("gen_tone_init: chan=%d, modem_type=%d, bps=%d, samples_per_sec=%d\n",
		chan,
		save_audio_config_p->achan[chan].modem_type,
		audio_config_p->achan[chan].baud,
		audio_config_p->adev[a].samples_per_sec);
#endif

	    tone_phase[chan] = 0;
	    bit_len_acc[chan] = 0;
	    lfsr[chan] = 0;

	    ticks_per_sample[chan] = (int) ((TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);

	    // The terminology is all wrong here.  Didn't matter with 1200 and 9600.
	    // The config speed should be bits per second rather than baud.
	    // ticks_per_bit should be ticks_per_symbol.

	    switch (save_audio_config_p->achan[chan].modem_type) {

	      case MODEM_QPSK:

	        audio_config_p->achan[chan].mark_freq = 1800;
	        audio_config_p->achan[chan].space_freq = audio_config_p->achan[chan].mark_freq;	// Not Used.

	        // symbol time is 1 / (half of bps)
	        ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / ((double)audio_config_p->achan[chan].baud * 0.5)) + 0.5);
	        f1_change_per_sample[chan] = (int) (((double)audio_config_p->achan[chan].mark_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        f2_change_per_sample[chan] = f1_change_per_sample[chan];	// Not used.
	        samples_per_symbol[chan] = 2. * (float)audio_config_p->adev[a].samples_per_sec / (float)audio_config_p->achan[chan].baud;

	        tone_phase[chan] = PHASE_SHIFT_45;	// Just to mimic first attempt.
							// ??? Why?  We are only concerned with the difference
							// from one symbol to the next.
	        break;

	      case MODEM_8PSK:

	        audio_config_p->achan[chan].mark_freq = 1800;
	        audio_config_p->achan[chan].space_freq = audio_config_p->achan[chan].mark_freq;	// Not Used.

	        // symbol time is 1 / (third of bps)
	        ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / ((double)audio_config_p->achan[chan].baud / 3.)) + 0.5);
	        f1_change_per_sample[chan] = (int) (((double)audio_config_p->achan[chan].mark_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        f2_change_per_sample[chan] = f1_change_per_sample[chan];	// Not used.
	        samples_per_symbol[chan] = 3. * (float)audio_config_p->adev[a].samples_per_sec / (float)audio_config_p->achan[chan].baud;
	        break;

	      case MODEM_BASEBAND:
	      case MODEM_SCRAMBLE:
	      case MODEM_AIS:

		// Tone is half baud.
	        ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / (double)audio_config_p->achan[chan].baud ) + 0.5);
	        f1_change_per_sample[chan] = (int) (((double)audio_config_p->achan[chan].baud * 0.5 * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        samples_per_symbol[chan] = (float)audio_config_p->adev[a].samples_per_sec / (float)audio_config_p->achan[chan].baud;
	        break;

	      case MODEM_EAS:		//  EAS.

		// TODO: Proper fix would be to use float for baud, mark, space.

	        ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / 520.833333333333 ) + 0.5);
	        samples_per_symbol[chan] = (int)((audio_config_p->adev[a].samples_per_sec / 520.83333) + 0.5);
	        f1_change_per_sample[chan] = (int) ((2083.33333333333 * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        f2_change_per_sample[chan] = (int) ((1562.5000000 * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        break;

	      default:		// AFSK

	        ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / (double)audio_config_p->achan[chan].baud ) + 0.5);
	        samples_per_symbol[chan] = (float)audio_config_p->adev[a].samples_per_sec / (float)audio_config_p->achan[chan].baud;
	        f1_change_per_sample[chan] = (int) (((double)audio_config_p->achan[chan].mark_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        f2_change_per_sample[chan] = (int) (((double)audio_config_p->achan[chan].space_freq * TICKS_PER_CYCLE / (double)audio_config_p->adev[a].samples_per_sec ) + 0.5);
	        break;
	    }
	  }
	}

        for (j=0; j<256; j++) {
	  double a;
	  int s;

	  a = ((double)(j) / 256.0) * (2 * M_PI);
	  s = (int) (sin(a) * 32767 * amp / 100.0);

	  /* 16 bit sound sample must fit in range of -32768 .. +32767. */
	
	  if (s < -32768) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("gen_tone_init: Excessive amplitude is being clipped.\n");
	    s = -32768;
	  }
	  else if (s > 32767) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("gen_tone_init: Excessive amplitude is being clipped.\n");
	    s = 32767;
	  }
	  sine_table[j] = s;
        }

	return (0);

 } /* end gen_tone_init */


/*-------------------------------------------------------------------
 *
 * Name:        tone_gen_put_bit
 *
 * Purpose:     Generate tone of proper duration for one data bit.
 *
 * Inputs:      chan	- Audio channel, 0 = first.
 *
 *		dat	- 0 for f1, 1 for f2.
 *
 * 			  	-1 inserts half bit to test data	
 *				recovery PLL.
 *
 * Assumption:  fp is open to a file for write.
 *
 * Version 1.4:	Attempt to implement 2400 and 4800 bps PSK modes.
 *
 * Version 1.6: For G3RUH, rather than generating square wave and low
 *		pass filtering, generate the waveform directly.
 *		This avoids overshoot, ringing, and adding more jitter.
 *		Alternating bits come out has sine wave of baud/2 Hz.
 *
 * Version 1.6:	MFJ-2400 compatibility for V.26.
 *
 *--------------------------------------------------------------------*/

// Interpolate between two values.
// My original approximation simply jumped between phases, producing a discontinuity,
// and increasing bandwidth.
// According to multiple sources, we should transition more gently.
// Below see see a rough approximation of:
//  * A step function, immediately going to new value.
//  * Linear interpoation.
//  * Raised cosine.  Square root of cosine is also mentioned.
//
//	new	      -		    /		   --
//		      |		   /		  /
//		      |		  /		  |
//		      |		 /		  /
//	old	-------		/		--
//		step		linear		raised cosine
//
// Inputs are the old (previous value), new value, and a blending control
// 0 -> take old value
// 1 -> take new value.
// inbetween some sort of weighted average.

static inline float interpol8 (float oldv, float newv, float bc)
{
	// Step function.
	//return (newv);				// 78 on 11/7

	assert (bc >= 0);
	assert (bc <= 1.1);

	if (bc < 0) return (oldv);
	if (bc > 1) return (newv);

	// Linear interpolation, just for comparison.
	//return (bc * newv + (1.0f - bc) * oldv);	// 39 on 11/7

	float rc = 0.5f * (cosf(bc * M_PI - M_PI) + 1.0f);
	float rrc = bc >= 0.5f
				? 0.5f * (sqrtf(fabsf(cosf(bc * M_PI - M_PI))) + 1.0f)
				: 0.5f * (-sqrtf(fabsf(cosf(bc * M_PI - M_PI))) + 1.0f);

	(void)rrc;
	return (rc * newv + (1.0f - bc) * oldv);	// 49 on 11/7
	//return (rrc * newv + (1.0f - bc) * oldv);	// 55 on 11/7
}

static const int gray2phase_v26[4] = {0, 1, 3, 2};
static const int gray2phase_v27[8] = {1, 0, 2, 3, 6, 7, 5, 4};

// #define PSKIQ 1  // not ready for prime time yet.
#if PSKIQ
static int xmit_octant[MAX_CHANS];	// absolute phase in 45 degree units.
static int xmit_prev_octant[MAX_CHANS];	// from previous symbol.

// For PSK, we generate the final signal by combining fixed frequency cosine and
// sine by the following weights.
static const float ci[8] = { 1,	.7071,	0,	-.7071,	-1,	-.7071,	0,	.7071	};
static const float sq[8] = { 0,	.7071,	1,	.7071,	0,	-.7071,	-1,	-.7071	};
#endif

void tone_gen_put_bit (int chan, int dat)
{
	int a = ACHAN2ADEV(chan);	/* device for channel. */

	assert (save_audio_config_p != NULL);

	if (save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid channel %d for tone generation.\n", chan);
	  return;
	}

        if (dat < 0) { 
	  /* Hack to test receive PLL recovery. */
	  bit_len_acc[chan] -= ticks_per_bit[chan]; 
	  dat = 0; 
	} 

// TODO: change to switch instead of if if if

	if (save_audio_config_p->achan[chan].modem_type == MODEM_QPSK) {

	  int dibit;
	  int symbol;

	  dat &= 1;	// Keep only LSB to be extra safe.

	  if ( ! (bit_count[chan] & 1)) {
	    save_bit[chan] = dat;
	    bit_count[chan]++;
	    return;
	  }

	  // All zero bits should give us steady 1800 Hz.
	  // All one bits should flip phase by 180 degrees each time.
	  // For V.26B, add another 45 degrees.
	  // This seems to work a little better.

	  dibit = (save_bit[chan] << 1) | dat;

	  symbol = gray2phase_v26[dibit];	// 0 .. 3 for QPSK.
#if PSKIQ
	  // One phase shift unit is 45 degrees.
	  // Remember what it was last time and calculate new.
	  // values 0 .. 7.
	  xmit_prev_octant[chan] = xmit_octant[chan];
	  xmit_octant[chan] += symbol * 2;
	  if (save_audio_config_p->achan[chan].v26_alternative == V26_B) {
	    xmit_octant[chan] += 1;
	  }
	  xmit_octant[chan] &= 0x7;
#else
	  tone_phase[chan] += symbol * PHASE_SHIFT_90;
	  if (save_audio_config_p->achan[chan].v26_alternative == V26_B) {
	    tone_phase[chan] += PHASE_SHIFT_45;
	  }
#endif
	  bit_count[chan]++;
	}

	if (save_audio_config_p->achan[chan].modem_type == MODEM_8PSK) {

	  int tribit;
	  int symbol;

	  dat &= 1;	// Keep only LSB to be extra safe.

	  if (bit_count[chan] < 2) {
	    save_bit[chan] = (save_bit[chan] << 1) | dat;
	    bit_count[chan]++;
	    return;
	  }

	  // The bit pattern 001 should give us steady 1800 Hz.
	  // All one bits should flip phase by 180 degrees each time.

	  tribit = (save_bit[chan] << 1) | dat;

	  symbol = gray2phase_v27[tribit];
	  tone_phase[chan] += symbol * PHASE_SHIFT_45;

	  save_bit[chan] = 0;
	  bit_count[chan] = 0;
	}

	// Would be logical to have MODEM_BASEBAND for IL2P rather than checking here.  But...
	// That would mean putting in at least 3 places and testing all rather than just one.
	if (save_audio_config_p->achan[chan].modem_type == MODEM_SCRAMBLE &&
	    save_audio_config_p->achan[chan].layer2_xmit != LAYER2_IL2P) {
	  int x;

	  x = (dat ^ (lfsr[chan] >> 16) ^ (lfsr[chan] >> 11)) & 1;
	  lfsr[chan] = (lfsr[chan] << 1) | (x & 1);
	  dat = x;
	}
#if PSKIQ
	int blend = 1;
#endif
	do {		/* until enough audio samples for this symbol. */

	  int sam;

	  switch (save_audio_config_p->achan[chan].modem_type) {

	    case MODEM_AFSK:

#if DEBUG2
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("tone_gen_put_bit %d AFSK\n", __LINE__);
#endif

	      // v1.7 reversed.
	      // Previously a data '1' selected the second (usually higher) tone.
	      // It never really mattered before because we were using NRZI.
	      // With the addition of IL2P, we need to be more careful.
	      // A data '1' should be the mark tone.

	      tone_phase[chan] += dat ? f1_change_per_sample[chan] : f2_change_per_sample[chan];
              sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
	      gen_tone_put_sample (chan, a, sam);
	      break;

	    case MODEM_EAS:

	      tone_phase[chan] += dat ? f1_change_per_sample[chan] : f2_change_per_sample[chan];
              sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
	      gen_tone_put_sample (chan, a, sam);
	      break;

	    case MODEM_QPSK:

#if DEBUG2
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("tone_gen_put_bit %d PSK\n", __LINE__);
#endif
	      tone_phase[chan] += f1_change_per_sample[chan];
#if PSKIQ
#if 1  // blend JWL
	      // remove loop invariant
	      float old_i = ci[xmit_prev_octant[chan]];
	      float old_q = sq[xmit_prev_octant[chan]];

	      float new_i = ci[xmit_octant[chan]];
	      float new_q = sq[xmit_octant[chan]];

	      float b = blend / samples_per_symbol[chan];	// roughly 0 to 1
	      blend++;
	     // b = (b - 0.5) * 20 + 0.5;
	     // if (b < 0) b = 0;
	     // if (b > 1) b = 1;
		// b = b > 0.5;
		//b = 1;		// 78 decoded with this.
					// only 39 without.


	      //float blended_i = new_i * b + old_i * (1.0f - b);
	      //float blended_q = new_q * b + old_q * (1.0f - b);

	      float blended_i = interpol8 (old_i, new_i, b);
	      float blended_q = interpol8 (old_q, new_q, b);

	      sam = blended_i * sine_table[((tone_phase[chan] - PHASE_SHIFT_90) >> 24) & 0xff] +
	            blended_q * sine_table[(tone_phase[chan] >> 24) & 0xff];
#else  // jump
	      sam = ci[xmit_octant[chan]] * sine_table[((tone_phase[chan] - PHASE_SHIFT_90) >> 24) & 0xff] +
	            sq[xmit_octant[chan]] * sine_table[(tone_phase[chan] >> 24) & 0xff];
#endif
#else
              sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
#endif
	      gen_tone_put_sample (chan, a, sam);
	      break;

	    case MODEM_8PSK:
#if DEBUG2
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("tone_gen_put_bit %d PSK\n", __LINE__);
#endif
	      tone_phase[chan] += f1_change_per_sample[chan];
              sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
	      gen_tone_put_sample (chan, a, sam);
	      break;

	    case MODEM_BASEBAND:
	    case MODEM_SCRAMBLE:
	    case MODEM_AIS:

	      if (dat != prev_dat[chan]) {
	        tone_phase[chan] += f1_change_per_sample[chan];
	      }
	      else {
	        if (tone_phase[chan] & 0x80000000)
	          tone_phase[chan] = 0xc0000000;	// 270 degrees.
	        else
	          tone_phase[chan] = 0x40000000;	// 90 degrees.
	      }
              sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
	      gen_tone_put_sample (chan, a, sam);
	      break;

	    default:
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("INTERNAL ERROR: %s %d achan[%d].modem_type = %d\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].modem_type);
	      exit (EXIT_FAILURE);
	  }

	  /* Enough for the bit time? */

	  bit_len_acc[chan] += ticks_per_sample[chan];

        } while (bit_len_acc[chan] < ticks_per_bit[chan]);

	bit_len_acc[chan] -= ticks_per_bit[chan];

	prev_dat[chan] = dat;		// Only needed for G3RUH baseband/scrambled.

}  /* end tone_gen_put_bit */


void gen_tone_put_sample (int chan, int a, int sam) {

        /* Ship out an audio sample. */
	/* 16 bit is signed, little endian, range -32768 .. +32767 */
	/* 8 bit is unsigned, range 0 .. 255 */

	assert (save_audio_config_p != NULL);

	assert (save_audio_config_p->adev[a].num_channels == 1 || save_audio_config_p->adev[a].num_channels == 2);

	assert (save_audio_config_p->adev[a].bits_per_sample == 16 || save_audio_config_p->adev[a].bits_per_sample == 8);

	// Bad news if we are clipping and distorting the signal.
	// We are using the full range.
	// Too late to change now because everyone would need to recalibrate their
	// transmit audio level.

	if (sam < -32767) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Warning: Audio sample %d clipped to -32767.\n", sam);
	  sam = -32767;
	}
	else if (sam > 32767) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Warning: Audio sample %d clipped to +32767.\n", sam);
	  sam = 32767;
	}

	if (save_audio_config_p->adev[a].num_channels == 1) {

	  /* Mono */

	  if (save_audio_config_p->adev[a].bits_per_sample == 8) {
            audio_put (a, ((sam+32768) >> 8) & 0xff);
	  }
	  else {
            audio_put (a, sam & 0xff);
            audio_put (a, (sam >> 8) & 0xff);
	  }
 	}
	else {

	  if (chan == ADEVFIRSTCHAN(a)) {
	  
	    /* Stereo, left channel. */

	    if (save_audio_config_p->adev[a].bits_per_sample == 8) {
              audio_put (a, ((sam+32768) >> 8) & 0xff);
              audio_put (a, 0);
	    }
	    else {
              audio_put (a, sam & 0xff);
              audio_put (a, (sam >> 8) & 0xff);
 
              audio_put (a, 0);
              audio_put (a, 0);
	    }
	  }
	  else { 

	    /* Stereo, right channel. */
	  
	    if (save_audio_config_p->adev[a].bits_per_sample == 8) {
              audio_put (a, 0);
              audio_put (a, ((sam+32768) >> 8) & 0xff);
	    }
	    else {
              audio_put (a, 0);
              audio_put (a, 0);

              audio_put (a, sam & 0xff);
              audio_put (a, (sam >> 8) & 0xff);
	    }
	  }
	}
}

void gen_tone_put_quiet_ms (int chan, int time_ms) {

	int a = ACHAN2ADEV(chan);	/* device for channel. */
	int sam = 0;

	int nsamples = (int) ((time_ms * (float)save_audio_config_p->adev[a].samples_per_sec / 1000.) + 0.5);

	for (int j=0; j<nsamples; j++)  {
	  gen_tone_put_sample (chan, a, sam);
        };

	// Avoid abrupt change when it starts up again.
	tone_phase[chan] = 0;
}


/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test program for above.
 *
 * Description: Compile like this for unit test:
 *
 *		gcc -Wall -DMAIN -o gen_tone_test gen_tone.c audio.c textcolor.c
 *
 *		gcc -Wall -DMAIN -o gen_tone_test.exe gen_tone.c audio_win.c textcolor.c -lwinmm
 *
 *--------------------------------------------------------------------*/


#if MAIN


int main ()
{
	int n;
	int chan1 = 0;
	int chan2 = 1;
	int r;
	struct audio_s my_audio_config;


/* to sound card */
/* one channel.  2 times:  one second of each tone. */

	memset (&my_audio_config, 0, sizeof(my_audio_config));
	strlcpy (my_audio_config.adev[0].adevice_in, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_in));
	strlcpy (my_audio_config.adev[0].adevice_out, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_out));

	audio_open (&my_audio_config);
	gen_tone_init (&my_audio_config, 100);

	for (r=0; r<2; r++) {

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 1 );
	  }

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 0 );
	  }
	}

	audio_close();

/* Now try stereo. */

	memset (&my_audio_config, 0, sizeof(my_audio_config));
	strlcpy (my_audio_config.adev[0].adevice_in, DEFAULT_ADEVICE, sizeof(my_audio_config.adev[0].adevice_in));
	strlcpy (my_audio_config.adev[0].adevice_out, DEFAULT_ADEVICE, , sizeof(my_audio_config.adev[0].adevice_out));
	my_audio_config.adev[0].num_channels = 2;

	audio_open (&my_audio_config);
	gen_tone_init (&my_audio_config, 100);

	for (r=0; r<4; r++) {

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 1 );
	  }

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 0 );
	  }

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan2, 1 );
	  }

	  for (n=0; n<my_audio_config.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan2, 0 );
	  }
	}

	audio_close();

	return(0);
}

#endif


/* end gen_tone.c */

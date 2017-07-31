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


//#define DEBUG1 1	/* display debugging info */

//#define DEBUG3 1	/* print carrier detect changes. */

//#define DEBUG4 1	/* capture PSK demodulator output to log files */



/*------------------------------------------------------------------
 *
 * Module:      demod_psk.c
 *
 * Purpose:   	Demodulator for Phase Shift Keying (PSK).
 *
 *		This is my initial attempt at implementing a 2400 bps mode.
 *		The MFJ-2400 & AEA PK232-2400 used V.26 / Bell 201 so I will follow that precedent.
 *
 *		
 * Input:	Audio samples from either a file or the "sound card."
 *
 * Outputs:	Calls hdlc_rec_bit() for each bit demodulated.
 *
 * Current Status:	New for Version 1.4.
 *
 *		Don't know if this is correct and/or compatible with
 *		other implementations.
 *		There is a lot of stuff going on here with phase
 *		shifting, gray code, bit order for the dibit, NRZI and
 *		bit-stuffing for HDLC.  Plenty of opportunity for
 *		misinterpreting a protocol spec or just stupid mistakes.
 *
 * References:	MFJ-2400 Product description and manual:
 *
 *			http://www.mfjenterprises.com/Product.php?productid=MFJ-2400
 *			http://www.mfjenterprises.com/Downloads/index.php?productid=MFJ-2400&filename=MFJ-2400.pdf&company=mfj
 *
 *		AEA had a 2400 bps packet modem, PK232-2400.
 *
 *			http://www.repeater-builder.com/aea/pk232/pk232-2400-baud-dpsk-modem.pdf
 *
 *		There was also a Kantronics KPC-2400 that had 2400 bps.
 *
 *			http://www.brazoriacountyares.org/winlink-collection/TNC%20manuals/Kantronics/2400_modem_operators_guide@rgf.pdf
 *
 *
 *		The MFJ and AEA both use the EXAR XR-2123 PSK modem chip.
 *		The Kantronics has a P423 ???
 *
 *		Can't find the chip specs on the EXAR website so Google it.
 *
 *			http://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/Linear/XR2123.pdf
 *
 *		The XR-2123 implements the V.26 / Bell 201 standard:
 *
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.26-198811-I!!PDF-E&type=items
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.26bis-198811-I!!PDF-E&type=items
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.26ter-198811-I!!PDF-E&type=items
 *
 *		"bis" and "ter" are from Latin for second and third.
 *		I used the "ter" version which has phase shifts of 0, 90, 180, and 270 degrees.
 *
 *		There are other references to an alternative B which uses other multiples of 45.
 *		The XR-2123 data sheet mentions only multiples of 90.  That's what I went with.
 *
 *		The XR-2123 does not perform the scrambling as specified in V.26 so I wonder if
 *		the vendors implemented it in software or just left it out.
 *		I left out scrambling for now.  Eventually, I'd like to get my hands on an old
 *		2400 bps TNC for compatibility testing.
 *
 *		After getting QPSK working, it was not much more effort to add V.27 with 8 phases.
 *
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.27bis-198811-I!!PDF-E&type=items
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.27ter-198811-I!!PDF-E&type=items
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>


#include "audio.h"
#include "tune.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "hdlc_rec.h"
#include "textcolor.h"
#include "demod_psk.h"
#include "dsp.h"


/* Add sample to buffer and shift the rest down. */

__attribute__((hot)) __attribute__((always_inline))
static inline void push_sample (float val, float *buff, int size)
{
	memmove(buff+1,buff,(size-1)*sizeof(float));
	buff[0] = val; 
}


/* FIR filter kernel. */

__attribute__((hot)) __attribute__((always_inline))
static inline float convolve (const float *__restrict__ data, const float *__restrict__ filter, int filter_size)
{
	float sum = 0.0;
	int j;

	for (j=0; j<filter_size; j++) {
	    sum += filter[j] * data[j];
	}

	return (sum);
}


/* Might replace this with faster, lower precision version someday. */

static inline float my_atan2f (float y, float x)
{
	if ( y == 0 && x == 0) return (0.0);		// different atan2 implementations behave differently.

	return (atan2f(y,x));
}


/*------------------------------------------------------------------
 *
 * Name:        demod_psk_init
 *
 * Purpose:     Initialization for an psk demodulator.
 *		Select appropriate parameters and set up filters.
 *
 * Inputs:   	modem_type	- MODEM_QPSK or MODEM_8PSK.
 *
 *		samples_per_sec	- Audio sample rate.
 *
 *		bps		- Bits per second.  
 *				  Should be 2400 for V.26 but we don't enforce it.
 *				  The carrier frequency will be proportional.
 *
 *		profile		- Select different variations.  For QPSK:
 *
 *					P - Using self-correlation technique.
 *					Q - Same preceded by bandpass filter.
 *					R - Using local oscillator to derive phase.
 *					S - Same with bandpass filter.
 *
 *				  For 8-PSK:
 *
 *					T, U, V, W  same as above.
 *	
 *		D		- Pointer to demodulator state for given channel.
 *
 * Outputs:	D->ms_filter_size
 *
 * Returns:     None.
 *		
 * Bugs:	This doesn't do much error checking so don't give it
 *		anything crazy.
 *
 *----------------------------------------------------------------*/

void demod_psk_init (enum modem_t modem_type, int samples_per_sec, int bps, char profile, struct demodulator_state_s *D)
{
	int correct_baud;	// baud is not same as bits/sec here!
	int carrier_freq;
	int j;


	memset (D, 0, sizeof(struct demodulator_state_s));

	D->modem_type = modem_type;
	D->num_slicers = 1;		// Haven't thought about this yet.  Is it even applicable?
	



#ifdef TUNE_PROFILE
	profile = TUNE_PROFILE;
#endif

	if (modem_type == MODEM_QPSK) {

	  correct_baud = bps / 2;
	  // Originally I thought of scaling it to the data rate,
	  // e.g. 2400 bps -> 1800 Hz, but decided to make it a
	  // constant since it is the same for V.26 and V.27.
	  carrier_freq = 1800;

#if DEBUG1
	  dw_printf ("demod_psk_init QPSK (sample rate=%d, bps=%d, baud=%d, carrier=%d, profile=%c\n",
		samples_per_sec, bps, correct_baud, carrier_freq, profile);
#endif

	  switch (toupper(profile)) {

	    case 'P':				/* Self correlation technique. */

	      D->use_prefilter = 0;		/* No bandpass filter. */

	      D->lpf_baud = 0.60;
	      D->lp_filter_len_bits = 39. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.95;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'Q':				/* Self correlation technique. */

	      D->use_prefilter = 1;		/* Add a bandpass filter. */
	      D->prefilter_baud = 1.3;		
	      D->pre_filter_len_bits = 55. * 1200. / 44100.;	
	      D->pre_window = BP_WINDOW_COSINE;

	      D->lpf_baud = 0.60;
	      D->lp_filter_len_bits = 39. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.87;
	      D->pll_searching_inertia = 0.50;

	      break;

	    default:
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Invalid demodulator profile %c for v.26 QPSK.  Valid choices are P, Q, R, S.  Using default.\n", profile);
	      // fall thru.

	    case 'R':				/* Mix with local oscillator. */

	      D->psk_use_lo = 1;

	      D->use_prefilter = 0;		/* No bandpass filter. */

	      D->lpf_baud = 0.70;
	      D->lp_filter_len_bits = 37. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_TRUNCATED;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'S':				/* Mix with local oscillator. */

	      D->psk_use_lo = 1;

	      D->use_prefilter = 1;		/* Add a bandpass filter. */
	      D->prefilter_baud = 0.55;		
	      D->pre_filter_len_bits = 74. * 1200. / 44100.;	
	      D->pre_window = BP_WINDOW_FLATTOP;

	      D->lpf_baud = 0.60;
	      D->lp_filter_len_bits = 39. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;
	  }

	  D->ms_filter_len_bits = 1.25;		// Delay line > 13/12 * symbol period		

	  D->coffs = (int) round( (11.f / 12.f) * (float)samples_per_sec / (float)correct_baud );
	  D->boffs = (int) round(               (float)samples_per_sec / (float)correct_baud );
	  D->soffs = (int) round( (13.f / 12.f) * (float)samples_per_sec / (float)correct_baud );
	}
	else {

	  correct_baud = bps / 3;
	  carrier_freq = 1800;

#if DEBUG1
	  dw_printf ("demod_psk_init 8-PSK (sample rate=%d, bps=%d, baud=%d, carrier=%d, profile=%c\n",
		samples_per_sec, bps, correct_baud, carrier_freq, profile);
#endif

	  switch (toupper(profile)) {


	    case 'T':				/* Self correlation technique. */

	      D->use_prefilter = 0;		/* No bandpass filter. */

	      D->lpf_baud = 1.15;
	      D->lp_filter_len_bits = 32. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.95;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'U':				/* Self correlation technique. */

	      D->use_prefilter = 1;		/* Add a bandpass filter. */
	      D->prefilter_baud = 0.9;		
	      D->pre_filter_len_bits = 21. * 1200. / 44100.;	
	      D->pre_window = BP_WINDOW_FLATTOP;

	      D->lpf_baud = 1.15;
	      D->lp_filter_len_bits = 32. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.87;
	      D->pll_searching_inertia = 0.50;

	      break;

	    default:
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Invalid demodulator profile %c for v.27 8PSK.  Valid choices are T, U, V, W.  Using default.\n", profile);
	      // fall thru.

	    case 'V':				/* Mix with local oscillator. */

	      D->psk_use_lo = 1;

	      D->use_prefilter = 0;		/* No bandpass filter. */

	      D->lpf_baud = 0.85;
	      D->lp_filter_len_bits = 31. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'W':				/* Mix with local oscillator. */

	      D->psk_use_lo = 1;

	      D->use_prefilter = 1;		/* Add a bandpass filter. */
	      D->prefilter_baud = 0.85;		
	      D->pre_filter_len_bits = 31. * 1200. / 44100.;	
	      D->pre_window = BP_WINDOW_COSINE;

	      D->lpf_baud = 0.85;
	      D->lp_filter_len_bits = 31. * 1200. / 44100.;
	      D->lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;
	  }

	  D->ms_filter_len_bits = 1.25;		// Delay line > 10/9 * symbol period		

	  D->coffs = (int) round( (8.f / 9.f)  * (float)samples_per_sec / (float)correct_baud );
	  D->boffs = (int) round(              (float)samples_per_sec / (float)correct_baud );
	  D->soffs = (int) round( (10.f / 9.f) * (float)samples_per_sec / (float)correct_baud );
	}


	if (D->psk_use_lo) {
	  D->lo_step = (int) round( 256. * 256. * 256. * 256. * carrier_freq / (double)samples_per_sec);

	  assert (MAX_FILTER_SIZE >= 256);
	  for (j = 0; j < 256; j++) {
	    D->m_sin_table[j] = sinf(2.f * (float)M_PI * j / 256.f);
	  }
	}

#ifdef TUNE_PRE_BAUD
	D->prefilter_baud = TUNE_PRE_BAUD;
#endif
#ifdef TUNE_PRE_WINDOW
	D->pre_window = TUNE_PRE_WINDOW;
#endif



#ifdef TUNE_LPF_BAUD
	D->lpf_baud = TUNE_LPF_BAUD;
#endif
#ifdef TUNE_LP_WINDOW
	D->lp_window = TUNE_LP_WINDOW;
#endif


#ifdef TUNE_HYST
	D->hysteresis = TUNE_HYST;
#endif

#if defined(TUNE_PLL_SEARCHING)
	D->pll_searching_inertia = TUNE_PLL_SEARCHING;
#endif
#if defined(TUNE_PLL_LOCKED)
	D->pll_locked_inertia = TUNE_PLL_LOCKED;
#endif


/*
 * Calculate constants used for timing.
 * The audio sample rate must be at least a few times the data rate.
 */

	D->pll_step_per_sample = (int) round((TICKS_PER_PLL_CYCLE * (double)correct_baud) / ((double)samples_per_sec));

/*
 * Convert number of symbol times to number of taps.
 */

	D->pre_filter_size = (int) round( D->pre_filter_len_bits * (float)samples_per_sec / (float)correct_baud );
	D->ms_filter_size  = (int) round( D->ms_filter_len_bits * (float)samples_per_sec / (float)correct_baud );
	D->lp_filter_size =  (int) round( D->lp_filter_len_bits * (float)samples_per_sec / (float)correct_baud );


#ifdef TUNE_PRE_FILTER_SIZE
	D->pre_filter_size = TUNE_PRE_FILTER_SIZE;
#endif

#ifdef TUNE_LP_FILTER_SIZE
	D->lp_filter_size = TUNE_LP_FILTER_SIZE;
#endif


	if (D->pre_filter_size > MAX_FILTER_SIZE) 
	{
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated filter size of %d is too large.\n", D->pre_filter_size);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

	if (D->ms_filter_size > MAX_FILTER_SIZE) 
	{
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated filter size of %d is too large.\n", D->ms_filter_size);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

	if (D->lp_filter_size > MAX_FILTER_SIZE) 
	{
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated filter size of %d is too large.\n", D->pre_filter_size);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

/* 
 * Optionally apply a bandpass ("pre") filter to attenuate
 * frequencies outside the range of interest.
 */

	if (D->use_prefilter) {
	  float f1, f2;

	  f1 = carrier_freq - D->prefilter_baud * correct_baud;
	  f2 = carrier_freq + D->prefilter_baud * correct_baud;
#if 0
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Generating prefilter %.0f to %.0f Hz.\n", (double)f1, (double)f2);
#endif
	  if (f1 <= 0) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("Prefilter of %.0f to %.0f Hz doesn't make sense.\n", (double)f1, (double)f2);
	    f1 = 10;
	  }

	  f1 = f1 / (float)samples_per_sec;
	  f2 = f2 / (float)samples_per_sec;
	  
	  gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, D->pre_window);
	}

/*
 * Now the lowpass filter.
 */

	float fc = correct_baud * D->lpf_baud / (float)samples_per_sec;
	gen_lowpass (fc, D->lp_filter, D->lp_filter_size, D->lp_window);

/*
 * No point in having multiple numbers for signal level.
 */

	D->alevel_mark_peak = -1;
	D->alevel_space_peak = -1;

}  /* demod_psk_init */




/*-------------------------------------------------------------------
 *
 * Name:        demod_psk_process_sample
 *
 * Purpose:     (1) Demodulate the psk signal into I & Q components.
 *		(2) Recover clock and sample data at the right time.
 *		(3) Produce two bits per symbol based on phase change from previous.
 *
 * Inputs:	chan	- Audio channel.  0 for left, 1 for right.
 *		subchan - modem of the channel.
 *		sam	- One sample of audio.
 *			  Should be in range of -32768 .. 32767.
 *
 * Outputs:	For each recovered data bit, we call:
 *
 *			  hdlc_rec (channel, demodulated_bit);
 *
 *		to decode HDLC frames from the stream of bits.
 *
 * Returns:	None 
 *
 * Descripion:	All the literature, that I could find, described mixing
 *		with a local oscillator.  First we multiply the input by
 *		cos and sin then low pass filter each.  This gives us
 *		correlation to the different phases.  The signs of these two
 *		results produces two data bits per symbol period.
 *
 *		An 1800 Hz local oscillator was derived from the 1200 Hz
 *		PLL used to sample the data.
 *		This worked wonderfully for the ideal condition where
 *		we start off with the proper phase and all the timing
 *		is perfect.  However, when random delays were added
 *		before the frame, the PLL would lock on only about 
 *		half the time.
 *
 *		Late one night, it dawned on me that there is no
 *		need for a local oscillator (LO) at the carrier frequency.
 *		Simply correlate the signal with the previous symbol,
 *		phase shifted by + and - 45 degrees.
 *		The code is much simpler and very reliable.
 *
 *		Later, I realized it was not necessary to synchronize the LO
 *		because we only care about the phase shift between symbols.
 *
 *		This works better under noisy conditions because we are
 *		including the noise from only the current symbol and not
 *		the previous one.
 *
 *		Finally, once we know how to distinguish 4 different phases,
 *		it is not much effort to use 8 phases to double the bit rate.
 *
 *--------------------------------------------------------------------*/



inline static void nudge_pll (int chan, int subchan, int slice, int demod_bits, struct demodulator_state_s *D);

__attribute__((hot))
void demod_psk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D)
{
	float fsam;
	float sam_x_cos, sam_x_sin;
	float I, Q;
	int demod_phase_shift;		// Phase shift relative to previous symbol.
					// range 0-3, 1 unit for each 90 degrees.
	int slice = 0;

#if DEBUG4
	static FILE *demod_log_fp = NULL;
	static int log_file_seq = 0;		/* Part of log file name */
#endif


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);


	/* Scale to nice number for plotting during debug. */

	fsam = sam / 16384.0f;


/*
 * Optional bandpass filter before the phase detector.
 */

	if (D->use_prefilter) {
	  push_sample (fsam, D->raw_cb, D->pre_filter_size);
	  fsam = convolve (D->raw_cb, D->pre_filter, D->pre_filter_size);
	}

	if (D->psk_use_lo) {
	  float a, delta;
	  int id;
/*
 * Mix with local oscillator to obtain phase.
 * The absolute phase doesn't matter.  
 * We are just concerned with the change since the previous symbol.
 */

	  sam_x_cos = fsam * D->m_sin_table[((D->lo_phase >> 24) + 64) & 0xff];

	  sam_x_sin = fsam * D->m_sin_table[(D->lo_phase >> 24) & 0xff];

	  push_sample (sam_x_cos, D->m_amp_cb, D->lp_filter_size);
	  I = convolve (D->m_amp_cb, D->lp_filter, D->lp_filter_size);

	  push_sample (sam_x_sin, D->s_amp_cb, D->lp_filter_size);
	  Q = convolve (D->s_amp_cb, D->lp_filter, D->lp_filter_size);

	  a = my_atan2f(I,Q);
	  push_sample (a, D->ms_in_cb, D->ms_filter_size);

	  delta = a - D->ms_in_cb[D->boffs];

	  /* 256 units/cycle makes modulo processing easier. */
	  /* Make sure it is positive before truncating to integer. */

	  id = ((int)((delta / (2.f * (float)M_PI) + 1.f) * 256.f)) & 0xff;
	  
	  if (D->modem_type == MODEM_QPSK) {
	    demod_phase_shift = ((id + 32) >> 6) & 0x3;
	  }
	  else {
	    demod_phase_shift = ((id + 16) >> 5) & 0x7;
	  }
	  nudge_pll (chan, subchan, slice, demod_phase_shift, D);

	  D->lo_phase += D->lo_step;
	}
	else {
/*
 * Correlate with previous symbol.  We are looking for the phase shift.
 */
	  push_sample (fsam, D->ms_in_cb, D->ms_filter_size);

	  sam_x_cos = fsam *  D->ms_in_cb[D->coffs];
	  sam_x_sin = fsam *  D->ms_in_cb[D->soffs];

	  push_sample (sam_x_cos, D->m_amp_cb, D->lp_filter_size);
	  I = convolve (D->m_amp_cb, D->lp_filter, D->lp_filter_size);

	  push_sample (sam_x_sin, D->s_amp_cb, D->lp_filter_size);
	  Q = convolve (D->s_amp_cb, D->lp_filter, D->lp_filter_size);

	  if (D->modem_type == MODEM_QPSK) {

#if 1				// Speed up special case.		
	    if (I > 0) {
	      if (Q > 0)
	        demod_phase_shift = 0;	/* 0 to 90 degrees, etc. */
	      else
	        demod_phase_shift = 1;
	    }
	    else {
	      if (Q > 0)
	        demod_phase_shift = 3;
	      else
	        demod_phase_shift = 2;
	    }
#else
	    a = my_atan2f(I,Q);
	    int id = ((int)((a / (2.f * (float)M_PI) + 1.f) * 256.f)) & 0xff;
	    // 128 compensates for 180 degree phase shift due
	    // to 1 1/2 carrier cycles per symbol period.
	    demod_phase_shift = ((id + 128) >> 6) & 0x3;
#endif
	  }
	  else {
	    float a;
	    int idelta;

	    a = my_atan2f(I,Q);
	    idelta = ((int)((a / (2.f * (float)M_PI) + 1.f) * 256.f)) & 0xff;
	    // 32 (90 degrees) compensates for 1800 carrier vs. 1800 baud.
	    // 16 is to set threshold between constellation points.
	    demod_phase_shift = ((idelta - 32 - 16) >> 5) & 0x7;
	  }

	  nudge_pll (chan, subchan, slice, demod_phase_shift, D);
	}

#if DEBUG4

	if (chan == 0) {

	  if (1) {
	  //if (hdlc_rec_gathering (chan, subchan, slice)) {
	    char fname[30];

	  
	    if (demod_log_fp == NULL) {
	      log_file_seq++;
	      snprintf (fname, sizeof(fname), "demod/%04d.csv", log_file_seq);
	      //if (log_file_seq == 1) mkdir ("demod", 0777);
	      if (log_file_seq == 1) mkdir ("demod");

	      demod_log_fp = fopen (fname, "w");
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Starting demodulator log file %s\n", fname);
	      fprintf (demod_log_fp, "Audio, sin,  cos,  *cos, *sin,   I,    Q, phase, Clock\n");
	    }

	    fprintf (demod_log_fp, "%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.2f, %.2f, %.2f\n", 
			fsam + 2,
			- D->ms_in_cb[D->soffs] + 6,
			- D->ms_in_cb[D->coffs] + 6,
			sam_x_cos + 8,
			sam_x_sin + 10, 
			2 * I + 12,
			2 * Q + 12,
			demod_phase_shift * 2. / 3. + 14.,
			(D->slicer[slice].data_clock_pll & 0x80000000) ? .5 : .0);

	    fflush (demod_log_fp);
	  }
	  else {
	    if (demod_log_fp != NULL) {
	      fclose (demod_log_fp);
	      demod_log_fp = NULL;
	    }
	  }
	}
#endif


} /* end demod_psk_process_sample */

static const int phase_to_gray_v26[4] = {0, 1, 3, 2};	
static const int phase_to_gray_v27[8] = {1, 0, 2, 3, 7, 6, 4, 5};	



__attribute__((hot))
inline static void nudge_pll (int chan, int subchan, int slice, int demod_bits, struct demodulator_state_s *D)
{

/*
 * Finally, a PLL is used to sample near the centers of the data bits.
 *
 * D points to a demodulator for a channel/subchannel pair so we don't
 * have to keep recalculating it.
 *
 * D->data_clock_pll is a SIGNED 32 bit variable.
 * When it overflows from a large positive value to a negative value, we 
 * sample a data bit from the demodulated signal.
 *
 * Ideally, the the demodulated signal transitions should be near
 * zero we we sample mid way between the transitions.
 *
 * Nudge the PLL by removing some small fraction from the value of 
 * data_clock_pll, pushing it closer to zero.
 * 
 * This adjustment will never change the sign so it won't cause
 * any erratic data bit sampling.
 *
 * If we adjust it too quickly, the clock will have too much jitter.
 * If we adjust it too slowly, it will take too long to lock on to a new signal.
 *
 * Be a little more agressive about adjusting the PLL
 * phase when searching for a signal.
 * Don't change it as much when locked on to a signal.
 *
 * I don't think the optimal value will depend on the audio sample rate
 * because this happens for each transition from the demodulator.
 */


	D->slicer[slice].prev_d_c_pll = D->slicer[slice].data_clock_pll;

	// Perform the add as unsigned to avoid signed overflow error.
	D->slicer[slice].data_clock_pll = (signed)((unsigned)(D->slicer[slice].data_clock_pll) + (unsigned)(D->pll_step_per_sample));

	if (D->slicer[slice].data_clock_pll < 0 && D->slicer[slice].prev_d_c_pll >= 0) {

	  /* Overflow of PLL counter. */
	  /* This is where we sample the data. */

	  if (D->modem_type == MODEM_QPSK) {

	    int gray = phase_to_gray_v26[ demod_bits ];

#if DEBUG4
	    text_color_set(DW_COLOR_DEBUG);

	    dw_printf ("a=%.2f deg, delta=%.2f deg, phaseshift=%d, bits= %d %d \n", 
		a * 360 / (2*M_PI), delta * 360 / (2*M_PI), demod_bits, (gray >> 1) & 1, gray & 1);

	    //dw_printf ("phaseshift=%d, bits= %d %d \n", demod_bits, (gray >> 1) & 1, gray & 1);
#endif
	    hdlc_rec_bit (chan, subchan, slice, (gray >> 1) & 1, 0, -1);
	    hdlc_rec_bit (chan, subchan, slice, gray & 1, 0, -1);
	  }
	  else {
	    int gray = phase_to_gray_v27[ demod_bits ];

	    hdlc_rec_bit (chan, subchan, slice, (gray >> 2) & 1, 0, -1);
	    hdlc_rec_bit (chan, subchan, slice, (gray >> 1) & 1, 0, -1);
	    hdlc_rec_bit (chan, subchan, slice, gray & 1, 0, -1);
	  }
	}

/*
 * If demodulated data has changed,
 * Pull the PLL phase closer to zero.
 * Use "floor" instead of simply casting so the sign won't flip.
 * For example if we had -0.7 we want to end up with -1 rather than 0.
 */

// TODO: demod_9600 has an improved technique.  Would it help us here?

        if (demod_bits != D->slicer[slice].prev_demod_data) {

	  if (hdlc_rec_gathering (chan, subchan, slice)) {
	    D->slicer[slice].data_clock_pll = (int)floorf((float)(D->slicer[slice].data_clock_pll) * D->pll_locked_inertia);
	  }
	  else {
	    D->slicer[slice].data_clock_pll = (int)floorf((float)(D->slicer[slice].data_clock_pll) * D->pll_searching_inertia);
	  }
	}

/*
 * Remember demodulator output so we can compare next time.
 */
	D->slicer[slice].prev_demod_data = demod_bits;

} /* end nudge_pll */



/* end demod_psk.c */

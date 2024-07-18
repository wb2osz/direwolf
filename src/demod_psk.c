//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016, 2019  John Langner, WB2OSZ
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


/*------------------------------------------------------------------
 *
 * Module:      demod_psk.c
 *
 * Purpose:   	Demodulator for 2400 and 4800 bits per second Phase Shift Keying (PSK).
 *
 * Input:	Audio samples from either a file or the "sound card."
 *
 * Outputs:	Calls hdlc_rec_bit() for each bit demodulated.
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
 *		From what I'm able to gather, they all used the EXAR XR-2123 PSK modem chip.
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
 *		There are earlier references to an alternative B which uses other phase shifts offset
 *		by another 45 degrees.
 *
 *		After getting QPSK working, it was not much more effort to add V.27 with 8 phases.
 *
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.27bis-198811-I!!PDF-E&type=items
 *			https://www.itu.int/rec/dologin_pub.asp?lang=e&id=T-REC-V.27ter-198811-I!!PDF-E&type=items
 *
 * Compatibility:
 *		V.26 has two variations, A and B.  Initially I implemented the A alternative.
 *		It later turned out that the MFJ-2400 used the B alternative.  In version 1.6 you have a
 *		choice between compatibility with MFJ (and probably the others) or the original implementation.
 *		The B alternative works a little more reliably, perhaps because there is never a
 *		zero phase difference between adjacent symbols.
 *		Eventually the A alternative might disappear to reduce confusion.
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

// Fine tuning for different demodulator types.

#define DCD_THRESH_ON 30		// Hysteresis: Can miss 2 out of 32 for detecting lock.
#define DCD_THRESH_OFF 6		// Might want a little more fine tuning.
#define DCD_GOOD_WIDTH 512
#include "fsk_demod_state.h"		// Values above override defaults.

#include "audio.h"
//#include "tune.h"	// obsolete. eventually remove all references.
#include "fsk_gen_filter.h"
#include "hdlc_rec.h"
#include "textcolor.h"
#include "demod_psk.h"
#include "dsp.h"


#define TUNE(envvar,param,name,fmt) { 				\
	char *e = getenv(envvar);				\
	if (e != NULL) {					\
	  param = atof(e);					\
	  text_color_set (DW_COLOR_ERROR);			\
	  dw_printf ("TUNE: " name " = " fmt "\n", param);	\
	} }


static const int phase_to_gray_v26[4] = {0, 1, 3, 2};	
static const int phase_to_gray_v27[8] = {1, 0, 2, 3, 7, 6, 4, 5};	


static int phase_shift_to_symbol (float phase_shift, int bits_per_symbol, int *bit_quality);

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

//Does pragma make any difference?  Annoying warning on Mac.
//#pragma GCC ivdep
	for (j=0; j<filter_size; j++) {
	    sum += filter[j] * data[j];
	}

	return (sum);
}


/* Might replace this with faster, lower precision, approximation someday if it does not harm results. */

static inline float my_atan2f (float y, float x)
{
	if ( y == 0 && x == 0) return (0.0);		// different atan2 implementations behave differently.

	return (atan2f(y,x));
}


/*------------------------------------------------------------------
 *
 * Name:        demod_psk_init
 *
 * Purpose:     Initialization for a PSK demodulator.
 *		Select appropriate parameters and set up filters.
 *
 * Inputs:   	modem_type	- MODEM_QPSK or MODEM_8PSK.
 *
 *		v26_alt		- V26_A (classic) or V25_B (MFJ compatible)
 *
 *		samples_per_sec	- Audio sample rate.
 *
 *		bps		- Bits per second.  
 *				  Should be 2400 for V.26 or 4800 for V.27.
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

void demod_psk_init (enum modem_t modem_type, enum v26_e v26_alt, int samples_per_sec, int bps, char profile, struct demodulator_state_s *D)
{
	int correct_baud;	// baud is not same as bits/sec here!
	int carrier_freq;
	int j;


	memset (D, 0, sizeof(struct demodulator_state_s));

	D->modem_type = modem_type;
	D->u.psk.v26_alt = v26_alt;

	D->num_slicers = 1;		// Haven't thought about this yet.  Is it even applicable?


//#ifdef TUNE_PROFILE
//	profile = TUNE_PROFILE;
//#endif
	TUNE("TUNE_PROFILE", profile, "profile", "%c")

	if (modem_type == MODEM_QPSK) {

	  assert (D->u.psk.v26_alt != V26_UNSPECIFIED);

	  correct_baud = bps / 2;
	  carrier_freq = 1800;

#if DEBUG1
	  dw_printf ("demod_psk_init QPSK (sample rate=%d, bps=%d, baud=%d, carrier=%d, profile=%c\n",
		samples_per_sec, bps, correct_baud, carrier_freq, profile);
#endif

	  switch (toupper(profile)) {

	    case 'P':				/* Self correlation technique. */

	      D->u.psk.use_prefilter = 0;		/* No bandpass filter. */

	      D->u.psk.lpf_baud = 0.60;
	      D->u.psk.lp_filter_width_sym = 1.061;  // 39. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.95;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'Q':				/* Self correlation technique. */

	      D->u.psk.use_prefilter = 1;		/* Add a bandpass filter. */
	      D->u.psk.prefilter_baud = 1.3;		
	      D->u.psk.pre_filter_width_sym = 1.497;  // 55. * 1200. / 44100.;	
	      D->u.psk.pre_window = BP_WINDOW_COSINE;

	      D->u.psk.lpf_baud = 0.60;
	      D->u.psk.lp_filter_width_sym = 1.061;  // 39. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.87;
	      D->pll_searching_inertia = 0.50;

	      break;

	    default:
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Invalid demodulator profile %c for v.26 QPSK.  Valid choices are P, Q, R, S.  Using default.\n", profile);
	      // fall thru.

	    case 'R':				/* Mix with local oscillator. */

	      D->u.psk.psk_use_lo = 1;

	      D->u.psk.use_prefilter = 0;		/* No bandpass filter. */

	      D->u.psk.lpf_baud = 0.70;
	      D->u.psk.lp_filter_width_sym = 1.007;  // 37. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_TRUNCATED;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'S':				/* Mix with local oscillator. */

	      D->u.psk.psk_use_lo = 1;

	      D->u.psk.use_prefilter = 1;		/* Add a bandpass filter. */
	      D->u.psk.prefilter_baud = 0.55;		
	      D->u.psk.pre_filter_width_sym = 2.014;  // 74. * 1200. / 44100.;	
	      D->u.psk.pre_window = BP_WINDOW_FLATTOP;

	      D->u.psk.lpf_baud = 0.60;
	      D->u.psk.lp_filter_width_sym = 1.061;  // 39. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;
	  }

	  D->u.psk.delay_line_width_sym = 1.25;		// Delay line > 13/12 * symbol period		

// JWL experiment 11-7.  Should delay be based on audio freq rather than baud?
#if 0   // experiment made things much worse.   55 went down to 21.
	  D->u.psk.coffs = (int) round( (11.f / 12.f) * (float)samples_per_sec / (float)carrier_freq );
	  D->u.psk.boffs = (int) round(                 (float)samples_per_sec / (float)carrier_freq );
	  D->u.psk.soffs = (int) round( (13.f / 12.f) * (float)samples_per_sec / (float)carrier_freq );
#else
	  D->u.psk.coffs = (int) round( (11.f / 12.f) * (float)samples_per_sec / (float)correct_baud );
	  D->u.psk.boffs = (int) round(                 (float)samples_per_sec / (float)correct_baud );
	  D->u.psk.soffs = (int) round( (13.f / 12.f) * (float)samples_per_sec / (float)correct_baud );
#endif
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

	      D->u.psk.use_prefilter = 0;		/* No bandpass filter. */

	      D->u.psk.lpf_baud = 1.15;
	      D->u.psk.lp_filter_width_sym = 0.871;  // 32. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.95;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'U':				/* Self correlation technique. */

	      D->u.psk.use_prefilter = 1;		/* Add a bandpass filter. */
	      D->u.psk.prefilter_baud = 0.9;		
	      D->u.psk.pre_filter_width_sym = 0.571;  // 21. * 1200. / 44100.;	
	      D->u.psk.pre_window = BP_WINDOW_FLATTOP;

	      D->u.psk.lpf_baud = 1.15;
	      D->u.psk.lp_filter_width_sym = 0.871;  // 32. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.87;
	      D->pll_searching_inertia = 0.50;

	      break;

	    default:
	      text_color_set (DW_COLOR_ERROR);
	      dw_printf ("Invalid demodulator profile %c for v.27 8PSK.  Valid choices are T, U, V, W.  Using default.\n", profile);
	      // fall thru.

	    case 'V':				/* Mix with local oscillator. */

	      D->u.psk.psk_use_lo = 1;

	      D->u.psk.use_prefilter = 0;		/* No bandpass filter. */

	      D->u.psk.lpf_baud = 0.85;
	      D->u.psk.lp_filter_width_sym = 0.844;  // 31. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;

	    case 'W':				/* Mix with local oscillator. */

	      D->u.psk.psk_use_lo = 1;

	      D->u.psk.use_prefilter = 1;		/* Add a bandpass filter. */
	      D->u.psk.prefilter_baud = 0.85;		
	      D->u.psk.pre_filter_width_sym = 0.844;  // 31. * 1200. / 44100.;	
	      D->u.psk.pre_window = BP_WINDOW_COSINE;

	      D->u.psk.lpf_baud = 0.85;
	      D->u.psk.lp_filter_width_sym = 0.844;  // 31. * 1200. / 44100.;
	      D->u.psk.lp_window = BP_WINDOW_COSINE;

	      D->pll_locked_inertia = 0.925;
	      D->pll_searching_inertia = 0.50;

	      break;
	  }

	  D->u.psk.delay_line_width_sym = 1.25;		// Delay line > 10/9 * symbol period		

	  D->u.psk.coffs = (int) round( (8.f / 9.f)  * (float)samples_per_sec / (float)correct_baud );
	  D->u.psk.boffs = (int) round(              (float)samples_per_sec / (float)correct_baud );
	  D->u.psk.soffs = (int) round( (10.f / 9.f) * (float)samples_per_sec / (float)correct_baud );
	}


	if (D->u.psk.psk_use_lo) {
	  D->u.psk.lo_step = (int) round( 256. * 256. * 256. * 256. * carrier_freq / (double)samples_per_sec);

// Our own sin table for speed later.

	  for (j = 0; j < 256; j++) {
	    D->u.psk.sin_table256[j] = sinf(2.f * (float)M_PI * j / 256.f);
	  }
	}

//#ifdef TUNE_PRE_BAUD
//	D->u.psk.prefilter_baud = TUNE_PRE_BAUD;
//#endif
	TUNE("TUNE_PRE_BAUD", D->u.psk.prefilter_baud, "prefilter_baud", "%.3f")

//#ifdef TUNE_PRE_WINDOW
//	D->u.psk.pre_window = TUNE_PRE_WINDOW;
//#endif
	TUNE("TUNE_PRE_WINDOW", D->u.psk.pre_window, "pre_window", "%d")

//#ifdef TUNE_LPF_BAUD
//	D->u.psk.lpf_baud = TUNE_LPF_BAUD;
//#endif
//#ifdef TUNE_LP_WINDOW
//	D->u.psk.lp_window = TUNE_LP_WINDOW;
//#endif
	TUNE("TUNE_LPF_BAUD", D->u.psk.lpf_baud, "lpf_baud", "%.3f")
	TUNE("TUNE_LP_WINDOW", D->u.psk.lp_window, "lp_window", "%d")


	TUNE("TUNE_LP_FILTER_WIDTH_SYM", D->u.psk.lp_filter_width_sym, "lp_filter_width_sym", "%.3f")





//#if defined(TUNE_PLL_SEARCHING)
//	D->pll_searching_inertia = TUNE_PLL_SEARCHING;
//#endif
//#if defined(TUNE_PLL_LOCKED)
//	D->pll_locked_inertia = TUNE_PLL_LOCKED;
//#endif
	TUNE("TUNE_PLL_LOCKED", D->pll_locked_inertia, "pll_locked_inertia", "%.2f")
	TUNE("TUNE_PLL_SEARCHING", D->pll_searching_inertia, "pll_searching_inertia", "%.2f")


/*
 * Calculate constants used for timing.
 * The audio sample rate must be at least a few times the data rate.
 */

	D->pll_step_per_sample = (int) round((TICKS_PER_PLL_CYCLE * (double)correct_baud) / ((double)samples_per_sec));

/*
 * Convert number of symbol times to number of taps.
 */

	D->u.psk.pre_filter_taps = (int) round( D->u.psk.pre_filter_width_sym * (float)samples_per_sec / (float)correct_baud );

// JWL experiment 11/7 - Should delay line be based on audio frequency?
	D->u.psk.delay_line_taps  = (int) round( D->u.psk.delay_line_width_sym * (float)samples_per_sec / (float)correct_baud );
	D->u.psk.delay_line_taps  = (int) round( D->u.psk.delay_line_width_sym * (float)samples_per_sec / (float)correct_baud );


	D->u.psk.lp_filter_taps =  (int) round( D->u.psk.lp_filter_width_sym * (float)samples_per_sec / (float)correct_baud );


//#ifdef TUNE_PRE_FILTER_TAPS
//	D->u.psk.pre_filter_taps = TUNE_PRE_FILTER_TAPS;
//#endif
	  TUNE("TUNE_PRE_FILTER_TAPS", D->u.psk.pre_filter_taps, "pre_filter_taps", "%d")

//#ifdef TUNE_lp_filter_taps
//	D->u.psk.lp_filter_taps = TUNE_lp_filter_taps;
//#endif
	  TUNE("TUNE_LP_FILTER_TAPS", D->u.psk.lp_filter_taps, "lp_filter_taps (FIR)", "%d")


	if (D->u.psk.pre_filter_taps > MAX_FILTER_SIZE) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated pre filter size of %d is too large.\n", D->u.psk.pre_filter_taps);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

	if (D->u.psk.delay_line_taps > MAX_FILTER_SIZE) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated delay line size of %d is too large.\n", D->u.psk.delay_line_taps);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

	if (D->u.psk.lp_filter_taps > MAX_FILTER_SIZE) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated low pass filter size of %d is too large.\n", D->u.psk.lp_filter_taps);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}

/* 
 * Optionally apply a bandpass ("pre") filter to attenuate
 * frequencies outside the range of interest.
 * It's a tradeoff.  Attenuate frequencies outside the the range of interest
 * but also distort the signal.  This demodulator is not compuationally
 * intensive so we can usually run both in parallel.
 */

	if (D->u.psk.use_prefilter) {
	  float f1, f2;

	  f1 = carrier_freq - D->u.psk.prefilter_baud * correct_baud;
	  f2 = carrier_freq + D->u.psk.prefilter_baud * correct_baud;
#if DEBUG1
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
	  
	  gen_bandpass (f1, f2, D->u.psk.pre_filter, D->u.psk.pre_filter_taps, D->u.psk.pre_window);
	}

/*
 * Now the lowpass filter.
 */

	float fc = correct_baud * D->u.psk.lpf_baud / (float)samples_per_sec;
	gen_lowpass (fc, D->u.psk.lp_filter, D->u.psk.lp_filter_taps, D->u.psk.lp_window);

/*
 * No point in having multiple numbers for signal level.
 */

	D->alevel_mark_peak = -1;
	D->alevel_space_peak = -1;

#if 0
	// QPSK - CSV format to make plot.

	printf ("Phase shift degrees, bit 0, quality 0, bit 1, quality 1\n");
	for (int degrees = 0; degrees <= 360; degrees++) {
	  float a = degrees * M_PI * 2./ 360.;
	  int bit_quality[3];

	  int new_gray = phase_shift_to_symbol (a, 2, bit_quality);

	  float offset = 3 * 1.5;
	  printf ("%d, ", degrees);
	  printf ("%.3f, ", offset + (new_gray & 1)); offset -= 1.5;
	  printf ("%.3f, ", offset + (bit_quality[0] / 100.)); offset -= 1.5;
	  printf ("%.3f, ", offset + ((new_gray >> 1) & 1)); offset -= 1.5;
	  printf ("%.3f\n", offset + (bit_quality[1] / 100.));
	}
#endif

#if 0
	// 8-PSK - CSV format to make plot.

	printf ("Phase shift degrees,  bit 0, quality 0, bit 1, quality 1, bit 2, quality 2\n");
	for (int degrees = 0; degrees <= 360; degrees++) {
	  float a = degrees * M_PI * 2./ 360.;
	  int bit_quality[3];

	  int new_gray = phase_shift_to_symbol (a, 3, bit_quality);

	  float offset = 5 * 1.5;
	  printf ("%d, ", degrees);
	  printf ("%.3f, ", offset + (new_gray & 1)); offset -= 1.5;
	  printf ("%.3f, ", offset + (bit_quality[0] / 100.)); offset -= 1.5;
	  printf ("%.3f, ", offset + ((new_gray >> 1) & 1)); offset -= 1.5;
	  printf ("%.3f, ", offset + (bit_quality[1] / 100.)); offset -= 1.5;
	  printf ("%.3f, ", offset + ((new_gray >> 2) & 1)); offset -= 1.5;
	  printf ("%.3f\n", offset + (bit_quality[2] / 100.));
	}
#endif	
}  /* demod_psk_init */



/*-------------------------------------------------------------------
 *
 * Name:        phase_shift_to_symbol
 *
 * Purpose:     Translate phase shift, between two symbols, into 2 or 3 bits.
 *
 * Inputs:	phase_shift	- in radians.
 *
 *		bits_per_symbol	- 2 for QPSK, 3 for 8PSK.
 *
 * Outputs:	bit_quality[]	- Value of 0 (at threshold) to 100 (perfect) for each bit.
 *
 * Returns:	2 or 3 bit symbol value in Gray code.
 *
 *--------------------------------------------------------------------*/

__attribute__((hot)) __attribute__((always_inline))
static inline int phase_shift_to_symbol (float phase_shift, int bits_per_symbol, int * __restrict__ bit_quality)
{
// Number of different symbol states.
	assert (bits_per_symbol == 2 || bits_per_symbol == 3);
	int N = 1 << bits_per_symbol;
	assert (N == 4 || N == 8);

// Scale angle to 1 per symbol then separate into integer and fractional parts.
	float a = phase_shift * (float)N / (M_PI * 2.0f);
	while (a >= (float)N) a -= (float)N;
	while (a < 0.) a += (float)N;
	int i = (int)a;
	if (i == N) i = N-1;	// Should be < N. Watch out for possible roundoff errors.
	float f = a - (float)i;
	assert (i >= 0 && i < N);
	assert (f >= -0.001f && f <= 1.001f);

// Interpolate between the ideal angles to get a level of certainty.
	int result = 0;
	for (int b = 0; b < bits_per_symbol; b++) {
	  float demod = bits_per_symbol == 2 ?
		((phase_to_gray_v26[i] >> b) & 1) * (1.0f - f) + ((phase_to_gray_v26[(i+1)&3] >> b) & 1) * f :
		((phase_to_gray_v27[i] >> b) & 1) * (1.0f - f) + ((phase_to_gray_v27[(i+1)&7] >> b) & 1) * f;
// Slice to get boolean value and quality measurement.
	  if (demod >= 0.5f) result |= 1<<b;
	  bit_quality[b] = lrintf (100.0f * 2.0f * fabsf(demod - 0.5f));
	}
	return (result);

}  // end phase_shift_to_symbol



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
 *			  hdlc_rec (channel etc., demodulated_bit, quality);
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



inline static void nudge_pll (int chan, int subchan, int slice, int demod_bits, struct demodulator_state_s *D, int *bit_quality);

__attribute__((hot))
void demod_psk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D)
{
	int slice = 0;		// Would it make sense to have more than one?

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	/* Scale to nice number for plotting during debug. */

	float fsam = sam / 16384.0f;

/*
 * Optional bandpass filter before the phase detector.
 */

	if (D->u.psk.use_prefilter) {
	  push_sample (fsam, D->u.psk.audio_in, D->u.psk.pre_filter_taps);
	  fsam = convolve (D->u.psk.audio_in, D->u.psk.pre_filter, D->u.psk.pre_filter_taps);
	}

	if (D->u.psk.psk_use_lo) {
/*
 * Mix with local oscillator to obtain phase.
 * The absolute phase doesn't matter.  
 * We are just concerned with the change since the previous symbol.
 */

	  float sam_x_cos = fsam * D->u.psk.sin_table256[((D->u.psk.lo_phase >> 24) + 64) & 0xff];
	  push_sample (sam_x_cos, D->u.psk.I_raw, D->u.psk.lp_filter_taps);
	  float I = convolve (D->u.psk.I_raw, D->u.psk.lp_filter, D->u.psk.lp_filter_taps);

	  float sam_x_sin = fsam * D->u.psk.sin_table256[(D->u.psk.lo_phase >> 24) & 0xff];
	  push_sample (sam_x_sin, D->u.psk.Q_raw, D->u.psk.lp_filter_taps);
	  float Q = convolve (D->u.psk.Q_raw, D->u.psk.lp_filter, D->u.psk.lp_filter_taps);

	  float a = my_atan2f(I,Q);

	  // This is just a delay line of one symbol time.

	  push_sample (a, D->u.psk.delay_line, D->u.psk.delay_line_taps);
	  float delta = a - D->u.psk.delay_line[D->u.psk.boffs];

	  int gray;
	  int bit_quality[3];
	  if (D->modem_type == MODEM_QPSK) {
	    if (D->u.psk.v26_alt == V26_B) {
	      gray = phase_shift_to_symbol (delta + (float)(-M_PI/4), 2, bit_quality);;	// MFJ compatible
	    }
	    else {
	      gray = phase_shift_to_symbol (delta, 2, bit_quality);	// Classic
	    }
	  }
	  else {
	      gray = phase_shift_to_symbol (delta, 3, bit_quality);;	// 8-PSK
	  }
	  nudge_pll (chan, subchan, slice, gray, D, bit_quality);

	  D->u.psk.lo_phase += D->u.psk.lo_step;
	}
	else {
/*
 * Correlate with previous symbol.  We are looking for the phase shift.
 */
	  push_sample (fsam, D->u.psk.delay_line, D->u.psk.delay_line_taps);

	  float sam_x_cos = fsam *  D->u.psk.delay_line[D->u.psk.coffs];
	  push_sample (sam_x_cos, D->u.psk.I_raw, D->u.psk.lp_filter_taps);
	  float I = convolve (D->u.psk.I_raw, D->u.psk.lp_filter, D->u.psk.lp_filter_taps);

	  float sam_x_sin = fsam *  D->u.psk.delay_line[D->u.psk.soffs];
	  push_sample (sam_x_sin, D->u.psk.Q_raw, D->u.psk.lp_filter_taps);
	  float Q = convolve (D->u.psk.Q_raw, D->u.psk.lp_filter, D->u.psk.lp_filter_taps);

	  int gray;
	  int bit_quality[3];
	  float delta = my_atan2f(I,Q);

	  if (D->modem_type == MODEM_QPSK) {
	    if (D->u.psk.v26_alt == V26_B) {
	      gray = phase_shift_to_symbol (delta + (float)(M_PI/2), 2, bit_quality);	// MFJ compatible
	    }
	    else {
	      gray = phase_shift_to_symbol (delta + (float)(3*M_PI/4), 2, bit_quality);	// Classic
	    }
	  }
	  else {
	    gray = phase_shift_to_symbol (delta + (float)(3*M_PI/2), 3, bit_quality);
	  }
	  nudge_pll (chan, subchan, slice, gray, D, bit_quality);
	}

} /* end demod_psk_process_sample */



__attribute__((hot))
static void nudge_pll (int chan, int subchan, int slice, int demod_bits, struct demodulator_state_s *D, int *bit_quality)
{

/*
 * Finally, a PLL is used to sample near the centers of the data bits.
 *
 * D points to a demodulator for a channel/subchannel pair.
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
 * Be a little more aggressive about adjusting the PLL
 * phase when searching for a signal.
 * Don't change it as much when locked on to a signal.
 */

	D->slicer[slice].prev_d_c_pll = D->slicer[slice].data_clock_pll;

	// Perform the add as unsigned to avoid signed overflow error.
	D->slicer[slice].data_clock_pll = (signed)((unsigned)(D->slicer[slice].data_clock_pll) + (unsigned)(D->pll_step_per_sample));

	if (D->slicer[slice].data_clock_pll < 0 && D->slicer[slice].prev_d_c_pll >= 0) {

	  /* Overflow of PLL counter. */
	  /* This is where we sample the data. */

	  if (D->modem_type == MODEM_QPSK) {

	    int gray = demod_bits;

	    hdlc_rec_bit_new (chan, subchan, slice, (gray >> 1) & 1, 0, bit_quality[1],
			&(D->slicer[slice].pll_nudge_total), &(D->slicer[slice].pll_symbol_count));
	    hdlc_rec_bit_new (chan, subchan, slice, gray & 1, 0, bit_quality[0],
			&(D->slicer[slice].pll_nudge_total), &(D->slicer[slice].pll_symbol_count));
	  }
	  else {
	    int gray = demod_bits;

	    hdlc_rec_bit_new (chan, subchan, slice, (gray >> 2) & 1, 0, bit_quality[2],
			&(D->slicer[slice].pll_nudge_total), &(D->slicer[slice].pll_symbol_count));
	    hdlc_rec_bit_new (chan, subchan, slice, (gray >> 1) & 1, 0, bit_quality[1],
			&(D->slicer[slice].pll_nudge_total), &(D->slicer[slice].pll_symbol_count));
	    hdlc_rec_bit_new (chan, subchan, slice, gray & 1, 0, bit_quality[0],
			&(D->slicer[slice].pll_nudge_total), &(D->slicer[slice].pll_symbol_count));
	  }
	  D->slicer[slice].pll_symbol_count++;
	  pll_dcd_each_symbol2 (D, chan, subchan, slice);
	}

/*
 * If demodulated data has changed,
 * Pull the PLL phase closer to zero.
 * Use "floor" instead of simply casting so the sign won't flip.
 * For example if we had -0.7 we want to end up with -1 rather than 0.
 */

// TODO: demod_9600 has an improved technique.  Would it help us here?

        if (demod_bits != D->slicer[slice].prev_demod_data) {

	  pll_dcd_signal_transition2 (D, slice, D->slicer[slice].data_clock_pll);

	  signed int before = (signed int)(D->slicer[slice].data_clock_pll);	// Treat as signed.
	  if (D->slicer[slice].data_detect) {
	    D->slicer[slice].data_clock_pll = (int)floorf((float)(D->slicer[slice].data_clock_pll) * D->pll_locked_inertia);
	  }
	  else {
	    D->slicer[slice].data_clock_pll = (int)floorf((float)(D->slicer[slice].data_clock_pll) * D->pll_searching_inertia);
	  }
	  D->slicer[slice].pll_nudge_total += (int64_t)((signed int)(D->slicer[slice].data_clock_pll)) - (int64_t)before;
	}

/*
 * Remember demodulator output so we can compare next time.
 */
	D->slicer[slice].prev_demod_data = demod_bits;

} /* end nudge_pll */



/* end demod_psk.c */

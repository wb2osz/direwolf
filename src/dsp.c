//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2015, 2019  John Langner, WB2OSZ
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
 * Name:        dsp.c
 *
 * Purpose:     Generate the filters used by the demodulators.
 *
 *----------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "audio.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "textcolor.h"
#include "dsp.h"



#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))


// Don't remove this.  It serves as a reminder that an experiment is underway.

#if defined(TUNE_MS_FILTER_SIZE) || defined(TUNE_MS2_FILTER_SIZE) || defined(TUNE_AGC_FAST) || defined(TUNE_LPF_BAUD) || defined(TUNE_PLL_LOCKED) || defined(TUNE_PROFILE)
#define DEBUG1 1		// Don't remove this.
#endif


/*------------------------------------------------------------------
 *
 * Name:        window
 *
 * Purpose:     Filter window shape functions.
 *
 * Inputs:   	type	- BP_WINDOW_HAMMING, etc.
 *		size	- Number of filter taps.
 *		j	- Index in range of 0 to size-1.
 *
 * Returns:     Multiplier for the window shape.
 *		
 *----------------------------------------------------------------*/

float window (bp_window_t type, int size, int j)
{
	float center;
	float w;

	center = 0.5 * (size - 1);

	switch (type) {

	  case BP_WINDOW_COSINE:
	    w = cos((j - center) / size * M_PI);
	    //w = sin(j * M_PI / (size - 1));
	    break;

	  case BP_WINDOW_HAMMING:
	    w = 0.53836 - 0.46164 * cos((j * 2 * M_PI) / (size - 1));
	    break;

	  case BP_WINDOW_BLACKMAN:
	    w =  0.42659 - 0.49656 * cos((j * 2 * M_PI) / (size - 1)) 
			 + 0.076849 * cos((j * 4 * M_PI) / (size - 1));
	    break;

	  case BP_WINDOW_FLATTOP:
	    w =  1.0    - 1.93  * cos((j * 2 * M_PI) / (size - 1)) 
			+ 1.29  * cos((j * 4 * M_PI) / (size - 1))
			- 0.388 * cos((j * 6 * M_PI) / (size - 1))
			+ 0.028 * cos((j * 8 * M_PI) / (size - 1));
	    break;

	  case BP_WINDOW_TRUNCATED:
	    default:
	    w = 1.0;
	    break;
	}
	return (w);
}


/*------------------------------------------------------------------
 *
 * Name:        gen_lowpass
 *
 * Purpose:     Generate low pass filter kernel.
 *
 * Inputs:   	fc		- Cutoff frequency as fraction of sampling frequency.
 *		filter_size	- Number of filter taps.
 *		wtype		- Window type, BP_WINDOW_HAMMING, etc.
 *		lp_delay_fract	- Fudge factor for the delay value.
 *
 * Outputs:     lp_filter
 *		
 * Returns:	Signal delay thru the filter in number of audio samples.
 *
 *----------------------------------------------------------------*/

 
void gen_lowpass (float fc, float *lp_filter, int filter_size, bp_window_t wtype)
{
	int j;
	float G;


#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("Lowpass, size=%d, fc=%.2f\n", filter_size, fc);
	dw_printf ("   j     shape   sinc   final\n");
#endif

	assert (filter_size >= 3 && filter_size <= MAX_FILTER_SIZE);

        for (j=0; j<filter_size; j++) {
	  float center;
	  float sinc;
	  float shape;

	  center = 0.5 * (filter_size - 1);

	  if (j - center == 0) {
	    sinc = 2 * fc;
	  }
	  else {
	    sinc = sin(2 * M_PI * fc * (j-center)) / (M_PI*(j-center));
	  }

	  shape = window (wtype, filter_size, j);
	  lp_filter[j] = sinc * shape;

#if DEBUG1
	  dw_printf ("%6d  %6.2f  %6.3f  %6.3f\n", j, shape, sinc, lp_filter[j] ) ;
#endif
        }

/*
 * Normalize lowpass for unity gain at DC.
 */
	G = 0;
        for (j=0; j<filter_size; j++) {
	  G += lp_filter[j];
	}
        for (j=0; j<filter_size; j++) {
	  lp_filter[j] = lp_filter[j] / G;
	}

	return;

}  /* end gen_lowpass */


#undef DEBUG1



/*------------------------------------------------------------------
 *
 * Name:        gen_bandpass
 *
 * Purpose:     Generate band pass filter kernel for the prefilter.
 *		This is NOT for the mark/space filters.
 *
 * Inputs:   	f1		- Lower cutoff frequency as fraction of sampling frequency.
 *		f2		- Upper cutoff frequency...
 *		filter_size	- Number of filter taps.
 *		wtype		- Window type, BP_WINDOW_HAMMING, etc.
 *
 * Outputs:     bp_filter
 *
 * Reference:	http://www.labbookpages.co.uk/audio/firWindowing.html
 *
 *		Does it need to be an odd length?
 *
 *----------------------------------------------------------------*/


void gen_bandpass (float f1, float f2, float *bp_filter, int filter_size, bp_window_t wtype)
{
	int j;
	float w;
	float G;
	float center = 0.5 * (filter_size - 1);

#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("Bandpass, size=%d\n", filter_size);
	dw_printf ("   j     shape   sinc   final\n");
#endif

	assert (filter_size >= 3 && filter_size <= MAX_FILTER_SIZE);

        for (j=0; j<filter_size; j++) {
	  float sinc;
	  float shape;

	  if (j - center == 0) {
	    sinc = 2 * (f2 - f1);
	  }
	  else {
	    sinc = sin(2 * M_PI * f2 * (j-center)) / (M_PI*(j-center))
		 - sin(2 * M_PI * f1 * (j-center)) / (M_PI*(j-center));
	  }

	  shape = window (wtype, filter_size, j);
	  bp_filter[j] = sinc * shape;

#if DEBUG1
	  dw_printf ("%6d  %6.2f  %6.3f  %6.3f\n", j, shape, sinc, bp_filter[j] ) ;
#endif
	}


/*
 * Normalize bandpass for unity gain in middle of passband.
 * Can't use same technique as for lowpass.
 * Instead compute gain in middle of passband.
 * See http://dsp.stackexchange.com/questions/4693/fir-filter-gain
 */
	w = 2 * M_PI * (f1 + f2) / 2;
	G = 0;
        for (j=0; j<filter_size; j++) {
	  G += 2 * bp_filter[j] * cos((j-center)*w);  // is this correct?
	}

#if DEBUG1
	dw_printf ("Before normalizing, G=%.3f\n", G);
#endif
        for (j=0; j<filter_size; j++) {
	  bp_filter[j] = bp_filter[j] / G;
	}

} /* end gen_bandpass */



/*------------------------------------------------------------------
 *
 * Name:        gen_ms
 *
 * Purpose:     Generate mark and space filters.
 *
 * Inputs:   	fc		- Tone frequency, i.e. mark or space.
 *		sps		- Samples per second.
 *		filter_size	- Number of filter taps.
 *		wtype		- Window type, BP_WINDOW_HAMMING, etc.
 *
 * Outputs:     bp_filter
 *
 * Reference:	http://www.labbookpages.co.uk/audio/firWindowing.html
 *
 *		Does it need to be an odd length?
 *
 *----------------------------------------------------------------*/


void gen_ms (int fc, int sps, float *sin_table, float *cos_table, int filter_size, int wtype)
{
	int j;
	float Gs = 0, Gc = 0;;

        for (j=0; j<filter_size; j++) {

	  float center = 0.5f * (filter_size - 1);
	  float am = ((float)(j - center) / (float)sps) * ((float)fc) * (2.0f * (float)M_PI);

	  float shape = window (wtype, filter_size, j);

	  sin_table[j] = sinf(am) * shape;
	  cos_table[j] = cosf(am) * shape;

	  Gs += sin_table[j] * sinf(am);
	  Gc += cos_table[j] * cosf(am);

#if DEBUG1
	  dw_printf ("%6d  %6.2f  %6.2f  %6.2f\n", j, shape, sin_table[j], cos_table[j]) ;
#endif
        }


/* Normalize for unity gain */

#if DEBUG1
	dw_printf ("Before normalizing, Gs = %.2f, Gc = %.2f\n", Gs, Gc) ;
#endif
        for (j=0; j<filter_size; j++) {
	  sin_table[j] = sin_table[j] / Gs;
	  cos_table[j] = cos_table[j] / Gc;
	}

} /* end gen_ms */





/*------------------------------------------------------------------
 *
 * Name:        rrc
 *
 * Purpose:     Root Raised Cosine function.
 *		Why do they call it that?
 *		It's mostly the sinc function with cos windowing to taper off edges faster.
 *
 * Inputs:      t		- Time in units of symbol duration.
 *				  i.e. The centers of two adjacent symbols would differ by 1.
 *
 *		a		- Roll off factor, between 0 and 1.
 *
 * Returns:	Basically the sinc  (sin(x)/x) function with edges decreasing faster.
 *		Should be 1 for t = 0 and 0 at all other integer values of t.
 *		
 *----------------------------------------------------------------*/

__attribute__((const))
float rrc (float t, float a)
{
	float sinc, window, result;

	if (t > -0.001 && t < 0.001) {
	  sinc = 1;
	}
	else {
	  sinc = sinf(M_PI * t) / (M_PI * t);
	}

	if (fabsf(a * t) > 0.499 && fabsf(a * t) < 0.501) {
	  window = M_PI / 4;
	}
	else {
	  window = cos(M_PI * a * t) / ( 1 - powf(2 * a * t, 2));
	  // This made nicer looking waveforms for generating signal.
	  //window = cos(M_PI * a * t);
	  // Do we want to let it go negative?
	  // I think this would happen when a > 0.5 / (filter width in symbol times)
	  if (window < 0) {
	    //printf ("'a' is too large for range of 't'.\n");
	    //window = 0;
	  }
	}

	result = sinc * window;

#if DEBUGRRC
	// t should vary from - to + half of filter size in symbols.
	// Result should be 1 at t=0 and 0 at all other integer values of t.

	printf ("%.3f, %.3f, %.3f, %.3f\n", t, sinc, window, result);
#endif
	return (result);
}

// The Root Raised Cosine (RRC) low pass filter is suppposed to minimize Intersymbol Interference (ISI).

void gen_rrc_lowpass (float *pfilter, int filter_taps, float rolloff, float samples_per_symbol)
{
	int k;
	float t;

	for (k = 0; k < filter_taps; k++) {
	  t = (k - ((filter_taps - 1.0) / 2.0)) / samples_per_symbol;
	  pfilter[k] = rrc (t, rolloff);
	}

	// Scale it for unity gain.

	t = 0;
	for (k = 0; k < filter_taps; k++) {
	  t += pfilter[k];
	}
	for (k = 0; k < filter_taps; k++) {
	  pfilter[k] = pfilter[k] / t;
	}
}

/* end dsp.c */

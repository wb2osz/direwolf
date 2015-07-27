//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2015  John Langner, WB2OSZ
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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "direwolf.h"
#include "audio.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "textcolor.h"
#include "dsp.h"


//#include "fsk_demod_agc.h"	/* for M_FILTER_SIZE, etc. */

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))


// Don't remove this.  It serves as a reminder that an experiment is underway.

#if defined(TUNE_MS_FILTER_SIZE) || defined(TUNE_AGC_FAST) || defined(TUNE_LPF_BAUD) || defined(TUNE_PLL_LOCKED) || defined(TUNE_PROFILE)
#define DEBUG1 1
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
 *
 * Outputs:     lp_filter
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
}


/*------------------------------------------------------------------
 *
 * Name:        gen_bandpass
 *
 * Purpose:     Generate band pass filter kernel.
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
}

/* end dsp.c */
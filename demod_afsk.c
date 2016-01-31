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


// #define DEBUG1 1     /* display debugging info */

// #define DEBUG3 1	/* print carrier detect changes. */

// #define DEBUG4 1	/* capture AFSK demodulator output to log files */

// #define DEBUG5 1	/* capture 9600 output to log files */


/*------------------------------------------------------------------
 *
 * Module:      demod_afsk.c
 *
 * Purpose:   	Demodulator for Audio Frequency Shift Keying (AFSK).
 *		
 * Input:	Audio samples from either a file or the "sound card."
 *
 * Outputs:	Calls hdlc_rec_bit() for each bit demodulated.  
 *
 *---------------------------------------------------------------*/



#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "direwolf.h"
#include "audio.h"

#include "tune.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "hdlc_rec.h"
#include "textcolor.h"
#include "demod_afsk.h"
#include "dsp.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))




/* Quick approximation to sqrt(x*x+y*y) */
/* No benefit for regular PC. */
/* Should help with microcomputer platform. */


__attribute__((hot)) __attribute__((always_inline))
static inline float z (float x, float y)
{
        x = fabsf(x);
        y = fabsf(y);

        if (x > y) {
          return (x * .941246f + y * .41f);
        }
        else {
          return (y * .941246f + x * .41f);
        }
}

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
	float sum = 0.0f;
	int j;


#pragma GCC ivdep				// ignored until gcc 4.9
	for (j=0; j<filter_size; j++) {
	    sum += filter[j] * data[j];
	}

	return (sum);
}

/* Automatic gain control. */
/* Result should settle down to 1 unit peak to peak.  i.e. -0.5 to +0.5 */

__attribute__((hot)) __attribute__((always_inline))
static inline float agc (float in, float fast_attack, float slow_decay, float *ppeak, float *pvalley)
{
	if (in >= *ppeak) {
	  *ppeak = in * fast_attack + *ppeak * (1.0f - fast_attack);
	}
	else {
	  *ppeak = in * slow_decay + *ppeak * (1.0f - slow_decay);
	}

	if (in <= *pvalley) {
	  *pvalley = in * fast_attack + *pvalley * (1.0f - fast_attack);
	}
	else  {   
	  *pvalley = in * slow_decay + *pvalley * (1.0f - slow_decay);
	}

	if (*ppeak > *pvalley) {
	  return ((in - 0.5f * (*ppeak + *pvalley)) / (*ppeak - *pvalley));
	}
	return (0.0f);
}


/*
 * for multi-slicer experiment.
 */

#define MIN_G 0.5f
#define MAX_G 4.0f

/* TODO: static */  float space_gain[MAX_SUBCHANS];



/*------------------------------------------------------------------
 *
 * Name:        demod_afsk_init
 *
 * Purpose:     Initialization for an AFSK demodulator.
 *		Select appropriate parameters and set up filters.
 *
 * Inputs:   	samples_per_sec
 *		baud
 *		mark_freq
 *		space_freq
 *	
 *		D		- Pointer to demodulator state for given channel.
 *
 * Outputs:	D->ms_filter_size
 *		D->m_sin_table[] 
 *		D->m_cos_table[]
 *		D->s_sin_table[] 
 *		D->s_cos_table[]
 *
 * Returns:     None.
 *		
 * Bugs:	This doesn't do much error checking so don't give it
 *		anything crazy.
 *
 *----------------------------------------------------------------*/

void demod_afsk_init (int samples_per_sec, int baud, int mark_freq,
			int space_freq, char profile, struct demodulator_state_s *D)
{
	
	int j;
	
	memset (D, 0, sizeof(struct demodulator_state_s));
	D->num_slicers = 1;

#if DEBUG1
	dw_printf ("demod_afsk_init (rate=%d, baud=%d, mark=%d, space=%d, profile=%c\n",
		samples_per_sec, baud, mark_freq, space_freq, profile);
#endif
				
#ifdef TUNE_PROFILE
	profile = TUNE_PROFILE;
#endif


	if (profile == 'F') {

	  if (baud != DEFAULT_BAUD ||
	      mark_freq != DEFAULT_MARK_FREQ ||
	      space_freq!= DEFAULT_SPACE_FREQ ||
	      samples_per_sec != DEFAULT_SAMPLES_PER_SEC) {

	    	text_color_set(DW_COLOR_INFO);
		dw_printf ("Note: Decoder 'F' works only for %d baud, %d/%d tones, %d samples/sec.\n",
			DEFAULT_BAUD, DEFAULT_MARK_FREQ, DEFAULT_SPACE_FREQ, DEFAULT_SAMPLES_PER_SEC);
		dw_printf ("Using Decoder 'A' instead.\n");
		profile = 'A';
	  }
	}

	D->profile = profile;		// so we know whether to take fast path later.

	switch (profile) {

	  case 'A':
	  case 'F':

		/* Original.  52 taps, truncated bandpass, IIR lowpass */
		/* 'F' is the fast version for low end processors. */
		/* It is a special case that works only for a particular */
		/* baud rate, tone pair, and sampling rate. */

	    D->use_prefilter = 0;		

	    D->ms_filter_len_bits = 1.415;		/* 52 @ 44100, 1200 */
	    D->ms_window = BP_WINDOW_TRUNCATED;

	    //D->bp_window = BP_WINDOW_TRUNCATED;

	    D->lpf_use_fir = 0;
	    D->lpf_iir = 0.195;

	    D->agc_fast_attack = 0.250;		
	    D->agc_slow_decay = 0.00012;
	    D->hysteresis = 0.005;

	    D->pll_locked_inertia = 0.700;
	    D->pll_searching_inertia = 0.580;
	    break;

	  case 'B':

		/* Original bandpass.  Use FIR lowpass instead. */

	    D->use_prefilter = 0;		

	    D->ms_filter_len_bits = 1.415;		/* 52 @ 44100, 1200 */
	    D->ms_window = BP_WINDOW_TRUNCATED;

	    //D->bp_window = BP_WINDOW_TRUNCATED;

	    D->lpf_use_fir = 1;
	    D->lpf_baud = 1.09;
	    D->lp_filter_len_bits = D->ms_filter_len_bits;		
	    D->lp_window = BP_WINDOW_TRUNCATED;

	    D->agc_fast_attack = 0.370;		
	    D->agc_slow_decay = 0.00014;
	    D->hysteresis = 0.003;

	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	    break;

	  case 'C':

		/* Cosine window, 76 taps for bandpass, FIR lowpass. */

	    D->use_prefilter = 0;		

	    D->ms_filter_len_bits = 2.068;		/* 76 @ 44100, 1200 */
	    D->ms_window = BP_WINDOW_COSINE;

	    //D->bp_window = BP_WINDOW_COSINE;

	    D->lpf_use_fir = 1;
	    D->lpf_baud = 1.09;
	    D->lp_filter_len_bits = D->ms_filter_len_bits;		
	    D->lp_window = BP_WINDOW_TRUNCATED;

	    D->agc_fast_attack = 0.495;		
	    D->agc_slow_decay = 0.00022;
	    D->hysteresis = 0.005;

	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	    break;

	  case 'D':

		/* Prefilter, Cosine window, FIR lowpass. Tweeked for 300 baud. */

	    D->use_prefilter = 1;		/* first, a bandpass filter. */
	    D->prefilter_baud = 0.87;		
	    D->pre_filter_len_bits = 1.857;	
	    D->pre_window = BP_WINDOW_COSINE;

	    D->ms_filter_len_bits = 1.857;		/* 91 @ 44100/3, 300 */
	    D->ms_window = BP_WINDOW_COSINE;
		
	    //D->bp_window = BP_WINDOW_COSINE;

	    D->lpf_use_fir = 1;
	    D->lpf_baud = 1.10;
	    D->lp_filter_len_bits = D->ms_filter_len_bits;	
	    D->lp_window = BP_WINDOW_TRUNCATED;

	    D->agc_fast_attack = 0.495;		
	    D->agc_slow_decay = 0.00022;
	    D->hysteresis = 0.027;

	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	    break;

	  case 'E':

		/* 1200 baud - Started out similar to C but add prefilter. */
		/* Version 1.2 */
		/* Enhancements: 					*/
		/*  + Add prefilter.  Previously used for 300 baud D, but not 1200. */
		/*  + Prefilter length now independent of M/S filters.	*/
		/*  + Lowpass filter length now independent of M/S filters.	*/
		/*  + Allow mixed window types.	*/

	    //D->bp_window = BP_WINDOW_COSINE;	/* The name says BP but it is used for all of them. */

	    D->use_prefilter = 1;		/* first, a bandpass filter. */
	    D->prefilter_baud = 0.23;		
	    D->pre_filter_len_bits = 156 * 1200. / 44100.;	
	    D->pre_window = BP_WINDOW_TRUNCATED;

	    D->ms_filter_len_bits = 74 * 1200. / 44100.;		
	    D->ms_window = BP_WINDOW_COSINE;

	    D->lpf_use_fir = 1;
	    D->lpf_baud = 1.18;
	    D->lp_filter_len_bits = 63 * 1200. / 44100.;		
	    D->lp_window = BP_WINDOW_TRUNCATED;

	    //D->agc_fast_attack = 0.300;		
	    //D->agc_slow_decay = 0.000185;
	    D->agc_fast_attack = 0.820;		
	    D->agc_slow_decay = 0.000214;
	    D->hysteresis = 0.01;

	    //D->pll_locked_inertia = 0.57;
	    //D->pll_searching_inertia = 0.33;
	    D->pll_locked_inertia = 0.74;
	    D->pll_searching_inertia = 0.50;
	    break;

	  case 'G':

		/* 1200 baud - Started out same as E but add 3 way interleave. */
		/* Version 1.3 - EXPERIMENTAL - Needs more fine tuning. */

	    //D->bp_window = BP_WINDOW_COSINE;	/* The name says BP but it is used for all of them. */

	    D->use_prefilter = 1;		/* first, a bandpass filter. */
	    D->prefilter_baud = 0.15;
	    D->pre_filter_len_bits = 128 * 1200. / (44100. / 3.);
	    D->pre_window = BP_WINDOW_TRUNCATED;

	    D->ms_filter_len_bits = 25 * 1200. / (44100. / 3.);
	    D->ms_window = BP_WINDOW_COSINE;

	    D->lpf_use_fir = 1;
	    D->lpf_baud = 1.16;
	    D->lp_filter_len_bits = 21 * 1200. / (44100. / 3.);
	    D->lp_window = BP_WINDOW_TRUNCATED;

	    D->agc_fast_attack = 0.130;
	    D->agc_slow_decay = 0.00013;
	    D->hysteresis = 0.01;

	    D->pll_locked_inertia = 0.73;
	    D->pll_searching_inertia = 0.64;
	    break;

	  default:

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Invalid filter profile = %c\n", profile);
	    exit (1);
	}

#ifdef TUNE_PRE_WINDOW
	D->pre_window = TUNE_PRE_WINDOW;
#endif
#ifdef TUNE_MS_WINDOW
	D->ms_window = TUNE_MS_WINDOW;
#endif
#ifdef TUNE_LP_WINDOW
	D->lp_window = TUNE_LP_WINDOW;
#endif


#if defined(TUNE_AGC_FAST) && defined(TUNE_AGC_SLOW)
	D->agc_fast_attack = TUNE_AGC_FAST;
	D->agc_slow_decay = TUNE_AGC_SLOW;
#endif
#ifdef TUNE_HYST
	D->hysteresis = TUNE_HYST;
#endif
#if defined(TUNE_PLL_LOCKED) && defined(TUNE_PLL_SEARCHING)
	D->pll_locked_inertia = TUNE_PLL_LOCKED;
	D->pll_searching_inertia = TUNE_PLL_SEARCHING;
#endif
#ifdef TUNE_LPF_BAUD
	D->lpf_baud = TUNE_LPF_BAUD;
#endif
#ifdef TUNE_PRE_BAUD
	D->prefilter_baud = TUNE_PRE_BAUD;
#endif


/*
 * Calculate constants used for timing.
 * The audio sample rate must be at least a few times the data rate.
 */

	D->pll_step_per_sample = (int) round((TICKS_PER_PLL_CYCLE * (double)baud) / ((double)samples_per_sec));

/*
 * Convert number of bit times to number of taps.
 */

	D->pre_filter_size = (int) round( D->pre_filter_len_bits * (float)samples_per_sec / (float)baud );
	D->ms_filter_size = (int) round( D->ms_filter_len_bits * (float)samples_per_sec / (float)baud );
	D->lp_filter_size = (int) round( D->lp_filter_len_bits * (float)samples_per_sec / (float)baud );
	  	 
/* Experiment with other sizes. */

#ifdef TUNE_PRE_FILTER_SIZE
	D->pre_filter_size = TUNE_PRE_FILTER_SIZE;
#endif
#ifdef TUNE_MS_FILTER_SIZE
	D->ms_filter_size = TUNE_MS_FILTER_SIZE;
#endif
#ifdef TUNE_LP_FILTER_SIZE
	D->lp_filter_size = TUNE_LP_FILTER_SIZE;
#endif

	//assert (D->pre_filter_size >= 4);
	assert (D->ms_filter_size >= 4);
	//assert (D->lp_filter_size >= 4);

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
 * This was first used for the "D" profile for 300 baud
 * which uses narrow shift.  We expect it to have significant
 * benefit for a narrow shift.
 * In version 1.2, we will also try it with 1200 baud "E" as
 * an experiment to see how much it actually helps.
 */

	if (D->use_prefilter) {
	  float f1, f2;

	  f1 = MIN(mark_freq,space_freq) - D->prefilter_baud * baud;
	  f2 = MAX(mark_freq,space_freq) + D->prefilter_baud * baud;
#if 0
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Generating prefilter %.0f to %.0f Hz.\n", f1, f2);
#endif
	  f1 = f1 / (float)samples_per_sec;
	  f2 = f2 / (float)samples_per_sec;
	  
	  //gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, BP_WINDOW_HAMMING);
	  //gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, BP_WINDOW_BLACKMAN);
	  //gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, BP_WINDOW_COSINE);
	  //gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, D->bp_window);
	  gen_bandpass (f1, f2, D->pre_filter, D->pre_filter_size, D->pre_window);
	}

/*
 * Filters for detecting mark and space tones.
 */

#if DEBUG1
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("%s:  \n", __FILE__);
	  dw_printf ("%d baud, %d samples_per_sec\n", baud, samples_per_sec);
	  dw_printf ("AFSK %d & %d Hz\n", mark_freq, space_freq);
	  dw_printf ("spll_step_per_sample = %d = 0x%08x\n", D->pll_step_per_sample, D->pll_step_per_sample);
	  dw_printf ("D->ms_filter_size = %d = 0x%08x\n", D->ms_filter_size, D->ms_filter_size);
	  dw_printf ("\n");
	  dw_printf ("Mark\n");
	  dw_printf ("   j     shape   M sin   M cos \n");
#endif

	  float Gs = 0, Gc = 0;

          for (j=0; j<D->ms_filter_size; j++) {
	    float am;
	    float center;
	    float shape = 1;		/* Shape is an attempt to smooth out the */
					/* abrupt edges in hopes of reducing */
					/* overshoot and ringing. */
					/* My first thought was to use a cosine shape. */
					/* Should investigate Hamming and Blackman */
					/* windows mentioned in the literature. */
					/* http://en.wikipedia.org/wiki/Window_function */

	    center = 0.5 * (D->ms_filter_size - 1);
	    am = ((float)(j - center) / (float)samples_per_sec) * ((float)mark_freq) * (2 * M_PI);

	    shape = window (D->ms_window, D->ms_filter_size, j);

	    D->m_sin_table[j] = sin(am) * shape;
  	    D->m_cos_table[j] = cos(am) * shape;

	    Gs += D->m_sin_table[j] * sin(am);
	    Gc += D->m_cos_table[j] * cos(am);

#if DEBUG1
	    dw_printf ("%6d  %6.2f  %6.2f  %6.2f\n", j, shape, D->m_sin_table[j], D->m_cos_table[j]) ;
#endif
          }


/* Normalize for unity gain */

#if DEBUG1
	  dw_printf ("Before normalizing, Gs = %.2f, Gc = %.2f\n", Gs, Gc) ;
#endif
          for (j=0; j<D->ms_filter_size; j++) {
	    D->m_sin_table[j] = D->m_sin_table[j] / Gs;
	    D->m_cos_table[j] = D->m_cos_table[j] / Gc;
	  }


#if DEBUG1
	  text_color_set(DW_COLOR_DEBUG);

	  dw_printf ("Space\n");
	  dw_printf ("   j     shape   S sin   S cos\n");
#endif
	  Gs = 0;
	  Gc = 0;

          for (j=0; j<D->ms_filter_size; j++) {
	    float as;
	    float center;
	    float shape = 1;

	    center = 0.5 * (D->ms_filter_size - 1);
	    as = ((float)(j - center) / (float)samples_per_sec) * ((float)space_freq) * (2 * M_PI);

	    shape = window (D->ms_window, D->ms_filter_size, j);

	    D->s_sin_table[j] = sin(as) * shape;
  	    D->s_cos_table[j] = cos(as) * shape;

	    Gs += D->s_sin_table[j] * sin(as);
	    Gc += D->s_cos_table[j] * cos(as);

#if DEBUG1
	    dw_printf ("%6d  %6.2f  %6.2f  %6.2f\n", j, shape, D->s_sin_table[j], D->s_cos_table[j] ) ;
#endif
          }


/* Normalize for unity gain */

#if DEBUG1
	  dw_printf ("Before normalizing, Gs = %.2f, Gc = %.2f\n", Gs, Gc) ;
#endif
          for (j=0; j<D->ms_filter_size; j++) {
	    D->s_sin_table[j] = D->s_sin_table[j] / Gs;
	    D->s_cos_table[j] = D->s_cos_table[j] / Gc;
	  }

/*
 * Now the lowpass filter.
 * I thought we'd want a cutoff of about 0.5 the baud rate 
 * but it turns out about 1.1x is better.  Still investigating...
 */

	if (D->lpf_use_fir) {
	  float fc;
	  fc = baud * D->lpf_baud / (float)samples_per_sec;
	  gen_lowpass (fc, D->lp_filter, D->lp_filter_size, D->lp_window);
	}

/*
 * A non-whole number of cycles results in a DC bias. 
 * Let's see if it helps to take it out.
 * Actually makes things worse:  20 fewer decoded.
 * Might want to try again after EXPERIMENTC.
 */

#if 0
#ifndef AVOID_FLOATING_POINT

failed experiment

	dc_bias = 0;
        for (j=0; j<D->ms_filter_size; j++) {
	  dc_bias += D->m_sin_table[j];
	}
        for (j=0; j<D->ms_filter_size; j++) {
	  D->m_sin_table[j] -= dc_bias / D->ms_filter_size;
	}

	dc_bias = 0;
        for (j=0; j<D->ms_filter_size; j++) {
	  dc_bias += D->m_cos_table[j];
	}
        for (j=0; j<D->ms_filter_size; j++) {
	  D->m_cos_table[j] -= dc_bias / D->ms_filter_size;
	}


	dc_bias = 0;
        for (j=0; j<D->ms_filter_size; j++) {
	  dc_bias += D->s_sin_table[j];
	}
        for (j=0; j<D->ms_filter_size; j++) {
	  D->s_sin_table[j] -= dc_bias / D->ms_filter_size;
	}

	dc_bias = 0;
        for (j=0; j<D->ms_filter_size; j++) {
	  dc_bias += D->s_cos_table[j];
	}
        for (j=0; j<D->ms_filter_size; j++) {
	  D->s_cos_table[j] -= dc_bias / D->ms_filter_size;
	}

#endif
#endif

/*
 * In version 1.2 we try another experiment.
 * Try using multiple slicing points instead of the traditional AGC.
 */

	space_gain[0] = MIN_G;
	float step = powf(10.0, log10f(MAX_G/MIN_G) / (MAX_SUBCHANS-1));
	for (j=1; j<MAX_SUBCHANS; j++) {
	  space_gain[j] = space_gain[j-1] * step;
	}

#ifndef GEN_FFF
#if 0
	text_color_set(DW_COLOR_DEBUG);
	for (j=0; j<MAX_SUBCHANS; j++) {
	  float db = 20.0 * log10f(space_gain[j]);
	  dw_printf ("G = %.3f, %+.1f dB\n", space_gain[j], db);
	}
#endif
#endif

}  /* fsk_gen_filter */


#if GEN_FFF



// Properties of the radio channels.

static struct audio_s modem;


// Filters will be stored here. 

static struct demodulator_state_s ds;


#define SPARSE 3


static void emit_macro (char *name, int size, float *coeff)
{
	int i;

	dw_printf ("#define %s(x) \\\n", name);

	for (i=SPARSE/2; i<size; i+=SPARSE) {
	  dw_printf ("\t%c (%.6ff * x[%d]) \\\n", (i==0 ? ' ' : '+'), coeff[i], i);
	}
	dw_printf ("\n");
}

int main (void)
{
	//int n;
	char fff_profile;

	fff_profile = 'F';		

	memset (&modem, 0, sizeof(modem));
	memset (&ds, 0, sizeof(ds));

	modem.adev[0].num_channels = 1;
	modem.adev[0].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;
	modem.achan[0].mark_freq = DEFAULT_MARK_FREQ;
	modem.achan[0].space_freq = DEFAULT_SPACE_FREQ;
	modem.achan[0].baud = DEFAULT_BAUD;
 	modem.achan[0].num_subchan = 1;
 	modem.achan[0].num_slicers = 1;


	demod_afsk_init (modem.adev[0].samples_per_sec, modem.achan[0].baud,
			modem.achan[0].mark_freq, modem.achan[0].space_freq, fff_profile, &ds);
	
	printf ("/* This is an automatically generated file.  Do not edit. */\n");
	printf ("\n");
	printf ("#define FFF_SAMPLES_PER_SEC %d\n", modem.adev[0].samples_per_sec);
	printf ("#define FFF_BAUD %d\n", modem.achan[0].baud);
	printf ("#define FFF_MARK_FREQ %d\n", modem.achan[0].mark_freq);
	printf ("#define FFF_SPACE_FREQ %d\n", modem.achan[0].space_freq);
	printf ("#define FFF_PROFILE '%c'\n", fff_profile);
	printf ("\n");

	emit_macro ("CALC_M_SUM1", ds.ms_filter_size, ds.m_sin_table);
	emit_macro ("CALC_M_SUM2", ds.ms_filter_size, ds.m_cos_table);
	emit_macro ("CALC_S_SUM1", ds.ms_filter_size, ds.s_sin_table);
	emit_macro ("CALC_S_SUM2", ds.ms_filter_size, ds.s_cos_table);

	exit(0);
}

#endif



#ifndef GEN_FFF

/* Optimization for slow processors. */

#include "fsk_fast_filter.h"



/*-------------------------------------------------------------------
 *
 * Name:        demod_afsk_process_sample
 *
 * Purpose:     (1) Demodulate the AFSK signal.
 *		(2) Recover clock and data.
 *
 * Inputs:	chan	- Audio channel.  0 for left, 1 for right.
 *		subchan - modem of the channel.
 *		sam	- One sample of audio.
 *			  Should be in range of -32768 .. 32767.
 *
 * Returns:	None 
 *
 * Descripion:	We start off with two bandpass filters tuned to
 *		the given frequencies.  In the case of VHF packet
 *		radio, this would be 1200 and 2200 Hz.
 *
 *		The bandpass filter amplitudes are compared to 
 *		obtain the demodulated signal.
 *
 *		We also have a digital phase locked loop (PLL)
 *		to recover the clock and pick out data bits at
 *		the proper rate.
 *
 *		For each recovered data bit, we call:
 *
 *			  hdlc_rec (channel, demodulated_bit);
 *
 *		to decode HDLC frames from the stream of bits.
 *
 * Future:	This could be generalized by passing in the name
 *		of the function to be called for each bit recovered
 *		from the demodulator.  For now, it's simply hard-coded.
 *
 *--------------------------------------------------------------------*/

static void inline nudge_pll (int chan, int subchan, int slice, int demod_data, struct demodulator_state_s *D);

__attribute__((hot))
void demod_afsk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D)
{
	float fsam;
	//float abs_fsam;
	float m_sum1, m_sum2, s_sum1, s_sum2;
	float m_amp, s_amp;
	float m_norm, s_norm;
	float demod_out;
#if DEBUG4
	static FILE *demod_log_fp = NULL;
	static int seq = 0;			/* for log file name */
#endif


	//int j;
	int demod_data;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

/* 
 * Filters use last 'filter_size' samples.
 *
 * First push the older samples down. 
 *
 * Finally, put the most recent at the beginning.
 *
 * Future project?  Can we do better than shifting each time?
 */

	/* Scale to nice number, TODO: range -1.0 to +1.0, not 2. */

	fsam = sam / 16384.0f;

	//abs_fsam = fsam >= 0.0f ? fsam : -fsam;


/*
 * Optional bandpass filter before the mark/space discriminator.
 */

	if (D->use_prefilter) {
	  float cleaner;

	  push_sample (fsam, D->raw_cb, D->pre_filter_size);
	  cleaner = convolve (D->raw_cb, D->pre_filter, D->pre_filter_size);
	  push_sample (cleaner, D->ms_in_cb, D->ms_filter_size);
	}
	else {
	  push_sample (fsam, D->ms_in_cb, D->ms_filter_size);
	}

/*
 * Next we have bandpass filters for the mark and space tones.
 *
 * This takes a lot of computation.
 * It's not a problem on a typical (Intel x86 based) PC.
 * Dire Wolf takes only about 2 or 3% of the CPU time.
 *
 * It might be too much for a little microcomputer to handle.
 *
 * Here we have an optimized case for the default values.
 */



// TODO1.2:   is this right or do we need to store profile in the modulator info?

	
	if (D->profile == toupper(FFF_PROFILE)) {

				/* ========== Faster for default values on slower processors. ========== */

	  m_sum1 = CALC_M_SUM1(D->ms_in_cb);
	  m_sum2 = CALC_M_SUM2(D->ms_in_cb);
	  m_amp = z(m_sum1,m_sum2);

	  s_sum1 = CALC_S_SUM1(D->ms_in_cb);
	  s_sum2 = CALC_S_SUM2(D->ms_in_cb);
	  s_amp = z(s_sum1,s_sum2);
	}
	else {

				/* ========== General case to handle all situations. ========== */
	
/*
 * find amplitude of "Mark" tone.
 */
	  m_sum1 = convolve (D->ms_in_cb, D->m_sin_table, D->ms_filter_size);
	  m_sum2 = convolve (D->ms_in_cb, D->m_cos_table, D->ms_filter_size);

	  m_amp = sqrtf(m_sum1 * m_sum1 + m_sum2 * m_sum2);

/*
 * Find amplitude of "Space" tone.
 */
	  s_sum1 = convolve (D->ms_in_cb, D->s_sin_table, D->ms_filter_size);
	  s_sum2 = convolve (D->ms_in_cb, D->s_cos_table, D->ms_filter_size);

	  s_amp = sqrtf(s_sum1 * s_sum1 + s_sum2 * s_sum2);

				/* ========== End of general case. ========== */
	}
		

/* 
 * Apply some low pass filtering BEFORE the AGC to remove
 * overshoot, ringing, and other bad stuff.
 *
 * A simple IIR filter is faster but FIR produces better results.
 *
 * It is a balancing act between removing high frequency components
 * from the tone dectection while letting the data thru.
 */

	if (D->lpf_use_fir) {

	  push_sample (m_amp, D->m_amp_cb, D->lp_filter_size);
	  m_amp = convolve (D->m_amp_cb, D->lp_filter, D->lp_filter_size);

	  push_sample (s_amp, D->s_amp_cb, D->lp_filter_size);
	  s_amp = convolve (D->s_amp_cb, D->lp_filter, D->lp_filter_size);
	}
	else {
	
	  /* Original, but faster, IIR. */

	  m_amp = D->lpf_iir * m_amp + (1.0f - D->lpf_iir) * D->m_amp_prev;
	  D->m_amp_prev = m_amp;

	  s_amp = D->lpf_iir * s_amp + (1.0f - D->lpf_iir) * D->s_amp_prev;
	  D->s_amp_prev = s_amp;
	}

/*
 * Version 1.2: Try new approach to capturing the amplitude for display.
 * This is same as the AGC above without the normalization step.
 * We want decay to be substantially slower to get a longer
 * range idea of the received audio.
 */

	if (m_amp >= D->alevel_mark_peak) {
	  D->alevel_mark_peak = m_amp * D->quick_attack + D->alevel_mark_peak * (1.0f - D->quick_attack);
	}
	else {
	  D->alevel_mark_peak = m_amp * D->sluggish_decay + D->alevel_mark_peak * (1.0f - D->sluggish_decay);
	}

	if (s_amp >= D->alevel_space_peak) {
	  D->alevel_space_peak = s_amp * D->quick_attack + D->alevel_space_peak * (1.0f - D->quick_attack);
	}
	else {
	  D->alevel_space_peak = s_amp * D->sluggish_decay + D->alevel_space_peak * (1.0f - D->sluggish_decay);
	}


/* 
 * Which tone is stronger?
 *
 * In an ideal world, simply compare.  In my first naive attempt, that
 * worked perfectly with perfect signals. In the real world, we don't
 * have too many perfect signals.
 *
 * Here is an excellent explanation:
 * http://www.febo.com/packet/layer-one/transmit.html
 *
 * Under real conditions, we find that the higher tone has a
 * considerably smaller amplitude due to the passband characteristics
 * of the transmitter and receiver.  To make matters worse, it
 * varies considerably from one station to another.
 *
 * The two filters also have different amounts of DC bias.
 *
 * My solution was to apply automatic gain control (AGC) to the mark and space 
 * levels.  This works by looking at the minimum and maximum outputs
 * for each filter and scaling the results to be roughly in the -0.5 to +0.5 range.
 * Results were excellent after tweaking the attack and decay times.
 *
 * 4X6IZ took a different approach.  See QEX Jul-Aug 2012.
 *
 * He ran two different demodulators in parallel.  One of them boosted the higher
 * frequency tone by 6 dB.  Any duplicates were removed.  This produced similar results.
 * He also used a bandpass filter before the mark/space filters.  
 * I haven't tried this combination yet for 1200 baud.
 *
 * First, let's take a look at Track 1 of the TNC test CD.  Here the receiver
 * has a flat response.  We find the mark/space strength ratios very from 0.53 to 1.38
 * with a median of 0.81.  This in in line with expections because most
 * transmitters add pre-emphasis to boost the higher audio frequencies.
 * Track 2 should more closely resemble what comes out of the speaker on a typical
 * transceiver.  Here we see a ratio from 1.73 to 3.81 with a median of 2.48.
 * 
 * This is similar to my observations of local signals, from the speaker.
 * The amplitude ratio varies from 1.48 to 3.41 with a median of 2.70. 
 *
 * Rather than only two filters, let's try slicing the data in more places. 
 */

	/* Fast attack and slow decay. */
	/* Numbers were obtained by trial and error from actual */
	/* recorded less-than-optimal signals. */

	/* See fsk_demod_agc.h for more information. */

	m_norm = agc (m_amp, D->agc_fast_attack, D->agc_slow_decay, &(D->m_peak), &(D->m_valley));
	s_norm = agc (s_amp, D->agc_fast_attack, D->agc_slow_decay, &(D->s_peak), &(D->s_valley));

	if (D->num_slicers <= 1) {

	  /* Normal case of one demodulator to one HDLC decoder. */
	  /* Demodulator output is difference between response from two filters. */
	  /* AGC should generally keep this around -1 to +1 range. */

	  demod_out = m_norm - s_norm;

	  /* Try adding some Hysteresis. */
	  /* (Not to be confused with Hysteria.) */

	  if (demod_out > D->hysteresis) {
	    demod_data = 1;
	  }
	  else if (demod_out < (- (D->hysteresis))) {
	    demod_data = 0;
	  } 
	  else {
	    demod_data = D->slicer[subchan].prev_demod_data;
	  }
	  nudge_pll (chan, subchan, 0, demod_data, D);
	}
	else {
	  int slice;

	  for (slice=0; slice<D->num_slicers; slice++) {
	    demod_data = m_amp > s_amp * space_gain[slice];
	    nudge_pll (chan, subchan, slice, demod_data, D);
	  }
	}


#if DEBUG4

	if (chan == 0) {
	if (hdlc_rec_gathering (chan, subchan)) {
	  char fname[30];

	  
	  if (demod_log_fp == NULL) {
	    seq++;
	    snprintf (fname, sizeof(fname), "demod/%04d.csv", seq);
	    if (seq == 1) mkdir ("demod", 0777);

	    demod_log_fp = fopen (fname, "w");
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Starting demodulator log file %s\n", fname);
	    fprintf (demod_log_fp, "Audio, Mark, Space, Demod, Data, Clock\n");
	  }
	  fprintf (demod_log_fp, "%.3f, %.3f, %.3f, %.3f, %.2f, %.2f\n", fsam + 3.5, m_norm + 2, s_norm + 2, 
			(m_norm - s_norm) / 2 + 1.5,
			demod_data ? .9 : .55,  
			(D->data_clock_pll & 0x80000000) ? .1 : .45);
	}
	else {
	  if (demod_log_fp != NULL) {
	    fclose (demod_log_fp);
	    demod_log_fp = NULL;
	  }
	}
	}

#endif


} /* end demod_afsk_process_sample */


__attribute__((hot))
static void inline nudge_pll (int chan, int subchan, int slice, int demod_data, struct demodulator_state_s *D)
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
 * phase when searching for a signal.  Don't change it as much when
 * locked on to a signal.
 *
 * I don't think the optimal value will depend on the audio sample rate
 * because this happens for each transition from the demodulator.
 */

	D->slicer[slice].prev_d_c_pll = D->slicer[slice].data_clock_pll;
	D->slicer[slice].data_clock_pll += D->pll_step_per_sample;

	  //text_color_set(DW_COLOR_DEBUG);
	  // dw_printf ("prev = %lx, new data clock pll = %lx\n" D->prev_d_c_pll, D->data_clock_pll);

	if (D->slicer[slice].data_clock_pll < 0 && D->slicer[slice].prev_d_c_pll > 0) {

	  /* Overflow. */

	  hdlc_rec_bit (chan, subchan, slice, demod_data, 0, -1);
	}

        if (demod_data != D->slicer[slice].prev_demod_data) {

	  if (hdlc_rec_gathering (chan, subchan, slice)) {
	    D->slicer[slice].data_clock_pll = (int)(D->slicer[slice].data_clock_pll * D->pll_locked_inertia);
	  }
	  else {
	    D->slicer[slice].data_clock_pll = (int)(D->slicer[slice].data_clock_pll * D->pll_searching_inertia);
	  }
	}

/*
 * Remember demodulator output so we can compare next time.
 */
	D->slicer[slice].prev_demod_data = demod_data;

} /* end nudge_pll */


#endif   /* GEN_FFF */


/* end demod_afsk.c */

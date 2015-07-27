//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
// 
//    Copyright (C) 2011,2012,2013,2014  John Langner, WB2OSZ
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
//#include "fsk_demod.h"
//#include "gen_tone.h"
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


__attribute__((hot))
static inline float z (float x, float y)
{
        x = fabsf(x);
        y = fabsf(y);

        if (x > y) {
          return (x * .941246 + y * .41);
        }
        else {
          return (y * .941246 + x * .41);
        }
}

/* Add sample to buffer and shift the rest down. */

__attribute__((hot))
static inline void push_sample (float val, float *buff, int size)
{
	memmove(buff+1,buff,(size-1)*sizeof(float));
	buff[0] = val; 
}


/* FIR filter kernel. */

__attribute__((hot))
static inline float convolve (const float *data, const float *filter, int filter_size)
{
	  float sum = 0;
	  int j;

	  for (j=0; j<filter_size; j++) {
	    sum += filter[j] * data[j];
	  }
	  return (sum);
}

/* Automatic gain control. */
/* Result should settle down to 1 unit peak to peak.  i.e. -0.5 to +0.5 */

__attribute__((hot))
static inline float agc (float in, float fast_attack, float slow_decay, float *ppeak, float *pvalley)
{
	if (in >= *ppeak) {
	  *ppeak = in * fast_attack + *ppeak * (1. - fast_attack);
	}
	else {
	  *ppeak = in * slow_decay + *ppeak * (1. - slow_decay);
	}

	if (in <= *pvalley) {
	  *pvalley = in * fast_attack + *pvalley * (1. - fast_attack);
	}
	else  {   
	  *pvalley = in * slow_decay + *pvalley * (1. - slow_decay);
	}

	if (*ppeak > *pvalley) {
	  return ((in - 0.5 * (*ppeak + *pvalley)) / (*ppeak - *pvalley));
	}
	return (0.0);
}



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

#if DEBUG1
	dw_printf ("demod_afsk_init (rate=%d, baud=%d, mark=%d, space=%d, profile=%c\n",
		samples_per_sec, baud, mark_freq, space_freq, profile);
#endif
				
#ifdef TUNE_PROFILE
	profile = TUNE_PROFILE;
#endif

	if (toupper(profile) == 'F') {

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

	if (profile == 'a' || profile == 'A' || profile == 'f' || profile == 'F') {

		/* Original.  52 taps, truncated bandpass, IIR lowpass */
		/* 'F' is the fast version for low end processors. */
		/* It is a special case that works only for a particular */
		/* baud rate, tone pair, and sampling rate. */

	    D->filter_len_bits = 1.415;		/* 52 @ 44100, 1200 */
	    D->bp_window = BP_WINDOW_TRUNCATED;
	    D->lpf_use_fir = 0;
	    D->lpf_iir = 0.195;
	    D->lpf_baud = 0;
	    D->agc_fast_attack = 0.250;		
	    D->agc_slow_decay = 0.00012;
	    D->hysteresis = 0.005;
	    D->pll_locked_inertia = 0.700;
	    D->pll_searching_inertia = 0.580;
	}
	else if (profile == 'b' || profile == 'B') {

		/* Original bandpass.  Use FIR lowpass instead. */

	    D->filter_len_bits = 1.415;		/* 52 @ 44100, 1200 */
	    D->bp_window = BP_WINDOW_TRUNCATED;
	    D->lpf_use_fir = 1;
	    D->lpf_iir = 0;
	    D->lpf_baud = 1.09;
	    D->agc_fast_attack = 0.370;		
	    D->agc_slow_decay = 0.00014;
	    D->hysteresis = 0.003;
	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	}
	else if (profile == 'c' || profile == 'C') {

		/* Cosine window, 76 taps for bandpass, FIR lowpass. */

	    D->filter_len_bits = 2.068;		/* 76 @ 44100, 1200 */
	    D->bp_window = BP_WINDOW_COSINE;
	    D->lpf_use_fir = 1;
	    D->lpf_iir = 0;
	    D->lpf_baud = 1.09;
	    D->agc_fast_attack = 0.495;		
	    D->agc_slow_decay = 0.00022;
	    D->hysteresis = 0.005;
	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	}
	else if (profile == 'd' || profile == 'D') {

		/* Prefilter, Cosine window, FIR lowpass. Tweeked for 300 baud. */

	    D->use_prefilter = 1;		/* first, a bandpass filter. */
	    D->prefilter_baud = 0.87;		/* Cosine window. */
	    D->filter_len_bits = 1.857;		/* 91 @ 44100/3, 300 */
	    D->bp_window = BP_WINDOW_COSINE;
	    D->lpf_use_fir = 1;
	    D->lpf_iir = 0;
	    D->lpf_baud = 1.10;
	    D->agc_fast_attack = 0.495;		
	    D->agc_slow_decay = 0.00022;
	    D->hysteresis = 0.027;
	    D->pll_locked_inertia = 0.620;
	    D->pll_searching_inertia = 0.350;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid filter profile = %c\n", profile);
	  exit (1);
	}


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
 * My initial guess at length of filter was about one bit time.
 * By trial and error, the optimal value was found to somewhat longer.
 * This was optimized for 44,100 sample rate, 1200 baud, 1200/2200 Hz.
 * More experimentation is needed for other situations.
 */

	D->ms_filter_size = (int) round( D->filter_len_bits * (float)samples_per_sec / (float)baud );
	  	 
/* Experiment with other sizes. */

#if defined(TUNE_MS_FILTER_SIZE)
	D->ms_filter_size = TUNE_MS_FILTER_SIZE;
#endif
	D->lp_filter_size = D->ms_filter_size;

	assert (D->ms_filter_size >= 4);

	if (D->ms_filter_size > MAX_FILTER_SIZE) 
	{
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("Calculated filter size of %d is too large.\n", D->ms_filter_size);
	  dw_printf ("Decrease the audio sample rate or increase the baud rate or\n");
	  dw_printf ("recompile the application with MAX_FILTER_SIZE larger than %d.\n",
							MAX_FILTER_SIZE);
	  exit (1);
	}


/* 
 * For narrow AFSK (e.g. 200 Hz shift), it might be beneficial to 
 * have a bandpass filter before the mark/space detector.
 * For now, make it the same number of taps for simplicity.
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
	  
	  //gen_bandpass (f1, f2, D->pre_filter, D->ms_filter_size, BP_WINDOW_HAMMING);
	  //gen_bandpass (f1, f2, D->pre_filter, D->ms_filter_size, BP_WINDOW_BLACKMAN);
	  gen_bandpass (f1, f2, D->pre_filter, D->ms_filter_size, BP_WINDOW_COSINE);
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

	    shape = window (D->bp_window, D->ms_filter_size, j);

	    D->m_sin_table[j] = sin(am) * shape;
  	    D->m_cos_table[j] = cos(am) * shape;

#if DEBUG1
	    dw_printf ("%6d  %6.2f  %6.2f  %6.2f\n", j, shape, D->m_sin_table[j], D->m_cos_table[j]) ;
#endif
          }


#if DEBUG1
	  text_color_set(DW_COLOR_DEBUG);

	  dw_printf ("Space\n");
	  dw_printf ("   j     shape   S sin   S cos\n");
#endif
          for (j=0; j<D->ms_filter_size; j++) {
	    float as;
	    float center;
	    float shape = 1;

	    center = 0.5 * (D->ms_filter_size - 1);
	    as = ((float)(j - center) / (float)samples_per_sec) * ((float)space_freq) * (2 * M_PI);

	    shape = window (D->bp_window, D->ms_filter_size, j);

	    D->s_sin_table[j] = sin(as) * shape;
  	    D->s_cos_table[j] = cos(as) * shape;

#if DEBUG1
	    dw_printf ("%6d  %6.2f  %6.2f  %6.2f\n", j, shape, D->s_sin_table[j], D->s_cos_table[j] ) ;
#endif
          }


/* Do we want to normalize for unity gain? */


/*
 * Now the lowpass filter.
 * I thought we'd want a cutoff of about 0.5 the baud rate 
 * but it turns out about 1.1x is better.  Still investigating...
 */

	if (D->lpf_use_fir) {
	  float fc;
	  fc = baud * D->lpf_baud / (float)samples_per_sec;
	  gen_lowpass (fc, D->lp_filter, D->lp_filter_size, BP_WINDOW_TRUNCATED);
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
	  dw_printf ("\t%c (%.6f * x[%d]) \\\n", (i==0 ? ' ' : '+'), coeff[i], i);
	}
	dw_printf ("\n");
}

int main ()
{
	//int n;
	char fff_profile;

	fff_profile = 'F';		

	memset (&modem, 0, sizeof(modem));
	memset (&ds, 0, sizeof(ds));

	modem.num_channels = 1;
	modem.samples_per_sec = DEFAULT_SAMPLES_PER_SEC;
	modem.mark_freq[0] = DEFAULT_MARK_FREQ;
	modem.space_freq[0] = DEFAULT_SPACE_FREQ;
	modem.baud[0] = DEFAULT_BAUD;
 	modem.num_subchan[0] = 1;


	demod_afsk_init (modem.samples_per_sec, modem.baud[0],
			modem.mark_freq[0], modem.space_freq[0], fff_profile, &ds);
	
	printf ("/* This is an automatically generated file.  Do not edit. */\n");
	printf ("\n");
	printf ("#define FFF_SAMPLES_PER_SEC %d\n", modem.samples_per_sec);
	printf ("#define FFF_BAUD %d\n", modem.baud[0]);
	printf ("#define FFF_MARK_FREQ %d\n", modem.mark_freq[0]);
	printf ("#define FFF_SPACE_FREQ %d\n", modem.space_freq[0]);
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


__attribute__((hot))
void demod_afsk_process_sample (int chan, int subchan, int sam, struct demodulator_state_s *D)
{
	float fsam, abs_fsam;
	float m_sum1, m_sum2, s_sum1, s_sum2;
	float m_amp, s_amp;
	float m_norm, s_norm;
	float demod_out;
#if DEBUG4
	static FILE *demod_log_fp = NULL;
	static int seq = 0;			/* for log file name */
#endif

	int j;
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

	fsam = sam / 16384.0;

/*
 * Accumulate measure of the input signal level.
 */
	abs_fsam = fsam >= 0 ? fsam : -fsam;
 
// TODO:  move to common code
              
	if (abs_fsam > D->lev_peak_acc) {
	  D->lev_peak_acc = abs_fsam;
	}
	D->lev_sum_acc += abs_fsam;

	D->lev_count++;
	if (D->lev_count >= D->lev_period) {
	  D->lev_prev_peak = D->lev_last_peak;
          D->lev_last_peak = D->lev_peak_acc;
          D->lev_peak_acc = 0;

          D->lev_prev_ave = D->lev_last_ave;
   	  D->lev_last_ave = D->lev_sum_acc / D->lev_count;
	  D->lev_sum_acc = 0;

	  D->lev_count = 0;
	}

/*
 * Optional bandpass filter before the mark/space discriminator.
 */

	if (D->use_prefilter) {
	  float cleaner;

	  push_sample (fsam, D->raw_cb, D->ms_filter_size);
	  cleaner = convolve (D->raw_cb, D->pre_filter, D->ms_filter_size);
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



// TODO:   How do we test for profile F here?

	if (0) {
	//if (toupper(modem.profiles[chan][subchan]) == toupper(FFF_PROFILE)) {

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

	  m_amp = D->lpf_iir * m_amp + (1.0 - D->lpf_iir) * D->m_amp_prev;
	  D->m_amp_prev = m_amp;

	  s_amp = D->lpf_iir * s_amp + (1.0 - D->lpf_iir) * D->s_amp_prev;
	  D->s_amp_prev = s_amp;
	}

/* 
 * Which tone is stronger?
 *
 * Under real conditions, we find that the higher tone has a
 * considerably smaller amplitude due to the passband characteristics
 * of the transmitter and receiver.  To make matters worse, it
 * varies considerably from one station to another.
 *
 * The two filters have different amounts of DC bias.
 *
 * Try to compensate for this by normalizing them separately with automatic gain
 * control (AGC). This works by looking at the minimum and maximum outputs
 * for each filter and scaling the results to be roughly in the -0.5 to +0.5 range.
 */

	/* Fast attack and slow decay. */
	/* Numbers were obtained by trial and error from actual */
	/* recorded less-than-optimal signals. */

	/* See agc.c and fsk_demod_agc.h for more information. */

	m_norm = agc (m_amp, D->agc_fast_attack, D->agc_slow_decay, &(D->m_peak), &(D->m_valley));
	s_norm = agc (s_amp, D->agc_fast_attack, D->agc_slow_decay, &(D->s_peak), &(D->s_valley));

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
	  demod_data = D->prev_demod_data;
	}


/*
 * Finally, a PLL is used to sample near the centers of the data bits.
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
	D->prev_d_c_pll = D->data_clock_pll;
	D->data_clock_pll += D->pll_step_per_sample;

	  //text_color_set(DW_COLOR_DEBUG);
	  // dw_printf ("prev = %lx, new data clock pll = %lx\n" D->prev_d_c_pll, D->data_clock_pll);

	if (D->data_clock_pll < 0 && D->prev_d_c_pll > 0) {

	  /* Overflow. */
#if SLICENDICE
	  hdlc_rec_bit_sam (chan, subchan, demod_data, demod_out);
#else
	  hdlc_rec_bit (chan, subchan, demod_data, 0, -1);
#endif
	}

        if (demod_data != D->prev_demod_data) {

	  // Note:  Test for this demodulator, not overall for channel.

	  if (hdlc_rec_data_detect_1 (chan, subchan)) {
	    D->data_clock_pll = (int)(D->data_clock_pll * D->pll_locked_inertia);
	  }
	  else {
	    D->data_clock_pll = (int)(D->data_clock_pll * D->pll_searching_inertia);
	  }
	}


#if DEBUG4

	if (chan == 0) {
	if (hdlc_rec_data_detect_1 (chan, subchan)) {
	  char fname[30];

	  
	  if (demod_log_fp == NULL) {
	    seq++;
	    sprintf (fname, "demod/%04d.csv", seq);
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


/*
 * Remember demodulator output so we can compare next time.
 */
	D->prev_demod_data = demod_data;


} /* end demod_afsk_process_sample */

#endif   /* GEN_FFF */


#if 0

/*-------------------------------------------------------------------
 *
 * Name:        fsk_demod_print_agc
 *
 * Purpose:     Print information about input signal amplitude.
 *		This will be useful for adjusting transmitter audio levels.
 *		We also want to avoid having an input level so high
 *		that the A/D converter "clips" the signal.
 *
 *
 * Inputs:	chan	- Audio channel.  0 for left, 1 for right.
 *
 * Returns:	None 
 *
 * Descripion:	Not sure what to use for final form.
 *		For now display the AGC peaks for both tones.
 *		This will be called at the end of a frame.
 *
 * Future:	Come up with a sensible scale and add command line option.
 *		Probably makes more sense to return a single number
 *		and let the caller print it.
 *		Just an experiment for now.
 *
 *--------------------------------------------------------------------*/

#if 0
void fsk_demod_print_agc (int chan, int subchan)
{

	struct demodulator_state_s *D;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	D = &demodulator_state[chan][subchan];

	dw_printf ("%d\n", (int)((D->lev_last_peak + D->lev_prev_peak)*50));



	//dw_printf ("Peak= %.2f, %.2f Ave= %.2f, %.2f AGC M= %.2f / %.2f S= %.2f / %.2f\n", 
	//	D->lev_last_peak, D->lev_prev_peak, D->lev_last_ave, D->lev_prev_ave,
	//	D->m_peak, D->m_valley, D->s_peak, D->s_valley);

}
#endif

/* Resulting scale is 0 to almost 100. */
/* Cranking up the input level produces no more than 97 or 98. */
/* We currently produce a message when this goes over 90. */

int fsk_demod_get_audio_level (int chan, int subchan) 
{
	struct demodulator_state_s *D;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	D = &demodulator_state[chan][subchan];

	return ( (int) ((D->lev_last_peak + D->lev_prev_peak) * 50 ) );
}




#endif   /* 0 */

/* end demod_afsk.c */

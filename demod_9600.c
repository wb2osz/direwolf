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


// #define DEBUG5 1	/* capture 9600 output to log files */


/*------------------------------------------------------------------
 *
 * Module:      demod_9600.c
 *
 * Purpose:   	Demodulator for scrambled baseband encoding.
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
#include "tune.h"
#include "fsk_demod_state.h"
#include "hdlc_rec.h"
#include "demod_9600.h"
#include "textcolor.h"
#include "dsp.h"


static float slice_point[MAX_SUBCHANS];


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

#if 0
	//  As suggested here,  http://locklessinc.com/articles/vectorize/
	//  Unfortunately, older compilers don't recognize it.

	//  Get more information by using -ftree-vectorizer-verbose=5

	float *d = __builtin_assume_aligned(data, 16);
	float *f = __builtin_assume_aligned(filter, 16);

#pragma GCC ivdep
	for (j=0; j<filter_size; j++) {
	    sum += f[j] * d[j];
	}
#else
#pragma GCC ivdep				// ignored until gcc 4.9
	for (j=0; j<filter_size; j++) {
	    sum += filter[j] * data[j];
	}
#endif
	return (sum);
}

/* Automatic gain control. */
/* Result should settle down to 1 unit peak to peak.  i.e. -0.5 to +0.5 */

__attribute__((hot)) __attribute__((always_inline))
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
 * Name:        demod_9600_init
 *
 * Purpose:     Initialize the 9600 baud demodulator.
 *
 * Inputs:      samples_per_sec	- Number of samples per second.
 *				Might be upsampled in hopes of 
 *				reducing the PLL jitter.
 *
 *		baud		- Data rate in bits per second.
 *
 *		D		- Address of demodulator state.
 *
 * Returns:     None
 *		
 *----------------------------------------------------------------*/

void demod_9600_init (int samples_per_sec, int baud, struct demodulator_state_s *D)
{	
	float fc;
	int j;

	memset (D, 0, sizeof(struct demodulator_state_s));
	D->num_slicers = 1;

	//dw_printf ("demod_9600_init(rate=%d, baud=%d, D ptr)\n", samples_per_sec, baud);

        D->pll_step_per_sample = 
		(int) round(TICKS_PER_PLL_CYCLE * (double) baud / (double)samples_per_sec);
	
	D->lp_filter_len_bits =  72 * 9600.0 / (44100.0 * 2.0);		
	D->lp_filter_size = (int) (( D->lp_filter_len_bits * (float)samples_per_sec / baud) + 0.5);
	D->lp_window = BP_WINDOW_HAMMING;
	D->lpf_baud = 0.59;	

	D->agc_fast_attack = 0.080;	
	D->agc_slow_decay = 0.00012;

	D->pll_locked_inertia = 0.88;
	D->pll_searching_inertia = 0.67;


#ifdef TUNE_LP_WINDOW
	D->lp_window = TUNE_LP_WINDOW;
#endif

#if TUNE_LP_FILTER_SIZE
	D->lp_filter_size = TUNE_LP_FILTER_SIZE;
#endif

#ifdef TUNE_LPF_BAUD
	D->lpf_baud = TUNE_LPF_BAUD;
#endif	

#ifdef TUNE_AGC_FAST
	D->agc_fast_attack = TUNE_AGC_FAST;
#endif

#ifdef TUNE_AGC_SLOW
	D->agc_slow_decay = TUNE_AGC_SLOW;
#endif

#if defined(TUNE_PLL_LOCKED) && defined(TUNE_PLL_SEARCHING)
	D->pll_locked_inertia = TUNE_PLL_LOCKED;
	D->pll_searching_inertia = TUNE_PLL_SEARCHING;
#endif

	fc = (float)baud * D->lpf_baud / (float)samples_per_sec;

	//dw_printf ("demod_9600_init: call gen_lowpass(fc=%.2f, , size=%d, )\n", fc, D->lp_filter_size);

	gen_lowpass (fc, D->lp_filter, D->lp_filter_size, D->lp_window);

	/* Version 1.2: Experiment with different slicing levels. */

	for (j = 0; j < MAX_SUBCHANS; j++) {
	  slice_point[j] = 0.02 * (j - 0.5 * (MAX_SUBCHANS-1));
	  //dw_printf ("slice_point[%d] = %+5.2f\n", j, slice_point[j]);
	}

} /* end fsk_demod_init */



/*-------------------------------------------------------------------
 *
 * Name:        demod_9600_process_sample
 *
 * Purpose:     (1) Filter & slice the signal.
 *		(2) Descramble it.
 *		(2) Recover clock and data.
 *
 * Inputs:	chan	- Audio channel.  0 for left, 1 for right.
 *
 *		sam	- One sample of audio.
 *			  Should be in range of -32768 .. 32767.
 *
 * Returns:	None 
 *
 * Descripion:	"9600 baud" packet is FSK for an FM voice transceiver.
 *		By the time it gets here, it's really a baseband signal.
 *		At one extreme, we could have a 4800 Hz square wave.
 *		A the other extreme, we could go a considerable number
 *		of bit times without any transitions.
 *
 *		The trick is to extract the digital data which has
 *		been distorted by going thru voice transceivers not
 *		intended to pass this sort of "audio" signal.
 *
 *		Data is "scrambled" to reduce the amount of DC bias.
 *		The data stream must be unscrambled at the receiving end.
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
 * References:	9600 Baud Packet Radio Modem Design
 *		http://www.amsat.org/amsat/articles/g3ruh/109.html
 *
 *		The KD2BD 9600 Baud Modem
 *		http://www.amsat.org/amsat/articles/kd2bd/9k6modem/
 *
 *		9600 Baud Packet Handbook
 * 		ftp://ftp.tapr.org/general/9600baud/96man2x0.txt
 *
 *
 *--------------------------------------------------------------------*/

static void inline nudge_pll (int chan, int subchan, int slice, int demod_data, struct demodulator_state_s *D);

__attribute__((hot))
void demod_9600_process_sample (int chan, int sam, struct demodulator_state_s *D)
{

	float fsam;
	//float abs_fsam;
	float amp;
	float demod_out;

#if DEBUG5
	static FILE *demod_log_fp = NULL;
	static int seq = 0;			/* for log file name */
#endif

	//int j;
	int subchan = 0;
	int demod_data;				/* Still scrambled. */


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);


/* 
 * Filters use last 'filter_size' samples.
 *
 * First push the older samples down. 
 *
 * Finally, put the most recent at the beginning.
 *
 * Future project?  Rather than shifting the samples,
 * it might be faster to add another variable to keep
 * track of the most recent sample and change the 
 * indexing in the later loops that multipy and add.
 */

	/* Scale to nice number for convenience. */
	/* Consistent with the AFSK demodulator, we'd like to use */
	/* only half of the dynamic range to have some headroom. */
	/* i.e.  input range +-16k becomes +-1 here and is */
	/* displayed in the heard line as audio level 100. */

	fsam = sam / 16384.0;

	push_sample (fsam, D->raw_cb, D->lp_filter_size);

/*
 * Low pass filter to reduce noise yet pass the data. 
 */

	amp = convolve (D->raw_cb, D->lp_filter, D->lp_filter_size);


/*
 * Version 1.2: Capture the post-filtering amplitude for display.
 * This is similar to the AGC without the normalization step.
 * We want decay to be substantially slower to get a longer
 * range idea of the received audio.
 * For AFSK, we keep mark and space amplitudes.
 * Here we keep + and - peaks because there could be a DC bias.
 */

	if (amp >= D->alevel_mark_peak) {
	  D->alevel_mark_peak = amp * D->quick_attack + D->alevel_mark_peak * (1. - D->quick_attack);
	}
	else {
	  D->alevel_mark_peak = amp * D->sluggish_decay + D->alevel_mark_peak * (1. - D->sluggish_decay);
	}

	if (amp <= D->alevel_space_peak) {
	  D->alevel_space_peak = amp * D->quick_attack + D->alevel_space_peak * (1. - D->quick_attack);
	}
	else {
	  D->alevel_space_peak = amp * D->sluggish_decay + D->alevel_space_peak * (1. - D->sluggish_decay);
	}

/* 
 * The input level can vary greatly.
 * More importantly, there could be a DC bias which we need to remove.
 *
 * Normalize the signal with automatic gain control (AGC). 
 * This works by looking at the minimum and maximum signal peaks
 * and scaling the results to be roughly in the -1.0 to +1.0 range.
 */

	demod_out = agc (amp, D->agc_fast_attack, D->agc_slow_decay, &(D->m_peak), &(D->m_valley));


// TODO: There is potential for multiple decoders with one filter.

//dw_printf ("peak=%.2f valley=%.2f amp=%.2f norm=%.2f\n", D->m_peak, D->m_valley, amp, norm);

	/* Throw in a little Hysteresis??? */
	/* (Not to be confused with Hysteria.) */
	/* Doesn't seem to have any value. */
	/* Using a level of .02 makes things worse. */
	/* Might want to experiment with this again someday. */


//	if (demod_out > 0.03) {
//	  demod_data = 1;
//	}
//	else if (demod_out < -0.03) {
//	  demod_data = 0;
//	} 
//	else {
//	  demod_data = D->slicer[subchan].prev_demod_data;
//	}

	if (D->num_slicers <= 1) {

	  /* Normal case of one demodulator to one HDLC decoder. */
	  /* Demodulator output is difference between response from two filters. */
	  /* AGC should generally keep this around -1 to +1 range. */

	  demod_data = demod_out > 0;
	  nudge_pll (chan, subchan, 0, demod_data, D);
	}
	else {
	  int slice;

	  /* Multiple slicers each feeding its own HDLC decoder. */

	  for (slice=0; slice<D->num_slicers; slice++) {
	    demod_data = demod_out > slice_point[slice];
	    nudge_pll (chan, subchan, slice, demod_data, D);
	  }
	}

} /* end demod_9600_process_sample */


__attribute__((hot))
static void inline nudge_pll (int chan, int subchan, int slice, int demod_data, struct demodulator_state_s *D)
{

/*
 * Next, a PLL is used to sample near the centers of the data bits.
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
 * I don't think the optimal value will depend on the audio sample rate
 * because this happens for each transition from the demodulator.
 *
 * This was optimized for 1200 baud AFSK.  There might be some opportunity
 * for improvement here.
 */

	D->slicer[slice].prev_d_c_pll = D->slicer[slice].data_clock_pll;
	D->slicer[slice].data_clock_pll += D->pll_step_per_sample;

	if (D->slicer[slice].data_clock_pll < 0 && D->slicer[slice].prev_d_c_pll > 0) {

	  /* Overflow. */

/*
 * At this point, we need to descramble the data as
 * in hardware based designs by G3RUH and K9NG.
 *
 * Future Idea:  allow unscrambled baseband data.
 *
 * http://www.amsat.org/amsat/articles/g3ruh/109/fig03.gif
 */
	  // Warning: 'descram' set but not used.
	  // It's used in conditional debug code below.
	  // descram =
	  descramble (demod_data, &(D->slicer[slice].lfsr));

	  hdlc_rec_bit (chan, subchan, slice, demod_data, 1, D->slicer[slice].lfsr);
	}

        if (demod_data != D->slicer[slice].prev_demod_data) {

	  // Note:  Test for this demodulator, not overall for channel.

	  if (hdlc_rec_gathering (chan, subchan, slice)) {
	    D->slicer[slice].data_clock_pll = (int)(D->slicer[slice].data_clock_pll * D->pll_locked_inertia);
	  }
	  else {
	    D->slicer[slice].data_clock_pll = (int)(D->slicer[slice].data_clock_pll * D->pll_searching_inertia);
	  }
	}


#if DEBUG5

	//if (chan == 0) {
	if (hdlc_rec_gathering (chan,subchan,slice)) {
	
	  char fname[30];

	  
	  if (demod_log_fp == NULL) {
	    seq++;
	    snprintf (fname, sizeof(fname), "demod96/%04d.csv", seq);
	    if (seq == 1) mkdir ("demod96"
#ifndef __WIN32__
					, 0777
#endif
						);

	    demod_log_fp = fopen (fname, "w");
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Starting 9600 decoder log file %s\n", fname);
	    fprintf (demod_log_fp, "Audio, Peak, Valley, Demod, SData, Descram, Clock\n");
	  }
	  fprintf (demod_log_fp, "%.3f, %.3f, %.3f, %.3f, %.2f, %.2f, %.2f\n", 
			0.5 * fsam + 3.5,  
			0.5 * D->m_peak + 3.5,
			0.5 * D->m_valley + 3.5,
			0.5 * demod_out + 2.0,
			demod_data ? 1.35 : 1.0,  
			descram ? .9 : .55,  
			(D->data_clock_pll & 0x80000000) ? .1 : .45);
	}
	else {
	  if (demod_log_fp != NULL) {
	    fclose (demod_log_fp);
	    demod_log_fp = NULL;
	  }
	}
	//}

#endif


/*
 * Remember demodulator output (pre-descrambling) so we can compare next time
 * for the DPLL sync.
 */
	D->slicer[slice].prev_demod_data = demod_data;

} /* end nudge_pll */





/* end demod_9600.c */

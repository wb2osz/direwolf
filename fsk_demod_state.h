/* fsk_demod_state.h */

#ifndef FSK_DEMOD_STATE_H

#include "rpack.h"


/*
 * Demodulator state.
 * Different copy is required for each channel & subchannel being processed concurrently.
 */


typedef enum bp_window_e { BP_WINDOW_TRUNCATED, 
				BP_WINDOW_COSINE, 
				BP_WINDOW_HAMMING,
				BP_WINDOW_BLACKMAN,
				BP_WINDOW_FLATTOP } bp_window_t;

struct demodulator_state_s
{
/*
 * These are set once during initialization.
 */

#define TICKS_PER_PLL_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

	int pll_step_per_sample;	// PLL is advanced by this much each audio sample.
					// Data is sampled when it overflows.


	int ms_filter_size;		/* Size of mark & space filters, in audio samples. */
					/* Started off as a guess of one bit length */
					/* but somewhat longer turned out to be better. */
					/* Currently using same size for any prefilter. */

#define MAX_FILTER_SIZE 320		/* 304 is needed for profile C, 300 baud & 44100. */

/*
 * FIR filter length relative to one bit time.
 * Use same for both bandpass and lowpass.
 */
	float filter_len_bits;

/* 
 * Window type for the mark/space filters.
 */
	bp_window_t bp_window;

/*
 * Alternate Low pass filters.
 * First is arbitrary number for quick IIR.
 * Second is frequency as ratio to baud rate for FIR.
 */
	int lpf_use_fir;		/* 0 for IIR, 1 for FIR. */
	float lpf_iir;
	float lpf_baud;

/*
 * Automatic gain control.  Fast attack and slow decay factors.
 */
	float agc_fast_attack;
	float agc_slow_decay;
/*
 * Hysteresis before final demodulator 0 / 1 decision.		
 */
	float hysteresis;

/* 
 * Phase Locked Loop (PLL) inertia.
 * Larger number means less influence by signal transitions.
 */
	float pll_locked_inertia;
	float pll_searching_inertia;
			

/*
 * Optional band pass pre-filter before mark/space detector.
 */
	int use_prefilter;	/* True to enable it. */

	float prefilter_baud;	/* Cutoff frequencies, as fraction of */
				/* baud rate, beyond tones used.  */
				/* Example, if we used 1600/1800 tones at */
				/* 300 baud, and this was 0.5, the cutoff */
				/* frequencies would be: */
				/* lower = min(1600,1800) - 0.5 * 300 = 1450 */
				/* upper = max(1600,1800) + 0.5 * 300 = 1950 */

	float pre_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));

/*
 * Kernel for the mark and space detection filters.
 */
					
	float m_sin_table[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	float m_cos_table[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	float s_sin_table[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	float s_cos_table[MAX_FILTER_SIZE] __attribute__((aligned(16)));

/*
 * The rest are continuously updated.
 */
	signed int data_clock_pll;		// PLL for data clock recovery.
						// It is incremented by pll_step_per_sample
						// for each audio sample.

	signed int prev_d_c_pll;		// Previous value of above, before
						// incrementing, to detect overflows.

/*
 * Most recent raw audio samples, before/after prefiltering.
 */
	float raw_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));

/*
 * Input to the mark/space detector.
 * Could be prefiltered or raw audio.
 */
	float ms_in_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));

/*
 * Outputs from the mark and space amplitude detection, 
 * used as inputs to the FIR lowpass filters.
 * Kernel for the lowpass filters.
 */

	int lp_filter_size;

	float m_amp_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	float s_amp_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	float lp_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));


	float m_peak, s_peak;
	float m_valley, s_valley;
	float m_amp_prev, s_amp_prev;

	int prev_demod_data;			// Previous data bit detected.
						// Used to look for transitions.


/* These are used only for "9600" baud data. */

	int lfsr;				// Descrambler shift register.


/* 
 * Finally, try to come up with some sort of measure of the audio input level. 
 * Let's try gathering both the peak and average of the 
 * absolute value of the input signal over some period such as 100 mS.
 * 
 */
	int lev_period;				// How many samples go into one measure.

	int lev_count;				// Number accumulated so far.

	float lev_peak_acc;			// Highest peak so far.

	float lev_sum_acc;			// Accumulated sum so far.

/*
 * These will be updated every 'lev_period' samples:
 */
	float lev_last_peak;
	float lev_last_ave;
	float lev_prev_peak;
	float lev_prev_ave;

/* 
 * Special for Rino decoder only.
 * One for each possible signal polarity.
 */

#if 1

	struct gr_state_s {

	  signed int data_clock_pll;		// PLL for data clock recovery.
						// It is incremented by pll_step_per_sample
						// for each audio sample.
  
	  signed int prev_d_c_pll;		// Previous value of above, before
						// incrementing, to detect overflows.

	  float gr_minus_peak;	// For automatic gain control.
	  float gr_plus_peak;

	  int gr_sync;		// Is sync pulse present?
	  int gr_prev_sync;	// Previous state to detect leading edge.

	  int gr_first_sample;	// Index of starting sample index for debugging.

	  int gr_dcd;		// Data carrier detect.  i.e. are we 
				// currently decoding a message.

	  float gr_early_sum;	// For averaging bit values in two regions.
	  int gr_early_count;
	  float gr_late_sum;
	  int gr_late_count;
	  float gr_sync_sum;
	  int gr_sync_count;

	  int gr_bit_count;	// Bit index into message.

	  struct rpack_s rpack;	// Collection of bits.

	} gr_state[2];
#endif

};

#define FSK_DEMOD_STATE_H 1
#endif
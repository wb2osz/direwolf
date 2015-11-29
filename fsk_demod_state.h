/* fsk_demod_state.h */

#ifndef FSK_DEMOD_STATE_H

#include "rpack.h"


/*
 * Demodulator state.
 * Different copy is required for each channel & subchannel being processed concurrently.
 */

// TODO1.2:  change prefix from BP_ to DSP_

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

	char profile;			// 'A', 'B', etc.	Upper case.
					// Only needed to see if we are using 'F' to take fast path.

#define TICKS_PER_PLL_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

	int pll_step_per_sample;	// PLL is advanced by this much each audio sample.
					// Data is sampled when it overflows.


	int ms_filter_size;		/* Size of mark & space filters, in audio samples. */
					/* Started off as a guess of one bit length */
					/* but somewhat longer turned out to be better. */
					/* Currently using same size for any prefilter. */

#define MAX_FILTER_SIZE 320		/* 304 is needed for profile C, 300 baud & 44100. */

/*
 * Filter length for Mark & Space in bit times.
 * e.g.  1 means 1/1200 second for 1200 baud.
 */
	float ms_filter_len_bits;

/* 
 * Window type for the various filters.
 */
	
	bp_window_t pre_window;
	bp_window_t ms_window;
	bp_window_t lp_window;


/*
 * Alternate Low pass filters.
 * First is arbitrary number for quick IIR.
 * Second is frequency as ratio to baud rate for FIR.
 */
	int lpf_use_fir;		/* 0 for IIR, 1 for FIR. */

	float lpf_iir;			/* Only if using IIR. */

	float lpf_baud;			/* Cutoff frequency as fraction of baud. */
					/* Intuitively we'd expect this to be somewhere */
					/* in the range of 0.5 to 1. */
					/* In practice, it turned out a little larger */
					/* for profiles B, C, D. */

	float lp_filter_len_bits;  	/* Length in number of bit times. */

	int lp_filter_size;		/* Size of Low Pass filter, in audio samples. */
					/* Previously it was always the same as the M/S */
					/* filters but in version 1.2 it's now independent. */

/*
 * Automatic gain control.  Fast attack and slow decay factors.
 */
	float agc_fast_attack;
	float agc_slow_decay;

/*
 * Use a longer term view for reporting signal levels.
 */
	float quick_attack;
	float sluggish_decay;

/*
 * Hysteresis before final demodulator 0 / 1 decision.		
 */
	float hysteresis;
	int num_slicers;		/* >1 for multiple slicers. */

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

	float pre_filter_len_bits;  /* Length in number of bit times. */

	int pre_filter_size;	/* Size of pre filter, in audio samples. */									

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

/*
 * Most recent raw audio samples, before/after prefiltering.
 */
	float raw_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));

/*
 * Use half of the AGC code to get a measure of input audio amplitude.
 * These use "quick" attack and "sluggish" decay while the 
 * AGC uses "fast" attack and "slow" decay.
 */

	float alevel_rec_peak;
	float alevel_rec_valley;
	float alevel_mark_peak;
	float alevel_space_peak;

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

	float m_amp_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	float s_amp_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	float lp_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));


	float m_peak, s_peak;
	float m_valley, s_valley;
	float m_amp_prev, s_amp_prev;

/*
 * For the PLL and data bit timing.
 * starting in version 1.2 we can have multiple slicers for one demodulator.
 * Each slicer has its own PLL and HDLC decoder.
 */

/*
 * Version 1.3: Clean up subchan vs. slicer.
 *
 * Originally some number of CHANNELS (originally 2, later 6)
 * which can have multiple parallel demodulators called SUB-CHANNELS.
 * This was originally for staggered frequencies for HF SSB.
 * It can also be used for multiple demodulators with the same
 * frequency but other differing parameters.
 * Each subchannel has its own demodulator and HDLC decoder.
 *
 * In version 1.2 we added multiple SLICERS.
 * The data structure, here, has multiple slicers per
 * demodulator (subchannel).  Due to fuzzy thinking or
 * expediency, the multiple slicers got mapped into subchannels.
 * This means we can't use both multiple decoders and
 * multiple slicers at the same time.
 *
 * Clean this up in 1.3 and keep the concepts separate.
 * This means adding a third variable many places
 * we are passing around the origin.
 *
 */
	struct {

		signed int data_clock_pll;		// PLL for data clock recovery.
							// It is incremented by pll_step_per_sample
							// for each audio sample.

		signed int prev_d_c_pll;		// Previous value of above, before
							// incrementing, to detect overflows.

		int prev_demod_data;			// Previous data bit detected.
							// Used to look for transitions.

		/* This is used only for "9600" baud data. */

		int lfsr;				// Descrambler shift register.

	} slicer [MAX_SLICERS];				// Actual number in use is num_slicers.
							// Should be in range 1 .. MAX_SLICERS,

/* 
 * Special for Rino decoder only.
 * One for each possible signal polarity.
 * The project showed promise but fell by the wayside.
 */

#if 0

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
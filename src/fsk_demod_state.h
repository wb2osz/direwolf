/* fsk_demod_state.h */

#ifndef FSK_DEMOD_STATE_H

#include <stdint.h>          // int64_t

#include "rpack.h"

#include "audio.h"		// for enum modem_t

/*
 * Demodulator state.
 * The name of the file is from we only had FSK.  Now we have other techniques.
 * Different copy is required for each channel & subchannel being processed concurrently.
 */

// TODO1.2:  change prefix from BP_ to DSP_

typedef enum bp_window_e { BP_WINDOW_TRUNCATED, 
				BP_WINDOW_COSINE, 
				BP_WINDOW_HAMMING,
				BP_WINDOW_BLACKMAN,
				BP_WINDOW_FLATTOP } bp_window_t;

// Experimental low pass filter to detect DC bias or low frequency changes.
// IIR behaves like an analog R-C filter.
// Intuitively, it seems like FIR would be better because it is based on a finite history.
// However, it would require MANY taps and a LOT of computation for a low frequency.
// We can use a little trick here to keep a running average.
// This would be equivalent to convolving with an array of all 1 values.
// That would eliminate the need to multiply.
// We can also eliminate the need to add them all up each time by keeping a running total.
// Add a sample to the total when putting it in our array of recent samples.
// Subtract it from the total when it gets pushed off the end.
// We can also eliminate the need to shift them all down by using a circular buffer.

#define CIC_LEN_MAX 4000

typedef struct cic_s {
	int len;		// Number of elements used.
				// Might want to dynamically allocate.
	short in[CIC_LEN_MAX];	// Samples coming in.
	int sum;		// Running sum.
	int inext;		// Next position to fill.
} cic_t;


#define MAX_FILTER_SIZE 480		/* 401 is needed for profile A, 300 baud & 44100. Revisit someday. */
					// Size comes out to 417 for 1200 bps with 48000 sample rate
					// v1.7 - Was 404.  Bump up to 480.

struct demodulator_state_s
{
/*
 * These are set once during initialization.
 */
	enum modem_t modem_type;		// MODEM_AFSK, MODEM_8PSK, etc.

//	enum v26_e v26_alt;			// Which alternative when V.26.

	char profile;			// 'A', 'B', etc.	Upper case.
					// Only needed to see if we are using 'F' to take fast path.

#define TICKS_PER_PLL_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

	int pll_step_per_sample;	// PLL is advanced by this much each audio sample.
					// Data is sampled when it overflows.


/* 
 * Window type for the various filters.
 */
	
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

	float lp_filter_width_sym;  	/* Length in number of symbol times. */

#define lp_filter_len_bits lp_filter_width_sym	// FIXME: temp hack

	int lp_filter_taps;		/* Size of Low Pass filter, in audio samples. */

#define lp_filter_size lp_filter_taps		// FIXME: temp hack


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
 * It is more resistant to change when locked on to a signal.
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

	float pre_filter_len_sym;  	// Length in number of symbol times.
#define pre_filter_len_bits pre_filter_len_sym 		// temp until all references changed.

	bp_window_t pre_window;		// Window type for filter shaping.

	int pre_filter_taps;		// Calculated number of filter taps.
#define pre_filter_size pre_filter_taps		// temp until all references changed.

	float pre_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	float raw_cb[MAX_FILTER_SIZE] __attribute__((aligned(16)));	// audio in,  need better name.

/*
 * The rest are continuously updated.
 */

	unsigned int lo_phase;	/* Local oscillator for PSK. */


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
 * Outputs from the mark and space amplitude detection, 
 * used as inputs to the FIR lowpass filters.
 * Kernel for the lowpass filters.
 */

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
							// Must be 32 bits!!!
							// So far, this is the case for every compiler used.

		signed int prev_d_c_pll;		// Previous value of above, before
							// incrementing, to detect overflows.

		int pll_symbol_count;			// Number symbols during time nudge_total is accumulated.
		int64_t pll_nudge_total;		// Sum of DPLL nudge amounts.
							// Both of these are cleared at start of frame.
							// At end of frame, we can see if incoming
							// baud rate is a little off.

		int prev_demod_data;			// Previous data bit detected.
							// Used to look for transitions.
		float prev_demod_out_f;

		/* This is used only for "9600" baud data. */

		int lfsr;				// Descrambler shift register.

		// This is for detecting phase lock to incoming signal.

		int good_flag;				// Set if transition is near where expected,
							// i.e. at a good time.
		int bad_flag;				// Set if transition is not where expected,
							// i.e. at a bad time.
		unsigned char good_hist;		// History of good transitions for past octet.
		unsigned char bad_hist;			// History of bad transitions for past octet.
		unsigned int score;			// History of whether good triumphs over bad
							// for past 32 symbols.
		int data_detect;			// True when locked on to signal.
		
	} slicer [MAX_SLICERS];				// Actual number in use is num_slicers.
							// Should be in range 1 .. MAX_SLICERS,
/*
 * Version 1.6:
 *
 *	This has become quite disorganized and messy with different combinations of
 *	fields used for different demodulator types.  Start to reorganize it into a common
 *	part (with things like the DPLL for clock recovery), and separate sections
 *	for each of the demodulator types.
 *	Still a lot to do here.
 */

	union {

//////////////////////////////////////////////////////////////////////////////////
//										//
//			AFSK only - new method in 1.7				//
//										//
//////////////////////////////////////////////////////////////////////////////////


	  struct afsk_only_s {

	    unsigned int m_osc_phase;		// Phase for Mark local oscillator.
	    unsigned int m_osc_delta;		// How much to change for each audio sample.

	    unsigned int s_osc_phase;		// Phase for Space local oscillator.
	    unsigned int s_osc_delta;		// How much to change for each audio sample.

	    unsigned int c_osc_phase;		// Phase for Center frequency local oscillator.
	    unsigned int c_osc_delta;		// How much to change for each audio sample.

	    // Need two mixers for profile "A".

	    float m_I_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	    float m_Q_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	    float s_I_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	    float s_Q_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	    // Only need one mixer for profile "B".  Reuse the same storage?

//#define c_I_raw m_I_raw
//#define c_Q_raw m_Q_raw
	    float c_I_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));
	    float c_Q_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	    int use_rrc;		// Use RRC rather than generic low pass.

	    float rrc_width_sym;	/* Width of RRC filter in number of symbols.  */

	    float rrc_rolloff;		/* Rolloff factor for RRC.  Between 0 and 1. */

	    float prev_phase;		// To see phase shift between samples for FM demod.

	    float normalize_rpsam;	// Normalize to -1 to +1 for expected tones.

	  } afsk;

//////////////////////////////////////////////////////////////////////////////////
//										//
//				Baseband only, AKA G3RUH			//
//										//
//////////////////////////////////////////////////////////////////////////////////

// TODO: Continue experiments with root raised cosine filter.
// Either switch to that or take out all the related stuff.

	  struct bb_only_s {

		float rrc_width_sym;		/* Width of RRC filter in number of symbols. */

		float rrc_rolloff;		/* Rolloff factor for RRC.  Between 0 and 1. */

		int rrc_filter_taps;		// Number of elements used in the next two.

// FIXME: TODO: reevaluate max size needed.

		float audio_in[MAX_FILTER_SIZE] __attribute__((aligned(16)));	// Audio samples in.


		float lp_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));	// Low pass filter.

		// New in 1.7 - Polyphase filter to reduce CPU requirements.

		float lp_polyphase_1[MAX_FILTER_SIZE] __attribute__((aligned(16)));
		float lp_polyphase_2[MAX_FILTER_SIZE] __attribute__((aligned(16)));
		float lp_polyphase_3[MAX_FILTER_SIZE] __attribute__((aligned(16)));
		float lp_polyphase_4[MAX_FILTER_SIZE] __attribute__((aligned(16)));

		float lp_1_iir_param;		// very low pass filters to get DC offset.
		float lp_1_out;

		float lp_2_iir_param;
		float lp_2_out;

		float agc_1_fast_attack;	// Signal envelope detection.
		float agc_1_slow_decay;
		float agc_1_peak;
		float agc_1_valley;

		float agc_2_fast_attack;
		float agc_2_slow_decay;
		float agc_2_peak;
		float agc_2_valley;

		float agc_3_fast_attack;
		float agc_3_slow_decay;
		float agc_3_peak;
		float agc_3_valley;

		// CIC low pass filters to detect DC bias or low frequency changes.
		// IIR behaves like an analog R-C filter.
		// Intuitively, it seems like FIR would be better because it is based on a finite history.
		// However, it would require MANY taps and a LOT of computation for a low frequency.
		// We can use a little trick here to keep a running average.
		// This would be equivalent to convolving with an array of all 1 values.
		// That would eliminate the need to multiply.
		// We can also eliminate the need to add them all up each time by keeping a running total.
		// Add a sample to the total when putting it in our array of recent samples.
		// Subtract it from the total when it gets pushed off the end.
		// We can also eliminate the need to shift them all down by using a circular buffer.
		// This only works with integers because float would have cumulated round off errors.

		cic_t cic_center1;
		cic_t cic_above;
		cic_t cic_below;

	  } bb;

//////////////////////////////////////////////////////////////////////////////////
//										//
//					PSK only.				//
//										//
//////////////////////////////////////////////////////////////////////////////////


	  struct psk_only_s {

		enum v26_e v26_alt;		// Which alternative when V.26.

		float sin_table256[256];	// Precomputed sin table for speed.

		
	// Optional band pass pre-filter before phase detector.

// TODO? put back into common section?
// TODO? Why was I thinking that?

		int use_prefilter;	// True to enable it.

		float prefilter_baud;	// Cutoff frequencies, as fraction of baud rate, beyond tones used.
					// In the case of PSK, we use only a single tone of 1800 Hz.
					// If we were using 2400 bps (= 1200 baud), this would be
					// the fraction of 1200 for the cutoff below and above 1800.


		float pre_filter_width_sym;  /* Length in number of symbol times. */

		int pre_filter_taps;	/* Size of pre filter, in audio samples. */									

		bp_window_t pre_window;

		float audio_in[MAX_FILTER_SIZE] __attribute__((aligned(16)));
		float pre_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	// Use local oscillator or correlate with previous sample.

		int psk_use_lo;		/* Use local oscillator rather than self correlation. */

		unsigned int lo_step;	/* How much to advance the local oscillator */
					/* phase for each audio sample. */

		unsigned int lo_phase;	/* Local oscillator phase accumulator for PSK. */
	
		// After mixing with LO before low pass filter.

		float I_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));	// signal * LO cos.
		float Q_raw[MAX_FILTER_SIZE] __attribute__((aligned(16)));	// signal * LO sin.

		// Number of delay line taps into previous symbol.
		// They are one symbol period and + or - 45 degrees of the carrier frequency.

		int boffs;		/* symbol length based on sample rate and baud. */
		int coffs;		/* to get cos component of previous symbol. */
		int soffs;		/* to get sin component of previous symbol. */

		float delay_line_width_sym;
		int delay_line_taps;	// In audio samples.

		float delay_line[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	// Low pass filter Second is frequency as ratio to baud rate for FIR.

// TODO? put back into common section?
// TODO? What are the tradeoffs?
		float lpf_baud;			/* Cutoff frequency as fraction of baud. */
						/* Intuitively we'd expect this to be somewhere */
						/* in the range of 0.5 to 1. */

		float lp_filter_width_sym;  	/* Length in number of symbol times. */

		int lp_filter_taps;		/* Size of Low Pass filter, in audio samples (i.e. filter taps). */

		bp_window_t lp_window;

		float lp_filter[MAX_FILTER_SIZE] __attribute__((aligned(16)));

	  } psk;

	} u;	// end of union for different demodulator types.

};


/*-------------------------------------------------------------------
 *
 * Name:        pll_dcd_signal_transition2
 *		dcd_each_symbol2
 *
 * Purpose:     New DCD strategy for 1.6.
 *
 * Inputs:	D		Pointer to demodulator state.
 *
 *		chan		Radio channel: 0 to MAX_CHANS - 1	
 *
 *		subchan		Which of multiple demodulators: 0 to MAX_SUBCHANS - 1
 *
 *		slice		Slicer number: 0 to MAX_SLICERS - 1.
 *
 *		dpll_phase	Signed 32 bit counter for DPLL phase.
 *				Wraparound is where data is sampled.
 *				Ideally transitions would occur close to 0.
 *				
 * Output:	D->slicer[slice].data_detect - true when PLL is locked to incoming signal.
 *
 * Description:	From the beginning, DCD was based on finding several flag octets
 *		in a row and dropping when eight bits with no transitions.
 *		It was less than ideal but we limped along with it all these years.
 *		This fell apart when FX.25 came along and a couple of the
 *		correlation tags have eight "1" bits in a row.
 *
 * 		Our new strategy is to keep a running score of how well demodulator
 *		output transitions match to where expected.
 *
 *--------------------------------------------------------------------*/

#include "hdlc_rec.h"        // for dcd_change

// These are good for 1200 bps AFSK.
// Might want to override for other modems.

#ifndef DCD_THRESH_ON
#define DCD_THRESH_ON 30		// Hysteresis: Can miss 2 out of 32 for detecting lock.
					// 31 is best for TNC Test CD.  30 almost as good.
					// 30 better for 1200 regression test.
#endif

#ifndef DCD_THRESH_OFF
#define DCD_THRESH_OFF 6		// Might want a little more fine tuning.
#endif

#ifndef DCD_GOOD_WIDTH
#define DCD_GOOD_WIDTH 512		// No more than 1024!!!
#endif

__attribute__((always_inline))
inline static void pll_dcd_signal_transition2 (struct demodulator_state_s *D, int slice, int dpll_phase)
{
	if (dpll_phase > - DCD_GOOD_WIDTH * 1024 * 1024 && dpll_phase < DCD_GOOD_WIDTH * 1024 * 1024) {
	  D->slicer[slice].good_flag = 1;
	}
	else {
	  D->slicer[slice].bad_flag = 1;
	}
}

__attribute__((always_inline))
inline static void pll_dcd_each_symbol2 (struct demodulator_state_s *D, int chan, int subchan, int slice)
{
	D->slicer[slice].good_hist <<= 1;
	D->slicer[slice].good_hist |= D->slicer[slice].good_flag;
	D->slicer[slice].good_flag = 0;

	D->slicer[slice].bad_hist <<= 1;
	D->slicer[slice].bad_hist |= D->slicer[slice].bad_flag;
	D->slicer[slice].bad_flag = 0;

	D->slicer[slice].score <<= 1;
	// 2 is to detect 'flag' patterns with 2 transitions per octet.
	D->slicer[slice].score |= (signed)__builtin_popcount(D->slicer[slice].good_hist)
					- (signed)__builtin_popcount(D->slicer[slice].bad_hist) >= 2;

	int s = __builtin_popcount(D->slicer[slice].score);
	if (s >= DCD_THRESH_ON) {
	  if (D->slicer[slice].data_detect == 0) {
	    D->slicer[slice].data_detect = 1;
	    dcd_change (chan, subchan, slice, D->slicer[slice].data_detect);
	  }
	}
	else if (s <= DCD_THRESH_OFF) {
	  if (D->slicer[slice].data_detect != 0) {
	    D->slicer[slice].data_detect = 0;
	    dcd_change (chan, subchan, slice, D->slicer[slice].data_detect);
	  }
	}
}


#define FSK_DEMOD_STATE_H 1
#endif

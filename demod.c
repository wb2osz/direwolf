//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
// 
//    Copyright (C) 2011,2012,2013  John Langner, WB2OSZ
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
 * Module:      demod.c
 *
 * Purpose:   	Common entry point for multiple types of demodulators.
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
#include "demod.h"
#include "tune.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "fsk_fast_filter.h"
#include "hdlc_rec.h"
#include "textcolor.h"
#include "demod_9600.h"
#include "demod_afsk.h"



// Properties of the radio channels.

static struct audio_s modem;

// Current state of all the decoders.

static struct demodulator_state_s demodulator_state[MAX_CHANS][MAX_SUBCHANS];


#define UPSAMPLE 2

static int sample_sum[MAX_CHANS][MAX_SUBCHANS];
static int sample_count[MAX_CHANS][MAX_SUBCHANS];


/*------------------------------------------------------------------
 *
 * Name:        demod_init
 *
 * Purpose:     Initialize the demodulator(s) used for reception.
 *
 * Inputs:      pa		- Pointer to modem_s structure with
 *				  various parameters for the modem(s).
 *
 * Returns:     0 for success, -1 for failure.
 *		
 *
 * Bugs:	This doesn't do much error checking so don't give it
 *		anything crazy.
 *
 *----------------------------------------------------------------*/

int demod_init (struct audio_s *pa)
{
	int j;
	int chan;		/* Loop index over number of radio channels. */
	int subchan;		/* for each modem for channel. */
	char profile;
	//float fc;

	struct demodulator_state_s *D;


/*
 * Save parameters for later use.
 */
	memcpy (&modem, pa, sizeof(modem));

	for (chan = 0; chan < modem.num_channels; chan++) {

	  assert (chan >= 0 && chan < MAX_CHANS);

	  switch (modem.modem_type[chan]) {

	    case AFSK:
/*
 * Pick a good default demodulator if none specified. 
 */
	      if (strlen(modem.profiles[chan]) == 0) {

	        if (modem.baud[chan] < 600) {

	          /* This has been optimized for 300 baud. */

	          strcpy (modem.profiles[chan], "D");
		  if (modem.samples_per_sec > 40000) {
		    modem.decimate[chan] = 3;
		  }
	        }
	        else {
#if __arm__
	          /* We probably don't have a lot of CPU power available. */

	          if (modem.baud[chan] == FFF_BAUD &&
		      modem.mark_freq[chan] == FFF_MARK_FREQ && 
		      modem.space_freq[chan] == FFF_SPACE_FREQ &&
		      modem.samples_per_sec == FFF_SAMPLES_PER_SEC) {

	            modem.profiles[chan][0] = FFF_PROFILE;
	            modem.profiles[chan][1] = '\0';
	          }
	          else {
	            strcpy (modem.profiles[chan], "A");
	          }
#else
	          strcpy (modem.profiles[chan], "C");
#endif
	        }
	      }

	      if (modem.decimate[chan] == 0) modem.decimate[chan] = 1;

	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d baud, AFSK %d & %d Hz, %s, %d sample rate",
		    chan, modem.baud[chan], 
		    modem.mark_freq[chan], modem.space_freq[chan],
		    modem.profiles[chan],
		    modem.samples_per_sec);
	      if (modem.decimate[chan] != 1) 
	        dw_printf (" / %d", modem.decimate[chan]);
	      dw_printf (".\n");

	      if (strlen(modem.profiles[chan]) > 1) {

/*
 * Multiple profiles, usually for 1200 baud.
 */
	        assert (modem.num_subchan[chan] == strlen(modem.profiles[chan]));
	  
	        for (subchan = 0; subchan < modem.num_subchan[chan]; subchan++) {

	          int mark, space;
	          assert (subchan >= 0 && subchan < MAX_SUBCHANS);
	          D = &demodulator_state[chan][subchan];

	          profile = modem.profiles[chan][subchan];
	          mark = modem.mark_freq[chan];
	          space = modem.space_freq[chan];

	          if (modem.num_subchan[chan] != 1) {
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("        %d.%d: %c %d & %d\n", chan, subchan, profile, mark, space);
	          }
      
	          demod_afsk_init (modem.samples_per_sec / modem.decimate[chan], modem.baud[chan],
			    mark, space,
			    profile,
			    D);
	        }
	      }
	      else {
/*
 * Possibly multiple frequency pairs.
 */
	  
	        assert (modem.num_freq[chan] == modem.num_subchan[chan]);
	        assert (strlen(modem.profiles[chan]) == 1);

	        for (subchan = 0; subchan < modem.num_freq[chan]; subchan++) {

	          int mark, space, k;
	          assert (subchan >= 0 && subchan < MAX_SUBCHANS);
	          D = &demodulator_state[chan][subchan];

	          profile = modem.profiles[chan][0];

	          k = subchan * modem.offset[chan] - ((modem.num_subchan[chan] - 1) * modem.offset[chan]) / 2;
	          mark = modem.mark_freq[chan] + k;
	          space = modem.space_freq[chan] + k;

	          if (modem.num_subchan[chan] != 1) {
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("        %d.%d: %c %d & %d\n", chan, subchan, profile, mark, space);
	          }
      
	          demod_afsk_init (modem.samples_per_sec / modem.decimate[chan], modem.baud[chan],
			mark, space,
			profile,
			D);

	        } 	  /* for subchan */
	      }	
	      break;

	    default:	

	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d baud, %d sample rate x %d.\n",
		    chan, modem.baud[chan], 
		    modem.samples_per_sec, UPSAMPLE);

	      subchan = 0;
	      D = &demodulator_state[chan][subchan];

	      demod_9600_init (UPSAMPLE * modem.samples_per_sec, modem.baud[chan], D);

	      break;

	  }  /* switch on modulation type. */
    
	}     /* for chan ... */



	for (chan=0; chan<MAX_CHANS; chan++) 
	{
	  for (subchan = 0; subchan < modem.num_subchan[chan]; subchan++) {
	    struct demodulator_state_s *D;

	    assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	    sample_sum[chan][subchan] = 0;
	    sample_count[chan][subchan] = subchan;	/* stagger */

	    D = &demodulator_state[chan][subchan];

/* For collecting input signal level. */

	    D->lev_period = modem.samples_per_sec * 0.100;  // Samples in 0.100 seconds.

	  }
	}

        return (0);

} /* end demod_init */



/*------------------------------------------------------------------
 *
 * Name:        demod_get_sample
 *
 * Purpose:     Get one audio sample fromt the sound input source.
 *
 * Returns:     -32768 .. 32767 for a valid audio sample.
 *              256*256 for end of file or other error.
 *
 * Global In:	modem.bits_per_sample - So we know whether to 
 *			read 1 or 2 bytes from audio stream.
 *
 * Description:	Grab 1 or two btyes depending on data source.
 *
 *		When processing stereo, the caller will call this
 *		at twice the normal rate to obtain alternating left 
 *		and right samples.
 *
 *----------------------------------------------------------------*/

#define FSK_READ_ERR (256*256)


__attribute__((hot))
int demod_get_sample (void)
{
	int x1, x2;
	signed short sam;	/* short to force sign extention. */


	assert (modem.bits_per_sample == 8 || modem.bits_per_sample == 16);


	if (modem.bits_per_sample == 8) {

	  x1 = audio_get();	
	  if (x1 < 0) return(FSK_READ_ERR);

	  assert (x1 >= 0 && x1 <= 255);

	  /* Scale 0..255 into -32k..+32k */

	  sam = (x1 - 128) * 256;

	}
	else {
	  x1 = audio_get();	/* lower byte first */
	  if (x1 < 0) return(FSK_READ_ERR);

	  x2 = audio_get();
	  if (x2 < 0) return(FSK_READ_ERR);

	  assert (x1 >= 0 && x1 <= 255);
	  assert (x2 >= 0 && x2 <= 255);

          sam = ( x2 << 8 ) | x1;
	}

	return (sam);
}


/*-------------------------------------------------------------------
 *
 * Name:        demod_process_sample
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
void demod_process_sample (int chan, int subchan, int sam)
{
	float fsam, abs_fsam;
	int k;


#if DEBUG4
	static FILE *demod_log_fp = NULL;
	static int seq = 0;			/* for log file name */
#endif

	int j;
	int demod_data;
	struct demodulator_state_s *D;

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	D = &demodulator_state[chan][subchan];


#if 1	/* TODO:  common level detection. */

	/* Scale to nice number, TODO: range -1.0 to +1.0, not 2. */

	fsam = sam / 16384.0;

/*
 * Accumulate measure of the input signal level.
 */
	abs_fsam = fsam >= 0 ? fsam : -fsam;
               
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

#endif

/*
 * Select decoder based on modulation type.
 */

	switch (modem.modem_type[chan]) {

	  case AFSK:

	    if (modem.decimate[chan] > 1) {

	      sample_sum[chan][subchan] += sam;
	      sample_count[chan][subchan]++;
	      if (sample_count[chan][subchan] >= modem.decimate[chan]) {
  	        demod_afsk_process_sample (chan, subchan, sample_sum[chan][subchan] / modem.decimate[chan], D);
	        sample_sum[chan][subchan] = 0;
	        sample_count[chan][subchan] = 0;
	      }
	    }
	    else {
  	      demod_afsk_process_sample (chan, subchan, sam, D);
	    }
	    break;

	  default:

#define ZEROSTUFF 1

	
#if ZEROSTUFF
	    /* Literature says this is better if followed */
	    /* by appropriate low pass filter. */
	    /* So far, both are same in tests with different */
	    /* optimal low pass filter parameters. */

	    for (k=1; k<UPSAMPLE; k++) {
	      demod_9600_process_sample (chan, 0, D);
	    }
	    demod_9600_process_sample (chan, sam*UPSAMPLE, D);
#else
	    /* Linear interpolation. */
	    static int prev_sam;
	    switch (UPSAMPLE) {
	      case 1:
	        demod_9600_process_sample (chan, sam);

	        break;
	      case 2:
	        demod_9600_process_sample (chan, (prev_sam + sam) / 2, D);
	        demod_9600_process_sample (chan, sam, D);
	        break;
              case 3:
                demod_9600_process_sample (chan, (2 * prev_sam + sam) / 3, D);
                demod_9600_process_sample (chan, (prev_sam + 2 * sam) / 3, D);
                demod_9600_process_sample (chan, sam, D);
                break;
              case 4:
                demod_9600_process_sample (chan, (3 * prev_sam + sam) / 4, D);
                demod_9600_process_sample (chan, (prev_sam + sam) / 2, D);
                demod_9600_process_sample (chan, (prev_sam + 3 * sam) / 4, D);
                demod_9600_process_sample (chan, sam, D);
                break;
              default:
                assert (0);
                break;
	    }
	    prev_sam = sam;
#endif
	    break;
	}
	return;

} /* end demod_process_sample */




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
void demod_print_agc (int chan, int subchan)
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

int demod_get_audio_level (int chan, int subchan) 
{
	struct demodulator_state_s *D;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	D = &demodulator_state[chan][subchan];

	return ( (int) ((D->lev_last_peak + D->lev_prev_peak) * 50 ) );
}


/* end demod.c */

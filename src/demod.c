//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
// 
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2019, 2021  John Langner, WB2OSZ
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
 * Module:      demod.c
 *
 * Purpose:   	Common entry point for multiple types of demodulators.
 *		
 * Input:	Audio samples from either a file or the "sound card."
 *
 * Outputs:	Calls hdlc_rec_bit() for each bit demodulated.  
 *
 *---------------------------------------------------------------*/

#define DEMOD_C 1

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "audio.h"
#include "demod.h"
#include "tune.h"
#include "fsk_demod_state.h"
#include "fsk_gen_filter.h"
#include "hdlc_rec.h"
#include "textcolor.h"
#include "demod_9600.h"
#include "demod_afsk.h"
#include "demod_psk.h"



// Properties of the radio channels.

static struct audio_s          *save_audio_config_p;



// Current state of all the decoders.

static struct demodulator_state_s demodulator_state[MAX_RADIO_CHANS][MAX_SUBCHANS];


static int sample_sum[MAX_RADIO_CHANS][MAX_SUBCHANS];
static int sample_count[MAX_RADIO_CHANS][MAX_SUBCHANS];


/*------------------------------------------------------------------
 *
 * Name:        demod_init
 *
 * Purpose:     Initialize the demodulator(s) used for reception.
 *
 * Inputs:      pa		- Pointer to audio_s structure with
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
	int chan;		/* Loop index over number of radio channels. */
	char profile;
	


/*
 * Save audio configuration for later use.
 */

	save_audio_config_p = pa;

	for (chan = 0; chan < MAX_RADIO_CHANS; chan++) {

	 if (save_audio_config_p->chan_medium[chan] == MEDIUM_RADIO) {

	  char *p;
	  char just_letters[16];
	  int num_letters;
	  int have_plus;

	  /*
	   * These are derived from config file parameters.
	   *
	   * num_subchan is number of demodulators.
	   * This can be increased by:
	   *	Multiple frequencies.
	   *	Multiple letters (not sure if I will continue this).
	   *
	   * num_slicers is set to max by the "+" option.
	   */

	  save_audio_config_p->achan[chan].num_subchan = 1;
	  save_audio_config_p->achan[chan].num_slicers = 1;

	  switch (save_audio_config_p->achan[chan].modem_type) {

	    case MODEM_OFF:
	      break;

	    case MODEM_AFSK:
	    case MODEM_EAS:

	      if (save_audio_config_p->achan[chan].modem_type == MODEM_EAS) {
		if (save_audio_config_p->achan[chan].fix_bits != RETRY_NONE) {
	          text_color_set(DW_COLOR_INFO);
		  dw_printf ("Channel %d: FIX_BITS option has been turned off for EAS.\n", chan);
	          save_audio_config_p->achan[chan].fix_bits = RETRY_NONE;
	        }
		if (save_audio_config_p->achan[chan].passall != 0) {
	          text_color_set(DW_COLOR_INFO);
		  dw_printf ("Channel %d: PASSALL option has been turned off for EAS.\n", chan);
	          save_audio_config_p->achan[chan].passall = 0;
	        }
	      }

/*
 * Tear apart the profile and put it back together in a normalized form:
 *	- At least one letter, supply suitable default if necessary.
 *	- Upper case only.
 *	- Any plus will be at the end.
 */
	      num_letters = 0;
	      just_letters[num_letters] = '\0';
	      have_plus = 0;
	      for (p = save_audio_config_p->achan[chan].profiles; *p != '\0'; p++) {

	        if (islower(*p)) {
	          just_letters[num_letters] = toupper(*p);
	          num_letters++;
	          just_letters[num_letters] = '\0';
	        }

	        else if (isupper(*p)) {
	          just_letters[num_letters] = *p;
	          num_letters++;
	          just_letters[num_letters] = '\0';
	        }

	        else if (*p == '+') {
	          have_plus = 1;
	          if (p[1] != '\0') {
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("Channel %d: + option must appear at end of demodulator types \"%s\" \n", 
					chan, save_audio_config_p->achan[chan].profiles);
		  }	    
	        }

	        else if (*p == '-') {
	          have_plus = -1;
	          if (p[1] != '\0') {
		    text_color_set(DW_COLOR_ERROR);
		    dw_printf ("Channel %d: - option must appear at end of demodulator types \"%s\" \n", 
					chan, save_audio_config_p->achan[chan].profiles);
		  }	
    
	        } else {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Channel %d: Demodulator types \"%s\" can contain only letters and + - characters.\n", 
					chan, save_audio_config_p->achan[chan].profiles);
	        }
	      }

	      assert (num_letters == (int)(strlen(just_letters)));

/*
 * Pick a good default demodulator if none specified.
 * Previously, we had "D" optimized for 300 bps.
 * Gone in 1.7 so it is always "A+".
 */
	      if (num_letters == 0) {
	        strlcpy (just_letters, "A", sizeof(just_letters));
	        num_letters = strlen(just_letters);

	        if (have_plus != -1) have_plus = 1;		// Add as default for version 1.2
								// If not explicitly turned off.
	      }

/*
 * Special case for ARM.
 * The higher end ARM chips have loads of power but many people
 * are using a single core Pi Zero or similar.
 * (I'm still using a model 1 for my digipeater/IGate!)
 * Decreasing CPU requirement has a negligible impact on decoding performance.
 *
 * 	atest -PA- 01_Track_1.wav		--> 1002 packets decoded.
 * 	atest -PA- -D3 01_Track_1.wav		--> 997 packets decoded.
 *
 * Someone concerned about 1/2 of one percent difference can add "-D 1"
 */
#if __arm__
	      if (save_audio_config_p->achan[chan].decimate == 0) {
	        if (save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec > 40000) {
	          save_audio_config_p->achan[chan].decimate = 3;
	        }
	      }
#endif

/*
 * Number of filter taps is proportional to number of audio samples in a "symbol" duration.
 * These can get extremely large for low speeds, e.g. 300 baud.
 * In this case, increase the decimation ration.  Crude approximation. Could be improved.
 */
	      if (save_audio_config_p->achan[chan].decimate == 0 &&
		  save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec > 40000 &&
	          save_audio_config_p->achan[chan].baud < 600) {

		// Avoid enormous number of filter taps.

	        save_audio_config_p->achan[chan].decimate = 3;
	      }


/*
 * Put it back together again.
 */
	      assert (num_letters == (int)(strlen(just_letters)));

		/* At this point, have_plus can have 3 values: */
		/* 	1 = turned on, either explicitly or by applied default */
		/*	-1 = explicitly turned off.  change to 0 here so it is false. */
		/* 	0 = off by default. */

	      if (have_plus == -1) have_plus = 0;

	      strlcpy (save_audio_config_p->achan[chan].profiles, just_letters, sizeof(save_audio_config_p->achan[chan].profiles));
	      
	      assert (strlen(save_audio_config_p->achan[chan].profiles) >= 1);

	      if (have_plus) {
	        strlcat (save_audio_config_p->achan[chan].profiles, "+", sizeof(save_audio_config_p->achan[chan].profiles));
	      }

	      /* These can be increased later for the multi-frequency case. */

	      save_audio_config_p->achan[chan].num_subchan = num_letters;
	      save_audio_config_p->achan[chan].num_slicers = 1;

/*
 * Some error checking - Can use only one of these:
 *
 *	- Multiple letters.
 *	- New + multi-slicer.
 *	- Multiple frequencies.
 */

	      if (have_plus && save_audio_config_p->achan[chan].num_freq > 1) {

		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Channel %d: Demodulator + option can't be combined with multiple frequencies.\n", chan);
	          save_audio_config_p->achan[chan].num_subchan = 1;	// Will be set higher later.
	          save_audio_config_p->achan[chan].num_freq = 1;
	      }

	      if (num_letters > 1 && save_audio_config_p->achan[chan].num_freq > 1) {

		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Channel %d: Multiple demodulator types can't be combined with multiple frequencies.\n", chan);

	          save_audio_config_p->achan[chan].profiles[1] = '\0';
		  num_letters = 1;
	      }

	      if (save_audio_config_p->achan[chan].decimate == 0) {
	        save_audio_config_p->achan[chan].decimate = 1;
		if (strchr (just_letters, 'B') != NULL && save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec > 40000) {
		  save_audio_config_p->achan[chan].decimate = 3;
		}
	      }

	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d baud, AFSK %d & %d Hz, %s, %d sample rate",
		    chan, save_audio_config_p->achan[chan].baud, 
		    save_audio_config_p->achan[chan].mark_freq, save_audio_config_p->achan[chan].space_freq,
		    save_audio_config_p->achan[chan].profiles,
		    save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec);
	      if (save_audio_config_p->achan[chan].decimate != 1) 
	        dw_printf (" / %d", save_audio_config_p->achan[chan].decimate);
	      dw_printf (", Tx %s", layer2_tx[(int)(save_audio_config_p->achan[chan].layer2_xmit)]);
	      if (save_audio_config_p->achan[chan].dtmf_decode != DTMF_DECODE_OFF) 
	        dw_printf (", DTMF decoder enabled");
	      dw_printf (".\n");


/* 
 * Initialize the demodulator(s).
 *
 * We have 3 cases to consider.
 */

// TODO1.3: revisit this logic now that it is less restrictive.

	      if (num_letters > 1) {
	        int d;

/*	
 * Multiple letters, usually for 1200 baud.
 * Each one corresponds to a demodulator and subchannel.
 *
 * An interesting experiment but probably not too useful.
 * Can't have multiple frequency pairs.
 * In version 1.3 this can be combined with the + option.
 */

	        save_audio_config_p->achan[chan].num_subchan = num_letters;
		
		if (save_audio_config_p->achan[chan].num_subchan != num_letters) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, num_subchan(%d) != strlen(\"%s\")\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].num_subchan, save_audio_config_p->achan[chan].profiles);
		}

	        if (save_audio_config_p->achan[chan].num_freq != 1) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, num_freq(%d) != 1\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].num_freq);
		}

	        for (d = 0; d < save_audio_config_p->achan[chan].num_subchan; d++) {
	          int mark, space;
	          assert (d >= 0 && d < MAX_SUBCHANS);

	          struct demodulator_state_s *D;
	          D = &demodulator_state[chan][d];

	          profile = save_audio_config_p->achan[chan].profiles[d];
	          mark = save_audio_config_p->achan[chan].mark_freq;
	          space = save_audio_config_p->achan[chan].space_freq;

	          if (save_audio_config_p->achan[chan].num_subchan != 1) {
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("        %d.%d: %c %d & %d\n", chan, d, profile, mark, space);
	          }

	          demod_afsk_init (save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].decimate, 
			    save_audio_config_p->achan[chan].baud,
		            mark, 
	                    space,
			    profile,
			    D);

	          if (have_plus) {
		    /* I'm not happy about putting this hack here. */
		    /* should pass in as a parameter rather than adding on later. */

	            save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
		    D->num_slicers = MAX_SLICERS;
	          }

	          /* For signal level reporting, we want a longer term view. */
		  // TODO: Should probably move this into the init functions.

	          D->quick_attack = D->agc_fast_attack * 0.2f;
	          D->sluggish_decay = D->agc_slow_decay * 0.2f;
	        }
	      }
	      else if (have_plus) {
	       
/*
 * PLUS - which (formerly) implies we have only one letter and one frequency pair.
 *
 * One demodulator feeds multiple slicers, each a subchannel.
 */

	        if (num_letters != 1) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, strlen(\"%s\") != 1\n",
				__FILE__, __LINE__, chan, just_letters);
		}

	        if (save_audio_config_p->achan[chan].num_freq != 1) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, num_freq(%d) != 1\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].num_freq);
		}

	        if (save_audio_config_p->achan[chan].num_freq != save_audio_config_p->achan[chan].num_subchan) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, num_freq(%d) != num_subchan(%d)\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].num_freq, save_audio_config_p->achan[chan].num_subchan);
		}

	        struct demodulator_state_s *D;
	        D = &demodulator_state[chan][0];

		/* I'm not happy about putting this hack here. */
		/* This belongs in demod_afsk_init but it doesn't have access to the audio config. */

	        save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
     
	        demod_afsk_init (save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].decimate, 
			save_audio_config_p->achan[chan].baud,
			save_audio_config_p->achan[chan].mark_freq, 
	                save_audio_config_p->achan[chan].space_freq,
			save_audio_config_p->achan[chan].profiles[0],
			D);

	        if (have_plus) {
		  /* I'm not happy about putting this hack here. */
		  /* should pass in as a parameter rather than adding on later. */

	          save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
		  D->num_slicers = MAX_SLICERS;
	        }

	        /* For signal level reporting, we want a longer term view. */

	        D->quick_attack = D->agc_fast_attack * 0.2f;
	        D->sluggish_decay = D->agc_slow_decay * 0.2f;
	      }	
	      else {
	        int d;
/*
 * One letter.
 * Can be combined with multiple frequencies.
 */

	        if (num_letters != 1) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("INTERNAL ERROR, %s:%d, chan=%d, strlen(\"%s\") != 1\n",
				__FILE__, __LINE__, chan, save_audio_config_p->achan[chan].profiles);
		}

	        save_audio_config_p->achan[chan].num_subchan = save_audio_config_p->achan[chan].num_freq;

	        for (d = 0; d < save_audio_config_p->achan[chan].num_freq; d++) {

	          int mark, space, k;
	          assert (d >= 0 && d < MAX_SUBCHANS);

	          struct demodulator_state_s *D;
	          D = &demodulator_state[chan][d];

	          profile = save_audio_config_p->achan[chan].profiles[0];

	          k = d * save_audio_config_p->achan[chan].offset - ((save_audio_config_p->achan[chan].num_freq - 1) * save_audio_config_p->achan[chan].offset) / 2;
	          mark = save_audio_config_p->achan[chan].mark_freq + k;
	          space = save_audio_config_p->achan[chan].space_freq + k;

	          if (save_audio_config_p->achan[chan].num_freq != 1) {
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("        %d.%d: %c %d & %d\n", chan, d, profile, mark, space);
	          }
      
	          demod_afsk_init (save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].decimate, 
			save_audio_config_p->achan[chan].baud,
			mark, space,
			profile,
			D);

	          if (have_plus) {
		    /* I'm not happy about putting this hack here. */
		    /* should pass in as a parameter rather than adding on later. */

	            save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
		    D->num_slicers = MAX_SLICERS;
	          }

	          /* For signal level reporting, we want a longer term view. */

	          D->quick_attack = D->agc_fast_attack * 0.2f;
	          D->sluggish_decay = D->agc_slow_decay * 0.2f;

	        } 	  /* for each freq pair */
	      }	
	      break;

	    case MODEM_QPSK:		// New for 1.4

	      // In versions 1.4 and 1.5, V.26 "Alternative A" was used.
	      // years later, I discover that the MFJ-2400 used "Alternative B."
	      // It looks like the other two manufacturers use the same but we
              // can't be sure until we find one for compatibility testing.

	      // In version 1.6 we add a choice for the user.
	      // If neither one was explicitly specified, print a message and take
	      // a default.  My current thinking is that we default to direwolf <= 1.5
	      // compatible for version 1.6 and MFJ compatible after that.

	      if (save_audio_config_p->achan[chan].v26_alternative == V26_UNSPECIFIED) {

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Two incompatible versions of 2400 bps QPSK are now available.\n");
	        dw_printf ("For compatibility with direwolf <= 1.5, use 'V26A' modem option in config file.\n");
	        dw_printf ("For compatibility MFJ-2400 use 'V26B' modem option in config file.\n");
	        dw_printf ("Command line options -j and -J can be used for channel 0.\n");
	        dw_printf ("For more information, read the Dire Wolf User Guide and\n");
	        dw_printf ("2400-4800-PSK-for-APRS-Packet-Radio.pdf.\n");
	        dw_printf ("The default is now MFJ-2400 compatibility mode.\n");

	        save_audio_config_p->achan[chan].v26_alternative = V26_DEFAULT;
	      }


// TODO: See how much CPU this takes on ARM and decide if we should have different defaults.

	      if (strlen(save_audio_config_p->achan[chan].profiles) == 0) {
//#if __arm__
//	        strlcpy (save_audio_config_p->achan[chan].profiles, "R", sizeof(save_audio_config_p->achan[chan].profiles));
//#else
	        strlcpy (save_audio_config_p->achan[chan].profiles, "PQRS", sizeof(save_audio_config_p->achan[chan].profiles));
//#endif
	      }
	      save_audio_config_p->achan[chan].num_subchan = strlen(save_audio_config_p->achan[chan].profiles);

	      save_audio_config_p->achan[chan].decimate = 1;	// think about this later.
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d bps, QPSK, %s, %d sample rate",
		    chan, save_audio_config_p->achan[chan].baud,
		    save_audio_config_p->achan[chan].profiles,
		    save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec);
	      if (save_audio_config_p->achan[chan].decimate != 1)
	        dw_printf (" / %d", save_audio_config_p->achan[chan].decimate);
	      dw_printf (", Tx %s", layer2_tx[(int)(save_audio_config_p->achan[chan].layer2_xmit)]);
	      if (save_audio_config_p->achan[chan].v26_alternative == V26_B)
	        dw_printf (", compatible with MFJ-2400");
	      else
	        dw_printf (", compatible with earlier direwolf");

	      if (save_audio_config_p->achan[chan].dtmf_decode != DTMF_DECODE_OFF)
	        dw_printf (", DTMF decoder enabled");
	      dw_printf (".\n");

	      int d;
	      for (d = 0; d < save_audio_config_p->achan[chan].num_subchan; d++) {

	        assert (d >= 0 && d < MAX_SUBCHANS);
	        struct demodulator_state_s *D;
	        D = &demodulator_state[chan][d];
	        profile = save_audio_config_p->achan[chan].profiles[d];

	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("About to call demod_psk_init for Q-PSK case, modem_type=%d, profile='%c'\n",
		//	save_audio_config_p->achan[chan].modem_type, profile);

	        demod_psk_init (save_audio_config_p->achan[chan].modem_type,
			save_audio_config_p->achan[chan].v26_alternative,
			save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].decimate, 
			save_audio_config_p->achan[chan].baud,
			profile,
			D);

	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("Returned from demod_psk_init\n");

	        /* For signal level reporting, we want a longer term view. */
		/* Guesses based on 9600.  Maybe revisit someday. */

	        D->quick_attack = 0.080 * 0.2;
	        D->sluggish_decay = 0.00012 * 0.2;
	      }
	      break;

	    case MODEM_8PSK:		// New for 1.4

// TODO: See how much CPU this takes on ARM and decide if we should have different defaults.

	      if (strlen(save_audio_config_p->achan[chan].profiles) == 0) {
//#if __arm__
//	        strlcpy (save_audio_config_p->achan[chan].profiles, "V", sizeof(save_audio_config_p->achan[chan].profiles));
//#else
	        strlcpy (save_audio_config_p->achan[chan].profiles, "TUVW", sizeof(save_audio_config_p->achan[chan].profiles));
//#endif
	      }
	      save_audio_config_p->achan[chan].num_subchan = strlen(save_audio_config_p->achan[chan].profiles);

	      save_audio_config_p->achan[chan].decimate = 1;	// think about this later
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d bps, 8PSK, %s, %d sample rate",
		    chan, save_audio_config_p->achan[chan].baud,
		    save_audio_config_p->achan[chan].profiles,
		    save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec);
	      if (save_audio_config_p->achan[chan].decimate != 1)
	        dw_printf (" / %d", save_audio_config_p->achan[chan].decimate);
	      dw_printf (", Tx %s", layer2_tx[(int)(save_audio_config_p->achan[chan].layer2_xmit)]);
	      if (save_audio_config_p->achan[chan].dtmf_decode != DTMF_DECODE_OFF)
	        dw_printf (", DTMF decoder enabled");
	      dw_printf (".\n");

	      //int d;
	      for (d = 0; d < save_audio_config_p->achan[chan].num_subchan; d++) {

	        assert (d >= 0 && d < MAX_SUBCHANS);
	        struct demodulator_state_s *D;
	        D = &demodulator_state[chan][d];
	        profile = save_audio_config_p->achan[chan].profiles[d];

	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("About to call demod_psk_init for 8-PSK case, modem_type=%d, profile='%c'\n",
		//	save_audio_config_p->achan[chan].modem_type, profile);

	        demod_psk_init (save_audio_config_p->achan[chan].modem_type,
			save_audio_config_p->achan[chan].v26_alternative,
			save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].decimate,
			save_audio_config_p->achan[chan].baud,
			profile,
			D);

	        //text_color_set(DW_COLOR_DEBUG);
	        //dw_printf ("Returned from demod_psk_init\n");

	        /* For signal level reporting, we want a longer term view. */
		/* Guesses based on 9600.  Maybe revisit someday. */

	        D->quick_attack = 0.080 * 0.2;
	        D->sluggish_decay = 0.00012 * 0.2;
	      }
	      break;

//TODO: how about MODEM_OFF case?

	    case MODEM_BASEBAND:
	    case MODEM_SCRAMBLE:
	    case MODEM_AIS:
	    default:	/* Not AFSK */
	      {

	      // For AIS we will accept only a good CRC without any fixup attempts.
	      // Even with that, there are still a lot of CRC false matches with random noise.

	      if (save_audio_config_p->achan[chan].modem_type == MODEM_AIS) {
		if (save_audio_config_p->achan[chan].fix_bits != RETRY_NONE) {
	          text_color_set(DW_COLOR_INFO);
		  dw_printf ("Channel %d: FIX_BITS option has been turned off for AIS.\n", chan);
	          save_audio_config_p->achan[chan].fix_bits = RETRY_NONE;
	        }
		if (save_audio_config_p->achan[chan].passall != 0) {
	          text_color_set(DW_COLOR_INFO);
		  dw_printf ("Channel %d: PASSALL option has been turned off for AIS.\n", chan);
	          save_audio_config_p->achan[chan].passall = 0;
	        }
	      }

	      if (strcmp(save_audio_config_p->achan[chan].profiles, "") == 0) {

		/* Apply default if not set earlier. */
		/* Not sure if it should be on for ARM too. */
		/* Need to take a look at CPU usage and performance difference. */

		/* Version 1.5:  Remove special case for ARM. */
		/* We want higher performance to be the default. */
		/* "MODEM 9600 -" can be used on very slow CPU if necessary. */

	        strlcpy (save_audio_config_p->achan[chan].profiles, "+", sizeof(save_audio_config_p->achan[chan].profiles));
	      }

/*
 * We need a minimum number of audio samples per bit time for good performance.
 * Easier to check here because demod_9600_init might have an adjusted sample rate.
 */

	      float ratio = (float)(save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec)
							/ (float)(save_audio_config_p->achan[chan].baud);

/*
 * Set reasonable upsample ratio if user did not override.
 */

	      if (save_audio_config_p->achan[chan].upsample == 0) {

	        if (ratio < 4) {

	           // This is extreme.
		   // No one should be using a sample rate this low but
		   // amazingly a recording with 22050 rate can be decoded.
	           // 3 and 4 are the same.  Need more tests.

	          save_audio_config_p->achan[chan].upsample = 4;
	        }
	        else if (ratio < 5) {

	          // example: 44100 / 9600 is 4.59
	          // 3 is slightly better than 2 or 4.

	          save_audio_config_p->achan[chan].upsample = 3;
	        }
	        else if (ratio < 10) {

	          // example: 48000 / 9600 = 5
	          // 3 is slightly better than 2 or 4.

	          save_audio_config_p->achan[chan].upsample = 3;
	        }
	        else if (ratio < 15) {

	          // ... guessing

	          save_audio_config_p->achan[chan].upsample = 2;
	        }
	        else {	// >= 15
	          //
	          // An example of this might be .....
	          // Probably no benefit.

	          save_audio_config_p->achan[chan].upsample = 1;
	        }
	      }

#ifdef TUNE_UPSAMPLE
	      save_audio_config_p->achan[chan].upsample = TUNE_UPSAMPLE;
#endif

	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Channel %d: %d baud, %s, %s, %d sample rate x %d",
		    chan,
	            save_audio_config_p->achan[chan].baud,
	            save_audio_config_p->achan[chan].modem_type == MODEM_AIS ? "AIS" : "K9NG/G3RUH",
		    save_audio_config_p->achan[chan].profiles,
		    save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec,
	            save_audio_config_p->achan[chan].upsample);
	      dw_printf (", Tx %s", layer2_tx[(int)(save_audio_config_p->achan[chan].layer2_xmit)]);
	      if (save_audio_config_p->achan[chan].dtmf_decode != DTMF_DECODE_OFF) 
	        dw_printf (", DTMF decoder enabled");
	      dw_printf (".\n");
	      
	      struct demodulator_state_s *D;
	      D = &demodulator_state[chan][0];	// first subchannel


	      save_audio_config_p->achan[chan].num_subchan = 1;
              save_audio_config_p->achan[chan].num_slicers = 1;

	      if (strchr(save_audio_config_p->achan[chan].profiles, '+') != NULL) {

		/* I'm not happy about putting this hack here. */
		/* This belongs in demod_9600_init but it doesn't have access to the audio config. */

	        save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
     	      }
	        

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("The ratio of audio samples per sec (%d) to data rate in baud (%d) is %.1f\n",
				save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec,
				save_audio_config_p->achan[chan].baud,
				(double)ratio);
	      if (ratio < 3) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("There is little hope of success with such a low ratio.  Use a higher sample rate.\n");
	      }
	      else if (ratio < 5) {
	        dw_printf ("This is on the low side for best performance.  Can you use a higher sample rate?\n");
	        if (save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec == 44100) {
	          dw_printf ("For example, can you use 48000 rather than 44100?\n");
	        }
	      }
	      else if (ratio < 6) {
	        dw_printf ("Increasing the sample rate should improve decoder performance.\n");
	      }
	      else if (ratio > 15) {
	        dw_printf ("Sample rate is more than adequate.  You might lower it if CPU load is a concern.\n");
	      }
	      else {
	        dw_printf ("This is a suitable ratio for good performance.\n");
	      }

	      demod_9600_init (save_audio_config_p->achan[chan].modem_type,
			save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec,
			save_audio_config_p->achan[chan].upsample,
			save_audio_config_p->achan[chan].baud, D);

	      if (strchr(save_audio_config_p->achan[chan].profiles, '+') != NULL) {

		/* I'm not happy about putting this hack here. */
		/* should pass in as a parameter rather than adding on later. */

	        save_audio_config_p->achan[chan].num_slicers = MAX_SLICERS;
		D->num_slicers = MAX_SLICERS;
	      }

	      /* For signal level reporting, we want a longer term view. */

	      D->quick_attack = D->agc_fast_attack * 0.2f;
	      D->sluggish_decay = D->agc_slow_decay * 0.2f;
	      }
	      break;

	  }  /* switch on modulation type. */
    
	 }  /* if channel medium is radio */

// FIXME dw_printf ("-------- end of loop for chn %d \n", chan);

	}  /* for chan ... */

	// Now the virtual channels.  FIXME:  could be single loop.

	for (chan = MAX_RADIO_CHANS; chan < MAX_TOTAL_CHANS; chan++) {

// FIXME dw_printf ("-------- virtual channel loop %d \n", chan);

	  if (chan == save_audio_config_p->igate_vchannel) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Channel %d: IGate virtual channel.\n", chan);
	  }
	}

        return (0);

} /* end demod_init */



/*------------------------------------------------------------------
 *
 * Name:        demod_get_sample
 *
 * Purpose:     Get one audio sample from the specified sound input source.
 *
 * Inputs:	a	- Index for audio device.  0 = first.
 *
 * Returns:     -32768 .. 32767 for a valid audio sample.
 *              256*256 for end of file or other error.
 *
 * Global In:	save_audio_config_p->adev[ACHAN2ADEV(chan)].bits_per_sample - So we know whether to 
 *			read 1 or 2 bytes from audio stream.
 *
 * Description:	Grab 1 or two bytes depending on data source.
 *
 *		When processing stereo, the caller will call this
 *		at twice the normal rate to obtain alternating left 
 *		and right samples.
 *
 *----------------------------------------------------------------*/

#define FSK_READ_ERR (256*256)

__attribute__((hot))
int demod_get_sample (int a)		
{
	int x1, x2;
	signed short sam;	/* short to force sign extension. */


	assert (save_audio_config_p->adev[a].bits_per_sample == 8 || save_audio_config_p->adev[a].bits_per_sample == 16);

	if (save_audio_config_p->adev[a].bits_per_sample == 8) {

	  x1 = audio_get(a);				
	  if (x1 < 0) return(FSK_READ_ERR);

	  assert (x1 >= 0 && x1 <= 255);

	  /* Scale 0..255 into -32k..+32k */

	  sam = (x1 - 128) * 256;

	}
	else {
	  x1 = audio_get(a);	/* lower byte first */
	  if (x1 < 0) return(FSK_READ_ERR);

	  x2 = audio_get(a);
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

static volatile int mute_input[MAX_RADIO_CHANS];

// New in 1.7.
// A few people have a really bad audio cross talk situation where they receive their own transmissions.
// It usually doesn't cause a problem but it is confusing to look at.
// "half duplex" setting applied only to the transmit logic.  i.e. wait for clear channel before sending.
// Receiving was still active.
// I think the simplest solution is to mute/unmute the audio input at this point if not full duplex.
// This is called from ptt_set for half duplex.

void demod_mute_input (int chan, int mute_during_xmit)
{
	assert (chan >= 0 && chan < MAX_RADIO_CHANS);
	mute_input[chan] = mute_during_xmit;
}

__attribute__((hot))
void demod_process_sample (int chan, int subchan, int sam)
{
	float fsam;
	//int k;


	struct demodulator_state_s *D;

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	if (mute_input[chan]) {
	  sam = 0;
	};

	D = &demodulator_state[chan][subchan];


	/* Scale to nice number, actually -2.0 to +2.0 for extra headroom */

	fsam = sam / 16384.0f;

/*
 * Accumulate measure of the input signal level.
 */


/*
 * Version 1.2: Try new approach to capturing the amplitude.
 * This is same as the later AGC without the normalization step.
 * We want decay to be substantially slower to get a longer
 * range idea of the received audio.
 */

	if (fsam >= D->alevel_rec_peak) {
	  D->alevel_rec_peak = fsam * D->quick_attack + D->alevel_rec_peak * (1.0f - D->quick_attack);
	}
	else {
	  D->alevel_rec_peak = fsam * D->sluggish_decay + D->alevel_rec_peak * (1.0f - D->sluggish_decay);
	}

	if (fsam <= D->alevel_rec_valley) {
	  D->alevel_rec_valley = fsam * D->quick_attack + D->alevel_rec_valley * (1.0f - D->quick_attack);
	}
	else  {   
	  D->alevel_rec_valley = fsam * D->sluggish_decay + D->alevel_rec_valley * (1.0f - D->sluggish_decay);
	}


/*
 * Select decoder based on modulation type.
 */

	switch (save_audio_config_p->achan[chan].modem_type) {

	  case MODEM_OFF:

	    // Might have channel only listening to DTMF for APRStt gateway.
	    // Don't waste CPU time running a demodulator here.
	    break;

	  case MODEM_AFSK:
	  case MODEM_EAS:

	    if (save_audio_config_p->achan[chan].decimate > 1) {

	      sample_sum[chan][subchan] += sam;
	      sample_count[chan][subchan]++;
	      if (sample_count[chan][subchan] >= save_audio_config_p->achan[chan].decimate) {
  	        demod_afsk_process_sample (chan, subchan, sample_sum[chan][subchan] / save_audio_config_p->achan[chan].decimate, D);
	        sample_sum[chan][subchan] = 0;
	        sample_count[chan][subchan] = 0;
	      }
	    }
	    else {
	      demod_afsk_process_sample (chan, subchan, sam, D);
	    }
	    break;

	  case MODEM_QPSK:
	  case MODEM_8PSK:

	    if (save_audio_config_p->achan[chan].decimate > 1) {

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid combination of options.  Exiting.\n");
	      // Would probably work but haven't thought about it or tested yet.
	      exit (1);
	    }
	    else {
	      demod_psk_process_sample (chan, subchan, sam, D);
	    }
	    break;

	  case MODEM_BASEBAND:
	  case MODEM_SCRAMBLE:
	  case MODEM_AIS:
	  default:
	
	    demod_9600_process_sample (chan, sam, save_audio_config_p->achan[chan].upsample, D);
	    break;

	}  /* switch modem_type */
	return;

} /* end demod_process_sample */






/* Doesn't seem right.  Need to revisit this. */
/* Resulting scale is 0 to almost 100. */
/* Cranking up the input level produces no more than 97 or 98. */
/* We currently produce a message when this goes over 90. */

alevel_t demod_get_audio_level (int chan, int subchan) 
{
	struct demodulator_state_s *D;
	alevel_t alevel;

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	/* We have to consider two different cases here. */
	/* N demodulators, each with own slicer and HDLC decoder. */
	/* Single demodulator, multiple slicers each with own HDLC decoder. */

	if (demodulator_state[chan][0].num_slicers > 1) {
	  subchan = 0;
	}

	D = &demodulator_state[chan][subchan];

	// Take half of peak-to-peak for received audio level.

	alevel.rec = (int) (( D->alevel_rec_peak - D->alevel_rec_valley ) * 50.0f + 0.5f);

	if (save_audio_config_p->achan[chan].modem_type == MODEM_AFSK ||
	    save_audio_config_p->achan[chan].modem_type == MODEM_EAS) {

	  /* For AFSK, we have mark and space amplitudes. */

	  alevel.mark = (int) ((D->alevel_mark_peak ) * 100.0f + 0.5f);
	  alevel.space = (int) ((D->alevel_space_peak ) * 100.0f + 0.5f);
	}
	else if (save_audio_config_p->achan[chan].modem_type == MODEM_QPSK ||
	         save_audio_config_p->achan[chan].modem_type == MODEM_8PSK) {
	  alevel.mark = -1;
	  alevel.space = -1;
	}
	else {

#if 1	
	  /* Display the + and - peaks.  */
	  /* Normally we'd expect them to be about the same. */
	  /* However, with SDR, or other DC coupling, we could have an offset. */

	  alevel.mark = (int) ((D->alevel_mark_peak) * 200.0f  + 0.5f);
	  alevel.space = (int) ((D->alevel_space_peak) * 200.0f - 0.5f);


#else
	  /* Here we have + and - peaks after filtering. */
	  /* Take half of the peak to peak. */
	  /* The "5/6" factor worked out right for the current low pass filter. */
	  /* Will it need to be different if the filter is tweaked? */

	  alevel.mark = (int) ((D->alevel_mark_peak - D->alevel_space_peak) * 100.0f * 5.0f/6.0f + 0.5f);
	  alevel.space = -1;		/* to print one number inside of ( ) */
#endif
	}
	return (alevel);
}


/* end demod.c */

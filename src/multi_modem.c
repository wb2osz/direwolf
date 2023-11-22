//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015, 2016, 2019  John Langner, WB2OSZ
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
 * Name:	multi_modem.c
 *
 * Purpose:	Use multiple modems in parallel to increase chances
 *		of decoding less than ideal signals.
 *
 * Description:	The initial motivation was for HF SSB where mistuning
 *		causes a shift in the audio frequencies.  Here, we can
 * 		have multiple modems tuned to staggered pairs of tones
 *		in hopes that one will be close enough.
 *
 *		The overall structure opens the door to other approaches
 *		as well.  For VHF FM, the tones should always have the
 *		right frequencies but we might want to tinker with other
 *		modem parameters instead of using a single compromise.
 *
 * Originally:	The the interface application is in 3 places:
 *
 *		(a) Main program (direwolf.c or atest.c) calls 
 *		    demod_init to set up modem properties and
 *		    hdlc_rec_init for the HDLC decoders.
 *
 *		(b) demod_process_sample is called for each audio sample
 *		    from the input audio stream.
 *
 *	   	(c) When a valid AX.25 frame is found, process_rec_frame,
 *		    provided by the application, in direwolf.c or atest.c,
 *		    is called.  Normally this comes from hdlc_rec.c but
 *		    there are a couple other special cases to consider.
 *		    It can be called from hdlc_rec2.c if it took a long 
 *  		    time to "fix" corrupted bits.  aprs_tt.c constructs 
 * 		    a fake packet when a touch tone message is received.
 *
 * New in version 0.9:
 *
 *		Put an extra layer in between which potentially uses
 *		multiple modems & HDLC decoders per channel.  The tricky
 *		part is picking the best one when there is more than one
 *		success and discarding the rest.
 *
 * New in version 1.1:
 *
 *		Several enhancements provided by Fabrice FAURE:
 *
 *		Additional types of attempts to fix a bad CRC.
 *		Optimized code to reduce execution time.
 *		Improved detection of duplicate packets from
 *		different fixup attempts.
 *		Set limit on number of packets in fix up later queue.
 *
 * New in version 1.6:
 *
 *		FX.25.  Previously a delay of a couple bits (or more accurately
 *		symbols) was fine because the decoders took about the same amount of time.
 *		Now, we can have an additional delay of up to 64 check bytes and
 *		some filler in the data portion.  We can't simply wait that long.
 *		With normal AX.25 a couple frames can come and go during that time.	
 *		We want to delay the duplicate removal while FX.25 block reception
 *		is going on.
 *		
 *------------------------------------------------------------------*/

//#define DEBUG 1

#define DIGIPEATER_C		// Why?

#include "direwolf.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

#include "ax25_pad.h"
#include "textcolor.h"
#include "multi_modem.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "dlq.h"
#include "fx25.h"
#include "version.h"
#include "ais.h"



// Properties of the radio channels.

static struct audio_s          *save_audio_config_p;


// Candidates for further processing.

static struct {
	packet_t packet_p;
	alevel_t alevel;
	float speed_error;
	fec_type_t fec_type;	// Type of FEC: none(0), fx25, il2p
	retry_t retries;	// For the old "fix bits" strategy, this is the
				// number of bits that were modified to get a good CRC.
				// It would be 0 to something around 4.
				// For FX.25, it is the number of corrected.
				// This could be from 0 thru 32.
	int age;
	unsigned int crc;
	int score;
} candidate[MAX_CHANS][MAX_SUBCHANS][MAX_SLICERS];



//#define PROCESS_AFTER_BITS 2		// version 1.4.  Was a little short for skew of PSK with different modem types, optional pre-filter

#define PROCESS_AFTER_BITS 3


static int process_age[MAX_CHANS];

static void pick_best_candidate (int chan);



/*------------------------------------------------------------------------------
 *
 * Name:	multi_modem_init
 * 
 * Purpose:	Called at application start up to initialize appropriate
 *		modems and HDLC decoders.
 *
 * Input:	Modem properties structure as filled in from the configuration file.
 *		
 * Outputs:	
 *		
 * Description:	Called once at application startup time.
 *
 *------------------------------------------------------------------------------*/

void multi_modem_init (struct audio_s *pa) 
{
	int chan;


/*
 * Save audio configuration for later use.
 */

	save_audio_config_p = pa;

	memset (candidate, 0, sizeof(candidate));

	demod_init (save_audio_config_p);
	hdlc_rec_init (save_audio_config_p);

	for (chan=0; chan<MAX_CHANS; chan++) {
	  if (save_audio_config_p->chan_medium[chan] == MEDIUM_RADIO) {
	    if (save_audio_config_p->achan[chan].baud <= 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Internal error, chan=%d, %s, %d\n", chan, __FILE__, __LINE__);
	      save_audio_config_p->achan[chan].baud = DEFAULT_BAUD;
	    }
	    int real_baud = save_audio_config_p->achan[chan].baud;
	    if (save_audio_config_p->achan[chan].modem_type == MODEM_QPSK) real_baud = save_audio_config_p->achan[chan].baud / 2;
	    if (save_audio_config_p->achan[chan].modem_type == MODEM_8PSK) real_baud = save_audio_config_p->achan[chan].baud / 3;

	    process_age[chan] = PROCESS_AFTER_BITS * save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / real_baud ;
	    //crc_queue_of_last_to_app[chan] = NULL;
	  }
	}

}


/*------------------------------------------------------------------------------
 *
 * Name:	multi_modem_process_sample
 * 
 * Purpose:	Feed the sample into the proper modem(s) for the channel.	
 *
 * Inputs:	chan	- Radio channel number
 *
 *		audio_sample 
 *
 * Description:	In earlier versions we always had a one-to-one mapping with
 *		demodulators and HDLC decoders.
 *		This was added so we could have multiple modems running in
 *		parallel with different mark/space tones to compensate for 
 *		mistuning of HF SSB signals.
 * 		It was also possible to run multiple filters, for the same
 *		tones, in parallel (e.g. ABC).
 *
 * Version 1.2:	Let's try something new for an experiment.
 *		We will have a single mark/space demodulator but multiple
 *		slicers, using different levels, each with its own HDLC decoder.
 *		We now have a separate variable, num_demod, which could be 1
 *		while num_subchan is larger.
 *
 * Version 1.3:	Go back to num_subchan with single meaning of number of demodulators.
 *		We now have separate independent variable, num_slicers, for the
 *		mark/space imbalance compensation.
 *		num_demod, while probably more descriptive, should not exist anymore.
 *
 *------------------------------------------------------------------------------*/

static float dc_average[MAX_CHANS];

int multi_modem_get_dc_average (int chan)
{
	// Scale to +- 200 so it will like the deviation measurement.

	return ( (int) ((float)(dc_average[chan]) * (200.0f / 32767.0f) ) );
}

__attribute__((hot))
void multi_modem_process_sample (int chan, int audio_sample) 
{
	int d;
	int subchan;

// Accumulate an average DC bias level.
// Shouldn't happen with a soundcard but could with mistuned SDR.

	dc_average[chan] = dc_average[chan] * 0.999f + (float)audio_sample * 0.001f;


// Issue 128.  Someone ran into this.

	//assert (save_audio_config_p->achan[chan].num_subchan > 0 && save_audio_config_p->achan[chan].num_subchan <= MAX_SUBCHANS);
	//assert (save_audio_config_p->achan[chan].num_slicers > 0 && save_audio_config_p->achan[chan].num_slicers <= MAX_SLICERS);

	if (save_audio_config_p->achan[chan].num_subchan <= 0 || save_audio_config_p->achan[chan].num_subchan > MAX_SUBCHANS ||
	    save_audio_config_p->achan[chan].num_slicers <= 0 || save_audio_config_p->achan[chan].num_slicers > MAX_SLICERS) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR!  Something is seriously wrong in %s %s.\n", __FILE__, __func__);
	  dw_printf ("chan = %d, num_subchan = %d [max %d], num_slicers = %d [max %d]\n", chan,
									save_audio_config_p->achan[chan].num_subchan, MAX_SUBCHANS,
									save_audio_config_p->achan[chan].num_slicers, MAX_SLICERS);
	  dw_printf ("Please report this message and include a copy of your configuration file.\n");
	  exit (EXIT_FAILURE);
	}

	/* Formerly one loop. */
	/* 1.2: We can feed one demodulator but end up with multiple outputs. */

	/* Send same thing to all. */
	for (d = 0; d < save_audio_config_p->achan[chan].num_subchan; d++) {
	  demod_process_sample(chan, d, audio_sample);
	}

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {
	  int slice;

	  for (slice = 0; slice < save_audio_config_p->achan[chan].num_slicers; slice++) {

	    if (candidate[chan][subchan][slice].packet_p != NULL) {
	      candidate[chan][subchan][slice].age++;
	      if (candidate[chan][subchan][slice].age > process_age[chan]) {
	        if (fx25_rec_busy(chan)) {
		  candidate[chan][subchan][slice].age = 0;
	        }
	        else {
	          pick_best_candidate (chan);
	        }
	      }
	    }
	  }
	}
}



/*-------------------------------------------------------------------
 *
 * Name:        multi_modem_process_rec_frame
 *
 * Purpose:     This is called when we receive a frame with a valid 
 *		FCS and acceptable size.
 *
 * Inputs:	chan	- Audio channel number, 0 or 1.
 *		subchan	- Which modem found it.
 *		slice	- Which slice found it.
 *		fbuf	- Pointer to first byte in HDLC frame.
 *		flen	- Number of bytes excluding the FCS.
 *		alevel	- Audio level, range of 0 - 100.
 *				(Special case, use negative to skip
 *				 display of audio level line.
 *				 Use -2 to indicate DTMF message.)
 *		retries	- Level of correction used.
 *		fec_type	- none(0), fx25, il2p
 *
 * Description:	Add to list of candidates.  Best one will be picked later.
 *
 *--------------------------------------------------------------------*/


void multi_modem_process_rec_frame (int chan, int subchan, int slice, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries, fec_type_t fec_type)
{
	packet_t pp;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SUBCHANS);

// Special encapsulation for AIS & EAS so they can be treated normally pretty much everywhere else.

	if (save_audio_config_p->achan[chan].modem_type == MODEM_AIS) {
	  char nmea[256];
	  ais_to_nmea (fbuf, flen, nmea, sizeof(nmea));

	  char monfmt[276];
	  snprintf (monfmt, sizeof(monfmt), "AIS>%s%1d%1d:{%c%c%s", APP_TOCALL, MAJOR_VERSION, MINOR_VERSION, USER_DEF_USER_ID, USER_DEF_TYPE_AIS, nmea);
	  pp = ax25_from_text (monfmt, 1);

	  // alevel gets in there somehow making me question why it is passed thru here.
	}
	else if (save_audio_config_p->achan[chan].modem_type == MODEM_EAS) {
	  char monfmt[300];	// EAS SAME message max length is 268

	  snprintf (monfmt, sizeof(monfmt), "EAS>%s%1d%1d:{%c%c%s", APP_TOCALL, MAJOR_VERSION, MINOR_VERSION, USER_DEF_USER_ID, USER_DEF_TYPE_EAS, fbuf);
	  pp = ax25_from_text (monfmt, 1);

	  // alevel gets in there somehow making me question why it is passed thru here.
	}
	else {
	  pp = ax25_from_frame (fbuf, flen, alevel);
	}

	multi_modem_process_rec_packet (chan, subchan, slice, pp, alevel, retries, fec_type);
}

// TODO: Eliminate function above and move code elsewhere?

void multi_modem_process_rec_packet (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, fec_type_t fec_type)
{
	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unexpected internal problem, %s %d\n", __FILE__, __LINE__);
	  return;	/* oops!  why would it fail? */
	}

/*
 * If only one demodulator/slicer, and no FX.25 in progress,
 * push it thru and forget about all this foolishness.
 */
	if (save_audio_config_p->achan[chan].num_subchan == 1 &&
	    save_audio_config_p->achan[chan].num_slicers == 1 &&
	    ! fx25_rec_busy(chan)) {


	  int drop_it = 0;
	  if (save_audio_config_p->recv_error_rate != 0) {
	    float r = (float)(rand()) / (float)RAND_MAX;		// Random, 0.0 to 1.0

	    //text_color_set(DW_COLOR_INFO);
	    //dw_printf ("TEMP DEBUG.  recv error rate = %d\n", save_audio_config_p->recv_error_rate);

	    if (save_audio_config_p->recv_error_rate / 100.0 > r) {
	      drop_it = 1;
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Intentionally dropping incoming frame.  Recv Error rate = %d per cent.\n", save_audio_config_p->recv_error_rate);
	    }
	  }

	  if (drop_it ) {
	    ax25_delete (pp);
	  }
	  else {
	    dlq_rec_frame (chan, subchan, slice, pp, alevel, fec_type, retries, "");
	  }
	  return;
	}


/*
 * Otherwise, save them up for a few bit times so we can pick the best.
 */
	if (candidate[chan][subchan][slice].packet_p != NULL) {
	  /* Plain old AX.25: Oops!  Didn't expect it to be there. */
	  /* FX.25: Quietly replace anything already there.  It will have priority. */
	  ax25_delete (candidate[chan][subchan][slice].packet_p);
	  candidate[chan][subchan][slice].packet_p = NULL;
	}

	assert (pp != NULL);

	candidate[chan][subchan][slice].packet_p = pp;
	candidate[chan][subchan][slice].alevel = alevel;
	candidate[chan][subchan][slice].fec_type = fec_type;
	candidate[chan][subchan][slice].retries = retries;
	candidate[chan][subchan][slice].age = 0;
	candidate[chan][subchan][slice].crc = ax25_m_m_crc(pp);
}




/*-------------------------------------------------------------------
 *
 * Name:        pick_best_candidate
 *
 * Purpose:     This is called when we have one or more candidates
 *		available for a certain amount of time.
 *
 * Description:	Pick the best one and send it up to the application.
 *		Discard the others.
 *		
 * Rules:	We prefer one received perfectly but will settle for
 *		one where some bits had to be flipped to get a good CRC.
 *
 *--------------------------------------------------------------------*/

/* This is a suitable order for interleaved "G" demodulators. */
/* Opposite order would be suitable for multi-frequency although */
/* multiple slicers are of questionable value for HF SSB. */

#define subchan_from_n(x) ((x) % save_audio_config_p->achan[chan].num_subchan)
#define slice_from_n(x)   ((x) / save_audio_config_p->achan[chan].num_subchan)


static void pick_best_candidate (int chan) 
{
	int best_n, best_score;
	char spectrum[MAX_SUBCHANS*MAX_SLICERS+1];
	int n, j, k;
	if (save_audio_config_p->achan[chan].num_slicers < 1) {
	  save_audio_config_p->achan[chan].num_slicers = 1;
	}
	int num_bars = save_audio_config_p->achan[chan].num_slicers * save_audio_config_p->achan[chan].num_subchan;

	memset (spectrum, 0, sizeof(spectrum));

	for (n = 0; n < num_bars; n++) {
	  j = subchan_from_n(n);
	  k = slice_from_n(n);

	  /* Build the spectrum display. */

	  if (candidate[chan][j][k].packet_p == NULL) {
	    spectrum[n] = '_';
	  }
	  else if (candidate[chan][j][k].fec_type != fec_type_none) {		// FX.25 or IL2P
	    // FIXME: using retries both as an enum and later int too.
	    if ((int)(candidate[chan][j][k].retries) <= 9) {
	      spectrum[n] = '0' + candidate[chan][j][k].retries;
	    }
	    else {
	      spectrum[n] = '+';
	    }
	  }									// AX.25 below
	  else if (candidate[chan][j][k].retries == RETRY_NONE) {
	    spectrum[n] = '|';
	  }
	  else if (candidate[chan][j][k].retries == RETRY_INVERT_SINGLE) {
	    spectrum[n] = ':';
	  }
	  else  {
	    spectrum[n] = '.';
	  }

	  /* Beginning score depends on effort to get a valid frame CRC. */

	  if (candidate[chan][j][k].packet_p == NULL) {
	    candidate[chan][j][k].score = 0;
	  }
	  else {
	    if (candidate[chan][j][k].fec_type != fec_type_none) {
	      candidate[chan][j][k].score = 9000 - 100 * candidate[chan][j][k].retries;		// has FEC
	    }
	    else {
	      /* Originally, this produced 0 for the PASSALL case. */
	      /* This didn't work so well when looking for the best score. */
	      /* Around 1.3 dev H, we add an extra 1 in here so the minimum */
	      /* score should now be 1 for anything received.  */

	      candidate[chan][j][k].score = RETRY_MAX * 1000 - ((int)candidate[chan][j][k].retries * 1000) + 1;
	    }
	  }
	}

	// FIXME: IL2p & FX.25 don't have CRC calculated. Must fill it in first.


	/* Bump it up slightly if others nearby have the same CRC. */

	for (n = 0; n < num_bars; n++) {
	  int m;

	  j = subchan_from_n(n);
	  k = slice_from_n(n);

	  if (candidate[chan][j][k].packet_p != NULL) {

	    for (m = 0; m < num_bars; m++) {

	      int mj = subchan_from_n(m);
	      int mk = slice_from_n(m);

	      if (m != n && candidate[chan][mj][mk].packet_p != NULL) {
	        if (candidate[chan][j][k].crc == candidate[chan][mj][mk].crc) {
	          candidate[chan][j][k].score += (num_bars+1) - abs(m-n);
	        }
	      }
	    }
	  }
	}
  
	best_n = 0;
	best_score = 0;

	for (n = 0; n < num_bars; n++) {
	  j = subchan_from_n(n);
	  k = slice_from_n(n);

	  if (candidate[chan][j][k].packet_p != NULL) {
	    if (candidate[chan][j][k].score > best_score) {
	       best_score = candidate[chan][j][k].score;
	       best_n = n;
	    }
	  }
	}

#if DEBUG	
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n%s\n", spectrum);

	for (n = 0; n < num_bars; n++) {
	  j = subchan_from_n(n);
	  k = slice_from_n(n);

	  if (candidate[chan][j][k].packet_p == NULL) {
	    dw_printf ("%d.%d.%d: ptr=%p\n", chan, j, k,
		candidate[chan][j][k].packet_p);
	  }
	  else {
	    dw_printf ("%d.%d.%d: ptr=%p, fec_type=%d, retry=%d, age=%3d, crc=%04x, score=%d  %s\n", chan, j, k,
		candidate[chan][j][k].packet_p,
		(int)(candidate[chan][j][k].fec_type),
		(int)(candidate[chan][j][k].retries),
		candidate[chan][j][k].age,
		candidate[chan][j][k].crc,
		candidate[chan][j][k].score,
		(n == best_n) ? "***" : "");
	  }
	}
#endif

	if (best_score == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unexpected internal problem, %s %d.  How can best score be zero?\n", __FILE__, __LINE__);
	}

/*
 * send the best one along.
 */

	/* Delete those not chosen. */

	for (n = 0; n < num_bars; n++) {
	  j = subchan_from_n(n);
	  k = slice_from_n(n);
	  if (n != best_n && candidate[chan][j][k].packet_p != NULL) {
	    ax25_delete (candidate[chan][j][k].packet_p);
	    candidate[chan][j][k].packet_p = NULL;
	  }
	}

	/* Pass along one. */


	j = subchan_from_n(best_n);
	k = slice_from_n(best_n);

	int drop_it = 0;
	if (save_audio_config_p->recv_error_rate != 0) {
	  float r = (float)(rand()) / (float)RAND_MAX;		// Random, 0.0 to 1.0

	  //text_color_set(DW_COLOR_INFO);
	  //dw_printf ("TEMP DEBUG.  recv error rate = %d\n", save_audio_config_p->recv_error_rate);

	  if (save_audio_config_p->recv_error_rate / 100.0 > r) {
	    drop_it = 1;
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Intentionally dropping incoming frame.  Recv Error rate = %d per cent.\n", save_audio_config_p->recv_error_rate);
	  }
	}

	if ( drop_it ) {
	  ax25_delete (candidate[chan][j][k].packet_p);
	  candidate[chan][j][k].packet_p = NULL;
	}
	else {
	  assert (candidate[chan][j][k].packet_p != NULL);
	  dlq_rec_frame (chan, j, k,
		candidate[chan][j][k].packet_p,
		candidate[chan][j][k].alevel,
		candidate[chan][j][k].fec_type,
		(int)(candidate[chan][j][k].retries),
		spectrum);

	  /* Someone else owns it now and will delete it later. */
	  candidate[chan][j][k].packet_p = NULL;
	}

	/* Clear in preparation for next time. */

	memset (candidate[chan], 0, sizeof(candidate[chan]));

} /* end pick_best_candidate */


/* end multi_modem.c */

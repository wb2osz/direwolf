//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015  John Langner, WB2OSZ
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
 *------------------------------------------------------------------*/
//#define DEBUG 1
#define DIGIPEATER_C


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <sys/unistd.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "multi_modem.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "dlq.h"


// Properties of the radio channels.

static struct audio_s          *save_audio_config_p;


// Candidates for further processing.

static struct {

	packet_t packet_p;
	alevel_t alevel;
	retry_t retries;
	int age;
	unsigned int crc;
	int score;

} candidate[MAX_CHANS][MAX_SUBCHANS];
#define MAX_STORED_CRC 256

typedef struct crc_s {
	struct crc_s* nextp;	/* Next pointer to maintain a queue. */
	unsigned int crc;
} *crc_t;

static crc_t crc_queue_of_last_to_app[MAX_CHANS];

#define PROCESS_AFTER_BITS 2

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
	  if (save_audio_config_p->achan[chan].valid) { 
	    if (save_audio_config_p->achan[chan].baud <= 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("Internal error, chan=%d, %s, %d\n", chan, __FILE__, __LINE__);
	      save_audio_config_p->achan[chan].baud = DEFAULT_BAUD;
	    }
	    process_age[chan] = PROCESS_AFTER_BITS * save_audio_config_p->adev[ACHAN2ADEV(chan)].samples_per_sec / save_audio_config_p->achan[chan].baud;
	    crc_queue_of_last_to_app[chan] = NULL;
	  }
	}

}

//Add a crc to the end of the queue and returns the numbers of CRC stored in the queue
int crc_queue_append (unsigned int crc, unsigned int chan) {
	crc_t plast;
//	crc_t plast1;
	crc_t pnext;
	crc_t new_crc;
	
	unsigned int nb_crc = 1;
	if (chan>=MAX_CHANS) {
	  return -1;
	}
	new_crc = (crc_t) malloc (10*sizeof(struct crc_s));
	if (!new_crc)
	  return -1;
	new_crc->crc = crc;
	new_crc->nextp = NULL;
	if (crc_queue_of_last_to_app[chan] == NULL) {
	  crc_queue_of_last_to_app[chan] = new_crc;
	  nb_crc = 1;
	}
	else {
	  nb_crc = 2;
	  plast = crc_queue_of_last_to_app[chan];
	  pnext = plast->nextp;
	  while (pnext != NULL) {
	    nb_crc++;
	    plast = pnext;
	    pnext = pnext->nextp;
	  }
	  plast->nextp = new_crc;
	}
#if DEBUG 
	text_color_set(DW_COLOR_DEBUG);
	dw_printf("Out crc_queue_append nb_crc = %d\n", nb_crc);
#endif
	return nb_crc;


}

//Remove the crc from the top of the queue
unsigned int crc_queue_remove (unsigned int chan) {

	unsigned int res;
//	crc_t plast;
//	crc_t pnext;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf("In crc_queue_remove\n");
#endif
	crc_t removed_crc;
	if (chan>=MAX_CHANS) {
	  return 0;
	}
	removed_crc = crc_queue_of_last_to_app[chan];
	if (removed_crc == NULL) {
	  return 0;
	}
	else {

	  crc_queue_of_last_to_app[chan] = removed_crc->nextp;
	  res = removed_crc->crc;
	  free(removed_crc);
	  
	}
	return res;

}

unsigned char is_crc_in_queue(unsigned int chan, unsigned int crc) { 
	crc_t plast;
	crc_t pnext;

	if (crc_queue_of_last_to_app[chan] == NULL) {
	  return 0;
	}
	else {
	  plast = crc_queue_of_last_to_app[chan];
	  do {
	    pnext = plast->nextp;
	    if (plast->crc == crc) {
	      return 1;
	    }
	    plast = pnext;
	  } while (pnext != NULL);
	}
	return 0;
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
 *------------------------------------------------------------------------------*/


__attribute__((hot))
void multi_modem_process_sample (int chan, int audio_sample) 
{
	int d;
	int subchan;

	/* Formerly one loop. */
	/* 1.2: We can feed one demodulator but end up with multiple outputs. */


	for (d = 0; d < save_audio_config_p->achan[chan].num_demod; d++) {

	  demod_process_sample(chan, d, audio_sample);
	}

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {

	  if (candidate[chan][subchan].packet_p != NULL) {
	    candidate[chan][subchan].age++;
	    if (candidate[chan][subchan].age > process_age[chan]) {
	      pick_best_candidate (chan);
	    }
	  }  
	}}



/*-------------------------------------------------------------------
 *
 * Name:        multi_modem_process_rec_frame
 *
 * Purpose:     This is called when we receive a frame with a valid 
 *		FCS and acceptable size.
 *
 * Inputs:	chan	- Audio channel number, 0 or 1.
 *		subchan	- Which modem/decoder found it.
 *		fbuf	- Pointer to first byte in HDLC frame.
 *		flen	- Number of bytes excluding the FCS.
 *		alevel	- Audio level, range of 0 - 100.
 *				(Special case, use negative to skip
 *				 display of audio level line.
 *				 Use -2 to indicate DTMF message.)
 *		retries	- Level of bit correction used.
 *
 *
 * Description:	Add to list of candidates.  Best one will be picked later.
 *
 *--------------------------------------------------------------------*/

/*
 
	It gets a little more complicated when we try fixing frames
	with imperfect CRCs.   

	Changing of adjacent bits is quick and done immediately.  These
	all come in at nearly the same time.  The processing of two 
	separated bits can take a long time and is handled in the 
	background by another thread.  These could come in seconds later.

	We need a way to remove duplicates.  I think these are the
	two cases we need to consider.

	(1) Same result as earlier no error or adjacent bit errors.

		____||||_
		0.0: ptr=00000000
		0.1: ptr=00000000
		0.2: ptr=00000000
		0.3: ptr=00000000
		0.4: ptr=009E5540, retry=0, age=295, crc=9458, score=5024
		0.5: ptr=0082F008, retry=0, age=294, crc=9458, score=5026  ***
		0.6: ptr=009CE560, retry=0, age=293, crc=9458, score=5026
		0.7: ptr=009CEE08, retry=0, age=293, crc=9458, score=5024
		0.8: ptr=00000000

		___._____
		0.0: ptr=00000000
		0.1: ptr=00000000
		0.2: ptr=00000000
		0.3: ptr=009E5540, retry=4, age=295, crc=9458, score=1000  ***
		0.4: ptr=00000000
		0.5: ptr=00000000
		0.6: ptr=00000000
		0.7: ptr=00000000
		0.8: ptr=00000000

	(2) Only results from adjusting two non-adjacent bits.


		||||||||_
		0.0: ptr=022EBA08, retry=0, age=289, crc=5acd, score=5042
		0.1: ptr=022EA8B8, retry=0, age=290, crc=5acd, score=5048
		0.2: ptr=022EB160, retry=0, age=290, crc=5acd, score=5052
		0.3: ptr=05BD0048, retry=0, age=291, crc=5acd, score=5054  ***
		0.4: ptr=04FE0048, retry=0, age=292, crc=5acd, score=5054
		0.5: ptr=05E10048, retry=0, age=294, crc=5acd, score=5052
		0.6: ptr=053D0048, retry=0, age=294, crc=5acd, score=5048
		0.7: ptr=02375558, retry=0, age=295, crc=5acd, score=5042
		0.8: ptr=00000000

		_______._
		0.0: ptr=00000000
		0.1: ptr=00000000
		0.2: ptr=00000000
		0.3: ptr=00000000
		0.4: ptr=00000000
		0.5: ptr=00000000
		0.6: ptr=00000000
		0.7: ptr=02375558, retry=4, age=295, crc=5fc5, score=1000  ***
		0.8: ptr=00000000

		________.
		0.0: ptr=00000000
		0.1: ptr=00000000
		0.2: ptr=00000000
		0.3: ptr=00000000
		0.4: ptr=00000000
		0.5: ptr=00000000
		0.6: ptr=00000000
		0.7: ptr=00000000
		0.8: ptr=02375558, retry=4, age=295, crc=5fc5, score=1000  ***


	These can both be covered by keepin the last CRC and dropping 
	duplicates.  In theory we could get another frame in between with
	a slow computer so the complete solution would be to remember more
	than one.
*/

void multi_modem_process_rec_frame (int chan, int subchan, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries)  
{	
	packet_t pp;


	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= 0 && subchan < MAX_SUBCHANS);

	pp = ax25_from_frame (fbuf, flen, alevel);

	if (pp == NULL) {
	  return;	/* oops!  why would it fail? */
	}


/*
 * If single modem/deocder, push it thru and forget about all this foolishness.
 */
	if (save_audio_config_p->achan[chan].num_subchan == 1) {
	  dlq_append (DLQ_REC_FRAME, chan, subchan, pp, alevel, retries, "");
	  return;
	}

/*
 * Special handing for two separated bit errors.
 * See description earlier.
 *
 * Not combined with others to find the best score.
 * Either pass it along or drop if duplicate.
 */

	if (retries >= RETRY_SWAP_TWO_SEP) {
	  int mycrc;
	  char spectrum[MAX_SUBCHANS+1];
	  int dropped = 0;

	  memset (spectrum, 0, sizeof(spectrum));
	  memset (spectrum, '_', (size_t)save_audio_config_p->achan[chan].num_subchan);
	  spectrum[subchan] = '.';

	  mycrc = ax25_m_m_crc(pp);
/* Smetimes recovered packet is not the latest one send to the app:
 * It can be a packet sent to the app before the latest one because of the processing time ...
 * So we check if the crc of current packet has already been received in the queue of others crc
 */
	  dropped = is_crc_in_queue(chan, mycrc);

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("\n%s\n%d.%d: ptr=%p, retry=%d, age=, crc=%04x, score=  , dropped =%d\n", 
		spectrum, chan, subchan, pp, (int)retries, mycrc,dropped);
#endif	   
	  if (dropped) {
	     /* Same as last one.  Drop it. */
	     ax25_delete (pp);
#if DEBUG
	     dw_printf ("Drop duplicate.\n");
#endif
	     return;
	   }

#if DEBUG
	  dw_printf ("Send the best one along.\n");
#endif
	  dlq_append (DLQ_REC_FRAME, chan, subchan, pp, alevel, retries, spectrum);
	  if (crc_queue_append(mycrc, chan) > MAX_STORED_CRC)
	    crc_queue_remove(chan);
	  return;
	}


/*
 * Otherwise, save them up for a few bit times so we can pick the best.
 */
	if (candidate[chan][subchan].packet_p != NULL) {
	  /* Oops!  Didn't expect it to be there. */
	  ax25_delete (candidate[chan][subchan].packet_p);
	  candidate[chan][subchan].packet_p = NULL;
	}

	candidate[chan][subchan].packet_p = pp;
	candidate[chan][subchan].alevel = alevel;
	candidate[chan][subchan].retries = retries;
	candidate[chan][subchan].age = 0;
	candidate[chan][subchan].crc = ax25_m_m_crc(pp);
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


static void pick_best_candidate (int chan) 
{
	int subchan;
	int best_subchan, best_score;
	char spectrum[MAX_SUBCHANS+1];
	int k;

	memset (spectrum, 0, sizeof(spectrum));

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {

	  /* Build the spectrum display. */

	  if (candidate[chan][subchan].packet_p == NULL) {
	    spectrum[subchan] = '_';
	  }
	  else if (candidate[chan][subchan].retries == RETRY_NONE) {
	    spectrum[subchan] = '|';
	  }
	  else if (candidate[chan][subchan].retries == RETRY_SWAP_SINGLE) {
	    spectrum[subchan] = ':';
	  }
	  else  {
	    spectrum[subchan] = '.';
	  }

	  /* Begining score depends on effort to get a valid frame CRC. */

	  candidate[chan][subchan].score = RETRY_MAX * 1000 - ((int)candidate[chan][subchan].retries * 1000);

	  /* Bump it up slightly if others nearby have the same CRC. */
	  
	  for (k = 0; k < save_audio_config_p->achan[chan].num_subchan; k++) {
	    if (k != subchan && candidate[chan][k].packet_p != NULL) {
	      if (candidate[chan][k].crc == candidate[chan][subchan].crc) {
	        candidate[chan][subchan].score += (MAX_SUBCHANS+1) - abs(subchan-k);
	      }
	    }
	  }
	}
  
	best_subchan = 0;
	best_score = 0;

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {
	  if (candidate[chan][subchan].packet_p != NULL) {
	    if (candidate[chan][subchan].score > best_score) {
	       best_score = candidate[chan][subchan].score;
	       best_subchan = subchan;
	    }
	  }
	}

#if DEBUG	
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n%s\n", spectrum);

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {

	  if (candidate[chan][subchan].packet_p == NULL) {
	    dw_printf ("%d.%d: ptr=%p\n", chan, subchan,
		candidate[chan][subchan].packet_p);
	  }
	  else {
	    dw_printf ("%d.%d: ptr=%p, retry=%d, age=%3d, crc=%04x, score=%d  %s\n", chan, subchan,
		candidate[chan][subchan].packet_p, 
		(int)(candidate[chan][subchan].retries), 
		candidate[chan][subchan].age,
		candidate[chan][subchan].crc,
		candidate[chan][subchan].score,
		subchan == best_subchan ? "***" : "");
	  }
	}
#endif

/*
 * send the best one along.
 */

#if 1		// v1.2 dev F, Reverse original order.  Delete rejects THEN process the best one.


	/* Delete those not chosen. */

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {
	  if (subchan != best_subchan && candidate[chan][subchan].packet_p != NULL) {
	    ax25_delete (candidate[chan][subchan].packet_p);
	    candidate[chan][subchan].packet_p = NULL;
	  }
	}

	/* Pass along one. */

	dlq_append (DLQ_REC_FRAME, chan, best_subchan, 
		candidate[chan][best_subchan].packet_p, 
		candidate[chan][best_subchan].alevel, 
		(int)(candidate[chan][best_subchan].retries), 
		spectrum);
	if (crc_queue_append(candidate[chan][best_subchan].crc, chan) > MAX_STORED_CRC)
	    crc_queue_remove(chan);

	/* Someone else owns it now and will delete it later. */
	candidate[chan][best_subchan].packet_p = NULL;

	/* Clear in preparation for next time. */

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {

	  candidate[chan][subchan].alevel.rec = 0;
	  candidate[chan][subchan].alevel.mark = 0;
	  candidate[chan][subchan].alevel.space = 0;

	  candidate[chan][subchan].retries = 0;
	  candidate[chan][subchan].age = 0;
	  candidate[chan][subchan].crc = 0;
	}
#else

	dlq_append (DLQ_REC_FRAME, chan, best_subchan, 
		candidate[chan][best_subchan].packet_p, 
		candidate[chan][best_subchan].alevel, 
		(int)(candidate[chan][best_subchan].retries), 
		spectrum);
	if (crc_queue_append(candidate[chan][best_subchan].crc, chan) > MAX_STORED_CRC)
	    crc_queue_remove(chan);
	/* Someone else will delete so don't do it below. */
	candidate[chan][best_subchan].packet_p = NULL;

	/* Clear out in preparation for next time. */

	for (subchan = 0; subchan < save_audio_config_p->achan[chan].num_subchan; subchan++) {
	  if (candidate[chan][subchan].packet_p != NULL) {
	    ax25_delete (candidate[chan][subchan].packet_p);
	    candidate[chan][subchan].packet_p = NULL;
	  }

	  candidate[chan][subchan].alevel.rec = 0;
	  candidate[chan][subchan].alevel.mark = 0;
	  candidate[chan][subchan].alevel.space = 0;

	  candidate[chan][subchan].retries = 0;
	  candidate[chan][subchan].age = 0;
	  candidate[chan][subchan].crc = 0;
	}

#endif

}


/* end multi_modem.c */

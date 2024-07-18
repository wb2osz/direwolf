
//#define DEBUG 1


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015, 2016  John Langner, WB2OSZ
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
 * Module:      dtmf.c
 *
 * Purpose:   	Decoder for DTMF, commonly known as "touch tones."
 *		
 * Description: This uses the Goertzel Algorithm for tone detection.
 *
 * References:	http://eetimes.com/design/embedded/4024443/The-Goertzel-Algorithm
 * 		http://www.ti.com/ww/cn/uprogram/share/ppt/c5000/17dtmf_v13.ppt
 *
 * Revisions:	1.4 - Added transmit capability.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <string.h>

#include "dtmf.h"
#include "hdlc_rec.h"	// for dcd_change
#include "textcolor.h"
#include "gen_tone.h"



#if DTMF_TEST
#define TIMEOUT_SEC 1	/* short for unit test below. */
#define DEBUG 1		// Don't remove this.  We want more output for test.
#else
#define TIMEOUT_SEC 5	/* for normal operation. */
#endif


#define NUM_TONES 8
static int const dtmf_tones[NUM_TONES] = { 697, 770, 852, 941, 1209, 1336, 1477, 1633 };

/*
 * Current state of the DTMF decoding. 
 */

static struct dd_s {	 /* Separate for each audio channel. */

	int sample_rate;	/* Samples per sec.  Typ. 44100, 8000, etc. */
	int block_size;		/* Number of samples to process in one block. */
	float coef[NUM_TONES];	

	int n;			/* Samples processed in this block. */
	float Q1[NUM_TONES];
	float Q2[NUM_TONES];
	char prev_dec;	
	char debounced;
	char prev_debounced;
	int timeout;

} dd[MAX_RADIO_CHANS];


static int s_amplitude = 100;	// range of 0 .. 100


static void push_button (int chan, char button, int ms);


/*------------------------------------------------------------------
 *
 * Name:        dtmf_init
 *
 * Purpose:     Initialize the DTMF decoder.
 *		This should be called once at application start up time.
 *
 * Inputs:      p_audio_config - Configuration for audio interfaces.
 *
 *			All we care about is:
 *
 *				samples_per_sec - Audio sample frequency, typically 
 *				  		44100, 22050, 8000, etc.
 *
 *			This is a associated with the soundcard.
 *			In version 1.2, we can have multiple soundcards
 *			with potentially different sample rates.
 *
 *		amp		- Signal amplitude, for transmit, on scale of 0 .. 100.
 *
 *				  100 will produce maximum amplitude of +-32k samples.
 *
 * Returns:     None.
 *
 *----------------------------------------------------------------*/


void dtmf_init (struct audio_s *p_audio_config, int amp)
{
	int j;		/* Loop over all tones frequencies. */
	int c;		/* Loop over all audio channels. */
	

	s_amplitude = amp;

/*
 * Pick a suitable processing block size.
 * Larger = narrower bandwidth, slower response.
 */

	for (c=0; c<MAX_RADIO_CHANS; c++) {
	  struct dd_s *D = &(dd[c]);
	  int a = ACHAN2ADEV(c);

	  D->sample_rate = p_audio_config->adev[a].samples_per_sec;

	  if (p_audio_config->achan[c].dtmf_decode != DTMF_DECODE_OFF) {

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("channel %d:\n", c);
#endif
	    D->block_size = (205 * D->sample_rate) / 8000;

#if DEBUG
	    dw_printf ("    freq      k     coef    \n");
#endif
	    for (j=0; j<NUM_TONES; j++) {
	      float k; 


// Why do some insist on rounding k to the nearest integer?
// That would move the filter center frequency away from ideal.
// What is to be gained?
// More consistent results for all the tones when k is not rounded off.

	      k = D->block_size * (float)(dtmf_tones[j]) / (float)(D->sample_rate);

	      D->coef[j] = 2.0f * cosf(2.0f * (float)M_PI * (float)k / (float)(D->block_size));

	      assert (D->coef[j] > 0.0f && D->coef[j] < 2.0f);
#if DEBUG
	      dw_printf ("%8d   %5.1f   %8.5f  \n", dtmf_tones[j], k, D->coef[j]);
#endif
	    }
	  }
	}

	for (c=0; c<MAX_RADIO_CHANS; c++) {
	  struct dd_s *D = &(dd[c]); 
	  D->n = 0;
	  for (j=0; j<NUM_TONES; j++) {
	    D->Q1[j] = 0;
	    D->Q2[j] = 0;
	  }
	  D->prev_dec = ' ';
	  D->debounced = ' ';
	  D->prev_debounced = ' ';
	  D->timeout = 0;
	}

}

/*------------------------------------------------------------------
 *
 * Name:        dtmf_sample
 *
 * Purpose:     Process one audio sample from the sound input source.
 *
 * Inputs:	c	- Audio channel number.
 *			  This can process multiple channels in parallel.
 *		input	- Audio sample.
 *
 * Returns:     0123456789ABCD*# for a button push.
 *		. for nothing happening during sample interval.
 *		$ after several seconds of inactivity.
 *		space between sample intervals.
 *		
 *
 *----------------------------------------------------------------*/
				
__attribute__((hot))
char dtmf_sample (int c, float input)
{
	int i;
	float Q0;
	float output[NUM_TONES];
	char decoded;
	char ret;
	struct dd_s *D;
	static const char rc2char[16] = { 	'1', '2', '3', 'A',
						'4', '5', '6', 'B',
						'7', '8', '9', 'C',
						'*', '0', '#', 'D' };

// Only applies to radio channels.  Should not be here.
	if (c >= MAX_RADIO_CHANS) {
	  return ('$');
	}

	D = &(dd[c]);

	for (i=0; i<NUM_TONES; i++) {
	  Q0 = input + D->Q1[i] * D->coef[i] - D->Q2[i];
	  D->Q2[i] = D->Q1[i];
	  D->Q1[i] = Q0;
	}

/*
 * Is it time to process the block?
 */
	D->n++;
	if (D->n == D->block_size) {
	  int row, col;

	  for (i=0; i<NUM_TONES; i++) {
	    output[i] = sqrt(D->Q1[i] * D->Q1[i] + D->Q2[i] * D->Q2[i] - D->Q1[i] * D->Q2[i] * D->coef[i]);
	    D->Q1[i] = 0;
	    D->Q2[i] = 0;
	  }
	  D->n = 0;


/*
 * The input signal can vary over a couple orders of
 * magnitude so we can't set some absolute threshold.
 *
 * See if one tone is stronger than the sum of the 
 * others in the same group multiplied by some factor.
 *
 * For perfect synthetic signals this needs to be in
 * the range of about 1.33 (very sensitive) to 2.15 (very fussy).
 *
 * Too low will cause false triggers on random noise.
 * Too high will won't decode less than perfect signals.
 *
 * Use the mid point 1.74 as our initial guess.
 * It might need some fine tuning for imperfect real world signals.
 */


#define THRESHOLD 1.74f

	  if      (output[0] > THRESHOLD * (            output[1] + output[2] + output[3])) row = 0;
	  else if (output[1] > THRESHOLD * (output[0]             + output[2] + output[3])) row = 1;
	  else if (output[2] > THRESHOLD * (output[0] + output[1]             + output[3])) row = 2;
	  else if (output[3] > THRESHOLD * (output[0] + output[1] + output[2]            )) row = 3;
	  else row = -1;

	  if      (output[4] > THRESHOLD * (            output[5] + output[6] + output[7])) col = 0;
	  else if (output[5] > THRESHOLD * (output[4]             + output[6] + output[7])) col = 1;
	  else if (output[6] > THRESHOLD * (output[4] + output[5]             + output[7])) col = 2;
	  else if (output[7] > THRESHOLD * (output[4] + output[5] + output[6]            )) col = 3;
	  else col = -1;

	  for (i=0; i<NUM_TONES; i++) {
#if DEBUG
	    dw_printf ("%5.0f ", output[i]);
#endif
	  }
	  if (row >= 0 && col >= 0) {
	    decoded = rc2char[row*4+col];
	  }
	  else {
	    decoded = ' ';
	  }

// Consider valid only if we get same twice in a row.

	  if (decoded == D->prev_dec) {
	    D->debounced = decoded;

	    // Update Data Carrier Detect Indicator.

#ifndef DTMF_TEST
	    dcd_change (c, MAX_SUBCHANS, 0, decoded != ' ');
#endif

	    /* Reset timeout timer. */
	    if (decoded != ' ') {
	      D->timeout = ((TIMEOUT_SEC) * D->sample_rate) / D->block_size;
	    }
	  }
	  D->prev_dec = decoded;

// Return only new button pushes.
// Also report timeout after period of inactivity.

	  ret = '.';
	  if (D->debounced != D->prev_debounced) {
	    if (D->debounced != ' ') {
	      ret = D->debounced;
	    }
	  }
	  if (ret == '.') {
	    if (D->timeout > 0) {
	      D->timeout--;
	      if (D->timeout == 0) {
	        ret = '$';
              }
            }
	  }
	  D->prev_debounced = D->debounced;

#if DEBUG
	  dw_printf ("     dec=%c, deb=%c, ret=%c, to=%d \n", 
			decoded, D->debounced, ret, D->timeout);
#endif
	  return (ret);
	}

 	return (' ');
}



/*-------------------------------------------------------------------
 *
 * Name:        dtmf_send
 *
 * Purpose:    	Generate DTMF tones from text string.
 *
 * Inputs:	chan	- Radio channel number.
 *		str	- Character string to send.  0-9, A-D, *, #
 *		speed	- Number of tones per second.  Range 1 to 10.
 *		txdelay	- Delay (ms) from PTT to start.
 *		txtail	- Delay (ms) from end to PTT off.
 *
 * Returns:	Total number of milliseconds to activate PTT.
 *		This includes delays before the first tone
 *		and after the last to avoid chopping off part of it.
 *
 * Description:	xmit_thread calls this instead of the usual hdlc_send
 *		when we have a special packet that means send DTMF.
 *
 *--------------------------------------------------------------------*/

int dtmf_send (int chan, char *str, int speed, int txdelay, int txtail)
{
	char *p;
	int len_ms;	// Length of tone or gap between.

	len_ms = (int) ( ( 500.0f / (float)speed ) + 0.5f);

	push_button (chan, ' ', txdelay);

	for (p = str; *p != '\0'; p++) {

	  push_button (chan, *p, len_ms);
	  push_button (chan, ' ', len_ms);
	}

	push_button (chan, ' ', txtail);

#ifndef DTMF_TEST
	audio_flush(ACHAN2ADEV(chan));
#endif
	return (txdelay +
		(int) (1000.0f * (float)strlen(str) / (float)speed + 0.5f) +
		txtail);

}  /* end dtmf_send */



/*------------------------------------------------------------------
 *
 * Name:        push_button
 *
 * Purpose:     Generate DTMF tone for a button push.
 *
 * Inputs:	chan	- Radio channel number.
 *
 *		button	- One of 0-9, A-D, *, #.  Others result in silence.
 *			  '?' is a special case used only for unit testing.
 *
 *		ms	- Duration in milliseconds.
 *			  Use 50 ms for tone and 50 ms of silence for max rate of 10 per second.
 *
 * Outputs:	Audio is sent to radio.
 *
 *----------------------------------------------------------------*/

static void push_button (int chan, char button, int ms)
{
	float phasea = 0;
	float phaseb = 0;
	float fa = 0;
	float fb = 0;
	int i;
	float dtmf;	// Audio.  Sum of two sine waves.
#if DTMF_TEST
	char x;
	static char result[100];
	static int result_len = 0;
#endif

	switch (button) {
	  case '1':  fa = dtmf_tones[0]; fb = dtmf_tones[4]; break;
	  case '2':  fa = dtmf_tones[0]; fb = dtmf_tones[5]; break;
	  case '3':  fa = dtmf_tones[0]; fb = dtmf_tones[6]; break;
	  case 'a':
	  case 'A':  fa = dtmf_tones[0]; fb = dtmf_tones[7]; break;

	  case '4':  fa = dtmf_tones[1]; fb = dtmf_tones[4]; break;
	  case '5':  fa = dtmf_tones[1]; fb = dtmf_tones[5]; break;
	  case '6':  fa = dtmf_tones[1]; fb = dtmf_tones[6]; break;
	  case 'b':
	  case 'B':  fa = dtmf_tones[1]; fb = dtmf_tones[7]; break;

	  case '7':  fa = dtmf_tones[2]; fb = dtmf_tones[4]; break;
	  case '8':  fa = dtmf_tones[2]; fb = dtmf_tones[5]; break;
	  case '9':  fa = dtmf_tones[2]; fb = dtmf_tones[6]; break;
	  case 'c':
	  case 'C':  fa = dtmf_tones[2]; fb = dtmf_tones[7]; break;

	  case '*':  fa = dtmf_tones[3]; fb = dtmf_tones[4]; break;
	  case '0':  fa = dtmf_tones[3]; fb = dtmf_tones[5]; break;
	  case '#':  fa = dtmf_tones[3]; fb = dtmf_tones[6]; break;
	  case 'd':
	  case 'D':  fa = dtmf_tones[3]; fb = dtmf_tones[7]; break;

#if DTMF_TEST

	  case '?':	/* check result */

	    if (strcmp(result, "123A456B789C*0#D123$789$") == 0) {
	      text_color_set(DW_COLOR_REC);
	      dw_printf ("\nSuccess!\n");
	    }
	    else if (strcmp(result, "123A456B789C*0#D123789") == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\n * Time-out failed, otherwise OK *\n");
	      dw_printf ("\"%s\"\n", result);
	      exit (EXIT_FAILURE);
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\n *** TEST FAILED ***\n");
	      dw_printf ("\"%s\"\n", result);
	      exit (EXIT_FAILURE);
	    }
	    break;
#endif
	}

	//dw_printf ("push_button (%d, '%c', %d), fa=%.0f, fb=%.0f. %d samples\n", chan, button, ms, fa, fb, (ms*dd[chan].sample_rate)/1000);

	for (i = 0; i < (ms*dd[chan].sample_rate)/1000; i++) {

	  // This could be more efficient with a precomputed sine wave table
	  // but I'm not that worried about it.
	  // With a Raspberry Pi, model 2, default 1200 receiving takes about 14% of one CPU core.
	  // When transmitting tones, it briefly shoots up to about 33%.

	  if (fa > 0 && fb > 0) {
	    dtmf = sinf(phasea) + sinf(phaseb);
	    phasea += 2.0f * (float)M_PI * fa / dd[chan].sample_rate;
	    phaseb += 2.0f * (float)M_PI * fb / dd[chan].sample_rate;
	  }
	  else {
	    dtmf = 0;
	  }

#if DTMF_TEST

	  /* Make sure it is insensitive to signal amplitude. */
	  /* (Uncomment each of below when testing.) */

	  x = dtmf_sample (0, dtmf);
	  //x = dtmf_sample (0, dtmf * 1000);
	  //x = dtmf_sample (0, dtmf * 0.001);

	  if (x != ' ' && x != '.') {
	    result[result_len] = x;
	    result_len++;
	    result[result_len] = '\0';
	  }
#else

	  // 'dtmf' can be in range of +-2.0 because it is sum of two sine waves.
	  // Amplitude of 100 would use full +-32k range.

	  int sam = (int)(dtmf * 16383.0f * (float)s_amplitude / 100.0f);
	  gen_tone_put_sample (chan, ACHAN2ADEV(chan), sam);

#endif
	}
}


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Unit test for functions above.
 *
 * Usage:	rm a.exe ; gcc -DDTMF_TEST dtmf.c textcolor.c ; ./a.exe
 *		or
 *		make dtmftest
 *
 *----------------------------------------------------------------*/

#if DTMF_TEST

static struct audio_s my_audio_config;


int main ()
{
	int c = 0;	// radio channel.

	memset (&my_audio_config, 0, sizeof(my_audio_config));
	my_audio_config.adev[ACHAN2ADEV(c)].defined = 1;
	my_audio_config.adev[ACHAN2ADEV(c)].samples_per_sec = 44100;
	my_audio_config.chan_medium[c] = MEDIUM_RADIO;
	my_audio_config.achan[c].dtmf_decode = DTMF_DECODE_ON;

	dtmf_init(&my_audio_config, 50);	

	text_color_set(DW_COLOR_INFO);
	dw_printf ("\nFirst, check all button tone pairs. \n\n");
	/* Max auto dialing rate is 10 per second. */

	push_button (c,  '1', 50); push_button (c,  ' ', 50);
	push_button (c,  '2', 50); push_button (c,  ' ', 50);
	push_button (c,  '3', 50); push_button (c,  ' ', 50);
	push_button (c,  'A', 50); push_button (c,  ' ', 50);

	push_button (c,  '4', 50); push_button (c,  ' ', 50);
	push_button (c,  '5', 50); push_button (c,  ' ', 50);
	push_button (c,  '6', 50); push_button (c,  ' ', 50);
	push_button (c,  'B', 50); push_button (c,  ' ', 50);

	push_button (c,  '7', 50); push_button (c,  ' ', 50);
	push_button (c,  '8', 50); push_button (c,  ' ', 50);
	push_button (c,  '9', 50); push_button (c,  ' ', 50);
	push_button (c,  'C', 50); push_button (c,  ' ', 50);

	push_button (c,  '*', 50); push_button (c,  ' ', 50);
	push_button (c,  '0', 50); push_button (c,  ' ', 50);
	push_button (c,  '#', 50); push_button (c,  ' ', 50);
	push_button (c,  'D', 50); push_button (c,  ' ', 50);

	text_color_set(DW_COLOR_INFO);
	dw_printf ("\nShould reject very short pulses.\n\n");
	
	push_button (c,  '1', 20); push_button (c,  ' ', 50);
	push_button (c,  '1', 20); push_button (c,  ' ', 50);
	push_button (c,  '1', 20); push_button (c,  ' ', 50);
	push_button (c,  '1', 20); push_button (c,  ' ', 50);
	push_button (c,  '1', 20); push_button (c,  ' ', 50);

	text_color_set(DW_COLOR_INFO);
	dw_printf ("\nTest timeout after inactivity.\n\n");
	/* For this test we use 1 second. */
	/* In practice, it will probably more like 5. */

	push_button (c,  '1', 250); push_button (c,  ' ', 500);
	push_button (c,  '2', 250); push_button (c,  ' ', 500);
	push_button (c,  '3', 250); push_button (c,  ' ', 1200);

	push_button (c,  '7', 250); push_button (c,  ' ', 500);
	push_button (c,  '8', 250); push_button (c,  ' ', 500);
	push_button (c,  '9', 250); push_button (c,  ' ', 1200);

	/* Check for expected results. */

	push_button (c,  '?', 0);

	exit (EXIT_SUCCESS);

}  /* end main */

#endif

/* end dtmf.c */


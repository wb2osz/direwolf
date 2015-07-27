//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013  John Langner, WB2OSZ
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
 *---------------------------------------------------------------*/


#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#include "direwolf.h"
#include "dtmf.h"


// Define for unit test.
//#define DTMF_TEST 1


#if DTMF_TEST
#define TIMEOUT_SEC 1	/* short for unit test below. */
#define DEBUG 1
#else
#define TIMEOUT_SEC 5	/* for normal operation. */
#endif


#define NUM_TONES 8
static int const dtmf_tones[NUM_TONES] = { 697, 770, 852, 941, 1209, 1336, 1477, 1633 };

/*
 * Current state of the decoding. 
 */

static struct {
	int sample_rate;	/* Samples per sec.  Typ. 44100, 8000, etc. */
	int block_size;		/* Number of samples to process in one block. */
	float coef[NUM_TONES];	

	struct {		/* Separate for each audio channel. */

		int n;		/* Samples processed in this block. */
		float Q1[NUM_TONES];
		float Q2[NUM_TONES];
		char prev_dec;	
		char debounced;
		char prev_debounced;
		int timeout;
	} C[MAX_CHANS];		
} D;




/*------------------------------------------------------------------
 *
 * Name:        dtmf_init
 *
 * Purpose:     Initialize the DTMF decoder.
 *		This should be called once at application start up time.
 *
 * Inputs:      sample_rate	- Audio sample frequency, typically 
 *				  44100, 22050, 8000, etc.
 *
 * Returns:     None.
 *
 *----------------------------------------------------------------*/

void dtmf_init (int sample_rate)
{
	int j;		/* Loop over all tones frequencies. */
	int c;		/* Loop over all audio channels. */

/*
 * Processing block size.
 * Larger = narrower bandwidth, slower response.
 */
	D.sample_rate = sample_rate;
	D.block_size = (205 * sample_rate) / 8000;

#if DEBUG
	dw_printf ("    freq      k     coef    \n");
#endif
	for (j=0; j<NUM_TONES; j++) {
	  float k; 


// Why do some insist on rounding k to the nearest integer?
// That would move the filter center frequency away from ideal.
// What is to be gained?
// More consistent results for all the tones when k is not rounded off.

	  k = D.block_size * (float)(dtmf_tones[j]) / (float)(D.sample_rate);

	  D.coef[j] = 2 * cos(2 * M_PI * (float)k / (float)(D.block_size));

	  assert (D.coef[j] > 0 && D.coef[j] < 2.0);
#if DEBUG
	  dw_printf ("%8d   %5.1f   %8.5f  \n", dtmf_tones[j], k, D.coef[j]);
#endif
	}

	for (c=0; c<MAX_CHANS; c++) {
	  D.C[c].n = 0;
	  for (j=0; j<NUM_TONES; j++) {
	    D.C[c].Q1[j] = 0;
	    D.C[c].Q2[j] = 0;
	  }
	  D.C[c].prev_dec = ' ';
	  D.C[c].debounced = ' ';
	  D.C[c].prev_debounced = ' ';
	  D.C[c].timeout = 0;
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
	static const char rc2char[16] = { 	'1', '2', '3', 'A',
						'4', '5', '6', 'B',
						'7', '8', '9', 'C',
						'*', '0', '#', 'D' };
	for (i=0; i<NUM_TONES; i++) {
	  Q0 = input + D.C[c].Q1[i] * D.coef[i] - D.C[c].Q2[i];
	  D.C[c].Q2[i] = D.C[c].Q1[i];
	  D.C[c].Q1[i] = Q0;
	}

/*
 * Is it time to process the block?
 */
	D.C[c].n++;
	if (D.C[c].n == D.block_size) {
	  int row, col;

	  for (i=0; i<NUM_TONES; i++) {
	    output[i] = sqrt(D.C[c].Q1[i] * D.C[c].Q1[i] + D.C[c].Q2[i] * D.C[c].Q2[i] - D.C[c].Q1[i] * D.C[c].Q2[i] * D.coef[i]);
	    D.C[c].Q1[i] = 0;
	    D.C[c].Q2[i] = 0;
	  }
	  D.C[c].n = 0;


/*
 * The input signal can vary over a couple orders of
 * magnitude so we can't set some absolute threshold.
 *
 * See if one tone is stronger than the sum of the 
 * others in the same group multiplied by some factor.
 *
 * For perfect synthetic signals this needs to be in
 * the range of about 1.33 (very senstive) to 2.15 (very fussy).
 *
 * Too low will cause false triggers on random noise.
 * Too high will won't decode less than perfect signals.
 *
 * Use the mid point 1.74 as our initial guess.
 * It might need some fine tuning for imperfect real world signals.
 */


#define THRESHOLD 1.74

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
	    decoded = '.';
	  }

// Consider valid only if we get same twice in a row.

	  if (decoded == D.C[c].prev_dec) {
	    D.C[c].debounced = decoded;
	    /* Reset timeout timer. */
	    if (decoded != ' ') {
	      D.C[c].timeout = ((TIMEOUT_SEC) * D.sample_rate) / D.block_size;
	    }
	  }
	  D.C[c].prev_dec = decoded;

// Return only new button pushes.
// Also report timeout after period of inactivity.

	  ret = '.';
	  if (D.C[c].debounced != D.C[c].prev_debounced) {
	    if (D.C[c].debounced != ' ') {
	      ret = D.C[c].debounced;
	    }
	  }
	  if (ret == '.') {
	    if (D.C[c].timeout > 0) {
	      D.C[c].timeout--;
	      if (D.C[c].timeout == 0) {
	        ret = '$';
              }
            }
	  }
	  D.C[c].prev_debounced = D.C[c].debounced;

#if DEBUG
	  dw_printf ("     dec=%c, deb=%c, ret=%c \n", 
			decoded, D.C[c].debounced, ret);
#endif
	  return (ret);
	}

 	return (' ');
}


/*------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Unit test for functions above.
 *
 *----------------------------------------------------------------*/


#if DTMF_TEST

push_button (char button, int ms)
{
	static float phasea = 0;
	static float phaseb = 0;
	float fa, fb;
	int i;
	float input;
	char x;
	static char result[100];
	static int result_len = 0;


	switch (button) {
	  case '1':  fa = dtmf_tones[0]; fb = dtmf_tones[4]; break;
	  case '2':  fa = dtmf_tones[0]; fb = dtmf_tones[5]; break;
	  case '3':  fa = dtmf_tones[0]; fb = dtmf_tones[6]; break;
	  case 'A':  fa = dtmf_tones[0]; fb = dtmf_tones[7]; break;
	  case '4':  fa = dtmf_tones[1]; fb = dtmf_tones[4]; break;
	  case '5':  fa = dtmf_tones[1]; fb = dtmf_tones[5]; break;
	  case '6':  fa = dtmf_tones[1]; fb = dtmf_tones[6]; break;
	  case 'B':  fa = dtmf_tones[1]; fb = dtmf_tones[7]; break;
	  case '7':  fa = dtmf_tones[2]; fb = dtmf_tones[4]; break;
	  case '8':  fa = dtmf_tones[2]; fb = dtmf_tones[5]; break;
	  case '9':  fa = dtmf_tones[2]; fb = dtmf_tones[6]; break;
	  case 'C':  fa = dtmf_tones[2]; fb = dtmf_tones[7]; break;
	  case '*':  fa = dtmf_tones[3]; fb = dtmf_tones[4]; break;
	  case '0':  fa = dtmf_tones[3]; fb = dtmf_tones[5]; break;
	  case '#':  fa = dtmf_tones[3]; fb = dtmf_tones[6]; break;
	  case 'D':  fa = dtmf_tones[3]; fb = dtmf_tones[7]; break;
	  case '?':

	    if (strcmp(result, "123A456B789C*0#D123$789$") == 0) {
	      dw_printf ("\nSuccess!\n");
	    }
	    else {
	      dw_printf ("\n *** TEST FAILED ***\n");
	      dw_printf ("\"%s\"\n", result);
	    }
	    break;

	  default:  fa = 0; fb = 0;
	}

	for (i = 0; i < (ms*D.sample_rate)/1000; i++) {

	  input = sin(phasea) + sin(phaseb);
	  phasea += 2 * M_PI * fa / D.sample_rate;
	  phaseb += 2 * M_PI * fb / D.sample_rate;

	  /* Make sure it is insensitive to signal amplitude. */

	  x = dtmf_sample (0, input);
	  //x = dtmf_sample (0, input * 1000);
	  //x = dtmf_sample (0, input * 0.001);

	  if (x != ' ' && x != '.') {
	    result[result_len] = x;
	    result_len++;
	    result[result_len] = '\0';
	  }
	}
}

main ()
{

	dtmf_init(44100);	

	dw_printf ("\nFirst, check all button tone pairs. \n\n");
	/* Max auto dialing rate is 10 per second. */

	push_button ('1', 50); push_button (' ', 50);
	push_button ('2', 50); push_button (' ', 50);
	push_button ('3', 50); push_button (' ', 50);
	push_button ('A', 50); push_button (' ', 50);

	push_button ('4', 50); push_button (' ', 50);
	push_button ('5', 50); push_button (' ', 50);
	push_button ('6', 50); push_button (' ', 50);
	push_button ('B', 50); push_button (' ', 50);

	push_button ('7', 50); push_button (' ', 50);
	push_button ('8', 50); push_button (' ', 50);
	push_button ('9', 50); push_button (' ', 50);
	push_button ('C', 50); push_button (' ', 50);

	push_button ('*', 50); push_button (' ', 50);
	push_button ('0', 50); push_button (' ', 50);
	push_button ('#', 50); push_button (' ', 50);
	push_button ('D', 50); push_button (' ', 50);

	dw_printf ("\nShould reject very short pulses.\n\n");
	
	push_button ('1', 20); push_button (' ', 50);
	push_button ('1', 20); push_button (' ', 50);
	push_button ('1', 20); push_button (' ', 50);
	push_button ('1', 20); push_button (' ', 50);
	push_button ('1', 20); push_button (' ', 50);

	dw_printf ("\nTest timeout after inactivity.\n\n");
	/* For this test we use 1 second. */
	/* In practice, it will probably more like 10 or 20. */

	push_button ('1', 250); push_button (' ', 500);
	push_button ('2', 250); push_button (' ', 500);
	push_button ('3', 250); push_button (' ', 1200);

	push_button ('7', 250); push_button (' ', 500);
	push_button ('8', 250); push_button (' ', 500);
	push_button ('9', 250); push_button (' ', 1200);

	/* Check for expected results. */

	push_button ('?', 0);

}  /* end main */

#endif

/* end dtmf.c */


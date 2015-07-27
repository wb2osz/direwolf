//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011  John Langner, WB2OSZ
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
 * Module:      gen_tone.c
 *
 * Purpose:     Convert bits to AFSK for writing to .WAV sound file 
 *		or a sound device.
 *
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "direwolf.h"
#include "audio.h"
#include "gen_tone.h"
#include "textcolor.h"



// Properties of the digitized sound stream & modem.

static struct audio_s modem;

/*
 * 8 bit samples are unsigned bytes in range of 0 .. 255.
 *
 * 16 bit samples are signed short in range of -32768 .. +32767.
 */


/* Constants after initialization. */

#define TICKS_PER_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

static int ticks_per_sample;		/* same for all channels. */

static int ticks_per_bit[MAX_CHANS];
static int f1_change_per_sample[MAX_CHANS];
static int f2_change_per_sample[MAX_CHANS];

static short sine_table[256];


/* Accumulators. */

static unsigned int tone_phase[MAX_CHANS]; // Phase accumulator for tone generation.
					    // Upper bits are used as index into sine table.

static int bit_len_acc[MAX_CHANS];	// To accumulate fractional samples per bit.

static int lfsr[MAX_CHANS];		// Shift register for scrambler.


/*------------------------------------------------------------------
 *
 * Name:        gen_tone_init
 *
 * Purpose:     Initialize for AFSK tone generation which might
 *		be used for RTTY or amateur packet radio.
 *
 * Inputs:      pp		- Pointer to modem parameter structure, modem_s.
 *
 *				The fields we care about are:
 *
 *					samples_per_sec
 *					baud
 *					mark_freq
 *					space_freq
 *					samples_per_sec
 *
 *		amp		- Signal amplitude on scale of 0 .. 100.
 *
 * Returns:     0 for success.
 *              -1 for failure.
 *
 * Description:	 Calculate various constants for use by the direct digital synthesis
 * 		audio tone generation.
 *
 *----------------------------------------------------------------*/

static int amp16bit;	/* for 9600 baud */


int gen_tone_init (struct audio_s *pp, int amp)  
{
	int j;
	int chan = 0;
/* 
 * Save away modem parameters for later use. 
 */

	memcpy (&modem, pp, sizeof(struct audio_s));

	amp16bit = (32767 * amp) / 100;

	ticks_per_sample = (int) ((TICKS_PER_CYCLE / (double)modem.samples_per_sec ) + 0.5);

	for (chan = 0; chan < modem.num_channels; chan++) {

	  ticks_per_bit[chan] = (int) ((TICKS_PER_CYCLE / (double)modem.baud[chan] ) + 0.5);

	  f1_change_per_sample[chan] = (int) (((double)modem.mark_freq[chan] * TICKS_PER_CYCLE / (double)modem.samples_per_sec ) + 0.5);

	  f2_change_per_sample[chan] = (int) (((double)modem.space_freq[chan] * TICKS_PER_CYCLE / (double)modem.samples_per_sec ) + 0.5);

	  tone_phase[chan] = 0;
				
	  bit_len_acc[chan] = 0;

	  lfsr[chan] = 0;
	}

        for (j=0; j<256; j++) {
	  double a;
	  int s;

	  a = ((double)(j) / 256.0) * (2 * M_PI);
	  s = (int) (sin(a) * 32767 * amp / 100.0);

	  /* 16 bit sound sample is in range of -32768 .. +32767. */
	  
	  assert (s >= -32768 && s <= 32767);
	
	  sine_table[j] = s;
        }

	return (0);

 } /* end gen_tone_init */


/*-------------------------------------------------------------------
 *
 * Name:        gen_tone_put_bit
 *
 * Purpose:     Generate tone of proper duration for one data bit.
 *
 * Inputs:      chan	- Audio channel, 0 = first.
 *
 *		dat	- 0 for f1, 1 for f2.
 *
 * 			  	-1 inserts half bit to test data	
 *				recovery PLL.
 *
 * Assumption:  fp is open to a file for write.
 *
 *--------------------------------------------------------------------*/

void tone_gen_put_bit (int chan, int dat)
{
	int cps = dat ? f2_change_per_sample[chan] : f1_change_per_sample[chan];
	unsigned short sam = 0;
	int x;


        if (dat < 0) { 
	  /* Hack to test receive PLL recovery. */
	  bit_len_acc[chan] -= ticks_per_bit[chan]; 
	  dat = 0; 
	} 

	if (modem.modem_type[chan] == SCRAMBLE) {
	  x = (dat ^ (lfsr[chan] >> 16) ^ (lfsr[chan] >> 11)) & 1;
	  lfsr[chan] = (lfsr[chan] << 1) | (x & 1);
	  dat = x;
	}
	  
	do {

	  if (modem.modem_type[chan] == AFSK) {
	    tone_phase[chan] += cps;
            sam = sine_table[(tone_phase[chan] >> 24) & 0xff];
	  }
  	  else {
	    // TODO: Might want to low pass filter this.
	    sam = dat ? amp16bit : (-amp16bit);
	  }

          /* Ship out an audio sample. */

	  assert (modem.num_channels == 1 || modem.num_channels == 2);

	  /* Generalize to allow 8 bits someday? */

	  assert (modem.bits_per_sample == 16);


	  if (modem.num_channels == 1)
	  {
            audio_put (sam & 0xff);
            audio_put ((sam >> 8) & 0xff);
 	  }
	  else if (modem.num_channels == 2)
	  {
	    if (chan == 1)
	    {
              audio_put (0);		// silent left
              audio_put (0);
	    }

            audio_put (sam & 0xff);
            audio_put ((sam >> 8) & 0xff);
            //byte_count += 2;

	    if (chan == 0)
	    {
              audio_put (0);		// silent right
              audio_put (0);
	    }
	  }

	  /* Enough for the bit time? */

	  bit_len_acc[chan] += ticks_per_sample;

        } while (bit_len_acc[chan] < ticks_per_bit[chan]);

	bit_len_acc[chan] -= ticks_per_bit[chan];
}


/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Quick test program for above.
 *
 * Description: Compile like this for unit test:
 *
 *		gcc -Wall -DMAIN -o gen_tone_test gen_tone.c audio.c textcolor.c
 *
 *		gcc -Wall -DMAIN -o gen_tone_test.exe gen_tone.c audio_win.c textcolor.c -lwinmm
 *
 *--------------------------------------------------------------------*/


#if MAIN


int main ()
{
	int n;
	int chan1 = 0;
	int chan2 = 1;
	int r;
	struct audio_s audio_param;


/* to sound card */
/* one channel.  2 times:  one second of each tone. */

	memset (&audio_param, 0, sizeof(audio_param));
	strcpy (audio_param.adevice_in, DEFAULT_ADEVICE);
	strcpy (audio_param.adevice_out, DEFAULT_ADEVICE);

	audio_open (&audio_param);
	gen_tone_init (&audio_param, 100);

	for (r=0; r<2; r++) {

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 1 );
	  }

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 0 );
	  }
	}

	audio_close();

/* Now try stereo. */

	memset (&audio_param, 0, sizeof(audio_param));
	strcpy (audio_param.adevice_in, DEFAULT_ADEVICE);
	strcpy (audio_param.adevice_out, DEFAULT_ADEVICE);
	audio_param.num_channels = 2;

	audio_open (&audio_param);
	gen_tone_init (&audio_param, 100);

	for (r=0; r<4; r++) {

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 1 );
	  }

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan1, 0 );
	  }

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan2, 1 );
	  }

	  for (n=0; n<audio_param.baud[0] * 2 ; n++) {
 	    tone_gen_put_bit ( chan2, 0 );
	  }
	}

	audio_close();

	return(0);
}

#endif


/* end gen_tone.c */

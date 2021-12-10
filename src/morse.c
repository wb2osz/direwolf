//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2015  John Langner, WB2OSZ
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
 * Module:      morse.c
 *
 * Purpose:   	Generate audio for morse code.
 *		
 * Description:	
 *
 * Reference:	
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

#include "textcolor.h"
#include "audio.h"
#include "ptt.h"
#include "gen_tone.h"		/* for gen_tone_put_sample */
#include "morse.h"

/*
 * Might get ambitious and make this adjustable some day.
 * Good enough for now.
 */

#define MORSE_TONE 800

#define TIME_UNITS_TO_MS(tu,wpm)  (((tu)*1200.0)/(wpm))



static const struct morse_s {
	char ch;
	char enc[8];	/* $ has 7 elements */
} morse[] = {
	{ 'A', ".-" },
	{ 'B', "-..." },
	{ 'C', "-.-." },
	{ 'D', "-.." },
	{ 'E', "." },
	{ 'F', "..-." },
	{ 'G', "--." },
	{ 'H', "...." },
	{ 'I', ".." },
	{ 'J', ".---" },
	{ 'K', "-.-" },
	{ 'L', ".-.." },
	{ 'M', "--" },
	{ 'N', "-." },
	{ 'O', "---" },
	{ 'P', ".--." },
	{ 'Q', "--.-" },
	{ 'R', ".-." },
	{ 'S', "..." },
	{ 'T', "-" },
	{ 'U', "..-" },
	{ 'V', "...-" },
	{ 'W', ".--" },
	{ 'X', "-..-" },
	{ 'Y', "-.--" },
	{ 'Z', "--.." },
	{ '1', ".----" },
	{ '2', "..---" },
	{ '3', "...--" },
	{ '4', "....-" },
	{ '5', "....." },
	{ '6', "-...." },
	{ '7', "--..." },
	{ '8', "---.." },
	{ '9', "----." },
	{ '0', "-----" },
	{ '-', "-...-" },
	{ '.', ".-.-.-" },
	{ ',', "--..--" },
	{ '?', "..--.." },
	{ '/', "-..-." },

	{ '=', "-...-" },	/* from ARRL */
	{ '-', "-....-" },
	{ ')', "-.--.-" },	/* does not distinguish open/close */
	{ ':', "---..." },
	{ ';', "-.-.-." },
	{ '"', ".-..-." },
	{ '\'', ".----." },
	{ '$', "...-..-" },

	{ '!', "-.-.--" },	/* more from wikipedia */
	{ '(', "-.--." },
	{ '&', ".-..." },
	{ '+', ".-.-." },
	{ '_', "..--.-" },
	{ '@', ".--.-." },

};

#define NUM_MORSE ((int)(sizeof(morse) / sizeof(struct morse_s)))

static void morse_tone (int chan, int tu, int wpm);
static void morse_quiet (int chan, int tu, int wpm);
static void morse_quiet_ms (int chan, int ms);
static int morse_lookup (int ch);
static int morse_units_ch (int ch);
static int morse_units_str (char *str);



/*
 *  Properties of the digitized sound stream.
 */

static struct audio_s *save_audio_config_p;


/* Constants after initialization. */

#define TICKS_PER_CYCLE ( 256.0 * 256.0 * 256.0 * 256.0 )

static short sine_table[256];



/*------------------------------------------------------------------
 *
 * Name:        morse_init
 *
 * Purpose:     Initialize for tone generation.
 *
 * Inputs:      audio_config_p		- Pointer to audio configuration structure.
 *
 *				The fields we care about are:
 *
 *					samples_per_sec
 *
 *		amp		- Signal amplitude on scale of 0 .. 100.
 *
 *				  100 will produce maximum amplitude of +-32k samples. 
 *
 * Returns:     0 for success.
 *              -1 for failure.
 *
 * Description:	 Precompute a sine wave table.
 *
 *----------------------------------------------------------------*/


int morse_init (struct audio_s *audio_config_p, int amp)  
{
	int j;
	
/* 
 * Save away modem parameters for later use. 
 */

	save_audio_config_p = audio_config_p;
	
        for (j=0; j<256; j++) {
	  double a;
	  int s;

	  a = ((double)(j) / 256.0) * (2 * M_PI);
	  s = (int) (sin(a) * 32767.0 * amp / 100.0);

	  /* 16 bit sound sample is in range of -32768 .. +32767. */
	  assert (s >= -32768 && s <= 32767);
	  sine_table[j] = s;
        }

	return (0);

} /* end morse_init */


/*-------------------------------------------------------------------
 *
 * Name:        morse_send
 *
 * Purpose:    	Given a string, generate appropriate lengths of
 *		tone and silence.
 *
 * Inputs:	chan	- Radio channel number.
 *		str	- Character string to send.
 *		wpm	- Speed in words per minute.
 *		txdelay	- Delay (ms) from PTT to first character.
 *		txtail	- Delay (ms) from last character to PTT off.	
 *		
 *
 * Returns:	Total number of milliseconds to activate PTT.
 *		This includes delays before the first character
 *		and after the last to avoid chopping off part of it.
 *
 * Description:	xmit_thread calls this instead of the usual hdlc_send
 *		when we have a special packet that means send morse
 *		code.
 *
 *--------------------------------------------------------------------*/

int morse_send (int chan, char *str, int wpm, int txdelay, int txtail)
{
	int time_units;
	char *p;

	
	time_units = 0;

	morse_quiet_ms (chan, txdelay);

	for (p = str; *p != '\0'; p++) {
	  int i;

	  i = morse_lookup (*p);
	  if (i >= 0) {
	    const char *e;

	    for (e = morse[i].enc; *e != '\0'; e++) {
	      if (*e == '.') {
	        morse_tone (chan,1,wpm);
	        time_units++;
	      }
	      else {
	        morse_tone (chan,3,wpm);
	        time_units += 3;
	      }
	      if (e[1] != '\0') {
	        morse_quiet (chan,1,wpm);
	        time_units++;
	      }
	    }
	  }
	  else {
	    morse_quiet (chan,1,wpm);
	    time_units++;
	  }
	  if (p[1] != '\0') {
	    morse_quiet (chan,3,wpm);
	    time_units += 3;
	  }
	}

	morse_quiet_ms (chan, txtail);

	if (time_units != morse_units_str(str)) {
	  dw_printf ("morse: Internal error.  Inconsistent length, %d vs. %d calculated.\n", 
		time_units, morse_units_str(str));
	}

	audio_flush(ACHAN2ADEV(chan));

	return (txdelay +
		(int) (TIME_UNITS_TO_MS(time_units, wpm) + 0.5) +
		txtail);

}  /* end morse_send */



/*-------------------------------------------------------------------
 *
 * Name:        morse_tone
 *
 * Purpose:    	Generate tone for specified number of time units.
 *
 * Inputs:	chan	- Radio channel.
 *		tu	- Number of time units.  Should be 1 or 3.
 *		wpm	- Speed in WPM.
 *
 *--------------------------------------------------------------------*/

static void morse_tone (int chan, int tu, int wpm) {

#if MTEST1
	int n;
	for (n=0; n<tu; n++) {
	  dw_printf ("#");
	}
#else

	int a = ACHAN2ADEV(chan);	/* device for channel. */
	int sam;
	int nsamples;
	int j;
	unsigned int tone_phase; // Phase accumulator for tone generation.
				 // Upper bits are used as index into sine table.

	int f1_change_per_sample;  // How much to advance phase for each audio sample.


	if (save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid channel %d for sending Morse Code.\n", chan);
	  return;
	}

	tone_phase = 0;

	f1_change_per_sample = (int) (((double)MORSE_TONE * TICKS_PER_CYCLE / (double)save_audio_config_p->adev[a].samples_per_sec ) + 0.5);

	nsamples = (int) ((TIME_UNITS_TO_MS(tu,wpm) * (float)save_audio_config_p->adev[a].samples_per_sec / 1000.) + 0.5);

	for (j=0; j<nsamples; j++)  {

	  tone_phase += f1_change_per_sample;
          sam = sine_table[(tone_phase >> 24) & 0xff];
	  gen_tone_put_sample (chan, a, sam);

        };

#endif
	

} /* end morse_tone */


/*-------------------------------------------------------------------
 *
 * Name:        morse_quiet
 *
 * Purpose:    	Generate silence for specified number of time units.
 *
 * Inputs:	chan	- Radio channel.
 *		tu	- Number of time units.
 *		wpm	- Speed in WPM.
 *
 *--------------------------------------------------------------------*/

static void morse_quiet (int chan, int tu, int wpm) {


#if MTEST1
	int n;
	for (n=0; n<tu; n++) {
	  dw_printf (".");
	}
#else
	int a = ACHAN2ADEV(chan);	/* device for channel. */
	int sam = 0;
	int nsamples;
	int j;

	if (save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid channel %d for sending Morse Code.\n", chan);
	  return;
	}

	nsamples = (int) ((TIME_UNITS_TO_MS(tu,wpm) * (float)save_audio_config_p->adev[a].samples_per_sec / 1000.) + 0.5);

	for (j=0; j<nsamples; j++)  {

	  gen_tone_put_sample (chan, a, sam);

        };
#endif

} /* end morse_quiet */


/*-------------------------------------------------------------------
 *
 * Name:        morse_quiet
 *
 * Purpose:    	Generate silence for specified number of milliseconds.
 *		This is used for the txdelay and txtail times.
 *
 * Inputs:	chan	- Radio channel.
 *		tu	- Number of time units.
 *
 *--------------------------------------------------------------------*/

static void morse_quiet_ms (int chan, int ms) {

#if MTEST1
#else
	int a = ACHAN2ADEV(chan);	/* device for channel. */
	int sam = 0;
	int nsamples;
	int j;

	if (save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid channel %d for sending Morse Code.\n", chan);
	  return;
	}

	nsamples = (int) ((ms * (float)save_audio_config_p->adev[a].samples_per_sec / 1000.) + 0.5);

	for (j=0; j<nsamples; j++)  {

	  gen_tone_put_sample (chan, a, sam);

        };

#endif

} /* end morse_quiet_ms */


/*-------------------------------------------------------------------
 *
 * Name:        morse_lookup
 *
 * Purpose:    	Given a character, find index in table above.
 *
 * Inputs:	ch
 *
 * Returns:	Index into table above or -1 if not found.
 *		Notice that space is not in the table.
 *		Any unusual character, that is not in the table, 
 *		ends up being treated like space.
 *
 *--------------------------------------------------------------------*/

static int morse_lookup (int ch)
{
	int i;

	if (islower(ch)) {
	  ch = toupper(ch);
	}

	for (i=0; i<NUM_MORSE; i++) {
	  if (ch == morse[i].ch) {
	    return (i);
	  }
	}
	return (-1);
}


/*-------------------------------------------------------------------
 *
 * Name:        morse_units_ch
 *
 * Purpose:    	Find number of time units for a character.
 *
 * Inputs:	ch
 *
 * Returns:	1 for E (.)
 *		3 for T (-)
 *		3 for I.= (..)
 *		etc.
 *	
 *		The one unexpected result is 1 for space.  Why not 7?
 *		When a space appears between two other characters,
 *		we already have 3 before and after so only 1 more is needed.
 *
 *--------------------------------------------------------------------*/

static int morse_units_ch (int ch)
{
	int i;
	int len;
	int k;
	int units;

	i = morse_lookup (ch);
	if (i < 0) {
	  return (1);	/* space or any invalid character */
	}

	
	len = strlen(morse[i].enc);
	units = len - 1;

	for (k = 0; k < len; k++) {
	  switch (morse[i].enc[k]) {
	    case '.':  units++; break;
	    case '-':  units += 3; break;
	    default:  dw_printf ("ERROR: morse_units_ch: should not be here.\n"); break;
	  }
	}
	
	return (units); 
}


/*-------------------------------------------------------------------
 *
 * Name:        morse_units_str
 *
 * Purpose:    	Find number of time units for a string of characters.
 *
 * Inputs:	str
 *
 * Returns:	1 for E 	
 *		5 for EE	(1 + 3 + 1)
 *		9 for E E	(1 + 7 + 1)
 *		etc.
 *
 *--------------------------------------------------------------------*/

static int morse_units_str (char *str)
{
	//int i;
	int len;
	int k;
	int units;

	len = strlen(str);
	units = (len - 1) * 3;

	for (k = 0; k < len; k++) {
	  units += morse_units_ch(str[k]);
	}
	
	return (units); 
}


#if MTEST1

int main (int argc, char *argv[]) {

	dw_printf ("CQ DX\n");
	morse_send (0, "CQ DX", 10, 10, 10);
	dw_printf ("\n\n");

	dw_printf ("wb2osz/9\n");
	morse_send (0, "wb2osz/9", 10, 10, 10);
	dw_printf ("\n\n");

} 

#endif


/* end morse.c */




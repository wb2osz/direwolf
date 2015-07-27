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
 * Module:      morse.c
 *
 * Purpose:   	Generate audio for morse code.
 *		
 * Description:	
 *
 * Reference:	
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#if __WIN32__
#include <windows.h>
#else
#include <sys/termios.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#endif

#include "direwolf.h"
#include "textcolor.h"
#include "audio.h"
#include "ptt.h"


#define WPM 10
#define TIME_UNITS_TO_MS(tu,wpm)  (((tu)*1200)/(wpm))


// TODO : should be in .h file.

/*
 * Delay from PTT on to start of first character.
 * Currently the only anticipated use for this is 
 * APRStt responses.  In this case, we want an adequate
 * delay for someone to press the # button, release 
 * the PTT button, and start listening for a response.
 */
#define MORSE_TXDELAY_MS  1500

/*
 * Delay from end of last character to PTT off.
 * Avoid chopping off the last element.
 */
#define MORSE_TXTAIL_MS   200


static const struct morse_s {
	char ch;
	char enc[7];
} morse[] = {
	{ 'A', ".-" },
	{ 'B', "-..." },
	{ 'C', "-.-." },
	{ 'D', "-.." },
	{ 'E', "." },
	{ 'F', "..-." },
	{ 'G', "--." },
	{ 'H', "...." },
	{ 'I', "." },
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
	{ '/', "-..-." }
};

#define NUM_MORSE (sizeof(morse) / sizeof(struct morse_s))

static void morse_tone (int tu);
static void morse_quiet (int tu);
static int morse_lookup (int ch);
static int morse_units_ch (int ch);
static int morse_units_str (char *str);



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
	for (p = str; *p != '\0'; p++) {
	  int i;

	  i = morse_lookup (*p);
	  if (i >= 0) {
	    const char *e;

	    for (e = morse[i].enc; *e != '\0'; e++) {
	      if (*e == '.') {
	        morse_tone (1);
	        time_units++;
	      }
	      else {
	        morse_tone (3);
	        time_units += 3;
	      }
	      if (e[1] != '\0') {
	        morse_quiet (1);
	        time_units++;
	      }
	    }
	  }
	  else {
	    morse_quiet (1);
	    time_units++;
	  }
	  if (p[1] != '\0') {
	    morse_quiet (3);
	    time_units += 3;
	  }
	}

	if (time_units != morse_units_str(str)) {
	  dw_printf ("morse: Internal error.  Inconsistent length, %d vs. %d calculated.\n", 
		time_units, morse_units_str(str));
	}


	return (txdelay +
		TIME_UNITS_TO_MS(time_units, wpm) +
		txtail);

}  /* end morse_send */



/*-------------------------------------------------------------------
 *
 * Name:        morse_tone
 *
 * Purpose:    	Generate tone for specified number of time units.
 *
 * Inputs:	tu	- Number of time units.
 *
 *--------------------------------------------------------------------*/

static void morse_tone (int tu) {
	int num_cycles;
	int n;

	for (n=0; n<tu; n++) {
	  dw_printf ("#");
	}

	

} /* end morse_tone */


/*-------------------------------------------------------------------
 *
 * Name:        morse_quiet
 *
 * Purpose:    	Generate silence for specified number of time units.
 *
 * Inputs:	tu	- Number of time units.
 *
 *--------------------------------------------------------------------*/

static void morse_quiet (int tu) {
	int n;

	for (n=0; n<tu; n++) {
	  dw_printf (".");
	}

} /* end morse_quiet */



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
	int i;
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


int main (int argc, char *argv[]) {

	dw_printf ("CQ DX\n");
	morse_send (0, "CQ DX", 10, 10, 10);
	dw_printf ("\n\n");

	dw_printf ("wb2osz/9\n");
	morse_send (0, "wb2osz/9", 10, 10, 10);
	dw_printf ("\n\n");

} 


/* end morse.c */




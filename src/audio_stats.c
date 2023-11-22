
// 
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2015  John Langner, WB2OSZ
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
 * Module:      audio_stats.c
 *
 * Purpose:   	Print statistics for audio input stream.
 *
 * 		A common complaint is that there is no indication of 
 *		audio input level until a packet is received correctly.
 *		That's true for the Windows version but the Linux version
 *		prints something like this each 100 seconds:
 *
 *		ADEVICE0: Sample rate approx. 44.1 k, 0 errors, receive audio level CH0 73
 *
 *		Some complain about the clutter but it has been a useful
 *		troubleshooting tool.  In the earlier RPi days, the sample
 *		rate was quite low due to a device driver issue.  
 *		Using a USB hub on the RPi also caused audio problems.
 *		One adapter, that I tried, produces samples at the 
 *		right rate but all the samples are 0.
 *
 *		Here we pull the code out of the Linux version of audio.c
 *		so we have a common function for all the platforms.
 *
 *		We also add a command line option to adjust the time
 *		between reports or turn them off entirely.
 *		
 * Revisions: 	This is new in version 1.3.
 *
 *---------------------------------------------------------------*/


#include "direwolf.h"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <time.h>


#include "audio_stats.h"
#include "textcolor.h"
#include "demod.h"		/* for alevel_t & demod_get_audio_level() */



/*------------------------------------------------------------------
 *
 * Name:        audio_stats 
 *
 * Purpose:     Add sample count from one buffer to the statistics.
 *		Print if specified amount of time has passed.
 *
 * Inputs:	adev	- Audio device number:  0, 1, ..., MAX_ADEVS-1
 *
 		nchan	- Number of channels for this device, 1 or 2.
 *
 *		nsamp	- How many audio samples were read.
 *
 *		interval - How many seconds between reports.
 *				0 to turn off.
 *
 * Returns:     none
 *
 * Description:	...
 *
 *----------------------------------------------------------------*/


void audio_stats (int adev, int nchan, int nsamp, int interval)
{

	/* Gather numbers for read from audio device. */


	static time_t last_time[MAX_ADEVS] = { 0, 0, 0 };
	time_t this_time[MAX_ADEVS];
	static int sample_count[MAX_ADEVS];
	static int error_count[MAX_ADEVS];
	static int suppress_first[MAX_ADEVS];


	if (interval <= 0) {
	  return;
	}

	assert (adev >= 0 && adev < MAX_ADEVS);

/*
 * Print information about the sample rate as a troubleshooting aid.
 * I've never seen an issue with Windows or x86 Linux but the Raspberry Pi
 * has a very troublesome audio input system where many samples got lost.
 *
 * While we are at it we can also print the current audio level(s) providing 
 * more clues if nothing is being decoded.
 */

	if (last_time[adev] == 0) {
	  last_time[adev] = time(NULL);
	  sample_count[adev] = 0;
	  error_count[adev] = 0;
	  suppress_first[adev] = 1;
	 	/* suppressing the first one could mean a rather */
		/* long wait for the first message.  We make the */
		/* first collection interval 3 seconds. */
	  last_time[adev] -= (interval - 3);
	}
	else {
	  if (nsamp > 0) {
	     sample_count[adev] += nsamp;
	  }
	  else {
	     error_count[adev]++;
	  }
	  this_time[adev] = time(NULL);
	  if (this_time[adev] >= last_time[adev] + interval) {

	    if (suppress_first[adev]) {

		/* The issue we had is that the first time the rate */
		/* would be off considerably because we didn't start */
		/* on a second boundary.  So we will suppress printing */
		/* of the first one.  */

	      suppress_first[adev] = 0;
	    }
	    else {
	      float ave_rate = (sample_count[adev] / 1000.0) / interval;

	      text_color_set(DW_COLOR_DEBUG);

	      if (nchan > 1) {
	        int ch0 = ADEVFIRSTCHAN(adev);
	        alevel_t alevel0 = demod_get_audio_level(ch0,0);
	        int ch1 = ADEVFIRSTCHAN(adev) + 1;
	        alevel_t alevel1 = demod_get_audio_level(ch1,0);

	        dw_printf ("\nADEVICE%d: Sample rate approx. %.1f k, %d errors, receive audio levels CH%d %d, CH%d %d\n\n", 
			adev, ave_rate, error_count[adev], ch0, alevel0.rec, ch1, alevel1.rec);
	      }
	      else {
	        int ch0 = ADEVFIRSTCHAN(adev);
	        alevel_t alevel0 = demod_get_audio_level(ch0,0);

	        dw_printf ("\nADEVICE%d: Sample rate approx. %.1f k, %d errors, receive audio level CH%d %d\n\n", 
			adev, ave_rate, error_count[adev], ch0, alevel0.rec);
	      }
	    }
	    last_time[adev] = this_time[adev];
	    sample_count[adev] = 0;
	    error_count[adev] = 0;
	  }      
	}

}   /* end audio_stats.c */


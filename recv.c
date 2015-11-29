
// 
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015  John Langner, WB2OSZ
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
 * Module:      recv.c
 *
 * Purpose:   	Process audio input for receiving.
 *
 *		This is for all platforms.
 *
 *
 * Description:	In earlier versions, we supported a single audio device
 *		and the main program looped around processing the 
 *		audio samples.  The structure looked like this:
 *
 *		main in direwolf.c:
 *
 *			audio_init()
 *			various other *_init()
 *
 *			loop forever:
 *				s = demod_get_sample.
 *				multi_modem_process_sample(s)
 *				
 *
 *		When a packet is succesfully decoded, somebody calls
 *		app_process_rec_frame, also in direwolf.c
 *
 *
 *		Starting in version 1.2, we support multiple audio 
 *		devices at the same time.  We now have a separate
 *		thread for each audio device.   Decoded frames are
 *		sent to a single queue for serial processing.
 *
 *		The new flow looks like this:
 *
 *		main in direwolf.c:
 *
 *			audio_init()
 *			various other *_init()
 *			recv_init()
 *			recv_process()  -- does not return
 *
 *			
 *		recv_init()		This starts up a separate thread
 *					for each audio device.
 *					Each thread reads audio samples and
 *					passes them to multi_modem_process_sample.
 *
 *					The difference is that app_process_rec_frame
 *					is no longer called directly.  Instead
 *					the frame is appended to a queue with dlq_append.
 *
 *					Received frames can now be processed one at 
 *					a time and we don't need to worry about later
 *					processing being reentrant.
 *				
 *		recv_process()  	This simply waits for something to show up
 *					in the dlq queue and calls app_process_rec_frame
 *					for each.
 *
 *---------------------------------------------------------------*/


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
//#include <sys/stat.h>
//#include <sys/ioctl.h>
//#include <fcntl.h>
#include <assert.h>

#ifdef __FreeBSD__
#include <errno.h>
#endif

#include "direwolf.h"
#include "audio.h"
#include "demod.h"
#include "multi_modem.h"
#include "textcolor.h"
#include "dlq.h"
#include "recv.h"
#include "dtmf.h"
#include "aprs_tt.h"


#if __WIN32__
static unsigned __stdcall recv_adev_thread (void *arg);
#else
static void * recv_adev_thread (void *arg);
#endif


static struct audio_s *save_pa;		/* Keep pointer to audio configuration */
					/* for later use. */

/*------------------------------------------------------------------
 *
 * Name:        recv_init
 *
 * Purpose:     Start up a thread for each audio device.
 *
 *
 * Inputs:      pa		- Address of structure of type audio_s.
 *				
 *              
 * Returns:     None.
 *
 * Errors:	Exit if error.
 *		No point in going on if we can't get audio.
 *		
 *----------------------------------------------------------------*/



void recv_init (struct audio_s *pa)
{
#if __WIN32__
	HANDLE xmit_th[MAX_ADEVS];
#else
	pthread_t xmit_tid[MAX_ADEVS];
#endif
	int a;

	save_pa = pa;

	for (a=0; a<MAX_ADEVS; a++) {

	  if (pa->adev[a].defined) {

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("recv_init: start up thread, a=%d\n", a);
#endif

#if __WIN32__
	    xmit_th[a] = (HANDLE)_beginthreadex (NULL, 0, recv_adev_thread, (void*)(long)a, 0, NULL);
	    if (xmit_th[a] == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("FATAL: Could not create audio receive thread for device %d.\n", a);
	      exit(1);
	    }
#else
	    int e;
	    e = pthread_create (&xmit_tid[a], NULL, recv_adev_thread, (void *)(long)a);

	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("FATAL: Could not create audio receive thread for device %d.\n", a);
	      exit(1);
	    }
#endif
	  }

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("recv_init: all done\n");
#endif
	}


} /* end recv_init */




/* Try using "hot" attribute for all functions */
/* which are used for each audio sample. */
/* Compiler & linker might gather */
/* them together to improve memory cache performance. */
/* Or maybe it won't make any difference. */

__attribute__((hot))
#if __WIN32__
static unsigned __stdcall recv_adev_thread (void *arg)
#else
static void * recv_adev_thread (void *arg)
#endif
{
	int a = (int)(long)arg;	// audio device number.
	int eof;
	
	/* This audio device can have one (mono) or two (stereo) channels. */
	/* Find number of the first channel. */

	int first_chan =  ADEVFIRSTCHAN(a); 
	int num_chan = save_pa->adev[a].num_channels;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("recv_adev_thread is now running for a=%d\n", a);
#endif
/*
 * Get sound samples and decode them.
 */
	eof = 0;
	while ( ! eof) 
	{

	  int audio_sample;
	  int c;
	  char tt;

	  for (c=0; c<num_chan; c++)
	  {
	    audio_sample = demod_get_sample (a);
	  
 	    if (audio_sample >= 256 * 256) 
	      eof = 1;

	    multi_modem_process_sample(first_chan + c, audio_sample);


	    /* Originally, the DTMF decoder was always active. */
	    /* It took very little CPU time and the thinking was that an */
	    /* attached application might be interested in this even when */
	    /* the APRStt gateway was not being used.  */

	    /* Unfortunately it resulted in too many false detections of */
	    /* touch tones when hearing other types of digital communications */
	    /* on HF.  Starting in version 1.0, the DTMF decoder is active */
	    /* only when the APRStt gateway is configured. */

	    /* The test below allows us to listen to only a single channel for */
	    /* for touch tone sequences.  The DTMF decoder and the accumulation */
	    /* of digits into a sequence maintain separate data for each channel. */
	    /* We should be able to accept touch tone sequences concurrently on */
	    /* all channels.  The only issue is when a complete sequence is */
	    /* sent to aprs_tt_sequence which doesn't have separate data for each */
	    /* channel.  This shouldn't be a problem unless we have multiple */
	    /* sequences arriving at the same instant. */

	    if (save_pa->achan[first_chan + c].dtmf_decode != DTMF_DECODE_OFF) {
	      tt = dtmf_sample (first_chan + c, audio_sample/16384.);
	      if (tt != ' ') {
	        aprs_tt_button (first_chan + c, tt);
	      }
	    }
	  }

		/* When a complete frame is accumulated, */
		/* dlq_append, is called. */

		/* recv_process, below, drains the queue. */

	}

// What should we do now?
// Seimply terminate the application?  
// Try to re-init the audio device a couple times before giving up?

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Terminating after audio input failure.\n");
	exit (1);
}





void recv_process (void) 
{

	int ok;
	dlq_type_t type;
	int chan;
	int subchan;
	int slice;
	packet_t pp;
	alevel_t alevel;
	retry_t retries;
	char spectrum[MAX_SUBCHANS*MAX_SLICERS+1];

	while (1) {

	  dlq_wait_while_empty ();
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("recv_process: woke up\n");
#endif 

	  ok = dlq_remove (&type, &chan, &subchan, &slice, &pp, &alevel, &retries, spectrum, sizeof(spectrum));

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("recv_process: dlq_remove() returned ok=%d, type=%d, chan=%d, pp=%p\n", 
				ok, (int)type, chan, pp);
#endif
	  if (ok) {
		app_process_rec_packet (chan, subchan, slice, pp, alevel, retries, spectrum);
	  }
#if DEBUG
	  else {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("recv_process: spurious wakeup. (Temp debugging message - not a problem if only occasional.)\n");
	  }
#endif
	}

} /* end recv_process */



/* end recv.c */


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2015  John Langner, WB2OSZ
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
 * Module:      redecode.c
 *
 * Purpose:   	Retry decoding frames that have a bad FCS.
 *		
 * Description:	
 *
 *
 * Usage:	(1) The main application calls redecode_init.
 *
 *			This will initialize the retry decoding queue
 *			and create a thread to work on contents of the queue.
 *
 *		(2) The application queues up frames by calling rdq_append.
 *
 *
 *		(3) redecode_thread removes raw frames from the queue and 
 *			tries to recover from errors.
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <time.h>

#if __WIN32__
#include <windows.h>
#endif

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "rdq.h"
#include "redecode.h"
#include "hdlc_send.h"
#include "hdlc_rec2.h"
#include "ptt.h"


/* Audio configuration for the fix_bits / passall optiions. */

static struct audio_s          *save_audio_config_p;



#if __WIN32__
static unsigned redecode_thread (void *arg);
#else
static void * redecode_thread (void *arg);
#endif


/*-------------------------------------------------------------------
 *
 * Name:        redecode_init
 *
 * Purpose:     Initialize the process to try fixing bits in frames with bad FCS.
 *
 * Inputs:	none.
 *
 * Outputs:	none.
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *		Start up redecode_thread to actually process the
 *		raw frames from the queue.
 *
 *--------------------------------------------------------------------*/


void redecode_init (struct audio_s *p_audio_config)
{

#if 0

#if __WIN32__
	HANDLE redecode_th;
#else
	pthread_t redecode_tid;
	int e;
#endif


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("redecode_init ( ... )\n");
#endif

	save_audio_config_p = p_audio_config;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("redecode_init: about to call rdq_init \n");
#endif
	rdq_init ();

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("redecode_init: about to create thread \n");
#endif


#if __WIN32__
	redecode_th = _beginthreadex (NULL, 0, redecode_thread, NULL, 0, NULL);
	if (redecode_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create redecode thread\n");
	  return;
	}
#else

//TODO: Give thread lower priority.

	e = pthread_create (&redecode_tid, NULL, redecode_thread, (void *)0);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create redecode thread");
	  return;
	}
#endif



#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("redecode_init: finished \n");
#endif
#endif

} /* end redecode_init */





/*-------------------------------------------------------------------
 *
 * Name:        redecode_thread
 *
 * Purpose:     Try to decode frames with a bad FCS.
 *
 * Inputs:	None.
 *
 * Outputs:	
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *
 *--------------------------------------------------------------------*/

#if 0

#if __WIN32__
static unsigned redecode_thread (void *arg)
#else
static void * redecode_thread (void *arg)
#endif
{
#if __WIN32__
	HANDLE tid = GetCurrentThread();
	//int tp;

	//tp = GetThreadPriority (tid);
	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("Starting redecode thread priority=%d\n", tp);
	SetThreadPriority (tid, THREAD_PRIORITY_LOWEST);
	//tp = GetThreadPriority (tid);
	//dw_printf ("New redecode thread priority=%d\n", tp);
#endif

	while (1) {
	  rrbb_t block;

	  rdq_wait_while_empty ();
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("redecode_thread: woke up\n");
#endif
	  
	  block = rdq_remove ();

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("redecode_thread: rdq_remove() returned %p\n", block);
#endif

/* Don't expect null ever but be safe. */

	  if (block != NULL) {

	    int chan = rrbb_get_chan(block);
	    int subchan = rrbb_get_subchan(block);
	    int blen = rrbb_get_len(block);
	    alevel_t alevel = rrbb_get_audio_level(block);
	    //retry_t fix_bits = save_audio_config_p->achan[chan].fix_bits;
	    //int passall = save_audio_config_p->achan[chan].passall;

	    int ok;

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("redecode_thread: begin processing %p, from channel %d, blen=%d\n", block, chan, blen);
#endif

	    ok = hdlc_rec2_try_to_fix_later (block, chan, subchan, alevel);

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("redecode_thread: finished processing %p\n", block);
#endif
	    rrbb_delete (block);
	  }

	}

	return 0;

} /* end redecode_thread */

#endif



/* end redecode.c */




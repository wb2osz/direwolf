
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
 * Module:      dlq.c
 *
 * Purpose:   	Received frame queue.
 *
 * Description: In previous versions, the main thread read from the
 *		audio device and performed the receive demodulation/decoding.
 *		In version 1.2 we now have a seprate receive thread
 *		for each audio device.  This queue is used to collect
 *		received frames from all channels and process them
 *		serially.
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "dlq.h"
#include "dedupe.h"


/* The queue is a linked list of these. */

struct dlq_item_s {

	struct dlq_item_s *nextp;	/* Next item in queue. */

	dlq_type_t type;		/* Type of item. */
					/* Only received frames at this time. */

	int chan;			/* Radio channel of origin. */

	int subchan;			/* Winning "subchannel" when using multiple */
					/* decoders on one channel.  */
					/* Special case, -1 means DTMF decoder. */
					/* Maybe we should have a different type in this case? */

	int slice;			/* Winning slicer. */

	packet_t pp;			/* Pointer to frame structure. */

	alevel_t alevel;		/* Audio level. */

	retry_t retries;		/* Effort expended to get a valid CRC. */

	char spectrum[MAX_SUBCHANS*MAX_SLICERS+1];	/* "Spectrum" display for multi-decoders. */
};


static struct dlq_item_s *queue_head = NULL;	/* Head of linked list for queue. */

#if __WIN32__

// TODO1.2: use dw_mutex_t

static CRITICAL_SECTION dlq_cs;			/* Critical section for updating queues. */

static HANDLE wake_up_event;			/* Notify received packet processing thread when queue not empty. */

#else

static pthread_mutex_t dlq_mutex;		/* Critical section for updating queues. */

static pthread_cond_t wake_up_cond;		/* Notify received packet processing thread when queue not empty. */

static pthread_mutex_t wake_up_mutex;		/* Required by cond_wait. */

static int recv_thread_is_waiting = 0;

#endif

static int dlq_is_empty (void);

static int was_init = 0;			/* was initialization performed? */


/*-------------------------------------------------------------------
 *
 * Name:        dlq_init
 *
 * Purpose:     Initialize the queue.
 *
 * Inputs:	None.
 *
 * Outputs:	
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *--------------------------------------------------------------------*/


void dlq_init (void)
{
	int c, p;
	int err;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_init ( )\n");
#endif

	queue_head = NULL;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_init: pthread_mutex_init...\n");
#endif

#if __WIN32__
	InitializeCriticalSection (&dlq_cs);
#else
	err = pthread_mutex_init (&wake_up_mutex, NULL);
	err = pthread_mutex_init (&dlq_mutex, NULL);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_init: pthread_mutex_init err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif



#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_init: pthread_cond_init...\n");
#endif

#if __WIN32__

	wake_up_event = CreateEvent (NULL, 0, 0, NULL);

	if (wake_up_event == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_init: pthread_cond_init: can't create receive wake up event");
	  exit (1);
	}

#else
	err = pthread_cond_init (&wake_up_cond, NULL);


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_init: pthread_cond_init returns %d\n", err);
#endif


	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_init: pthread_cond_init err=%d", err);
	  perror ("");
	  exit (1);
	}

	recv_thread_is_waiting = 0;
#endif

	was_init = 1;

} /* end dlq_init */


/*-------------------------------------------------------------------
 *
 * Name:        dlq_append
 *
 * Purpose:     Add a packet to the end of the specified receive queue.
 *
 * Inputs:	type	- One of the following:
 *
 *				DLQ_REC_FRAME - Frame received from radio.
 *
 *		chan	- Channel, 0 is first.
 *
 *		subchan	- Which modem caught it.  
 *			  Special case -1 for APRStt gateway.
 *
 *		slice	- Which slice we picked.
 *
 *		pp	- Address of packet object.
 *				Caller should NOT make any references to
 *				it after this point because it could
 *				be deleted at any time.
 *
 *		alevel	- Audio level, range of 0 - 100.
 *				(Special case, use negative to skip
 *				 display of audio level line.
 *				 Use -2 to indicate DTMF message.)
 *
 *		retries	- Level of bit correction used.
 *
 *		spectrum - Display of how well multiple decoders did.
 *
 *
 * Outputs:	Information is appended to queue.
 *
 * Description:	Add item to end of linked list.
 *		Signal the receive processing thread if the queue was formerly empty.
 *
 * IMPORTANT!	Don't make an further references to the packet object after
 *		giving it to dlq_append.
 *
 *--------------------------------------------------------------------*/

void dlq_append (dlq_type_t type, int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, char *spectrum)
{

	struct dlq_item_s *pnew;
	struct dlq_item_s *plast;
	int err;
	int queue_length = 0;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_append (type=%d, chan=%d, pp=%p, ...)\n", type, chan, pp);
#endif

	if ( ! was_init) {
	  dlq_init ();
	}

	assert (chan >= 0 && chan < MAX_CHANS);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR:  dlq_append NULL packet pointer. Please report this!\n");
	  return;
	}

#if AX25MEMDEBUG

	if (ax25memdebug_get()) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_append (type=%d, chan=%d.%d, seq=%d, ...)\n", type, chan, subchan, ax25memdebug_seq(pp));
	}
#endif

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);

	pnew->nextp = NULL;
	pnew->type = type;
	pnew->chan = chan;
	pnew->slice = slice;
	pnew->subchan = subchan;
	pnew->pp = pp;
	pnew->alevel = alevel;
	pnew->retries = retries;
	if (spectrum == NULL) 
	  strlcpy(pnew->spectrum, "", sizeof(pnew->spectrum));
	else
	  strlcpy(pnew->spectrum, spectrum, sizeof(pnew->spectrum));

#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_append: enter critical section\n");
#endif
#if __WIN32__
	EnterCriticalSection (&dlq_cs);
#else
	err = pthread_mutex_lock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_append: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	if (queue_head == NULL) {
	  queue_head = pnew;
	  queue_length = 1;
	}
	else {
	  queue_length = 2;	/* head + new one */
	  plast = queue_head;
	  while (plast->nextp != NULL) {
	    plast = plast->nextp;
	    queue_length++;
	  }
	  plast->nextp = pnew;
	}


#if __WIN32__ 
	LeaveCriticalSection (&dlq_cs);
#else
	err = pthread_mutex_unlock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_append: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_append: left critical section\n");
	dw_printf ("dlq_append (): about to wake up recv processing thread.\n");
#endif


/*
 * Bug:  June 2015, version 1.2
 *
 * It has long been known that we will eventually block trying to write to a 
 * pseudo terminal if nothing is reading from the other end.  There is even 
 * a warning at start up time:
 *
 *	Virtual KISS TNC is available on /dev/pts/2
 *	WARNING - Dire Wolf will hang eventually if nothing is reading from it.
 *	Created symlink /tmp/kisstnc -> /dev/pts/2
 *
 * In earlier versions, where the audio input and demodulation was in the main 
 * thread, that would stop and it was pretty obvious something was wrong.
 * In version 1.2, the audio in / demodulating was moved to a device specific 
 * thread.  Packet objects are appended to this queue.
 *
 * The main thread should wake up and process them which includes printing and
 * forwarding to clients over multiple protocols and transport methods.
 * Just before the 1.2 release someone reported a memory leak which only showed
 * up after about 20 hours.  It happened to be on a Cubie Board 2, which shouldn't
 * make a difference unless there was some operating system difference.
 * (cubieez 2.0 is based on Debian wheezy, just like Raspian.)
 *
 * The debug output revealed:
 *
 *	It was using AX.25 for Linux (not APRS).
 *	The pseudo terminal KISS interface was being used.
 *	Transmitting was continuing fine.  (So something must be writing to the other end.)
 *	Frames were being received and appended to this queue.
 *	They were not coming out of the queue.
 *
 * My theory is that writing to the the pseudo terminal is blocking so the 
 * main thread is stopped.   It's not taking anything from this queue and we detect
 * it as a memory leak.  
 *
 * Add a new check here and complain if the queue is growing too large.
 * That will get us a step closer to the root cause.  
 * This has been documented in the User Guide and the CHANGES.txt file which is
 * a minimal version of Release Notes.
 * The proper fix will be somehow avoiding or detecting the pseudo terminal filling up
 * and blocking on a write.
 */

	if (queue_length > 10) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Received frame queue is out of control. Length=%d.\n", queue_length);
	  dw_printf ("Reader thread is probably frozen.\n");
	  dw_printf ("This can be caused by using a pseudo terminal (direwolf -p) where another\n");
	  dw_printf ("application is not reading the frames from the other side.\n");
	}



#if __WIN32__
	SetEvent (wake_up_event);
#else
	if (recv_thread_is_waiting) {

	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_append: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_cond_signal (&wake_up_cond);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_append: pthread_cond_signal err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_append: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }
	}
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        dlq_wait_while_empty
 *
 * Purpose:     Sleep while the received data queue is empty rather than
 *		polling periodically.
 *
 * Inputs:	None.
 *		
 *--------------------------------------------------------------------*/


void dlq_wait_while_empty (void)
{
	int err;


#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_wait_while_empty () \n");
#endif

	if ( ! was_init) {
	  dlq_init ();
	}


	if (queue_head == NULL) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_wait_while_empty (): prepare to SLEEP - about to call cond wait\n");
#endif


#if __WIN32__
	  WaitForSingleObject (wake_up_event, INFINITE);

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_wait_while_empty (): returned from wait\n");
#endif

#else
	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_wait_while_empty: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  recv_thread_is_waiting = 1;
	  err = pthread_cond_wait (&wake_up_cond, &wake_up_mutex);
	  recv_thread_is_waiting = 0;

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_wait_while_empty (): WOKE UP - returned from cond wait, err = %d\n", err);
#endif

	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_wait_while_empty: pthread_cond_wait err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_wait_while_empty: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

#endif
	}


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_wait_while_empty () returns\n");
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        dlq_remove
 *
 * Purpose:     Remove an item from the head of the queue.
 *
 * Inputs:	None.
 *
 * Outputs:	type		- type of queue entry.
 *
 *		chan		- channel of received frame.
 *		subchan		- which demodulator caught it.
 *		slice		- which slicer caught it.
 *
 *		pp		- pointer to packet object when type is DLQ_REC_FRAME.
 *				   Caller should destroy it with ax25_delete when finished with it.
 *
 * Returns:	1 for success.
 *		0 if queue is empty.  
 *
 *--------------------------------------------------------------------*/


int dlq_remove (dlq_type_t *type, int *chan, int *subchan, int *slice, packet_t *pp, alevel_t *alevel, retry_t *retries, char *spectrum, size_t spectrumsize)
{

	struct dlq_item_s *phead;
	int result;
	int err;

#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_remove() enter critical section\n");
#endif

	if ( ! was_init) {
	  dlq_init ();
	}

#if __WIN32__
	EnterCriticalSection (&dlq_cs);
#else
	err = pthread_mutex_lock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_remove: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	if (queue_head == NULL) {

	  *type = -1;
	  *chan = -1;
	  *subchan = -1;
	  *slice = -1;
	  *pp = NULL;

	  memset (alevel, 0xff, sizeof(*alevel));

	  *retries = -1;
	  strlcpy(spectrum, "", spectrumsize);
	  result = 0;
	}
	else {

	  phead = queue_head;
	  queue_head = queue_head->nextp;

	  *type = phead->type;
	  *chan = phead->chan;
	  *subchan = phead->subchan;
	  *slice = phead->slice;
	  *pp = phead->pp;
	  *alevel = phead->alevel;
	  *retries = phead->retries;
	  strlcpy (spectrum, phead->spectrum, spectrumsize);
	  result = 1;
	}
	 
#if __WIN32__
	LeaveCriticalSection (&dlq_cs);
#else
	err = pthread_mutex_unlock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_remove: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_remove()  returns type=%d, chan=%d\n", (int)(*type), *chan);
#endif

#if AX25MEMDEBUG

	if (ax25memdebug_get() && result) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_remove (type=%d, chan=%d.%d, seq=%d, ...)\n", *type, *chan, *subchan, ax25memdebug_seq(*pp));
	}
#endif
	if (result) {
	  free (phead);
	}

	return (result);
}


/*-------------------------------------------------------------------
 *
 * Name:        dlq_is_empty
 *
 * Purpose:     Test whether queue is empty.
 *
 * Inputs:	None 
 *
 * Returns:	True if nothing in the queue.	
 *
 *--------------------------------------------------------------------*/

#if 0
static int dlq_is_empty (void)
{
	if (queue_head == NULL) {
	  return (1);
	}
	return (0);

} /* end dlq_is_empty */
#endif

/* end dlq.c */

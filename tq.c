//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2012  John Langner, WB2OSZ
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
 * Module:      tq.c
 *
 * Purpose:   	Transmit queue - hold packets for transmission until the channel is clear.
 *		
 * Description:	Producers of packets to be transmitted call tq_append and then
 *		go merrily on their way, unconcerned about when the packet might
 *		actually get transmitted.
 *
 *		Another thread waits until the channel is clear and then removes
 *		packets from the queue and transmits them.
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
#include "tq.h"
#include "dedupe.h"



static int tq_num_channels;			/* Set once during intialization and */
						/* should not change after that. */

static packet_t queue_head[MAX_CHANS][TQ_NUM_PRIO];	/* Head of linked list for each queue. */

#if __WIN32__

static CRITICAL_SECTION tq_cs;			/* Critical section for updating queues. */

static HANDLE wake_up_event;			/* Notify transmit thread when queue not empty. */

#else

static pthread_mutex_t tq_mutex;		/* Critical section for updating queues. */

static pthread_cond_t wake_up_cond;			/* Notify transmit thread when queue not empty. */

static pthread_mutex_t wake_up_mutex;		/* Required by cond_wait. */

static int xmit_thread_is_waiting = 0;

#endif

static int tq_is_empty (void);


/*-------------------------------------------------------------------
 *
 * Name:        tq_init
 *
 * Purpose:     Initialize the transmit queue.
 *
 * Inputs:	nchan		- Number of communication channels.
 *
 * Outputs:	
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *		We have different timing rules for different types of
 *		packets so they are put into different queues.
 *
 *		High Priority -
 *
 *			Packets which are being digipeated go out first.
 *			Latest recommendations are to retransmit these
 *			immdediately (after no one else is heard, of course)
 *			rather than waiting random times to avoid collisions.
 *			The KPC-3 configuration option for this is "UIDWAIT OFF".
 *
 *		Low Priority - 
 *
 *			Other packets are sent after a random wait time
 *			(determined by PERSIST & SLOTTIME) to help avoid
 *			collisions.	
 *
 *		If more than one audio channel is being used, a separate
 *		pair of transmit queues is used for each channel.
 *	
 *--------------------------------------------------------------------*/


void tq_init (int nchan)
{
	int c, p;
	int err;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_init ( %d )\n", nchan);
#endif
	tq_num_channels = nchan;
	assert (tq_num_channels >= 1 && tq_num_channels <= MAX_CHANS);

	for (c=0; c<MAX_CHANS; c++) {
	  for (p=0; p<TQ_NUM_PRIO; p++) {
	    queue_head[c][p] = NULL;
	  }
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_init: pthread_mutex_init...\n");
#endif

#if __WIN32__
	InitializeCriticalSection (&tq_cs);
#else
	err = pthread_mutex_init (&wake_up_mutex, NULL);
	err = pthread_mutex_init (&tq_mutex, NULL);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_init: pthread_mutex_init err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif



#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_init: pthread_cond_init...\n");
#endif

#if __WIN32__

	wake_up_event = CreateEvent (NULL, 0, 0, NULL);

	if (wake_up_event == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_init: pthread_cond_init: can't create transmit wake up event");
	  exit (1);
	}

#else
	err = pthread_cond_init (&wake_up_cond, NULL);


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_init: pthread_cond_init returns %d\n", err);
#endif


	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_init: pthread_cond_init err=%d", err);
	  perror ("");
	  exit (1);
	}

	xmit_thread_is_waiting = 0;
#endif


} /* end tq_init */


/*-------------------------------------------------------------------
 *
 * Name:        tq_append
 *
 * Purpose:     Add a packet to the end of the specified transmit queue.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI or TQ_PRIO_1_LO.
 *
 *		pp	- Address of packet object.
 *				Caller should NOT make any references to
 *				it after this point because it could
 *				be deleted at any time.
 *
 * Outputs:	
 *
 * Description:	Add packet to end of linked list.
 *		Signal the transmit thread if the queue was formerly empty.
 *
 * IMPORTANT!	Don't make an further references to the packet object after
 *		giving it to tq_append.
 *
 *--------------------------------------------------------------------*/

void tq_append (int chan, int prio, packet_t pp)
{
//	int was_empty;
//	int c, p;
	packet_t plast;
	packet_t pnext;
	int err;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append (chan=%d, prio=%d, pp=%p)\n", chan, prio, pp);
#endif
	assert (tq_num_channels >= 1 && tq_num_channels <= MAX_CHANS);
	assert (prio >= 0 && prio < TQ_NUM_PRIO);

	if (chan < 0 || chan >= tq_num_channels) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Request to transmit on radio channel %d.\n", chan);
	  ax25_delete(pp);
	  return;
	}

/* Is transmit queue out of control? */

	if (tq_count(chan,prio) > 20) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Transmit packet queue is too long.  Discarding transmit request.\n");
	  dw_printf ("Perhaps the channel is so busy there is no opportunity to send.\n");
	  ax25_delete(pp);
	  return;
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append: enter critical section\n");
#endif
#if __WIN32__
	EnterCriticalSection (&tq_cs);
#else
	err = pthread_mutex_lock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_append: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

//	was_empty = 1;
//	for (c=0; c<tq_num_channels; c++) {
//	  for (p=0; p<TQ_NUM_PRIO; p++) {
//	    if (queue_head[c][p] != NULL)
//	       was_empty = 0;
//	  }
//	}

	if (queue_head[chan][prio] == NULL) {
	  queue_head[chan][prio] = pp;
	}
	else {
	  plast = queue_head[chan][prio];
	  while ((pnext = ax25_get_nextp(plast)) != NULL) {
	    plast = pnext;
	  }
	  ax25_set_nextp (plast, pp);
	}


#if __WIN32__ 
	LeaveCriticalSection (&tq_cs);
#else
	err = pthread_mutex_unlock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_append: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append: left critical section\n");
	dw_printf ("tq_append (): about to wake up xmit thread.\n");
#endif

#if __WIN32__
	SetEvent (wake_up_event);
#else
	if (xmit_thread_is_waiting) {

	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_append: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_cond_signal (&wake_up_cond);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_append: pthread_cond_signal err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_append: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }
	}
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        tq_wait_while_empty
 *
 * Purpose:     Sleep while the transmit queue is empty rather than
 *		polling periodically.
 *
 * Inputs:	None.
 *		
 *--------------------------------------------------------------------*/


void tq_wait_while_empty (void)
{
	int is_empty;
	//int c, p;
	int err;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty () : enter critical section\n");
#endif
	assert (tq_num_channels >= 1 && tq_num_channels <= MAX_CHANS);


#if __WIN32__
	EnterCriticalSection (&tq_cs);
#else
	err = pthread_mutex_lock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_wait_while_empty: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

#if DEBUG
	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("tq_wait_while_empty (): after pthread_mutex_lock\n");
#endif
	is_empty = tq_is_empty();

#if __WIN32__
	LeaveCriticalSection (&tq_cs);
#else
	err = pthread_mutex_unlock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_wait_while_empty: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty () : left critical section\n");
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty (): is_empty = %d\n", is_empty);
#endif

	if (is_empty) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (): SLEEP - about to call cond wait\n");
#endif


#if __WIN32__
	  WaitForSingleObject (wake_up_event, INFINITE);

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (): returned from wait\n");
#endif

#else
	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_wait_while_empty: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  xmit_thread_is_waiting = 1;
	  err = pthread_cond_wait (&wake_up_cond, &wake_up_mutex);
	  xmit_thread_is_waiting = 0;

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (): WOKE UP - returned from cond wait, err = %d\n", err);
#endif

	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_wait_while_empty: pthread_cond_wait err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_wait_while_empty: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

#endif
	}


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty () returns\n");
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        tq_remove
 *
 * Purpose:     Remove a packet from the head of the specified transmit queue.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI or TQ_PRIO_1_LO.
 *
 * Returns:	Pointer to packet object.
 *		Caller should destroy it with ax25_delete when finished with it.	
 *
 *--------------------------------------------------------------------*/

packet_t tq_remove (int chan, int prio)
{

	packet_t result_p;
	int err;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_remove(%d,%d) enter critical section\n", chan, prio);
#endif

#if __WIN32__
	EnterCriticalSection (&tq_cs);
#else
	err = pthread_mutex_lock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_remove: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	if (queue_head[chan][prio] == NULL) {

	  result_p = NULL;
	}
	else {

	  result_p = queue_head[chan][prio];
	  queue_head[chan][prio] = ax25_get_nextp(result_p);
	  ax25_set_nextp (result_p, NULL);
	}
	 
#if __WIN32__
	LeaveCriticalSection (&tq_cs);
#else
	err = pthread_mutex_unlock (&tq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("tq_remove: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_remove(%d,%d) leave critical section, returns %p\n", chan, prio, result_p);
#endif
	return (result_p);
}


/*-------------------------------------------------------------------
 *
 * Name:        tq_is_empty
 *
 * Purpose:     Test queue is empty.
 *
 * Inputs:	None - this applies to all channels and priorities.
 *
 * Returns:	True if nothing in the queue.	
 *
 *--------------------------------------------------------------------*/

static int tq_is_empty (void)
{
	int c, p;


	for (c=0; c<tq_num_channels; c++) {
	  for (p=0; p<TQ_NUM_PRIO; p++) {

	    assert (c >= 0 && c < MAX_CHANS);
	    assert (p >= 0 && p < TQ_NUM_PRIO);

	    if (queue_head[c][p] != NULL)
	       return (0);
	  }
	}
	return (1);

} /* end tq_is_empty */


/*-------------------------------------------------------------------
 *
 * Name:        tq_count
 *
 * Purpose:     Return count of the number of packets in the specified transmit queue.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI or TQ_PRIO_1_LO.
 *
 * Returns:	Number of items in specified queue.	
 *
 *--------------------------------------------------------------------*/

int tq_count (int chan, int prio)
{

	packet_t p;
	int n;


/* Don't bother with critical section. */
/* Only used for debugging a problem. */

	n = 0;
	p = queue_head[chan][prio];
	while (p != NULL) {
	  n++;
	  p = ax25_get_nextp(p);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_count(%d,%d) returns %d\n", chan, prio, n);
#endif

	return (n);

} /* end tq_count */

/* end tq.c */

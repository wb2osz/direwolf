//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2014, 2015  John Langner, WB2OSZ
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
 * Revisions:	1.2 - Enhance for multiple audio devices.
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




static packet_t queue_head[MAX_CHANS][TQ_NUM_PRIO];	/* Head of linked list for each queue. */


static dw_mutex_t tq_mutex;				/* Critical section for updating queues. */
							/* Just one for all queues. */

#if __WIN32__

static HANDLE wake_up_event[MAX_CHANS];			/* Notify transmit thread when queue not empty. */

#else

static pthread_cond_t wake_up_cond[MAX_CHANS];		/* Notify transmit thread when queue not empty. */

static pthread_mutex_t wake_up_mutex[MAX_CHANS];	/* Required by cond_wait. */

static int xmit_thread_is_waiting[MAX_CHANS];

#endif

static int tq_is_empty (int chan);


/*-------------------------------------------------------------------
 *
 * Name:        tq_init
 *
 * Purpose:     Initialize the transmit queue.
 *
 * Inputs:	audio_config_p	- Audio device configuration.
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
 *		Each audio channel has its own queue.
 *	
 *--------------------------------------------------------------------*/


static struct audio_s *save_audio_config_p;



void tq_init (struct audio_s *audio_config_p)
{
	int c, p;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_init ( %d )\n", nchan);
#endif

	save_audio_config_p = audio_config_p;

	for (c=0; c<MAX_CHANS; c++) {
	  for (p=0; p<TQ_NUM_PRIO; p++) {
	    queue_head[c][p] = NULL;
	  }
	}

/*
 * Mutex to coordinate access to the queue.
 */

	dw_mutex_init(&tq_mutex);

/*
 * Windows and Linux have different wake up methods.
 * Put a wrapper around this someday to hide the details.
 */

#if __WIN32__

	for (c = 0; c < MAX_CHANS; c++) {

	  if (audio_config_p->achan[c].valid) {

	    wake_up_event[c] = CreateEvent (NULL, 0, 0, NULL);

	    if (wake_up_event[c] == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("tq_init: CreateEvent: can't create transmit wake up event, c=%d", c);
	      exit (1);
	    }	
	  }
	}

#else
	int err;

	for (c = 0; c < MAX_CHANS; c++) {

	  xmit_thread_is_waiting[c] = 0;

	  if (audio_config_p->achan[c].valid) {
	    err = pthread_cond_init (&(wake_up_cond[c]), NULL);
	    if (err != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("tq_init: pthread_cond_init c=%d err=%d", c, err);
	      perror ("");
	      exit (1);
	    }

	    dw_mutex_init(&(wake_up_mutex[c]));
	  }
	}

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
 *		Note that we have a transmit thread each audio channel.
 *		Two channels can share one audio output device.
 *
 * IMPORTANT!	Don't make an further references to the packet object after
 *		giving it to tq_append.
 *
 *--------------------------------------------------------------------*/

void tq_append (int chan, int prio, packet_t pp)
{
	packet_t plast;
	packet_t pnext;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append (chan=%d, prio=%d, pp=%p)\n", chan, prio, pp);
#endif


	assert (prio >= 0 && prio < TQ_NUM_PRIO);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR:  tq_append NULL packet pointer. Please report this!\n");
	  return;
	}

#if AX25MEMDEBUG

	if (ax25memdebug_get()) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_append (chan=%d, prio=%d, seq=%d)\n", chan, prio, ax25memdebug_seq(pp));
	}
#endif

	if (chan < 0 || chan >= MAX_CHANS || ! save_audio_config_p->achan[chan].valid) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Request to transmit on invalid radio channel %d.\n", chan);
	  ax25_delete(pp);
	  return;
	}

/* 
 * Is transmit queue out of control? 
 *
 * There is no technical reason to limit the transmit packet queue length, it just seemed like a good 
 * warning that something wasn't right.
 * When this was written, I was mostly concerned about APRS where packets would only be sent 
 * occasionally and they can be discarded if they can't be sent out in a reasonable amount of time.
 *
 * If a large file is being sent, with TCP/IP, it is perfectly reasonable to have a large number 
 * of packets waiting for transmission.
 *
 * Ideally, the application should be able to throttle the transmissions so the queue doesn't get too long.
 * If using the KISS interface, there is no way to get this information from the TNC back to the client app.
 * The AGW network interface does have a command 'y' to query about the number of frames waiting for transmission.
 * This was implemented in version 1.2.
 *
 * I'd rather not take out the queue length check because it is a useful sanity check for something going wrong.
 * Maybe the check should be performed only for APRS packets.  
 * The check would allow an unlimited number of other types.
 *
 * Limit was 20.  Changed to 100 in version 1.2 as a workaround.
 *
 * Implementing the 6PACK protocol is probably the proper solution.
 */

	if (ax25_is_aprs(pp) && tq_count(chan,prio) > 100) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Transmit packet queue for channel %d is too long.  Discarding packet.\n", chan);
	  dw_printf ("Perhaps the channel is so busy there is no opportunity to send.\n");
	  ax25_delete(pp);
	  return;
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append: enter critical section\n");
#endif

	dw_mutex_lock (&tq_mutex);

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

	dw_mutex_unlock (&tq_mutex);


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append: left critical section\n");
	dw_printf ("tq_append (): about to wake up xmit thread.\n");
#endif

#if __WIN32__
	SetEvent (wake_up_event[chan]);
#else
	if (xmit_thread_is_waiting[chan]) {
	  int err;

	  dw_mutex_lock (&(wake_up_mutex[chan]));

	  err = pthread_cond_signal (&(wake_up_cond[chan]));
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_append: pthread_cond_signal err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  dw_mutex_unlock (&(wake_up_mutex[chan]));
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
 * Inputs:	chan	- Audio device number.  
 *
 * Description:	We have one transmit thread for each audio device.
 *		This handles 1 or 2 channels.
 *		
 *--------------------------------------------------------------------*/


void tq_wait_while_empty (int chan)
{
	int is_empty;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty (%d) : enter critical section\n", chan);
#endif
	assert (chan >= 0 && chan < MAX_CHANS);

	dw_mutex_lock (&tq_mutex);

#if DEBUG
	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("tq_wait_while_empty (%d): after pthread_mutex_lock\n", chan);
#endif
	is_empty = tq_is_empty(chan);

	dw_mutex_unlock (&tq_mutex);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty (%d) : left critical section\n", chan);
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty (%d): is_empty = %d\n", chan, is_empty);
#endif

	if (is_empty) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (%d): SLEEP - about to call cond wait\n", chan);
#endif


#if __WIN32__
	  WaitForSingleObject (wake_up_event[chan], INFINITE);

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (): returned from wait\n");
#endif

#else
	  dw_mutex_lock (&(wake_up_mutex[chan]));

	  xmit_thread_is_waiting[chan] = 1;
	  int err;
	  err = pthread_cond_wait (&(wake_up_cond[chan]), &(wake_up_mutex[chan]));
	  xmit_thread_is_waiting[chan] = 0;

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_wait_while_empty (%d): WOKE UP - returned from cond wait, err = %d\n", chan, err);
#endif

	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("tq_wait_while_empty (%d): pthread_cond_wait err=%d", chan, err);
	    perror ("");
	    exit (1);
	  }

	  dw_mutex_unlock (&(wake_up_mutex[chan]));
#endif
	}


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_wait_while_empty (%d) returns\n", chan);
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

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_remove(%d,%d) enter critical section\n", chan, prio);
#endif

	dw_mutex_lock (&tq_mutex);

	if (queue_head[chan][prio] == NULL) {

	  result_p = NULL;
	}
	else {

	  result_p = queue_head[chan][prio];
	  queue_head[chan][prio] = ax25_get_nextp(result_p);
	  ax25_set_nextp (result_p, NULL);
	}
	 
	dw_mutex_unlock (&tq_mutex);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_remove(%d,%d) leave critical section, returns %p\n", chan, prio, result_p);
#endif

#if AX25MEMDEBUG

	if (ax25memdebug_get() && result_p != NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_remove (chan=%d, prio=%d)  seq=%d\n", chan, prio, ax25memdebug_seq(result_p));
	}
#endif
	return (result_p);
}


/*-------------------------------------------------------------------
 *
 * Name:        tq_is_empty
 *
 * Purpose:     Test if queues for specified channel are empty.
 *		
 * Inputs:	chan		Channel 
 *
 * Returns:	True if nothing in the queue.	
 *
 *--------------------------------------------------------------------*/

static int tq_is_empty (int chan)
{
	int p;
	
	assert (chan >= 0 && chan < MAX_CHANS);


	for (p=0; p<TQ_NUM_PRIO; p++) {

	  assert (p >= 0 && p < TQ_NUM_PRIO);

	  if (queue_head[chan][p] != NULL)
	     return (0);
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

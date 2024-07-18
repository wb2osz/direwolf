//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2014, 2015, 2016, 2023  John Langner, WB2OSZ
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

#define TQ_C 1

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "tq.h"
#include "dedupe.h"
#include "igate.h"
#include "dtime_now.h"
#include "nettnc.h"


static packet_t queue_head[MAX_RADIO_CHANS][TQ_NUM_PRIO];	/* Head of linked list for each queue. */


static dw_mutex_t tq_mutex;				/* Critical section for updating queues. */
							/* Just one for all queues. */

#if __WIN32__

static HANDLE wake_up_event[MAX_RADIO_CHANS];			/* Notify transmit thread when queue not empty. */

#else

static pthread_cond_t wake_up_cond[MAX_RADIO_CHANS];		/* Notify transmit thread when queue not empty. */

static pthread_mutex_t wake_up_mutex[MAX_RADIO_CHANS];	/* Required by cond_wait. */

static int xmit_thread_is_waiting[MAX_RADIO_CHANS];

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
	dw_printf ("tq_init (  )\n");
#endif

	save_audio_config_p = audio_config_p;

	for (c=0; c<MAX_RADIO_CHANS; c++) {
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

	for (c = 0; c < MAX_RADIO_CHANS; c++) {

	  if (audio_config_p->chan_medium[c] == MEDIUM_RADIO) {

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

	for (c = 0; c < MAX_RADIO_CHANS; c++) {

	  xmit_thread_is_waiting[c] = 0;

	  if (audio_config_p->chan_medium[c] == MEDIUM_RADIO) {
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
 * Purpose:     Add an APRS packet to the end of the specified transmit queue.
 *
 * 		Connected mode is a little different.  Use lm_data_request instead.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *				New in 1.7:
 *				Channel can be assigned to IGate rather than a radio.
 *
 *				New in 1.8:
 *				Channel can be assigned to a network TNC.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI for digipeated or
 *				TQ_PRIO_1_LO for normal.
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
	unsigned char *pinfo;
	int info_len = ax25_get_info (pp, &pinfo);
	if (info_len > 10) info_len = 10;
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_append (chan=%d, prio=%d, pp=%p) \"%*s\"\n", chan, prio, pp, info_len, (char*)pinfo);
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

// New in 1.7 - A channel can be assigned to the IGate rather than a radio.
// New in 1.8: Assign a channel to external network TNC.
// Send somewhere else, rather than the transmit queue.

#ifndef DIGITEST		// avoid dtest link error

	if (save_audio_config_p->chan_medium[chan] == MEDIUM_IGATE ||
		save_audio_config_p->chan_medium[chan] == MEDIUM_NETTNC) {

	  char ts[100];		// optional time stamp.

	  if (strlen(save_audio_config_p->timestamp_format) > 0) {
	    char tstmp[100];
	    timestamp_user_format (tstmp, sizeof(tstmp), save_audio_config_p->timestamp_format);
	    strlcpy (ts, " ", sizeof(ts));	// space after channel.
	    strlcat (ts, tstmp, sizeof(ts));
	  }
	  else {
	    strlcpy (ts, "", sizeof(ts));
	  }

	  char stemp[256];	// Formated addresses.
	  ax25_format_addrs (pp, stemp);
	  unsigned char *pinfo;
	  int info_len = ax25_get_info (pp, &pinfo);
	  text_color_set(DW_COLOR_XMIT);

	  if (save_audio_config_p->chan_medium[chan] == MEDIUM_IGATE) {

	    dw_printf ("[%d>is%s] ", chan, ts);
	    dw_printf ("%s", stemp);			/* stations followed by : */
	    ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	    dw_printf ("\n");

	    igate_send_rec_packet (chan, pp);
	  }
	  else {	// network TNC
	    dw_printf ("[%d>nt%s] ", chan, ts);
	    dw_printf ("%s", stemp);			/* stations followed by : */
	    ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	    dw_printf ("\n");

	    nettnc_send_packet (chan, pp);

	  }

	  ax25_delete(pp);
	  return;
	}
#endif





// Normal case - put in queue for radio transmission.
// Error if trying to transmit to a radio channel which was not configured.

	if (chan < 0 || chan >= MAX_RADIO_CHANS || save_audio_config_p->chan_medium[chan] == MEDIUM_NONE) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Request to transmit on invalid radio channel %d.\n", chan);
	  dw_printf ("This is probably a client application error, not a problem with direwolf.\n");
	  dw_printf ("Are you using AX.25 for Linux?  It might be trying to use a modified\n");
	  dw_printf ("version of KISS which uses the port field differently than the\n");
	  dw_printf ("original KISS protocol specification.  The solution might be to use\n");
	  dw_printf ("a command like \"kissparms -c 1 -p radio\" to set CRC none mode.\n");
	  dw_printf ("\n");
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
 */

	if (ax25_is_aprs(pp) && tq_count(chan,prio,"","",0) > 100) {
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

} /* end tq_append */



/*-------------------------------------------------------------------
 *
 * Name:        lm_data_request
 *
 * Purpose:     Add an AX.25 frame to the end of the specified transmit queue.
 *
 *		Use tq_append instead for APRS.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI for priority (expedited)
 *				or TQ_PRIO_1_LO for normal.
 *
 *		pp	- Address of packet object.
 *				Caller should NOT make any references to
 *				it after this point because it could
 *				be deleted at any time.
 *
 * Outputs:	A packet object is added to transmit queue.
 *
 * Description:	5.4.
 *
 *		LM-DATA Request. The Data-link State Machine uses this primitive to pass
 *		frames of any type (SABM, RR, UI, etc.) to the Link Multiplexer State Machine.
 *
 *		LM-EXPEDITED-DATA Request. The data-link machine uses this primitive to
 *		request transmission of each digipeat or expedite data frame.
 *
 *		C2a.1
 *
 *		PH-DATA Request. This primitive from the Link Multiplexer State Machine
 *		provides an AX.25 frame of any type (UI, SABM, I, etc.) that is to be transmitted. An
 *		unlimited number of frames may be provided. If the transmission exceeds the 10-
 *		minute limit or the anti-hogging time limit, the half-duplex Physical State Machine
 *		automatically relinquishes the channel for use by the other stations. The
 *		transmission is automatically resumed at the next transmission opportunity
 *		indicated by the CSMA/p-persistence contention algorithm.
 *
 *		PH-EXPEDITED-DATA Request. This primitive from the Link Multiplexer State
 *		Machine provides the AX.25 frame that is to be transmitted immediately. The
 *		simplex Physical State Machine gives preference to priority frames over normal
 *		frames, and will take advantage of the PRIACK window. Priority frames can be
 *		provided by the link multiplexer at any time; a PH-SEIZE Request and subsequent
 *		PH Release Request are not employed for priority frames.
 *
 *		C3.1
 *
 *		LM-DATA Request. This primitive from the Data-link State Machine provides a
 *		AX.25 frame of any type (UI, SABM, I, etc.) that is to be transmitted. An unlimited
 *		number of frames may be provided. The Link Multiplexer State Machine
 *		accumulates the frames in a first-in, first-out queue until it is time to transmit them.
 *
 *		C4.2
 *
 *		LM-DATA Request. This primitive is used by the Data link State Machines to pass
 *		frames of any type (SABM, RR, UI, etc.) to the Link Multiplexer State Machine.
 *
 *		LM-EXPEDITED-DATA Request. This primitive is used by the Data link State
 *		Machine to pass expedited data to the link multiplexer.
 *
 *
 * Implementation: Add packet to end of linked list.
 *		Signal the transmit thread if the queue was formerly empty.
 *
 *		Note that we have a transmit thread each audio channel.
 *		Two channels can share one audio output device.
 *
 * IMPORTANT!	Don't make an further references to the packet object after
 *		giving it to lm_data_request.
 *
 *--------------------------------------------------------------------*/


// TODO: FIXME:  this is a copy of tq_append.  Need to fine tune and explain why.


void lm_data_request (int chan, int prio, packet_t pp)
{
	packet_t plast;
	packet_t pnext;


#if DEBUG
	unsigned char *pinfo;
	int info_len = ax25_get_info (pp, &pinfo);
	if (info_len > 10) info_len = 10;
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("lm_data_request (chan=%d, prio=%d, pp=%p) \"%*s\"\n", chan, prio, pp, info_len, (char*)pinfo);
#endif


	assert (prio >= 0 && prio < TQ_NUM_PRIO);

	if (pp == NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR:  lm_data_request NULL packet pointer. Please report this!\n");
	  return;
	}

#if AX25MEMDEBUG

	if (ax25memdebug_get()) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("lm_data_request (chan=%d, prio=%d, seq=%d)\n", chan, prio, ax25memdebug_seq(pp));
	}
#endif

	if (chan < 0 || chan >= MAX_RADIO_CHANS || save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  // Connected mode is allowed only with internal modems.
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Request to transmit on invalid radio channel %d.\n", chan);
	  dw_printf ("Connected packet mode is allowed only with internal modems.\n");
	  dw_printf ("Why aren't external KISS modems allowed?  See\n");
	  dw_printf ("Why-is-9600-only-twice-as-fast-as-1200.pdf for explanation.\n");
	  ax25_delete(pp);
	  return;
	}

/* 
 * Is transmit queue out of control? 
 */

	if (tq_count(chan,prio,"","",0) > 250) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Warning: Transmit packet queue for channel %d is extremely long.\n", chan);
	  dw_printf ("Perhaps the channel is so busy there is no opportunity to send.\n");
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("lm_data_request: enter critical section\n");
#endif

	dw_mutex_lock (&tq_mutex);


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
	dw_printf ("lm_data_request: left critical section\n");
#endif

	// Appendix C2a, from the Ax.25 protocol spec, says that a priority frame
	// will start transmission.  If not already transmitting, normal frames
	// will pile up until LM-SEIZE Request starts transmission.


// Erratum: It doesn't take long for that to fail.
// We send SABM(e) frames to the transmit queue and the transmitter doesn't get activated.


//NO!	if (prio == TQ_PRIO_0_HI) {

#if DEBUG
	  dw_printf ("lm_data_request (): about to wake up xmit thread.\n");
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
	      dw_printf ("lm_data_request: pthread_cond_signal err=%d", err);
	      perror ("");
	      exit (1);
	    }

	    dw_mutex_unlock (&(wake_up_mutex[chan]));
	  }
#endif
//NO!	}

} /* end lm_data_request */




/*-------------------------------------------------------------------
 *
 * Name:        lm_seize_request
 *
 * Purpose:     Force start of transmit even if transmit queue is empty.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 * Description:	5.4.
 *
 *		LM-SEIZE Request. The Data-link State Machine uses this primitive to request the
 *		Link Multiplexer State Machine to arrange for transmission at the next available
 *		opportunity. The Data-link State Machine uses this primitive when an
 *		acknowledgement must be made; the exact frame in which the acknowledgement
 *		is sent will be chosen when the actual time for transmission arrives.
 *
 *		C2a.1
 *
 *		PH-SEIZE Request. This primitive requests the simplex state machine to begin
 *		transmitting at the next available opportunity. When that opportunity has been
 *		identified (according to the CSMA/p-persistence algorithm included within), the
 *		transmitter started, a parameterized window provided for the startup of a
 *		conventional repeater (if required), and a parameterized time allowed for the
 *		synchronization of the remote station's receiver (known as TXDELAY in most
 *		implementations), then a PH-SEIZE Confirm primitive is returned to the link
 *		multiplexer.
 *
 *		C3.1
 *
 *		LM-SEIZE Request. This primitive requests the Link Multiplexer State Machine to
 *		arrange for transmission at the next available opportunity. The Data-link State
 *		Machine uses this primitive when an acknowledgment must be made, but the exact
 *		frame in which the acknowledgment will be sent will be chosen when the actual
 *		time for transmission arrives. The Link Multiplexer State Machine uses the LMSEIZE
 *		Confirm primitive to indicate that the transmission opportunity has arrived.
 *		After the Data-link State Machine has provided the acknowledgment, the Data-link
 *		State Machine gives permission to stop transmission with the LM Release Request
 *		primitive.
 *
 *		C4.2
 *
 *		LM-SEIZE Request. This primitive is used by the Data link State Machine to
 *		request the Link Multiplexer State Machine to arrange for transmission at the next
 *		available opportunity. The Data link State Machine uses this primitive when an
 *		acknowledgment must be made, but the exact frame in which the acknowledgment
 *		is sent will be chosen when the actual time for transmission arrives.
 *
 *
 * Implementation: Add a null frame (i.e. length of 0) to give the process a kick.
 *		xmit.c needs to be smart enough to discard it.
 *
 *--------------------------------------------------------------------*/


void lm_seize_request (int chan)
{
	packet_t pp;
	int prio = TQ_PRIO_1_LO;

	packet_t plast;
	packet_t pnext;


#if DEBUG
	unsigned char *pinfo;
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("lm_seize_request (chan=%d)\n", chan);
#endif


	if (chan < 0 || chan >= MAX_RADIO_CHANS || save_audio_config_p->chan_medium[chan] != MEDIUM_RADIO) {
	  // Connected mode is allowed only with internal modems.
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Request to transmit on invalid radio channel %d.\n", chan);
	  dw_printf ("Connected packet mode is allowed only with internal modems.\n");
	  dw_printf ("Why aren't external KISS modems allowed?  See\n");
	  dw_printf ("Why-is-9600-only-twice-as-fast-as-1200.pdf for explanation.\n");
	  return;
	}

	pp = ax25_new();

#if AX25MEMDEBUG

	if (ax25memdebug_get()) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("lm_seize_request (chan=%d, seq=%d)\n", chan, ax25memdebug_seq(pp));
	}
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("lm_seize_request: enter critical section\n");
#endif

	dw_mutex_lock (&tq_mutex);


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
	dw_printf ("lm_seize_request: left critical section\n");
#endif


#if DEBUG
	dw_printf ("lm_seize_request (): about to wake up xmit thread.\n");
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
	    dw_printf ("lm_seize_request: pthread_cond_signal err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  dw_mutex_unlock (&(wake_up_mutex[chan]));
	}
#endif

} /* end lm_seize_request */




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
	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

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

} /* end tq_remove */



/*-------------------------------------------------------------------
 *
 * Name:        tq_peek
 *
 * Purpose:     Take a peek at the next frame in the queue but don't remove it.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI or TQ_PRIO_1_LO.
 *
 * Returns:	Pointer to packet object or NULL.
 *
 *		Caller should NOT destroy it because it is still in the queue.
 *
 *--------------------------------------------------------------------*/

packet_t tq_peek (int chan, int prio)
{

	packet_t result_p;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_peek(%d,%d) enter critical section\n", chan, prio);
#endif

	// I don't think we need critical region here.
	//dw_mutex_lock (&tq_mutex);

	result_p = queue_head[chan][prio];
	// Just take a peek at the head.  Don't remove it.

	//dw_mutex_unlock (&tq_mutex);

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

} /* end tq_peek */



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
	
	assert (chan >= 0 && chan < MAX_RADIO_CHANS);


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
 * Purpose:     Return count of the number of packets (or bytes) in the specified transmit queue.
 *		This is used only for queries from KISS or AWG client applications.
 *
 * Inputs:	chan	- Channel, 0 is first.
 *
 *		prio	- Priority, use TQ_PRIO_0_HI or TQ_PRIO_1_LO.
 *			  Specify -1 for total of both.
 *
 *		source - If specified, count only those with this source address.
 *
 *		dest	- If specified, count only those with this destination address.
 *
 *		bytes	- If true, return number of bytes rather than packets.
 *
 * Returns:	Number of items in specified queue.	
 *
 *--------------------------------------------------------------------*/

//#define DEBUG2 1


int tq_count (int chan, int prio, char *source, char *dest, int bytes)
{


#if DEBUG2
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_count(chan=%d, prio=%d, source=\"%s\", dest=\"%s\", bytes=%d)\n", chan, prio, source, dest, bytes);
#endif

	if (prio == -1) {
	  return (tq_count(chan, TQ_PRIO_0_HI, source, dest, bytes)
		+ tq_count(chan, TQ_PRIO_1_LO, source, dest, bytes));
	}

	// Array bounds check.  FIXME: TODO:  should have internal error instead of dying.

	if (chan < 0 || chan >= MAX_RADIO_CHANS || prio < 0 || prio >= TQ_NUM_PRIO) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("INTERNAL ERROR - tq_count(%d, %d, \"%s\", \"%s\", %d)\n", chan, prio, source, dest, bytes);
	  return (0);
	}

	if (queue_head[chan][prio] == 0) {
#if DEBUG2
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("tq_count: queue chan %d, prio %d is empty, returning 0.\n", chan, prio);
#endif
	  return (0);
	}

	// Don't want lists being rearranged while we are traversing them.

	dw_mutex_lock (&tq_mutex);

	int n = 0;		// Result.  Number of bytes or packets.
	packet_t pp = queue_head[chan][prio];;

	while (pp != NULL) {
	 if (ax25_get_num_addr(pp) >= AX25_MIN_ADDRS) {
	  // Consider only real packets.

	  int count_it = 1;

	  if (source != NULL && *source != '\0') {
	    char frame_source[AX25_MAX_ADDR_LEN];
	    ax25_get_addr_with_ssid (pp, AX25_SOURCE, frame_source);
#if DEBUG2
	// I'm cringing at the thought of printing while in a critical region.  But it's only for temp debug.  :-(
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("tq_count: compare to frame source %s\n", frame_source);
#endif
	    if (strcmp(source,frame_source) != 0) count_it = 0;
	  }
	  if (count_it && dest != NULL && *dest != '\0') {
	    char frame_dest[AX25_MAX_ADDR_LEN];
	    ax25_get_addr_with_ssid (pp, AX25_DESTINATION, frame_dest);
#if DEBUG2
	// I'm cringing at the thought of printing while in a critical region.  But it's only for debug debug.  :-(
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("tq_count: compare to frame destination %s\n", frame_dest);
#endif
	    if (strcmp(dest,frame_dest) != 0) count_it = 0;
	  }

	  if (count_it) {
	    if (bytes) {
	      n += ax25_get_frame_len(pp);
	    }
	    else {
	      n++;
	    }
	  }
	 }
	 pp = ax25_get_nextp(pp);
	}

	dw_mutex_unlock (&tq_mutex);

#if DEBUG2
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("tq_count(%d, %d, \"%s\", \"%s\", %d) returns %d\n", chan, prio, source, dest, bytes, n);
#endif

	return (n);

} /* end tq_count */

/* end tq.c */

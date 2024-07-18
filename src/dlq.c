//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2014, 2015, 2016, 2018  John Langner, WB2OSZ
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
 * Description: In earlier versions, the main thread read from the
 *		audio device and performed the receive demodulation/decoding.
 *
 *		Since version 1.2 we have a separate receive thread
 *		for each audio device.  This queue is used to collect
 *		received frames from all channels and process them
 *		serially.
 *
 *		In version 1.4, other types of events also go into this
 *		queue and we use it to drive the data link state machine.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#if __WIN32__
#else
#include <errno.h>
#endif

#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "dlq.h"
#include "dedupe.h"
#include "dtime_now.h"


/* The queue is a linked list of these. */

static struct dlq_item_s *queue_head = NULL;	/* Head of linked list for queue. */

#if __WIN32__

// TODO1.2: use dw_mutex_t

static CRITICAL_SECTION dlq_cs;			/* Critical section for updating queues. */

static HANDLE wake_up_event;			/* Notify received packet processing thread when queue not empty. */

#else

static pthread_mutex_t dlq_mutex;		/* Critical section for updating queues. */

static pthread_cond_t wake_up_cond;		/* Notify received packet processing thread when queue not empty. */

static pthread_mutex_t wake_up_mutex;		/* Required by cond_wait. */

static volatile int recv_thread_is_waiting = 0;

#endif

static int was_init = 0;			/* was initialization performed? */

static void append_to_queue (struct dlq_item_s *pnew);

static volatile int s_new_count = 0;		/* To detect memory leak for queue items. */
static volatile int s_delete_count = 0;		// TODO:  need to test.


static volatile int s_cdata_new_count = 0;		/* To detect memory leak for connected mode data. */
static volatile int s_cdata_delete_count = 0;		// TODO:  need to test.



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
	int err;
	err = pthread_mutex_init (&wake_up_mutex, NULL);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_init: pthread_mutex_init err=%d", err);
	  perror ("");
	  exit (EXIT_FAILURE);
	}
	err = pthread_mutex_init (&dlq_mutex, NULL);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_init: pthread_mutex_init err=%d", err);
	  perror ("");
	  exit (EXIT_FAILURE);
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
 * Name:        dlq_rec_frame
 *
 * Purpose:     Add a received packet to the end of the queue.
 *		Normally this was received over the radio but we can create
 *		our own from APRStt or beaconing.
 *
 *		This would correspond to PH-DATA Indication in the AX.25 protocol spec.
 *
 * Inputs:	chan	- Channel, 0 is first.
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
 *		fec_type - Was it from FX.25 or IL2P?  Need to know because
 *			  meaning of retries is different.
 *
 *		retries	- Level of correction used.
 *
 *		spectrum - Display of how well multiple decoders did.
 *
 *
 * IMPORTANT!	Don't make an further references to the packet object after
 *		giving it to dlq_append.
 *
 *--------------------------------------------------------------------*/

void dlq_rec_frame (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum)
{

	struct dlq_item_s *pnew;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_rec_frame (chan=%d, pp=%p, ...)\n", chan, pp);
#endif

	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);	// TOTAL to include virtual channels.

	if (pp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR:  dlq_rec_frame NULL packet pointer. Please report this!\n");
	  return;
	}

#if AX25MEMDEBUG

	if (ax25memdebug_get()) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_rec_frame (chan=%d.%d, seq=%d, ...)\n", chan, subchan, ax25memdebug_seq(pp));
	}
#endif


/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	if (s_new_count > s_delete_count + 50) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR:  DLQ memory leak, new=%d, delete=%d\n", s_new_count, s_delete_count);
	}

	pnew->nextp = NULL;
	pnew->type = DLQ_REC_FRAME;
	pnew->chan = chan;
	pnew->slice = slice;
	pnew->subchan = subchan;
	pnew->pp = pp;
	pnew->alevel = alevel;
	pnew->fec_type = fec_type;
	pnew->retries = retries;
	if (spectrum == NULL) 
	  strlcpy(pnew->spectrum, "", sizeof(pnew->spectrum));
	else
	  strlcpy(pnew->spectrum, spectrum, sizeof(pnew->spectrum));

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_rec_frame */



/*-------------------------------------------------------------------
 *
 * Name:        append_to_queue
 *
 * Purpose:     Append some type of event to queue.
 *		This includes frames received over the radio,
 *		requests from client applications, and notifications
 *		from the frame transmission process.
 *
 *
 * Inputs:	pnew		- Pointer to queue element structure.
 *
 * Outputs:	Information is appended to queue.
 *
 * Description:	Add item to end of linked list.
 *		Signal the receive processing thread if the queue was formerly empty.
 *
 *--------------------------------------------------------------------*/

static void append_to_queue (struct dlq_item_s *pnew)
{
	struct dlq_item_s *plast;
	int queue_length = 0;

	if ( ! was_init) {
	  dlq_init ();
	}

	pnew->nextp = NULL;

#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq append_to_queue: enter critical section\n");
#endif
#if __WIN32__
	EnterCriticalSection (&dlq_cs);
#else
	int err;
	err = pthread_mutex_lock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq append_to_queue: pthread_mutex_lock err=%d", err);
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
	  dw_printf ("dlq append_to_queue: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq append_to_queue: left critical section\n");
	dw_printf ("dlq append_to_queue (): about to wake up recv processing thread.\n");
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
	    dw_printf ("dlq append_to_queue: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_cond_signal (&wake_up_cond);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq append_to_queue: pthread_cond_signal err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq append_to_queue: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }
	}
#endif

} /* end append_to_queue */


/*-------------------------------------------------------------------
 *
 * Name:        dlq_connect_request
 *
 * Purpose:     Client application has requested connection to another station.
 *
 * Inputs:	addrs		- Source (owncall), destination (peercall),
 *				  and possibly digipeaters.
 *
 *		num_addr	- Number of addresses.  2 to 10.
 *
 *		chan		- Channel, 0 is first.
 *
 *		client		- Client application instance.  We could have multiple
 *				  applications, all on the same channel, connecting
 *				  to different stations.   We need to know which one
 *				  should get the results.
 *
 *		pid		- Protocol ID for data.  Normally 0xf0 but the API
 *				  allows the client app to use something non-standard
 *				  for special situations.
 *						TODO: remove this.   PID is only for I and UI frames.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 *--------------------------------------------------------------------*/

void dlq_connect_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client, int pid)
{
	struct dlq_item_s *pnew;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_connect_request (...)\n");
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_CONNECT_REQUEST;
	pnew->chan = chan;
	memcpy (pnew->addrs, addrs, sizeof(pnew->addrs));
	pnew->num_addr = num_addr;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_connect_request */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_disconnect_request
 *
 * Purpose:     Client application has requested to disconnect.
 *
 * Inputs:	addrs		- Source (owncall), destination (peercall),
 *				  and possibly digipeaters.
 *
 *		num_addr	- Number of addresses.  2 to 10.
 *				  Only first two matter in this case.
 *
 *		chan		- Channel, 0 is first.
 *
 *		client		- Client application instance.  We could have multiple
 *				  applications, all on the same channel, connecting
 *				  to different stations.   We need to know which one
 *				  should get the results.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 *--------------------------------------------------------------------*/

void dlq_disconnect_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client)
{
	struct dlq_item_s *pnew;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_disconnect_request (...)\n");
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_DISCONNECT_REQUEST;
	pnew->chan = chan;
	memcpy (pnew->addrs, addrs, sizeof(pnew->addrs));
	pnew->num_addr = num_addr;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_disconnect_request */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_outstanding_frames_request
 *
 * Purpose:     Client application wants to know number of outstanding information
 *		frames supplied, supplied by the client, that have not yet been
 *		delivered to the remote station.
 *
 * Inputs:	addrs		- Source (owncall), destination (peercall)
 *
 *		num_addr	- Number of addresses.  Should be 2.
 *				  If more they will be ignored.
 *
 *		chan		- Channel, 0 is first.
 *
 *		client		- Client application instance.  We could have multiple
 *				  applications, all on the same channel, connecting
 *				  to different stations.   We need to know which one
 *				  should get the results.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 * Description:	The data link state machine will count up all information frames 
 *		for the given source(mycall) / destination(remote) / channel link.
 *		A 'Y' response will be sent back to the client application.
 *
 *--------------------------------------------------------------------*/

void dlq_outstanding_frames_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client)
{
	struct dlq_item_s *pnew;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_outstanding_frames_request (...)\n");
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_OUTSTANDING_FRAMES_REQUEST;
	pnew->chan = chan;
	memcpy (pnew->addrs, addrs, sizeof(pnew->addrs));
	pnew->num_addr = num_addr;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_outstanding_frames_request */






/*-------------------------------------------------------------------
 *
 * Name:        dlq_xmit_data_request
 *
 * Purpose:     Client application has requested transmission of connected
 *		data over an established link.
 *
 * Inputs:	addrs		- Source (owncall), destination (peercall),
 *				  and possibly digipeaters.
 *
 *		num_addr	- Number of addresses.  2 to 10.
 *				  First two are used to uniquely identify link.
 *				  Any digipeaters involved are remembered
 *				  from when the link was established.
 *
 *		chan		- Channel, 0 is first.
 *
 *		client		- Client application instance.
 *
 *		pid		- Protocol ID for data.  Normally 0xf0 but the API
 *				  allows the client app to use something non-standard
 *				  for special situations.
 *
 *		xdata_ptr	- Pointer to block of data.
 *
 *		xdata_len	- Length of data in bytes.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 *--------------------------------------------------------------------*/


void dlq_xmit_data_request (char addrs[AX25_MAX_ADDRS][AX25_MAX_ADDR_LEN], int num_addr, int chan, int client, int pid, char *xdata_ptr, int xdata_len)
{
	struct dlq_item_s *pnew;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_xmit_data_request (...)\n");
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_XMIT_DATA_REQUEST;
	pnew->chan = chan;
	memcpy (pnew->addrs, addrs, sizeof(pnew->addrs));
	pnew->num_addr = num_addr;
	pnew->client = client;

/* Attach the transmit data. */

	pnew->txdata = cdata_new(pid,xdata_ptr,xdata_len);

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_xmit_data_request */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_register_callsign
 *		dlq_unregister_callsign
 *
 * Purpose:     Register callsigns that we will recognize for incoming connection requests.
 *
 * Inputs:	addr		- Callsign to [un]register.
 *
 *		chan		- Channel, 0 is first.
 *
 *		client		- Client application instance.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 * Description:	The data link state machine does not use MYCALL from the APRS configuration.
 *		For outgoing frames, the client supplies the source callsign.
 *		For incoming connection requests, we need to know what address(es) to respond to.
 *
 *		Note that one client application can register multiple callsigns for
 *		multiple channels.
 *		Different clients can register different different addresses on the same channel.
 *
 *--------------------------------------------------------------------*/


void dlq_register_callsign (char *addr, int chan, int client)
{
	struct dlq_item_s *pnew;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_register_callsign (%s, chan=%d, client=%d)\n", addr, chan, client);
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_REGISTER_CALLSIGN;
	pnew->chan = chan;
	strlcpy (pnew->addrs[0], addr, sizeof(pnew->addrs[0]));
	pnew->num_addr = 1;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_register_callsign */


void dlq_unregister_callsign (char *addr, int chan, int client)
{
	struct dlq_item_s *pnew;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_unregister_callsign (%s, chan=%d, client=%d)\n", addr, chan, client);
#endif

	assert (chan >= 0 && chan < MAX_RADIO_CHANS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_UNREGISTER_CALLSIGN;
	pnew->chan = chan;
	strlcpy (pnew->addrs[0], addr, sizeof(pnew->addrs[0]));
	pnew->num_addr = 1;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_unregister_callsign */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_channel_busy
 *
 * Purpose:     Inform data link state machine about activity on the radio channel.
 *
 * Inputs:	chan		- Radio channel number.
 *
 *		activity	- OCTYPE_PTT or OCTYPE_DCD, as defined in audio.h.
 *				  Other values will be discarded.
 *
 *		status		- 1 for active or 0 for quiet.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 * Description:	Notify the link state machine about changes in carrier detect
 *		and our transmitter.
 *		This is needed for pausing some of our timers.   For example,
 *		if we transmit a frame and expect a response in 3 seconds, that
 *		might be delayed because someone else is using the channel.
 *
 *--------------------------------------------------------------------*/

void dlq_channel_busy (int chan, int activity, int status)
{
	struct dlq_item_s *pnew;

	if (activity == OCTYPE_PTT || activity == OCTYPE_DCD) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_channel_busy (...)\n");
#endif


/* Allocate a new queue item. */

	  pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	  if (pnew == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("FATAL ERROR: Out of memory.\n");
	    exit (EXIT_FAILURE);
	  }
	  s_new_count++;

	  pnew->type = DLQ_CHANNEL_BUSY;
	  pnew->chan = chan;
	  pnew->activity = activity;
	  pnew->status = status;

/* Put it into queue. */

	  append_to_queue (pnew);
	}

} /* end dlq_channel_busy */





/*-------------------------------------------------------------------
 *
 * Name:        dlq_seize_confirm
 *
 * Purpose:     Inform data link state machine that the transmitter is on.
 *		This is in response to lm_seize_request.
 *
 * Inputs:	chan		- Radio channel number.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 * Description:	When removed from the data link state machine queue, this
 *		becomes lm_seize_confirm.
 *
 *--------------------------------------------------------------------*/

void dlq_seize_confirm (int chan)
{
	struct dlq_item_s *pnew;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_seize_confirm (chan=%d)\n", chan);
#endif


/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	pnew->type = DLQ_SEIZE_CONFIRM;
	pnew->chan = chan;

/* Put it into queue. */

	append_to_queue (pnew);


} /* end dlq_seize_confirm */




/*-------------------------------------------------------------------
 *
 * Name:        dlq_client_cleanup
 *
 * Purpose:     Client application has disappeared.
 *		i.e. The TCP connection has been broken.
 *
 * Inputs:	client		- Client application instance.
 *
 * Outputs:	Request is appended to queue for processing by
 *		the data link state machine.
 *
 * Description:	Notify the link state machine that given client has gone away.
 *		Clean up all information related to that client application.
 *
 *--------------------------------------------------------------------*/

void dlq_client_cleanup (int client)
{
	struct dlq_item_s *pnew;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_client_cleanup (...)\n");
#endif

	// assert (client >= 0 && client < MAX_NET_CLIENTS);

/* Allocate a new queue item. */

	pnew = (struct dlq_item_s *) calloc (sizeof(struct dlq_item_s), 1);
	if (pnew == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}
	s_new_count++;

	// All we care about is the client number.

	pnew->type = DLQ_CLIENT_CLEANUP;
	pnew->client = client;

/* Put it into queue. */

	append_to_queue (pnew);

} /* end dlq_client_cleanup */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_wait_while_empty
 *
 * Purpose:     Sleep while the received data queue is empty rather than
 *		polling periodically.
 *
 * Inputs:	timeout		- Return at this time even if queue is empty.
 *				  Zero for no timeout.
 *
 * Returns:	True if timed out before any event arrived.
 *
 * Description:	In version 1.4, we add timeout option so we can continue after
 *		some amount of time even if no events are in the queue.
 *		
 *--------------------------------------------------------------------*/


int dlq_wait_while_empty (double timeout)
{
	int timed_out_result = 0;

#if DEBUG1
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dlq_wait_while_empty (%.3f)\n", timeout);
#endif

	if ( ! was_init) {
	  dlq_init ();
	}


	if (queue_head == NULL) {

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("dlq_wait_while_empty (): prepare to SLEEP...\n");
#endif


#if __WIN32__

	  if (timeout != 0.0) {

	    DWORD ms = (timeout - dtime_now()) * 1000;
	    if (ms <= 0) ms = 1;
#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("WaitForSingleObject: timeout after %d ms\n", ms);
#endif
	    if (WaitForSingleObject (wake_up_event, ms) == WAIT_TIMEOUT) {
	      timed_out_result = 1;
	    }
	  }
	  else {
	    WaitForSingleObject (wake_up_event, INFINITE);
	  }

#else
	  int err;

	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("dlq_wait_while_empty: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  recv_thread_is_waiting = 1;
	  if (timeout != 0.0) {
	    struct timespec abstime;

	    abstime.tv_sec = (time_t)(long)timeout;
	    abstime.tv_nsec = (long)((timeout - (long)abstime.tv_sec) * 1000000000.0);

	    err = pthread_cond_timedwait (&wake_up_cond, &wake_up_mutex, &abstime);
	    if (err == ETIMEDOUT) {
	      timed_out_result = 1;
	    }
	  }
	  else {
	    err = pthread_cond_wait (&wake_up_cond, &wake_up_mutex);
	  }
	  recv_thread_is_waiting = 0;

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
	dw_printf ("dlq_wait_while_empty () returns timedout=%d\n", timed_out_result);
#endif
	return (timed_out_result);

} /* end dlq_wait_while_empty */



/*-------------------------------------------------------------------
 *
 * Name:        dlq_remove
 *
 * Purpose:     Remove an item from the head of the queue.
 *
 * Inputs:	None.
 *
 * Returns:	Pointer to a queue item.  Caller is responsible for deleting it.
 *		NULL if queue is empty.
 *
 *--------------------------------------------------------------------*/


struct dlq_item_s *dlq_remove (void)
{

	struct dlq_item_s *result = NULL;
	//int err;

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
	int err;

	err = pthread_mutex_lock (&dlq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("dlq_remove: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	if (queue_head != NULL) {
	  result = queue_head;
	  queue_head = queue_head->nextp;
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
	dw_printf ("dlq_remove()  returns \n");
#endif

#if AX25MEMDEBUG

	if (ax25memdebug_get() && result != NULL) {
	  text_color_set(DW_COLOR_DEBUG);
	  if (result->pp != NULL) {
// TODO: mnemonics for type.
	    dw_printf ("dlq_remove (type=%d, chan=%d.%d, seq=%d, ...)\n", result->type, result->chan, result->subchan, ax25memdebug_seq(result->pp));
	  }
	  else {
	    dw_printf ("dlq_remove (type=%d, chan=%d, ...)\n", result->type, result->chan);
	  }
	}
#endif

	return (result);
}


/*-------------------------------------------------------------------
 *
 * Name:        dlq_delete
 *
 * Purpose:     Release storage used by a queue item.
 *
 * Inputs:	pitem		- Pointer to a queue item.
 *
 *--------------------------------------------------------------------*/


void dlq_delete (struct dlq_item_s *pitem)
{
	if (pitem == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR: dlq_delete()  given NULL pointer.\n");
	  return;
	}

	s_delete_count++;

	if (pitem->pp != NULL) {
	  ax25_delete (pitem->pp);
	  pitem->pp = NULL;
	}

	if (pitem->txdata != NULL) {
	  cdata_delete (pitem->txdata);
	  pitem->txdata = NULL;
	}

	free (pitem);

} /* end dlq_delete */




/*-------------------------------------------------------------------
 *
 * Name:        cdata_new
 *
 * Purpose:     Allocate blocks of data for sending and receiving connected data.
 *
 * Inputs:	pid	- protocol id.
 *		data	- pointer to data.  Can be NULL for segment reassembler.
 *		len	- length of data.
 *
 * Returns:	Structure with a copy of the data.
 *
 * Description:	The flow goes like this:
 *
 *		Client application extablishes a connection with another station.
 *		Client application calls "dlq_xmit_data_request."
 *		A copy of the data is made with this function and attached to the queue item.
 *		The txdata block is attached to the appropriate link state machine.
 *		At the proper time, it is transmitted in an I frame.
 *		It needs to be kept around in case it needs to be retransmitted.
 *		When no longer needed, it is freed with cdata_delete.
 *
 *--------------------------------------------------------------------*/


cdata_t *cdata_new (int pid, char *data, int len)
{
	int size;
	cdata_t *cdata;

	s_cdata_new_count++;

	/* Round up the size to the next 128 bytes. */
	/* The theory is that a smaller number of unique sizes might be */
	/* beneficial for memory fragmentation and garbage collection. */

	size = ( len + 127 ) & ~0x7f;

	cdata = malloc ( sizeof(cdata_t) + size );
	if (cdata == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("FATAL ERROR: Out of memory.\n");
	  exit (EXIT_FAILURE);
	}

	cdata->magic = TXDATA_MAGIC;
	cdata->next = NULL;
	cdata->pid = pid;
	cdata->size = size;
	cdata->len = len;

	assert (len >= 0 && len <= size);
	if (data == NULL) {
	  memset (cdata->data, '?', size);
	}
	else {
	  memcpy (cdata->data, data, len);
	}
	return (cdata);

}  /* end cdata_new */



/*-------------------------------------------------------------------
 *
 * Name:        cdata_delete
 *
 * Purpose:     Release storage used by a connected data block.
 *
 * Inputs:	cdata		- Pointer to a data block.
 *
 *--------------------------------------------------------------------*/


void cdata_delete (cdata_t *cdata)
{
	if (cdata == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR: cdata_delete()  given NULL pointer.\n");
	  return;
	}

	if (cdata->magic != TXDATA_MAGIC) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("INTERNAL ERROR: cdata_delete()  given corrupted data.\n");
	  return;
	}

	s_cdata_delete_count++;

	cdata->magic = 0;

	free (cdata);

} /* end cdata_delete */


/*-------------------------------------------------------------------
 *
 * Name:        cdata_check_leak
 *
 * Purpose:     Check for memory leak of cdata items.
 *
 * Description:	This is called when we expect no outstanding allocations.
 *
 *--------------------------------------------------------------------*/


void cdata_check_leak (void)
{
	if (s_cdata_delete_count != s_cdata_new_count) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal Error, %s, new=%d, delete=%d\n", __func__, s_cdata_new_count, s_cdata_delete_count);
	}

} /* end cdata_check_leak */



/* end dlq.c */

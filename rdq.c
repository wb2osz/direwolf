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
 * Module:      rdq.c
 *
 * Purpose:   	Retry later decode queue for frames with bad FCS.
 *		
 * Description:	
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
#include "rdq.h"
#include "dedupe.h"



static rrbb_t queue_head;			/* Head of linked list for queue. */

#if __WIN32__

static CRITICAL_SECTION rdq_cs;			/* Critical section for updating queues. */

static HANDLE wake_up_event;			/* Notify try decode again thread when queue not empty. */

#else

static pthread_mutex_t rdq_mutex;		/* Critical section for updating queues. */

static pthread_cond_t wake_up_cond;		/* Notify try decode again thread when queue not empty. */

static pthread_mutex_t wake_up_mutex;		/* Required by cond_wait. */

#endif


/*-------------------------------------------------------------------
 *
 * Name:        rdq_init
 *
 * Purpose:     Initialize the receive decode again queue.
 *
 * Inputs:	None.  Only single queue for all channels.
 *
 * Outputs:	
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *--------------------------------------------------------------------*/


void rdq_init (void)
{
	//int c, p;
#if __WIN32__
#else
	int err;
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_init (  )\n");
	dw_printf ("rdq_init: pthread_mutex_init...\n");
#endif

#if __WIN32__
	InitializeCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_init (&wake_up_mutex, NULL);
	err = pthread_mutex_init (&rdq_mutex, NULL);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_init: pthread_mutex_init err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif



#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_init: pthread_cond_init...\n");
#endif

#if __WIN32__

	wake_up_event = CreateEvent (NULL, 0, 0, NULL);

	if (wake_up_event == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_init: pthread_cond_init: can't create decode wake up event");
	  exit (1);
	}

#else
	err = pthread_cond_init (&wake_up_cond, NULL);


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_init: pthread_cond_init returns %d\n", err);
#endif


	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_init: pthread_cond_init err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif


} /* end rdq_init */


/*-------------------------------------------------------------------
 *
 * Name:        rdq_append
 *
 * Purpose:     Add a packet to the end of the queue.
 *
 * Inputs:	pp	- Address of raw received bit buffer.
 *				Caller should NOT make any references to
 *				it after this point because it could
 *				be deleted at any time.
 *
 * Outputs:	
 *
 * Description:	Add buffer to end of linked list.
 *		Signal the decode thread if the queue was formerly empty.
 *
 *--------------------------------------------------------------------*/

void rdq_append (rrbb_t rrbb)
{
	//int was_empty;
	rrbb_t plast;
	rrbb_t pnext;
#ifndef __WIN32__
	int err;
#endif


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_append (rrbb=%p)\n", rrbb);
	dw_printf ("rdq_append: enter critical section\n");
#endif
#if __WIN32__
	EnterCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_lock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_append: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	//was_empty = 1;
	//if (queue_head != NULL) {
	       //was_empty = 0;
	//}
	if (queue_head == NULL) {
	  queue_head = rrbb;
	}
	else {
	  plast = queue_head;
	  while ((pnext = rrbb_get_nextp(plast)) != NULL) {
	    plast = pnext;
	  }
	  rrbb_set_nextp (plast, rrbb);
	}


#if __WIN32__ 
	LeaveCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_unlock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_append: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_append: left critical section\n");
	dw_printf ("rdq_append (): about to wake up retry decode thread.\n");
#endif

#if __WIN32__
	SetEvent (wake_up_event);
#else
	err = pthread_mutex_lock (&wake_up_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_append: pthread_mutex_lock wu err=%d", err);
	  perror ("");
	  exit (1);
	}

	err = pthread_cond_signal (&wake_up_cond);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_append: pthread_cond_signal err=%d", err);
	  perror ("");
	  exit (1);
	}

	err = pthread_mutex_unlock (&wake_up_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_append: pthread_mutex_unlock wu err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        rdq_wait_while_empty
 *
 * Purpose:     Sleep while the queue is empty rather than
 *		polling periodically.
 *
 * Inputs:	None.
 *		
 *--------------------------------------------------------------------*/


void rdq_wait_while_empty (void)
{
	int is_empty;
#ifndef __WIN32__
	int err;
#endif


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_wait_while_empty () : enter critical section\n");
#endif

#if __WIN32__
	EnterCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_lock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_wait_while_empty: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

#if DEBUG
	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("rdq_wait_while_empty (): after pthread_mutex_lock\n");
#endif
	is_empty = 1;
        if (queue_head != NULL)
	       is_empty = 0;


#if __WIN32__
	LeaveCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_unlock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_wait_while_empty: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_wait_while_empty () : left critical section\n");
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_wait_while_empty (): is_empty = %d\n", is_empty);
#endif

	if (is_empty) {
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rdq_wait_while_empty (): SLEEP - about to call cond wait\n");
#endif


#if __WIN32__
	  WaitForSingleObject (wake_up_event, INFINITE);

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rdq_wait_while_empty (): returned from wait\n");
#endif

#else
	  err = pthread_mutex_lock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("rdq_wait_while_empty: pthread_mutex_lock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_cond_wait (&wake_up_cond, &wake_up_mutex);


#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rdq_wait_while_empty (): WOKE UP - returned from cond wait, err = %d\n", err);
#endif

	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("rdq_wait_while_empty: pthread_cond_wait err=%d", err);
	    perror ("");
	    exit (1);
	  }

	  err = pthread_mutex_unlock (&wake_up_mutex);
	  if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("rdq_wait_while_empty: pthread_mutex_unlock wu err=%d", err);
	    perror ("");
	    exit (1);
	  }

#endif
	}


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_wait_while_empty () returns\n");
#endif

}


/*-------------------------------------------------------------------
 *
 * Name:        rdq_remove
 *
 * Purpose:     Remove raw bit buffer from the head of the queue.
 *
 * Inputs:	none
 *
 * Returns:	Pointer to rrbb object.
 *		Caller should destroy it with rrbb_delete when finished with it.	
 *
 *--------------------------------------------------------------------*/

rrbb_t rdq_remove (void)
{

	rrbb_t result_p;
#ifndef __WIN32__
	int err;
#endif


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_remove() enter critical section\n");
#endif

#if __WIN32__
	EnterCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_lock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_remove: pthread_mutex_lock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

	if (queue_head == NULL) {
	  result_p = NULL;
	}
	else {

	  result_p = queue_head;
	  queue_head = rrbb_get_nextp(result_p);
	  rrbb_set_nextp (result_p, NULL);
	}
	 
#if __WIN32__
	LeaveCriticalSection (&rdq_cs);
#else
	err = pthread_mutex_unlock (&rdq_mutex);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("rdq_remove: pthread_mutex_unlock err=%d", err);
	  perror ("");
	  exit (1);
	}
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("rdq_remove() leave critical section, returns %p\n", result_p);
#endif
	return (result_p);
}



/* end rdq.c */

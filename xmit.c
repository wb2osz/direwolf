//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2013,2014  John Langner, WB2OSZ
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
 * Module:      xmit.c
 *
 * Purpose:   	Transmit queued up packets when channel is clear.
 *		
 * Description:	Producers of packets to be transmitted call tq_append and then
 *		go merrily on their way, unconcerned about when the packet might
 *		actually get transmitted.
 *
 *		This thread waits until the channel is clear and then removes
 *		packets from the queue and transmits them.
 *
 *
 * Usage:	(1) The main application calls xmit_init.
 *
 *			This will initialize the transmit packet queue
 *			and create a thread to empty the queue when
 *			the channel is clear.
 *
 *		(2) The application queues up packets by calling tq_append.
 *
 *			Packets that are being digipeated should go in the 
 *			high priority queue so they will go out first.
 *
 *			Other packets should go into the lower priority queue.
 *
 *		(3) xmit_thread removes packets from the queue and transmits
 *			them when other signals are not being heard.
 *
 *---------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>

//#include <sys/time.h>
#include <time.h>

#if __WIN32__
#include <windows.h>
#endif

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "tq.h"
#include "xmit.h"
#include "hdlc_send.h"
#include "hdlc_rec.h"
#include "ptt.h"


static int xmit_num_channels;		/* Number of radio channels. */


/*
 * Parameters for transmission.
 * Each channel can have different timing values.
 *
 * These are initialized once at application startup time
 * and some can be changed later by commands from connected applications.
 */





static int xmit_slottime[MAX_CHANS];	/* Slot time in 10 mS units for persistance algorithm. */

static int xmit_persist[MAX_CHANS];	/* Sets probability for transmitting after each */
					/* slot time delay.  Transmit if a random number */
					/* in range of 0 - 255 <= persist value.  */
					/* Otherwise wait another slot time and try again. */

static int xmit_txdelay[MAX_CHANS];	/* After turning on the transmitter, */
					/* send "flags" for txdelay * 10 mS. */

static int xmit_txtail[MAX_CHANS];		/* Amount of time to keep transmitting after we */
					/* are done sending the data.  This is to avoid */
					/* dropping PTT too soon and chopping off the end */
					/* of the frame.  Again 10 mS units. */

static int xmit_bits_per_sec[MAX_CHANS];	/* Data transmission rate. */
					/* Often called baud rate which is equivalent in */
					/* this case but could be different with other */
					/* modulation techniques. */

static int g_debug_xmit_packet;		/* print packet in hexadecimal form for debugging. */



#define BITS_TO_MS(b,ch) (((b)*1000)/xmit_bits_per_sec[(ch)])

#define MS_TO_BITS(ms,ch) (((ms)*xmit_bits_per_sec[(ch)])/1000)


#if __WIN32__
static unsigned __stdcall xmit_thread (void *arg);
#else
static void * xmit_thread (void *arg);
#endif

static int wait_for_clear_channel (int channel, int nowait, int slotttime, int persist);


/*-------------------------------------------------------------------
 *
 * Name:        xmit_init
 *
 * Purpose:     Initialize the transmit process.
 *
 * Inputs:	modem		- Structure with modem and timing parameters.
 *
 *
 * Outputs:	Remember required information for future use.
 *
 * Description:	Initialize the queue to be empty and set up other
 *		mechanisms for sharing it between different threads.
 *
 *		Start up xmit_thread to actually send the packets
 *		at the appropriate time.
 *
 *--------------------------------------------------------------------*/



void xmit_init (struct audio_s *p_modem, int debug_xmit_packet)
{
	int j;
#if __WIN32__
	HANDLE xmit_th;
#else
	//pthread_attr_t attr;
	//struct sched_param sp;
	pthread_t xmit_tid;
#endif
	int e;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init ( ... )\n");
#endif

	g_debug_xmit_packet = debug_xmit_packet;

/*
 * Push to Talk (PTT) control.
 */
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: about to call ptt_init \n");
#endif
	ptt_init (p_modem);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: back from ptt_init \n");
#endif

/* 
 * Save parameters for later use.
 */
	xmit_num_channels = p_modem->num_channels;
	assert (xmit_num_channels >= 1 && xmit_num_channels <= MAX_CHANS);

	for (j=0; j<MAX_CHANS; j++) {
	  xmit_bits_per_sec[j] = p_modem->baud[j];
	  xmit_slottime[j] = p_modem->slottime[j];
	  xmit_persist[j] = p_modem->persist[j];
	  xmit_txdelay[j] = p_modem->txdelay[j];
	  xmit_txtail[j] = p_modem->txtail[j];
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: about to call tq_init \n");
#endif
	tq_init (xmit_num_channels);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: about to create thread \n");
#endif

//TODO:  xmit thread should be higher priority to avoid
// underrun on the audio output device.

#if __WIN32__
	xmit_th = (HANDLE)_beginthreadex (NULL, 0, xmit_thread, NULL, 0, NULL);
	if (xmit_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create xmit thread\n");
	  return;
	}
#else

#if 0

//TODO: not this simple.  probably need FIFO policy.
	pthread_attr_init (&attr);
  	e = pthread_attr_getschedparam (&attr, &sp);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("pthread_attr_getschedparam");
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Default scheduling priority = %d, min=%d, max=%d\n", 
		sp.sched_priority, 
		sched_get_priority_min(SCHED_OTHER),
		sched_get_priority_max(SCHED_OTHER));
	sp.sched_priority--;

  	e = pthread_attr_setschedparam (&attr, &sp);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("pthread_attr_setschedparam");
	}
	
	e = pthread_create (&xmit_tid, &attr, xmit_thread, (void *)0);
	pthread_attr_destroy (&attr);
#else
	e = pthread_create (&xmit_tid, NULL, xmit_thread, (void *)0);
#endif
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create xmit thread");
	  return;
	}
#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: finished \n");
#endif


} /* end tq_init */




/*-------------------------------------------------------------------
 *
 * Name:        xmit_set_txdelay
 *		xmit_set_persist
 *		xmit_set_slottime
 *		xmit_set_txtail
 *				
 *
 * Purpose:     The KISS protocol, and maybe others, can specify
 *		transmit timing parameters.  If the application
 *		specifies these, they will override what was read
 *		from the configuration file.
 *
 * Inputs:	channel	- should be 0 or 1.
 *
 *		value	- time values are in 10 mSec units.
 *
 *
 * Outputs:	Remember required information for future use.
 *
 * Question:	Should we have an option to enable or disable the
 *		application changing these values?
 *
 * Bugs:	No validity checking other than array subscript out of bounds.
 *
 *--------------------------------------------------------------------*/

void xmit_set_txdelay (int channel, int value)
{
	if (channel >= 0 && channel < MAX_CHANS) {
	  xmit_txdelay[channel] = value;
	}
}

void xmit_set_persist (int channel, int value)
{
	if (channel >= 0 && channel < MAX_CHANS) {
	  xmit_persist[channel] = value;
	}
}

void xmit_set_slottime (int channel, int value)
{
	if (channel >= 0 && channel < MAX_CHANS) {
	  xmit_slottime[channel] = value;
	}
}

void xmit_set_txtail (int channel, int value)
{
	if (channel >= 0 && channel < MAX_CHANS) {
	  xmit_txtail[channel] = value;
	}
}

/*-------------------------------------------------------------------
 *
 * Name:        xmit_thread
 *
 * Purpose:     Initialize the transmit process.
 *
 * Inputs:	None.
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
 *			The KPC-3 configuration option for this is "UIDWAIT OFF".  (?)
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
 *
 * Thought for future research:
 *
 *		Should we send multiple frames in one transmission if we 
 *		have more than one sitting in the queue?  At first I was thinking
 *		this would help reduce channel congestion.  I don't recall seeing
 *		anything in the specifications allowing or disallowing multiple
 *		frames in one transmission.  I can think of some scenarios 
 *		where it might help.  I can think of some where it would 
 *		definitely be counter productive.  
 *		For now, one frame per transmission.
 *
 * What to others have to say about this topic?
 *
 *	"For what it is worth, the original APRSdos used a several second random
 *	generator each time any kind of packet was generated... This is to avoid
 *	bundling. Because bundling, though good for connected packet, is not good
 *	on APRS. Sometimes the digi begins digipeating the first packet in the
 *	bundle and steps all over the remainder of them. So best to make sure each
 *	packet is isolated in time from others..."
 *	
 *		Bob, WB4APR
 *	
 *
 * Version 0.9:	Earlier versions always sent one frame per transmission.
 *		This was fine for APRS but more and more people are now
 *		using this as a KISS TNC for connected protocols.
 *		Rather than having a MAXFRAME configuration file item,
 *		we try setting the maximum number automatically.
 *		1 for digipeated frames, 7 for others.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall xmit_thread (void *arg)
#else
static void * xmit_thread (void *arg)
#endif
{
	packet_t pp;
    	unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
    	int flen;
	int c, p;
	char stemp[1024];	/* max size needed? */
	int info_len;
	unsigned char *pinfo;
	int pre_flags, post_flags;
	int num_bits;		/* Total number of bits in transmission */
				/* including all flags and bit stuffing. */
	int duration;		/* Transmission time in milliseconds. */
	int already;
	int wait_more;
	int ok;

	int maxframe;		/* Maximum number of frames for one transmission. */
	int numframe;		/* Number of frames sent during this transmission. */

/*
 * These are for timing of a transmission.
 * All are in usual unix time (seconds since 1/1/1970) but higher resolution
 */
	double time_ptt;	/* Time when PTT is turned on. */
	double time_now;	/* Current time. */



	while (1) {

	  tq_wait_while_empty ();
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("xmit_thread: woke up\n");
#endif
	  
	  for (p=0; p<TQ_NUM_PRIO; p++) {

	    for (c=0; c<xmit_num_channels; c++) {

	      pp = tq_remove (c, p);
#if DEBUG
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("xmit_thread: tq_remove(chan=%d, prio=%d) returned %p\n", c, p, pp);
#endif
	      if (pp != NULL) {

		maxframe = (p == TQ_PRIO_0_HI) ? 1 : 7;
/* 
 * Wait for the channel to be clear.
 * For the high priority queue, begin transmitting immediately.
 * For the low priority queue, wait a random amount of time, in hopes
 * of minimizing collisions.
 */
	        ok = wait_for_clear_channel (c, (p==TQ_PRIO_0_HI), xmit_slottime[c], xmit_persist[c]);

	        if (ok) {

/*
 * Channel is clear.  
 * Turn on transmitter.
 * Start sending leading flag bytes.
 */
		  time_ptt = dtime_now ();
		  ptt_set (c, 1);

		  pre_flags = MS_TO_BITS(xmit_txdelay[c] * 10, c) / 8;
		  num_bits =  hdlc_send_flags (c, pre_flags, 0);

/*
 * Print trasmitted packet.  Prefix by channel and priority.
 */
	          ax25_format_addrs (pp, stemp);
	          info_len = ax25_get_info (pp, &pinfo);
	          text_color_set(DW_COLOR_XMIT);
	          dw_printf ("[%d%c] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L');
	          dw_printf ("%s", stemp);			/* stations followed by : */
	          ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	          dw_printf ("\n");

/* Optional hex dump of packet. */

		  if (g_debug_xmit_packet) {

	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("------\n");
		    ax25_hex_dump (pp);
    	            dw_printf ("------\n");
		  }

/*
 * Transmit the frame.
 * 1.1J fixed order.
 */	
		  flen = ax25_pack (pp, fbuf);
		  assert (flen >= 1 && flen <= sizeof(fbuf));
		  num_bits += hdlc_send_frame (c, fbuf, flen);
		  numframe = 1;
		  ax25_delete (pp);

/*
 * Additional packets if available and not exceeding max.
 */

		  while (numframe < maxframe && tq_count (c,p) > 0) {

	            pp = tq_remove (c, p);
#if DEBUG
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("xmit_thread: tq_remove(chan=%d, prio=%d) returned %p\n", c, p, pp);
#endif
	            ax25_format_addrs (pp, stemp);
	            info_len = ax25_get_info (pp, &pinfo);
	            text_color_set(DW_COLOR_XMIT);
	            dw_printf ("[%d%c] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L');
	            dw_printf ("%s", stemp);			/* stations followed by : */
	            ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	            dw_printf ("\n");

		    if (g_debug_xmit_packet) {
	              text_color_set(DW_COLOR_DEBUG);
	              dw_printf ("------\n");
		      ax25_hex_dump (pp);
    	              dw_printf ("------\n");
		    }

/*
 * Transmit the frame.
 */		
		    flen = ax25_pack (pp, fbuf);
		    assert (flen >= 1 && flen <= sizeof(fbuf));
		    num_bits += hdlc_send_frame (c, fbuf, flen);
		    numframe++;
		    ax25_delete (pp);
		  }

/* 
 * Generous TXTAIL because we don't know exactly when the sound is done.
 */

		  post_flags = MS_TO_BITS(xmit_txtail[c] * 10, c) / 8;
		  num_bits += hdlc_send_flags (c, post_flags, 1);


/* 
 * We don't know when the sound has actually been produced.
 * hldc_send finishes before anything starts coming out of the speaker.
 * It's all queued up somewhere.
 *
 * Calculate duration of entire frame in milliseconds.
 *
 * Subtract out elapsed time already since PTT was turned to determine
 * how much longer to wait til we turn PTT off.
 */
		  duration = BITS_TO_MS(num_bits, c);
		  time_now = dtime_now();
		  already = (int) ((time_now - time_ptt) * 1000.);
		  wait_more = duration - already;

#if DEBUG
	          text_color_set(DW_COLOR_DEBUG);
	          dw_printf ("xmit_thread: maxframe = %d, numframe = %d\n", maxframe, numframe);
#endif

/* 
 * Wait for all audio to be out before continuing.
 * Provide a hint at delay required in case we don't have a 
 * way to ask the hardware when all the sound has been pushed out.
 */
// TODO:  We have an issue if this is negative.  That means
// we couldn't generate the data fast enough for the sound
// system output and there probably gaps in the signal.

		  audio_wait(wait_more);		

/*
 * Turn off transmitter.
 */
		
		  ptt_set (c, 0);
	        }
	        else {
/*
 * Timeout waiting for clear channel.
 * Discard the packet.
 * Display with ERROR color rather than XMIT color.
 */

	          text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Waited too long for clear channel.  Discarding packet below.\n");

	          ax25_format_addrs (pp, stemp);

	          info_len = ax25_get_info (pp, &pinfo);

	          text_color_set(DW_COLOR_INFO);
	          dw_printf ("[%d%c] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L');

	          dw_printf ("%s", stemp);			/* stations followed by : */
	          ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	          dw_printf ("\n");
		  ax25_delete (pp);

		}
	      } /* for each channel */
	    } /* for high priority then low priority */
	  }
	}

} /* end xmit_thread */



/*-------------------------------------------------------------------
 *
 * Name:        wait_for_clear_channel
 *
 * Purpose:     Wait for the radio channel to be clear and any
 *		additional time for collision avoidance.
 *
 * Inputs:	channel	-	Radio channel number.
 *
 *		nowait	- 	Should be true for the high priority queue
 *				(packets being digipeated).  This will 
 *				allow transmission immediately when the 
 *				channel is clear rather than waiting a 
 *				random amount of time.
 *
 *		slottime - 	Amount of time to wait for each iteration
 *				of the waiting algorithm.  10 mSec units.
 *
 *		persist -	Probability of transmitting 
 *
 * Returns:	1 for OK.  0 for timeout.
 *
 * Description:	
 *
 * Transmit delay algorithm:
 *
 *		Wait for channel to be clear.
 *		Return if nowait is true.
 *
 *		Wait slottime * 10 milliseconds.
 *		Generate an 8 bit random number in range of 0 - 255.
 *		If random number <= persist value, return.
 *		Otherwise repeat.
 *
 * Example:
 *
 *		For typical values of slottime=10 and persist=63,
 *
 *		Delay		Probability
 *		-----		-----------
 *		100		.25					= 25%
 *		200		.75 * .25				= 19%
 *		300		.75 * .75 * .25				= 14%
 *		400		.75 * .75 * .75 * .25			= 11%
 *		500		.75 * .75 * .75 * .75 * .25		= 8%
 *		600		.75 * .75 * .75 * .75 * .75 * .25	= 6%
 *		700		.75 * .75 * .75 * .75 * .75 * .75 * .25	= 4%
 *		etc.		...
 *
 *--------------------------------------------------------------------*/

/* Give up if we can't get a clear channel in a minute. */

#define WAIT_TIMEOUT_MS (60 * 1000)	
#define WAIT_CHECK_EVERY_MS 10

static int wait_for_clear_channel (int channel, int nowait, int slottime, int persist)
{
	int r;
	int n;

	n = 0;
	while (hdlc_rec_data_detect_any(channel)) {
	  SLEEP_MS(WAIT_CHECK_EVERY_MS);
	  n++;
	  if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS)) {
	    return 0;
	  }
	}

	if (nowait) {
	  return 1;
	}

	while (1) {

	  SLEEP_MS (slottime * 10);

	  if (hdlc_rec_data_detect_any(channel)) {
	    continue;   
	  }

	  r = rand() & 0xff;
	  if (r <= persist) {
	    return 1;
 	  }	
	}

} /* end wait_for_clear_channel */




/* Current time in seconds but more resolution than time(). */

/* We don't care what date a 0 value represents because we */
/* only use this to calculate elapsed time. */



double dtime_now (void)
{
#if __WIN32__
	/* 64 bit integer is number of 100 nanosecond intervals from Jan 1, 1601. */

	FILETIME ft;
	
	GetSystemTimeAsFileTime (&ft);

	return ((( (double)ft.dwHighDateTime * (256. * 256. * 256. * 256.) + 
			(double)ft.dwLowDateTime ) / 10000000.) - 11644473600.);
#else
	/* tv_sec is seconds from Jan 1, 1970. */

	struct timespec ts;
	int sec, ns;
	double x1, x2;
	double result;

	clock_gettime (CLOCK_REALTIME, &ts);

	sec = (int)(ts.tv_sec);
	ns = (int)(ts.tv_nsec);
	x1 = (double)(sec);
	x2 = (double)(ns/1000000) *.001;
	result = x1 + x2;

	/* Sometimes this returns NAN.  How could that possibly happen? */
	/* This is REALLY BIZARRE! */
	/* Multiplying a number by a billionth often produces NAN. */
	/* Adding a fraction to a number over a billion often produces NAN. */
	
	/* Turned out to be a hardware problem with one specific computer. */

	if (isnan(result)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\ndtime_now(): %d, %d -> %.3f + %.3f -> NAN!!!\n\n", sec, ns, x1, x2);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("dtime_now() returns %.3f\n", result);
#endif

	return (result);
#endif
}


/* end xmit.c */





//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015, 2016, 2017  John Langner, WB2OSZ
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

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stddef.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "tq.h"
#include "xmit.h"
#include "hdlc_send.h"
#include "hdlc_rec.h"
#include "ptt.h"
#include "dtime_now.h"
#include "morse.h"
#include "dtmf.h"
#include "xid.h"
#include "dlq.h"
#include "server.h"


/*
 * Parameters for transmission.
 * Each channel can have different timing values.
 *
 * These are initialized once at application startup time
 * and some can be changed later by commands from connected applications.
 */


static int xmit_slottime[MAX_RADIO_CHANS];	/* Slot time in 10 mS units for persistence algorithm. */

static int xmit_persist[MAX_RADIO_CHANS];	/* Sets probability for transmitting after each */
					/* slot time delay.  Transmit if a random number */
					/* in range of 0 - 255 <= persist value.  */
					/* Otherwise wait another slot time and try again. */

static int xmit_txdelay[MAX_RADIO_CHANS];	/* After turning on the transmitter, */
					/* send "flags" for txdelay * 10 mS. */

static int xmit_txtail[MAX_RADIO_CHANS];	/* Amount of time to keep transmitting after we */
					/* are done sending the data.  This is to avoid */
					/* dropping PTT too soon and chopping off the end */
					/* of the frame.  Again 10 mS units. */

static int xmit_fulldup[MAX_RADIO_CHANS];	/* Full duplex if non-zero. */

static int xmit_bits_per_sec[MAX_RADIO_CHANS];	/* Data transmission rate. */
					/* Often called baud rate which is equivalent for */
					/* 1200 & 9600 cases but could be different with other */
					/* modulation techniques. */

static int g_debug_xmit_packet;		/* print packet in hexadecimal form for debugging. */


// TODO: When this was first written, bits/sec was same as baud.
// Need to revisit this for PSK modes where they are not the same.

#if 0		// Added during 1.5 beta test

static int BITS_TO_MS (int b, int ch) {

	int bits_per_symbol;

	switch (save_audio_config_p->achan[ch].modem_type) {
	  case MODEM_QPSK:	bits_per_symbol = 2; break;
	  case MODEM_8PSK:	bits_per_symbol = 3; break;
	  case default:		bits_per_symbol = 1; break;
	}

	return ( (b * 1000) / (xmit_bits_per_sec[(ch)] * bits_per_symbol) );
}

static int MS_TO_BITS (int ms, int ch) {

	int bits_per_symbol;

	switch (save_audio_config_p->achan[ch].modem_type) {
	  case MODEM_QPSK:	bits_per_symbol = 2; break;
	  case MODEM_8PSK:	bits_per_symbol = 3; break;
	  case default:		bits_per_symbol = 1; break;
	}

	return ( (ms * xmit_bits_per_sec[(ch)] * bits_per_symbol) / 1000 );  TODO...
}

#else		// OK for 1200, 9600 but wrong for PSK

#define BITS_TO_MS(b,ch) (((b)*1000)/xmit_bits_per_sec[(ch)])

#define MS_TO_BITS(ms,ch) (((ms)*xmit_bits_per_sec[(ch)])/1000)

#endif

#define MAXX(a,b) (((a)>(b)) ? (a) : (b))


#if __WIN32__
static unsigned __stdcall xmit_thread (void *arg);
#else
static void * xmit_thread (void *arg);
#endif


/*
 * When an audio device is in stereo mode, we can have two 
 * different channels that want to transmit at the same time.
 * We are not clever enough to multiplex them so use this
 * so only one is activte at the same time.
 */
static dw_mutex_t audio_out_dev_mutex[MAX_ADEVS];



static int wait_for_clear_channel (int channel, int slotttime, int persist, int fulldup);
static void xmit_ax25_frames (int c, int p, packet_t pp, int max_bundle);
static int send_one_frame (int c, int p, packet_t pp);
static void xmit_speech (int c, packet_t pp);
static void xmit_morse (int c, packet_t pp, int wpm);
static void xmit_dtmf (int c, packet_t pp, int speed);


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
 *		Start up xmit_thread(s) to actually send the packets
 *		at the appropriate time.
 *
 * Version 1.2:	We now allow multiple audio devices with one or two channels each.
 *		Each audio channel has its own thread.
 *
 *--------------------------------------------------------------------*/

static struct audio_s *save_audio_config_p;


void xmit_init (struct audio_s *p_modem, int debug_xmit_packet)
{
	int j;
	int ad;

#if __WIN32__
	HANDLE xmit_th[MAX_RADIO_CHANS];
#else
	//pthread_attr_t attr;
	//struct sched_param sp;
	pthread_t xmit_tid[MAX_RADIO_CHANS];
#endif
	//int e;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init ( ... )\n");
#endif

	save_audio_config_p = p_modem;

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
 * TODO1.2:  Any reason to use global config rather than making a copy?
 */

	for (j=0; j<MAX_RADIO_CHANS; j++) {
	  xmit_bits_per_sec[j] = p_modem->achan[j].baud;
	  xmit_slottime[j] = p_modem->achan[j].slottime;
	  xmit_persist[j] = p_modem->achan[j].persist;
	  xmit_txdelay[j] = p_modem->achan[j].txdelay;
	  xmit_txtail[j] = p_modem->achan[j].txtail;
	  xmit_fulldup[j] = p_modem->achan[j].fulldup;
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: about to call tq_init \n");
#endif
	tq_init (p_modem);


	for (ad = 0; ad < MAX_ADEVS; ad++) {
	  dw_mutex_init (&(audio_out_dev_mutex[ad]));
	}
 
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_init: about to create threads \n");
#endif

//TODO:  xmit thread should be higher priority to avoid
// underrun on the audio output device.


	for (j=0; j<MAX_RADIO_CHANS; j++) {

	  if (p_modem->chan_medium[j] == MEDIUM_RADIO) {
#if __WIN32__
	    xmit_th[j] = (HANDLE)_beginthreadex (NULL, 0, xmit_thread, (void*)(ptrdiff_t)j, 0, NULL);
	    if (xmit_th[j] == NULL) {
	       text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not create xmit thread %d\n", j);
	      return;
	    }
#else
	    int e;
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
	
	    e = pthread_create (&(xmit_tid[j]), &attr, xmit_thread, (void *)(ptrdiff_t)j);
	    pthread_attr_destroy (&attr);
#else
	    e = pthread_create (&(xmit_tid[j]), NULL, xmit_thread, (void *)(ptrdiff_t)j);
#endif
	    if (e != 0) {
	      text_color_set(DW_COLOR_ERROR);
	      perror("Could not create xmit thread for audio device");
	      return;
	    }
#endif
	  }
	}

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
 *		xmit_set_fulldup
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
	if (channel >= 0 && channel < MAX_RADIO_CHANS) {
	  xmit_txdelay[channel] = value;
	}
}

void xmit_set_persist (int channel, int value)
{
	if (channel >= 0 && channel < MAX_RADIO_CHANS) {
	  xmit_persist[channel] = value;
	}
}

void xmit_set_slottime (int channel, int value)
{
	if (channel >= 0 && channel < MAX_RADIO_CHANS) {
	  xmit_slottime[channel] = value;
	}
}

void xmit_set_txtail (int channel, int value)
{
	if (channel >= 0 && channel < MAX_RADIO_CHANS) {
	  xmit_txtail[channel] = value;
	}
}

void xmit_set_fulldup (int channel, int value)
{
	if (channel >= 0 && channel < MAX_RADIO_CHANS) {
	  xmit_fulldup[channel] = value;
	}
}


/*-------------------------------------------------------------------
 *
 * Name:        frame_flavor
 *
 * Purpose:     Separate frames into different flavors so we can decide
 *		which can be bundled into a single transmission and which should
 *		be sent separately.
 *
 * Inputs:	pp	- Packet object.
 *
 * Returns:	Flavor, one of:
 *
 *		FLAVOR_SPEECH		- Destination address is SPEECH.
 *		FLAVOR_MORSE		- Destination address is MORSE.
 *		FLAVOR_DTMF		- Destination address is DTMF.
 *		FLAVOR_APRS_NEW		- APRS original, i.e. not digipeating.
 *		FLAVOR_APRS_DIGI	- APRS digipeating.
 *		FLAVOR_OTHER		- Anything left over, i.e. connected mode.
 *
 *--------------------------------------------------------------------*/

typedef enum flavor_e { FLAVOR_APRS_NEW, FLAVOR_APRS_DIGI, FLAVOR_SPEECH, FLAVOR_MORSE, FLAVOR_DTMF, FLAVOR_OTHER } flavor_t;

static flavor_t frame_flavor (packet_t pp)
{

	if (ax25_is_aprs (pp)) { 	// UI frame, PID 0xF0.
					// It's unfortunate APRS did not use its own special PID.

	  char dest[AX25_MAX_ADDR_LEN];

	  ax25_get_addr_no_ssid(pp, AX25_DESTINATION, dest);

	  if (strcmp(dest, "SPEECH") == 0) {
	   return (FLAVOR_SPEECH);
	  }

	  if (strcmp(dest, "MORSE") == 0) {
	   return (FLAVOR_MORSE);
	  }

	  if (strcmp(dest, "DTMF") == 0) {
	   return (FLAVOR_DTMF);
	  }

	  /* Is there at least one digipeater AND has first one been used? */
	  /* I could be the first in the list or later.  Doesn't matter. */

	  if (ax25_get_num_repeaters(pp) >= 1 && ax25_get_h(pp,AX25_REPEATER_1)) {
	    return (FLAVOR_APRS_DIGI);
	  }

	  return (FLAVOR_APRS_NEW);
	}

	return (FLAVOR_OTHER);

} /* end frame_flavor */

  
/*-------------------------------------------------------------------
 *
 * Name:        xmit_thread
 *
 * Purpose:     Process transmit queue for one channel.
 *
 * Inputs:	transmit packet queue.	
 *
 * Outputs:	
 *
 * Description:	We have different timing rules for different types of
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
 *			AX.25 connected mode also has a couple cases
 *			where "expedited" frames are sent.
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
 * 
 * Version 1.2:	Allow more than one audio device.
 * 		each channel has its own thread.
 *		Add speech capability.
 *
 * Version 1.4:	Rearranged logic for bundling multiple frames into a single transmission.
 *
 *		The rule is that Speech, Morse Code, DTMF, and APRS digipeated frames
 *		are all sent separately.  The rest can be bundled.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall xmit_thread (void *arg)
#else
static void * xmit_thread (void *arg)
#endif
{
	int chan = (int)(ptrdiff_t)arg; // channel number.
	packet_t pp;
	int prio;
	int ok;


	while (1) {

	  tq_wait_while_empty (chan);
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("xmit_thread, channel %d: woke up\n", chan);
#endif

	  // Does this extra loop offer any benefit?
	  while (tq_peek(chan, TQ_PRIO_0_HI) != NULL || tq_peek(chan, TQ_PRIO_1_LO) != NULL) {

/* 
 * Wait for the channel to be clear.
 * If there is something in the high priority queue, begin transmitting immediately.
 * Otherwise, wait a random amount of time, in hopes of minimizing collisions.
 */
	    ok = wait_for_clear_channel (chan, xmit_slottime[chan], xmit_persist[chan], xmit_fulldup[chan]);

	    prio = TQ_PRIO_1_LO;
	    pp = tq_remove (chan, TQ_PRIO_0_HI);
	    if (pp != NULL) {
	      prio = TQ_PRIO_0_HI;
	    }
	    else {
	      pp = tq_remove (chan, TQ_PRIO_1_LO);
	    }

#if DEBUG
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("xmit_thread: tq_remove(chan=%d, prio=%d) returned %p\n", chan, prio, pp);
#endif
	    // Shouldn't have NULL here but be careful.

	    if (pp != NULL) {


	      if (ok) {
/*
 * Channel is clear and we have lock on output device. 
 *
 * If destination is "SPEECH" send info part to speech synthesizer.
 * If destination is "MORSE" send as morse code.
 * If destination is "DTMF" send as Touch Tones.
 */

	        int ssid, wpm, speed;

	        switch (frame_flavor(pp)) {

	          case FLAVOR_SPEECH:
	            xmit_speech (chan, pp);
	            break;

	          case FLAVOR_MORSE:
		    ssid = ax25_get_ssid(pp, AX25_DESTINATION);
		    wpm = (ssid > 0) ? (ssid * 2) : MORSE_DEFAULT_WPM;

		    // This is a bit of a hack so we don't respond too quickly for APRStt.
		    // It will be sent in high priority queue while a beacon wouldn't.  
		    // Add a little delay so user has time release PTT after sending #.
		    // This and default txdelay would give us a second.

		    if (prio == TQ_PRIO_0_HI) {
	              //text_color_set(DW_COLOR_DEBUG);
		      //dw_printf ("APRStt morse xmit delay hack...\n");
		      SLEEP_MS (700);
		    }
	            xmit_morse (chan, pp, wpm);
	            break;

	          case FLAVOR_DTMF:
		    speed = ax25_get_ssid(pp, AX25_DESTINATION);
		    if (speed == 0) speed = 5;	// default half of maximum
	            if (speed > 10) speed = 10;

	            xmit_dtmf (chan, pp, speed);
	            break;

	          case FLAVOR_APRS_DIGI:
	            xmit_ax25_frames (chan, prio, pp, 1);	/* 1 means don't bundle */
					// I don't know if this in some official specification
					// somewhere, but it is generally agreed that APRS digipeaters
					// should send only one frame at a time rather than
					// bundling multiple frames into a single transmission.
					// Discussion here:  http://lists.tapr.org/pipermail/aprssig_lists.tapr.org/2021-September/049034.html
	            break;

	          case FLAVOR_APRS_NEW:
	          case FLAVOR_OTHER:
	          default:
	            xmit_ax25_frames (chan, prio, pp, 256);
	            break;
	        }

	        // Corresponding lock is in wait_for_clear_channel.

	        dw_mutex_unlock (&(audio_out_dev_mutex[ACHAN2ADEV(chan)]));
	      }
	      else {
/*
 * Timeout waiting for clear channel.
 * Discard the packet.
 * Display with ERROR color rather than XMIT color.
 */
		char stemp[1024];	/* max size needed? */
		int info_len;
		unsigned char *pinfo;


	        text_color_set(DW_COLOR_ERROR);
		dw_printf ("Waited too long for clear channel.  Discarding packet below.\n");

	        ax25_format_addrs (pp, stemp);

	        info_len = ax25_get_info (pp, &pinfo);

	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("[%d%c] ", chan, (prio==TQ_PRIO_0_HI) ? 'H' : 'L');

	        dw_printf ("%s", stemp);			/* stations followed by : */
	        ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	        dw_printf ("\n");
		ax25_delete (pp);

	      } /* wait for clear channel error. */
	    } /* Have pp */
	  } /* while queue not empty */
	} /* while 1 */

	return 0;	/* unreachable but quiet the warning. */

} /* end xmit_thread */



/*-------------------------------------------------------------------
 *
 * Name:        xmit_ax25_frames
 *
 * Purpose:     After we have a clear channel, and possibly waited a random time,
 *		we transmit one or more frames.
 *
 * Inputs:	chan	- Channel number.
 *	
 *		prio	- Priority of the first frame.
 *			  Subsequent frames could be different.
 *
 *		pp	- Packet object pointer.
 *			  It will be deleted so caller should not try
 *			  to reference it after this.	
 *
 *		max_bundle - Max number of frames to bundle into one transmission.
 *
 * Description:	Turn on transmitter.
 *		Send flags for TXDELAY time.
 *		Send the first packet, given by pp.
 *		Possibly send more packets from either queue.
 *		Send flags for TXTAIL time.
 *		Turn off transmitter.
 *
 *
 * How many frames in one transmission?  (for APRS)
 *
 *		Should we send multiple frames in one transmission if we 
 *		have more than one sitting in the queue?  At first I was thinking
 *		this would help reduce channel congestion.  I don't recall seeing
 *		anything in the APRS specifications allowing or disallowing multiple
 *		frames in one transmission.  I can think of some scenarios 
 *		where it might help.  I can think of some where it would 
 *		definitely be counter productive.  
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
 *		Rather than having a configuration file item,
 *		we try setting the maximum number automatically.
 *		1 for digipeated frames, 7 for others.
 *
 * Version 1.4: Lift the limit.  We could theoretically have a window size up to 127.
 *		If another section pumps out that many quickly we shouldn't
 *		break it up here.  Empty out both queues with some exceptions.
 *
 *		Digipeated APRS, Speech, and Morse code should have
 *		their own separate transmissions.
 *		Everything else can be bundled together.
 *		Different priorities can share a single transmission.
 *		Once we have control of the channel, we might as well keep going.
 *		[High] Priority frames will always go to head of the line,
 *
 * Version 1.5:	Add full duplex option.
 *
 *--------------------------------------------------------------------*/


static void xmit_ax25_frames (int chan, int prio, packet_t pp, int max_bundle)
{

	int pre_flags, post_flags;
	int num_bits;		/* Total number of bits in transmission */
				/* including all flags and bit stuffing. */
	int duration;		/* Transmission time in milliseconds. */
	int already;
	int wait_more;

	int numframe = 0;	/* Number of frames sent during this transmission. */

/*
 * These are for timing of a transmission.
 * All are in usual unix time (seconds since 1/1/1970) but higher resolution
 */
	double time_ptt;	/* Time when PTT is turned on. */
	double time_now;	/* Current time. */


	int nb;

/* 
 * Turn on transmitter.
 * Start sending leading flag bytes.
 */
	time_ptt = dtime_now ();

// TODO: This was written assuming bits/sec = baud.
// Does it is need to be scaled differently for PSK?

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: t=%.3f, Turn on PTT now for channel %d. speed = %d\n", dtime_now()-time_ptt, chan, xmit_bits_per_sec[chan]);
#endif
	ptt_set (OCTYPE_PTT, chan, 1);

// Inform data link state machine that we are now transmitting.

	dlq_seize_confirm (chan);	// C4.2.  "This primitive indicates, to the Data-link State
					// machine, that the transmission opportunity has arrived."

	pre_flags = MS_TO_BITS(xmit_txdelay[chan] * 10, chan) / 8;
	num_bits =  layer2_preamble_postamble (chan, pre_flags, 0, save_audio_config_p);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: t=%.3f, txdelay=%d [*10], pre_flags=%d, num_bits=%d\n", dtime_now()-time_ptt, xmit_txdelay[chan], pre_flags, num_bits);
	double presleep = dtime_now();
#endif

	SLEEP_MS (10);			// Give data link state machine a chance to
					// to stuff more frames into the transmit queue,
					// in response to dlq_seize_confirm, so
					// we don't run off the end too soon.

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	// How long did sleep last?
	dw_printf ("xmit_thread: t=%.3f, Should be 0.010 second after the above.\n", dtime_now()-time_ptt);
	double naptime = dtime_now() - presleep;
	if (naptime > 0.015) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Sleep for 10 ms actually took %.3f second!\n", naptime);
	}
#endif

/*
 * Transmit the frame.
 */

	nb = send_one_frame (chan, prio, pp);

	num_bits += nb;
	if (nb > 0) numframe++;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: t=%.3f, nb=%d, num_bits=%d, numframe=%d\n", dtime_now()-time_ptt, nb, num_bits, numframe);
#endif
	ax25_delete (pp);

/*
 * See if we can bundle additional frames into this transmission.
 */

	int done = 0;
	while (numframe < max_bundle && ! done) {

/*
 * Peek at what is available.
 * Don't remove from queue yet because it might not be eligible.
 */
	  prio = TQ_PRIO_1_LO;
	  pp = tq_peek (chan, TQ_PRIO_0_HI);
	  if (pp != NULL) {
	    prio = TQ_PRIO_0_HI;
	  }
	  else {
	    pp = tq_peek (chan, TQ_PRIO_1_LO);
	  }

	  if (pp != NULL) {

	    switch (frame_flavor(pp)) {

	      case FLAVOR_SPEECH:
	      case FLAVOR_MORSE:
	      case FLAVOR_DTMF:
	      case FLAVOR_APRS_DIGI:
	      default:
		done = 1;		// not eligible for bundling.
	        break;

	      case FLAVOR_APRS_NEW:
	      case FLAVOR_OTHER:

	        pp = tq_remove (chan, prio);
#if DEBUG
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("xmit_thread: t=%.3f, tq_remove(chan=%d, prio=%d) returned %p\n", dtime_now()-time_ptt, chan, prio, pp);
#endif

	        nb = send_one_frame (chan, prio, pp);

	        num_bits += nb;
	        if (nb > 0) numframe++;
#if DEBUG
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("xmit_thread: t=%.3f, nb=%d, num_bits=%d, numframe=%d\n", dtime_now()-time_ptt, nb, num_bits, numframe);
#endif
	        ax25_delete (pp);

	        break;
	    }
	  }
	  else {
	    done = 1;
	  }
	}

/* 
 * Need TXTAIL because we don't know exactly when the sound is done.
 */

	post_flags = MS_TO_BITS(xmit_txtail[chan] * 10, chan) / 8;
	nb = layer2_preamble_postamble (chan, post_flags, 1, save_audio_config_p);
	num_bits += nb;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: t=%.3f, txtail=%d [*10], post_flags=%d, nb=%d, num_bits=%d\n", dtime_now()-time_ptt, xmit_txtail[chan], post_flags, nb, num_bits);
#endif


/* 
 * While demodulating is CPU intensive, generating the tones is not.
 * Example: on the RPi model 1, with 50% of the CPU taken with two receive
 * channels, a transmission of more than a second is generated in
 * about 40 mS of elapsed real time.
 */

	audio_wait(ACHAN2ADEV(chan));		

/* 
 * Ideally we should be here just about the time when the audio is ending.
 * However, the innards of "audio_wait" are not satisfactory in all cases.
 *
 * Calculate how long the frame(s) should take in milliseconds.
 */

	duration = BITS_TO_MS(num_bits, chan);

/*
 * See how long it has been since PTT was turned on.
 * Wait additional time if necessary.
 */

	time_now = dtime_now();
	already = (int) ((time_now - time_ptt) * 1000.);
	wait_more = duration - already;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: t=%.3f, xmit duration=%d, %d already elapsed since PTT, wait %d more\n", dtime_now()-time_ptt, duration, already, wait_more );
#endif

	if (wait_more > 0) {
	  SLEEP_MS(wait_more);
	}
	else if (wait_more < -100) {

	  /* If we run over by 10 mSec or so, it's nothing to worry about. */
	  /* However, if PTT is still on about 1/10 sec after audio */
	  /* should be done, something is wrong. */

	  /* Looks like a bug with the RPi audio system. Never an issue with Ubuntu.  */
	  /* This runs over randomly sometimes. TODO:  investigate more fully sometime. */
#ifndef __arm__
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Transmit timing error: PTT is on %d mSec too long.\n", -wait_more);
#endif
	}

/*
 * Turn off transmitter.
 */
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	time_now = dtime_now();
	dw_printf ("xmit_thread: t=%.3f, Turn off PTT now. Actual time on was %d mS, vs. %d desired\n", dtime_now()-time_ptt, (int) ((time_now - time_ptt) * 1000.), duration);
#endif
		
	ptt_set (OCTYPE_PTT, chan, 0);

} /* end xmit_ax25_frames */



/*-------------------------------------------------------------------
 *
 * Name:        send_one_frame
 *
 * Purpose:     Send one AX.25 frame.
 *
 * Inputs:	c	- Channel number.
 *
 *		p	- Priority.
 *
 *		pp	- Packet object pointer.  Caller will delete it.
 *
 * Returns:	Number of bits transmitted.
 *
 * Description:	Caller is responsible for activiating PTT, TXDELAY,
 *		deciding how many frames can be in one transmission,
 *		deactivating PTT.
 *
 *--------------------------------------------------------------------*/


static int send_one_frame (int c, int p, packet_t pp)
{
	char stemp[1024];	/* max size needed? */
	int info_len;
	unsigned char *pinfo;
	int nb;


	if (ax25_is_null_frame(pp)) {

	  // Issue 132 - We could end up in a situation where:
	  // Transmitter is already on.
	  // Application wants to send a frame.
	  // dl_seize_request turns into this null frame.
	  // It was being ignored here so the data got stuck in the queue.
	  // I think the solution is to send back a seize confirm here.
	  // It shouldn't hurt if we send it redundantly.
	  // Added for 1.5 beta test 4.

	  dlq_seize_confirm (c);	// C4.2.  "This primitive indicates, to the Data-link State
					// machine, that the transmission opportunity has arrived."

	  SLEEP_MS (10);		// Give data link state machine a chance to
					// to stuff more frames into the transmit queue,
					// in response to dlq_seize_confirm, so
					// we don't run off the end too soon.

	  return(0);
	}

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

	ax25_format_addrs (pp, stemp);
	info_len = ax25_get_info (pp, &pinfo);
	text_color_set(DW_COLOR_XMIT);
#if 0						// FIXME - enable this?
	dw_printf ("[%d%c%s%s] ", c,
			p==TQ_PRIO_0_HI ? 'H' : 'L',
			save_audio_config_p->achan[c].fx25_strength ? "F" : "",
			ts);
#else
	dw_printf ("[%d%c%s] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L', ts);
#endif
	dw_printf ("%s", stemp);			/* stations followed by : */

/* Demystify non-APRS.  Use same format for received frames in direwolf.c. */

	if ( ! ax25_is_aprs(pp)) {
	  ax25_frame_type_t ftype;
	  cmdres_t cr;
	  char desc[80];
	  int pf;
	  int nr;
	  int ns;

	  ftype = ax25_frame_type (pp, &cr, desc, &pf, &nr, &ns);

	  dw_printf ("(%s)", desc);

	  if (ftype == frame_type_U_XID) {
	    struct xid_param_s param;
	    char info2text[150];

	    xid_parse (pinfo, info_len, &param, info2text, sizeof(info2text));
	    dw_printf (" %s\n", info2text);
	  }
	  else {
	    ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	    dw_printf ("\n");
	  }
	}
	else {
	  ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	  dw_printf ("\n");
	}

	(void)ax25_check_addresses (pp);

/* Optional hex dump of packet. */

	if (g_debug_xmit_packet) {

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("------\n");
	  ax25_hex_dump (pp);
	  dw_printf ("------\n");
	}


/*
 * Transmit the frame.
 */
	int send_invalid_fcs2 = 0;

	if (save_audio_config_p->xmit_error_rate != 0) {
	  float r = (float)(rand()) / (float)RAND_MAX;		// Random, 0.0 to 1.0

	  if (save_audio_config_p->xmit_error_rate / 100.0 > r) {
	    send_invalid_fcs2 = 1;
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Intentionally sending invalid CRC for frame above.  Xmit Error rate = %d per cent.\n", save_audio_config_p->xmit_error_rate);
	  }
	}

	nb = layer2_send_frame (c, pp, send_invalid_fcs2, save_audio_config_p);

// Optionally send confirmation to AGW client app if monitoring enabled.

	server_send_monitored (c, pp, 1);

	return (nb);

} /* end send_one_frame */




/*-------------------------------------------------------------------
 *
 * Name:        xmit_speech
 *
 * Purpose:     After we have a clear channel, and possibly waited a random time,
 *		we transmit information part of frame as speech.
 *
 * Inputs:	c	- Channel number.
 *	
 *		pp	- Packet object pointer.
 *			  It will be deleted so caller should not try
 *			  to reference it after this.	
 *
 * Description:	Turn on transmitter.
 *		Invoke the text-to-speech script.
 *		Turn off transmitter.
 *
 *--------------------------------------------------------------------*/


static void xmit_speech (int c, packet_t pp)
{
	int info_len;
	unsigned char *pinfo;

/*
 * Print spoken packet.  Prefix by channel.
 */

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

	info_len = ax25_get_info (pp, &pinfo);
	(void)info_len;

	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d.speech%s] \"%s\"\n", c, ts, pinfo);


	if (strlen(save_audio_config_p->tts_script) == 0) {
          text_color_set(DW_COLOR_ERROR);
          dw_printf ("Text-to-speech script has not been configured.\n");
	  ax25_delete (pp);
	  return;
	}

/* 
 * Turn on transmitter.
 */
	ptt_set (OCTYPE_PTT, c, 1);

/*
 * Invoke the speech-to-text script.
 */	

	xmit_speak_it (save_audio_config_p->tts_script, c, (char*)pinfo);

/*
 * Turn off transmitter.
 */
		
	ptt_set (OCTYPE_PTT, c, 0);
	ax25_delete (pp);

} /* end xmit_speech */


/* Broken out into separate function so configuration can validate it. */
/* Returns 0 for success. */

int xmit_speak_it (char *script, int c, char *orig_msg)
{
	int err;
	char msg[2000];
	char cmd[sizeof(msg) + 16];
	char *p;

/* Remove any quotes because it will mess up command line argument parsing. */

	strlcpy (msg, orig_msg, sizeof(msg));

	for (p=msg; *p!='\0'; p++) {
	  if (*p == '"') *p = ' ';
	}

#if __WIN32__
	snprintf (cmd, sizeof(cmd), "%s %d \"%s\" >nul", script, c, msg);
#else
	snprintf (cmd, sizeof(cmd), "%s %d \"%s\"", script, c, msg);
#endif

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("cmd=%s\n", cmd);

	err = system (cmd);

	if (err != 0) {
	  char cwd[1000];
	  char path[3000];
	  char *ignore;

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Failed to run text-to-speech script, %s\n", script);

	  ignore = getcwd (cwd, sizeof(cwd));
	  (void)ignore;
	  strlcpy (path, getenv("PATH"), sizeof(path));

	  dw_printf ("CWD = %s\n", cwd);
	  dw_printf ("PATH = %s\n", path);
	
	}
	return (err);
}



/*-------------------------------------------------------------------
 *
 * Name:        xmit_morse
 *
 * Purpose:     After we have a clear channel, and possibly waited a random time,
 *		we transmit information part of frame as Morse code.
 *
 * Inputs:	c	- Channel number.
 *	
 *		pp	- Packet object pointer.
 *			  It will be deleted so caller should not try
 *			  to reference it after this.	
 *
 *		wpm	- Speed in words per minute.
 *
 * Description:	Turn on transmitter.
 *		Send text as Morse code.
 *		A small amount of quiet padding will appear at start and end.
 *		Turn off transmitter.
 *
 *--------------------------------------------------------------------*/


static void xmit_morse (int c, packet_t pp, int wpm)
{
	int info_len;
	unsigned char *pinfo;
	int length_ms, wait_ms;
	double start_ptt, wait_until, now;

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

	info_len = ax25_get_info (pp, &pinfo);
	(void)info_len;
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d.morse%s] \"%s\"\n", c, ts, pinfo);

	ptt_set (OCTYPE_PTT, c, 1);
	start_ptt = dtime_now();

	// make txdelay at least 300 and txtail at least 250 ms.

	length_ms = morse_send (c, (char*)pinfo, wpm, MAXX(xmit_txdelay[c] * 10, 300), MAXX(xmit_txtail[c] * 10, 250));

	// there is probably still sound queued up in the output buffers.

	wait_until = start_ptt + length_ms * 0.001;

	now = dtime_now();

	wait_ms = (int) ( ( wait_until - now ) * 1000 );
	if (wait_ms > 0) {
	  SLEEP_MS(wait_ms);
	}

	ptt_set (OCTYPE_PTT, c, 0);
	ax25_delete (pp);

} /* end xmit_morse */



/*-------------------------------------------------------------------
 *
 * Name:        xmit_dtmf
 *
 * Purpose:     After we have a clear channel, and possibly waited a random time,
 *		we transmit information part of frame as DTMF tones.
 *
 * Inputs:	c	- Channel number.
 *
 *		pp	- Packet object pointer.
 *			  It will be deleted so caller should not try
 *			  to reference it after this.
 *
 *		speed	- Button presses per second.
 *
 * Description:	Turn on transmitter.
 *		Send text as touch tones.
 *		A small amount of quiet padding will appear at start and end.
 *		Turn off transmitter.
 *
 *--------------------------------------------------------------------*/


static void xmit_dtmf (int c, packet_t pp, int speed)
{
	int info_len;
	unsigned char *pinfo;
	int length_ms, wait_ms;
	double start_ptt, wait_until, now;

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

	info_len = ax25_get_info (pp, &pinfo);
	(void)info_len;
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d.dtmf%s] \"%s\"\n", c, ts, pinfo);

	ptt_set (OCTYPE_PTT, c, 1);
	start_ptt = dtime_now();

	// make txdelay at least 300 and txtail at least 250 ms.

	length_ms = dtmf_send (c, (char*)pinfo, speed, MAXX(xmit_txdelay[c] * 10, 300), MAXX(xmit_txtail[c] * 10, 250));

	// there is probably still sound queued up in the output buffers.

	wait_until = start_ptt + length_ms * 0.001;

	now = dtime_now();

	wait_ms = (int) ( ( wait_until - now ) * 1000 );
	if (wait_ms > 0) {
	  SLEEP_MS(wait_ms);
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Oops.  CPU too slow to keep up with DTMF generation.\n");
	}

	ptt_set (OCTYPE_PTT, c, 0);
	ax25_delete (pp);

} /* end xmit_dtmf */



/*-------------------------------------------------------------------
 *
 * Name:        wait_for_clear_channel
 *
 * Purpose:     Wait for the radio channel to be clear and any
 *		additional time for collision avoidance.
 *
 * Inputs:	chan	-	Radio channel number.
 *
 *		slottime - 	Amount of time to wait for each iteration
 *				of the waiting algorithm.  10 mSec units.
 *
 *		persist -	Probability of transmitting.
 *
 *		fulldup -	Full duplex.  Just start sending immediately.
 *
 * Returns:	1 for OK.  0 for timeout.
 *
 * Description:	New in version 1.2: also obtain a lock on audio out device.
 *
 *		New in version 1.5: full duplex.
 *		Just start transmitting rather than waiting for clear channel.
 *		This would only be appropriate when transmit and receive are
 *		using different radio frequencies.  e.g.  VHF up, UHF down satellite.
 *
 * Transmit delay algorithm:
 *
 *		Wait for channel to be clear.
 *		If anything in high priority queue, bail out of the following.
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
/* That's a long time to wait for APRS. */
/* Might need to revisit some day for connected mode file transfers. */

#define WAIT_TIMEOUT_MS (60 * 1000)	
#define WAIT_CHECK_EVERY_MS 10

static int wait_for_clear_channel (int chan, int slottime, int persist, int fulldup)
{
	int n = 0;

/*
 * For dull duplex we skip the channel busy check and random wait.
 * We still need to wait if operating in stereo and the other audio
 * half is busy.
 */
	if ( ! fulldup) {

start_over_again:

	while (hdlc_rec_data_detect_any(chan)) {
	  SLEEP_MS(WAIT_CHECK_EVERY_MS);
	  n++;
	  if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS)) {
	    return 0;
	  }
	}

//TODO:  rethink dwait.

/*
 * Added in version 1.2 - for transceivers that can't
 * turn around fast enough when using squelch and VOX.
 */

	if (save_audio_config_p->achan[chan].dwait > 0) {
	  SLEEP_MS (save_audio_config_p->achan[chan].dwait * 10);
	}

	if (hdlc_rec_data_detect_any(chan)) {
	  goto start_over_again;
	}

/*
 * Wait random time.
 * Proceed to transmit sooner if anything shows up in high priority queue.
 */
	while (tq_peek(chan, TQ_PRIO_0_HI) == NULL) {
	  int r;

	  SLEEP_MS (slottime * 10);

	  if (hdlc_rec_data_detect_any(chan)) {
	    goto start_over_again;
	  }

	  r = rand() & 0xff;
	  if (r <= persist) {
	    break;
 	  }	
	}
	}

/*
 * This is to prevent two channels from transmitting at the same time
 * thru a stereo audio device.
 * We are not clever enough to combine two audio streams.
 * They must go out one at a time.
 * Documentation recommends using separate audio device for each channel rather than stereo.
 * That also allows better use of multiple cores for receiving.
 */

// TODO: review this.

	while ( ! dw_mutex_try_lock(&(audio_out_dev_mutex[ACHAN2ADEV(chan)]))) {
	  SLEEP_MS(WAIT_CHECK_EVERY_MS);
	  n++;
	  if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS)) {
	    return 0;
	  }
	}

	return 1;

} /* end wait_for_clear_channel */


/* end xmit.c */




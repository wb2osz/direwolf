
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015  John Langner, WB2OSZ
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
#include <errno.h>

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

static int xmit_txtail[MAX_CHANS];	/* Amount of time to keep transmitting after we */
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


/*
 * When an audio device is in stereo mode, we can have two 
 * different channels that want to transmit at the same time.
 * We are not clever enough to multiplex them so use this
 * so only one is activte at the same time.
 */
static dw_mutex_t audio_out_dev_mutex[MAX_ADEVS];



static int wait_for_clear_channel (int channel, int nowait, int slotttime, int persist);
static void xmit_ax25_frames (int c, int p, packet_t pp);
static void xmit_speech (int c, packet_t pp);
static void xmit_morse (int c, packet_t pp, int wpm);


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
	HANDLE xmit_th[MAX_CHANS];
#else
	//pthread_attr_t attr;
	//struct sched_param sp;
	pthread_t xmit_tid[MAX_CHANS];
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

	for (j=0; j<MAX_CHANS; j++) {
	  xmit_bits_per_sec[j] = p_modem->achan[j].baud;
	  xmit_slottime[j] = p_modem->achan[j].slottime;
	  xmit_persist[j] = p_modem->achan[j].persist;
	  xmit_txdelay[j] = p_modem->achan[j].txdelay;
	  xmit_txtail[j] = p_modem->achan[j].txtail;
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


	for (j=0; j<MAX_CHANS; j++) {

	  if (p_modem->achan[j].valid) {

#if __WIN32__
	    xmit_th[j] = (HANDLE)_beginthreadex (NULL, 0, xmit_thread, (void*)(long)j, 0, NULL);
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
	
	    e = pthread_create (&(xmit_tid[j]), &attr, xmit_thread, (void *)(long)j);
	    pthread_attr_destroy (&attr);
#else
	    e = pthread_create (&(xmit_tid[j]), NULL, xmit_thread, (void *)(long)j);
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
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall xmit_thread (void *arg)
#else
static void * xmit_thread (void *arg)
#endif
{
	int c = (int)(long)arg; // channel number.
	packet_t pp;
	int p;
	int ok;

/*
 * These are for timing of a transmission.
 * All are in usual unix time (seconds since 1/1/1970) but higher resolution
 */


	while (1) {

	  tq_wait_while_empty (c);
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("xmit_thread, channel %d: woke up\n", c);
#endif
	  
	  for (p=0; p<TQ_NUM_PRIO; p++) {

	      pp = tq_remove (c, p);
#if DEBUG
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("xmit_thread: tq_remove(chan=%d, prio=%d) returned %p\n", c, p, pp);
#endif
	      if (pp != NULL) {

/* 
 * Wait for the channel to be clear.
 * For the high priority queue, begin transmitting immediately.
 * For the low priority queue, wait a random amount of time, in hopes
 * of minimizing collisions.
 */
	        ok = wait_for_clear_channel (c, (p==TQ_PRIO_0_HI), xmit_slottime[c], xmit_persist[c]);

	        if (ok) {
/*
 * Channel is clear and we have lock on output device. 
 *
 * If destination is "SPEECH" send info part to speech synthesizer.
 * If destination is "MORSE" send as morse code.
 */
	          char dest[AX25_MAX_ADDR_LEN];
		  int ssid = 0;


	          if (ax25_is_aprs (pp)) { 

		    ax25_get_addr_no_ssid(pp, AX25_DESTINATION, dest);
		    ssid = ax25_get_ssid(pp, AX25_DESTINATION);
	 	  }
	 	  else {
		    strlcpy (dest, "", sizeof(dest));
	          }

		  if (strcmp(dest, "SPEECH") == 0) {
	            xmit_speech (c, pp);
	          }
		  else if (strcmp(dest, "MORSE") == 0) {

		    int wpm = ssid * 2;
		    if (wpm == 0) wpm = MORSE_DEFAULT_WPM;

		    // This is a bit of a hack so we don't respond too quickly for APRStt.
		    // It will be sent in high priority queue while a beacon wouldn't.  
		    // Add a little delay so user has time release PTT after sending #.
		    // This and default txdelay would give us a second.

		    if (p == TQ_PRIO_0_HI) {
	              //text_color_set(DW_COLOR_DEBUG);
		      //dw_printf ("APRStt morse xmit delay hack...\n");
		      SLEEP_MS (700);
		    }

	            xmit_morse (c, pp, wpm);
	          }
	          else {
	            xmit_ax25_frames (c, p, pp);
		  }

	          dw_mutex_unlock (&(audio_out_dev_mutex[ACHAN2ADEV(c)]));
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
	          dw_printf ("[%d%c] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L');

	          dw_printf ("%s", stemp);			/* stations followed by : */
	          ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	          dw_printf ("\n");
		  ax25_delete (pp);

		} /* wait for clear channel. */
	    } /* for high priority then low priority */
	  }
	}

	return 0;	/* unreachable but quiet the warning. */

} /* end xmit_thread */



/*-------------------------------------------------------------------
 *
 * Name:        xmit_ax25_frames
 *
 * Purpose:     After we have a clear channel, and possibly waited a random time,
 *		we transmit one or more frames.
 *
 * Inputs:	c	- Channel number.
 *	
 *		p	- Priority of the queue.
 *
 *		pp	- Packet object pointer.
 *			  It will be deleted so caller should not try
 *			  to reference it after this.	
 *
 * Description:	Turn on transmitter.
 *		Send flags for TXDELAY time.
 *		Send the first packet, given by pp.
 *		Possibly send more packets from the same queue.
 *		Send flags for TXTAIL time.
 *		Turn off transmitter.
 *
 *
 * How many frames in one transmission?
 *
 *		Should we send multiple frames in one transmission if we 
 *		have more than one sitting in the queue?  At first I was thinking
 *		this would help reduce channel congestion.  I don't recall seeing
 *		anything in the specifications allowing or disallowing multiple
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
 *		Rather than having a MAXFRAME configuration file item,
 *		we try setting the maximum number automatically.
 *		1 for digipeated frames, 7 for others.
 *
 *--------------------------------------------------------------------*/


static void xmit_ax25_frames (int c, int p, packet_t pp)
{

  	unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
    	int flen;
	char stemp[1024];	/* max size needed? */
	int info_len;
	unsigned char *pinfo;
	int pre_flags, post_flags;
	int num_bits;		/* Total number of bits in transmission */
				/* including all flags and bit stuffing. */
	int duration;		/* Transmission time in milliseconds. */
	int already;
	int wait_more;

	int maxframe;		/* Maximum number of frames for one transmission. */
	int numframe;		/* Number of frames sent during this transmission. */

/*
 * These are for timing of a transmission.
 * All are in usual unix time (seconds since 1/1/1970) but higher resolution
 */
	double time_ptt;	/* Time when PTT is turned on. */
	double time_now;	/* Current time. */


	int nb;

	maxframe = (p == TQ_PRIO_0_HI) ? 1 : 7;


/*
 * Print trasmitted packet.  Prefix by channel and priority.
 * Do this before we get into the time critical part.
 */
	ax25_format_addrs (pp, stemp);
	info_len = ax25_get_info (pp, &pinfo);
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d%c] ", c, p==TQ_PRIO_0_HI ? 'H' : 'L');
	dw_printf ("%s", stemp);			/* stations followed by : */
	ax25_safe_print ((char *)pinfo, info_len, ! ax25_is_aprs(pp));
	dw_printf ("\n");
	(void)ax25_check_addresses (pp);

/* Optional hex dump of packet. */

	if (g_debug_xmit_packet) {

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("------\n");
	  ax25_hex_dump (pp);
    	  dw_printf ("------\n");
	}

/* 
 * Turn on transmitter.
 * Start sending leading flag bytes.
 */
	time_ptt = dtime_now ();

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: Turn on PTT now for channel %d. speed = %d\n", c, xmit_bits_per_sec[c]);
#endif
	ptt_set (OCTYPE_PTT, c, 1);

	pre_flags = MS_TO_BITS(xmit_txdelay[c] * 10, c) / 8;
	num_bits =  hdlc_send_flags (c, pre_flags, 0);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: txdelay=%d [*10], pre_flags=%d, num_bits=%d\n", xmit_txdelay[c], pre_flags, num_bits);
#endif


/*
 * Transmit the frame.
 */	
	flen = ax25_pack (pp, fbuf);
	assert (flen >= 1 && flen <= sizeof(fbuf));
	nb = hdlc_send_frame (c, fbuf, flen);
	num_bits += nb;
	numframe = 1;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: flen=%d, nb=%d, num_bits=%d, numframe=%d\n", flen, nb, num_bits, numframe);
#endif
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
	 (void)ax25_check_addresses (pp);

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
	  nb = hdlc_send_frame (c, fbuf, flen);
	  num_bits += nb;
	  numframe++;
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("xmit_thread: flen=%d, nb=%d, num_bits=%d, numframe=%d\n", flen, nb, num_bits, numframe);
#endif
	  ax25_delete (pp);
	}

/* 
 * Need TXTAIL because we don't know exactly when the sound is done.
 */

	post_flags = MS_TO_BITS(xmit_txtail[c] * 10, c) / 8;
	nb = hdlc_send_flags (c, post_flags, 1);
	num_bits += nb;
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: txtail=%d [*10], post_flags=%d, nb=%d, num_bits=%d\n", xmit_txtail[c], post_flags, nb, num_bits);
#endif


/* 
 * While demodulating is CPU intensive, generating the tones is not.
 * Example: on the RPi, with 50% of the CPU taken with two receive
 * channels, a transmission of more than a second is generated in
 * about 40 mS of elapsed real time.
 */

	audio_wait(ACHAN2ADEV(c));		

/* 
 * Ideally we should be here just about the time when the audio is ending.
 * However, the innards of "audio_wait" are not satisfactory in all cases.
 *
 * Calculate how long the frame(s) should take in milliseconds.
 */

	duration = BITS_TO_MS(num_bits, c);

/*
 * See how long it has been since PTT was turned on.
 * Wait additional time if necessary.
 */

	time_now = dtime_now();
	already = (int) ((time_now - time_ptt) * 1000.);
	wait_more = duration - already;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("xmit_thread: xmit duration=%d, %d already elapsed since PTT, wait %d more\n", duration, already, wait_more );
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
	dw_printf ("xmit_thread: Turn off PTT now. Actual time on was %d mS, vs. %d desired\n", (int) ((time_now - time_ptt) * 1000.), duration);
#endif
		
	ptt_set (OCTYPE_PTT, c, 0);

} /* end xmit_ax25_frames */



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

	info_len = ax25_get_info (pp, &pinfo);
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d.speech] \"%s\"\n", c, pinfo);


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
	char cmd[2000];	
	char *p;
	char msg[2000];

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
 *		Turn off transmitter.
 *
 *--------------------------------------------------------------------*/


static void xmit_morse (int c, packet_t pp, int wpm)
{


	int info_len;
	unsigned char *pinfo;


	info_len = ax25_get_info (pp, &pinfo);
	text_color_set(DW_COLOR_XMIT);
	dw_printf ("[%d.morse] \"%s\"\n", c, pinfo);

	ptt_set (OCTYPE_PTT, c, 1);

	morse_send (c, (char*)pinfo, wpm, xmit_txdelay[c] * 10, xmit_txtail[c] * 10);

	ptt_set (OCTYPE_PTT, c, 0);
	ax25_delete (pp);

} /* end xmit_morse */


/*-------------------------------------------------------------------
 *
 * Name:        wait_for_clear_channel
 *
 * Purpose:     Wait for the radio channel to be clear and any
 *		additional time for collision avoidance.
 *
 *
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
 *		New in version 1.2: also obtain a lock on audio out device.
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
/* That's a long time to wait for APRS. */
/* Might need to revisit some day for connected mode file transfers. */

#define WAIT_TIMEOUT_MS (60 * 1000)	
#define WAIT_CHECK_EVERY_MS 10

static int wait_for_clear_channel (int channel, int nowait, int slottime, int persist)
{
	int r;
	int n;


	n = 0;

start_over_again:

	while (hdlc_rec_data_detect_any(channel)) {
	  SLEEP_MS(WAIT_CHECK_EVERY_MS);
	  n++;
	  if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS)) {
	    return 0;
	  }
	}

//TODO1.2:  rethink dwait.

/*
 * Added in version 1.2 - for transceivers that can't
 * turn around fast enough when using squelch and VOX.
 */

	if (save_audio_config_p->achan[channel].dwait > 0) {
	  SLEEP_MS (save_audio_config_p->achan[channel].dwait * 10);
	}

	if (hdlc_rec_data_detect_any(channel)) {
	  goto start_over_again;
	}

	if ( ! nowait) {

	  while (1) {

	    SLEEP_MS (slottime * 10);

	    if (hdlc_rec_data_detect_any(channel)) {
	      goto start_over_again;
	    }

	    r = rand() & 0xff;
	    if (r <= persist) {
	      break;
 	    }	
	  }
	}

// TODO1.2

	while ( ! dw_mutex_try_lock(&(audio_out_dev_mutex[ACHAN2ADEV(channel)]))) {
	  SLEEP_MS(WAIT_CHECK_EVERY_MS);
	  n++;
	  if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS)) {
	    return 0;
	  }
	}

	return 1;

} /* end wait_for_clear_channel */


/* end xmit.c */




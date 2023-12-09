

// 
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Module:      audio.c
 *
 * Purpose:   	Interface to audio device commonly called a "sound card" for
 *		historical reasons.	
 *
 *		This version is for Linux and Cygwin.	
 *
 *		Two different types of sound interfaces are supported:
 *
 *		* OSS - For Cygwin or Linux versions with /dev/dsp.
 *
 *		* ALSA - For Linux versions without /dev/dsp.
 *			In this case, define preprocessor symbol USE_ALSA.
 *
 * References:	Some tips on on using Linux sound devices.
 *
 *		http://www.oreilly.de/catalog/multilinux/excerpt/ch14-05.htm
 *		http://cygwin.com/ml/cygwin-patches/2004-q1/msg00116/devdsp.c
 *		http://manuals.opensound.com/developer/fulldup.c.html
 *
 *		"Introduction to Sound Programming with ALSA"
 *		http://www.linuxjournal.com/article/6735?page=0,1
 *
 *		http://www.alsa-project.org/main/index.php/Asoundrc
 *
 * Credits:	Release 1.0: Fabrice FAURE contributed code for the SDR UDP interface.
 *
 *		Discussion here:  http://gqrx.dk/doc/streaming-audio-over-udp
 *
 *		Release 1.1:  Gabor Berczi provided fixes for the OSS code
 *		which had fallen into decay.
 *		
 * Major Revisions: 	
 *
 *		1.2 - Add ability to use more than one audio device. 
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>


#if USE_ALSA
#include <alsa/asoundlib.h>
#elif USE_SNDIO
#include <sndio.h>
#include <poll.h>
#else
#include <sys/soundcard.h>
#endif


#include "audio.h"
#include "audio_stats.h"
#include "textcolor.h"
#include "dtime_now.h"
#include "demod.h"		/* for alevel_t & demod_get_audio_level() */


/* Audio configuration. */

static struct audio_s          *save_audio_config_p;


/* Current state for each of the audio devices. */

static struct adev_s {

#if USE_ALSA
	snd_pcm_t *audio_in_handle;
	snd_pcm_t *audio_out_handle;

	int bytes_per_frame;		/* number of bytes for a sample from all channels. */
					/* e.g. 4 for stereo 16 bit. */
#elif USE_SNDIO
	struct sio_hdl *sndio_in_handle;
	struct sio_hdl *sndio_out_handle;

#else
	int oss_audio_device_fd;	/* Single device, both directions. */

#endif

	int inbuf_size_in_bytes;	/* number of bytes allocated */
	unsigned char *inbuf_ptr;
	int inbuf_len;			/* number byte of actual data available. */
	int inbuf_next;			/* index of next to remove. */

	int outbuf_size_in_bytes;
	unsigned char *outbuf_ptr;
	int outbuf_len;

	enum audio_in_type_e g_audio_in_type;

	int udp_sock;			/* UDP socket for receiving data */

} adev[MAX_ADEVS];


// Originally 40.  Version 1.2, try 10 for lower latency.

#define ONE_BUF_TIME 10


#if USE_ALSA
static int set_alsa_params (int a, snd_pcm_t *handle, struct audio_s *pa, char *name, char *dir);
//static void alsa_select_device (char *pick_dev, int direction, char *result);
#elif USE_SNDIO
static int set_sndio_params (int a, struct sio_hdl *handle, struct audio_s *pa, char *devname, char *inout);
static int poll_sndio (struct sio_hdl *hdl, int events);
#else
static int set_oss_params (int a, int fd, struct audio_s *pa);
#endif


#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)

static int calcbufsize(int rate, int chans, int bits)
{
	int size1 = (rate * chans * bits  / 8 * ONE_BUF_TIME) / 1000;
	int size2 = roundup1k(size1);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_open: calcbufsize (rate=%d, chans=%d, bits=%d) calc size=%d, round up to %d\n",
		rate, chans, bits, size1, size2);
#endif
	return (size2);
}


/*------------------------------------------------------------------
 *
 * Name:        audio_open
 *
 * Purpose:     Open the digital audio device.
 *		For "OSS", the device name is typically "/dev/dsp".
 *		For "ALSA", it's a lot more complicated.  See User Guide.
 *
 *		New in version 1.0, we recognize "udp:" optionally
 *		followed by a port number.
 *
 * Inputs:      pa		- Address of structure of type audio_s.
 *				
 *				Using a structure, rather than separate arguments
 *				seemed to make sense because we often pass around
 *				the same set of parameters various places.
 *
 *				The fields that we care about are:
 *					num_channels
 *					samples_per_sec
 *					bits_per_sample
 *				If zero, reasonable defaults will be provided.
 *
 *				The device names are in adevice_in and adevice_out.
 *				 - For "OSS", the device name is typically "/dev/dsp".
 *				 - For "ALSA", the device names are hw:c,d
 *				   where c is the "card" (for historical purposes)
 *				   and d is the "device" within the "card."
 *
 *
 * Outputs:	pa		- The ACTUAL values are returned here.
 *
 *				These might not be exactly the same as what was requested.
 *					
 *				Example: ask for stereo, 16 bits, 22050 per second.
 *				An ordinary desktop/laptop PC should be able to handle this.
 *				However, some other sort of smaller device might be
 *				more restrictive in its capabilities.
 *				It might say, the best I can do is mono, 8 bit, 8000/sec.
 *
 *				The software modem must use this ACTUAL information
 *				that the device is supplying, that could be different
 *				than what the user specified.
 *              
 * Returns:     0 for success, -1 for failure.
 *	
 *
 *----------------------------------------------------------------*/

int audio_open (struct audio_s *pa)
{
#if !USE_SNDIO
	int err;
#endif
	int chan;
	int a;
	char audio_in_name[30];
	char audio_out_name[30];


	save_audio_config_p = pa;

	memset (adev, 0, sizeof(adev));

	for (a=0; a<MAX_ADEVS; a++) {
#if USE_ALSA
	  adev[a].audio_in_handle = adev[a].audio_out_handle = NULL;
#elif USE_SNDIO
	  adev[a].sndio_in_handle = adev[a].sndio_out_handle = NULL;
#else
	  adev[a].oss_audio_device_fd = -1;
#endif
	  adev[a].udp_sock = -1;
	}


/*
 * Fill in defaults for any missing values.
 */

	for (a=0; a<MAX_ADEVS; a++) {

	  if (pa->adev[a].num_channels == 0)
	    pa->adev[a].num_channels = DEFAULT_NUM_CHANNELS;

	  if (pa->adev[a].samples_per_sec == 0)
	    pa->adev[a].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

	  if (pa->adev[a].bits_per_sample == 0)
	    pa->adev[a].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;

	  for (chan=0; chan<MAX_CHANS; chan++) {
	    if (pa->achan[chan].mark_freq == 0)
	      pa->achan[chan].mark_freq = DEFAULT_MARK_FREQ;

	    if (pa->achan[chan].space_freq == 0)
	      pa->achan[chan].space_freq = DEFAULT_SPACE_FREQ;

	    if (pa->achan[chan].baud == 0)
	      pa->achan[chan].baud = DEFAULT_BAUD;

	    if (pa->achan[chan].num_subchan == 0)
	      pa->achan[chan].num_subchan = 1;
	  }
	}

/*
 * Open audio device(s).
 */

    	for (a=0; a<MAX_ADEVS; a++) {
         if (pa->adev[a].defined) {

	    adev[a].inbuf_size_in_bytes = 0;
	    adev[a].inbuf_ptr = NULL;
	    adev[a].inbuf_len = 0;
	    adev[a].inbuf_next = 0;

	    adev[a].outbuf_size_in_bytes = 0;
	    adev[a].outbuf_ptr = NULL;
	    adev[a].outbuf_len = 0;

/*
 * Determine the type of audio input.
 */

	    adev[a].g_audio_in_type = AUDIO_IN_TYPE_SOUNDCARD; 	

	    if (strcasecmp(pa->adev[a].adevice_in, "stdin") == 0 || strcmp(pa->adev[a].adevice_in, "-") == 0) {
	      adev[a].g_audio_in_type = AUDIO_IN_TYPE_STDIN;
	      /* Change "-" to stdin for readability. */
	      strlcpy (pa->adev[a].adevice_in, "stdin", sizeof(pa->adev[a].adevice_in));
	    }
	    if (strncasecmp(pa->adev[a].adevice_in, "udp:", 4) == 0) {
	      adev[a].g_audio_in_type = AUDIO_IN_TYPE_SDR_UDP;
	      /* Supply default port if none specified. */
	      if (strcasecmp(pa->adev[a].adevice_in,"udp") == 0 ||
	        strcasecmp(pa->adev[a].adevice_in,"udp:") == 0) {
	        snprintf (pa->adev[a].adevice_in, sizeof(pa->adev[a].adevice_in), "udp:%d", DEFAULT_UDP_AUDIO_PORT);
	      }
	    } 

/* Let user know what is going on. */

	    /* If not specified, the device names should be "default". */

	    strlcpy (audio_in_name, pa->adev[a].adevice_in, sizeof(audio_in_name));
	    strlcpy (audio_out_name, pa->adev[a].adevice_out, sizeof(audio_out_name));

	    char ctemp[40];

	    if (pa->adev[a].num_channels == 2) {
	      snprintf (ctemp, sizeof(ctemp), " (channels %d & %d)", ADEVFIRSTCHAN(a), ADEVFIRSTCHAN(a)+1);
	    }
	    else {
	      snprintf (ctemp, sizeof(ctemp), " (channel %d)", ADEVFIRSTCHAN(a));
	    }

            text_color_set(DW_COLOR_INFO);

	    if (strcmp(audio_in_name,audio_out_name) == 0) {
              dw_printf ("Audio device for both receive and transmit: %s %s\n", audio_in_name, ctemp);
	    }
	    else {
              dw_printf ("Audio input device for receive: %s %s\n", audio_in_name, ctemp);
              dw_printf ("Audio out device for transmit: %s %s\n", audio_out_name, ctemp);
	    }

/*
 * Now attempt actual opens.
 */

/*
 * Input device.
 */

	    switch (adev[a].g_audio_in_type) {

/*
 * Soundcard - ALSA.
 */
	      case AUDIO_IN_TYPE_SOUNDCARD:
#if USE_ALSA
	        err = snd_pcm_open (&(adev[a].audio_in_handle), audio_in_name, SND_PCM_STREAM_CAPTURE, 0);
	        if (err < 0) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Could not open audio device %s for input\n%s\n", 
	  		audio_in_name, snd_strerror(err));
		  if (err == -EBUSY) {
	            dw_printf ("This means that some other application is using that device.\n");
	            dw_printf ("The solution is to identify that other application and stop it.\n");
	          }
	          return (-1);
	        }

	        adev[a].inbuf_size_in_bytes = set_alsa_params (a, adev[a].audio_in_handle, pa, audio_in_name, "input");
	    
#elif USE_SNDIO
		adev[a].sndio_in_handle = sio_open (audio_in_name, SIO_REC, 0);
		if (adev[a].sndio_in_handle == NULL) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Could not open audio device %s for input\n",
			audio_in_name);
		  return (-1);
		}

		adev[a].inbuf_size_in_bytes = set_sndio_params (a, adev[a].sndio_in_handle, pa, audio_in_name, "input");

		if (!sio_start (adev[a].sndio_in_handle)) {
		  text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Could not start audio device %s for input\n",
			audio_in_name);
		  return (-1);
		}

#else // OSS
	        adev[a].oss_audio_device_fd = open (pa->adev[a].adevice_in, O_RDWR);

	        if (adev[a].oss_audio_device_fd < 0) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("%s:\n", pa->adev[a].adevice_in);
//	          snprintf (message, sizeof(message), "Could not open audio device %s", pa->adev[a].adevice_in);
//	          perror (message);
	          return (-1);
	        }

	        adev[a].outbuf_size_in_bytes = adev[a].inbuf_size_in_bytes = set_oss_params (a, adev[a].oss_audio_device_fd, pa);

	        if (adev[a].inbuf_size_in_bytes <= 0 || adev[a].outbuf_size_in_bytes <= 0) {
	          return (-1);
	        }
#endif
	        break;
/*
 * UDP.
 */
	      case AUDIO_IN_TYPE_SDR_UDP:

	        //Create socket and bind socket
	    
	        {
	          struct sockaddr_in si_me;
	          //int slen=sizeof(si_me);
	          //int data_size = 0;

	          //Create UDP Socket
	          if ((adev[a].udp_sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Couldn't create socket, errno %d\n", errno);
	            return -1;
	          }

	          memset((char *) &si_me, 0, sizeof(si_me));
	          si_me.sin_family = AF_INET;   
	          si_me.sin_port = htons((short)atoi(audio_in_name+4));
	          si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	          //Bind to the socket
	          if (bind(adev[a].udp_sock, (const struct sockaddr *) &si_me, sizeof(si_me))==-1) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Couldn't bind socket, errno %d\n", errno);
	            return -1;
	          }
	        }
	        adev[a].inbuf_size_in_bytes = SDR_UDP_BUF_MAXLEN; 
	
	        break;

/* 
 * stdin.
 */
   	      case AUDIO_IN_TYPE_STDIN:

	        /* Do we need to adjust any properties of stdin? */

	        adev[a].inbuf_size_in_bytes = 1024; 
	    
	        break;

	      default:

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Internal error, invalid audio_in_type\n");
	        return (-1);
  	    }

/*
 * Output device.  Only "soundcard" is supported at this time. 
 */

#if USE_ALSA
	    err = snd_pcm_open (&(adev[a].audio_out_handle), audio_out_name, SND_PCM_STREAM_PLAYBACK, 0);

	    if (err < 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not open audio device %s for output\n%s\n", 
			audio_out_name, snd_strerror(err));
	      if (err == -EBUSY) {
	        dw_printf ("This means that some other application is using that device.\n");
	        dw_printf ("The solution is to identify that other application and stop it.\n");
	      }
	      return (-1);
	    }

	    adev[a].outbuf_size_in_bytes = set_alsa_params (a, adev[a].audio_out_handle, pa, audio_out_name, "output");

	    if (adev[a].inbuf_size_in_bytes <= 0 || adev[a].outbuf_size_in_bytes <= 0) {
	      return (-1);
	    }

#elif USE_SNDIO
	    adev[a].sndio_out_handle = sio_open (audio_out_name, SIO_PLAY, 0);
	    if (adev[a].sndio_out_handle == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not open audio device %s for output\n",
			audio_out_name);
	      return (-1);
	    }

	    adev[a].outbuf_size_in_bytes = set_sndio_params (a, adev[a].sndio_out_handle, pa, audio_out_name, "output");

	    if (adev[a].inbuf_size_in_bytes <= 0 || adev[a].outbuf_size_in_bytes <= 0) {
	      return (-1);
	    }

	    if (!sio_start (adev[a].sndio_out_handle)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not start audio device %s for output\n",
			audio_out_name);
	      return (-1);
	    }
#endif

/*
 * Finally allocate buffer for each direction.
 */
	    adev[a].inbuf_ptr = malloc(adev[a].inbuf_size_in_bytes);
	    assert (adev[a].inbuf_ptr  != NULL);
	    adev[a].inbuf_len = 0;
	    adev[a].inbuf_next = 0;

	    adev[a].outbuf_ptr = malloc(adev[a].outbuf_size_in_bytes);
	    assert (adev[a].outbuf_ptr  != NULL);
	    adev[a].outbuf_len = 0;

          } /* end of audio device defined */

        } /* end of for each audio device */
	
	return (0);

} /* end audio_open */




#if USE_ALSA

/*
 * Set parameters for sound card.
 *
 * See  ??  for details. 
 */
/* 
 * Terminology:
 *   Sample	- for one channel.		e.g. 2 bytes for 16 bit.
 *   Frame	- one sample for all channels.  e.g. 4 bytes for 16 bit stereo
 *   Period	- size of one transfer.
 */

static int set_alsa_params (int a, snd_pcm_t *handle, struct audio_s *pa, char *devname, char *inout)
{
	
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_uframes_t fpp; 			/* Frames per period. */

	unsigned int val;

	int dir;
	int err;

	int buf_size_in_bytes;			/* result, number of bytes per transfer. */


	err = snd_pcm_hw_params_malloc (&hw_params);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not alloc hw param structure.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	err = snd_pcm_hw_params_any (handle, hw_params);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not init hw param structure.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Interleaved data: L, R, L, R, ... */

	err = snd_pcm_hw_params_set_access (handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);

	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set interleaved mode.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Signed 16 bit little endian or unsigned 8 bit. */


	err = snd_pcm_hw_params_set_format (handle, hw_params,
 		pa->adev[a].bits_per_sample == 8 ? SND_PCM_FORMAT_U8 : SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set bits per sample.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Number of audio channels. */


	err = snd_pcm_hw_params_set_channels (handle, hw_params, pa->adev[a].num_channels);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set number of audio channels.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Audio sample rate. */


	val = pa->adev[a].samples_per_sec;

	dir = 0;


	err = snd_pcm_hw_params_set_rate_near (handle, hw_params, &val, &dir);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set audio sample rate.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	if (val != pa->adev[a].samples_per_sec) {

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Asked for %d samples/sec but got %d.\n",

				pa->adev[a].samples_per_sec, val);
	  dw_printf ("for %s %s.\n", devname, inout);

	  pa->adev[a].samples_per_sec = val;

	}
	
	/* Original: */
	/* Guessed around 20 reads/sec might be good. */
	/* Period too long = too much latency. */
	/* Period too short = more overhead of many small transfers. */

	/* fpp = pa->adev[a].samples_per_sec / 20; */

	/* The suggested period size was 2205 frames.  */
	/* I thought the later "...set_period_size_near" might adjust it to be */
	/* some more optimal nearby value based hardware buffer sizes but */
	/* that didn't happen.   We ended up with a buffer size of 4410 bytes. */

	/* In version 1.2, let's take a different approach. */
	/* Reduce the latency and round up to a multiple of 1 Kbyte. */
	
	/* For the typical case of 44100 sample rate, 1 channel, 16 bits, we calculate */
	/* a buffer size of 882 and round it up to 1k.  This results in 512 frames per period. */
	/* A period comes out to be about 80 periods per second or about 12.5 mSec each. */

	buf_size_in_bytes = calcbufsize(pa->adev[a].samples_per_sec, pa->adev[a].num_channels, pa->adev[a].bits_per_sample);

#if __arm__
	/* Ugly hack for RPi. */
	/* Reducing buffer size is fine for input but not so good for output. */
	
	if (*inout == 'o') {
	  buf_size_in_bytes = buf_size_in_bytes * 4;
	}
#endif

	fpp = buf_size_in_bytes / (pa->adev[a].num_channels * pa->adev[a].bits_per_sample / 8);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("suggest period size of %d frames\n", (int)fpp);
#endif
	dir = 0;
	err = snd_pcm_hw_params_set_period_size_near (handle, hw_params, &fpp, &dir);

	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set period size\n%s\n", snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	err = snd_pcm_hw_params (handle, hw_params);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set hw params\n%s\n", snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Driver might not like our suggested period size */
	/* and might have another idea. */

	err = snd_pcm_hw_params_get_period_size (hw_params, &fpp, NULL);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not get audio period size.\n%s\n", snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	snd_pcm_hw_params_free (hw_params);
	
	/* A "frame" is one sample for all channels. */

	/* The read and write use units of frames, not bytes. */

	adev[a].bytes_per_frame = snd_pcm_frames_to_bytes (handle, 1);

	assert (adev[a].bytes_per_frame == pa->adev[a].num_channels * pa->adev[a].bits_per_sample / 8);

	buf_size_in_bytes = fpp * adev[a].bytes_per_frame;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio buffer size = %d (bytes per frame) x %d (frames per period) = %d \n", adev[a].bytes_per_frame, (int)fpp, buf_size_in_bytes);
#endif

	/* Version 1.3 - after a report of this situation for Mac OSX version. */
	if (buf_size_in_bytes < 256 || buf_size_in_bytes > 32768) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio buffer has unexpected extreme size of %d bytes.\n", buf_size_in_bytes);
	  dw_printf ("Detected at %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("This might be caused by unusual audio device configuration values.\n"); 
	  buf_size_in_bytes = 2048;
	  dw_printf ("Using %d to attempt recovery.\n", buf_size_in_bytes);
	}

	return (buf_size_in_bytes);


} /* end alsa_set_params */


#elif USE_SNDIO

/*
 * Set parameters for sound card. (sndio)
 *
 * See  /usr/include/sndio.h  for details.
 */

static int set_sndio_params (int a, struct sio_hdl *handle, struct audio_s *pa, char *devname, char *inout)
{

	struct sio_par q, r;

	/* Signed 16 bit little endian or unsigned 8 bit. */
	sio_initpar (&q);
	q.bits = pa->adev[a].bits_per_sample;
	q.bps = (q.bits + 7) / 8;
	q.sig = (q.bits == 8) ? 0 : 1;
	q.le = 1; /* always little endian */
	q.msb = 0; /* LSB aligned */
	q.rchan = q.pchan = pa->adev[a].num_channels;
	q.rate = pa->adev[a].samples_per_sec;
	q.xrun = SIO_IGNORE;
	q.appbufsz = calcbufsize(pa->adev[a].samples_per_sec, pa->adev[a].num_channels, pa->adev[a].bits_per_sample);


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("suggest buffer size %d bytes for %s %s.\n",
		q.appbufsz, devname, inout);
#endif

	/* challenge new setting */
	if (!sio_setpar (handle, &q)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set hardware parameter for %s %s.\n",
		devname, inout);
	  return (-1);
	}

	/* get response */
	if (!sio_getpar (handle, &r)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not obtain current hardware setting for %s %s.\n",
		devname, inout);
	  return (-1);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio buffer size %d bytes for %s %s.\n",
		r.appbufsz, devname, inout);
#endif
	if (q.rate != r.rate) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Asked for %d samples/sec but got %d for %s %s.",
		     pa->adev[a].samples_per_sec, r.rate, devname, inout);
	  pa->adev[a].samples_per_sec = r.rate;
	}

	/* not supported */
	if (q.bits != r.bits || q.bps != r.bps || q.sig != r.sig ||
	    (q.bits > 8 && q.le != r.le) ||
	    (*inout == 'o' && q.pchan != r.pchan) ||
	    (*inout == 'i' && q.rchan != r.rchan)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unsupported format for %s %s.\n", devname, inout);
	  return (-1);
	}

	return r.appbufsz;

} /* end set_sndio_params */

static int poll_sndio (struct sio_hdl *hdl, int events)
{
	struct pollfd *pfds;
	int nfds, revents;

	nfds = sio_nfds (hdl);
	pfds = alloca (nfds * sizeof(struct pollfd));

	do {
	  nfds = sio_pollfd (hdl, pfds, events);
	  if (nfds < 1) {
	    /* no need to wait */
	    return (0);
	  }
	  if (poll (pfds, nfds, -1) < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("poll %d\n", errno);
	    return (-1);
	  }
	  revents = sio_revents (hdl, pfds);
	} while (!(revents & (events | POLLHUP)));

	/* unrecoverable error occurred */
	if (revents & POLLHUP) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("waited for %s, POLLHUP received\n", (events & POLLIN) ? "POLLIN" : "POLLOUT");
	  return (-1);
	}

	return (0);
}

#else


/*
 * Set parameters for sound card.  (OSS only)
 *
 * See  /usr/include/sys/soundcard.h  for details. 
 */

static int set_oss_params (int a, int fd, struct audio_s *pa)
{
	int err;
	int devcaps;
	int asked_for;
	char message[100];
	int ossbuf_size_in_bytes;


	err = ioctl (fd, SNDCTL_DSP_CHANNELS, &(pa->adev[a].num_channels));
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to set audio device number of channels");
 	  return (-1);
	}

        asked_for = pa->adev[a].samples_per_sec;

	err = ioctl (fd, SNDCTL_DSP_SPEED, &(pa->adev[a].samples_per_sec));
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to set audio device sample rate");
 	  return (-1);
	}

	if (pa->adev[a].samples_per_sec != asked_for) {
	  text_color_set(DW_COLOR_INFO);
          dw_printf ("Asked for %d samples/sec but actually using %d.\n",
		asked_for, pa->adev[a].samples_per_sec);
	}

	/* This is actually a bit mask but it happens that */
	/* 0x8 is unsigned 8 bit samples and */
	/* 0x10 is signed 16 bit little endian. */

	err = ioctl (fd, SNDCTL_DSP_SETFMT, &(pa->adev[a].bits_per_sample));
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to set audio device sample size");
 	  return (-1);
	}

/*
 * Determine capabilities.
 */
	err = ioctl (fd, SNDCTL_DSP_GETCAPS, &devcaps);
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to get audio device capabilities");
 	  // Is this fatal? //	return (-1);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_open(): devcaps = %08x\n", devcaps);	
	if (devcaps & DSP_CAP_DUPLEX) dw_printf ("Full duplex record/playback.\n");
	if (devcaps & DSP_CAP_BATCH) dw_printf ("Device has some kind of internal buffers which may cause delays.\n");
	if (devcaps & ~ (DSP_CAP_DUPLEX | DSP_CAP_BATCH)) dw_printf ("Others...\n");
#endif

	if (!(devcaps & DSP_CAP_DUPLEX)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio device does not support full duplex\n");
    	  // Do we care? //	return (-1);
	}

	err = ioctl (fd, SNDCTL_DSP_SETDUPLEX, NULL);
   	if (err == -1) {
	  // text_color_set(DW_COLOR_ERROR);
    	  // perror("Not able to set audio full duplex mode");
 	  // Unfortunate but not a disaster.
	}

/*
 * Get preferred block size.
 * Presumably this will provide the most efficient transfer.
 *
 * In my particular situation, this turned out to be
 *  	2816 for 11025 Hz 16 bit mono
 *	5568 for 11025 Hz 16 bit stereo
 *     11072 for 44100 Hz 16 bit mono
 *
 * This was long ago under different conditions.
 * Should study this again some day.
 *
 * Your mileage may vary.
 */
	err = ioctl (fd, SNDCTL_DSP_GETBLKSIZE, &ossbuf_size_in_bytes);
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to get audio block size");
	  ossbuf_size_in_bytes = 2048;	/* pick something reasonable */
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_open(): suggestd block size is %d\n", ossbuf_size_in_bytes);	
#endif

/*
 * That's 1/8 of a second which seems rather long if we want to
 * respond quickly.
 */

	ossbuf_size_in_bytes = calcbufsize(pa->adev[a].samples_per_sec, pa->adev[a].num_channels, pa->adev[a].bits_per_sample);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_open(): using block size of %d\n", ossbuf_size_in_bytes);	
#endif

#if 0
	/* Original - dies without good explanation. */
	assert (ossbuf_size_in_bytes >= 256 && ossbuf_size_in_bytes <= 32768);
#else
	/* Version 1.3 - after a report of this situation for Mac OSX version. */
	if (ossbuf_size_in_bytes < 256 || ossbuf_size_in_bytes > 32768) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio buffer has unexpected extreme size of %d bytes.\n", ossbuf_size_in_bytes);
	  dw_printf ("Detected at %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("This might be caused by unusual audio device configuration values.\n");
	  ossbuf_size_in_bytes = 2048;
	  dw_printf ("Using %d to attempt recovery.\n", ossbuf_size_in_bytes);
	}
#endif
	return (ossbuf_size_in_bytes);

} /* end set_oss_params */


#endif



/*------------------------------------------------------------------
 *
 * Name:        audio_get
 *
 * Purpose:     Get one byte from the audio device.
 *
 * Inputs:	a	- Our number for audio device.
 *
 * Returns:     0 - 255 for a valid sample.
 *              -1 for any type of error.
 *
 * Description:	The caller must deal with the details of mono/stereo
 *		and number of bytes per sample.
 *
 *		This will wait if no data is currently available.
 *
 *----------------------------------------------------------------*/

// Use hot attribute for all functions called for every audio sample.

__attribute__((hot))
int audio_get (int a)
{
	int n;
#if USE_ALSA
	int retries = 0;
#endif

#if STATISTICS
	/* Gather numbers for read from audio device. */

#define duration 100			/* report every 100 seconds. */
	static time_t last_time[MAX_ADEVS];
	time_t this_time[MAX_ADEVS];
	static int sample_count[MAX_ADEVS];
	static int error_count[MAX_ADEVS];
#endif

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("audio_get():\n");

#endif

	assert (adev[a].inbuf_size_in_bytes >= 100 && adev[a].inbuf_size_in_bytes <= 32768);

	  

	switch (adev[a].g_audio_in_type) {

/*
 * Soundcard - ALSA 
 */
	  case AUDIO_IN_TYPE_SOUNDCARD:


#if USE_ALSA


	    while (adev[a].inbuf_next >= adev[a].inbuf_len) {

	      assert (adev[a].audio_in_handle != NULL);
#if DEBUGx
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("audio_get(): readi asking for %d frames\n", adev[a].inbuf_size_in_bytes / adev[a].bytes_per_frame);	
#endif
	      n = snd_pcm_readi (adev[a].audio_in_handle, adev[a].inbuf_ptr, adev[a].inbuf_size_in_bytes / adev[a].bytes_per_frame);

#if DEBUGx	  
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("audio_get(): readi asked for %d and got %d frames\n",
		adev[a].inbuf_size_in_bytes / adev[a].bytes_per_frame, n);	
#endif

 
	      if (n > 0) {

	        /* Success */

	        adev[a].inbuf_len = n * adev[a].bytes_per_frame;		/* convert to number of bytes */
	        adev[a].inbuf_next = 0;

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			n, 
			save_audio_config_p->statistics_interval);

	      }
	      else if (n == 0) {

	        /* Didn't expect this, but it's not a problem. */
	        /* Wait a little while and try again. */

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Audio input got zero bytes: %s\n", snd_strerror(n));
	        SLEEP_MS(10);

	        adev[a].inbuf_len = 0;
	        adev[a].inbuf_next = 0;
	      }
	      else {
	        /* Error */
	        // TODO: Needs more study and testing. 

		// Only expected error conditions:
		//    -EBADFD	PCM is not in the right state (SND_PCM_STATE_PREPARED or SND_PCM_STATE_RUNNING)
		//    -EPIPE	an overrun occurred
		//    -ESTRPIPE	a suspend event occurred (stream is suspended and waiting for an application recovery)

		// Data overrun is displayed as "broken pipe" which seems a little misleading.
		// Add our own message which says something about CPU being too slow.

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Audio input device %d error code %d: %s\n", a, n, snd_strerror(n));

	        if (n == (-EPIPE)) {
	          dw_printf ("This is most likely caused by the CPU being too slow to keep up with the audio stream.\n");
	          dw_printf ("Use the \"top\" command, in another command window, to look at CPU usage.\n");
	          dw_printf ("This might be a temporary condition so we will attempt to recover a few times before giving up.\n");
	          dw_printf ("If using a very slow CPU, try reducing the CPU load by using -P- command\n");
	          dw_printf ("line option for 9600 bps or -D3 for slower AFSK .\n");
	        }

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			0, 
			save_audio_config_p->statistics_interval);

	        /* Try to recover a few times and eventually give up. */
	        if (++retries > 10) {
	          adev[a].inbuf_len = 0;
	          adev[a].inbuf_next = 0;
	          return (-1);
	        }

	        if (n == -EPIPE) {

	          /* EPIPE means overrun */

	          snd_pcm_recover (adev[a].audio_in_handle, n, 1);

	        } 
	        else {
	          /* Could be some temporary condition. */
	          /* Wait a little then try again. */
	          /* Sometimes I get "Resource temporarily available" */
	          /* when the Update Manager decides to run. */

	          SLEEP_MS (250);
	          snd_pcm_recover (adev[a].audio_in_handle, n, 1);
	        }
	      }
	    }


#elif USE_SNDIO

	    while (adev[a].inbuf_next >= adev[a].inbuf_len) {

	      assert (adev[a].sndio_in_handle != NULL);
	      if (poll_sndio (adev[a].sndio_in_handle, POLLIN) < 0) {
		adev[a].inbuf_len = 0;
		adev[a].inbuf_next = 0;
		return (-1);
	      }

	      n = sio_read (adev[a].sndio_in_handle, adev[a].inbuf_ptr, adev[a].inbuf_size_in_bytes);
	      adev[a].inbuf_len = n;
	      adev[a].inbuf_next = 0;

	      audio_stats (a,
			save_audio_config_p->adev[a].num_channels,
			n / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8),
			save_audio_config_p->statistics_interval);
	    }

#else	/* begin OSS */

	    /* Fixed in 1.2.  This was formerly outside of the switch */
	    /* so the OSS version did not process stdin or UDP. */

	    while (adev[a].g_audio_in_type == AUDIO_IN_TYPE_SOUNDCARD && adev[a].inbuf_next >= adev[a].inbuf_len) {
	      assert (adev[a].oss_audio_device_fd > 0);
	      n = read (adev[a].oss_audio_device_fd, adev[a].inbuf_ptr, adev[a].inbuf_size_in_bytes);
	      //text_color_set(DW_COLOR_DEBUG);
	      // dw_printf ("audio_get(): read %d returns %d\n", adev[a].inbuf_size_in_bytes, n);	
	      if (n < 0) {
	        text_color_set(DW_COLOR_ERROR);
	        perror("Can't read from audio device");
	        adev[a].inbuf_len = 0;
	        adev[a].inbuf_next = 0;

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			0, 
			save_audio_config_p->statistics_interval);

	        return (-1);
	      }
	      adev[a].inbuf_len = n;
	      adev[a].inbuf_next = 0;

	      audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			n / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);
	    }

#endif	/* USE_ALSA */


	    break;

/* 
 * UDP.
 */

	  case AUDIO_IN_TYPE_SDR_UDP:

	    while (adev[a].inbuf_next >= adev[a].inbuf_len) {
	      int res;

              assert (adev[a].udp_sock > 0);
	      res = recv(adev[a].udp_sock, adev[a].inbuf_ptr, adev[a].inbuf_size_in_bytes, 0);
	      if (res < 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't read from udp socket, res=%d", res);
	        adev[a].inbuf_len = 0;
	        adev[a].inbuf_next = 0;

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			0, 
			save_audio_config_p->statistics_interval);

	        return (-1);
	      }
	    
	      adev[a].inbuf_len = res;
	      adev[a].inbuf_next = 0;

	      audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			res / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);

	    }
	    break;

/*
 * stdin.
 */
	  case AUDIO_IN_TYPE_STDIN:

	    while (adev[a].inbuf_next >= adev[a].inbuf_len) {
	      //int ch, res,i;
	      int res;

	      res = read(STDIN_FILENO, adev[a].inbuf_ptr, (size_t)adev[a].inbuf_size_in_bytes);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("\nEnd of file on stdin.  Exiting.\n");
	        exit (0);
	      }
	    
	      audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			res / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);
	    
	      adev[a].inbuf_len = res;
	      adev[a].inbuf_next = 0;
	    }

	    break;
	}


	if (adev[a].inbuf_next < adev[a].inbuf_len)
	  n = adev[a].inbuf_ptr[adev[a].inbuf_next++];
	//No data to read, avoid reading outside buffer
	else
	  n = 0;

#if DEBUGx

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_get(): returns %d\n", n);

#endif
 

	return (n);

} /* end audio_get */


/*------------------------------------------------------------------
 *
 * Name:        audio_put
 *
 * Purpose:     Send one byte to the audio device.
 *
 * Inputs:	a
 *
 *		c	- One byte in range of 0 - 255.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 * Description:	The caller must deal with the details of mono/stereo
 *		and number of bytes per sample.
 *
 * See Also:	audio_flush
 *		audio_wait
 *
 *----------------------------------------------------------------*/

int audio_put (int a, int c)
{
	/* Should never be full at this point. */
	assert (adev[a].outbuf_len < adev[a].outbuf_size_in_bytes);

	adev[a].outbuf_ptr[adev[a].outbuf_len++] = c;

	if (adev[a].outbuf_len == adev[a].outbuf_size_in_bytes) {
	  return (audio_flush(a));
	}

	return (0);

} /* end audio_put */


/*------------------------------------------------------------------
 *
 * Name:        audio_flush
 *
 * Purpose:     Push out any partially filled output buffer.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 * See Also:	audio_flush
 *		audio_wait
 *
 *----------------------------------------------------------------*/

int audio_flush (int a)
{
#if USE_ALSA
	int k;
	unsigned char *psound;
	int retries = 10;
	snd_pcm_status_t *status;

	assert (adev[a].audio_out_handle != NULL);


/*
 * Trying to set the automatic start threshold didn't have the desired
 * effect.  After the first transmitted packet, they are saved up
 * for a few minutes and then all come out together.
 *
 * "Prepare" it if not already in the running state.
 * We stop it at the end of each transmitted packet.
 */


	snd_pcm_status_alloca(&status);

	k = snd_pcm_status (adev[a].audio_out_handle, status);
	if (k != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio output get status error.\n%s\n", snd_strerror(k));
	}

	if ((k = snd_pcm_status_get_state(status)) != SND_PCM_STATE_RUNNING) {

	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("Audio output state = %d.  Try to start.\n", k);

	  k = snd_pcm_prepare (adev[a].audio_out_handle);

	  if (k != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio output start error.\n%s\n", snd_strerror(k));
	  }
	}


	psound = adev[a].outbuf_ptr;

	while (retries-- > 0) {

	  k = snd_pcm_writei (adev[a].audio_out_handle, psound, adev[a].outbuf_len / adev[a].bytes_per_frame);	
#if DEBUGx
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("audio_flush(): snd_pcm_writei %d frames returns %d\n",
				adev[a].outbuf_len / adev[a].bytes_per_frame, k);
	  fflush (stdout);	
#endif
	  if (k == -EPIPE) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio output data underrun.\n");

	    /* No problemo.  Recover and go around again. */

	    snd_pcm_recover (adev[a].audio_out_handle, k, 1);
	  }
          else if (k == -ESTRPIPE) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf ("Driver suspended, recovering\n");
            snd_pcm_recover(adev[a].audio_out_handle, k, 1);
          }
          else if (k == -EBADFD) {
            k = snd_pcm_prepare (adev[a].audio_out_handle);
            if(k < 0) {
              dw_printf ("Error preparing after bad state: %s\n", snd_strerror(k));
            }
          }
 	  else if (k < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio write error: %s\n", snd_strerror(k));

	    /* Some other error condition. */
	    /* Try again. What do we have to lose? */

            k = snd_pcm_prepare (adev[a].audio_out_handle);
            if(k < 0) {
              dw_printf ("Error preparing after error: %s\n", snd_strerror(k));
            }
	  }
 	  else if (k != adev[a].outbuf_len / adev[a].bytes_per_frame) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio write took %d frames rather than %d.\n",
 			k, adev[a].outbuf_len / adev[a].bytes_per_frame);
	
	    /* Go around again with the rest of it. */

	    psound += k * adev[a].bytes_per_frame;
	    adev[a].outbuf_len -= k * adev[a].bytes_per_frame;
	  }
	  else {
	    /* Success! */
	    adev[a].outbuf_len = 0;
	    return (0);
	  }
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Audio write error retry count exceeded.\n");

	adev[a].outbuf_len = 0;
	return (-1);

#elif USE_SNDIO

	int k;
	unsigned char *ptr;
	int len;

	ptr = adev[a].outbuf_ptr;
	len = adev[a].outbuf_len;

	while (len > 0) {
	  assert (adev[a].sndio_out_handle != NULL);
	  if (poll_sndio (adev[a].sndio_out_handle, POLLOUT) < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Can't write to audio device");
	    adev[a].outbuf_len = 0;
	    return (-1);
	  }

	  k = sio_write (adev[a].sndio_out_handle, ptr, len);
#if DEBUGx
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("audio_flush(): write %d returns %d\n", len, k);
	  fflush (stdout);
#endif
	  ptr += k;
	  len -= k;
	}

	adev[a].outbuf_len = 0;
	return (0);

#else		/* OSS */

	int k;
	unsigned char *ptr;	
	int len;

	ptr = adev[a].outbuf_ptr;
	len = adev[a].outbuf_len;

	while (len > 0) {
	  assert (adev[a].oss_audio_device_fd > 0);
	  k = write (adev[a].oss_audio_device_fd, ptr, len);
#if DEBUGx
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("audio_flush(): write %d returns %d\n", len, k);
	  fflush (stdout);	
#endif
	  if (k < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Can't write to audio device");
	    adev[a].outbuf_len = 0;
	    return (-1);
	  }
	  if (k < len) {
	    /* presumably full but didn't block. */
	    usleep (10000);
	  }
	  ptr += k;
	  len -= k;
	}

	adev[a].outbuf_len = 0;
	return (0);
#endif

} /* end audio_flush */


/*------------------------------------------------------------------
 *
 * Name:        audio_wait
 *
 * Purpose:	Finish up audio output before turning PTT off.
 *
 * Inputs:	a		- Index for audio device (not channel!)
 *
 * Returns:     None.
 *
 * Description:	Flush out any partially filled audio output buffer.
 *		Wait until all the queued up audio out has been played.
 *		Take any other necessary actions to stop audio output.
 *
 * In an ideal world:
 * 
 *		We would like to ask the hardware when all the queued
 *		up sound has actually come out the speaker.
 *
 * In reality:
 *
 * 		This has been found to be less than reliable in practice.
 *
 *		Caller does the following:
 *
 *		(1) Make note of when PTT is turned on.
 *		(2) Calculate how long it will take to transmit the 
 *			frame including TXDELAY, frame (including 
 *			"flags", data, FCS and bit stuffing), and TXTAIL.
 *		(3) Call this function, which might or might not wait long enough.
 *		(4) Add (1) and (2) resulting in when PTT should be turned off.
 *		(5) Take difference between current time and desired PPT off time
 *			and wait for additional time if required.
 *
 *----------------------------------------------------------------*/

void audio_wait (int a)
{	

	audio_flush (a);

#if USE_ALSA

	/* For playback, this should wait for all pending frames */
	/* to be played and then stop. */

	snd_pcm_drain (adev[a].audio_out_handle);

	/*
	 * When this was first implemented, I observed:
 	 *
 	 * 	"Experimentation reveals that snd_pcm_drain doesn't
	 * 	 actually wait.  It returns immediately. 
	 * 	 However it does serve a useful purpose of stopping
	 * 	 the playback after all the queued up data is used."
 	 *
	 *
	 * Now that I take a closer look at the transmit timing, for
 	 * version 1.2, it seems that snd_pcm_drain DOES wait until all
	 * all pending frames have been played.  
	 * Either way, the caller will now compensate for it.
 	 */

#elif USE_SNDIO

	poll_sndio (adev[a].sndio_out_handle, POLLOUT);

#else

	assert (adev[a].oss_audio_device_fd > 0);

	// This caused a crash later on Cygwin.
	// Haven't tried it on other (non-Linux) Unix yet.

	// err = ioctl (adev[a].oss_audio_device_fd, SNDCTL_DSP_SYNC, NULL);

#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_wait(): after sync, status=%d\n", err);	
#endif

} /* end audio_wait */


/*------------------------------------------------------------------
 *
 * Name:        audio_close
 *
 * Purpose:     Close the audio device(s).
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 *
 *----------------------------------------------------------------*/

int audio_close (void)
{
	int err = 0;
	int a;

	for (a = 0; a < MAX_ADEVS; a++) {

#if USE_ALSA
	  if (adev[a].audio_in_handle != NULL && adev[a].audio_out_handle != NULL) {

	    audio_wait (a);

	    snd_pcm_close (adev[a].audio_in_handle);
	    snd_pcm_close (adev[a].audio_out_handle);

	    adev[a].audio_in_handle = adev[a].audio_out_handle = NULL;

#elif USE_SNDIO

	  if (adev[a].sndio_in_handle != NULL && adev[a].sndio_out_handle != NULL) {

	    audio_wait (a);

	    sio_stop (adev[a].sndio_in_handle);
	    sio_stop (adev[a].sndio_out_handle);
	    sio_close (adev[a].sndio_in_handle);
	    sio_close (adev[a].sndio_out_handle);

	    adev[a].sndio_in_handle = adev[a].sndio_out_handle = NULL;

#else

	  if  (adev[a].oss_audio_device_fd > 0) {
  
	    audio_wait (a);

	    close (adev[a].oss_audio_device_fd);

	    adev[a].oss_audio_device_fd = -1;
#endif

	    free (adev[a].inbuf_ptr);
	    free (adev[a].outbuf_ptr);

	    adev[a].inbuf_size_in_bytes = 0;
	    adev[a].inbuf_ptr = NULL;
	    adev[a].inbuf_len = 0;
	    adev[a].inbuf_next = 0;

	    adev[a].outbuf_size_in_bytes = 0;
	    adev[a].outbuf_ptr = NULL;
	    adev[a].outbuf_len = 0;
	  }
	}

	return (err);

} /* end audio_close */


/* end audio.c */


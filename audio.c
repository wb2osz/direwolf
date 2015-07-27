
// Remove next line to eliminate annoying debug messages every 100 seconds.
#define STATISTICS 1


// 
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014  John Langner, WB2OSZ
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
 * Credits:	Fabrice FAURE contributed code for the SDR UDP interface.
 *
 *		Discussion here:  http://gqrx.dk/doc/streaming-audio-over-udp
 *
 *
 * Future:	Will probably rip out the OSS code.
 *		ALSA was added to Linux kernel 10 years ago.
 *		Cygwin doesn't have it but I see no reason to support Cygwin
 *		now that we have a native Windows version.
 *
 *---------------------------------------------------------------*/


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


#if USE_ALSA
#include <alsa/asoundlib.h>
#else
#include <sys/soundcard.h>
#endif

#include "direwolf.h"
#include "audio.h"
#include "textcolor.h"


#if USE_ALSA
static snd_pcm_t *audio_in_handle = NULL;
static snd_pcm_t *audio_out_handle = NULL;

static int bytes_per_frame;	/* number of bytes for a sample from all channels. */
				/* e.g. 4 for stereo 16 bit. */

static int set_alsa_params (snd_pcm_t *handle, struct audio_s *pa, char *name, char *dir);

//static void alsa_select_device (char *pick_dev, int direction, char *result);
#else

static int oss_audio_device_fd = -1;	/* Single device, both directions. */

#endif

static int inbuf_size_in_bytes = 0;	/* number of bytes allocated */
static unsigned char *inbuf_ptr = NULL;
static int inbuf_len = 0;		/* number byte of actual data available. */
static int inbuf_next = 0;		/* index of next to remove. */

static int outbuf_size_in_bytes = 0;
static unsigned char *outbuf_ptr = NULL;
static int outbuf_len = 0;

#define ONE_BUF_TIME 40
		
static enum audio_in_type_e audio_in_type;

// UDP socket used for receiving data

static int udp_sock;


#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)
#define calcbufsize(rate,chans,bits) roundup1k( ( (rate)*(chans)*(bits) / 8 * ONE_BUF_TIME)/1000  )


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
 *				The sofware modem must use this ACTUAL information
 *				that the device is supplying, that could be different
 *				than what the user specified.
 *              
 * Returns:     0 for success, -1 for failure.
 *		
 *----------------------------------------------------------------*/

int audio_open (struct audio_s *pa)
{
	int err;
	int chan;

#if USE_ALSA

	char audio_in_name[30];
	char audio_out_name[30];

	assert (audio_in_handle == NULL);
	assert (audio_out_handle == NULL);

#else

	assert (oss_audio_device_fd == -1);
#endif

/*
 * Fill in defaults for any missing values.
 */
	if (pa -> num_channels == 0)
	  pa -> num_channels = DEFAULT_NUM_CHANNELS;

	if (pa -> samples_per_sec == 0)
	  pa -> samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

	if (pa -> bits_per_sample == 0)
	  pa -> bits_per_sample = DEFAULT_BITS_PER_SAMPLE;

	for (chan=0; chan<MAX_CHANS; chan++) {
	  if (pa -> mark_freq[chan] == 0)
	    pa -> mark_freq[chan] = DEFAULT_MARK_FREQ;

	  if (pa -> space_freq[chan] == 0)
	    pa -> space_freq[chan] = DEFAULT_SPACE_FREQ;

	  if (pa -> baud[chan] == 0)
	    pa -> baud[chan] = DEFAULT_BAUD;

	  if (pa->num_subchan[chan] == 0)
	    pa->num_subchan[chan] = 1;
	}

/*
 * Open audio device.
 */

	udp_sock = -1;

	inbuf_size_in_bytes = 0;
	inbuf_ptr = NULL;
	inbuf_len = 0;
	inbuf_next = 0;

	outbuf_size_in_bytes = 0;
	outbuf_ptr = NULL;
	outbuf_len = 0;

#if USE_ALSA

/*
 * Determine the type of audio input.
 */
	audio_in_type = AUDIO_IN_TYPE_SOUNDCARD; 	
	
	if (strcasecmp(pa->adevice_in, "stdin") == 0 || strcmp(pa->adevice_in, "-") == 0) {
	  audio_in_type = AUDIO_IN_TYPE_STDIN;
	  /* Change - to stdin for readability. */
	  strcpy (pa->adevice_in, "stdin");
	}
	else if (strncasecmp(pa->adevice_in, "udp:", 4) == 0) {
	  audio_in_type = AUDIO_IN_TYPE_SDR_UDP;
	  /* Supply default port if none specified. */
	  if (strcasecmp(pa->adevice_in,"udp") == 0 ||
	    strcasecmp(pa->adevice_in,"udp:") == 0) {
	    sprintf (pa->adevice_in, "udp:%d", DEFAULT_UDP_AUDIO_PORT);
	  }
	} 

/* Let user know what is going on. */

	/* If not specified, the device names should be "default". */

	strcpy (audio_in_name, pa->adevice_in);
	strcpy (audio_out_name, pa->adevice_out);

        text_color_set(DW_COLOR_INFO);

	if (strcmp(audio_in_name,audio_out_name) == 0) {
          dw_printf ("Audio device for both receive and transmit: %s\n", audio_in_name);
	}
	else {
          dw_printf ("Audio input device for receive: %s\n", audio_in_name);
          dw_printf ("Audio out device for transmit: %s\n", audio_out_name);
	}

/*
 * Now attempt actual opens.
 */

/*
 * Input device.
 */
	switch (audio_in_type) {

/*
 * Soundcard - ALSA.
 */
	  case AUDIO_IN_TYPE_SOUNDCARD:

	    err = snd_pcm_open (&audio_in_handle, audio_in_name, SND_PCM_STREAM_CAPTURE, 0);
	    if (err < 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not open audio device %s for input\n%s\n", 
	  		audio_in_name, snd_strerror(err));
	      return (-1);
	    }

	    inbuf_size_in_bytes = set_alsa_params (audio_in_handle, pa, audio_in_name, "input");
	    break;

/*
 * UDP.
 */
	  case AUDIO_IN_TYPE_SDR_UDP:

	    //Create socket and bind socket
	    
	    {
	      struct sockaddr_in si_me;
	      int slen=sizeof(si_me);
	      int data_size = 0;

	      //Create UDP Socket
	      if ((udp_sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Couldn't create socket, errno %d\n", errno);
	        return -1;
	      }

	      memset((char *) &si_me, 0, sizeof(si_me));
	      si_me.sin_family = AF_INET;   
	      si_me.sin_port = htons((short)atoi(audio_in_name+4));
	      si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	      //Bind to the socket
	      if (bind(udp_sock, (const struct sockaddr *) &si_me, sizeof(si_me))==-1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Couldn't bind socket, errno %d\n", errno);
	        return -1;
	      }
	    }
	    inbuf_size_in_bytes = SDR_UDP_BUF_MAXLEN; 
	
	    break;

/* 
 * stdin.
 */
   	  case AUDIO_IN_TYPE_STDIN:

	    /* Do we need to adjust any properties of stdin? */

	    inbuf_size_in_bytes = 1024; 
	
	    break;

	  default:

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error, invalid audio_in_type\n");
	    return (-1);
  	}

/*
 * Output device.  Only "soundcard" is supported at this time. 
 */
	err = snd_pcm_open (&audio_out_handle, audio_out_name, SND_PCM_STREAM_PLAYBACK, 0);

	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not open audio device %s for output\n%s\n", 
			audio_out_name, snd_strerror(err));
	  return (-1);
	}

	outbuf_size_in_bytes = set_alsa_params (audio_out_handle, pa, audio_out_name, "output");

	if (inbuf_size_in_bytes <= 0 || outbuf_size_in_bytes <= 0) {
	  return (-1);
	}




#else /* end of ALSA case */


#error OSS support will probably be removed.  Complain if you still care about OSS.

	oss_audio_device_fd = open (pa->adevice_in, O_RDWR);

	if (oss_audio_device_fd < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("%s:\n", pa->adevice_in);
	  sprintf (message, "Could not open audio device %s", pa->adevice_in);
	  perror (message);
	  return (-1);
	}

	outbuf_size_in_bytes = inbuf_size_in_bytes = set_oss_params (oss_audio_device_fd, pa);

	if (inbuf_size_in_bytes <= 0 || outbuf_size_in_bytes <= 0) {
	  return (-1);
	}



#endif	/* end of OSS case */


/*
 * Finally allocate buffer for each direction.
 */
	inbuf_ptr = malloc(inbuf_size_in_bytes);
	assert (inbuf_ptr  != NULL);
	inbuf_len = 0;
	inbuf_next = 0;

	outbuf_ptr = malloc(outbuf_size_in_bytes);
	assert (outbuf_ptr  != NULL);
	outbuf_len = 0;
	
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
 *   Sample	- for one channel.	e.g. 2 bytes for 16 bit.
 *   Frame	- one sample for all channels.  e.g. 4 bytes for 16 bit stereo
 *   Period	- size of one transfer.
 */

static int set_alsa_params (snd_pcm_t *handle, struct audio_s *pa, char *devname, char *inout)
{

	snd_pcm_hw_params_t *hw_params;
	snd_pcm_uframes_t fpp; 		/* Frames per period. */

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
 		pa->bits_per_sample == 8 ? SND_PCM_FORMAT_U8 : SND_PCM_FORMAT_S16_LE);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set bits per sample.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Number of audio channels. */


	err = snd_pcm_hw_params_set_channels (handle, hw_params, pa->num_channels);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set number of audio channels.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	/* Audio sample rate. */


	val = pa->samples_per_sec;

	dir = 0;


	err = snd_pcm_hw_params_set_rate_near (handle, hw_params, &val, &dir);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not set audio sample rate.\n%s\n", 
			snd_strerror(err));
	  dw_printf ("for %s %s.\n", devname, inout);
	  return (-1);
	}

	if (val != pa->samples_per_sec) {

	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Asked for %d samples/sec but got %d.\n",

				pa->samples_per_sec, val);
	  dw_printf ("for %s %s.\n", devname, inout);

	  pa->samples_per_sec = val;

	}

	/* Guessing around 20 reads/sec might be good. */
	/* Period too long = too much latency. */
	/* Period too short = too much overhead of many small transfers. */

	fpp = pa->samples_per_sec / 20;

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

	bytes_per_frame = snd_pcm_frames_to_bytes (handle, 1);
	assert (bytes_per_frame == pa->num_channels * pa->bits_per_sample / 8);


	buf_size_in_bytes = fpp * bytes_per_frame;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio buffer size = %d (bytes per frame) x %d (frames per period) = %d \n", bytes_per_frame, (int)fpp, buf_size_in_bytes);
#endif

	return (buf_size_in_bytes);


} /* end alsa_set_params */


#else


/*
 * Set parameters for sound card.  (OSS only)
 *
 * See  /usr/include/sys/soundcard.h  for details. 
 */

static int set_oss_params (int fd, struct audio_s *pa) 
{
	int err;
	int devcaps;
	int asked_for;
	char message[100];
	int ossbuf_size_in_bytes;


	err = ioctl (fd, SNDCTL_DSP_CHANNELS, &(pa->num_channels));
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to set audio device number of channels");
 	  return (-1);
	}

        asked_for = pa->samples_per_sec;

	err = ioctl (fd, SNDCTL_DSP_SPEED, &(pa->samples_per_sec));
   	if (err == -1) {
	  text_color_set(DW_COLOR_ERROR);
    	  perror("Not able to set audio device sample rate");
 	  return (-1);
	}

	if (pa->samples_per_sec != asked_for) {
	  text_color_set(DW_COLOR_INFO);
          dw_printf ("Asked for %d samples/sec but actually using %d.\n",
		asked_for, pa->samples_per_sec);
	}

	/* This is actually a bit mask but it happens that */
	/* 0x8 is unsigned 8 bit samples and */
	/* 0x10 is signed 16 bit little endian. */

	err = ioctl (fd, SNDCTL_DSP_SETFMT, &(pa->bits_per_sample));
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
 * Your milage may vary.
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

	ossbuf_size_in_bytes = calcbufsize(pa->samples_per_sec, pa->num_channels, pa->bits_per_sample);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_open(): using block size of %d\n", ossbuf_size_in_bytes);	
#endif

	assert (ossbuf_size_in_bytes >= 256 && ossbuf_size_in_bytes <= 32768);


	return (ossbuf_size_in_bytes);

} /* end set_oss_params */


#endif



/*------------------------------------------------------------------
 *
 * Name:        audio_get
 *
 * Purpose:     Get one byte from the audio device.
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
int audio_get (void)
{
	int n;
	int retries = 0;

#if STATISTICS
	/* Gather numbers for read from audio device. */

	static int duration = 100;	/* report every 100 seconds. */
	static time_t last_time = 0;
	time_t this_time;
	static int sample_count;
	static int error_count;
#endif

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("audio_get():\n");

#endif

	assert (inbuf_size_in_bytes >= 100 && inbuf_size_in_bytes <= 32768);

	  
#if USE_ALSA

	switch (audio_in_type) {

/*
 * Soundcard - ALSA 
 */
	  case AUDIO_IN_TYPE_SOUNDCARD:

	    while (inbuf_next >= inbuf_len) {

	      assert (audio_in_handle != NULL);
#if DEBUGx
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("audio_get(): readi asking for %d frames\n", inbuf_size_in_bytes / bytes_per_frame);	
#endif
	      n = snd_pcm_readi (audio_in_handle, inbuf_ptr, inbuf_size_in_bytes / bytes_per_frame);

#if DEBUGx	  
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("audio_get(): readi asked for %d and got %d frames\n",
		inbuf_size_in_bytes / bytes_per_frame, n);	
#endif

#if STATISTICS
	      if (last_time == 0) {
	        last_time = time(NULL);
	        sample_count = 0;
	        error_count = 0;
	      }
	      else {
	        if (n > 0) {
	           sample_count += n;
	        }
	        else {
	           error_count++;
	        }
	        this_time = time(NULL);
	        if (this_time >= last_time + duration) {
	          text_color_set(DW_COLOR_DEBUG);
	          dw_printf ("\nPast %d seconds, %d audio samples, %d errors.\n\n", 
			duration, sample_count, error_count);
	          last_time = this_time;
	          sample_count = 0;
	          error_count = 0;
	        }      
	      }
#endif
 
	      if (n > 0) {

	        /* Success */

	        inbuf_len = n * bytes_per_frame;		/* convert to number of bytes */
	        inbuf_next = 0;
	      }
	      else if (n == 0) {

	        /* Didn't expect this, but it's not a problem. */
	        /* Wait a little while and try again. */

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Audio input got zero bytes: %s\n", snd_strerror(n));
	        SLEEP_MS(10);

	        inbuf_len = 0;
	        inbuf_next = 0;
	      }
	      else {
	        /* Error */
	        // TODO: Needs more study and testing. 

		// TODO: print n.  should snd_strerror use n or errno?
		// Audio input device error: Unknown error

	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Audio input device error: %s\n", snd_strerror(n));

	        /* Try to recover a few times and eventually give up. */
	        if (++retries > 10) {
	          inbuf_len = 0;
	          inbuf_next = 0;
	          return (-1);
	        }

	        if (n == -EPIPE) {

	          /* EPIPE means overrun */

	          snd_pcm_recover (audio_in_handle, n, 1);

	        } 
	        else {
	          /* Could be some temporary condition. */
	          /* Wait a little then try again. */
	          /* Sometimes I get "Resource temporarily available" */
	          /* when the Update Manager decides to run. */

	          SLEEP_MS (250);
	          snd_pcm_recover (audio_in_handle, n, 1);
	        }
	      }
	    }
	    break;

/* 
 * UDP.
 */

	  case AUDIO_IN_TYPE_SDR_UDP:

	    while (inbuf_next >= inbuf_len) {
	      int ch, res,i;

              assert (udp_sock > 0);
	      res = recv(udp_sock, inbuf_ptr, inbuf_size_in_bytes, 0);
	      if (res < 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't read from udp socket, res=%d", res);
	        inbuf_len = 0;
	        inbuf_next = 0;
	        return (-1);
	      }
	    
	      inbuf_len = res;
	      inbuf_next = 0;
	    }
	    break;

/*
 * stdin.
 */
	  case AUDIO_IN_TYPE_STDIN:

	    while (inbuf_next >= inbuf_len) {
	      int ch, res,i;

	      res = read(STDIN_FILENO, inbuf_ptr, (size_t)inbuf_size_in_bytes);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("\nEnd of file on stdin.  Exiting.\n");
	        exit (0);
	      }
	    
	      inbuf_len = res;
	      inbuf_next = 0;
	    }

	    break;
	}



#else	/* end ALSA, begin OSS */

	while (audio_in_type == AUDIO_IN_TYPE_SOUNDCARD && inbuf_next >= inbuf_len) {
	  assert (oss_audio_device_fd > 0);
	  n = read (oss_audio_device_fd, inbuf_ptr, inbuf_size_in_bytes);
	  //text_color_set(DW_COLOR_DEBUG);
	  // dw_printf ("audio_get(): read %d returns %d\n", inbuf_size_in_bytes, n);	
	  if (n < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Can't read from audio device");
	    inbuf_len = 0;
	    inbuf_next = 0;
	    return (-1);
	  }
	  inbuf_len = n;
	  inbuf_next = 0;
	}

#endif	/* USE_ALSA */



	if (inbuf_next < inbuf_len)
	  n = inbuf_ptr[inbuf_next++];
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
 * Inputs:	c	- One byte in range of 0 - 255.
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

int audio_put (int c)
{
	/* Should never be full at this point. */
	assert (outbuf_len < outbuf_size_in_bytes);

	outbuf_ptr[outbuf_len++] = c;

	if (outbuf_len == outbuf_size_in_bytes) {
	  return (audio_flush());
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

int audio_flush (void)
{
#if USE_ALSA
	int k;
	char *psound;
	int retries = 10;
	snd_pcm_status_t *status;

	assert (audio_out_handle != NULL);


/*
 * Trying to set the automatic start threshold didn't have the desired
 * effect.  After the first transmitted packet, they are saved up
 * for a few minutes and then all come out together.
 *
 * "Prepare" it if not already in the running state.
 * We stop it at the end of each transmitted packet.
 */


	snd_pcm_status_alloca(&status);

	k = snd_pcm_status (audio_out_handle, status);
	if (k != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio output get status error.\n%s\n", snd_strerror(k));
	}

	if ((k = snd_pcm_status_get_state(status)) != SND_PCM_STATE_RUNNING) {

	  //text_color_set(DW_COLOR_DEBUG);
	  //dw_printf ("Audio output state = %d.  Try to start.\n", k);

	  k = snd_pcm_prepare (audio_out_handle);

	  if (k != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio output start error.\n%s\n", snd_strerror(k));
	  }
	}


	psound = outbuf_ptr;

	while (retries-- > 0) {

	  k = snd_pcm_writei (audio_out_handle, psound, outbuf_len / bytes_per_frame);	
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("audio_flush(): snd_pcm_writei %d frames returns %d\n",
				outbuf_len / bytes_per_frame, k);
	  fflush (stdout);	
#endif
	  if (k == -EPIPE) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio output data underrun.\n");

	    /* No problemo.  Recover and go around again. */

	    snd_pcm_recover (audio_out_handle, k, 1);
	  }
 	  else if (k < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio write error: %s\n", snd_strerror(k));

	    /* Some other error condition. */
	    /* Try again. What do we have to lose? */

	    snd_pcm_recover (audio_out_handle, k, 1);
	  }
 	  else if (k != outbuf_len / bytes_per_frame) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio write took %d frames rather than %d.\n",
 			k, outbuf_len / bytes_per_frame);
	
	    /* Go around again with the rest of it. */

	    psound += k * bytes_per_frame;
	    outbuf_len -= k * bytes_per_frame;
	  }
	  else {
	    /* Success! */
	    outbuf_len = 0;
	    return (0);
	  }
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Audio write error retry count exceeded.\n");

	outbuf_len = 0;
	return (-1);

#else		/* OSS */

	int k;
	unsigned char *ptr;	
	int len;

	ptr = outbuf_ptr;
	len = outbuf_len;

	while (len > 0) {
	  assert (oss_audio_device_fd > 0);
	  k = write (oss_audio_device_fd, ptr, len);	
#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("audio_flush(): write %d returns %d\n", len, k);
	  fflush (stdout);	
#endif
	  if (k < 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Can't write to audio device");
	    outbuf_len = 0;
	    return (-1);
	  }
	  if (k < len) {
	    /* presumably full but didn't block. */
	    usleep (10000);
	  }
	  ptr += k;
	  len -= k;
	}

	outbuf_len = 0;
	return (0);
#endif

} /* end audio_flush */


/*------------------------------------------------------------------
 *
 * Name:        audio_wait
 *
 * Purpose:     Wait until all the queued up audio out has been played.
 *
 * Inputs:	duration	- hint at number of milliseconds to wait.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 * Description:	In our particular application, we would want to make sure
 *		that the entire packet has been sent out before turning
 *		off the transmitter PTT control.
 *
 * In an ideal world:
 * 
 *		We would like to ask the hardware when all the queued
 *		up sound has actually come out the speaker.
 *		There is an OSS system call for this but it doesn't work
 *		on Cygwin.  The application crashes at a later time.
 *
 *		Haven't yet verified correct operation with ALSA.
 * 
 * In reality:
 *
 *		Caller does the following:
 *
 *		(1) Make note of when PTT is turned on.
 *		(2) Calculate how long it will take to transmit the 
 *			frame including TXDELAY, frame (including 
 *			"flags", data, FCS and bit stuffing), and TXTAIL.
 *		(3) Add (1) and (2) resulting in when PTT should be turned off.
 *		(4) Take difference between current time and PPT off time
 *			and provide this as the additional delay required.
 *
 *----------------------------------------------------------------*/

int audio_wait (int duration)
{	
	int err = 0;

	audio_flush ();
#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_wait(): before sync, fd=%d\n", oss_audio_device_fd);	
#endif

#if USE_ALSA

	//double t_enter, t_leave;
	//int drain_ms;

	//t_enter = dtime_now();

	/* For playback, this should wait for all pending frames */
	/* to be played and then stop. */

	snd_pcm_drain (audio_out_handle);

	//t_leave = dtime_now();
	//drain_ms = (int)((t_leave - t_enter) * 1000.);

	//text_color_set(DW_COLOR_DEBUG);
	//dw_printf ("audio_wait():  suggested delay = %d ms, actual = %d\n",
	//		duration, drain_ms);

	/*
 	 * Experimentation reveals that snd_pcm_drain doesn't
	 * actually wait.  It returns immediately. 
	 * However it does serve a useful purpose of stopping
	 * the playback after all the queued up data is used.
 	 *
	 * Keep the sleep delay so PTT is not turned off too soon.
 	 */

	if (duration > 0) {
	  SLEEP_MS (duration);
	}

#else

	assert (oss_audio_device_fd > 0);


	// This causes a crash later on Cygwin.
	// Haven't tried it on Linux yet.

	// err = ioctl (oss_audio_device_fd, SNDCTL_DSP_SYNC, NULL);

	if (duration > 0) {
	  SLEEP_MS (duration);
	}

#endif

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_wait(): after sync, status=%d\n", err);	
#endif

	return (err);

} /* end audio_wait */


/*------------------------------------------------------------------
 *
 * Name:        audio_close
 *
 * Purpose:     Close the audio device.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 *
 *----------------------------------------------------------------*/

int audio_close (void)
{
	int err = 0;

#if USE_ALSA
	assert (audio_in_handle != NULL);
	assert (audio_out_handle != NULL);

	audio_wait (0);

	snd_pcm_close (audio_in_handle);
	snd_pcm_close (audio_out_handle);

#else
	assert (oss_audio_device_fd > 0);

	audio_wait (0);

	close (oss_audio_device_fd);

	oss_audio_device_fd = -1;
#endif
	free (inbuf_ptr);
	free (outbuf_ptr);

	inbuf_size_in_bytes = 0;
	inbuf_ptr = NULL;
	inbuf_len = 0;
	inbuf_next = 0;

	outbuf_size_in_bytes = 0;
	outbuf_ptr = NULL;
	outbuf_len = 0;

	return (err);

} /* end audio_close */


/* end audio.c */


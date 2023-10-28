
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
 * Module:      audio_win.c
 *
 * Purpose:   	Interface to audio device commonly called a "sound card" for
 *		historical reasons.		
 *
 *		This version uses the native Windows sound interface.
 *
 * Credits:	Fabrice FAURE contributed Linux code for the SDR UDP interface.
 *
 *		Discussion here:  http://gqrx.dk/doc/streaming-audio-over-udp
 *
 * Major revisions:
 *
 *		1.2 - Add ability to use more than one audio device.
 *
 *---------------------------------------------------------------*/


#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h
				// Also includes windows.h.


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>

#include <mmsystem.h>

#ifndef WAVE_FORMAT_96M16
#define WAVE_FORMAT_96M16 0x40000
#define WAVE_FORMAT_96S16 0x80000
#endif

#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this


#include "audio.h"
#include "audio_stats.h"
#include "textcolor.h"
#include "ptt.h"
#include "demod.h"		/* for alevel_t & demod_get_audio_level() */



/* Audio configuration. */

static struct audio_s          *save_audio_config_p;


/* 
 * Allocate enough buffers for 1 second each direction. 
 * Each buffer size is a trade off between being responsive 
 * to activity on the channel vs. overhead of having too
 * many little transfers.
 */

/*
 * Originally, we had an arbitrary buf time of 40 mS.
 *
 * For mono, the buffer size was rounded up from 3528 to 4k so
 * it was really about 50 mS per buffer or about 20 per second.
 * For stereo, the buffer size was rounded up from 7056 to 7k so
 * it was really about 43.7 mS per buffer or about 23 per second.
 * 
 * In version 1.2, let's try changing it to 10 to reduce the latency.
 * For mono, the buffer size was rounded up from 882 to 1k so it
 * was really about 12.5 mS per buffer or about 80 per second.
 */

#define TOTAL_BUF_TIME 1000	
#define ONE_BUF_TIME 10
		
#define NUM_IN_BUF ((TOTAL_BUF_TIME)/(ONE_BUF_TIME))
#define NUM_OUT_BUF ((TOTAL_BUF_TIME)/(ONE_BUF_TIME))


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

	/* Version 1.3 - add a sanity check. */
	if (size2 < 256 || size2 > 32768) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio buffer has unexpected extreme size of %d bytes.\n", size2);
	  dw_printf ("Detected at %s, line %d.\n", __FILE__, __LINE__);
	  dw_printf ("This might be caused by unusual audio device configuration values.\n"); 
	  size2 = 2048;
	  dw_printf ("Using %d to attempt recovery.\n", size2);
	}

	return (size2);
}


/* Information for each audio stream (soundcard, stdin, or UDP) */

static struct adev_s {

	enum audio_in_type_e g_audio_in_type;	

/*
 * UDP socket for receiving audio stream.
 * Buffer, length, and pointer for UDP or stdin.
 */

	
	SOCKET udp_sock;
	char stream_data[SDR_UDP_BUF_MAXLEN];
	int stream_len;
	int stream_next;


/* For sound output. */
/* out_wavehdr.dwUser is used to keep track of output buffer state. */

#define DWU_FILLING 1		/* Ready to use or in process of being filled. */
#define DWU_PLAYING 2		/* Was given to sound system for playing. */
#define DWU_DONE 3		/* Sound system is done with it. */

	HWAVEOUT audio_out_handle;

	volatile WAVEHDR out_wavehdr[NUM_OUT_BUF];
	int out_current;		/* index to above. */
	int outbuf_size;


/* For sound input. */
/* In this case dwUser is index of next available byte to remove. */

	HWAVEIN  audio_in_handle;
	WAVEHDR in_wavehdr[NUM_IN_BUF];
	volatile WAVEHDR *in_headp;	/* head of queue to process. */
	CRITICAL_SECTION in_cs;

} adev[MAX_ADEVS];


/*------------------------------------------------------------------
 *
 * Name:        audio_open
 *
 * Purpose:     Open the digital audio device.
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
 * Outputs:	pa		- The ACTUAL values are returned here.
 *
 *				The Linux version adjusts strange values to the 
 *				nearest valid value.  Don't know, yet, if Windows
 *				does the same or just fails.  Or performs some
 *				expensive resampling from a rate supported by
 *				hardware.
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
 * References:	Multimedia Reference
 *
 *		http://msdn.microsoft.com/en-us/library/windows/desktop/dd743606%28v=vs.85%29.aspx
 *
 *----------------------------------------------------------------*/


static void CALLBACK in_callback (HWAVEIN handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);
static void CALLBACK out_callback (HWAVEOUT handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);

int audio_open (struct audio_s *pa)
{
	int a;

	int err;
	int chan;
	int n;
	int in_dev_no[MAX_ADEVS];
	int out_dev_no[MAX_ADEVS];


	int num_devices;
	WAVEINCAPS wic;
	WAVEOUTCAPS woc;

	save_audio_config_p = pa;


    	for (a=0; a<MAX_ADEVS; a++) {
      	  if (pa->adev[a].defined) {

            struct adev_s *A = &(adev[a]);

	    assert (A->audio_in_handle == 0);
	    assert (A->audio_out_handle == 0);

	    //text_color_set(DW_COLOR_DEBUG);
	    //dw_printf ("pa->adev[a].adevice_in = '%s'\n",  pa->adev[a].adevice_in);
	    //dw_printf ("pa->adev[a].adevice_out = '%s'\n", pa->adev[a].adevice_out);


/*
 * Fill in defaults for any missing values.
 */
	    if (pa -> adev[a].num_channels == 0)
	      pa -> adev[a].num_channels = DEFAULT_NUM_CHANNELS;

	    if (pa -> adev[a].samples_per_sec == 0)
	      pa -> adev[a].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

	    if (pa -> adev[a].bits_per_sample == 0)
	      pa -> adev[a].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;

	    A->g_audio_in_type = AUDIO_IN_TYPE_SOUNDCARD;

	    for (chan=0; chan<MAX_CHANS; chan++) {
	      if (pa -> achan[chan].mark_freq == 0)
	        pa -> achan[chan].mark_freq = DEFAULT_MARK_FREQ;

	      if (pa -> achan[chan].space_freq == 0)
	        pa -> achan[chan].space_freq = DEFAULT_SPACE_FREQ;

	      if (pa -> achan[chan].baud == 0)
	        pa -> achan[chan].baud = DEFAULT_BAUD;

	      if (pa->achan[chan].num_subchan == 0)
	        pa->achan[chan].num_subchan = 1;
	    }


	    A->udp_sock = INVALID_SOCKET;

	    in_dev_no[a] = WAVE_MAPPER;	/* = ((UINT)-1) in mmsystem.h */
	    out_dev_no[a] = WAVE_MAPPER;

/*
 * Determine the type of audio input and select device.
 * This can be soundcard, UDP stream, or stdin.
 */
	
	    if (strcasecmp(pa->adev[a].adevice_in, "stdin") == 0 || strcmp(pa->adev[a].adevice_in, "-") == 0) {
	      A->g_audio_in_type = AUDIO_IN_TYPE_STDIN;
	      /* Change - to stdin for readability. */
	      strlcpy (pa->adev[a].adevice_in, "stdin", sizeof(pa->adev[a].adevice_in));
	    }
	    else if (strncasecmp(pa->adev[a].adevice_in, "udp:", 4) == 0) {
	      A->g_audio_in_type = AUDIO_IN_TYPE_SDR_UDP;
	      /* Supply default port if none specified. */
	      if (strcasecmp(pa->adev[a].adevice_in,"udp") == 0 ||
	        strcasecmp(pa->adev[a].adevice_in,"udp:") == 0) {
	        snprintf (pa->adev[a].adevice_in, sizeof(pa->adev[a].adevice_in), "udp:%d", DEFAULT_UDP_AUDIO_PORT);
	      }
	    } 
	    else {
	      A->g_audio_in_type = AUDIO_IN_TYPE_SOUNDCARD; 	

	      /* Does config file have a number?  */
	      /* If so, it is an index into list of devices. */
	      /* Originally only a single digit was recognized.  */
	      /* v 1.5 also recognizes two digits.  (Issue 116) */

	      if (strlen(pa->adev[a].adevice_in) == 1 && isdigit(pa->adev[a].adevice_in[0])) {
	        in_dev_no[a] = atoi(pa->adev[a].adevice_in);
	      }
	      else if (strlen(pa->adev[a].adevice_in) == 2 && isdigit(pa->adev[a].adevice_in[0]) && isdigit(pa->adev[a].adevice_in[1])) {
	        in_dev_no[a] = atoi(pa->adev[a].adevice_in);
	      }

	      /* Otherwise, does it have search string? */

	      if ((UINT)(in_dev_no[a]) == WAVE_MAPPER && strlen(pa->adev[a].adevice_in) >= 1) {
	        num_devices = waveInGetNumDevs();
	        for (n=0 ; n<num_devices && (UINT)(in_dev_no[a]) == WAVE_MAPPER ; n++) {
	          if ( ! waveInGetDevCaps(n, &wic, sizeof(WAVEINCAPS))) {
	            if (strstr(wic.szPname, pa->adev[a].adevice_in) != NULL) {
	              in_dev_no[a] = n;
	            }
	          }
	        }
	        if ((UINT)(in_dev_no[a]) == WAVE_MAPPER) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("\"%s\" doesn't match any of the input devices.\n", pa->adev[a].adevice_in);
	        }
	      }
 	    }

/*
 * Select output device.
 * Only soundcard at this point.
 * Purhaps we'd like to add UDP for an SDR transmitter.
 */
	    if (strlen(pa->adev[a].adevice_out) == 1 && isdigit(pa->adev[a].adevice_out[0])) {
	      out_dev_no[a] = atoi(pa->adev[a].adevice_out);
	    }
	    else if (strlen(pa->adev[a].adevice_out) == 2 && isdigit(pa->adev[a].adevice_out[0]) && isdigit(pa->adev[a].adevice_out[1])) {
	      out_dev_no[a] = atoi(pa->adev[a].adevice_out);
	    }

	    if ((UINT)(out_dev_no[a]) == WAVE_MAPPER && strlen(pa->adev[a].adevice_out) >= 1) {
	      num_devices = waveOutGetNumDevs();
	      for (n=0 ; n<num_devices && (UINT)(out_dev_no[a]) == WAVE_MAPPER ; n++) {
	        if ( ! waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS))) {
	          if (strstr(woc.szPname, pa->adev[a].adevice_out) != NULL) {
	            out_dev_no[a] = n;
	          }
	        }
	      }
	      if ((UINT)(out_dev_no[a]) == WAVE_MAPPER) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("\"%s\" doesn't match any of the output devices.\n", pa->adev[a].adevice_out);
	      }
	    }
	  }   /* if defined */
	}    /* for each device */


/*
 * Display the input devices (soundcards) available and what is selected.
 */

	text_color_set(DW_COLOR_INFO);
	dw_printf ("Available audio input devices for receive (*=selected):\n");

	num_devices = waveInGetNumDevs();

        for (a=0; a<MAX_ADEVS; a++) {
          if (pa->adev[a].defined) {

	    if (in_dev_no[a] < -1 || in_dev_no[a] >= num_devices) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid input (receive) audio device number %d.\n", in_dev_no[a]);
	      in_dev_no[a] = WAVE_MAPPER;
	    }
	  }
        }

	text_color_set(DW_COLOR_INFO);
	for (n=0; n<num_devices; n++) {

	  if ( ! waveInGetDevCaps(n, &wic, sizeof(WAVEINCAPS))) {
	    for (a=0; a<MAX_ADEVS; a++) {
	      if (pa->adev[a].defined) {
	        dw_printf (" %c", n==in_dev_no[a] ? '*' : ' ');

	      }
	    }
	    dw_printf ("  %d: %s", n, wic.szPname);

	    for (a=0; a<MAX_ADEVS; a++) {
	      if (pa->adev[a].defined && n==in_dev_no[a]) {
	        if (pa->adev[a].num_channels == 2) {
	          dw_printf ("   (channels %d & %d)", ADEVFIRSTCHAN(a), ADEVFIRSTCHAN(a)+1);
	        }
	        else {
	          dw_printf ("   (channel %d)", ADEVFIRSTCHAN(a));
	        }
	      }
	    }
	    dw_printf ("\n");
 	  }
    	}

// Add UDP or stdin to end of device list if used.

    	for (a=0; a<MAX_ADEVS; a++) {
      	  if (pa->adev[a].defined) {

            struct adev_s *A = &(adev[a]);

	    /* Display stdin or udp:port if appropriate. */   

	    if (A->g_audio_in_type != AUDIO_IN_TYPE_SOUNDCARD) {

	      int aaa;
	      for (aaa=0; aaa<MAX_ADEVS; aaa++) {
	        if (pa->adev[aaa].defined) {
	          dw_printf (" %c", a == aaa ? '*' : ' ');

	        }
	      }
	      dw_printf ("  %s                             ", pa->adev[a].adevice_in);	/* should be UDP:nnnn or stdin */

	      if (pa->adev[a].num_channels == 2) {
	        dw_printf ("   (channels %d & %d)", ADEVFIRSTCHAN(a), ADEVFIRSTCHAN(a)+1);
	      }
	      else {
	        dw_printf ("   (channel %d)", ADEVFIRSTCHAN(a));
	      }
	      dw_printf ("\n");
	    }
      	  }
     	}


/*
 * Display the output devices (soundcards) available and what is selected.
 */

	dw_printf ("Available audio output devices for transmit (*=selected):\n");

	/* TODO? */
	/* No "*" is currently displayed when using the default device. */
	/* Should we put "*" next to the default device when using it? */
	/* Which is the default?  The first one? */

	num_devices = waveOutGetNumDevs();

        for (a=0; a<MAX_ADEVS; a++) {
          if (pa->adev[a].defined) {
	    if (out_dev_no[a] < -1 || out_dev_no[a] >= num_devices) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid output (transmit) audio device number %d.\n", out_dev_no[a]);
	      out_dev_no[a] = WAVE_MAPPER;
	    }
	  }
	}

	text_color_set(DW_COLOR_INFO);
	for (n=0; n<num_devices; n++) {

	  if ( ! waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS))) {
	    for (a=0; a<MAX_ADEVS; a++) {
	      if (pa->adev[a].defined) {
	        dw_printf (" %c", n==out_dev_no[a] ? '*' : ' ');

	      }
	    }
	    dw_printf ("  %d: %s", n, woc.szPname);

	    for (a=0; a<MAX_ADEVS; a++) {
	      if (pa->adev[a].defined && n==out_dev_no[a]) {
	        if (pa->adev[a].num_channels == 2) {
	          dw_printf ("   (channels %d & %d)", ADEVFIRSTCHAN(a), ADEVFIRSTCHAN(a)+1);
	        }
	        else {
	          dw_printf ("   (channel %d)", ADEVFIRSTCHAN(a));
	        }
	      }
	    }
	    dw_printf ("\n");
	  }
	}


/*
 * Open for each audio device input/output pair.
 */

     	for (a=0; a<MAX_ADEVS; a++) {
      	  if (pa->adev[a].defined) {

            struct adev_s *A = &(adev[a]);

	     WAVEFORMATEX wf;

	     wf.wFormatTag = WAVE_FORMAT_PCM;
	     wf.nChannels = pa -> adev[a].num_channels; 
	     wf.nSamplesPerSec = pa -> adev[a].samples_per_sec;
	     wf.wBitsPerSample = pa -> adev[a].bits_per_sample;
	     wf.nBlockAlign = (wf.wBitsPerSample / 8) * wf.nChannels;
	     wf.nAvgBytesPerSec = wf.nBlockAlign * wf.nSamplesPerSec;
	     wf.cbSize = 0;

	     A->outbuf_size = calcbufsize(wf.nSamplesPerSec,wf.nChannels,wf.wBitsPerSample);


/*
 * Open the audio output device.
 * Soundcard is only possibility at this time.
 */

	     err = waveOutOpen (&(A->audio_out_handle), out_dev_no[a], &wf, (DWORD_PTR)out_callback, a, CALLBACK_FUNCTION);
	     if (err != MMSYSERR_NOERROR) {
	       text_color_set(DW_COLOR_ERROR);
	       dw_printf ("Could not open audio device for output.\n");
	       return (-1);
	     }
	  

/*
 * Set up the output buffers.
 * We use dwUser to indicate it is available for filling.
 */

	     memset ((void*)(A->out_wavehdr), 0, sizeof(A->out_wavehdr));

	     for (n = 0; n < NUM_OUT_BUF; n++) {
	       A->out_wavehdr[n].lpData = malloc(A->outbuf_size);
	       A->out_wavehdr[n].dwUser = DWU_FILLING;	
	       A->out_wavehdr[n].dwBufferLength = 0;
	     }
	     A->out_current = 0;			

	
/*
 * Open audio input device.
 * More possibilities here:  soundcard, UDP port, stdin.
 */

	     switch (A->g_audio_in_type) {

/*
 * Soundcard.
 */
	       case AUDIO_IN_TYPE_SOUNDCARD:

		 // Use InitializeCriticalSectionAndSpinCount to avoid exceptions in low memory situations?

	         InitializeCriticalSection (&(A->in_cs));

	         err = waveInOpen (&(A->audio_in_handle), in_dev_no[a], &wf, (DWORD_PTR)in_callback, a, CALLBACK_FUNCTION);
	         if (err != MMSYSERR_NOERROR) {
	           text_color_set(DW_COLOR_ERROR);
	           dw_printf ("Could not open audio device for input.\n");
	           return (-1);
	         }	  


	         /*
	          * Set up the input buffers.
	          */

	         memset ((void*)(A->in_wavehdr), 0, sizeof(A->in_wavehdr));

	         for (n = 0; n < NUM_OUT_BUF; n++) {
	           A->in_wavehdr[n].dwBufferLength = A->outbuf_size;  /* all the same size */
	           A->in_wavehdr[n].lpData = malloc(A->outbuf_size);
	         }
	         A->in_headp = NULL;			

	         /*
	          * Give them to the sound input system.
	          */
	
	         for (n = 0; n < NUM_OUT_BUF; n++) {
	           waveInPrepareHeader(A->audio_in_handle, &(A->in_wavehdr[n]), sizeof(WAVEHDR));
	           waveInAddBuffer(A->audio_in_handle, &(A->in_wavehdr[n]), sizeof(WAVEHDR));
	         }

	         /*
	          * Start it up.
	          * The callback function is called when one is filled.
	          */

	         waveInStart (A->audio_in_handle);
	         break;

/*
 * UDP.
 */
	       case AUDIO_IN_TYPE_SDR_UDP:

	         {
	           WSADATA wsadata;
	           struct sockaddr_in si_me;
	           //int slen=sizeof(si_me);
	           //int data_size = 0;
	           int err;

	           err = WSAStartup (MAKEWORD(2,2), &wsadata);
	           if (err != 0) {
	               text_color_set(DW_COLOR_ERROR);
	               dw_printf("WSAStartup failed: %d\n", err);
	               return (-1);
	           }

	           if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
	             text_color_set(DW_COLOR_ERROR);
                     dw_printf("Could not find a usable version of Winsock.dll\n");
                     WSACleanup();
                     return (-1);
	           }

	           // Create UDP Socket

	           A->udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	           if (A->udp_sock == INVALID_SOCKET) {
	             text_color_set(DW_COLOR_ERROR);
	             dw_printf ("Couldn't create socket, errno %d\n", WSAGetLastError());
	             return -1;
	           }

	           memset((char *) &si_me, 0, sizeof(si_me));
	           si_me.sin_family = AF_INET;   
	           si_me.sin_port = htons((short)atoi(pa->adev[a].adevice_in + 4));
	           si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	           // Bind to the socket

	           if (bind(A->udp_sock, (SOCKADDR *) &si_me, sizeof(si_me)) != 0) {
	             text_color_set(DW_COLOR_ERROR);
	             dw_printf ("Couldn't bind socket, errno %d\n", WSAGetLastError());
	             return -1;
	           }
	           A->stream_next= 0;
	           A->stream_len = 0;
	         }

	         break;

/* 
 * stdin.
 */
   	       case AUDIO_IN_TYPE_STDIN:

  	         setmode (STDIN_FILENO, _O_BINARY);
	         A->stream_next= 0;
	         A->stream_len = 0;

	         break;

	       default:

	         text_color_set(DW_COLOR_ERROR);
	         dw_printf ("Internal error, invalid audio_in_type\n");
	         return (-1);
  	     }

	  }
    	}

	return (0);

} /* end audio_open */



/*
 * Called when input audio block is ready.
 */

static void CALLBACK in_callback (HWAVEIN handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{

	//dw_printf ("in_callback, handle = %p, msg = %d, instance = %I64d\n", handle, msg, instance);

	int a = instance;
	assert (a >= 0 && a < MAX_ADEVS);
	struct adev_s *A = &(adev[a]);

	if (msg == WIM_DATA) {
	
	  WAVEHDR *p = (WAVEHDR*)param1;
	  
	  p->dwUser = 0x5a5a5a5a;	/* needs to be unprepared. */
					/* dwUser can be 32 or 64 bit unsigned int. */
	  p->lpNext = NULL;

	  // dw_printf ("dwBytesRecorded = %ld\n", p->dwBytesRecorded);

	  EnterCriticalSection (&(A->in_cs));

	  if (A->in_headp == NULL) {
	    A->in_headp = p;		/* first one in list */
	  }
	  else {
	    WAVEHDR *last = (WAVEHDR*)(A->in_headp);

	    while (last->lpNext != NULL) {
	      last = last->lpNext;
	    }
	    last->lpNext = p;		/* append to last one */
	  }

	  LeaveCriticalSection (&(A->in_cs));
	}
}

/*
 * Called when output system is done with a block and it
 * is again available for us to fill.
 */


static void CALLBACK out_callback (HWAVEOUT handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
	if (msg == WOM_DONE) {   

	  WAVEHDR *p = (WAVEHDR*)param1;
	  
	  p->dwBufferLength = 0;
	  p->dwUser = DWU_DONE;
	}
}


/*------------------------------------------------------------------
 *
 * Name:        audio_get
 *
 * Purpose:     Get one byte from the audio device.
 *
 *
 * Inputs:	a	- Audio soundcard number.
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
	struct adev_s *A;
 
	WAVEHDR *p;
	int n;
	int sample;

        A = &(adev[a]); 

	switch (A->g_audio_in_type) {

/*
 * Soundcard.
 */
	  case AUDIO_IN_TYPE_SOUNDCARD:

	    while (1) {

	      /*
	       * Wait if nothing available.
	       * Could use an event to wake up but this is adequate.
	       */
	      int timeout = 25;

	      while (A->in_headp == NULL) {
	        //SLEEP_MS (ONE_BUF_TIME / 5);
	        SLEEP_MS (ONE_BUF_TIME);
	        timeout--;
	        if (timeout <= 0) {
	          text_color_set(DW_COLOR_ERROR);

// TODO1.2: Need more details.  Can we keep going?

	          dw_printf ("Timeout waiting for input from audio device %d.\n", a);

	          audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			0, 
			save_audio_config_p->statistics_interval);

	          return (-1);
	        }
	      }

	      p = (WAVEHDR*)(A->in_headp);		/* no need to be volatile at this point */

	      if (p->dwUser == 0x5a5a5a5a) {		// dwUser can be 32 or bit unsigned.
	        waveInUnprepareHeader(A->audio_in_handle, p, sizeof(WAVEHDR));
	        p->dwUser = 0;	/* Index for next byte. */

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			p->dwBytesRecorded / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);
	      }

	      if (p->dwUser < p->dwBytesRecorded) {
	        n = ((unsigned char*)(p->lpData))[p->dwUser++];
#if DEBUGx

	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("audio_get(): returns %d\n", n);

#endif
	        return (n);
	      }
	      /*
	       * Buffer is all used up.  Give it back to sound input system.
	       */

	      EnterCriticalSection (&(A->in_cs));
	      A->in_headp = p->lpNext;
	      LeaveCriticalSection (&(A->in_cs));

	      p->dwFlags = 0;
	      waveInPrepareHeader(A->audio_in_handle, p, sizeof(WAVEHDR));
	      waveInAddBuffer(A->audio_in_handle, p, sizeof(WAVEHDR));	  
	    }
	    break;
/*
 * UDP.
 */
	  case AUDIO_IN_TYPE_SDR_UDP:

	    while (A->stream_next >= A->stream_len) {
	      int res;

              assert (A->udp_sock > 0);

	      res = SOCK_RECV (A->udp_sock, A->stream_data, SDR_UDP_BUF_MAXLEN);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't read from udp socket, errno %d", WSAGetLastError());
	        A->stream_len = 0;
	        A->stream_next = 0;

	        audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			0, 
			save_audio_config_p->statistics_interval);

	        return (-1);
	      } 

	      audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			res / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);

	      A->stream_len = res;
	      A->stream_next = 0;
	    }
	    sample = A->stream_data[A->stream_next] & 0xff;
	    A->stream_next++;
	    return (sample);
	    break;
/* 
 * stdin.
 */
   	  case AUDIO_IN_TYPE_STDIN:

	    while (A->stream_next >= A->stream_len) {
	      int res;

	      res = read(STDIN_FILENO, A->stream_data, 1024);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("\nEnd of file on stdin.  Exiting.\n");
	        exit (0);
	      }

	      audio_stats (a, 
			save_audio_config_p->adev[a].num_channels, 
			res / (save_audio_config_p->adev[a].num_channels * save_audio_config_p->adev[a].bits_per_sample / 8), 
			save_audio_config_p->statistics_interval);
	    
	      A->stream_len = res;
	      A->stream_next = 0;
	    }
	    return (A->stream_data[A->stream_next++] & 0xff);
	    break;
  	}

	return (-1);

} /* end audio_get */


/*------------------------------------------------------------------
 *
 * Name:        audio_put
 *
 * Purpose:     Send one byte to the audio device.
 *
 * Inputs:	a	- Index for audio device.
 *
 *		c	- One byte in range of 0 - 255.
 *
 *
 * Global In:	out_current	- index of output buffer currently being filled.
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
	WAVEHDR *p;

	struct adev_s *A;
	A = &(adev[a]); 
	
/* 
 * Wait if no buffers are available.
 * Don't use p yet because compiler might might consider dwFlags a loop invariant. 
 */

	int timeout = 10;
	while ( A->out_wavehdr[A->out_current].dwUser == DWU_PLAYING) {
	  SLEEP_MS (ONE_BUF_TIME);
	  timeout--;
	  if (timeout <= 0) {
	    text_color_set(DW_COLOR_ERROR);

// TODO: open issues 78 & 165.  How can we avoid/improve this?

	    dw_printf ("Audio output failure waiting for buffer.\n");
	    dw_printf ("This can occur when we are producing audio output for\n");
	    dw_printf ("transmit and the operating system doesn't provide buffer\n");
	    dw_printf ("space after waiting and retrying many times.\n");
	    //dw_printf ("In recent years, this has been reported only when running the\n");
	    //dw_printf ("Windows version with VMWare on a Macintosh.\n");
	    ptt_term ();
	    return (-1);
	  }
	}

	p = (LPWAVEHDR)(&(A->out_wavehdr[A->out_current]));

	if (p->dwUser == DWU_DONE) {
	  waveOutUnprepareHeader (A->audio_out_handle, p, sizeof(WAVEHDR));
	  p->dwBufferLength = 0;
	  p->dwUser = DWU_FILLING;
	}

	/* Should never be full at this point. */

	assert (p->dwBufferLength >= 0);
	assert (p->dwBufferLength < (DWORD)(A->outbuf_size));

	p->lpData[p->dwBufferLength++] = c;

	if (p->dwBufferLength == (DWORD)(A->outbuf_size)) {
	  return (audio_flush(a));
	}

	return (0);

} /* end audio_put */


/*------------------------------------------------------------------
 *
 * Name:        audio_flush
 *
 * Purpose:     Send current buffer to the audio output system.
 *
 * Inputs:	a	- Index for audio device.
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
	WAVEHDR *p;
	MMRESULT e;
	struct adev_s *A;

	A = &(adev[a]); 
	
	p = (LPWAVEHDR)(&(A->out_wavehdr[A->out_current]));

	if (p->dwUser == DWU_FILLING && p->dwBufferLength > 0) {

	  p->dwUser = DWU_PLAYING;

	  waveOutPrepareHeader(A->audio_out_handle, p, sizeof(WAVEHDR));

	  e = waveOutWrite(A->audio_out_handle, p, sizeof(WAVEHDR));
	  if (e != MMSYSERR_NOERROR) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("audio out write error %d\n", e);

	    /* I don't expect this to ever happen but if it */
	    /* does, make the buffer available for filling. */

	    p->dwUser = DWU_DONE;
	    return (-1);
	  }
	  A->out_current = (A->out_current + 1) % NUM_OUT_BUF;
	}
	return (0);

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

} /* end audio_wait */


/*------------------------------------------------------------------
 *
 * Name:        audio_close
 *
 *
 * Purpose:     Close all of the audio devices.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 *
 *----------------------------------------------------------------*/

int audio_close (void)
{
	int err = 0;

	int n;

	int a;

    	for (a=0; a<MAX_ADEVS; a++) {
      	  if (save_audio_config_p->adev[a].defined) {

            struct adev_s *A = &(adev[a]);

	    assert (A->audio_in_handle != 0);
	    assert (A->audio_out_handle != 0);

	    audio_wait (a);

/* Shutdown audio input. */

	    waveInReset(A->audio_in_handle); 
	    waveInStop(A->audio_in_handle);
	    waveInClose(A->audio_in_handle);
	    A->audio_in_handle = 0;

	    for (n = 0; n < NUM_IN_BUF; n++) {

	      waveInUnprepareHeader (A->audio_in_handle, (LPWAVEHDR)(&(A->in_wavehdr[n])), sizeof(WAVEHDR));
	      A->in_wavehdr[n].dwFlags = 0;
	      free (A->in_wavehdr[n].lpData);
 	      A->in_wavehdr[n].lpData = NULL;
	    }

	    DeleteCriticalSection (&(A->in_cs));


/* Make sure all output buffers have been played then free them. */

	    for (n = 0; n < NUM_OUT_BUF; n++) {
	      if (A->out_wavehdr[n].dwUser == DWU_PLAYING) {

	        int timeout = 2 * NUM_OUT_BUF;
	        while (A->out_wavehdr[n].dwUser == DWU_PLAYING) {
	          SLEEP_MS (ONE_BUF_TIME);
	          timeout--;
	          if (timeout <= 0) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("Audio output failure on close.\n");
	          }
	        }

	        waveOutUnprepareHeader (A->audio_out_handle, (LPWAVEHDR)(&(A->out_wavehdr[n])), sizeof(WAVEHDR));

	        A->out_wavehdr[n].dwUser = DWU_DONE;
	      }
	      free (A->out_wavehdr[n].lpData);
 	      A->out_wavehdr[n].lpData = NULL;
	    }

	    waveOutClose (A->audio_out_handle);
	    A->audio_out_handle = 0;

          }  /* if device configured */
        }  /* for each device. */

        /* Not right.  always returns 0 but at this point, doesn't matter. */

	return (err);

} /* end audio_close */

/* end audio_win.c */


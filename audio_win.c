
#define DEBUGUDP 1


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2012,2013  John Langner, WB2OSZ
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
 *
 *		This version uses the native Windows sound interface.
 *
 * Credits:	Fabrice FAURE contributed Linux code for the SDR UDP interface.
 *
 *		Discussion here:  http://gqrx.dk/doc/streaming-audio-over-udp
 *
 *---------------------------------------------------------------*/


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>

#include <windows.h>		
#include <mmsystem.h>

#ifndef WAVE_FORMAT_96M16
#define WAVE_FORMAT_96M16 0x40000
#define WAVE_FORMAT_96S16 0x80000
#endif

#include <winsock2.h>
#define _WIN32_WINNT 0x0501
#include <ws2tcpip.h>


#include "direwolf.h"
#include "audio.h"
#include "textcolor.h"
#include "ptt.h"



/* 
 * Allocate enough buffers for 1 second each direction. 
 * Each buffer size is a trade off between being responsive 
 * to activity on the channel vs. overhead of having too
 * many little transfers.
 */

#define TOTAL_BUF_TIME 1000	
#define ONE_BUF_TIME 40
		
#define NUM_IN_BUF ((TOTAL_BUF_TIME)/(ONE_BUF_TIME))
#define NUM_OUT_BUF ((TOTAL_BUF_TIME)/(ONE_BUF_TIME))

static enum audio_in_type_e audio_in_type;

/*
 * UDP socket for receiving audio stream.
 * Buffer, length, and pointer for UDP or stdin.
 */

static SOCKET udp_sock;
static char stream_data[SDR_UDP_BUF_MAXLEN];
static int stream_len;
static int stream_next;


#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)
#define calcbufsize(rate,chans,bits) roundup1k( ( (rate)*(chans)*(bits) / 8 * ONE_BUF_TIME)/1000  )


/* For sound output. */
/* out_wavehdr.dwUser is used to keep track of output buffer state. */

#define DWU_FILLING 1		/* Ready to use or in process of being filled. */
#define DWU_PLAYING 2		/* Was given to sound system for playing. */
#define DWU_DONE 3		/* Sound system is done with it. */

static HWAVEOUT audio_out_handle = 0;

static volatile WAVEHDR out_wavehdr[NUM_OUT_BUF];
static int out_current;		/* index to above. */
static int outbuf_size;


/* For sound input. */
/* In this case dwUser is index of next available byte to remove. */

static HWAVEIN  audio_in_handle = 0;
static WAVEHDR in_wavehdr[NUM_IN_BUF];
static volatile WAVEHDR *in_headp;	/* head of queue to process. */
static CRITICAL_SECTION in_cs;







/*------------------------------------------------------------------
 *
 * Name:        print_capabilities
 *
 * Purpose:     Display capabilities of the available audio devices.
 *
 * Example:
 * 
 *
 *   Available audio input devices for receive (*=selected):
 *     0: Microphone (Realtek High Defini  mono: 11 22 44 96  stereo: 11 22 44 96
 *     1: Microphone (Bluetooth SCO Audio  mono: 11 22 44 96  stereo: 11 22 44 96
 *     2: Microphone (Bluetooth AV Audio)  mono: 11 22 44 96  stereo: 11 22 44 96
 *     3: Microphone (USB PnP Sound Devic  mono: 11 22 44 96  stereo: 11 22 44 96
 *   Available audio output devices for transmit (*=selected):
 *     0: Speakers (Realtek High Definiti  mono: 11 22 44 96  stereo: 11 22 44 96
 *     1: Speakers (Bluetooth SCO Audio)   mono: 11 22 44 96  stereo: 11 22 44 96
 *     2: Realtek Digital Output (Realtek  mono: 11 22 44 96  stereo: 11 22 44 96
 *     3: Realtek Digital Output(Optical)  mono: 11 22 44 96  stereo: 11 22 44 96
 *     4: Speakers (Bluetooth AV Audio)    mono: 11 22 44 96  stereo: 11 22 44 96
 *     5: Speakers (USB PnP Sound Device)  mono: 11 22 44 96  stereo: 11 22 44 96	
 *
 *
 * History:	Removed in version 0.9.
 *
 * Post Mortem discussion:
 *
 *		It turns out to be quite bogus and perhaps deceiving.
 *
 *		The chip (http://www.szlnst.com/Uploadfiles/HS100.pdf) in the cheap 
 *		USB Audio device is physically capable of only 44.1 and 48 KHz
 *		sampling rates.  Input is mono only.  Output is stereo only.
 *		There is discussion of this in the Raspberry Pi document.
 *
 *		Here, it looks like it has much more general capabilities.
 *		It seems the audio system puts some virtual layer on top of
 *		it to provide resampling for different rates and silent 
 *		right channel for stereo input.
 *
 *		
 *----------------------------------------------------------------*/

#if 0
static void print_capabilities (DWORD formats) 
{
	dw_printf ("  mono:");
	dw_printf ("%s", (formats & WAVE_FORMAT_1M16) ? " 11" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_2M16) ? " 22" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_4M16) ? " 44" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_96M16) ? " 96" : "   ");

	dw_printf ("  stereo:");
	dw_printf ("%s", (formats & WAVE_FORMAT_1S16) ? " 11" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_2S16) ? " 22" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_4S16) ? " 44" : "   ");
	dw_printf ("%s", (formats & WAVE_FORMAT_96S16) ? " 96" : "   ");
}
#endif



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
 *				The sofware modem must use this ACTUAL information
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


static void CALLBACK in_callback (HWAVEIN handle, UINT msg, DWORD instance, DWORD param1, DWORD param2);
static void CALLBACK out_callback (HWAVEOUT handle, UINT msg, DWORD instance, DWORD param1, DWORD param2);

int audio_open (struct audio_s *pa)
{
	int err;
	int chan;
	int n;
	int in_dev_no;
	int out_dev_no;

	WAVEFORMATEX wf;

	int num_devices;
	WAVEINCAPS wic;
	WAVEOUTCAPS woc;

	assert (audio_in_handle == 0);
	assert (audio_out_handle == 0);


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

	wf.wFormatTag = WAVE_FORMAT_PCM;
	wf.nChannels = pa -> num_channels; 
	wf.nSamplesPerSec = pa -> samples_per_sec;
	wf.wBitsPerSample = pa -> bits_per_sample;
	wf.nBlockAlign = (wf.wBitsPerSample / 8) * wf.nChannels;
	wf.nAvgBytesPerSec = wf.nBlockAlign * wf.nSamplesPerSec;
	wf.cbSize = 0;

	outbuf_size = calcbufsize(wf.nSamplesPerSec,wf.nChannels,wf.wBitsPerSample);


	udp_sock = INVALID_SOCKET;

	in_dev_no = WAVE_MAPPER;	/* = -1 */
	out_dev_no = WAVE_MAPPER;

/*
 * Determine the type of audio input and select device.
 */
	
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
	else {
	  audio_in_type = AUDIO_IN_TYPE_SOUNDCARD; 	

	  /* Does config file have a number?  */
	  /* If so, it is an index into list of devices. */

	  if (strlen(pa->adevice_in) == 1 && isdigit(pa->adevice_in[0])) {
	    in_dev_no = atoi(pa->adevice_in);
	  }

	  /* Otherwise, does it have search string? */

	  if (in_dev_no == WAVE_MAPPER && strlen(pa->adevice_in) >= 1) {
	    num_devices = waveInGetNumDevs();
	    for (n=0 ; n<num_devices && in_dev_no == WAVE_MAPPER ; n++) {
	      if ( ! waveInGetDevCaps(n, &wic, sizeof(WAVEINCAPS))) {
	        if (strstr(wic.szPname, pa->adevice_in) != NULL) {
	          in_dev_no = n;
	        }
	      }
	    }
	    if (in_dev_no == WAVE_MAPPER) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\"%s\" doesn't match any of the input devices.\n", pa->adevice_in);
	    }
	  }
 	}

/*
 * Select output device.
 */
	if (strlen(pa->adevice_out) == 1 && isdigit(pa->adevice_out[0])) {
	  out_dev_no = atoi(pa->adevice_out);
	}

	if (out_dev_no == WAVE_MAPPER && strlen(pa->adevice_out) >= 1) {
	  num_devices = waveOutGetNumDevs();
	  for (n=0 ; n<num_devices && out_dev_no == WAVE_MAPPER ; n++) {
	    if ( ! waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS))) {
	      if (strstr(woc.szPname, pa->adevice_out) != NULL) {
	        out_dev_no = n;
	      }
	    }
	  }
	  if (out_dev_no == WAVE_MAPPER) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\"%s\" doesn't match any of the output devices.\n", pa->adevice_out);
	  }
	}
 

/*
 * Display what is available and anything selected.
 */
	text_color_set(DW_COLOR_INFO);
	dw_printf ("Available audio input devices for receive (*=selected):\n");

	num_devices = waveInGetNumDevs();
	if (in_dev_no < -1 || in_dev_no >= num_devices) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid input (receive) audio device number %d.\n", in_dev_no);
	  in_dev_no = WAVE_MAPPER;
	}
	text_color_set(DW_COLOR_INFO);
	for (n=0; n<num_devices; n++) {
	  
	  if ( ! waveInGetDevCaps(n, &wic, sizeof(WAVEINCAPS))) {
	    dw_printf ("%c %d: %s\n", n==in_dev_no ? '*' : ' ', n, wic.szPname);
	    //dw_printf ("%c %d: %-31s\n", n==in_dev_no ? '*' : ' ', n, wic.szPname);
	    //print_capabilities (wic.dwFormats);
	    //dw_printf ("\n");
	  }
	}

	/* Display stdin or udp:port if appropriate. */

	if (audio_in_type != AUDIO_IN_TYPE_SOUNDCARD) {
	  dw_printf ("*    %s\n", pa->adevice_in);
	}

	dw_printf ("Available audio output devices for transmit (*=selected):\n");

	/* TODO? */
	/* No "*" is currently displayed when using the default device. */
	/* Should we put "*" next to the default device when using it? */
	/* Which is the default?  The first one? */

	num_devices = waveOutGetNumDevs();
	if (out_dev_no < -1 || out_dev_no >= num_devices) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid output (transmit) audio device number %d.\n", out_dev_no);
	  out_dev_no = WAVE_MAPPER;
	}
	text_color_set(DW_COLOR_INFO);
	for (n=0; n<num_devices; n++) {
	  
	  if ( ! waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS))) {
	    dw_printf ("%c %d: %s\n", n==out_dev_no ? '*' : ' ', n, woc.szPname);
	    //dw_printf ("%c %d: %-31s\n", n==out_dev_no ? '*' : ' ', n, woc.szPname);
	    //print_capabilities (woc.dwFormats);
	    //dw_printf ("\n");
	  }
	}

	err = waveOutOpen (&audio_out_handle, out_dev_no, &wf, (DWORD_PTR)out_callback, 0, CALLBACK_FUNCTION);
	if (err != MMSYSERR_NOERROR) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not open audio device for output.\n");
	  return (-1);
	}	  

/*
 * Set up the output buffers.
 * We use dwUser to indicate it is available for filling.
 */

	memset ((void*)out_wavehdr, 0, sizeof(out_wavehdr));

	for (n = 0; n < NUM_OUT_BUF; n++) {
	  out_wavehdr[n].lpData = malloc(outbuf_size);
	  out_wavehdr[n].dwUser = DWU_FILLING;	
	  out_wavehdr[n].dwBufferLength = 0;
	}
	out_current = 0;			

	
/*
 * Open audio input device.
 */

	switch (audio_in_type) {

/*
 * Soundcard.
 */
	  case AUDIO_IN_TYPE_SOUNDCARD:

	    InitializeCriticalSection (&in_cs);

	    err = waveInOpen (&audio_in_handle, in_dev_no, &wf, (DWORD_PTR)in_callback, 0, CALLBACK_FUNCTION);
	    if (err != MMSYSERR_NOERROR) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Could not open audio device for input.\n");
	      return (-1);
	    }	  


	    /*
	     * Set up the input buffers.
	     */

	    memset ((void*)in_wavehdr, 0, sizeof(in_wavehdr));

	    for (n = 0; n < NUM_OUT_BUF; n++) {
	      in_wavehdr[n].dwBufferLength = outbuf_size;  /* all the same size */
	      in_wavehdr[n].lpData = malloc(outbuf_size);
	    }
	    in_headp = NULL;			

	    /*
	     * Give them to the sound input system.
	     */
	
	    for (n = 0; n < NUM_OUT_BUF; n++) {
	      waveInPrepareHeader(audio_in_handle, &(in_wavehdr[n]), sizeof(WAVEHDR));
	      waveInAddBuffer(audio_in_handle, &(in_wavehdr[n]), sizeof(WAVEHDR));
	    }

	    /*
	     * Start it up.
	     * The callback function is called when one is filled.
	     */

	    waveInStart (audio_in_handle);
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

	      udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	      if (udp_sock == INVALID_SOCKET) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Couldn't create socket, errno %d\n", WSAGetLastError());
	        return -1;
	      }

	      memset((char *) &si_me, 0, sizeof(si_me));
	      si_me.sin_family = AF_INET;   
	      si_me.sin_port = htons((short)atoi(pa->adevice_in + 4));
	      si_me.sin_addr.s_addr = htonl(INADDR_ANY);

	      // Bind to the socket

	      if (bind(udp_sock, (SOCKADDR *) &si_me, sizeof(si_me)) != 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Couldn't bind socket, errno %d\n", WSAGetLastError());
	        return -1;
	      }
	      stream_next= 0;
	      stream_len = 0;
	    }

	    break;

/* 
 * stdin.
 */
   	  case AUDIO_IN_TYPE_STDIN:

  	    setmode (STDIN_FILENO, _O_BINARY);
	    stream_next= 0;
	    stream_len = 0;

	    break;

	  default:

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error, invalid audio_in_type\n");
	    return (-1);
  	}

	return (0);

} /* end audio_open */



/*
 * Called when input audio block is ready.
 */

static void CALLBACK in_callback (HWAVEIN handle, UINT msg, DWORD instance, DWORD param1, DWORD param2)
{
	if (msg == WIM_DATA) {
	
	  WAVEHDR *p = (WAVEHDR*)param1;
	  
	  p->dwUser = -1;		/* needs to be unprepared. */
	  p->lpNext = NULL;

	  EnterCriticalSection (&in_cs);

	  if (in_headp == NULL) {
	    in_headp = p;		/* first one in list */
	  }
	  else {
	    WAVEHDR *last = (WAVEHDR*)in_headp;

	    while (last->lpNext != NULL) {
	      last = last->lpNext;
	    }
	    last->lpNext = p;		/* append to last one */
	  }

	  LeaveCriticalSection (&in_cs);
	}
}

/*
 * Called when output system is done with a block and it
 * is again available for us to fill.
 */


static void CALLBACK out_callback (HWAVEOUT handle, UINT msg, DWORD instance, DWORD param1, DWORD param2)
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
	WAVEHDR *p;
	int n;
	int sample;

#if DEBUGUDP
	/* Gather numbers for read from UDP stream. */

	static int duration = 100;	/* report every 100 seconds. */
	static time_t last_time = 0;
	time_t this_time;
	static int sample_count;
	static int error_count;
#endif

	switch (audio_in_type) {

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

	      while (in_headp == NULL) {
	        SLEEP_MS (ONE_BUF_TIME / 5);
	        timeout--;
	        if (timeout <= 0) {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Audio input failure.\n");
	          return (-1);
	        }
	      }

	      p = (WAVEHDR*)in_headp;		/* no need to be volatile at this point */

	      if (p->dwUser == -1) {
	        waveInUnprepareHeader(audio_in_handle, p, sizeof(WAVEHDR));
	        p->dwUser = 0;	/* Index for next byte. */
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

	      EnterCriticalSection (&in_cs);
	      in_headp = p->lpNext;
	      LeaveCriticalSection (&in_cs);

	      p->dwFlags = 0;
	      waveInPrepareHeader(audio_in_handle, p, sizeof(WAVEHDR));
	      waveInAddBuffer(audio_in_handle, p, sizeof(WAVEHDR));	  
	    }
	    break;
/*
 * UDP.
 */
	  case AUDIO_IN_TYPE_SDR_UDP:

	    while (stream_next >= stream_len) {
	      int res;

              assert (udp_sock > 0);

	      res = recv (udp_sock, stream_data, SDR_UDP_BUF_MAXLEN, 0);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't read from udp socket, errno %d", WSAGetLastError());
	        stream_len = 0;
	        stream_next = 0;
	        return (-1);
	      } 

#if DEBUGUDP
	      if (last_time == 0) {
	        last_time = time(NULL);
	        sample_count = 0;
	        error_count = 0;
	      }
	      else {
	        if (res > 0) {
	           sample_count += res/2;
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
	      stream_len = res;
	      stream_next = 0;
	    }
	    sample = stream_data[stream_next] & 0xff;
	    stream_next++;
	    return (sample);
	    break;
/* 
 * stdin.
 */
   	  case AUDIO_IN_TYPE_STDIN:

	    while (stream_next >= stream_len) {
	      int res;

	      res = read(STDIN_FILENO, stream_data, 1024);
	      if (res <= 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf ("\nEnd of file on stdin.  Exiting.\n");
	        exit (0);
	      }
	    
	      stream_len = res;
	      stream_next = 0;
	    }
	    return (stream_data[stream_next++] & 0xff);
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
 * Inputs:	c	- One byte in range of 0 - 255.
 *
 *
 * Global In:	out_current	- index of output buffer currenly being filled.
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
	WAVEHDR *p;
	
/* 
 * Wait if no buffers are available.
 * Don't use p yet because compiler might might consider dwFlags a loop invariant. 
 */

	int timeout = 10;
	while ( out_wavehdr[out_current].dwUser == DWU_PLAYING) {
	  SLEEP_MS (ONE_BUF_TIME);
	  timeout--;
	  if (timeout <= 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Audio output failure waiting for buffer.\n");
	    ptt_term ();
	    return (-1);
	  }
	}

	p = (LPWAVEHDR)(&(out_wavehdr[out_current]));

	if (p->dwUser == DWU_DONE) {
	  waveOutUnprepareHeader (audio_out_handle, p, sizeof(WAVEHDR));
	  p->dwBufferLength = 0;
	  p->dwUser = DWU_FILLING;
	}

	/* Should never be full at this point. */

	assert (p->dwBufferLength >= 0);
	assert (p->dwBufferLength < outbuf_size);

	p->lpData[p->dwBufferLength++] = c;

	if (p->dwBufferLength == outbuf_size) {
	  return (audio_flush());
	}

	return (0);

} /* end audio_put */


/*------------------------------------------------------------------
 *
 * Name:        audio_flush
 *
 * Purpose:     Send current buffer to the audio output system.
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
	WAVEHDR *p;
	MMRESULT e;
	
	
	p = (LPWAVEHDR)(&(out_wavehdr[out_current]));

	if (p->dwUser == DWU_FILLING && p->dwBufferLength > 0) {

	  p->dwUser = DWU_PLAYING;

	  waveOutPrepareHeader(audio_out_handle, p, sizeof(WAVEHDR));

	  e = waveOutWrite(audio_out_handle, p, sizeof(WAVEHDR));
	  if (e != MMSYSERR_NOERROR) {
	    text_color_set (DW_COLOR_ERROR);
	    dw_printf ("audio out write error %d\n", e);

	    /* I don't expect this to ever happen but if it */
	    /* does, make the buffer available for filling. */

	    p->dwUser = DWU_DONE;
	    return (-1);
	  }
	  out_current = (out_current + 1) % NUM_OUT_BUF;
	}
	return (0);

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
 * Description:	In our particular application, we want to make sure
 *		that the entire packet has been sent out before turning
 *		off the transmitter PTT control.
 *
 * In an ideal world:
 * 
 *		We would like to ask the hardware when all the queued
 *		up sound has actually come out the speaker.
 *
 *		The original implementation (on Cygwin) tried using:
 *		
 *			ioctl (audio_device_fd, SNDCTL_DSP_SYNC, NULL);
 *
 *		but this caused the application to crash at a later time.
 *
 *		This might be revisited someday for the Windows version, 
 *		but for now, we continue to use the work-around because it
 *		works fine.
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
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("audio_wait(): before sync, fd=%d\n", audio_device_fd);	
#endif


	if (duration > 0) {
	  SLEEP_MS (duration);
	}

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

	int n;


	assert (audio_in_handle != 0);
	assert (audio_out_handle != 0);

	audio_wait (0);

/* Shutdown audio input. */

	waveInReset(audio_in_handle); 
	waveInStop(audio_in_handle);
	waveInClose(audio_in_handle);
	audio_in_handle = 0;

	for (n = 0; n < NUM_IN_BUF; n++) {

	  waveInUnprepareHeader (audio_in_handle, (LPWAVEHDR)(&(in_wavehdr[n])), sizeof(WAVEHDR));
	  in_wavehdr[n].dwFlags = 0;
	  free (in_wavehdr[n].lpData);
 	  in_wavehdr[n].lpData = NULL;
	}

	DeleteCriticalSection (&in_cs);


/* Make sure all output buffers have been played then free them. */

	for (n = 0; n < NUM_OUT_BUF; n++) {
	  if (out_wavehdr[n].dwUser == DWU_PLAYING) {

	    int timeout = 2 * NUM_OUT_BUF;
	    while (out_wavehdr[n].dwUser == DWU_PLAYING) {
	      SLEEP_MS (ONE_BUF_TIME);
	      timeout--;
	      if (timeout <= 0) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Audio output failure on close.\n");
	      }
	    }

	    waveOutUnprepareHeader (audio_out_handle, (LPWAVEHDR)(&(out_wavehdr[n])), sizeof(WAVEHDR));

	    out_wavehdr[n].dwUser = DWU_DONE;
	  }
	  free (out_wavehdr[n].lpData);
 	  out_wavehdr[n].lpData = NULL;
	}

	waveOutClose (audio_out_handle);
	audio_out_handle = 0;

	return (err);

} /* end audio_close */

/* end audio_win.c */


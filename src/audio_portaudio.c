
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015 John Langner, WB2OSZ
//    Copyright (C) 2015 Robert Stiles, KK5VD
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
 * Module:  audio_portaudio.c
 *
 * Purpose: Interface to audio device commonly called a "sound card" for
 *          historical reasons.
 *
 * This version is for Various OS' using Port Audio
 *
 * Major Revisions:
 *
 *		1.2 - Add ability to use more than one audio device.
 *		1.3 - New file added for Port Audio for Mac and possibly others.
 *
 *---------------------------------------------------------------*/

#if	defined(USE_PORTAUDIO)

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
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
#include <pthread.h>

#include "audio.h"
#include "audio_stats.h"
#include "textcolor.h"
#include "dtime_now.h"
#include "demod.h"		/* for alevel_t & demod_get_audio_level() */

#include "portaudio.h"

/* Audio configuration. */

static struct audio_s          *save_audio_config_p;

/* Current state for each of the audio devices. */

static struct adev_s {

	pthread_mutex_t input_mutex;
	pthread_cond_t  input_cond;

	PaStream *inStream;
	PaStreamParameters inputParameters;
	int pa_input_device_number;
	int no_of_input_channels;
	int input_finished;
	int input_pause;
	int input_flush;

	void *audio_in_handle;
	int inbuf_size_in_bytes;	  /* number of bytes allocated */
	unsigned char *inbuf_ptr;
	int inbuf_len;				  /* number byte of actual data available. */
	int inbuf_next;				  /* index of next to remove. */
	int inbuf_bytes_per_frame;	  /* number of bytes for a sample from all channels. */
	int inbuf_frames_per_buffer;  /* number of frames in a buffer. */

	pthread_mutex_t output_mutex;
	pthread_cond_t  output_cond;

	PaStream *outStream;
	PaStreamParameters outputParameters;
	int pa_output_device_number;
	int no_of_output_channels;
	int output_pause;
	int output_finished;
	int output_flush;
	int output_wait_flag;

	void *audio_out_handle;
	int outbuf_size_in_bytes;
	unsigned char *outbuf_ptr;
	int outbuf_len;
	int outbuf_next;			  /* index of next to remove. */
	int outbuf_bytes_per_frame;   /* number of bytes for a sample from all channels. */
	int outbuf_frames_per_buffer; /* number of frames in a buffer. */

	enum audio_in_type_e g_audio_in_type;

	int udp_sock;			      /* UDP socket for receiving data */

} adev[MAX_ADEVS];

// Originally 40.  Version 1.2, try 10 for lower latency.

#define ONE_BUF_TIME 10
#define SAMPLE_SILENCE 0

#define PA_INPUT  1
#define PA_OUTPUT 2

#define roundup1k(n) (((n) + 0x3ff) & ~0x3ff)

#undef FOR_FUTURE_USE

static int set_portaudio_params (int a, struct adev_s *dev, struct audio_s *pa, char *devname, char *inout);
static void print_pa_devices(void);
static int check_pa_configure(struct adev_s *dev, int sample_rate);
static void list_supported_sample_rates(struct adev_s *dev);
static int pa_devNN(char *deviceStr, char *_devName, size_t length, int *_devNo);
static int searchPADevice(struct adev_s *dev, char *_devName, int reqDeviceNo, int io_flag);
static int calcbufsize(int rate, int chans, int bits);


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
 * Search the portaudio device tree looking for the request device.
 * One of the issues with portaudio has to do with devices returning
 * the same device name for more then one connected device
 * (ie two SignaLinks). Appending a Portaudio device index to the
 * the device name ensure we can find the correct one. And if it's not
 * available return the first occurrence that matches the device name.
 *----------------------------------------------------------------*/
static int searchPADevice(struct adev_s *dev, char *_devName, int reqDeviceNo, int io_flag)
{
	int numDevices = Pa_GetDeviceCount();
	const PaDeviceInfo * di = (PaDeviceInfo *)0;
	int i = 0;

	// First check to see if the requested index matches the device name.
	if(reqDeviceNo < numDevices) {
		di = Pa_GetDeviceInfo((PaDeviceIndex) reqDeviceNo);
		if(strncmp(di->name, _devName, 80) == 0) {
			if((io_flag == PA_INPUT) && di->maxInputChannels)
				return reqDeviceNo;
			if((io_flag == PA_OUTPUT) && di->maxOutputChannels)
				return reqDeviceNo;
		}
	}

	// Requested device index doesn't match device name. Search for one.
	for(i = 0; i < numDevices; i++) {
		di = Pa_GetDeviceInfo((PaDeviceIndex) i);
		if(strncmp(di->name, _devName, 80) == 0) {
			if((io_flag == PA_INPUT) && di->maxInputChannels)
				return i;
			if((io_flag == PA_OUTPUT) && di->maxOutputChannels)
				return i;
		}
	}

	// No Matches found
	return -1;
}

/*------------------------------------------------------------------
 * Extract device name and number.
 *----------------------------------------------------------------*/
static int pa_devNN(char *deviceStr, char *_devName, size_t length, int *_devNo)
{
	char *cPtr = (char *)0;
	char cVal = 0;
	int count = 0;
	char numStr[8];

	if(!deviceStr || !_devName || !_devNo) {
		dw_printf( "Internal Error: Func %s passed null pointer.\n", __func__);
		return -1;
	}

	cPtr = deviceStr;

	memset(_devName, 0, length);
	memset(numStr, 0, sizeof(numStr));

	while(*cPtr) {
		cVal = *cPtr++;
		if(cVal == ':')  break;

		// See Issue 417.
		// Originally this copied only printable ASCII characters (space thru ~).
		// That is a problem for some locales that use UTF-8 characters in the device name.
		// original: if(((cVal >= ' ') && (cVal <= '~')) && (count < length)) {

		// At first I was thinking we should keep the test for < ' ' but then I
		// remembered that char type can be signed or unsigned depending on implementation.
		// If characters are signed then a value above 0x7f would be considered negative.

		// It seems to me that the test for buffer full is off by one.
		// count could reach length, leaving no room for a nul terminator.
		// Compare has been changed so count is limited to length minus 1.

		if(count < length - 1) {
			_devName[count++] = cVal;
		}

	}

	count = 0;

	while(*cPtr) {
		cVal = *cPtr++;
		if(isdigit(cVal) && (count < (sizeof(numStr) - 1))) {
			numStr[count++] = cVal;
		}
	}

	if(numStr[0] == 0) {
		*_devNo = 0;
	} else {
		sscanf(numStr, "%d", _devNo);
	}

	return 0;
}

/*------------------------------------------------------------------
 * List the supported sample rates.
 *----------------------------------------------------------------*/
static void list_supported_sample_rates(struct adev_s *dev)
{
	static double standardSampleRates[] = {
		8000.0, 9600.0, 11025.0, 12000.0, 16000.0, 22050.0, 24000.0, 32000.0,
		44100.0, 48000.0, 88200.0, 96000.0, 192000.0, -1 /* negative terminated  list */
	};
	int     i, printCount;
	PaError err;

	printCount = 0;
	for(i = 0; standardSampleRates[i] > 0; i++ ) {
		err = Pa_IsFormatSupported(&dev->inputParameters, &dev->outputParameters, standardSampleRates[i] );
		if( err == paFormatIsSupported ) {
			if( printCount == 0 ) {
				dw_printf( "\t%8.2f", standardSampleRates[i] );
				printCount = 1;
			}
			else if( printCount == 4 ) {
				dw_printf( ",\n\t%8.2f", standardSampleRates[i] );
				printCount = 1;
			}
			else {
				dw_printf( ", %8.2f", standardSampleRates[i] );
				++printCount;
			}
		}
	}

	if( !printCount )
		dw_printf( "None\n" );
	else
		dw_printf( "\n" );
}

/*------------------------------------------------------------------
 * Check PA Configure parameters.
 *----------------------------------------------------------------*/
static int check_pa_configure(struct adev_s *dev, int sample_rate)
{
	if(!dev) {
		dw_printf( "Internal Error: Func %s struct adev_s *dev null pointer.\n", __func__);
		return -1;
	}

	PaError err = 0;
	err = Pa_IsFormatSupported(&dev->inputParameters, &dev->outputParameters, sample_rate);
	if(err == paFormatIsSupported) return 0;
	dw_printf( "PortAudio Config Error: %s\n", Pa_GetErrorText(err));
	return err;
}

/*------------------------------------------------------------------
 * Print a list of device names and parameters
 *----------------------------------------------------------------*/
static void print_pa_devices(void)
{
	int     i, numDevices, defaultDisplayed;
	const   PaDeviceInfo *deviceInfo;

	numDevices = Pa_GetDeviceCount();

	if( numDevices < 0 ) {
		dw_printf( "ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
		return;
	}

	dw_printf( "Number of devices = %d\n", numDevices );

	for(i = 0; i < numDevices; i++ ) {
		deviceInfo = Pa_GetDeviceInfo( i );
		dw_printf( "--------------------------------------- device #%d\n", i );

		/* Mark global and API specific default devices */
		defaultDisplayed = 0;
		if( i == Pa_GetDefaultInputDevice() ) {
			dw_printf( "[ Default Input" );
			defaultDisplayed = 1;
		}
		else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultInputDevice ) {
			const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
			dw_printf( "[ Default %s Input", hostInfo->name );
			defaultDisplayed = 1;
		}

		if( i == Pa_GetDefaultOutputDevice() ) {
			dw_printf( (defaultDisplayed ? "," : "[") );
			dw_printf( " Default Output" );
			defaultDisplayed = 1;
		}
		else if( i == Pa_GetHostApiInfo( deviceInfo->hostApi )->defaultOutputDevice ) {
			const PaHostApiInfo *hostInfo = Pa_GetHostApiInfo( deviceInfo->hostApi );
			dw_printf( (defaultDisplayed ? "," : "[") );
			dw_printf( " Default %s Output", hostInfo->name );
			defaultDisplayed = 1;
		}

		if( defaultDisplayed )
			dw_printf( " ]\n" );

		/* print device info fields */
		dw_printf( "Name        = \"%s\"\n", deviceInfo->name );
		dw_printf( "Host API    = %s\n",     Pa_GetHostApiInfo( deviceInfo->hostApi )->name );
		dw_printf( "Max inputs  = %d\n",     deviceInfo->maxInputChannels  );
		dw_printf( "Max outputs = %d\n",     deviceInfo->maxOutputChannels  );
	}
}

/*------------------------------------------------------------------
 * Port Audio Input Callback
 *----------------------------------------------------------------*/
static int paInput16CB( const void *inputBuffer, void *outputBuffer,
					   unsigned long framesPerBuffer,
					   const PaStreamCallbackTimeInfo* timeInfo,
					   PaStreamCallbackFlags statusFlags,
					   void *userData )
{
	struct adev_s *data = (struct adev_s *) userData;
	const int16_t *rptr = (const int16_t *) inputBuffer;
	size_t framesToCalc = 0;
	size_t i = 0;
	int finished = 0;
	int word = 0;
	size_t bytes_left = data->inbuf_size_in_bytes - data->inbuf_len;
	size_t framesLeft = bytes_left / data->inbuf_bytes_per_frame;

	(void) outputBuffer; /* Prevent unused variable warnings. */
	(void) timeInfo;
	(void) statusFlags;
	(void) userData;

	if( framesLeft < framesPerBuffer ) {
		framesToCalc = framesLeft;
		finished = paComplete;
	} else {
		framesToCalc = framesPerBuffer;
		finished = paContinue;
	}

	if( inputBuffer == NULL || data->input_flush) {
		for(i = 0; i < framesToCalc; i++) {
			data->inbuf_ptr[data->inbuf_len++] = SAMPLE_SILENCE;
			data->inbuf_ptr[data->inbuf_len++] = SAMPLE_SILENCE;
			if(data->no_of_input_channels == 2) {
				data->inbuf_ptr[data->inbuf_len++] = SAMPLE_SILENCE;
				data->inbuf_ptr[data->inbuf_len++] = SAMPLE_SILENCE;
			}
		}
	} else {
		for(i = 0; i < framesToCalc; i++) {
			word = *rptr++;  /* left */
			data->inbuf_ptr[data->inbuf_len++] = word & 0xff;
			data->inbuf_ptr[data->inbuf_len++] = (word >> 8) & 0xff;

			if(data->no_of_input_channels == 2) {
				word = *rptr++;  /* right */
				data->inbuf_ptr[data->inbuf_len++] = word & 0xff;
				data->inbuf_ptr[data->inbuf_len++] = (word >> 8) & 0xff;
			}
		}
	}

	if((finished == paComplete) ||
	   (data->inbuf_len >= data->inbuf_size_in_bytes)) {
		pthread_cond_signal(&data->input_cond);
		finished = data->input_finished;
	}

	return finished;
}

#if FOR_FUTURE_USE
/*------------------------------------------------------------------
 * Port Audio Output Callback
 *----------------------------------------------------------------*/
static int paOutput16CB( const void *inputBuffer, void *outputBuffer,
						unsigned long framesPerBuffer,
						const PaStreamCallbackTimeInfo* timeInfo,
						PaStreamCallbackFlags statusFlags,
						void *userData)
{
	struct adev_s *data = (struct adev_s *) userData;
	int16_t *wptr = (int16_t *) outputBuffer;
	size_t i = 0;
	int finished = 0;
	size_t bytes_left = data->outbuf_size_in_bytes - data->outbuf_len;
	size_t framesLeft = bytes_left / data->outbuf_bytes_per_frame;
	int word = 0;

	(void) inputBuffer; /* Prevent unused variable warnings. */
	(void) timeInfo;
	(void) statusFlags;
	(void) userData;

	if(framesLeft && (framesLeft < framesPerBuffer)) {
		/* final buffer... */
		for(i = 0; i < framesLeft; i++ ) {
			word = data->outbuf_ptr[data->outbuf_len++] & 0xff;
			word |= (data->outbuf_ptr[data->outbuf_len++] << 8) & 0xff;
			*wptr++ = word;  /* left */
			if(data->no_of_output_channels == 2 ) {
				word = data->outbuf_ptr[data->outbuf_len++] & 0xff;
				word |= (data->outbuf_ptr[data->outbuf_len++] << 8) & 0xff;
				*wptr++ = word;  /* right */
			}
		}
		for( ; i < framesPerBuffer; i++ ) {
			*wptr++ = 0;  	/* left */
			if(data->no_of_output_channels == 2 )
				*wptr++ = 0;  /* right */
		}
		finished = paContinue;
	} else {
		for(i = 0; i < framesPerBuffer; i++ ) {
			word = data->outbuf_ptr[data->outbuf_len++] & 0xff;
			word |= (data->outbuf_ptr[data->outbuf_len++] << 8) & 0xff;
			*wptr++ = word;  /* left */
			if(data->no_of_output_channels == 2) {
				word = data->outbuf_ptr[data->outbuf_len++] & 0xff;
				word |= (data->outbuf_ptr[data->outbuf_len++] << 8) & 0xff;
				*wptr++ = word;  /* right */
			}
		}
		finished = paComplete;
	}

	if(data->output_flush) {
		data->output_flush = 0;
		finished = paComplete;
	}

	pthread_cond_signal(&data->output_cond);
	finished = data->output_finished;

	return finished;
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
 *				The device names are in adevice_in and adevice_out.
 *				   where c is the "card" (for historical purposes)
 *				   and d is the "device" within the "card."
 *
 *
 * Outputs:	pa  - The ACTUAL values are returned here.
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
	int err  = 0;
	int chan = 0;
	int a    = 0;
	int clear_value = 0;
	char audio_in_name[80];
	char audio_out_name[80];
	static int initalize_flag = 0;
	PaError paerr = paNoError;

	if(!initalize_flag) {
		paerr = Pa_Initialize();
		initalize_flag = -1;
	}

	if(paerr != paNoError ) return -1;

	save_audio_config_p = pa;

	memset (adev,           0, sizeof(adev));
	memset (audio_in_name,  0, sizeof(audio_in_name));
	memset (audio_out_name, 0, sizeof(audio_out_name));

	for (a = 0; a < MAX_ADEVS; a++) {
		adev[a].udp_sock = -1;
	}

	/*
	 * Fill in defaults for any missing values.
	 */

	for (a = 0; a < MAX_ADEVS; a++) {
		if (pa->adev[a].num_channels == 0)
			pa->adev[a].num_channels = DEFAULT_NUM_CHANNELS;

		if (pa->adev[a].samples_per_sec == 0)
			pa->adev[a].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

		if (pa->adev[a].bits_per_sample == 0)
			pa->adev[a].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;

		for (chan = 0; chan < MAX_RADIO_CHANS; chan++) {
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

	for (a = 0; a < MAX_ADEVS; a++) {
		if (pa->adev[a].defined) {

			adev[a].inbuf_size_in_bytes = 0;
			adev[a].outbuf_size_in_bytes = 0;

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
			} else {
				snprintf (ctemp, sizeof(ctemp), " (channel %d)", ADEVFIRSTCHAN(a));
			}

			text_color_set(DW_COLOR_INFO);

			if (strcmp(audio_in_name,audio_out_name) == 0) {
				dw_printf ("Audio device for both receive and transmit: %s %s\n", audio_in_name, ctemp);
			} else {
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

				case AUDIO_IN_TYPE_SOUNDCARD:
					print_pa_devices();
					err = set_portaudio_params (a, &adev[a], pa, audio_in_name, audio_out_name);
					if(err < 0) return -1;

					pthread_mutex_init(&adev[a].input_mutex, NULL);
					pthread_cond_init(&adev[a].input_cond, NULL);

					pthread_mutex_init(&adev[a].output_mutex, NULL);
					pthread_cond_init(&adev[a].output_cond, NULL);

					if(pa->adev[a].bits_per_sample == 8)
						clear_value = 128;
					else
						clear_value = 0;

					break;

					/*
					 * UDP.
					 */
				case AUDIO_IN_TYPE_SDR_UDP:

					// Create socket and bind socket

				{
					struct sockaddr_in si_me;
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
					//adev[a].inbuf_size_in_bytes = SDR_UDP_BUF_MAXLEN;

					break;

					/*
					 * stdin.
					 */
				case AUDIO_IN_TYPE_STDIN:

					/* Do we need to adjust any properties of stdin? */

					//adev[a].inbuf_size_in_bytes = 1024;

					break;

				default:

					text_color_set(DW_COLOR_ERROR);
					dw_printf ("Internal error, invalid audio_in_type\n");
					return (-1);
			}

			/*
			 * Finally allocate buffer for each direction.
			 */

	                /* Version 1.3 - Add sanity check on buffer size. */
	                /* There was a reported case of assert failure on buffer size in audio_get(). */

	                if (adev[a].inbuf_size_in_bytes < 256 || adev[a].inbuf_size_in_bytes > 32768) {
	                  text_color_set(DW_COLOR_ERROR);
	                  dw_printf ("Audio input buffer has unexpected extreme size of %d bytes.\n", adev[a].inbuf_size_in_bytes);
	                  dw_printf ("Detected at %s, line %d.\n", __FILE__, __LINE__);
	                  dw_printf ("This might be caused by unusual audio device configuration values.\n"); 
	                  adev[a].inbuf_size_in_bytes = 2048;
	                  dw_printf ("Using %d to attempt recovery.\n", adev[a].inbuf_size_in_bytes);
	                }

	                if (adev[a].outbuf_size_in_bytes < 256 || adev[a].outbuf_size_in_bytes > 32768) {
	                  text_color_set(DW_COLOR_ERROR);
	                  dw_printf ("Audio output buffer has unexpected extreme size of %d bytes.\n", adev[a].outbuf_size_in_bytes);
	                  dw_printf ("Detected at %s, line %d.\n", __FILE__, __LINE__);
	                  dw_printf ("This might be caused by unusual audio device configuration values.\n"); 
	                  adev[a].outbuf_size_in_bytes = 2048;
	                  dw_printf ("Using %d to attempt recovery.\n", adev[a].outbuf_size_in_bytes);
	                }

			adev[a].inbuf_ptr = malloc(adev[a].inbuf_size_in_bytes);
			assert (adev[a].inbuf_ptr != NULL);
			adev[a].inbuf_len = 0;
			adev[a].inbuf_next = 0;
			memset(adev[a].inbuf_ptr, clear_value, adev[a].inbuf_size_in_bytes);

			adev[a].outbuf_ptr = malloc(adev[a].outbuf_size_in_bytes);
			assert (adev[a].outbuf_ptr != NULL);
			adev[a].outbuf_len = 0;
			adev[a].outbuf_next = 0;
			memset(adev[a].outbuf_ptr, clear_value, adev[a].outbuf_size_in_bytes);

			if(adev[a].inStream) {
				err = Pa_StartStream(adev[a].inStream);
				if(err != paNoError) {
					dw_printf ("Input stream start Error %s\n", Pa_GetErrorText(err));
				}
			}

			if(adev[a].outStream) {
				err = Pa_StartStream(adev[a].outStream);
				if(err != paNoError) {
					dw_printf ("Output stream start Error %s\n", Pa_GetErrorText(err));
				}
			}
		} /* end of audio device defined */
	} /* end of for each audio device */

	return (0);

} /* end audio_open */


/*
 * Set parameters for sound card.
 *
 * See  ??  for details.
 */
static int set_portaudio_params (int a, struct adev_s *dev, struct audio_s *pa, char *_audio_in_name, char *_audio_out_name)
{
	int numDevices  = 0;
	int err = 0;
	int buffer_size = 0;
	int sampleFormat = 0;
	int no_of_bytes_per_sample = 0;
	int reqInDeviceNo  = 0;
	int reqOutDeviceNo = 0;
	char input_devName[80];
	char output_devName[80];

	text_color_set(DW_COLOR_ERROR);

	if(!dev || !pa || !_audio_in_name || !_audio_out_name) {
		dw_printf ("Internal error, invalid function parameter pointer(s) (null)\n");
		return -1;
	}

	if(_audio_in_name[0] == 0) {
		dw_printf ("Input device name null\n");
		return -1;
	}

	if(_audio_out_name[0] == 0) {
		dw_printf ("Output device name null\n");
		return -1;
	}

	numDevices = Pa_GetDeviceCount();
	if( numDevices < 0 ) {
		dw_printf( "ERROR: Pa_GetDeviceCount returned 0x%x\n", numDevices );
		return -1;
	}

	err = pa_devNN(_audio_in_name, input_devName, sizeof(input_devName), &reqInDeviceNo);
	if(err < 0)	return -1;

	reqInDeviceNo = searchPADevice(dev, input_devName, reqInDeviceNo, PA_INPUT);
	if(reqInDeviceNo < 0) {
		dw_printf ("Requested Input Audio Device not found %s.\n", input_devName);
		return -1;
	}

	err = pa_devNN(_audio_out_name, output_devName, sizeof(output_devName), &reqOutDeviceNo);
	if(err < 0)	return -1;

	reqOutDeviceNo = searchPADevice(dev, output_devName, reqOutDeviceNo, PA_OUTPUT);
	if(reqOutDeviceNo < 0) {
		dw_printf ("Requested Output Audio Device not found %s.\n", output_devName);
		return -1;
	}

	dev->pa_input_device_number  = reqInDeviceNo;
	dev->pa_output_device_number = reqOutDeviceNo;

	switch(pa->adev[a].bits_per_sample) {
		case 8:
			sampleFormat = paInt8;
			no_of_bytes_per_sample = sizeof(int8_t);
			assert("int8_t size not equal to 1" && sizeof(int8_t) == 1);
			break;

		case 16:
			sampleFormat = paInt16;
			no_of_bytes_per_sample = sizeof(int16_t);
			assert("int16_t size not equal to 2" && sizeof(int16_t) == 2);
			break;

		default:
			dw_printf ("Unsupported Sample Size %s.\n", output_devName);
			return -1;
	}


	buffer_size = calcbufsize(pa->adev[a].samples_per_sec, pa->adev[a].num_channels, pa->adev[a].bits_per_sample);

	dev->inbuf_size_in_bytes     = buffer_size;
	dev->inbuf_bytes_per_frame   = no_of_bytes_per_sample * pa->adev[a].num_channels;
	dev->inbuf_frames_per_buffer = dev->inbuf_size_in_bytes  / dev->inbuf_bytes_per_frame;

	dev->inputParameters.device       = dev->pa_input_device_number;
	dev->inputParameters.channelCount = pa->adev[a].num_channels;
	dev->inputParameters.sampleFormat = sampleFormat;
	dev->inputParameters.suggestedLatency = Pa_GetDeviceInfo(dev->inputParameters.device)->defaultLowInputLatency;
	dev->inputParameters.hostApiSpecificStreamInfo = NULL;

	dev->outbuf_size_in_bytes     = buffer_size;
	dev->outbuf_bytes_per_frame   = no_of_bytes_per_sample * pa->adev[a].num_channels;
	dev->outbuf_frames_per_buffer = dev->outbuf_size_in_bytes / dev->outbuf_bytes_per_frame;

	dev->outputParameters.device       = dev->pa_output_device_number;
	dev->outputParameters.channelCount = pa->adev[a].num_channels;
	dev->outputParameters.sampleFormat = sampleFormat;
	dev->outputParameters.suggestedLatency = Pa_GetDeviceInfo(dev->outputParameters.device)->defaultHighOutputLatency;
	dev->outputParameters.hostApiSpecificStreamInfo = NULL;

	err = check_pa_configure(dev, pa->adev[a].samples_per_sec);
	if(err) {
		if(err == paInvalidSampleRate)
			list_supported_sample_rates(dev);
		return -1;
	}

	err = Pa_OpenStream(&dev->inStream,	&dev->inputParameters, NULL,
						pa->adev[a].samples_per_sec, dev->inbuf_frames_per_buffer, 0, paInput16CB, dev );

	if( err != paNoError ) {
		dw_printf( "PortAudio OpenStream (input) Error: %s\n", Pa_GetErrorText(err));
		return -1;
	}

	err = Pa_OpenStream(&dev->outStream, NULL, &dev->outputParameters,
						// pa->adev[a].samples_per_sec, framesPerBuffer, 0, paOutput16CB, dev );
						pa->adev[a].samples_per_sec, dev->outbuf_frames_per_buffer, 0, NULL, dev );

	if( err != paNoError ) {
		dw_printf( "PortAudio OpenStream (output) Error: %s\n", Pa_GetErrorText(err));
		return -1;
	}

	dev->input_finished  = paContinue;
	dev->output_finished = paContinue;

	return buffer_size;
}


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
	int retries = 0;


#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);

	dw_printf ("audio_get():\n");

#endif

	assert (adev[a].inbuf_size_in_bytes >= 100 && adev[a].inbuf_size_in_bytes <= 32768);

	switch (adev[a].g_audio_in_type) {

			/*
			 * Soundcard - PortAudio
			 */
		case AUDIO_IN_TYPE_SOUNDCARD:

			while (adev[a].inbuf_next >= adev[a].inbuf_len) {

				assert (adev[a].inStream != NULL);
#if DEBUGx
				text_color_set(DW_COLOR_DEBUG);
				dw_printf ("audio_get(): readi asking for %d frames\n", adev[a].inbuf_size_in_bytes / adev[a].bytes_per_frame);
#endif
				if(adev[a].inbuf_len >= adev[a].inbuf_size_in_bytes) {
					adev[a].inbuf_len = 0;
					adev[a].inbuf_next = 0;
				}

				pthread_mutex_lock(&adev[a].input_mutex);
				pthread_cond_wait(&adev[a].input_cond, &adev[a].input_mutex);
				pthread_mutex_unlock(&adev[a].input_mutex);

				n = adev[a].inbuf_len / adev[a].inbuf_bytes_per_frame;
#if DEBUGx
				text_color_set(DW_COLOR_DEBUG);
				dw_printf ("audio_get(): readi asked for %d and got %d frames\n",
						   adev[a].inbuf_size_in_bytes / adev[a].bytes_per_frame, n);
#endif


				if (n > 0) {

					/* Success */

					adev[a].inbuf_len = n * adev[a].inbuf_bytes_per_frame;		/* convert to number of bytes */
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
					dw_printf ("[%s], Audio input got zero bytes\n", __func__);
					SLEEP_MS(10);

					adev[a].inbuf_len = 0;
					adev[a].inbuf_next = 0;
				}
				else {
					/* Error */
					// TODO: Needs more study and testing.

					// TODO: print n.  should snd_strerror use n or errno?
					// Audio input device error: Unknown error

					text_color_set(DW_COLOR_ERROR);
					dw_printf ("Audio input device %d error\n", a);

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

						//snd_pcm_recover (adev[a].audio_in_handle, n, 1);

					}
					else {
						/* Could be some temporary condition. */
						/* Wait a little then try again. */
						/* Sometimes I get "Resource temporarily available" */
						/* when the Update Manager decides to run. */

						SLEEP_MS (250);
						//snd_pcm_recover (adev[a].audio_in_handle, n, 1);
					}
				}
			}

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
	int err = 0;
	size_t frames = 0;

	//#define __TIMED__
#ifdef __TIMED__
	static int count = 0;
	static double start = 0, end = 0, diff = 0;

	if(adev[a].outbuf_len == 0)
		start = dtime_monotonic();
#endif

	if(c >= 0) {
		adev[a].outbuf_ptr[adev[a].outbuf_len++] = c;
	}

	if ((adev[a].outbuf_len >= adev[a].outbuf_size_in_bytes) || (c < 0)) {

		frames = adev[a].outbuf_len / adev[a].outbuf_bytes_per_frame;

		if(frames > 0) {
			err =  Pa_WriteStream(adev[a].outStream, adev[a].outbuf_ptr, frames);
		}

		// Getting underflow error for some reason on the first pass. Upon examination of the
		// audio data revealed no discontinuity in the signal. Time measurements indicate this routine
		// on this machine (2.8Ghz/Xeon E5462/2008 vintage) can handle ~6 times the current
		// sample rate (44100/2 bytes per frame). For now, mask the error.
		// Transfer Time:0.184750080 No of Frames:56264 Per frame:0.000003284 speed:6.905695

		if ((err != paNoError) && (err != paOutputUnderflowed)) {
			text_color_set(DW_COLOR_ERROR);
			dw_printf ("[%s] Audio Output Error: %s\n", __func__, Pa_GetErrorText(err));
		}

#ifdef __TIMED__
		count += frames;
		if(c < 0) { // When the Ax25 frames are flushed.
			end = dtime_monotonic();
			diff = end - start;
			if(count)
				dw_printf ("Transfer Time:%3.9f No of Frames:%d Per frame:%3.9f speed:%f\n",
						   diff, count, diff/(count * 1.0), (1.0/44100.0)/(diff/(count * 1.0)));
			count = 0;
		}
#endif
		adev[a].outbuf_len  = 0;
		adev[a].outbuf_next = 0;
	}

	return (0);
}

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
	audio_put(a, -1);
	return 0;
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
	audio_flush(a);
	
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
		if(adev[a].g_audio_in_type == AUDIO_IN_TYPE_SOUNDCARD) {
			
			audio_wait (a);
			
			if (adev[a].inStream != NULL) {
				pthread_mutex_destroy(&adev[a].input_mutex);
				pthread_cond_destroy(&adev[a].input_cond);
				err |= (int) Pa_CloseStream(adev[a].inStream);
			}
			
			if(adev[a].outStream != NULL) {
				pthread_mutex_destroy(&adev[a].output_mutex);
				pthread_cond_destroy(&adev[a].output_cond);
				err |= (int) Pa_CloseStream(adev[a].outStream);
			}
			
			err |= (int) Pa_Terminate();
		}
		
		if(adev[a].inbuf_ptr)
			free (adev[a].inbuf_ptr);
		
		if(adev[a].outbuf_ptr)
			free (adev[a].outbuf_ptr);
		
		adev[a].inbuf_size_in_bytes = 0;
		adev[a].inbuf_ptr  = NULL;
		adev[a].inbuf_len  = 0;
		adev[a].inbuf_next = 0;
		
		adev[a].outbuf_size_in_bytes = 0;
		adev[a].outbuf_ptr  = NULL;
		adev[a].outbuf_len  = 0;
		adev[a].outbuf_next = 0;
	}
	
	if(err < 0)
		err = -1;
	
	return (err);

} /* end audio_close */

/* end audio_portaudio.c */

#endif // USE_PORTAUDIO

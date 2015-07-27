
/*------------------------------------------------------------------
 *
 * Module:      audio.h
 *
 * Purpose:   	Interface to audio device commonly called a "sound card."
 *		
 *---------------------------------------------------------------*/


#ifndef AUDIO_H
#define AUDIO_H 1

#include "direwolf.h"		/* for MAX_CHANS used throughout the application. */
#include "hdlc_rec2.h"		/* for enum retry_e */
				

/*
 * PTT control. 
 */

enum ptt_method_e { 
	PTT_METHOD_NONE,	/* VOX or no transmit. */
	PTT_METHOD_SERIAL,	/* Serial port RTS or DTR. */
	PTT_METHOD_GPIO,	/* General purpose I/O, Linux only. */
	PTT_METHOD_LPT };	/* Parallel printer port, Linux only. */

typedef enum ptt_method_e ptt_method_t;

enum ptt_line_e { PTT_LINE_RTS = 1, PTT_LINE_DTR = 2 };		
typedef enum ptt_line_e ptt_line_t;

enum audio_in_type_e {
	AUDIO_IN_TYPE_SOUNDCARD,
	AUDIO_IN_TYPE_SDR_UDP,
	AUDIO_IN_TYPE_STDIN };

struct audio_s {

	/* Properites of the sound device. */

	char adevice_in[80];		/* Name of the audio input device (or file?). */
					/* TODO: Can be "-" to read from stdin. */

	char adevice_out[80];		/* Name of the audio output device (or file?). */

	int num_channels;		/* Should be 1 for mono or 2 for stereo. */
	int samples_per_sec;		/* Audio sampling rate.  Typically 11025, 22050, or 44100. */
	int bits_per_sample;		/* 8 (unsigned char) or 16 (signed short). */

	enum audio_in_type_e audio_in_type;
					/* Where is input (receive) audio coming from? */

	/* Common to all channels. */

	enum retry_e fix_bits;		/* Level of effort to recover from */
					/* a bad FCS on the frame. */

	/* Properties for each audio channel, common to receive and transmit. */
	/* Can be different for each radio channel. */

	enum modem_t {AFSK, NONE, SCRAMBLE} modem_type[MAX_CHANS];
					/* Usual AFSK. */
					/* Baseband signal. */
					/* Scrambled http://www.amsat.org/amsat/articles/g3ruh/109/fig03.gif */

	int decimate[MAX_CHANS];	/* Reduce AFSK sample rate by this factor to */
					/* decrease computational requirements. */

        int mark_freq[MAX_CHANS];	/* Two tones for AFSK modulation, in Hz. */
	int space_freq[MAX_CHANS];	/* Standard tones are 1200 and 2200 for 1200 baud. */

	int baud[MAX_CHANS];		/* Data bits (more accurately, symbols) per second. */
					/* Standard rates are 1200 for VHF and 300 for HF. */

	char profiles[MAX_CHANS][16];	/* 1 or more of ABC etc. */

	int num_freq[MAX_CHANS];	/* Number of different frequency pairs for decoders. */

	int offset[MAX_CHANS];		/* Spacing between filter frequencies. */

	int num_subchan[MAX_CHANS];	/* Total number of modems / hdlc decoders for each channel. */
					/* Potentially it could be product of strlen(profiles) * num_freq. */
					/* Currently can't use both at once. */


	/* Additional properties for transmit. */
			
	ptt_method_t ptt_method[MAX_CHANS];	/* serial port or GPIO. */

	char ptt_device[MAX_CHANS][20];	/* Serial device name for PTT.  e.g. COM1 or /dev/ttyS0 */
			
	ptt_line_t ptt_line[MAX_CHANS];	/* Control line wehn using serial port.  */
					/* PTT_RTS, PTT_DTR. */

	int ptt_gpio[MAX_CHANS];	/* GPIO number. */
	
	int ptt_lpt_bit[MAX_CHANS];	/* Bit number for parallel printer port.  */
					/* Bit 0 = pin 2, ..., bit 7 = pin 9. */

	int ptt_invert[MAX_CHANS];	/* Invert the output. */

	int slottime[MAX_CHANS];	/* Slot time in 10 mS units for persistance algorithm. */
					/* Typical value is 10 meaning 100 milliseconds. */

	int persist[MAX_CHANS];		/* Sets probability for transmitting after each */
					/* slot time delay.  Transmit if a random number */
					/* in range of 0 - 255 <= persist value.  */
					/* Otherwise wait another slot time and try again. */
					/* Default value is 63 for 25% probability. */

	int txdelay[MAX_CHANS];		/* After turning on the transmitter, */
					/* send "flags" for txdelay * 10 mS. */
					/* Default value is 30 meaning 300 milliseconds. */

	int txtail[MAX_CHANS];		/* Amount of time to keep transmitting after we */
					/* are done sending the data.  This is to avoid */
					/* dropping PTT too soon and chopping off the end */
					/* of the frame.  Again 10 mS units. */
					/* At this point, I'm thinking of 10 as the default. */
};

#if __WIN32__
#define DEFAULT_ADEVICE	""		/* Windows: Empty string = default audio device. */
#else
#if USE_ALSA
#define DEFAULT_ADEVICE	"default"	/* Use default device for ALSA. */
#else
#define DEFAULT_ADEVICE	"/dev/dsp"	/* First audio device for OSS. */
#endif					
#endif


/*
 * UDP audio receiving port.  Couldn't find any standard or usage precedent.
 * Got the number from this example:   http://gqrx.dk/doc/streaming-audio-over-udp
 * Any better suggestions?
 */

#define DEFAULT_UDP_AUDIO_PORT 7355


// Maximum size of the UDP buffer (for allowing IP routing, udp packets are often limited to 1472 bytes)

#define SDR_UDP_BUF_MAXLEN 2000



#define DEFAULT_NUM_CHANNELS 	1
#define DEFAULT_SAMPLES_PER_SEC	44100	/* Very early observations.  Might no longer be valid. */
					/* 22050 works a lot better than 11025. */
					/* 44100 works a little better than 22050. */
					/* If you have a reasonable machine, use the highest rate. */
#define MIN_SAMPLES_PER_SEC	8000
#define MAX_SAMPLES_PER_SEC	48000	/* Formerly 44100. */
					/* Software defined radio often uses 48000. */

#define DEFAULT_BITS_PER_SAMPLE	16

#define DEFAULT_FIX_BITS RETRY_SWAP_SINGLE

/* 
 * Standard for AFSK on VHF FM. 
 * Reversing mark and space makes no difference because
 * NRZI encoding only cares about change or lack of change
 * between the two tones.
 *
 * HF SSB uses 300 baud and 200 Hz shift.
 * 1600 & 1800 Hz is a popular tone pair, sometimes 
 * called the KAM tones.
 */

#define DEFAULT_MARK_FREQ	1200	
#define DEFAULT_SPACE_FREQ	2200
#define DEFAULT_BAUD		1200



/*
 * Typical transmit timings for VHF.
 */

#define DEFAULT_SLOTTIME	10
#define DEFAULT_PERSIST		63
#define DEFAULT_TXDELAY		30
#define DEFAULT_TXTAIL		10	/* not sure yet. */


/* 
 * Note that we have two versions of these in audio.c and audio_win.c.
 * Use one or the other depending on the platform.
 */


int audio_open (struct audio_s *pa);

int audio_get (void);

int audio_put (int c);

int audio_flush (void);

int audio_wait (int duration);

int audio_close (void);


#endif  /* ifdef AUDIO_H */


/* end audio.h */


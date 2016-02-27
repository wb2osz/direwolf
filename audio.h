
/*------------------------------------------------------------------
 *
 * Module:      audio.h
 *
 * Purpose:   	Interface to audio device commonly called a "sound card"
 *		for historical reasons.
 *		
 *---------------------------------------------------------------*/


#ifndef AUDIO_H
#define AUDIO_H 1

#ifdef USE_HAMLIB
#include <hamlib/rig.h>
#endif

#include "direwolf.h"		/* for MAX_CHANS used throughout the application. */
#include "ax25_pad.h"		/* for AX25_MAX_ADDR_LEN */

				

/*
 * PTT control. 
 */

enum ptt_method_e { 
	PTT_METHOD_NONE,	/* VOX or no transmit. */
	PTT_METHOD_SERIAL,	/* Serial port RTS or DTR. */
	PTT_METHOD_GPIO,	/* General purpose I/O, Linux only. */
	PTT_METHOD_LPT,	    /* Parallel printer port, Linux only. */
    PTT_METHOD_HAMLIB }; /* HAMLib, Linux only. */

typedef enum ptt_method_e ptt_method_t;

enum ptt_line_e { PTT_LINE_NONE = 0, PTT_LINE_RTS = 1, PTT_LINE_DTR = 2 };	  //  Important: 0 for neither.	
typedef enum ptt_line_e ptt_line_t;

enum audio_in_type_e {
	AUDIO_IN_TYPE_SOUNDCARD,
	AUDIO_IN_TYPE_SDR_UDP,
	AUDIO_IN_TYPE_STDIN };

/* For option to try fixing frames with bad CRC. */

typedef enum retry_e {
		RETRY_NONE=0,
		RETRY_INVERT_SINGLE=1,
		RETRY_INVERT_DOUBLE=2,
		RETRY_INVERT_TRIPLE=3,
		RETRY_INVERT_TWO_SEP=4,
		RETRY_MAX = 5}  retry_t;


typedef enum sanity_e { SANITY_APRS, SANITY_AX25, SANITY_NONE } sanity_t;
			 

struct audio_s {

	/* Previously we could handle only a single audio device. */
	/* In version 1.2, we generalize this to handle multiple devices. */
	/* This means we can now have more than 2 radio channels. */

	struct adev_param_s {

	    /* Properites of the sound device. */

	    int defined;		/* Was device defined? */
					/* First one defaults to yes. */

	    char adevice_in[80];	/* Name of the audio input device (or file?). */
					/* TODO: Can be "-" to read from stdin. */

	    char adevice_out[80];	/* Name of the audio output device (or file?). */

	    int num_channels;		/* Should be 1 for mono or 2 for stereo. */
	    int samples_per_sec;	/* Audio sampling rate.  Typically 11025, 22050, or 44100. */
	    int bits_per_sample;	/* 8 (unsigned char) or 16 (signed short). */

	} adev[MAX_ADEVS];


	/* Common to all channels. */

	char tts_script[80];		/* Script for text to speech. */

	int statistics_interval;	/* Number of seconds between the audio */
					/* statistics reports.  This is set by */
					/* the "-a" option.  0 to disable feature. */

	/* Properties for each audio channel, common to receive and transmit. */
	/* Can be different for each radio channel. */


	struct achan_param_s {

	    int valid;			/* Is this channel valid?  */

	    char mycall[AX25_MAX_ADDR_LEN];      /* Call associated with this radio channel. */
                                	/* Could all be the same or different. */


	    enum modem_t { MODEM_AFSK, MODEM_BASEBAND, MODEM_SCRAMBLE, MODEM_OFF } modem_type;

					/* Usual AFSK. */
					/* Baseband signal. Not used yet. */
					/* Scrambled http://www.amsat.org/amsat/articles/g3ruh/109/fig03.gif */
					/* No modem.  Might want this for DTMF only channel. */


	    enum dtmf_decode_t { DTMF_DECODE_OFF, DTMF_DECODE_ON } dtmf_decode; 

					/* Originally the DTMF ("Touch Tone") decoder was always */
					/* enabled because it took a negligible amount of CPU. */
					/* There were complaints about the false positives when */
					/* hearing other modulation schemes on HF SSB so now it */
					/* is enabled only when needed. */

					/* "On" will send special "t" packet to attached applications */
					/* and process as APRStt.  Someday we might want to separate */
					/* these but for now, we have a single off/on. */

	    int decimate;		/* Reduce AFSK sample rate by this factor to */
					/* decrease computational requirements. */

	    int interleave;		/* If > 1, interleave samples among multiple decoders. */
					/* Quick hack for experiment. */

            int mark_freq;		/* Two tones for AFSK modulation, in Hz. */
	    int space_freq;		/* Standard tones are 1200 and 2200 for 1200 baud. */

	    int baud;			/* Data bits (more accurately, symbols) per second. */
					/* Standard rates are 1200 for VHF and 300 for HF. */

	/* Next 3 come from config file or command line. */

	    char profiles[16];		/* zero or more of ABC etc, optional + */

	    int num_freq;		/* Number of different frequency pairs for decoders. */

	    int offset;			/* Spacing between filter frequencies. */

	    int num_slicers;		/* Number of different threshold points to decide */
					/* between mark or space. */

	/* This is derived from above by demod_init. */

	    int num_subchan;		/* Total number of modems for each channel. */


	/* These are for dealing with imperfect frames. */

	    enum retry_e fix_bits;	/* Level of effort to recover from */
					/* a bad FCS on the frame. */
					/* 0 = no effort */
					/* 1 = try fixing a single bit */
					/* 2... = more techniques... */

	    enum sanity_e sanity_test;	/* Sanity test to apply when finding a good */
					/* CRC after making a change. */
					/* Must look like APRS, AX.25, or anything. */

	    int passall;		/* Allow thru even with bad CRC. */


	/* Additional properties for transmit. */
	
	/* Originally we had control outputs only for PTT. */
	/* In version 1.2, we generalize this to allow others such as DCD. */
	/* Index following structure by one of these: */


#define OCTYPE_PTT 0
#define OCTYPE_DCD 1
#define OCTYPE_FUTURE 2

#define NUM_OCTYPES 4		/* number of values above */
	
	    struct {  		

	        ptt_method_t ptt_method; /* none, serial port, GPIO, LPT, HAMLIB. */

	        char ptt_device[20];	/* Serial device name for PTT.  e.g. COM1 or /dev/ttyS0 */
					/* Also used for HAMLIB.  Could be host:port when model is 1. */
			
	        ptt_line_t ptt_line;	/* Control line when using serial port. PTT_LINE_RTS, PTT_LINE_DTR. */
	        ptt_line_t ptt_line2;	/* Optional second one:  PTT_LINE_NONE when not used. */

	        int ptt_gpio;		/* GPIO number. */
	
	        int ptt_lpt_bit;	/* Bit number for parallel printer port.  */
					/* Bit 0 = pin 2, ..., bit 7 = pin 9. */

	        int ptt_invert;		/* Invert the output. */
	        int ptt_invert2;	/* Invert the secondary output. */

#ifdef USE_HAMLIB

	        int ptt_model;		/* HAMLIB model.  -1 for AUTO.  2 for rigctld.  Others are radio model. */
#endif

	    } octrl[NUM_OCTYPES];

#define ICTYPE_TXINH 0

#define NUM_ICTYPES 1

	    struct {
		ptt_method_t method;	/* none, serial port, GPIO, LPT. */
		int gpio;		/* GPIO number */
		int invert;		/* 1 = active low */
	    } ictrl[NUM_ICTYPES];

	/* Transmit timing. */

	    int dwait;			/* First wait extra time for receiver squelch. */
					/* Default 0 units of 10 mS each . */

	    int slottime;		/* Slot time in 10 mS units for persistance algorithm. */
					/* Typical value is 10 meaning 100 milliseconds. */

	    int persist;		/* Sets probability for transmitting after each */
					/* slot time delay.  Transmit if a random number */
					/* in range of 0 - 255 <= persist value.  */
					/* Otherwise wait another slot time and try again. */
					/* Default value is 63 for 25% probability. */

	    int txdelay;		/* After turning on the transmitter, */
					/* send "flags" for txdelay * 10 mS. */
					/* Default value is 30 meaning 300 milliseconds. */

	    int txtail;			/* Amount of time to keep transmitting after we */
					/* are done sending the data.  This is to avoid */
					/* dropping PTT too soon and chopping off the end */
					/* of the frame.  Again 10 mS units. */
					/* At this point, I'm thinking of 10 as the default. */

	} achan[MAX_CHANS];

#ifdef USE_HAMLIB
    int rigs;               /* Total number of configured rigs */
    RIG *rig[MAX_RIGS];     /* HAMLib rig instances */
#endif

};


#if __WIN32__ || __APPLE__
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

#define DEFAULT_FIX_BITS RETRY_INVERT_SINGLE

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

/* Used for sanity checking in config file and command line options. */
/* 9600 is known to work.  */
/* TODO: Is 19200 possible with a soundcard at 44100 samples/sec? */

#define MIN_BAUD		100
#define MAX_BAUD		10000


/*
 * Typical transmit timings for VHF.
 */

#define DEFAULT_DWAIT		0
#define DEFAULT_SLOTTIME	10
#define DEFAULT_PERSIST		63
#define DEFAULT_TXDELAY		30
#define DEFAULT_TXTAIL		10	


/* 
 * Note that we have two versions of these in audio.c and audio_win.c.
 * Use one or the other depending on the platform.
 */

int audio_open (struct audio_s *pa);

int audio_get (int a);		/* a = audio device, 0 for first */

int audio_put (int a, int c);

int audio_flush (int a);

void audio_wait (int a);

int audio_close (void);


#endif  /* ifdef AUDIO_H */


/* end audio.h */


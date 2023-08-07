
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
#include "version.h"
				

/*
 * PTT control. 
 */

enum ptt_method_e { 
	PTT_METHOD_NONE,	/* VOX or no transmit. */
	PTT_METHOD_SERIAL,	/* Serial port RTS or DTR. */
	PTT_METHOD_GPIO,	/* General purpose I/O, Linux only. */
	PTT_METHOD_LPT,	    	/* Parallel printer port, Linux only. */
	PTT_METHOD_HAMLIB, 	/* HAMLib, Linux only. */
	PTT_METHOD_CM108 };	/* GPIO pin of CM108/CM119/etc.  Linux only. */

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

// Type of communication medium associated with the channel.

enum medium_e { MEDIUM_NONE = 0,	// Channel is not valid for use.
		MEDIUM_RADIO,		// Internal modem for radio.
		MEDIUM_IGATE,		// Access IGate as ordinary channel.
		MEDIUM_NETTNC };	// Remote network TNC.  (possible future)


typedef enum sanity_e { SANITY_APRS, SANITY_AX25, SANITY_NONE } sanity_t;
			 

struct audio_s {

	/* Previously we could handle only a single audio device. */
	/* In version 1.2, we generalize this to handle multiple devices. */
	/* This means we can now have more than 2 radio channels. */

	struct adev_param_s {

	    /* Properties of the sound device. */

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

	int xmit_error_rate;		/* For testing purposes, we can generate frames with an invalid CRC */
					/* to simulate corruption while going over the air. */
					/* This is the probability, in per cent, of randomly corrupting it. */
					/* Normally this is 0.  25 would mean corrupt it 25% of the time. */

	int recv_error_rate;		/* Similar but the % probability of dropping a received frame. */

	float recv_ber;			/* Receive Bit Error Rate (BER). */
					/* Probability of inverting a bit coming out of the modem. */

	//int fx25_xmit_enable;		/* Enable transmission of FX.25.  */
					/* See fx25_init.c for explanation of values. */
					/* Initially this applies to all channels. */
					/* This should probably be per channel. One step at a time. */
					/* v1.7 - replaced by layer2_xmit==LAYER2_FX25 */

	int fx25_auto_enable;		/* Turn on FX.25 for current connected mode session */
					/* under poor conditions. */
					/* Set to 0 to disable feature. */
					/* I put it here, rather than with the rest of the link layer */
					/* parameters because it is really a part of the HDLC layer */
					/* and is part of the KISS TNC functionality rather than our data link layer. */
					/* Future: not used yet. */


	char timestamp_format[40];	/* -T option */
					/* Precede received & transmitted frames with timestamp. */
					/* Command line option uses "strftime" format string. */



	/* originally a "channel" was always connected to an internal modem. */
	/* In version 1.6, this is generalized so that a channel (as seen by client application) */
	/* can be connected to something else.  Initially, this will allow application */
	/* access to the IGate.  Later we might have network TNCs or other internal functions. */

	// Properties for all channels.

	enum medium_e chan_medium[MAX_TOTAL_CHANS];
					// MEDIUM_NONE for invalid.
					// MEDIUM_RADIO for internal modem.  (only possibility earlier)
					// MEDIUM_IGATE allows application access to IGate.
					// MEDIUM_NETTNC for external TNC via TCP.

	int igate_vchannel;		/* Virtual channel mapped to APRS-IS. */
					/* -1 for none. */
					/* Redundant but it makes things quicker and simpler */
					/* than always searching thru above. */

	/* Properties for each radio channel, common to receive and transmit. */
	/* Can be different for each radio channel. */

	struct achan_param_s {

	    // Currently, we have a fixed mapping from audio sources to channel.
	    //
	    //		ADEVICE		CHANNEL (mono)		(stereo)
	    //		0		0			0, 1
	    //		1		2			2, 3
	    //		2		4			4, 5
	    //
	    // A future feauture might allow the user to specify a different audio source.
	    // This would allow multiple modems (with associated channel) to share an audio source.
	    // int audio_source;	// Default would be [0,1,2,3,4,5]

	    // What else should be moved out of structure and enlarged when NETTNC is implemented.  ???
	    char mycall[AX25_MAX_ADDR_LEN];      /* Call associated with this radio channel. */
                                	/* Could all be the same or different. */


	    enum modem_t { MODEM_AFSK, MODEM_BASEBAND, MODEM_SCRAMBLE, MODEM_QPSK, MODEM_8PSK, MODEM_OFF, MODEM_16_QAM, MODEM_64_QAM, MODEM_AIS, MODEM_EAS } modem_type;

					/* Usual AFSK. */
					/* Baseband signal. Not used yet. */
					/* Scrambled http://www.amsat.org/amsat/articles/g3ruh/109/fig03.gif */
					/* Might try MFJ-2400 / CCITT v.26 / Bell 201 someday. */
					/* No modem.  Might want this for DTMF only channel. */

	    enum layer2_t { LAYER2_AX25 = 0, LAYER2_FX25, LAYER2_IL2P } layer2_xmit;

					// IL2P - New for version 1.7.
					// New layer 2 with FEC.  Much less overhead than FX.25 but no longer backward compatible.
					// Only applies to transmit.
					// Listening for FEC sync word should add negligible overhead so
					// we leave reception enabled all the time as we do with FX.25.
					// TODO:  FX.25 should probably be put here rather than global for all channels.

	    int fx25_strength;		// Strength of FX.25 FEC.
					// 16, 23, 64 for specific number of parity symbols.
					// 1 for automatic selection based on frame size.

	    int il2p_max_fec;		// 1 for max FEC length, 0 for automatic based on size.

	    int il2p_invert_polarity;	// 1 means invert on transmit.  Receive handles either automatically.

	    enum v26_e { V26_UNSPECIFIED=0, V26_A, V26_B } v26_alternative;

					// Original implementation used alternative A for 2400 bbps PSK.
					// Years later, we discover that MFJ-2400 used alternative B.
					// It's likely the others did too.  it also works a little better.
					// Default to MFJ compatible and print warning if user did not
					// pick one explicitly.

#define V26_DEFAULT V26_B

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

	    int upsample;		/* Upsample by this factor for G3RUH. */

            int mark_freq;		/* Two tones for AFSK modulation, in Hz. */
	    int space_freq;		/* Standard tones are 1200 and 2200 for 1200 baud. */

	    int baud;			/* Data bits per second. */
					/* Standard rates are 1200 for VHF and 300 for HF. */
					/* This should really be called bits per second. */

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
	/* In version 1.4 we add CON for connected to another station. */
	/* Index following structure by one of these: */


#define OCTYPE_PTT 0
#define OCTYPE_DCD 1
#define OCTYPE_CON 2

#define NUM_OCTYPES 3		/* number of values above.   i.e. last value +1. */
	
	    struct {  		

	        ptt_method_t ptt_method; /* none, serial port, GPIO, LPT, HAMLIB, CM108. */

	        char ptt_device[128];	/* Serial device name for PTT.  e.g. COM1 or /dev/ttyS0 */
					/* Also used for HAMLIB.  Could be host:port when model is 1. */
					/* For years, 20 characters was plenty then we start getting extreme names like this: */
					/* /dev/serial/by-id/usb-FTDI_Navigator__CAT___2nd_PTT__00000000-if00-port0 */
					/* /dev/serial/by-id/usb-Prolific_Technology_Inc._USB-Serial_Controller_D-if00-port0 */
					/* Issue 104, changed to 100 bytes in version 1.5. */

					/* This same field is also used for CM108/CM119 GPIO PTT which will */
					/* have a name like /dev/hidraw1 for Linux or */
					/* \\?\hid#vid_0d8c&pid_0008&mi_03#8&39d3555&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030} */
					/* for Windows.  Largest observed was 95 but add some extra to be safe. */
			
	        ptt_line_t ptt_line;	/* Control line when using serial port. PTT_LINE_RTS, PTT_LINE_DTR. */
	        ptt_line_t ptt_line2;	/* Optional second one:  PTT_LINE_NONE when not used. */

	        int out_gpio_num;	/* GPIO number.  Originally this was only for PTT. */
					/* It is now more general. */
					/* octrl array is indexed by PTT, DCD, or CONnected indicator. */
					/* For CM108/CM119, this should be in range of 1-8. */

#define MAX_GPIO_NAME_LEN 20	// 12 would cover any case I've seen so this should be safe

		char out_gpio_name[MAX_GPIO_NAME_LEN];
					/* originally, gpio number NN was assumed to simply */
					/* have the name gpioNN but this turned out not to be */
					/* the case for CubieBoard where it was longer. */
					/* This is filled in by ptt_init so we don't have to */
					/* recalculate it each time we access it. */

					/* This could probably be collapsed into ptt_device instead of being separate. */

	        int ptt_lpt_bit;	/* Bit number for parallel printer port.  */
					/* Bit 0 = pin 2, ..., bit 7 = pin 9. */

	        int ptt_invert;		/* Invert the output. */
	        int ptt_invert2;	/* Invert the secondary output. */

#ifdef USE_HAMLIB

	        int ptt_model;		/* HAMLIB model.  -1 for AUTO.  2 for rigctld.  Others are radio model. */
	        int ptt_rate;		/* Serial port speed when using hamlib CAT control for PTT. */
					/* If zero, hamlib will come up with a default for pariticular rig. */
#endif

	    } octrl[NUM_OCTYPES];


	/* Each channel can also have associated input lines. */
	/* So far, we just have one for transmit inhibit. */

#define ICTYPE_TXINH 0

#define NUM_ICTYPES 1		/* number of values above. i.e. last value +1. */

	    struct {
		ptt_method_t method;	/* none, serial port, GPIO, LPT. */

		int in_gpio_num;	/* GPIO number */

		char in_gpio_name[MAX_GPIO_NAME_LEN];
					/* originally, gpio number NN was assumed to simply */
					/* have the name gpioNN but this turned out not to be */
					/* the case for CubieBoard where it was longer. */
					/* This is filled in by ptt_init so we don't have to */
					/* recalculate it each time we access it. */

		int invert;		/* 1 = active low */
	    } ictrl[NUM_ICTYPES];

	/* Transmit timing. */

	    int dwait;			/* First wait extra time for receiver squelch. */
					/* Default 0 units of 10 mS each . */

	    int slottime;		/* Slot time in 10 mS units for persistence algorithm. */
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
					/* At this point, I'm thinking of 10 (= 100 mS) as the default */
					/* because we're not quite sure when the soundcard audio stops. */

	    int fulldup;		/* Full Duplex. */

	} achan[MAX_CHANS];

#ifdef USE_HAMLIB
    int rigs;               /* Total number of configured rigs */
    RIG *rig[MAX_RIGS];     /* HAMLib rig instances */
#endif

};


#if __WIN32__
#define DEFAULT_ADEVICE	""		/* Windows: Empty string = default audio device. */
#elif __APPLE__
#define DEFAULT_ADEVICE	""		/* Mac OSX: Empty string = default audio device. */
#elif USE_ALSA
#define DEFAULT_ADEVICE	"default"	/* Use default device for ALSA. */
#elif USE_SNDIO
#define DEFAULT_ADEVICE	"default"	/* Use default device for sndio. */
#else
#define DEFAULT_ADEVICE	"/dev/dsp"	/* First audio device for OSS.  (FreeBSD) */
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
//#define MAX_SAMPLES_PER_SEC	48000	/* Originally 44100.  Later increased because */
					/* Software Defined Radio often uses 48000. */

#define MAX_SAMPLES_PER_SEC	192000	/* The cheap USB-audio adapters (e.g. CM108) can handle 44100 and 48000. */
					/* The "soundcard" in my desktop PC can do 96kHz or even 192kHz. */
					/* We will probably need to increase the sample rate to go much above 9600 baud. */

#define DEFAULT_BITS_PER_SAMPLE	16

#define DEFAULT_FIX_BITS RETRY_NONE	// Interesting research project but even a single bit fix up
					// will occasionally let corrupted packets through.

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
/* 9600 baud is known to work.  */
/* TODO: Is 19200 possible with a soundcard at 44100 samples/sec or do we need a higher sample rate? */

#define MIN_BAUD		100
//#define MAX_BAUD		10000
#define MAX_BAUD		40000		// Anyone want to try 38.4 k baud?

/*
 * Typical transmit timings for VHF.
 */

#define DEFAULT_DWAIT		0
#define DEFAULT_SLOTTIME	10	// *10mS = 100mS
#define DEFAULT_PERSIST		63
#define DEFAULT_TXDELAY		30	// *10mS = 300mS
#define DEFAULT_TXTAIL		10	// *10mS = 100mS	
#define DEFAULT_FULLDUP		0	// false = half duplex

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


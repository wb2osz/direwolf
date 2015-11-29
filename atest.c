
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


/*-------------------------------------------------------------------
 *
 * Name:        atest.c
 *
 * Purpose:     Test fixture for the AFSK demodulator.
 *
 * Inputs:	Takes audio from a .WAV file insted of the audio device.
 *
 * Description:	This can be used to test the AFSK demodulator under
 *		controlled and reproducable conditions for tweaking.
 *	
 *		For example
 *
 *		(1) Download WA8LMF's TNC Test CD image file from
 *			http://wa8lmf.net/TNCtest/index.htm
 *
 *		(2) Burn a physical CD.
 *
 *		(3) "Rip" the desired tracks with Windows Media Player.
 *			Select .WAV file format.
 *		
 *		"Track 2" is used for most tests because that is more
 *		realistic for most people using the speaker output.
 *
 *
 * 	Without ONE_CHAN defined:
 *
 *	  Notice that the number of packets decoded, as reported by
 *	  this test program, will be twice the number expected because
 *	  we are decoding the left and right audio channels separately.
 *
 *
 * 	With ONE_CHAN defined:
 *
 *	  Only process one channel.  
 *
 *--------------------------------------------------------------------*/

// #define X 1


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <getopt.h>


#define ATEST_C 1

#include "audio.h"
#include "demod.h"
#include "multi_modem.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"
#include "dlq.h"
#include "ptt.h"



#if 0	/* Typical but not flexible enough. */

struct wav_header {             /* .WAV file header. */
        char riff[4];           /* "RIFF" */
        int filesize;          /* file length - 8 */
        char wave[4];           /* "WAVE" */
        char fmt[4];            /* "fmt " */
        int fmtsize;           /* 16. */
        short wformattag;       /* 1 for PCM. */
        short nchannels;        /* 1 for mono, 2 for stereo. */
        int nsamplespersec;    /* sampling freq, Hz. */
        int navgbytespersec;   /* = nblockalign*nsamplespersec. */
        short nblockalign;      /* = wbitspersample/8 * nchannels. */
        short wbitspersample;   /* 16 or 8. */
        char data[4];           /* "data" */
        int datasize;          /* number of bytes following. */
} ;
#endif
					/* 8 bit samples are unsigned bytes */
					/* in range of 0 .. 255. */
 
 					/* 16 bit samples are signed short */
					/* in range of -32768 .. +32767. */

static struct {
        char riff[4];          /* "RIFF" */
        int filesize;          /* file length - 8 */
        char wave[4];          /* "WAVE" */
} header;

static struct {
	char id[4];		/* "LIST" or "fmt " */
	int datasize;
} chunk;

static struct {
        short wformattag;       /* 1 for PCM. */
        short nchannels;        /* 1 for mono, 2 for stereo. */
        int nsamplespersec;    /* sampling freq, Hz. */
        int navgbytespersec;   /* = nblockalign*nsamplespersec. */
        short nblockalign;      /* = wbitspersample/8 * nchannels. */
        short wbitspersample;   /* 16 or 8. */
	char extras[4];
} format;

static struct {
	char data[4];		/* "data" */
	int datasize;
} wav_data;


static FILE *fp;
static int e_o_f;
static int packets_decoded = 0;
static int decimate = 0;		/* Reduce that sampling rate if set. */
					/* 1 = normal, 2 = half, etc. */

static struct audio_s my_audio_config;

static int error_if_less_than = -1;	/* Exit with error status if this minimum not reached. */
					/* Can be used to check that performance has not decreased. */

static int error_if_greater_than = -1;	/* Exit with error status if this maximum exceeded. */
					/* Can be used to check that duplicate removal is not broken. */



//#define EXPERIMENT_G 1
//#define EXPERIMENT_H 1

#if defined(EXPERIMENT_G) || defined(EXPERIMENT_H)

static 	int count[MAX_SUBCHANS];

#if EXPERIMENT_H
extern float space_gain[MAX_SUBCHANS];
#endif

#endif

static void usage (void);


static int decode_only = 0;		/* Set to 0 or 1 to decode only one channel.  2 for both.  */

static int sample_number = -1;		/* Sample number from the file. */
					/* Incremented only for channel 0. */
					/* Use to print timestamp, relative to beginning */
					/* of file, when frame was decoded. */

int main (int argc, char *argv[])
{

	int err;
	int c;
	int channel;
	time_t start_time;


#if defined(EXPERIMENT_G) || defined(EXPERIMENT_H)
	int j;

	for (j=0; j<MAX_SUBCHANS; j++) {
	  count[j] = 0;
	}
#endif

	text_color_init(1);
	text_color_set(DW_COLOR_INFO);

/* 
 * First apply defaults.
 */
	
	memset (&my_audio_config, 0, sizeof(my_audio_config));

	my_audio_config.adev[0].num_channels = DEFAULT_NUM_CHANNELS;		
	my_audio_config.adev[0].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;	
	my_audio_config.adev[0].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;	

	// Results v0.9: 
	//
	// fix_bits = 0 	971 packets, 69 sec
	// fix_bits = SINGLE	990          64
	// fix_bits = DOUBLE    992          65
	// fix_bits = TRIPLE    992          67
	// fix_bits = TWO_SEP   1004        476 

	// Essentially no difference in time for those with order N time.
	// Time increases greatly for the one with order N^2 time.


	// Results: version 1.1, decoder C, my_audio_config.fix_bits = RETRY_MAX - 1
	//
	// 971 NONE
	// +19 SINGLE
	// +2  DOUBLE
	// +12 TWO_SEP
	// +3  REMOVE_MANY
	// ----
	// 1007 Total in 1008 sec.   More than twice as long as earlier version.

	// Results: version 1.1, decoders ABC, my_audio_config.fix_bits = RETRY_MAX - 1
	//
	// 976 NONE
	// +21  SINGLE
	// +1   DOUBLE
	// +22  TWO_SEP
	// +1   MANY
	// +3   REMOVE_MANY
	// ----
	// 1024 Total in 2042 sec.
	// About 34 minutes of CPU time for a roughly 40 minute CD.
	// Many computers wouldn't be able to keep up.

	// The SINGLE and TWO_SEP techniques are the most effective.
	// Should we reorder the enum values so that TWO_SEP
	// comes after SINGLE?   That way "FIX_BITS 2" would 
	// use the two most productive techniques and not waste
	// time on the others.  People with plenty of CPU power
	// to spare can still specify larger numbers for the other
	// techniques with less return on investment.


	for (channel=0; channel<MAX_CHANS; channel++) {

	  my_audio_config.achan[channel].modem_type = MODEM_AFSK;

	  my_audio_config.achan[channel].mark_freq = DEFAULT_MARK_FREQ;		
	  my_audio_config.achan[channel].space_freq = DEFAULT_SPACE_FREQ;		
	  my_audio_config.achan[channel].baud = DEFAULT_BAUD;	

	  strlcpy (my_audio_config.achan[channel].profiles, "E", sizeof(my_audio_config.achan[channel].profiles));
 		
	  my_audio_config.achan[channel].num_freq = 1;				
	  my_audio_config.achan[channel].offset = 0;	

	  my_audio_config.achan[channel].fix_bits = RETRY_NONE;	

	  my_audio_config.achan[channel].sanity_test = SANITY_APRS;	
	  //my_audio_config.achan[channel].sanity_test = SANITY_AX25;	
	  //my_audio_config.achan[channel].sanity_test = SANITY_NONE;	

	  my_audio_config.achan[channel].passall = 0;				
	  //my_audio_config.achan[channel].passall = 1;				
	}

	while (1) {
          //int this_option_optind = optind ? optind : 1;
          int option_index = 0;
          static struct option long_options[] = {
            {"future1", 1, 0, 0},
            {"future2", 0, 0, 0},
            {"future3", 1, 0, 'c'},
            {0, 0, 0, 0}
          };

	  /* ':' following option character means arg is required. */

          c = getopt_long(argc, argv, "B:P:D:F:L:G:012",
                        long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

            case 'B':				/* -B for data Bit rate */
						/*    300 implies 1600/1800 AFSK. */
						/*    1200 implies 1200/2200 AFSK. */
						/*    9600 implies scrambled. */

              my_audio_config.achan[0].baud = atoi(optarg);

              dw_printf ("Data rate set to %d bits / second.\n", my_audio_config.achan[0].baud);

              if (my_audio_config.achan[0].baud < 100 || my_audio_config.achan[0].baud > 10000) {
		text_color_set(DW_COLOR_ERROR);
                dw_printf ("Use a more reasonable bit rate in range of 100 - 10000.\n");
                exit (EXIT_FAILURE);
              }
	      if (my_audio_config.achan[0].baud < 600) {
                my_audio_config.achan[0].modem_type = MODEM_AFSK;
                my_audio_config.achan[0].mark_freq = 1600;
                my_audio_config.achan[0].space_freq = 1800;
	        strlcpy (my_audio_config.achan[0].profiles, "D", sizeof(my_audio_config.achan[0].profiles));
	      }
	      else if (my_audio_config.achan[0].baud > 2400) {
                my_audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
                my_audio_config.achan[0].mark_freq = 0;
                my_audio_config.achan[0].space_freq = 0;
	        strlcpy (my_audio_config.achan[0].profiles, " ", sizeof(my_audio_config.achan[0].profiles));	// avoid getting default later.
                dw_printf ("Using scrambled baseband signal rather than AFSK.\n");
	      }
	      else {
                my_audio_config.achan[0].modem_type = MODEM_AFSK;
                my_audio_config.achan[0].mark_freq = 1200;
                my_audio_config.achan[0].space_freq = 2200;
	      }
              break;

	    case 'P':				/* -P for modem profile. */

	      dw_printf ("Demodulator profile set to \"%s\"\n", optarg);
	      strlcpy (my_audio_config.achan[0].profiles, optarg, sizeof(my_audio_config.achan[0].profiles)); 
	      break;	

	    case 'D':				/* -D reduce sampling rate for lower CPU usage. */

	      decimate = atoi(optarg);

	      dw_printf ("Divide audio sample rate by %d\n", decimate);
	      if (decimate < 1 || decimate > 8) {
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("Unreasonable value for -D.\n");
		exit (1);
	      }
	      dw_printf ("Divide audio sample rate by %d\n", decimate);
	      my_audio_config.achan[0].decimate = decimate;
	      break;	

	    case 'F':				/* -D set "fix bits" level. */

	      my_audio_config.achan[0].fix_bits = atoi(optarg);

	      if (my_audio_config.achan[0].fix_bits < RETRY_NONE || my_audio_config.achan[0].fix_bits >= RETRY_MAX) {
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("Invalid Fix Bits level.\n");
		exit (1);
	      }
	      break;	

	    case 'L':				/* -L error if less than this number decoded. */

	      error_if_less_than = atoi(optarg);
	      break;	

	    case 'G':				/* -G error if greater than this number decoded. */

	      error_if_greater_than = atoi(optarg);
	      break;

	     case '0':				/* channel 0, left from stereo */

	       decode_only = 0;
	       break;

	     case '1':				/* channel 1, right from stereo */

	       decode_only = 1;
	       break;

	     case '2':				/* decode both from stereo */

	       decode_only = 2;
	       break;

             case '?':

              /* Unknown option message was already printed. */
              usage ();
              break;

            default:

              /* Should not be here. */
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("?? getopt returned character code 0%o ??\n", c);
              usage ();
	    }
        }
    
	memcpy (&my_audio_config.achan[1], &my_audio_config.achan[0], sizeof(my_audio_config.achan[0]));


	if (optind >= argc) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Specify .WAV file name on command line.\n");
	  usage ();
	}

	fp = fopen(argv[optind], "rb");
        if (fp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("Couldn't open file for read: %s\n", argv[optind]);
	  //perror ("more info?");
          exit (1);
        }

	start_time = time(NULL);


/*
 * Read the file header.  
 * Doesn't handle all possible cases but good enough for our purposes.
 */

        err= fread (&header, (size_t)12, (size_t)1, fp);
	(void)(err);

	if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("This is not a .WAV format file.\n");
          exit (EXIT_FAILURE);
	}

	err = fread (&chunk, (size_t)8, (size_t)1, fp);

	if (strncmp(chunk.id, "LIST", 4) == 0) {
	  err = fseek (fp, (long)chunk.datasize, SEEK_CUR);
	  err = fread (&chunk, (size_t)8, (size_t)1, fp);
	}

	if (strncmp(chunk.id, "fmt ", 4) != 0) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("WAV file error: Found \"%4.4s\" where \"fmt \" was expected.\n", chunk.id);
	  exit(1);
	}
	if (chunk.datasize != 16 && chunk.datasize != 18) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("WAV file error: Need fmt chunk datasize of 16 or 18.  Found %d.\n", chunk.datasize);
	  exit(1);
	}

        err = fread (&format, (size_t)chunk.datasize, (size_t)1, fp);	

	err = fread (&wav_data, (size_t)8, (size_t)1, fp);

	if (strncmp(wav_data.data, "data", 4) != 0) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("WAV file error: Found \"%4.4s\" where \"data\" was expected.\n", wav_data.data);
	  exit(1);
	}

	// TODO: Should have proper message, not abort.
	assert (format.nchannels == 1 || format.nchannels == 2);
	assert (format.wbitspersample == 8 || format.wbitspersample == 16);

        my_audio_config.adev[0].samples_per_sec = format.nsamplespersec;
	my_audio_config.adev[0].bits_per_sample = format.wbitspersample;
 	my_audio_config.adev[0].num_channels = format.nchannels;

	my_audio_config.achan[0].valid = 1;
	if (format.nchannels == 2) my_audio_config.achan[1].valid = 1;

	text_color_set(DW_COLOR_INFO);
	dw_printf ("%d samples per second\n", my_audio_config.adev[0].samples_per_sec);
	dw_printf ("%d bits per sample\n", my_audio_config.adev[0].bits_per_sample);
	dw_printf ("%d audio channels\n", my_audio_config.adev[0].num_channels);
	dw_printf ("%d audio bytes in file\n", (int)(wav_data.datasize));
	dw_printf ("Fix Bits level = %d\n", my_audio_config.achan[0].fix_bits);

		
/*
 * Initialize the AFSK demodulator and HDLC decoder.
 */
	multi_modem_init (&my_audio_config);


	e_o_f = 0;
	while ( ! e_o_f) 
	{


          int audio_sample;
          int c;

          for (c=0; c<my_audio_config.adev[0].num_channels; c++)
          {

            /* This reads either 1 or 2 bytes depending on */
            /* bits per sample.  */

            audio_sample = demod_get_sample (ACHAN2ADEV(c));

            if (audio_sample >= 256 * 256)
               e_o_f = 1;

	    if (c == 0) sample_number++;

            if (decode_only == 0 && c != 0) continue;
            if (decode_only == 1 && c != 1) continue;

            multi_modem_process_sample(c,audio_sample);
          }

                /* When a complete frame is accumulated, */
                /* process_rec_frame, below, is called. */

	}
	text_color_set(DW_COLOR_INFO);
	dw_printf ("\n\n");

#if EXPERIMENT_G

	for (j=0; j<MAX_SUBCHANS; j++) {
	  float db = 20.0 * log10f(space_gain[j]);
	  dw_printf ("%+.1f dB, %d\n", db, count[j]);
	}
#endif
#if EXPERIMENT_H

	for (j=0; j<MAX_SUBCHANS; j++) {
	  dw_printf ("%d\n", count[j]);
	}
#endif
	dw_printf ("%d packets decoded in %d seconds.\n", packets_decoded, (int)(time(NULL) - start_time));

	if (error_if_less_than != -1 && packets_decoded < error_if_less_than) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n * * * TEST FAILED: number decoded is less than %d * * * \n", error_if_less_than);
	  exit (1);
	}
	if (error_if_greater_than != -1 && packets_decoded > error_if_greater_than) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n * * * TEST FAILED: number decoded is greater than %d * * * \n", error_if_greater_than);
	  exit (1);
	}

	exit (0);
}


/*
 * Simulate sample from the audio device.
 */

int audio_get (int a)
{
	int ch;

	if (wav_data.datasize <= 0) {
	  e_o_f = 1;
	  return (-1);
	}

	ch = getc(fp);
	wav_data.datasize--;

	if (ch < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Unexpected end of file.\n");
	  e_o_f = 1;
	}

	return (ch);
}



/*
 * Rather than queuing up frames with bad FCS, 
 * try to fix them immediately.
 */

void rdq_append (rrbb_t rrbb)
{
	int chan, subchan, slice;
	alevel_t alevel;


	chan = rrbb_get_chan(rrbb);
	subchan = rrbb_get_subchan(rrbb);
	slice = rrbb_get_slice(rrbb);
	alevel = rrbb_get_audio_level(rrbb);

	hdlc_rec2_try_to_fix_later (rrbb, chan, subchan, slice, alevel);
	rrbb_delete (rrbb);
}


/*
 * This is called when we have a good frame.
 */

void dlq_append (dlq_type_t type, int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, char *spectrum)
{	
	
	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	int h;
	char heard[AX25_MAX_ADDR_LEN];
	char alevel_text[AX25_ALEVEL_TO_TEXT_SIZE];

	packets_decoded++;

	ax25_format_addrs (pp, stemp);

	info_len = ax25_get_info (pp, &pinfo);

	/* Print so we can see what is going on. */

//TODO: quiet option - suppress packet printing, only the count at the end.

#if 1
	/* Display audio input level. */
        /* Who are we hearing?   Original station or digipeater? */

	if (ax25_get_num_addr(pp) == 0) {
	  /* Not AX.25. No station to display below. */
	  h = -1;
	  strlcpy (heard, "", sizeof(heard));
	}
	else {
	  h = ax25_get_heard(pp);
          ax25_get_addr_with_ssid(pp, h, heard);
	}

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n");
	dw_printf("DECODED[%d] ", packets_decoded );

	/* Insert time stamp relative to start of file. */

	double sec = (double)sample_number / my_audio_config.adev[0].samples_per_sec;
	int min = (int)(sec / 60.);
	sec -= min * 60;

	dw_printf ("%d:%07.4f ", min, sec);

	if (h != AX25_SOURCE) {
	  dw_printf ("Digipeater ");
	}
	ax25_alevel_to_text (alevel, alevel_text);

	if (my_audio_config.achan[chan].fix_bits == RETRY_NONE && my_audio_config.achan[chan].passall == 0) {
	  dw_printf ("%s audio level = %s     %s\n", heard, alevel_text, spectrum);
	}
	else {
	  dw_printf ("%s audio level = %s   [%s]   %s\n", heard, alevel_text, retry_text[(int)retries], spectrum);
	}

#endif

//#if defined(EXPERIMENT_G) || defined(EXPERIMENT_H)
//	int j;
//
//	for (j=0; j<MAX_SUBCHANS; j++) {
//	  if (spectrum[j] == '|') {
//	    count[j]++;
//	  }
//	}
//#endif


// Display non-APRS packets in a different color.

// Display channel with subchannel/slice if applicable.

	if (ax25_is_aprs(pp)) {
	  text_color_set(DW_COLOR_REC);
	}
	else {
	  text_color_set(DW_COLOR_DEBUG);
	}

	if (my_audio_config.achan[chan].num_subchan > 1 && my_audio_config.achan[chan].num_slicers == 1) {
	  dw_printf ("[%d.%d] ", chan, subchan);
	}
	else if (my_audio_config.achan[chan].num_subchan == 1 && my_audio_config.achan[chan].num_slicers > 1) {
	  dw_printf ("[%d.%d] ", chan, slice);
	}
	else if (my_audio_config.achan[chan].num_subchan > 1 && my_audio_config.achan[chan].num_slicers > 1) {
	  dw_printf ("[%d.%d.%d] ", chan, subchan, slice);
	}
	else {
	  dw_printf ("[%d] ", chan);
	}

	dw_printf ("%s", stemp);			/* stations followed by : */
	ax25_safe_print ((char *)pinfo, info_len, 0);
	dw_printf ("\n");

	ax25_delete (pp);

} /* end fake dlq_append */


void ptt_set (int ot, int chan, int ptt_signal)
{
	return;
}

int get_input (int it, int chan)
{
	return -1;
}

static void usage (void) {

	text_color_set(DW_COLOR_ERROR);

	dw_printf ("\n");
	dw_printf ("atest is a test application which decodes AX.25 frames from an audio\n");
	dw_printf ("recording.  This provides an easy way to test Dire Wolf decoding\n");
	dw_printf ("performance much quicker than normal real-time.   \n"); 
	dw_printf ("\n");
	dw_printf ("usage:\n");
	dw_printf ("\n");
	dw_printf ("        atest [ options ] wav-file-in\n");
	dw_printf ("\n");
	dw_printf ("        -B n   Bits/second  for data.  Proper modem automatically selected for speed.\n");
	dw_printf ("               300 baud uses 1600/1800 Hz AFSK.\n");
	dw_printf ("               1200 (default) baud uses 1200/2200 Hz AFSK.\n");
	dw_printf ("               9600 baud uses K9NG/G2RUH standard.\n");
	dw_printf ("\n");
	dw_printf ("        -D n   Divide audio sample rate by n.\n");
	dw_printf ("\n");
	dw_printf ("        -F n   Amount of effort to try fixing frames with an invalid CRC.  \n");
	dw_printf ("               0 (default) = consider only correct frames.  \n");
	dw_printf ("               1 = Try to fix only a single bit.  \n");
	dw_printf ("               more = Try modifying more bits to get a good CRC.\n");
	dw_printf ("\n");
	dw_printf ("        -P m   Select  the  demodulator  type such as A, B, C, D (default for 300 baud),\n");
	dw_printf ("               E (default for 1200 baud), F, A+, B+, C+, D+, E+, F+.\n");
	dw_printf ("\n");
	dw_printf ("        -0     Use channel 0 (left) of stereo audio (default).\n");
	dw_printf ("        -1     use channel 1 (right) of stereo audio.\n");
	dw_printf ("        -1     decode both channels of stereo audio.\n");
	dw_printf ("\n");
	dw_printf ("        wav-file-in is a WAV format audio file.\n");
	dw_printf ("\n");
	dw_printf ("Examples:\n");
	dw_printf ("\n");
	dw_printf ("        gen_packets -o test1.wav\n");
	dw_printf ("        atest test1.wav\n");
	dw_printf ("\n");
	dw_printf ("        gen_packets -B 300 -o test3.wav\n");
	dw_printf ("        atest -B 300 test3.wav\n");
	dw_printf ("\n");
	dw_printf ("        gen_packets -B 9600 -o test9.wav\n");
	dw_printf ("        atest -B 9600 test9.wav\n");
	dw_printf ("\n");
	dw_printf ("              This generates and decodes 3 test files with 1200, 300, and 9600\n");
	dw_printf ("              bits per second.\n");
	dw_printf ("\n");
	dw_printf ("        atest 02_Track_2.wav\n");
	dw_printf ("        atest -P C+ 02_Track_2.wav\n");
	dw_printf ("        atest -F 1 02_Track_2.wav\n");
	dw_printf ("        atest -P C+ -F 1 02_Track_2.wav\n");
	dw_printf ("\n");
	dw_printf ("              Try  different combinations of options to find the best decoding\n");
	dw_printf ("              performance.\n");

	exit (1);
}



/* end atest.c */

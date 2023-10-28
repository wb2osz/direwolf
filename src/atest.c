
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2019, 2021, 2022, 2023  John Langner, WB2OSZ
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
 * Purpose:     Test fixture for the Dire Wolf demodulators.
 *
 * Inputs:	Takes audio from a .WAV file instead of the audio device.
 *
 * Description:	This can be used to test the demodulators under
 *		controlled and reproducible conditions for tweaking.
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

#include "direwolf.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <ctype.h>


#define ATEST_C 1

#include "audio.h"
#include "demod.h"
#include "multi_modem.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"
#include "dlq.h"
#include "ptt.h"
#include "dtime_now.h"
#include "fx25.h"
#include "il2p.h"
#include "hdlc_rec.h"


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
 
 					/* 16 bit samples are little endian signed short */
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
static int packets_decoded_one = 0;
static int packets_decoded_total = 0;
static int decimate = 0;		/* Reduce that sampling rate if set. */
					/* 1 = normal, 2 = half, 3 = 1/3, etc. */

static int upsample = 0;		/* Upsample for G3RUH decoder. */
					/* Non-zero will override the default. */

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

// command line options.

static int B_opt = DEFAULT_BAUD;	// Bits per second.  Need to change all baud references to bps.
static int g_opt = 0;			// G3RUH modem regardless of speed.
static int j_opt = 0;			/* 2400 bps PSK compatible with direwolf <= 1.5 */
static int J_opt = 0;			/* 2400 bps PSK compatible MFJ-2400 and maybe others. */
static int h_opt = 0;			// Hexadecimal display of received packet.
static char P_opt[16] = "";		// Demodulator profiles.
static int d_x_opt = 1;			// FX.25 debug.
static int d_o_opt = 0;			// "-d o" option for DCD output control. */	
static int d_2_opt = 0;			// "-d 2" option for IL2P details. */
static int dcd_count = 0;
static int dcd_missing_errors = 0;


int main (int argc, char *argv[])
{

	int err;
	int c;
	int channel;

	double start_time;		// Time when we started so we can measure elapsed time.
	double one_filetime = 0;		// Length of one audio file in seconds.
	double total_filetime = 0;		// Length of all audio files in seconds.
	double elapsed;			// Time it took us to process it.


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


	for (channel=0; channel<MAX_CHANS; channel++) {

	  my_audio_config.achan[channel].modem_type = MODEM_AFSK;

	  my_audio_config.achan[channel].mark_freq = DEFAULT_MARK_FREQ;		
	  my_audio_config.achan[channel].space_freq = DEFAULT_SPACE_FREQ;		
	  my_audio_config.achan[channel].baud = DEFAULT_BAUD;	

	  strlcpy (my_audio_config.achan[channel].profiles, "A", sizeof(my_audio_config.achan[channel].profiles));
 		
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

          c = getopt_long(argc, argv, "B:P:D:U:gjJF:L:G:012he:d:",
                        long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

            case 'B':				/* -B for data Bit rate */
						/* Also implies modem type based on speed. */
						/* Special cases AIS, EAS rather than number. */
	      if (strcasecmp(optarg, "AIS") == 0) {
	        B_opt = 0xA15A15;	// See special case below.
	      }
	      else if (strcasecmp(optarg, "EAS") == 0) {
	        B_opt = 0xEA5EA5;	// See special case below.
	      }
	      else {
	        B_opt = atoi(optarg);
	      }
              break;

            case 'g':				/* -G Force G3RUH regardless of speed. */

	      g_opt = 1;
              break;

            case 'j':				/* -j V.26 compatible with earlier direwolf. */

	      j_opt = 1;
              break;

            case 'J':				/* -J V.26 compatible with MFJ-2400. */

	      J_opt = 1;
              break;

	    case 'P':				/* -P for modem profile. */

	      // Wait until after other options processed.
	      strlcpy (P_opt, optarg, sizeof(P_opt));
	      break;	

	    case 'D':				/* -D reduce sampling rate for lower CPU usage. */

	      decimate = atoi(optarg);

	      dw_printf ("Divide audio sample rate by %d\n", decimate);
	      if (decimate < 1 || decimate > 8) {
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("Unreasonable value for -D.\n");
		exit (EXIT_FAILURE);
	      }
	      dw_printf ("Divide audio sample rate by %d\n", decimate);
	      my_audio_config.achan[0].decimate = decimate;
	      break;	

	    case 'U':				/* -U upsample for G3RUH to improve performance */
						/* when the sample rate to baud ratio is low. */
						/* Actually it is set automatically and this will */
						/* override the normal calculation. */

	      upsample = atoi(optarg);

	      dw_printf ("Multiply audio sample rate by %d\n", upsample);
	      if (upsample < 1 || upsample > 4) {
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("Unreasonable value for -U.\n");
		exit (EXIT_FAILURE);
	      }
	      dw_printf ("Multiply audio sample rate by %d\n", upsample);
	      my_audio_config.achan[0].upsample = upsample;
	      break;

	    case 'F':				/* -F set "fix bits" level. */

	      my_audio_config.achan[0].fix_bits = atoi(optarg);

	      if (my_audio_config.achan[0].fix_bits < RETRY_NONE || my_audio_config.achan[0].fix_bits >= RETRY_MAX) {
		text_color_set(DW_COLOR_ERROR);
		dw_printf ("Invalid Fix Bits level.\n");
		exit (EXIT_FAILURE);
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

	     case 'h':				/* Hexadecimal display. */

	       h_opt = 1;
	       break;

	     case 'e':				/* Receive Bit Error Rate (BER). */

	       my_audio_config.recv_ber = atof(optarg);
	       break;

	     case 'd':				/* Debug message options. */

	       for (char *p=optarg; *p!='\0'; p++) {
	        switch (*p) {
	           case 'x':  d_x_opt++; break;			// FX.25
	           case 'o':  d_o_opt++; break;			// DCD output control
	           case '2':  d_2_opt++; break;			// IL2P debug out
	           default: break;
	        }
	       }
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
    
/*
 * Set modem type based on data rate.
 * (Could be overridden by -g, -j, or -J later.)
 */
	/*    300 implies 1600/1800 AFSK. */
	/*    1200 implies 1200/2200 AFSK. */
	/*    2400 implies V.26 QPSK. */
	/*    4800 implies V.27 8PSK. */
	/*    9600 implies G3RUH baseband scrambled. */

        my_audio_config.achan[0].baud = B_opt;


	/* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
	/* that need to be kept in sync.  Maybe it could be a common function someday. */

	if (my_audio_config.achan[0].baud == 100) {		// What was this for?
	  my_audio_config.achan[0].modem_type = MODEM_AFSK;
	  my_audio_config.achan[0].mark_freq = 1615;
	  my_audio_config.achan[0].space_freq = 1785;
	}
	else if (my_audio_config.achan[0].baud < 600) {		// e.g. HF SSB packet
	  my_audio_config.achan[0].modem_type = MODEM_AFSK;
	  my_audio_config.achan[0].mark_freq = 1600;
	  my_audio_config.achan[0].space_freq = 1800;
	  // Previously we had a "D" which was fine tuned for 300 bps.
	  // In v1.7, it's not clear if we should use "B" or just stick with "A".
	}
	else if (my_audio_config.achan[0].baud < 1800) {	// common 1200
	  my_audio_config.achan[0].modem_type = MODEM_AFSK;
	  my_audio_config.achan[0].mark_freq = DEFAULT_MARK_FREQ;
	  my_audio_config.achan[0].space_freq = DEFAULT_SPACE_FREQ;
	}
	else if (my_audio_config.achan[0].baud < 3600) {
	  my_audio_config.achan[0].modem_type = MODEM_QPSK;
	  my_audio_config.achan[0].mark_freq = 0;
	  my_audio_config.achan[0].space_freq = 0;
	  strlcpy (my_audio_config.achan[0].profiles, "", sizeof(my_audio_config.achan[0].profiles));
	}
	else if (my_audio_config.achan[0].baud < 7200) {
	  my_audio_config.achan[0].modem_type = MODEM_8PSK;
	  my_audio_config.achan[0].mark_freq = 0;
	  my_audio_config.achan[0].space_freq = 0;
	  strlcpy (my_audio_config.achan[0].profiles, "", sizeof(my_audio_config.achan[0].profiles));
	}
	else if (my_audio_config.achan[0].baud == 0xA15A15) {	// Hack for different use of 9600
	  my_audio_config.achan[0].modem_type = MODEM_AIS;
	  my_audio_config.achan[0].baud = 9600;
	  my_audio_config.achan[0].mark_freq = 0;
	  my_audio_config.achan[0].space_freq = 0;
	  strlcpy (my_audio_config.achan[0].profiles, " ", sizeof(my_audio_config.achan[0].profiles));	// avoid getting default later.
	}
	else if (my_audio_config.achan[0].baud == 0xEA5EA5) {
	  my_audio_config.achan[0].modem_type = MODEM_EAS;
	  my_audio_config.achan[0].baud = 521;	// Actually 520.83 but we have an integer field here.
						// Will make more precise in afsk demod init.
	  my_audio_config.achan[0].mark_freq = 2083;	// Actually 2083.3 - logic 1.
	  my_audio_config.achan[0].space_freq = 1563;	// Actually 1562.5 - logic 0.
	  strlcpy (my_audio_config.achan[0].profiles, "A", sizeof(my_audio_config.achan[0].profiles));
	}
	else {
	  my_audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
	  my_audio_config.achan[0].mark_freq = 0;
	  my_audio_config.achan[0].space_freq = 0;
	  strlcpy (my_audio_config.achan[0].profiles, " ", sizeof(my_audio_config.achan[0].profiles));	// avoid getting default later.
	}

        if (my_audio_config.achan[0].baud < MIN_BAUD || my_audio_config.achan[0].baud > MAX_BAUD) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("Use a more reasonable bit rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
          exit (EXIT_FAILURE);
        }

/*
 * -g option means force g3RUH regardless of speed.
 */

	if (g_opt) {
          my_audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
          my_audio_config.achan[0].mark_freq = 0;
          my_audio_config.achan[0].space_freq = 0;
	  strlcpy (my_audio_config.achan[0].profiles, " ", sizeof(my_audio_config.achan[0].profiles));	// avoid getting default later.
	}

/*
 * We have two different incompatible flavors of V.26.
 */
	if (j_opt) {

	  // V.26 compatible with earlier versions of direwolf.
	  //   Example:   -B 2400 -j    or simply   -j

	  my_audio_config.achan[0].v26_alternative = V26_A;
          my_audio_config.achan[0].modem_type = MODEM_QPSK;
          my_audio_config.achan[0].mark_freq = 0;
          my_audio_config.achan[0].space_freq = 0;
	  my_audio_config.achan[0].baud = 2400;
	  strlcpy (my_audio_config.achan[0].profiles, "", sizeof(my_audio_config.achan[0].profiles));
	}
	if (J_opt) {

	  // V.26 compatible with MFJ and maybe others.
	  //   Example:   -B 2400 -J     or simply   -J

	  my_audio_config.achan[0].v26_alternative = V26_B;
          my_audio_config.achan[0].modem_type = MODEM_QPSK;
          my_audio_config.achan[0].mark_freq = 0;
          my_audio_config.achan[0].space_freq = 0;
	  my_audio_config.achan[0].baud = 2400;
	  strlcpy (my_audio_config.achan[0].profiles, "", sizeof(my_audio_config.achan[0].profiles));
	}

	// Needs to be after -B, -j, -J.
	if (strlen(P_opt) > 0) {
	  dw_printf ("Demodulator profile set to \"%s\"\n", P_opt);
	  strlcpy (my_audio_config.achan[0].profiles, P_opt, sizeof(my_audio_config.achan[0].profiles));
	}

	memcpy (&my_audio_config.achan[1], &my_audio_config.achan[0], sizeof(my_audio_config.achan[0]));


	if (optind >= argc) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Specify .WAV file name on command line.\n");
	  usage ();
	}

	fx25_init (d_x_opt);
	il2p_init (d_2_opt);

	start_time = dtime_now();

	while (optind < argc) {

	fp = fopen(argv[optind], "rb");
        if (fp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("Couldn't open file for read: %s\n", argv[optind]);
	  //perror ("more info?");
          exit (EXIT_FAILURE);
        }

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
	  exit(EXIT_FAILURE);
	}
	if (chunk.datasize != 16 && chunk.datasize != 18) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("WAV file error: Need fmt chunk datasize of 16 or 18.  Found %d.\n", chunk.datasize);
	  exit(EXIT_FAILURE);
	}

        err = fread (&format, (size_t)chunk.datasize, (size_t)1, fp);	

	err = fread (&wav_data, (size_t)8, (size_t)1, fp);

	if (strncmp(wav_data.data, "data", 4) != 0) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("WAV file error: Found \"%4.4s\" where \"data\" was expected.\n", wav_data.data);
	  exit(EXIT_FAILURE);
	}

	if (format.wformattag != 1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Sorry, I only understand audio format 1 (PCM).  This file has %d.\n", format.wformattag);
	  exit (EXIT_FAILURE);
	}

	if (format.nchannels != 1 && format.nchannels != 2) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Sorry, I only understand 1 or 2 channels.  This file has %d.\n", format.nchannels);
	  exit (EXIT_FAILURE);
	}

	if (format.wbitspersample != 8 && format.wbitspersample != 16) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Sorry, I only understand 8 or 16 bits per sample.  This file has %d.\n", format.wbitspersample);
	  exit (EXIT_FAILURE);
	}

        my_audio_config.adev[0].samples_per_sec = format.nsamplespersec;
	my_audio_config.adev[0].bits_per_sample = format.wbitspersample;
 	my_audio_config.adev[0].num_channels = format.nchannels;

	my_audio_config.chan_medium[0] = MEDIUM_RADIO;
	if (format.nchannels == 2) {
	  my_audio_config.chan_medium[1] = MEDIUM_RADIO;
	}

	text_color_set(DW_COLOR_INFO);
	dw_printf ("%d samples per second.  %d bits per sample.  %d audio channels.\n",
		my_audio_config.adev[0].samples_per_sec,
		my_audio_config.adev[0].bits_per_sample,
		my_audio_config.adev[0].num_channels);
	one_filetime = (double) wav_data.datasize /
		((my_audio_config.adev[0].bits_per_sample / 8) * my_audio_config.adev[0].num_channels * my_audio_config.adev[0].samples_per_sec);
	total_filetime += one_filetime;

	dw_printf ("%d audio bytes in file.  Duration = %.1f seconds.\n",
		(int)(wav_data.datasize),
		one_filetime);
	dw_printf ("Fix Bits level = %d\n", my_audio_config.achan[0].fix_bits);
		
/*
 * Initialize the AFSK demodulator and HDLC decoder.
 * Needs to be done for each file because they could have different sample rates.
 */
	multi_modem_init (&my_audio_config);
	packets_decoded_one = 0;


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

            if (audio_sample >= 256 * 256) {
               e_o_f = 1;
	       continue;
	    }

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

	dw_printf ("%d from %s\n", packets_decoded_one, argv[optind]);
	packets_decoded_total += packets_decoded_one;

	fclose (fp);
	optind++;
	}

	elapsed = dtime_now() - start_time;

	dw_printf ("%d packets decoded in %.3f seconds.  %.1f x realtime\n", packets_decoded_total, elapsed, total_filetime/elapsed);
	if (d_o_opt) {
	  dw_printf ("DCD count = %d\n", dcd_count);
	  dw_printf ("DCD missing errors = %d\n", dcd_missing_errors);
	}

	if (error_if_less_than != -1 && packets_decoded_total < error_if_less_than) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n * * * TEST FAILED: number decoded is less than %d * * * \n", error_if_less_than);
	  exit (EXIT_FAILURE);
	}
	if (error_if_greater_than != -1 && packets_decoded_total > error_if_greater_than) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\n * * * TEST FAILED: number decoded is greater than %d * * * \n", error_if_greater_than);
	  exit (EXIT_FAILURE);
	}

	exit (EXIT_SUCCESS);
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
 * This is called when we have a good frame.
 */

void dlq_rec_frame (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum)
{	
	
	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	int h;
	char heard[2 * AX25_MAX_ADDR_LEN + 20];
	char alevel_text[AX25_ALEVEL_TO_TEXT_SIZE];

	packets_decoded_one++;
	if ( ! hdlc_rec_data_detect_any(chan)) dcd_missing_errors++;

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
	dw_printf("DECODED[%d] ", packets_decoded_one );

	/* Insert time stamp relative to start of file. */

	double sec = (double)sample_number / my_audio_config.adev[0].samples_per_sec;
	int min = (int)(sec / 60.);
	sec -= min * 60;

	dw_printf ("%d:%06.3f ", min, sec);

	if (h != AX25_SOURCE) {
	  dw_printf ("Digipeater ");
	}
	ax25_alevel_to_text (alevel, alevel_text);

	/* As suggested by KJ4ERJ, if we are receiving from */
	/* WIDEn-0, it is quite likely (but not guaranteed), that */
	/* we are actually hearing the preceding station in the path. */

	if (h >= AX25_REPEATER_2 &&
	      strncmp(heard, "WIDE", 4) == 0 &&
	      isdigit(heard[4]) &&
	      heard[5] == '\0') {

	  char probably_really[AX25_MAX_ADDR_LEN];
	  ax25_get_addr_with_ssid(pp, h-1, probably_really);

	  strlcat (heard, " (probably ", sizeof(heard));
	  strlcat (heard, probably_really, sizeof(heard));
	  strlcat (heard, ")", sizeof(heard));
	}

	switch (fec_type) {

	  case fec_type_fx25:
	    dw_printf ("%s audio level = %s   FX.25  %s\n", heard, alevel_text, spectrum);
	    break;

	  case fec_type_il2p:
	    dw_printf ("%s audio level = %s   IL2P  %s\n", heard, alevel_text, spectrum);
	    break;

	  case fec_type_none:
	  default:
	    if (my_audio_config.achan[chan].fix_bits == RETRY_NONE && my_audio_config.achan[chan].passall == 0) {
	      // No fix_bits or passall specified.
	      dw_printf ("%s audio level = %s     %s\n", heard, alevel_text, spectrum);
	    }
	    else {
	      assert (retries >= RETRY_NONE && retries <= RETRY_MAX);   // validate array index.
	      dw_printf ("%s audio level = %s   [%s]   %s\n", heard, alevel_text, retry_text[(int)retries], spectrum);
	    }
	    break;
	}

#endif


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

/*
 * -h option for hexadecimal display.  (new in 1.6)
 */

	if (h_opt) {

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("------\n");
	  ax25_hex_dump (pp);
	  dw_printf ("------\n");
	}




#if 0		// temp experiment

#include "decode_aprs.h"
#include "log.h"

	if (ax25_is_aprs(pp)) {

	  decode_aprs_t A;

	  decode_aprs (&A, pp, 0, NULL);

	  // Temp experiment to see how different systems set the RR bits in the source and destination.
	  // log_rr_bits (&A, pp);

	}
#endif


	ax25_delete (pp);

} /* end fake dlq_append */


void ptt_set (int ot, int chan, int ptt_signal)
{
	// Should only get here for DCD output control.
	static double dcd_start_time[MAX_CHANS];

	if (d_o_opt) {
	  double t = (double)sample_number / my_audio_config.adev[0].samples_per_sec;
	  double sec1, sec2;
	  int min1, min2;

	  text_color_set(DW_COLOR_INFO);

	  if (ptt_signal) {
	    //sec1 = t;
	    //min1 = (int)(sec1 / 60.);
	    //sec1 -= min1 * 60;
	    //dw_printf ("DCD[%d] = ON    %d:%06.3f\n",  chan, min1, sec1);
	    dcd_count++;
	    dcd_start_time[chan] = t;
	  }
	  else {
	    //dw_printf ("DCD[%d] = off   %d:%06.3f   %3.0f\n",  chan, min, sec, (t - dcd_start_time[chan]) * 1000.);

	    sec1 = dcd_start_time[chan];
	    min1 = (int)(sec1 / 60.);
	    sec1 -= min1 * 60;

	    sec2 = t;
	    min2 = (int)(sec2 / 60.);
	    sec2 -= min2 * 60;

	    dw_printf ("DCD[%d]  %d:%06.3f - %d:%06.3f =  %3.0f\n",  chan, min1, sec1, min2, sec2, (t - dcd_start_time[chan]) * 1000.);
	  }
	}
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
	dw_printf ("               300 bps defaults to AFSK tones of 1600 & 1800.\n");
	dw_printf ("               1200 bps uses AFSK tones of 1200 & 2200.\n");
	dw_printf ("               2400 bps uses QPSK based on V.26 standard.\n");
	dw_printf ("               4800 bps uses 8PSK based on V.27 standard.\n");
	dw_printf ("               9600 bps and up uses K9NG/G3RUH standard.\n");
	dw_printf ("               AIS for ship Automatic Identification System.\n");
	dw_printf ("               EAS for Emergency Alert System (EAS) Specific Area Message Encoding (SAME).\n");
	dw_printf ("\n");
	dw_printf ("        -g     Use G3RUH modem rather rather than default for data rate.\n");
	dw_printf ("        -j     2400 bps QPSK compatible with direwolf <= 1.5.\n");
	dw_printf ("        -J     2400 bps QPSK compatible with MFJ-2400.\n");
	dw_printf ("\n");
	dw_printf ("        -D n   Divide audio sample rate by n.\n");
	dw_printf ("\n");
	dw_printf ("        -h     Print frame contents as hexadecimal bytes.\n");
	dw_printf ("\n");
	dw_printf ("        -F n   Amount of effort to try fixing frames with an invalid CRC.  \n");
	dw_printf ("               0 (default) = consider only correct frames.  \n");
	dw_printf ("               1 = Try to fix only a single bit.  \n");
	dw_printf ("               more = Try modifying more bits to get a good CRC.\n");
	dw_printf ("\n");
	dw_printf ("        -d x   Debug information for FX.25.  Repeat for more detail.\n");
	dw_printf ("\n");
	dw_printf ("        -L     Error if less than this number decoded.\n");
	dw_printf ("\n");
	dw_printf ("        -G     Error if greater than this number decoded.\n");
	dw_printf ("\n");
	dw_printf ("        -P m   Select  the  demodulator  type such as D (default for 300 bps),\n");
	dw_printf ("               E+ (default for 1200 bps), PQRS for 2400 bps, etc.\n");
	dw_printf ("\n");
	dw_printf ("        -0     Use channel 0 (left) of stereo audio (default).\n");
	dw_printf ("        -1     use channel 1 (right) of stereo audio.\n");
	dw_printf ("        -2     decode both channels of stereo audio.\n");
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
	dw_printf ("        atest -P E- 02_Track_2.wav\n");
	dw_printf ("        atest -F 1 02_Track_2.wav\n");
	dw_printf ("        atest -P E- -F 1 02_Track_2.wav\n");
	dw_printf ("\n");
	dw_printf ("              Try  different combinations of options to compare decoding\n");
	dw_printf ("              performance.\n");

	exit (1);
}



/* end atest.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2013, 2014, 2015, 2016, 2019, 2021, 2023  John Langner, WB2OSZ
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
 * Name:	gen_packets.c
 *
 * Purpose:	Test program for generating AX.25 frames.
 *
 * Description:	Given messages are converted to audio and written 
 *		to a .WAV type audio file.
 *
 * Bugs:	Most options are implemented for only one audio channel.
 *
 * Examples:	Different speeds:
 *
 *			gen_packets -o z1.wav
 *			atest z1.wav
 *
 *			gen_packets -B 300 -o z3.wav
 *			atest -B 300 z3.wav
 *
 *			gen_packets -B 9600 -o z9.wav
 *			atest -B 300 z9.wav
 *
 *		User-defined content:
 *
 *			echo "WB2OSZ>APDW12:This is a test" | gen_packets -o z.wav -
 *			atest z.wav
 *
 *			echo "WB2OSZ>APDW12:Test line 1" >  z.txt
 *			echo "WB2OSZ>APDW12:Test line 2" >> z.txt
 *			echo "WB2OSZ>APDW12:Test line 3" >> z.txt
 *			gen_packets -o z.wav z.txt
 *			atest z.wav
 *
 *		With artificial noise added:
 *
 *			gen_packets -n 100 -o z2.wav
 *			atest z2.wav
 *
 *		Variable speed. e.g. 95% to 105% of normal speed.
 *		Required parameter is max % below and above normal.
 *		Optionally specify step other than 0.1%.
 *		Used to test how tolerant TNCs are to senders not
 *		not using exactly the right baud rate.
 *
 *			gen_packets -v 5
 *			gen_packets -v 5,0.5
 *
 *------------------------------------------------------------------*/


#include "direwolf.h"

#include <stdio.h>     
#include <stdlib.h>    
#include <getopt.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "audio.h"
#include "ax25_pad.h"
#include "hdlc_send.h"
#include "gen_tone.h"
#include "textcolor.h"
#include "morse.h"
#include "dtmf.h"
#include "fx25.h"
#include "il2p.h"


/* Own random number generator so we can get */
/* same results on Windows and Linux. */

#define MY_RAND_MAX 0x7fffffff
static int seed = 1;

static int my_rand (void) {
	// Perform the calculation as unsigned to avoid signed overflow error.
	seed = (int)(((unsigned)seed * 1103515245) + 12345) & MY_RAND_MAX;
	return (seed);
}

static void usage (char **argv);
static int audio_file_open (char *fname, struct audio_s *pa);
static int audio_file_close (void);

static int g_add_noise = 0;
static float g_noise_level = 0;
static int g_morse_wpm = 0;		/* Send morse code at this speed. */



static struct audio_s modem;


static void send_packet (char *str)
{
    	packet_t pp;
    	unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
    	int flen;
	int c = 0;	// channel number.

	if (g_morse_wpm > 0) {

	  // Why not use the destination field instead of command line option?
	  // For one thing, this is not in TNC-2 monitor format.

	  morse_send (c, str, g_morse_wpm, 100, 100);
	}
	else if (modem.achan[0].modem_type == MODEM_EAS) {

// Generate EAS SAME signal FOR RESEARCH AND TESTING ONLY!!!
// There could be legal consequences for sending unauhorized SAME
// over the radio so don't do it!

	  // I'm expecting to see TNC 2 monitor format.
	  // The source and destination are ignored.
	  // The optional destination SSID is the number of times to repeat.
	  // The user defined data type indicator can optionally be used
	  // for compatibility with how it is received and presented to client apps.
	  // Examples:
	  //	X>X-3:{DEZCZC-WXR-RWT-033019-033017-033015-033013-033011-025011-025017-033007-033005-033003-033001-025009-025027-033009+0015-1691525-KGYX/NWS-
	  //	X>X:NNNN

	  pp = ax25_from_text (str, 1);
	  if (pp == NULL) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf ("\"%s\" is not valid TNC2 monitoring format.\n", str);
	    return;
	  }
	  unsigned char *pinfo;
	  int info_len = ax25_get_info (pp, &pinfo);
	  if (info_len >= 3 && strncmp((char*)pinfo, "{DE", 3) == 0) {
	    pinfo += 3;
	    info_len -= 3;
	  }

	  int repeat = ax25_get_ssid (pp, AX25_DESTINATION);
	  if (repeat == 0) repeat = 1;

	  eas_send (c, pinfo, repeat, 500, 500);
	  ax25_delete (pp);
	}
	else {
	  pp = ax25_from_text (str, 1);
	  if (pp == NULL) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf ("\"%s\" is not valid TNC2 monitoring format.\n", str);
	    return;
	  }
	  flen = ax25_pack (pp, fbuf);
	  (void)flen;

	  // If stereo, put same thing in each channel.

	  for (c=0; c<modem.adev[0].num_channels; c++)
	  {

#if 1
	    int samples_per_symbol, n, j;

	    // Insert random amount of quiet time.

	    if (modem.achan[c].modem_type == MODEM_QPSK) {
	      samples_per_symbol = modem.adev[0].samples_per_sec / (modem.achan[c].baud / 2);
	    }
	    else if (modem.achan[c].modem_type == MODEM_8PSK) {
	      samples_per_symbol = modem.adev[0].samples_per_sec / (modem.achan[c].baud / 3);
	    }
	    else {
	      samples_per_symbol = modem.adev[0].samples_per_sec / modem.achan[c].baud;
	    }

	    // Provide enough time for the DCD to drop.
	    // Then throw in a random amount of time so that receiving
	    // DPLL will need to adjust to a new phase.

	    n = samples_per_symbol * (32 + (float)my_rand() / (float)MY_RAND_MAX );

	    for (j=0; j<n; j++) {
	      gen_tone_put_sample (c, 0, 0);
	    }
#endif

	    layer2_preamble_postamble (c, 32, 0, &modem);
	    layer2_send_frame (c, pp, 0, &modem);
	    layer2_preamble_postamble (c, 2, 1, &modem);
	  }
	  ax25_delete (pp);
	}
}



int main(int argc, char **argv)
{
	int c;
	//int digit_optind = 0;
	int err;
	int packet_count = 0;
	int i;
	int chan;

	int g_opt = 0;
	int j_opt = 0;
	int J_opt = 0;
	int X_opt = 0;		// send FX.25
	int I_opt = -1;		// send IL2P rather than AX.25, normal polarity
	int i_opt = -1;		// send IL2P rather than AX.25, inverted polarity
	double variable_speed_max_error  = 0;	// both in percent
	double variable_speed_increment = 0.1;


/*
 * Set up default values for the modem.
 */

	memset (&modem, 0, sizeof(modem));

	modem.adev[0].defined = 1;
        modem.adev[0].num_channels = DEFAULT_NUM_CHANNELS;              /* -2 stereo */
        modem.adev[0].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;        /* -r option */
        modem.adev[0].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;        /* -8 for 8 instead of 16 bits */
        
	for (chan = 0; chan < MAX_CHANS; chan++) {
	  modem.achan[chan].modem_type = MODEM_AFSK;			/* change with -g */
	  modem.achan[chan].mark_freq = DEFAULT_MARK_FREQ;              /* -m option */
          modem.achan[chan].space_freq = DEFAULT_SPACE_FREQ;            /* -s option */
          modem.achan[chan].baud = DEFAULT_BAUD;                        /* -b option */
	}

	modem.chan_medium[0] = MEDIUM_RADIO;


/*
 * Set up other default values.
 */
	int amplitude = 50;		/* -a option */
					/* 100% is actually half of the digital signal range so */
					/* we have some headroom for adding noise, etc. */

	int leading_zeros = 12;		/* -z option TODO: not implemented, should replace with txdelay frames. */
	char output_file[256];		/* -o option */
	FILE *input_fp = NULL;		/* File or NULL for built-in message */

	strlcpy (output_file, "", sizeof(output_file));

/*
 * Parse the command line options.
 */

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

          c = getopt_long(argc, argv, "gjJm:s:a:b:B:r:n:N:o:z:82M:X:I:i:v:",
                        long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

            case 0:				/* possible future use */

              text_color_set(DW_COLOR_INFO); 
              dw_printf("option %s", long_options[option_index].name);
              if (optarg) {
                dw_printf(" with arg %s", optarg);
              }
              dw_printf("\n");
              break;

            case 'b':				/* -b for data Bit rate */

              modem.achan[0].baud = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Data rate set to %d bits / second.\n", modem.achan[0].baud);
              if (modem.achan[0].baud < MIN_BAUD || modem.achan[0].baud > MAX_BAUD) {
                text_color_set(DW_COLOR_ERROR);
                dw_printf ("Use a more reasonable bit rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
                exit (EXIT_FAILURE);
              }
              break;

            case 'B':				/* -B for data Bit rate */
						/*    300 implies 1600/1800 AFSK. */
						/*    1200 implies 1200/2200 AFSK. */
						/*    9600 implies scrambled. */

						/* If you want something else, specify -B first */
						/* then anything to override these defaults with -m, -s, or -g. */

						// FIXME: options should not be order dependent.

              if (strcasecmp(optarg, "EAS") == 0) {
	        modem.achan[0].baud = 0xEA5EA5;	// See special case below.
	      }
	      else {
	        modem.achan[0].baud = atoi(optarg);
	      }

              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Data rate set to %d bits / second.\n", modem.achan[0].baud);

	      /* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
	      /* that need to be kept in sync.  Maybe it could be a common function someday. */

	      if (modem.achan[0].baud == 100) {			// What was this for?
                  modem.achan[0].modem_type = MODEM_AFSK;
                  modem.achan[0].mark_freq = 1615;
                  modem.achan[0].space_freq = 1785;
	      }
	      else if (modem.achan[0].baud == 0xEA5EA5) {
		  modem.achan[0].baud = 521;			// Fine tuned later. 520.83333
								// Proper fix is to make this float.
                  modem.achan[0].modem_type = MODEM_EAS;
                  modem.achan[0].mark_freq = 2083.3333;		// Ideally these should be floating point.
                  modem.achan[0].space_freq = 1562.5000 ;
	      }
	      else if (modem.achan[0].baud < 600) {
                  modem.achan[0].modem_type = MODEM_AFSK;
                  modem.achan[0].mark_freq = 1600;		// Typical for HF SSB
                  modem.achan[0].space_freq = 1800;
	      }
	      else if (modem.achan[0].baud < 1800) {
                  modem.achan[0].modem_type = MODEM_AFSK;
                  modem.achan[0].mark_freq = DEFAULT_MARK_FREQ;
                  modem.achan[0].space_freq = DEFAULT_SPACE_FREQ;
	      }
	      else if (modem.achan[0].baud < 3600) {
                  modem.achan[0].modem_type = MODEM_QPSK;
                  modem.achan[0].mark_freq = 0;
                  modem.achan[0].space_freq = 0;
                  dw_printf ("Using V.26 QPSK rather than AFSK.\n");
	          if (modem.achan[0].baud != 2400) {
                    text_color_set(DW_COLOR_ERROR); 
	            dw_printf ("Bit rate should be standard 2400 rather than specified %d.\n", modem.achan[0].baud);
	          }
	      }
	      else if (modem.achan[0].baud < 7200) {
                  modem.achan[0].modem_type = MODEM_8PSK;
                  modem.achan[0].mark_freq = 0;
                  modem.achan[0].space_freq = 0;
                  dw_printf ("Using V.27 8PSK rather than AFSK.\n");
	          if (modem.achan[0].baud != 4800) {
                    text_color_set(DW_COLOR_ERROR); 
	            dw_printf ("Bit rate should be standard 4800 rather than specified %d.\n", modem.achan[0].baud);
	          }
	      }
	      else {
                  modem.achan[0].modem_type = MODEM_SCRAMBLE;
                  text_color_set(DW_COLOR_INFO); 
                  dw_printf ("Using scrambled baseband signal rather than AFSK.\n");
	      }
              if (modem.achan[0].baud != 100 && (modem.achan[0].baud < MIN_BAUD || modem.achan[0].baud > MAX_BAUD)) {
                text_color_set(DW_COLOR_ERROR);
                dw_printf ("Use a more reasonable bit rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
                exit (EXIT_FAILURE);
              }
              break;

            case 'g':				/* -g for g3ruh scrambling */

	      g_opt = 1;
              break;

            case 'j':				/* -j V.26 compatible with earlier direwolf. */

	      j_opt = 1;
              break;

            case 'J':				/* -J V.26 compatible with MFJ-2400. */

	      J_opt = 1;
              break;

            case 'm':				/* -m for Mark freq */

              modem.achan[0].mark_freq = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Mark frequency set to %d Hz.\n", modem.achan[0].mark_freq);
              if (modem.achan[0].mark_freq < 300 || modem.achan[0].mark_freq > 3000) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Use a more reasonable value in range of 300 - 3000.\n");
                exit (EXIT_FAILURE);
              }
              break;

            case 's':				/* -s for Space freq */

              modem.achan[0].space_freq = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Space frequency set to %d Hz.\n", modem.achan[0].space_freq);
              if (modem.achan[0].space_freq < 300 || modem.achan[0].space_freq > 3000) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Use a more reasonable value in range of 300 - 3000.\n");
                exit (EXIT_FAILURE);
              }
              break;

            case 'n':				/* -n number of packets with increasing noise. */

	      packet_count = atoi(optarg);
	      g_add_noise = 1;
              break;

            case 'N':				/* -N number of packets.  Don't add noise. */

	      packet_count = atoi(optarg);
	      g_add_noise = 0;
              break;

            case 'a':				/* -a for amplitude */

              amplitude = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Amplitude set to %d%%.\n", amplitude);
              if (amplitude < 0 || amplitude > 200) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Amplitude must be in range of 0 to 200.\n");
                exit (EXIT_FAILURE);
              }
              break;

            case 'r':				/* -r for audio sample Rate */

              modem.adev[0].samples_per_sec = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Audio sample rate set to %d samples / second.\n", modem.adev[0].samples_per_sec);
              if (modem.adev[0].samples_per_sec < MIN_SAMPLES_PER_SEC || modem.adev[0].samples_per_sec > MAX_SAMPLES_PER_SEC) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Use a more reasonable audio sample rate in range of %d - %d.\n",
						MIN_SAMPLES_PER_SEC, MAX_SAMPLES_PER_SEC);
                exit (EXIT_FAILURE);
              }
              break;

            case 'z':				/* -z leading zeros before frame flag */

              leading_zeros = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Send %d zero bits before frame flag.\n", leading_zeros);

	      /* The demodulator needs a few for the clock recovery PLL. */
	      /* We don't want to be here all day either. */
              /* We can't translate to time yet because the data bit rate */
              /* could be changed later. */

              if (leading_zeros < 8 || leading_zeros > 12000) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Use a more reasonable value.\n");
                exit (EXIT_FAILURE);
              }
              break;

            case '8':				/* -8 for 8 bit samples */

              modem.adev[0].bits_per_sample = 8;
              text_color_set(DW_COLOR_INFO); 
              dw_printf("8 bits per audio sample rather than 16.\n");
              break;

            case '2':				/* -2 for 2 channels of sound */
  
              modem.adev[0].num_channels = 2;
	      modem.chan_medium[1] = MEDIUM_RADIO;
              text_color_set(DW_COLOR_INFO); 
              dw_printf("2 channels of sound rather than 1.\n");
              break;

            case 'o':				/* -o for Output file */

              strlcpy (output_file, optarg, sizeof(output_file));
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Output file set to %s\n", output_file);
              break;

            case 'M':				/* -M for morse code speed */

//TODO: document this.
// Why not base it on the destination field instead?

              g_morse_wpm = atoi(optarg);
              text_color_set(DW_COLOR_INFO); 
              dw_printf ("Morse code speed set to %d WPM.\n", g_morse_wpm);
              if (g_morse_wpm < 5 || g_morse_wpm > 50) {
                text_color_set(DW_COLOR_ERROR); 
	        dw_printf ("Morse code speed must be in range of 5 to 50 WPM.\n");
                exit (EXIT_FAILURE);
              }
              break;

            case 'X':

	      X_opt = atoi(optarg);
              break;

            case 'I':			// IL2P, normal polarity

	      I_opt = atoi(optarg);
              break;

            case 'i':			// IL2P, inverted polarity

	      i_opt = atoi(optarg);
              break;

            case 'v':			// Variable speed data + an - this percentage
					// optional comma and increment.

	      variable_speed_max_error = fabs(atof(optarg));
	      char *q = strchr(optarg, ',');
	      if (q != NULL) {
	        variable_speed_increment = fabs(atof(q+1));
	      }
	      break;

            case '?':

              /* Unknown option message was already printed. */
              usage (argv);
              break;

            default:

              /* Should not be here. */
              text_color_set(DW_COLOR_ERROR); 
              dw_printf("?? getopt returned character code 0%o ??\n", (unsigned)c);
              usage (argv);
          }
	}

// These must be processed after -B option.

	if (g_opt) {			/* -g for g3ruh scrambling */

              modem.achan[0].modem_type = MODEM_SCRAMBLE;
              text_color_set(DW_COLOR_INFO);
              dw_printf ("Using G3RUH mode regardless of bit rate.\n");
	}

	if (j_opt) {			/* -j V.26 compatible with earlier direwolf. */

	      modem.achan[0].v26_alternative = V26_A;
              modem.achan[0].modem_type = MODEM_QPSK;
              modem.achan[0].mark_freq = 0;
              modem.achan[0].space_freq = 0;
	      modem.achan[0].baud = 2400;
	}

	if (J_opt) {			/* -J V.26 compatible with MFJ-2400. */

	      modem.achan[0].v26_alternative = V26_B;
              modem.achan[0].modem_type = MODEM_QPSK;
              modem.achan[0].mark_freq = 0;
              modem.achan[0].space_freq = 0;
	      modem.achan[0].baud = 2400;
	}

	if (modem.achan[0].modem_type == MODEM_QPSK &&
	    modem.achan[0].v26_alternative == V26_UNSPECIFIED) {

          text_color_set(DW_COLOR_ERROR);
          dw_printf ("ERROR: Either -j or -J must be specified when using 2400 bps QPSK.\n");
          usage (argv);
          exit (1);
	}

	if (X_opt > 0) {
	    if (I_opt != -1 || i_opt != -1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't mix -X with -I or -i.\n");
	        exit (EXIT_FAILURE);
	    }
	    modem.achan[0].fx25_strength = X_opt;
	    modem.achan[0].layer2_xmit = LAYER2_FX25;
	}

	if (I_opt != -1 && i_opt != -1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Can't use both -I and -i at the same time.\n");
	  exit (EXIT_FAILURE);
	}

	if (I_opt >= 0) {
            text_color_set(DW_COLOR_INFO);
            dw_printf ("Using IL2P normal polarity.\n");
	    modem.achan[0].layer2_xmit = LAYER2_IL2P;
	    modem.achan[0].il2p_max_fec = (I_opt > 0);
	    modem.achan[0].il2p_invert_polarity = 0;	// normal
	}

	if (i_opt >= 0) {
            text_color_set(DW_COLOR_INFO);
            dw_printf ("Using IL2P inverted polarity.\n");
	    modem.achan[0].layer2_xmit = LAYER2_IL2P;
	    modem.achan[0].il2p_max_fec = (i_opt > 0);
	    modem.achan[0].il2p_invert_polarity = 1;	// invert for transmit
	    if (modem.achan[0].baud == 1200) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Using -i with 1200 bps is a bad idea.  Use -I instead.\n");
	    }
	}


/*
 * Open the output file.
 */

        if (strlen(output_file) == 0) {
          text_color_set(DW_COLOR_ERROR); 
          dw_printf ("ERROR: The -o output file option must be specified.\n");
          usage (argv);
          exit (1);
        }

	err = audio_file_open (output_file, &modem);


        if (err < 0) {
          text_color_set(DW_COLOR_ERROR); 
          dw_printf ("ERROR - Can't open output file.\n");
          exit (1);
        }


	gen_tone_init (&modem, amplitude/2, 1);
	morse_init (&modem, amplitude/2);
	dtmf_init (&modem, amplitude/2);

	// We don't have -d or -q options here.
	// Just use the default of minimal information.

	fx25_init (1);
	il2p_init (0);		// There are no "-d" options so far but it could be handy here.

        assert (modem.adev[0].bits_per_sample == 8 || modem.adev[0].bits_per_sample == 16);
        assert (modem.adev[0].num_channels == 1 || modem.adev[0].num_channels == 2);
        assert (modem.adev[0].samples_per_sec >= MIN_SAMPLES_PER_SEC && modem.adev[0].samples_per_sec <= MAX_SAMPLES_PER_SEC);


/*
 * Get user packets(s) from file or stdin if specified.
 * "-n" option is ignored in this case.
 */

	if (optind < argc) {

	  char str[400];

          // dw_printf("non-option ARGV-elements: ");
          // while (optind < argc)
          // dw_printf("%s ", argv[optind++]);
          //dw_printf("\n");

          if (optind < argc - 1) {
            text_color_set(DW_COLOR_ERROR); 
	    dw_printf ("Warning: File(s) beyond the first are ignored.\n");
          }

          if (strcmp(argv[optind], "-") == 0) {
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Reading from stdin ...\n");
            input_fp = stdin;
          }
          else {
            input_fp = fopen(argv[optind], "r");
            if (input_fp == NULL) {
              text_color_set(DW_COLOR_ERROR); 
 	      dw_printf ("Can't open %s for read.\n", argv[optind]);
              exit (EXIT_FAILURE);
            }
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Reading from %s ...\n", argv[optind]);    
          }

          while (fgets (str, sizeof(str), input_fp) != NULL) {
            text_color_set(DW_COLOR_REC); 
            dw_printf ("%s", str);
	    send_packet (str);
	  }

          if (input_fp != stdin) {
            fclose (input_fp);
          }

	  audio_file_close();
    	  return EXIT_SUCCESS;
	}

/* 
 * Otherwise, use the built in packets.
 */
      	text_color_set(DW_COLOR_INFO); 
      	dw_printf ("built in message...\n");

//
// Generate packets with variable speed.
// This overrides any other number of packets or adding noise.
//


	if (variable_speed_max_error != 0) {

	  int normal_speed = modem.achan[0].baud;

          text_color_set(DW_COLOR_INFO);
	  dw_printf ("Variable speed.\n");

	  for (double speed_error = - variable_speed_max_error;
			speed_error <= variable_speed_max_error + 0.001;
			speed_error += variable_speed_increment) {

	    // Baud is int so we get some roundoff.  Make it real?
	    modem.achan[0].baud = (int)round(normal_speed * (1. + speed_error / 100.));
	    gen_tone_init (&modem, amplitude/2, 1);

	    char stemp[256];
	    snprintf (stemp, sizeof(stemp), "WB2OSZ-15>TEST:, speed %+0.1f%%  The quick brown fox jumps over the lazy dog!", speed_error);
	    send_packet (stemp);
	  }
	}	

	else if (packet_count > 0)  {

/*
 * Generate packets with increasing noise level.
 * Would probably be better to record real noise from a radio but
 * for now just use a random number generator.
 */
	  for (i = 1; i <= packet_count; i++) {

	    char stemp[88];
	
	    if (modem.achan[0].baud < 600) {
	      /* e.g. 300 bps AFSK - About 2/3 should be decoded properly. */
	      g_noise_level = amplitude *.0048 * ((float)i / packet_count);
	    }
	    else if (modem.achan[0].baud < 1800) {
	      /* e.g. 1200 bps AFSK - About 2/3 should be decoded properly. */
	      g_noise_level = amplitude *.0023 * ((float)i / packet_count);
	    }
	    else if (modem.achan[0].baud < 3600) {
	      /* e.g. 2400 bps QPSK - T.B.D. */
	      g_noise_level = amplitude *.0015 * ((float)i / packet_count);
	    }
	    else if (modem.achan[0].baud < 7200) {
	      /* e.g. 4800 bps - T.B.D. */
	      g_noise_level = amplitude *.0007 * ((float)i / packet_count);
	    }
	    else {
	      /* e.g. 9600 */
	      g_noise_level = 0.33 * (amplitude / 200.0) * ((float)i / packet_count);
	      // temp test
	      //g_noise_level = 0.20 * (amplitude / 200.0) * ((float)i / packet_count);
	    }

	    snprintf (stemp, sizeof(stemp), "WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  %04d of %04d", i, packet_count);

	    send_packet (stemp);

	  }
	}
	else {

	  // This should send a total of 6.
	  // Note that sticking in the user defined type {DE is optional.

	  if (modem.achan[0].modem_type == MODEM_EAS) {
	    send_packet ("X>X-3:{DEZCZC-WXR-RWT-033019-033017-033015-033013-033011-025011-025017-033007-033005-033003-033001-025009-025027-033009+0015-1691525-KGYX/NWS-");
	    send_packet ("X>X-2:{DENNNN");
	    send_packet ("X>X:NNNN");
	  }
	  else {
/*
 * Builtin default 4 packets.
 */
	    send_packet ("WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  1 of 4");
	    send_packet ("WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  2 of 4");
	    send_packet ("WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  3 of 4");
	    send_packet ("WB2OSZ-15>TEST:,The quick brown fox jumps over the lazy dog!  4 of 4");
	  }
	}

	audio_file_close();

    	return EXIT_SUCCESS;
}


static void usage (char **argv)
{

	text_color_set(DW_COLOR_ERROR); 
	dw_printf ("\n");
	dw_printf ("Usage: gen_packets [options] [file]\n");
	dw_printf ("Options:\n");
	dw_printf ("  -a <number>   Signal amplitude in range of 0 - 200%%.  Default 50.\n");
	dw_printf ("  -b <number>   Bits / second for data.  Default is %d.\n", DEFAULT_BAUD);
	dw_printf ("  -B <number>   Bits / second for data.  Proper modem selected for 300, 1200, 2400, 4800, 9600, EAS.\n");
	dw_printf ("  -g            Scrambled baseband rather than AFSK.\n");
	dw_printf ("  -j            2400 bps QPSK compatible with direwolf <= 1.5.\n");
	dw_printf ("  -J            2400 bps QPSK compatible with MFJ-2400.\n");
	dw_printf ("  -X n           1 to enable FX.25 transmit.  16, 32, 64 for specific number of check bytes.\n");
	dw_printf ("  -I n           Enable IL2P transmit.  n=1 is recommended.  0 uses weaker FEC.\n");
	dw_printf ("  -i n           Enable IL2P transmit, inverted polarity.  n=1 is recommended.  0 uses weaker FEC.\n");
	dw_printf ("  -m <number>   Mark frequency.  Default is %d.\n", DEFAULT_MARK_FREQ);
	dw_printf ("  -s <number>   Space frequency.  Default is %d.\n", DEFAULT_SPACE_FREQ);
	dw_printf ("  -r <number>   Audio sample Rate.  Default is %d.\n", DEFAULT_SAMPLES_PER_SEC);
	dw_printf ("  -n <number>   Generate specified number of frames with increasing noise.\n");
	dw_printf ("  -o <file>     Send output to .wav file.\n");
	dw_printf ("  -8            8 bit audio rather than 16.\n");
	dw_printf ("  -2            2 channels (stereo) audio rather than one channel.\n");
	dw_printf ("  -v max[,incr] Variable speed with specified maximum error and increment.\n");
//	dw_printf ("  -z <number>   Number of leading zero bits before frame.\n");
//	dw_printf ("                  Default is 12 which is .01 seconds at 1200 bits/sec.\n");

	dw_printf ("\n");
	dw_printf ("An optional file may be specified to provide messages other than\n");
	dw_printf ("the default built-in message. The format should correspond to\n");
	dw_printf ("the standard packet monitoring representation such as,\n\n");
	dw_printf ("    WB2OSZ-1>APDW12,WIDE2-2:!4237.14NS07120.83W#\n");
	dw_printf ("User defined content can't be used with -n option.\n");
	dw_printf ("\n");
	dw_printf ("Example:  gen_packets -o x.wav \n");
	dw_printf ("\n");
        dw_printf ("    With all defaults, a built-in test message is generated\n");
	dw_printf ("    with standard Bell 202 tones used for packet radio on ordinary\n");
	dw_printf ("    VHF FM transceivers.\n");
	dw_printf ("\n");
	dw_printf ("Example:  gen_packets -o x.wav -g -b 9600\n");
	dw_printf ("Shortcut: gen_packets -o x.wav -B 9600\n");
	dw_printf ("\n");
        dw_printf ("    9600 baud mode.\n");
	dw_printf ("\n");
	dw_printf ("Example:  gen_packets -o x.wav -m 1600 -s 1800 -b 300\n");
	dw_printf ("Shortcut: gen_packets -o x.wav -B 300\n");
	dw_printf ("\n");
        dw_printf ("    200 Hz shift, 300 baud, suitable for HF SSB transceiver.\n");
	dw_printf ("\n");
	dw_printf ("Example:  echo -n \"WB2OSZ>WORLD:Hello, world!\" | gen_packets -a 25 -o x.wav -\n");
	dw_printf ("\n");
        dw_printf ("    Read message from stdin and put quarter volume sound into the file x.wav.\n");

	exit (EXIT_FAILURE);
}



/*------------------------------------------------------------------
 *
 * Name:        audio_file_open
 *
 * Purpose:     Open a .WAV format file for output.
 *
 * Inputs:      fname		- Name of .WAV file to create.
 *
 *		pa		- Address of structure of type audio_s.
 *				
 *				The fields that we care about are:
 *					num_channels
 *					samples_per_sec
 *					bits_per_sample
 *				If zero, reasonable defaults will be provided.
 *         
 * Returns:     0 for success, -1 for failure.
 *		
 *----------------------------------------------------------------*/

struct wav_header {             /* .WAV file header. */
        char riff[4];           /* "RIFF" */
        int filesize;          /* file length - 8 */
        char wave[4];           /* "WAVE" */
        char fmt[4];            /* "fmt " */
        int fmtsize;           /* 16. */
        short wformattag;       /* 1 for PCM. */
        short nchannels;        /* 1 for mono, 2 for stereo. */
        int nsamplespersec;    /* sampling freq, Hz. */
        int navgbytespersec;   /* = nblockalign * nsamplespersec. */
        short nblockalign;      /* = wbitspersample / 8 * nchannels. */
        short wbitspersample;   /* 16 or 8. */
        char data[4];           /* "data" */
        int datasize;          /* number of bytes following. */
} ;

				/* 8 bit samples are unsigned bytes */
				/* in range of 0 .. 255. */
 
				/* 16 bit samples are signed short */
				/* in range of -32768 .. +32767. */

static FILE *out_fp = NULL;

static struct wav_header header;

static int byte_count;			/* Number of data bytes written to file. */
					/* Will be written to header when file is closed. */


static int audio_file_open (char *fname, struct audio_s *pa)
{
	int n;

/*
 * Fill in defaults for any missing values.
 */
	if (pa -> adev[0].num_channels == 0)
	  pa -> adev[0].num_channels = DEFAULT_NUM_CHANNELS;

	if (pa -> adev[0].samples_per_sec == 0)
	  pa -> adev[0].samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

	if (pa -> adev[0].bits_per_sample == 0)
	  pa -> adev[0].bits_per_sample = DEFAULT_BITS_PER_SAMPLE;


/*
 * Write the file header.  Don't know length yet.
 */
        out_fp = fopen (fname, "wb");	
	
        if (out_fp == NULL) {
           text_color_set(DW_COLOR_ERROR); dw_printf ("Couldn't open file for write: %s\n", fname);
	   perror ("");
           return (-1);
        }

	memset (&header, 0, sizeof(header));

        memcpy (header.riff, "RIFF", (size_t)4);
        header.filesize = 0; 
        memcpy (header.wave, "WAVE", (size_t)4);
        memcpy (header.fmt, "fmt ", (size_t)4);
        header.fmtsize = 16;			// Always 16.
        header.wformattag = 1;     		// 1 for PCM.

        header.nchannels = pa -> adev[0].num_channels;   		
        header.nsamplespersec = pa -> adev[0].samples_per_sec;    
        header.wbitspersample = pa -> adev[0].bits_per_sample;  
		
        header.nblockalign = header.wbitspersample / 8 * header.nchannels;     
        header.navgbytespersec = header.nblockalign * header.nsamplespersec;   
        memcpy (header.data, "data", (size_t)4);
        header.datasize = 0;        

	assert (header.nchannels == 1 || header.nchannels == 2);

        n = fwrite (&header, sizeof(header), (size_t)1, out_fp);

	if (n != 1) {
          text_color_set(DW_COLOR_ERROR); 
	  dw_printf ("Couldn't write header to: %s\n", fname);
	  perror ("");
	  fclose (out_fp);
	  out_fp = NULL;
          return (-1);
        }


/*
 * Number of bytes written will be filled in later.
 */
        byte_count = 0;
	
	return (0);

} /* end audio_open */


/*------------------------------------------------------------------
 *
 * Name:        audio_put
 *
 * Purpose:     Send one byte to the audio output file.
 *
 * Inputs:	c	- One byte in range of 0 - 255.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 * Description:	The caller must deal with the details of mono/stereo
 *		and number of bytes per sample.
 *
 *----------------------------------------------------------------*/


int audio_put (int a, int c)
{
	static short sample16;
	int s;

	if (g_add_noise) {

	  if ((byte_count & 1) == 0) {
	    sample16 = c & 0xff;		/* save lower byte. */
	    byte_count++;
	    return c;
	  }
	  else {
	    float r;

	    sample16 |= (c << 8) & 0xff00;	/* insert upper byte. */
	    byte_count++;
	    s = sample16;  // sign extend.

/* Add random noise to the signal. */
/* r should be in range of -1 .. +1. */

/* Use own function instead of rand() from the C library. */
/* Windows and Linux have different results, messing up my self test procedure. */
/* No idea what Mac OSX and BSD might do. */
 

	    r = (my_rand() - MY_RAND_MAX/2.0) / (MY_RAND_MAX/2.0);

	    s += 5 * r * g_noise_level * 32767;

	    if (s > 32767) s = 32767;
	    if (s < -32767) s = -32767;

	    putc(s & 0xff, out_fp);  
	    return (putc((s >> 8) & 0xff, out_fp));
	  }
	}
	else {
	  byte_count++;
	  return (putc(c, out_fp));
	}

} /* end audio_put */


int audio_flush (int a)
{
	return 0;
}

/*------------------------------------------------------------------
 *
 * Name:        audio_file_close
 *
 * Purpose:     Close the audio output file.
 *
 * Returns:     Normally non-negative.
 *              -1 for any type of error.
 *
 *
 * Description:	Must go back to beginning of file and fill in the
 *		size of the data.
 *
 *----------------------------------------------------------------*/

static int audio_file_close (void)
{
	int n;

        //text_color_set(DW_COLOR_DEBUG); 
	//dw_printf ("audio_close()\n");

/*
 * Go back and fix up lengths in header.
 */
        header.filesize = byte_count + sizeof(header) - 8;           
        header.datasize = byte_count;        

	if (out_fp == NULL) {
	  return (-1);
 	}

        fflush (out_fp);

        fseek (out_fp, 0L, SEEK_SET);         
        n = fwrite (&header, sizeof(header), (size_t)1, out_fp);

	if (n != 1) {
          text_color_set(DW_COLOR_ERROR); 
	  dw_printf ("Couldn't write header to audio file.\n");
	  perror ("");		// TODO: remove perror.
	  fclose (out_fp);
	  out_fp = NULL;
          return (-1);
        }

        fclose (out_fp);
        out_fp = NULL;

	return (0);

} /* end audio_close */


// To keep dtmf.c happy.

#include "hdlc_rec.h"    // for dcd_change

void dcd_change (int chan, int subchan, int slice, int state)
{
}

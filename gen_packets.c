//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2013  John Langner, WB2OSZ
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
 * Purpose:	Test program for generating AFSK AX.25 frames.
 *
 * Description:	Given messages are converted to audio and written 
 *		to a .WAV type audio file.
 *
 *
 * Bugs:	Most options not implemented for second audio channel.
 *
 *------------------------------------------------------------------*/




#include <stdio.h>     
#include <stdlib.h>    
#include <getopt.h>
#include <string.h>
#include <assert.h>

#include "audio.h"
#include "ax25_pad.h"
#include "hdlc_send.h"
#include "gen_tone.h"
#include "textcolor.h"


static void usage (char **argv);
static int audio_file_open (char *fname, struct audio_s *pa);
static int audio_file_close (void);

static int g_add_noise = 0;
static float g_noise_level = 0;



int main(int argc, char **argv)
{
    int c;
    int digit_optind = 0;
    int err;
    unsigned char fbuf[AX25_MAX_PACKET_LEN+2];
    int flen;
    int packet_count = 0;
    int i;
    int chan;

/*
 * Set up default values for the modem.
 */
        struct audio_s modem;

        modem.num_channels = DEFAULT_NUM_CHANNELS;              /* -2 stereo */
        modem.samples_per_sec = DEFAULT_SAMPLES_PER_SEC;        /* -r option */
        modem.bits_per_sample = DEFAULT_BITS_PER_SAMPLE;        /* -8 for 8 instead of 16 bits */
        
	for (chan = 0; chan < MAX_CHANS; chan++) {
	  modem.modem_type[chan] = AFSK;				/* change with -g */
	  modem.mark_freq[chan] = DEFAULT_MARK_FREQ;                    /* -m option */
          modem.space_freq[chan] = DEFAULT_SPACE_FREQ;                  /* -s option */
          modem.baud[chan] = DEFAULT_BAUD;                              /* -b option */
	}

/*
 * Set up other default values.
 */
    int amplitude = 50;			/* -a option */
    int leading_zeros = 12;		/* -z option */
    char output_file[256];		/* -o option */
    FILE *input_fp = NULL;		/* File or NULL for built-in message */

    packet_t pp;


    strcpy (output_file, "");


    while (1) {
        int this_option_optind = optind ? optind : 1;
        int option_index = 0;
        static struct option long_options[] = {
            {"future1", 1, 0, 0},
            {"future2", 0, 0, 0},
            {"future3", 1, 0, 'c'},
            {0, 0, 0, 0}
        };

	/* ':' following option character means arg is required. */

        c = getopt_long(argc, argv, "gm:s:a:b:B:r:n:o:z:82",
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

            modem.baud[0] = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Data rate set to %d bits / second.\n", modem.baud[0]);
            if (modem.baud[0] < 100 || modem.baud[0] > 10000) {
              text_color_set(DW_COLOR_ERROR); 
              dw_printf ("Use a more reasonable bit rate in range of 100 - 10000.\n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'B':				/* -B for data Bit rate */
						/*    300 implies 1600/1800 AFSK. */
						/*    1200 implies 1200/2200 AFSK. */
						/*    9600 implies scrambled. */

            modem.baud[0] = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Data rate set to %d bits / second.\n", modem.baud[0]);
            if (modem.baud[0] < 100 || modem.baud[0] > 10000) {
              text_color_set(DW_COLOR_ERROR); 
              dw_printf ("Use a more reasonable bit rate in range of 100 - 10000.\n");
              exit (EXIT_FAILURE);
            }

	    switch (modem.baud[0]) {
	      case 300:
                modem.mark_freq[0] = 1600;
                modem.space_freq[0] = 1800;
	        break;
	      case 1200:
                modem.mark_freq[0] = 1200;
                modem.space_freq[0] = 2200;
	        break;
	      case 9600:
                modem.modem_type[0] = SCRAMBLE;
                text_color_set(DW_COLOR_INFO); 
                dw_printf ("Using scrambled baseband signal rather than AFSK.\n");
	        break;
	    }
            break;

          case 'g':				/* -g for g3ruh scrambling */

            modem.modem_type[0] = SCRAMBLE;
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Using scrambled baseband signal rather than AFSK.\n");
            break;

          case 'm':				/* -m for Mark freq */

            modem.mark_freq[0] = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Mark frequency set to %d Hz.\n", modem.mark_freq[0]);
            if (modem.mark_freq[0] < 300 || modem.mark_freq[0] > 3000) {
              text_color_set(DW_COLOR_ERROR); 
	      dw_printf ("Use a more reasonable value in range of 300 - 3000.\n");
              exit (EXIT_FAILURE);
            }
            break;

          case 's':				/* -s for Space freq */

            modem.space_freq[0] = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Space frequency set to %d Hz.\n", modem.space_freq[0]);
            if (modem.space_freq[0] < 300 || modem.space_freq[0] > 3000) {
              text_color_set(DW_COLOR_ERROR); 
	      dw_printf ("Use a more reasonable value in range of 300 - 3000.\n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'n':				/* -n number of packets with increasing noise. */

	    packet_count = atoi(optarg);

	    g_add_noise = 1;

            break;

          case 'a':				/* -a for amplitude */

            amplitude = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Amplitude set to %d%%.\n", amplitude);
            if (amplitude < 0 || amplitude > 100) {
              text_color_set(DW_COLOR_ERROR); 
	      dw_printf ("Amplitude must be in range of 0 to 100.\n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'r':				/* -r for audio sample Rate */

            modem.samples_per_sec = atoi(optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Audio sample rate set to %d samples / second.\n", modem.samples_per_sec);
            if (modem.samples_per_sec < MIN_SAMPLES_PER_SEC || modem.samples_per_sec > MAX_SAMPLES_PER_SEC) {
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
            /* We can't translast to time yet because the data bit rate */
            /* could be changed later. */

            if (leading_zeros < 8 || leading_zeros > 12000) {
              text_color_set(DW_COLOR_ERROR); 
	      dw_printf ("Use a more reasonable value.\n");
              exit (EXIT_FAILURE);
            }
            break;

          case '8':				/* -8 for 8 bit samples */

            modem.bits_per_sample = 8;
            text_color_set(DW_COLOR_INFO); 
            dw_printf("8 bits per audio sample rather than 16.\n");
            break;

          case '2':				/* -2 for 2 channels of sound */

            modem.num_channels = 2;
            text_color_set(DW_COLOR_INFO); 
            dw_printf("2 channels of sound rather than 1.\n");
            break;

          case 'o':				/* -o for Output file */

            strcpy (output_file, optarg);
            text_color_set(DW_COLOR_INFO); 
            dw_printf ("Output file set to %s\n", output_file);
            break;

          case '?':

            /* Unknown option message was already printed. */
            usage (argv);
            break;

          default:

            /* Should not be here. */
            text_color_set(DW_COLOR_ERROR); 
            dw_printf("?? getopt returned character code 0%o ??\n", c);
            usage (argv);
        }
    }

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
	}

        if (input_fp != stdin) {
          fclose (input_fp);
        }
    }
    else {
      text_color_set(DW_COLOR_INFO); 
      dw_printf ("built in message...\n");
    }


        if (strlen(output_file) == 0) {
          text_color_set(DW_COLOR_ERROR); 
          dw_printf ("ERROR: The -o ouput file option must be specified.\n");
          usage (argv);
          exit (1);
        }

	err = audio_file_open (output_file, &modem);


        if (err < 0) {
          text_color_set(DW_COLOR_ERROR); 
          dw_printf ("ERROR - Can't open output file.\n");
          exit (1);
        }


	gen_tone_init (&modem, amplitude);

        assert (modem.bits_per_sample == 8 || modem.bits_per_sample == 16);
        assert (modem.num_channels == 1 || modem.num_channels == 2);
        assert (modem.samples_per_sec >= MIN_SAMPLES_PER_SEC && modem.samples_per_sec <= MAX_SAMPLES_PER_SEC);


	if (packet_count > 0)  {

/*
 * Generate packets with increasing noise level.
 * Would probably be better to record real noise from a radio but
 * for now just use a random number generator.
 */
	  for (i = 1; i <= packet_count; i++) {

	    char stemp[80];
	
	    if (modem.modem_type[0] == SCRAMBLE) {
	      g_noise_level = 0.33 * (amplitude / 100.0) * ((float)i / packet_count);
	    }
	    else {
	      g_noise_level = (amplitude / 100.0) * ((float)i / packet_count);
	    }

	    sprintf (stemp, "WB2OSZ-1>APRS,W1AB-9,W1ABC-10,WB1ABC-15:,Hello, world!  %04d", i);

	    pp = ax25_from_text (stemp, 1);
	    flen = ax25_pack (pp, fbuf);
	    for (c=0; c<modem.num_channels; c++)
	    {
	      hdlc_send_flags (c, 8, 0);
	      hdlc_send_frame (c, fbuf, flen);
	      hdlc_send_flags (c, 2, 1);
	    }
	    ax25_delete (pp);
	  }
	}
	else {

/*
 * Builtin default 4 packets.
 */
	  pp = ax25_from_text ("WB2OSZ-1>APRS,W1AB-9,W1ABC-10,WB1ABC-15:,Hello, world!", 1);
	  flen = ax25_pack (pp, fbuf);
	  for (c=0; c<modem.num_channels; c++)
	  {
	      hdlc_send_flags (c, 8, 0);
	      hdlc_send_frame (c, fbuf, flen);
	      hdlc_send_flags (c, 2, 1);
	  }
	  ax25_delete (pp);

	  hdlc_send_flags (c, 8, 0);
	
	  pp = ax25_from_text ("WB2OSZ-1>APRS,W1AB-9*,W1ABC-10,WB1ABC-15:,Hello, world!", 1);
	  flen = ax25_pack (pp, fbuf);
	  for (c=0; c<modem.num_channels; c++)
	  {
	    hdlc_send_frame (c, fbuf, flen);
	  }
	  ax25_delete (pp);

	  pp = ax25_from_text ("WB2OSZ-1>APRS,W1AB-9,W1ABC-10*,WB1ABC-15:,Hello, world!", 1);
	  flen = ax25_pack (pp, fbuf);
	  for (c=0; c<modem.num_channels; c++)
	  {
	    hdlc_send_frame (c, fbuf, flen);
	  }
	  ax25_delete (pp);

	  pp = ax25_from_text ("WB2OSZ-1>APRS,W1AB-9,W1ABC-10,WB1ABC-15*:,Hello, world!", 1);
	  flen = ax25_pack (pp, fbuf);
	  for (c=0; c<modem.num_channels; c++)
	  {
	    hdlc_send_frame (c, fbuf, flen);
	  }
	  ax25_delete (pp);

	  hdlc_send_flags (c, 2, 1);

	}

	audio_file_close();

    	return EXIT_SUCCESS;
}


static void usage (char **argv)
{

	text_color_set(DW_COLOR_ERROR); 
	dw_printf ("\n");
	dw_printf ("Usage: xxx [options] [file]\n");
	dw_printf ("Options:\n");
	dw_printf ("  -a <number>   Signal amplitude in range of 0 - 100%%.  Default 50.\n");
	dw_printf ("  -b <number>   Bits / second for data.  Default is %d.\n", DEFAULT_BAUD);
	dw_printf ("  -B <number>   Bits / second for data.  Proper modem selected for 300, 1200, 9600.\n");
	dw_printf ("  -g            Scrambled baseband rather than AFSK.\n");
	dw_printf ("  -m <number>   Mark frequency.  Default is %d.\n", DEFAULT_MARK_FREQ);
	dw_printf ("  -s <number>   Space frequency.  Default is %d.\n", DEFAULT_SPACE_FREQ);
	dw_printf ("  -r <number>   Audio sample Rate.  Default is %d.\n", DEFAULT_SAMPLES_PER_SEC);
	dw_printf ("  -n <number>   Generate specified number of frames with increasing noise.\n");
	dw_printf ("  -o <file>     Send output to .wav file.\n");
	dw_printf ("  -8            8 bit audio rather than 16.\n");
	dw_printf ("  -2            2 channels of audio rather than 1.\n");
	dw_printf ("  -z <number>   Number of leading zero bits before frame.\n");
	dw_printf ("                  Default is 12 which is .01 seconds at 1200 bits/sec.\n");

	dw_printf ("\n");
	dw_printf ("An optional file may be specified to provide messages other than\n");
	dw_printf ("the default built-n message. The format should correspond to ...\n");
	dw_printf ("blah blah blah.  For example,\n");
	dw_printf ("    WB2OSZ-1>APDW10,WIDE2-2:!4237.14NS07120.83W#\n");
	dw_printf ("\n");
	dw_printf ("Example:  %s\n", argv[0]);
	dw_printf ("\n");
        dw_printf ("    With all defaults, a built-in test message is generated\n");
	dw_printf ("    with standard Bell 202 tones used for packet radio on ordinary\n");
	dw_printf ("    VHF FM transceivers.\n");
	dw_printf ("\n");
	dw_printf ("Example:  %s -g -b 9600\n", argv[0]);
	dw_printf ("Shortcut: %s -B 9600\n", argv[0]);
	dw_printf ("\n");
        dw_printf ("    9600 baud mode.\n");
	dw_printf ("\n");
	dw_printf ("Example:  %s -m 1600 -s 1800 -b 300\n", argv[0]);
	dw_printf ("Shortcut: %s -B 300\n", argv[0]);
	dw_printf ("\n");
        dw_printf ("    200 Hz shift, 300 baud, suitable for HF SSB transceiver.\n");
	dw_printf ("\n");
	dw_printf ("Example:  echo -n \"WB2OSZ>WORLD:Hello, world!\" | %s -a 25 -o x.wav -\n", argv[0]);
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
	if (pa -> num_channels == 0)
	  pa -> num_channels = DEFAULT_NUM_CHANNELS;

	if (pa -> samples_per_sec == 0)
	  pa -> samples_per_sec = DEFAULT_SAMPLES_PER_SEC;

	if (pa -> bits_per_sample == 0)
	  pa -> bits_per_sample = DEFAULT_BITS_PER_SAMPLE;


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

        header.nchannels = pa -> num_channels;   		
        header.nsamplespersec = pa -> samples_per_sec;    
        header.wbitspersample = pa -> bits_per_sample;  
		
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


int audio_put (int c)
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
	    
	    r = (rand() - RAND_MAX/2.0) / (RAND_MAX/2.0);

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


int audio_flush ()
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

        text_color_set(DW_COLOR_DEBUG); 
	dw_printf ("audio_close()\n");

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


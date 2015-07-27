
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


/*-------------------------------------------------------------------
 *
 * Name:        atest.c
 *
 * Purpose:     Unit test for the AFSK demodulator.
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
 *			This results in .WMA files.
 *		
 *		(4) Upload the .WMA file(s) to http://media.io/ and
 *			convert to .WAV format.
 *
 *
 * Comparison to others:
 *
 *	Here are some other scores from Track 2 of the TNC Test CD:
 *		http://sites.google.com/site/ki4mcw/Home/arduino-tnc
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
 *	  Version 0.4 decoded 870 packets.  
 *
 *	  After a little tweaking, version 0.5 decodes 931 packets.
 *
 *	  After more tweaking, version 0.6 gets 965 packets.
 *	  This is without the option to retry after getting a bad FCS.
 *
 *--------------------------------------------------------------------*/

// #define X 1


#include <stdio.h>
#include <unistd.h>
//#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <getopt.h>


#define ATEST_C 1

#include "audio.h"
#include "demod.h"
// #include "fsk_demod_agc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"



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

					/* 8 bit samples are unsigned bytes */
					/* in range of 0 .. 255. */
 
 					/* 16 bit samples are signed short */
					/* in range of -32768 .. +32767. */

static struct wav_header header;
static FILE *fp;
static int e_o_f;
static int packets_decoded = 0;
static int decimate = 1;		/* Reduce that sampling rate. */
					/* 1 = normal, 2 = half, etc. */


int main (int argc, char *argv[])
{

	//int err;
	int c;
	struct audio_s modem;
	int channel;
	time_t start_time;

	text_color_init(1);
	text_color_set(DW_COLOR_INFO);

/* 
 * First apply defaults.
 */
	
	memset (&modem, 0, sizeof(modem));

	modem.num_channels = DEFAULT_NUM_CHANNELS;		
	modem.samples_per_sec = DEFAULT_SAMPLES_PER_SEC;	
	modem.bits_per_sample = DEFAULT_BITS_PER_SAMPLE;	

	/* TODO: should have a command line option for this. */
	/* Results v0.9: 971/69, 990/64, 992/65, 992/67, 1004/476 */

	modem.fix_bits = RETRY_NONE;
	modem.fix_bits = RETRY_SINGLE;
	modem.fix_bits = RETRY_DOUBLE;
	//modem.fix_bits = RETRY_TRIPLE;
	//modem.fix_bits = RETRY_TWO_SEP;

	for (channel=0; channel<MAX_CHANS; channel++) {

	  modem.modem_type[channel] = AFSK;

	  modem.mark_freq[channel] = DEFAULT_MARK_FREQ;		
	  modem.space_freq[channel] = DEFAULT_SPACE_FREQ;		
	  modem.baud[channel] = DEFAULT_BAUD;	
	  strcpy (modem.profiles[channel], "C");	
	// temp	
	// strcpy (modem.profiles[channel], "F");		
	  modem.num_subchan[channel] = strlen(modem.profiles[channel]);	

	  modem.num_freq[channel] = 1;				
	  modem.offset[channel] = 0;				
// temp test
	  //modem.num_subchan[channel] = modem.num_freq[channel] = 3;
	  //modem.num_subchan[channel] = modem.num_freq[channel] = 5;				
	  //modem.offset[channel] = 100;				

	  //strcpy (modem.ptt_device[channel], "");
	  //modem.ptt_line[channel] = PTT_NONE;

	  //modem.slottime[channel] = DEFAULT_SLOTTIME;				
	  //modem.persist[channel] = DEFAULT_PERSIST;				
	  //modem.txdelay[channel] = DEFAULT_TXDELAY;				
	  //modem.txtail[channel] = DEFAULT_TXTAIL;				
	}

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

          c = getopt_long(argc, argv, "B:P:D:",
                        long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

            case 'B':				/* -B for data Bit rate */
						/*    300 implies 1600/1800 AFSK. */
						/*    1200 implies 1200/2200 AFSK. */
						/*    9600 implies scrambled. */

              modem.baud[0] = atoi(optarg);

              printf ("Data rate set to %d bits / second.\n", modem.baud[0]);

              if (modem.baud[0] < 100 || modem.baud[0] > 10000) {
                fprintf (stderr, "Use a more reasonable bit rate in range of 100 - 10000.\n");
                exit (EXIT_FAILURE);
              }
	      if (modem.baud[0] < 600) {
                modem.modem_type[0] = AFSK;
                modem.mark_freq[0] = 1600;
                modem.space_freq[0] = 1800;
	      }
	      else if (modem.baud[0] > 2400) {
                modem.modem_type[0] = SCRAMBLE;
                modem.mark_freq[0] = 0;
                modem.space_freq[0] = 0;
                printf ("Using scrambled baseband signal rather than AFSK.\n");
	      }
	      else {
                modem.modem_type[0] = AFSK;
                modem.mark_freq[0] = 1200;
                modem.space_freq[0] = 2200;
	      }
              break;

	    case 'P':				/* -P for modem profile. */

	      printf ("Demodulator profile set to \"%s\"\n", optarg);
	      strcpy (modem.profiles[0], optarg); 
	      break;	

	    case 'D':				/* -D reduce sampling rate for lower CPU usage. */

	      decimate = atoi(optarg);
	      printf ("Decimate factor = %d\n", decimate);
	      modem.decimate[0] = decimate;
	      break;	

            case '?':

              /* Unknown option message was already printed. */
              //usage (argv);
              break;

            default:

              /* Should not be here. */
              printf("?? getopt returned character code 0%o ??\n", c);
              //usage (argv);
	  }
        }
    
	if (optind >= argc) {
	  printf ("Specify .WAV file name on command line.\n");
	  exit (1);
	}

	fp = fopen(argv[optind], "rb");
        if (fp == NULL) {
	  text_color_set(DW_COLOR_ERROR);
           fprintf (stderr, "Couldn't open file for read: %s\n", argv[optind]);
	   //perror ("more info?");
           exit (1);
        }

	start_time = time(NULL);


/*
 * Read the file header.  
 */

        fread (&header, sizeof(header), (size_t)1, fp);

	assert (header.nchannels == 1 || header.nchannels == 2);
	assert (header.wbitspersample == 8 || header.wbitspersample == 16);

        modem.samples_per_sec = header.nsamplespersec;
	modem.samples_per_sec = modem.samples_per_sec;
	modem.bits_per_sample = header.wbitspersample;
 	modem.num_channels = header.nchannels;

	text_color_set(DW_COLOR_INFO);
	printf ("%d samples per second\n", modem.samples_per_sec);
	printf ("%d bits per sample\n", modem.bits_per_sample);
	printf ("%d audio channels\n", modem.num_channels);
	printf ("%d audio bytes in file\n", (int)(header.datasize));

		
/*
 * Initialize the AFSK demodulator and HDLC decoder.
 */
	multi_modem_init (&modem);


	e_o_f = 0;
	while ( ! e_o_f) 
	{


          int audio_sample;
          int c;

          for (c=0; c<modem.num_channels; c++)
          {

            /* This reads either 1 or 2 bytes depending on */
            /* bits per sample.  */

            audio_sample = demod_get_sample ();

            if (audio_sample >= 256 * 256)
              e_o_f = 1;

#define ONE_CHAN 1              /* only use one audio channel. */

#if ONE_CHAN
            if (c != 0) continue;
#endif

            multi_modem_process_sample(c,audio_sample);
          }

                /* When a complete frame is accumulated, */
                /* process_rec_frame, below, is called. */

	}

	text_color_set(DW_COLOR_INFO);
	printf ("\n\n");
	printf ("%d packets decoded in %d seconds.\n", packets_decoded, (int)(time(NULL) - start_time));

	exit (0);
}


/*
 * Simulate sample from the audio device.
 */

int audio_get (void)
{
	int ch;

	ch = getc(fp);

	if (ch < 0) e_o_f = 1;

	return (ch);
}



/*
 * Rather than queuing up frames with bad FCS, 
 * try to fix them immediately.
 */

void rdq_append (rrbb_t rrbb)
{
	int chan;
	int alevel;
	int subchan;


	chan = rrbb_get_chan(rrbb);
	subchan = rrbb_get_subchan(rrbb);
	alevel = rrbb_get_audio_level(rrbb);

	hdlc_rec2_try_to_fix_later (rrbb, chan, subchan, alevel);
}


/*
 * This is called when we have a good frame.
 */

void app_process_rec_packet (int chan, int subchan, packet_t pp, int alevel, retry_t retries, char *spectrum)  
{	
	
	//int err;
	//char *p;
	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	int h;
	char heard[20];
	//packet_t pp;


	packets_decoded++;


	ax25_format_addrs (pp, stemp);

	info_len = ax25_get_info (pp, &pinfo);

	/* Print so we can see what is going on. */

#if 1
	/* Display audio input level. */
        /* Who are we hearing?   Original station or digipeater. */

	h = ax25_get_heard(pp);
        ax25_get_addr_with_ssid(pp, h, heard);

	text_color_set(DW_COLOR_DEBUG);
	printf ("\n");

	if (h != AX25_SOURCE) {
	  printf ("Digipeater ");
	}
	printf ("%s audio level = %d   [%s]   %s\n", heard, alevel, retry_text[(int)retries], spectrum);


#endif

// Display non-APRS packets in a different color.

	if (ax25_is_aprs(pp)) {
	  text_color_set(DW_COLOR_REC);
	  printf ("[%d] ", chan);
	}
	else {
	  text_color_set(DW_COLOR_DEBUG);
	  printf ("[%d] ", chan);
	}

	printf ("%s", stemp);			/* stations followed by : */
	ax25_safe_print ((char *)pinfo, info_len, 0);
	printf ("\n");

	ax25_delete (pp);

} /* end app_process_rec_packet */


/* end atest.c */

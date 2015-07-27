
//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011,2012,2013  John Langner, WB2OSZ
//    Contributed by Fabrice Faure for UDP part
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
 * Name:        udp_test.c
 *
 * Purpose:     Unit test for the udp reception with AFSK demodulator.
 *
 * Inputs:	Get data by listening on a given UDP port (first parameter is the udp port)
 *
 * Description:	This can be used to test the AFSK demodulator with udp data
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

#define UDPTEST_C 1

#include "audio.h"
#include "demod.h"
// #include "fsk_demod_agc.h"
#include "textcolor.h"
#include "ax25_pad.h"
#include "hdlc_rec2.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
static FILE *fp;
static int e_o_f;
static int packets_decoded = 0;
//Bytes read in the current UDP socket buffer
static int bytes_read = 0;
//Total bytes read from UDP
static int total_bytes_read = 0;
//UDP socket used for receiving data
static int sock;

//UDP receiving port
#define DEFAULT_UDP_PORT 6667
//Maximum size of the UDP buffer (for allowing IP routing, udp packets are often limited to 1472 bytes)
#define UDP_BUF_MAXLEN 20000
//UDP receiving buffer , may use double or FIFO buffers in the future for better performance
unsigned char udp_buf[UDP_BUF_MAXLEN];

//TODO Provide cmdline parameters or config to change these values
#define DEFAULT_UDP_NUM_CHANNELS 1
#define DEFAULT_UDP_SAMPLES_PER_SEC 48000
#define DEFAULT_UDP_BITS_PER_SAMPLE 16


int main (int argc, char *argv[])
{

	struct audio_s modem;
	int channel;
	time_t start_time;
	int udp_port;

	text_color_init(1);
	text_color_set(DW_COLOR_INFO);
	if (argc < 2) {
		udp_port = DEFAULT_UDP_PORT;
		printf ("Using default UDP port : %d\n", udp_port);
	} else {
		udp_port = atoi(argv[1]);
	}

	struct sockaddr_in si_me;
	int i, slen=sizeof(si_me);
	int data_size = 0;

	if ((sock=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP))==-1) {
		fprintf (stderr, "Couldn't create socket %d\n", errno);
		exit(errno);
	}

	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(6667);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sock, &si_me, sizeof(si_me))==-1) {
		fprintf (stderr, "Couldn't bind socket %d\n", errno);
		exit(errno);
	}
	
#ifdef DEBUG_RECEIVED_DATA
	fp = fopen("udp.raw", "w");
        if (fp == NULL) {
           fprintf (stderr, "Couldn't open file for read: %s\n", argv[1]);
	   //perror ("more info?");
           exit (1);
        }
#endif

	start_time = time(NULL);

/* 
 * First apply defaults.
 * TODO: split config into two parts: _init (use here) and _read (only in direwolf).
 */
	modem.num_channels = DEFAULT_UDP_NUM_CHANNELS;
	modem.samples_per_sec = DEFAULT_UDP_SAMPLES_PER_SEC;	
	modem.bits_per_sample = DEFAULT_UDP_BITS_PER_SAMPLE;	

	/* TODO: should have a command line option for this. */
	//modem.fix_bits = RETRY_NONE;
	//modem.fix_bits = RETRY_SINGLE;
	//modem.fix_bits = RETRY_DOUBLE;
	//modem.fix_bits = RETRY_TRIPLE;
	modem.fix_bits = RETRY_TWO_SEP;
	//Only one channel for UDP
	channel = 0;

	modem.modem_type[channel] = AFSK;		
	modem.mark_freq[channel] = DEFAULT_MARK_FREQ;		
	modem.space_freq[channel] = DEFAULT_SPACE_FREQ;		
	modem.baud[channel] = DEFAULT_BAUD;				

	strcpy (modem.profiles[channel], "C");	
	// temp	
	// strcpy (modem.profiles[channel], "F");		
	modem.num_subchan[channel] = strlen(modem.profiles);	

	//TODO: add -h command line option.
//#define HF 1

#if HF		
	modem.mark_freq[channel] = 1600;		
	modem.space_freq[channel] = 1800;		
	modem.baud[channel] = 300;	
	strcpy (modem.profiles[channel], "B"); 	
	modem.num_subchan[channel] = strlen(modem.profiles);	
#endif


	modem.num_freq[channel] = 1;				
	modem.offset[channel] = 0;				


	text_color_set(DW_COLOR_INFO);
	printf ("%d samples per second\n", modem.samples_per_sec);
	printf ("%d bits per sample\n", modem.bits_per_sample);
	printf ("%d audio channels\n", modem.num_channels);
/*
 * Initialize the AFSK demodulator and HDLC decoder.
 */
	multi_modem_init (&modem);


	e_o_f = 0;
	bytes_read = 0;
	data_size = 0;	

	while ( ! e_o_f ) 
	{

		int audio_sample;
		int c;

		//If all the data in the udp buffer has been processed, get new data from udp socket
		if (bytes_read == data_size) {
			data_size = buffer_get(UDP_BUF_MAXLEN);
			//Got EOF packet 
			if (data_size >= 0 && data_size <= 1) {
				printf("Got NULL packet : terminate decoding (packet received with size %d)", data_size);
				e_o_f = 1;
				break;
			}

			bytes_read = 0; 
		} 


		/* This reads either 1 or 2 bytes depending on */
		/* bits per sample.  */

		audio_sample = demod_get_sample ();

		if (audio_sample >= 256 * 256) 
			e_o_f = 1;
		multi_modem_process_sample(c,audio_sample);

		/* When a complete frame is accumulated, */
		/* process_rec_frame, below, is called. */

	}

	text_color_set(DW_COLOR_INFO);
	printf ("\n\n");
	printf ("%d packets decoded in %d seconds.\n", packets_decoded, (int)(time(NULL) - start_time));
#ifdef DEBUG_RECEIVED_DATA
	fclose(fp);
#endif
	exit (0);
}

int buffer_get (unsigned int size) {
        struct sockaddr_in si_other;
        int slen=sizeof(si_other);
	int ch, res,i;
	if (size > UDP_BUF_MAXLEN) { 
		printf("size too big %d", size);
		return -1;
	}

	res = recvfrom(sock, udp_buf, size, 0, &si_other, &slen);
#ifdef DEBUG_RECEIVED_DATA
	fwrite(udp_buf,res,1,fp);
#endif

	return res;
}
/*
 * Simulate sample from the audio device.
 */
int audio_get (void)
{
	int ch;
	ch = udp_buf[bytes_read];
	bytes_read++;
	total_bytes_read++;

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






/* Current time in seconds but more resolution than time(). */

/* We don't care what date a 0 value represents because we */
/* only use this to calculate elapsed time. */



double dtime_now (void)
{
#if __WIN32__
	/* 64 bit integer is number of 100 nanosecond intervals from Jan 1, 1601. */

	FILETIME ft;
	
	GetSystemTimeAsFileTime (&ft);

	return ((( (double)ft.dwHighDateTime * (256. * 256. * 256. * 256.) + 
			(double)ft.dwLowDateTime ) / 10000000.) - 11644473600.);
#else
	/* tv_sec is seconds from Jan 1, 1970. */

	struct timespec ts;
	int sec, ns;
	double x1, x2;
	double result;

	clock_gettime (CLOCK_REALTIME, &ts);

	//result = (double)(ts.tv_sec) + (double)(ts.tv_nsec) / 1000000000.;
	//result = (double)(ts.tv_sec) + ((double)(ts.tv_nsec) * .001 * .001 *.001);
	sec = (int)(ts.tv_sec);
	ns = (int)(ts.tv_nsec);
	x1 = (double)(sec);
	//x1 = (double)(sec-1300000000);	/* try to work around strange result. */
	//x2 = (double)(ns) * .001 * .001 *.001;
	x2 = (double)(ns/1000000) *.001;
	result = x1 + x2;

	/* Sometimes this returns NAN.  How could that possibly happen? */
	/* This is REALLY BIZARRE! */
	/* Multiplying a number by a billionth often produces NAN. */
	/* Adding a fraction to a number over a billion often produces NAN. */
	
	/* Hardware problem???  Need to test on different computer. */

	if (isnan(result)) {
	  text_color_set(DW_COLOR_ERROR);
	  printf ("\ndtime_now(): %d, %d -> %.3f + %.3f -> NAN!!!\n\n", sec, ns, x1, x2);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	printf ("dtime_now() returns %.3f\n", result);
#endif

	return (result);
#endif
}



/* end atest.c */

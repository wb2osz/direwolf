//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017, 2019, 2020, 2021, 2023  John Langner, WB2OSZ
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
 * Module:      direwolf.c
 *
 * Purpose:   	Main program for "Dire Wolf" which includes:
 *			
 *			Various DSP modems using the "sound card."
 *			AX.25 encoder/decoder.
 *			APRS data encoder / decoder.
 *			APRS digipeater.
 *			KISS TNC emulator.
 *			APRStt (touch tone input) gateway
 *			Internet Gateway (IGate)
 *			Ham Radio of Things - IoT with Ham Radio
 *			FX.25 Forward Error Correction.
 *			IL2P Forward Error Correction.
 *			Emergency Alert System (EAS) Specific Area Message Encoding (SAME) receiver.
 *			AIS receiver for tracking ships.
 *
 *---------------------------------------------------------------*/


#define DIREWOLF_C 1

#include "direwolf.h"



#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>

#if __ARM__
//#include <asm/hwcap.h>
//#include <sys/auxv.h>		// Doesn't seem to be there.
				// We have libc 2.13.  Looks like we might need 2.17 & gcc 4.8
#endif

#if __WIN32__
#include <stdio.h>
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#if USE_SNDIO || __APPLE__
// no need to include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#if USE_HAMLIB
#include <hamlib/rig.h>
#endif



#include "version.h"
#include "audio.h"
#include "config.h"
#include "multi_modem.h"
#include "demod.h"
#include "hdlc_rec.h"
#include "hdlc_rec2.h"
#include "ax25_pad.h"
#include "xid.h"
#include "decode_aprs.h"
#include "encode_aprs.h"
#include "textcolor.h"
#include "server.h"
#include "kiss.h"
#include "kissnet.h"
#include "kissserial.h"
#include "kiss_frame.h"
#include "waypoint.h"
#include "gen_tone.h"
#include "digipeater.h"
#include "cdigipeater.h"
#include "tq.h"
#include "xmit.h"
#include "ptt.h"
#include "beacon.h"
#include "dtmf.h"
#include "aprs_tt.h"
#include "tt_user.h"
#include "igate.h"
#include "pfilter.h"
#include "symbols.h"
#include "dwgps.h"
#include "waypoint.h"
#include "log.h"
#include "recv.h"
#include "morse.h"
#include "mheard.h"
#include "ax25_link.h"
#include "dtime_now.h"
#include "fx25.h"
#include "il2p.h"
#include "dwsock.h"
#include "dns_sd_dw.h"
#include "dlq.h"		// for fec_type_t definition.


//static int idx_decoded = 0;

#if __WIN32__
static BOOL cleanup_win (int);
#else
static void cleanup_linux (int);
#endif

static void usage ();

#if defined(__SSE__) && !defined(__APPLE__)

static void __cpuid(int cpuinfo[4], int infotype){
    __asm__ __volatile__ (
        "cpuid":
        "=a" (cpuinfo[0]),
        "=b" (cpuinfo[1]),
        "=c" (cpuinfo[2]),
        "=d" (cpuinfo[3]):
        "a" (infotype)
    );
}

#endif


/*-------------------------------------------------------------------
 *
 * Name:        main
 *
 * Purpose:     Main program for packet radio virtual TNC.
 *
 * Inputs:	Command line arguments.
 *		See usage message for details.
 *
 * Outputs:	Decoded information is written to stdout.
 *
 *		A socket and pseudo terminal are created for 
 *		for communication with other applications.
 *
 *--------------------------------------------------------------------*/

static struct audio_s audio_config;
static struct tt_config_s tt_config;
static struct misc_config_s misc_config;


static const int audio_amplitude = 100;	/* % of audio sample range. */
					/* This translates to +-32k for 16 bit samples. */
					/* Currently no option to change this. */

static int d_u_opt = 0;			/* "-d u" command line option to print UTF-8 also in hexadecimal. */
static int d_p_opt = 0;			/* "-d p" option for dumping packets over radio. */				

static int q_h_opt = 0;			/* "-q h" Quiet, suppress the "heard" line with audio level. */
static int q_d_opt = 0;			/* "-q d" Quiet, suppress the printing of decoded of APRS packets. */

static int A_opt_ais_to_obj = 0;	/* "-A" Convert received AIS to APRS "Object Report." */


int main (int argc, char *argv[])
{
	int err;
	//int eof;
	int j;
	char config_file[100];
	int enable_pseudo_terminal = 0;
	struct digi_config_s digi_config;
	struct cdigi_config_s cdigi_config;
	struct igate_config_s igate_config;
	int r_opt = 0, n_opt = 0, b_opt = 0, B_opt = 0, D_opt = 0, U_opt = 0;	/* Command line options. */
	char P_opt[16];
	char l_opt_logdir[80];
	char L_opt_logfile[80];
	char input_file[80];
	char T_opt_timestamp[40];
	
	int t_opt = 1;		/* Text color option. */				
	int a_opt = 0;		/* "-a n" interval, in seconds, for audio statistics report.  0 for none. */
	int g_opt = 0;		/* G3RUH mode, ignoring default for speed. */				
	int j_opt = 0;		/* 2400 bps PSK compatible with direwolf <= 1.5 */
	int J_opt = 0;		/* 2400 bps PSK compatible MFJ-2400 and maybe others. */

	int d_k_opt = 0;	/* "-d k" option for serial port KISS.  Can be repeated for more detail. */					
	int d_n_opt = 0;	/* "-d n" option for Network KISS.  Can be repeated for more detail. */	
	int d_t_opt = 0;	/* "-d t" option for Tracker.  Can be repeated for more detail. */	
	int d_g_opt = 0;	/* "-d g" option for GPS. Can be repeated for more detail. */
	int d_o_opt = 0;	/* "-d o" option for output control such as PTT and DCD. */	
	int d_i_opt = 0;	/* "-d i" option for IGate.  Repeat for more detail */
	int d_m_opt = 0;	/* "-d m" option for mheard list. */
	int d_f_opt = 0;	/* "-d f" option for filtering.  Repeat for more detail. */
#if USE_HAMLIB
	int d_h_opt = 0;	/* "-d h" option for hamlib debugging.  Repeat for more detail */
#endif
	int d_x_opt = 1;	/* "-d x" option for FX.25.  Default minimal. Repeat for more detail.  -qx to silence. */
	int d_2_opt = 0;	/* "-d 2" option for IL2P.  Default minimal. Repeat for more detail. */

	int aprstt_debug = 0;	/* "-d d" option for APRStt (think Dtmf) debug. */

	int E_tx_opt = 0;		/* "-E n" Error rate % for clobbering transmit frames. */
	int E_rx_opt = 0;		/* "-E Rn" Error rate % for clobbering receive frames. */

	float e_recv_ber = 0.0;		/* Receive Bit Error Rate (BER). */
	int X_fx25_xmit_enable = 0;	/* FX.25 transmit enable. */

	int I_opt = -1;		/* IL2P transmit, normal polarity, arg is max_fec. */
	int i_opt = -1;		/* IL2P transmit, inverted polarity, arg is max_fec. */

	char x_opt_mode = ' ';		/* "-x N" option for transmitting calibration tones. */
	int x_opt_chan = 0;		/* Split into 2 parts.  Mode e.g.  m, a, and optional channel. */

	strlcpy(l_opt_logdir, "", sizeof(l_opt_logdir));
	strlcpy(L_opt_logfile, "", sizeof(L_opt_logfile));
	strlcpy(P_opt, "", sizeof(P_opt));
	strlcpy(T_opt_timestamp, "", sizeof(T_opt_timestamp));

#if __WIN32__

// Select UTF-8 code page for console output.
// http://msdn.microsoft.com/en-us/library/windows/desktop/ms686036(v=vs.85).aspx
// This is the default I see for windows terminal:  
// >chcp
// Active code page: 437

	//Restore on exit? oldcp = GetConsoleOutputCP();
	SetConsoleOutputCP(CP_UTF8);

#else

/*
 * Default on Raspian & Ubuntu Linux is fine.  Don't know about others.
 *
 * Should we look at LANG environment variable and issue a warning
 * if it doesn't look something like  en_US.UTF-8 ?
 */

#endif

/*
 * Pre-scan the command line options for the text color option.
 * We need to set this before any text output.
 * Default will be no colors if stdout is not a terminal (i.e. piped into
 * something else such as "tee") but command line can override this.
 */

#if __WIN32__
	t_opt = _isatty(_fileno(stdout)) > 0;
#else
	t_opt = isatty(fileno(stdout));
#endif
				/* 1 = normal, 0 = no text colors. */
				/* 2, 3, ... alternate escape sequences for different terminals. */

// FIXME: consider case of no space between t and number.

	for (j=1; j<argc-1; j++) {
	  if (strcmp(argv[j], "-t") == 0) {
	    t_opt = atoi (argv[j+1]);
	    //dw_printf ("DEBUG: text color option = %d.\n", t_opt);
	  }
	}

	// TODO: control development/beta/release by version.h instead of changing here.
	// Print platform.  This will provide more information when people send a copy the information displayed.

	// Might want to print OS version here.   For Windows, see:
	// https://msdn.microsoft.com/en-us/library/ms724451(v=VS.85).aspx

	text_color_init(t_opt);
	text_color_set(DW_COLOR_INFO);
	//dw_printf ("Dire Wolf version %d.%d (%s) BETA TEST 7\n", MAJOR_VERSION, MINOR_VERSION, __DATE__);
	//dw_printf ("Dire Wolf DEVELOPMENT version %d.%d %s (%s)\n", MAJOR_VERSION, MINOR_VERSION, "G", __DATE__);
	dw_printf ("Dire Wolf version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);


#if defined(ENABLE_GPSD) || defined(USE_HAMLIB) || defined(USE_CM108) || USE_AVAHI_CLIENT || USE_MACOS_DNSSD
	dw_printf ("Includes optional support for: ");
#if defined(ENABLE_GPSD)
	dw_printf (" gpsd");
#endif
#if defined(USE_HAMLIB)
	dw_printf (" hamlib");
#endif
#if defined(USE_CM108)
	dw_printf (" cm108-ptt");
#endif
#if (USE_AVAHI_CLIENT|USE_MACOS_DNSSD)
	dw_printf (" dns-sd");
#endif
	dw_printf ("\n");
#endif


#if __WIN32__
	//setlinebuf (stdout);   setvbuf???
	SetConsoleCtrlHandler ((PHANDLER_ROUTINE)cleanup_win, TRUE);
#else
	setlinebuf (stdout);
	signal (SIGINT, cleanup_linux);
#endif


/* 
 * Starting with version 0.9, the prebuilt Windows version 
 * requires a minimum of a Pentium 3 or equivalent so we can
 * use the SSE instructions.
 * Try to warn anyone using a CPU from the previous
 * century rather than just dying for no apparent reason.
 *
 * Apple computers with Intel processors started with P6. Since the
 * cpu test code was giving Clang compiler grief it has been excluded.
 *
 * Version 1.6: Newer compiler with i686, rather than i386 target.
 * This is running about 10% faster for the same hardware so it would
 * appear the compiler is using newer, more efficient, instructions.
 *
 * According to https://en.wikipedia.org/wiki/P6_(microarchitecture)
 * and https://en.wikipedia.org/wiki/Streaming_SIMD_Extensions
 * the Pentium III still seems to be the minimum required because
 * it has the P6 microarchitecture and SSE instructions.
 *
 * I've never heard any complaints about anyone getting the message below.
 */

#if defined(__SSE__) && !defined(__APPLE__)
	int cpuinfo[4];		// EAX, EBX, ECX, EDX
	__cpuid (cpuinfo, 0);
	if (cpuinfo[0] >= 1) {
	  __cpuid (cpuinfo, 1);
	  //dw_printf ("debug: cpuinfo = %x, %x, %x, %x\n", cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
	  // https://en.wikipedia.org/wiki/CPUID
	  if ( ! ( cpuinfo[3] & (1 << 25))) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("------------------------------------------------------------------\n");
	    dw_printf ("This version requires a minimum of a Pentium 3 or equivalent.\n");
	    dw_printf ("If you are seeing this message, you are probably using a computer\n");
	    dw_printf ("from the previous Century.  See instructions in User Guide for\n");
	    dw_printf ("information on how you can compile it for use with your antique.\n");
	    dw_printf ("------------------------------------------------------------------\n");
	  }
	}
	text_color_set(DW_COLOR_INFO);
#endif

// I've seen many references to people running this as root.
// There is no reason to do that.
// Ordinary users can access audio, gpio, etc. if they are in the correct groups.
// Giving an applications permission to do things it does not need to do
// is a huge security risk.

#ifndef __WIN32__
	if (getuid() == 0 || geteuid() == 0) {
	    text_color_set(DW_COLOR_ERROR);
	    for (int n=0; n<15; n++) {
	      dw_printf ("\n");
	      dw_printf ("Dire Wolf requires only privileges available to ordinary users.\n");
	      dw_printf ("Running this as root is an unnecessary security risk.\n");
	      //SLEEP_SEC(1);
	    }
	}
#endif

/*
 * Default location of configuration file is current directory.
 * Can be overridden by -c command line option.
 * TODO:  Automatically search other places.
 */
	
	strlcpy (config_file, "direwolf.conf", sizeof(config_file));

/*
 * Look at command line options.
 * So far, the only one is the configuration file location.
 */

	strlcpy (input_file, "", sizeof(input_file));
	while (1) {
          //int this_option_optind = optind ? optind : 1;
          int option_index = 0;
	  int c;
	  char *p;
          static struct option long_options[] = {
            {"future1", 1, 0, 0},
            {"future2", 0, 0, 0},
            {"future3", 1, 0, 'c'},
            {0, 0, 0, 0}
          };

	  /* ':' following option character means arg is required. */

          c = getopt_long(argc, argv, "hP:B:gjJD:U:c:px:r:b:n:d:q:t:ul:L:Sa:E:T:e:X:AI:i:",
                        long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

          case 0:				/* possible future use */
	    text_color_set(DW_COLOR_DEBUG);
            dw_printf("option %s", long_options[option_index].name);
            if (optarg) {
                dw_printf(" with arg %s", optarg);
            }
            dw_printf("\n");
            break;

          case 'a':				/* -a for audio statistics interval */

	    a_opt = atoi(optarg);
	    if (a_opt < 0) a_opt = 0;
	    if (a_opt < 10) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Setting such a small audio statistics interval will produce inaccurate sample rate display.\n");
   	    }
            break;

          case 'c':				/* -c for configuration file name */

	    strlcpy (config_file, optarg, sizeof(config_file));
            break;

#if __WIN32__
#else
          case 'p':				/* -p enable pseudo terminal */
		
	    /* We want this to be off by default because it hangs */
	    /* eventually when nothing is reading from other side. */

	    enable_pseudo_terminal = 1;
            break;
#endif

          case 'B':				/* -B baud rate and modem properties. */
						/* Also implies modem type based on speed. */
						/* Special case "AIS" rather than number. */
	    if (strcasecmp(optarg, "AIS") == 0) {
	      B_opt = 12345;	// See special case below.
	    }
	    else if (strcasecmp(optarg, "EAS") == 0) {
	      B_opt = 23456;	// See special case below.
	    }
	    else {
	      B_opt = atoi(optarg);
	    }
            if (B_opt < MIN_BAUD || B_opt > MAX_BAUD) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Use a more reasonable data baud rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
              exit (EXIT_FAILURE);
            }
            break;

          case 'g':				/* -g G3RUH modem, overriding default mode for speed. */
	 
	    g_opt = 1;
            break;

          case 'j':				/* -j V.26 compatible with earlier direwolf. */

	    j_opt = 1;
            break;

          case 'J':				/* -J V.26 compatible with MFJ-2400. */

	    J_opt = 1;
            break;

	  case 'P':				/* -P for modem profile. */

	    //debug: dw_printf ("Demodulator profile set to \"%s\"\n", optarg);
	    strlcpy (P_opt, optarg, sizeof(P_opt)); 
	    break;	

          case 'D':				/* -D divide AFSK demodulator sample rate */
	 
	    D_opt = atoi(optarg);
            if (D_opt < 1 || D_opt > 8) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Crazy value for -D. \n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'U':				/* -U multiply G3RUH demodulator sample rate (upsample) */
	 
	    U_opt = atoi(optarg);
            if (U_opt < 1 || U_opt > 4) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Crazy value for -U. \n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'x':				/* -x N for transmit calibration tones. */
						/* N is composed of a channel number and/or one letter */
						/* for the mode: mark, space, alternate, ptt-only. */

	    for (char *p = optarg; *p != '\0'; p++ ) {
	      switch (*p) {
	      case '0':
	      case '1':
	      case '2':
	      case '3':
	      case '4':
	      case '5':
	      case '6':
	      case '7':
	      case '8':
	      case '9':
	        x_opt_chan = x_opt_chan * 10 + *p - '0';
	        if (x_opt_mode == ' ') x_opt_mode = 'a';
	        break;
	      case 'a':  x_opt_mode = *p; break; // Alternating tones
	      case 'm':  x_opt_mode = *p; break; // Mark tone
	      case 's':  x_opt_mode = *p; break; // Space tone
	      case 'p':  x_opt_mode = *p; break; // Set PTT only
      	      default:
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Invalid option '%c' for -x. Must be a, m, s, or p.\n", *p);
	        text_color_set(DW_COLOR_INFO);
      	    	exit (EXIT_FAILURE);
      	    	break;
      	     }
	    }
	    if (x_opt_chan < 0 || x_opt_chan >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid channel %d for -x. \n", x_opt_chan);
	      text_color_set(DW_COLOR_INFO);
	      exit (EXIT_FAILURE);
	    }
            break;

          case 'r':				/* -r audio samples/sec.  e.g. 44100 */
	 
	    r_opt = atoi(optarg);
	    if (r_opt < MIN_SAMPLES_PER_SEC || r_opt > MAX_SAMPLES_PER_SEC) 
	    {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("-r option, audio samples/sec, is out of range.\n");
	      r_opt = 0;
   	    }
            break;

          case 'n':				/* -n number of audio channels for first audio device.  1 or 2. */
	 
	    n_opt = atoi(optarg);
	    if (n_opt < 1 || n_opt > 2) 
	    {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("-n option, number of audio channels, is out of range.\n");
	      n_opt = 0;
   	    }
            break;

          case 'b':				/* -b bits per sample.  8 or 16. */
	 
	    b_opt = atoi(optarg);
	    if (b_opt != 8 && b_opt != 16) 
	    {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("-b option, bits per sample, must be 8 or 16.\n");
	      b_opt = 0;
   	    }
            break;

	  case 'h':			// -h for help
          case '?':

            /* For '?' unknown option message was already printed. */
            usage ();
            break;

	  case 'd':				/* Set debug option. */
	
	    /* New in 1.1.  Can combine multiple such as "-d pkk" */

	    for (p=optarg; *p!='\0'; p++) {
	     switch (*p) {
	
	      case 'a':  server_set_debug(1); break;

	      case 'k':  d_k_opt++; kissserial_set_debug (d_k_opt); kisspt_set_debug (d_k_opt); break;
	      case 'n':  d_n_opt++; kiss_net_set_debug (d_n_opt); break;

	      case 'u':  d_u_opt = 1; break;

		// separate out gps & waypoints.

	      case 'g':  d_g_opt++; break;
	      case 'w':	 waypoint_set_debug (1); break;		// not documented yet.
	      case 't':  d_t_opt++; beacon_tracker_set_debug (d_t_opt); break;

	      case 'p':  d_p_opt = 1; break;			// TODO: packet dump for xmit side.
	      case 'o':  d_o_opt++; ptt_set_debug(d_o_opt); break;	
	      case 'i':  d_i_opt++; break;
	      case 'm':  d_m_opt++; break;
	      case 'f':  d_f_opt++; break;
#if AX25MEMDEBUG
	      case 'l':  ax25memdebug_set(); break;		// Track down memory Leak.  Not documented.
#endif								// Previously 'm' but that is now used for mheard.
#if USE_HAMLIB
	      case 'h':  d_h_opt++; break;			// Hamlib verbose level.
#endif
	      case 'x':  d_x_opt++; break;			// FX.25
	      case '2':  d_2_opt++; break;			// IL2P
	      case 'd':	 aprstt_debug++; break;			// APRStt (mnemonic Dtmf)
	      default: break;
	     }
	    }
	    break;
	      
	  case 'q':				/* Set quiet option. */
	
	    /* New in 1.2.  Quiet option to suppress some types of printing. */
	    /* Can combine multiple such as "-q hd" */

	    for (p=optarg; *p!='\0'; p++) {
	     switch (*p) {
	      case 'h':  q_h_opt = 1; break;
	      case 'd':  q_d_opt = 1; break;
	      case 'x':  d_x_opt = 0; break;	// Defaults to minimal info.  This silences.
	      default: break;
	     }
	    }
	    break;
	      
	  case 't':				/* Was handled earlier. */
	    break;


	  case 'u':				/* Print UTF-8 test and exit. */

	    dw_printf ("\n  UTF-8 test string: ma%c%cana %c%c F%c%c%c%ce\n\n", 
			0xc3, 0xb1,
			0xc2, 0xb0,
			0xc3, 0xbc, 0xc3, 0x9f);

	    exit (0);
	    break;

          case 'l':				/* -l for log directory with daily files */

	    strlcpy (l_opt_logdir, optarg, sizeof(l_opt_logdir));
            break;

          case 'L':				/* -L for log file name with full path */

	    strlcpy (L_opt_logfile, optarg, sizeof(L_opt_logfile));
            break;


	  case 'S':				/* Print symbol tables and exit. */

	    symbols_init ();
	    symbols_list ();
	    exit (0);
	    break;

          case 'E':				/* -E Error rate (%) for corrupting frames. */
						/* Just a number is transmit.  Precede by R for receive. */

	    if (*optarg == 'r' || *optarg == 'R') {
	      E_rx_opt = atoi(optarg+1);
	      if (E_rx_opt < 1 || E_rx_opt > 99) {
	        text_color_set(DW_COLOR_ERROR);
                  dw_printf("-ER must be in range of 1 to 99.\n");
	      E_rx_opt = 10;
	      }
	    }
	    else {
	      E_tx_opt = atoi(optarg);
	      if (E_tx_opt < 1 || E_tx_opt > 99) {
	        text_color_set(DW_COLOR_ERROR);
                dw_printf("-E must be in range of 1 to 99.\n");
	        E_tx_opt = 10;
	      }
	    }
            break;

          case 'T':				/* -T for receive timestamp. */
	    strlcpy (T_opt_timestamp, optarg, sizeof(T_opt_timestamp));
            break;

	  case 'e':				/* -e Receive Bit Error Rate (BER). */

	    e_recv_ber = atof(optarg);
	    break;

          case 'X':

	    X_fx25_xmit_enable = atoi(optarg);
            break;

          case 'I':			// IL2P, normal polarity

	    I_opt = atoi(optarg);
            break;

          case 'i':			// IL2P, inverted polarity

	    i_opt = atoi(optarg);
            break;

	  case 'A':			// -A 	convert AIS to APRS object

	    A_opt_ais_to_obj = 1;
	    break;

          default:

            /* Should not be here. */
	    text_color_set(DW_COLOR_DEBUG);
            dw_printf("?? getopt returned character code 0%o ??\n", c);
            usage ();
          }
	}  /* end while(1) for options */

	if (optind < argc) 
	{

          if (optind < argc - 1) 
	  {
	    text_color_set(DW_COLOR_ERROR);
            dw_printf ("Warning: File(s) beyond the first are ignored.\n");
          }

	  strlcpy (input_file, argv[optind], sizeof(input_file));

	}

/*
 * Get all types of configuration settings from configuration file.
 *
 * Possibly override some by command line options.
 */

#if USE_HAMLIB
        rig_set_debug(d_h_opt);
#endif

	symbols_init ();

	(void)dwsock_init();

	config_init (config_file, &audio_config, &digi_config, &cdigi_config, &tt_config, &igate_config, &misc_config);

	if (r_opt != 0) {
	  audio_config.adev[0].samples_per_sec = r_opt;
	}

	if (n_opt != 0) {
	  audio_config.adev[0].num_channels = n_opt;
	  if (n_opt == 2) {
	    audio_config.chan_medium[1] = MEDIUM_RADIO;
	  }
	}

	if (b_opt != 0) {
	  audio_config.adev[0].bits_per_sample = b_opt;
	}

	if (B_opt != 0) {
	  audio_config.achan[0].baud = B_opt;

	  /* We have similar logic in direwolf.c, config.c, gen_packets.c, and atest.c, */
	  /* that need to be kept in sync.  Maybe it could be a common function someday. */

	  if (audio_config.achan[0].baud < 600) {
            audio_config.achan[0].modem_type = MODEM_AFSK;
            audio_config.achan[0].mark_freq = 1600;		// Typical for HF SSB.
            audio_config.achan[0].space_freq = 1800;
	    audio_config.achan[0].decimate = 3;			// Reduce CPU load.
	  }
	  else if (audio_config.achan[0].baud < 1800) {
            audio_config.achan[0].modem_type = MODEM_AFSK;
            audio_config.achan[0].mark_freq = DEFAULT_MARK_FREQ;
            audio_config.achan[0].space_freq = DEFAULT_SPACE_FREQ;
	  }
	  else if (audio_config.achan[0].baud < 3600) {
            audio_config.achan[0].modem_type = MODEM_QPSK;
            audio_config.achan[0].mark_freq = 0;
            audio_config.achan[0].space_freq = 0;
	    if (audio_config.achan[0].baud != 2400) {
              text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Bit rate should be standard 2400 rather than specified %d.\n", audio_config.achan[0].baud);
	    }
	  }
	  else if (audio_config.achan[0].baud < 7200) {
            audio_config.achan[0].modem_type = MODEM_8PSK;
            audio_config.achan[0].mark_freq = 0;
            audio_config.achan[0].space_freq = 0;
	    if (audio_config.achan[0].baud != 4800) {
              text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Bit rate should be standard 4800 rather than specified %d.\n", audio_config.achan[0].baud);
	    }
	  }
	  else if (audio_config.achan[0].baud == 12345) {
	    audio_config.achan[0].modem_type = MODEM_AIS;
	    audio_config.achan[0].baud = 9600;
	    audio_config.achan[0].mark_freq = 0;
	    audio_config.achan[0].space_freq = 0;
	  }
	  else if (audio_config.achan[0].baud == 23456) {
	    audio_config.achan[0].modem_type = MODEM_EAS;
	    audio_config.achan[0].baud = 521;	// Actually 520.83 but we have an integer field here.
						// Will make more precise in afsk demod init.
	    audio_config.achan[0].mark_freq = 2083;	// Actually 2083.3 - logic 1.
	    audio_config.achan[0].space_freq = 1563;	// Actually 1562.5 - logic 0.
	    strlcpy (audio_config.achan[0].profiles, "A", sizeof(audio_config.achan[0].profiles));
	  }
	  else {
            audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
            audio_config.achan[0].mark_freq = 0;
            audio_config.achan[0].space_freq = 0;
	  }
	}

	if (g_opt) {

	  // Force G3RUH mode, overriding default for speed.
	  //   Example:   -B 2400 -g  

	  audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
          audio_config.achan[0].mark_freq = 0;
          audio_config.achan[0].space_freq = 0;
	}

	if (j_opt) {

	  // V.26 compatible with earlier versions of direwolf.
	  //   Example:   -B 2400 -j    or simply   -j

	  audio_config.achan[0].v26_alternative = V26_A;
          audio_config.achan[0].modem_type = MODEM_QPSK;
          audio_config.achan[0].mark_freq = 0;
          audio_config.achan[0].space_freq = 0;
	  audio_config.achan[0].baud = 2400;
	}
	if (J_opt) {

	  // V.26 compatible with MFJ and maybe others.
	  //   Example:   -B 2400 -J     or simply   -J

	  audio_config.achan[0].v26_alternative = V26_B;
          audio_config.achan[0].modem_type = MODEM_QPSK;
          audio_config.achan[0].mark_freq = 0;
          audio_config.achan[0].space_freq = 0;
	  audio_config.achan[0].baud = 2400;
	}


	audio_config.statistics_interval = a_opt;

	if (strlen(P_opt) > 0) { 
	  /* -P for modem profile. */
	  strlcpy (audio_config.achan[0].profiles, P_opt, sizeof(audio_config.achan[0].profiles)); 
	}	

	if (D_opt != 0) {
	    // Reduce audio sampling rate to reduce CPU requirements.
	    audio_config.achan[0].decimate = D_opt;
	}

	if (U_opt != 0) {
	    // Increase G3RUH audio sampling rate to improve performance.
	    // The value is normally determined automatically based on audio
	    // sample rate and baud.  This allows override for experimentation.
	    audio_config.achan[0].upsample = U_opt;
	}

	strlcpy(audio_config.timestamp_format, T_opt_timestamp, sizeof(audio_config.timestamp_format));

	// temp - only xmit errors.

	audio_config.xmit_error_rate = E_tx_opt;
	audio_config.recv_error_rate = E_rx_opt;


	if (strlen(l_opt_logdir) > 0 && strlen(L_opt_logfile) > 0) {
          text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Logging options -l and -L can't be used together.  Pick one or the other.\n");
	  exit(1);
	}

	if (strlen(L_opt_logfile) > 0) {
	  misc_config.log_daily_names = 0;
	  strlcpy (misc_config.log_path, L_opt_logfile, sizeof(misc_config.log_path));
	}
	else if (strlen(l_opt_logdir) > 0) {
	  misc_config.log_daily_names = 1;
	  strlcpy (misc_config.log_path, l_opt_logdir, sizeof(misc_config.log_path));
	}

	misc_config.enable_kiss_pt = enable_pseudo_terminal;

	if (strlen(input_file) > 0) {

	  strlcpy (audio_config.adev[0].adevice_in, input_file, sizeof(audio_config.adev[0].adevice_in));

	}

	audio_config.recv_ber = e_recv_ber;

	if (X_fx25_xmit_enable > 0) {
	    if (I_opt != -1 || i_opt != -1) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Can't mix -X with -I or -i.\n");
	        exit (EXIT_FAILURE);
	    }
	    audio_config.achan[0].fx25_strength = X_fx25_xmit_enable;
	    audio_config.achan[0].layer2_xmit = LAYER2_FX25;
	}

	if (I_opt != -1 && i_opt != -1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Can't use both -I and -i at the same time.\n");
	  exit (EXIT_FAILURE);
	}

	if (I_opt >= 0) {
	    audio_config.achan[0].layer2_xmit = LAYER2_IL2P;
	    audio_config.achan[0].il2p_max_fec = (I_opt > 0);
	    if (audio_config.achan[0].il2p_max_fec == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("It is highly recommended that 1, rather than 0, is used with -I for best results.\n");
	    }
	    audio_config.achan[0].il2p_invert_polarity = 0;	// normal
	}

	if (i_opt >= 0) {
	    audio_config.achan[0].layer2_xmit = LAYER2_IL2P;
	    audio_config.achan[0].il2p_max_fec = (i_opt > 0);
	    if (audio_config.achan[0].il2p_max_fec == 0) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("It is highly recommended that 1, rather than 0, is used with -i for best results.\n");
	    }
	    audio_config.achan[0].il2p_invert_polarity = 1;	// invert for transmit
	    if (audio_config.achan[0].baud == 1200) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Using -i with 1200 bps is a bad idea.  Use -I instead.\n");
	    }
	}


/*
 * Open the audio source 
 *	- soundcard
 *	- stdin
 *	- UDP
 * Files not supported at this time.
 * Can always "cat" the file and pipe it into stdin.
 */

	err = audio_open (&audio_config);
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Pointless to continue without audio device.\n");
	  SLEEP_SEC(5);
	  usage ();
	  exit (1);
	}

/*
 * Initialize the demodulator(s) and layer 2 decoder (HDLC, IL2P).
 */
	multi_modem_init (&audio_config);
	fx25_init (d_x_opt);
	il2p_init (d_2_opt);

/*
 * Initialize the touch tone decoder & APRStt gateway.
 */
	dtmf_init (&audio_config, audio_amplitude);
	aprs_tt_init (&tt_config, aprstt_debug);
	tt_user_init (&audio_config, &tt_config);

/*
 * Should there be an option for audio output level?
 * Note:  This is not the same as a volume control you would see on the screen.
 * It is the range of the digital sound representation.
*/
	gen_tone_init (&audio_config, audio_amplitude, 0);
	morse_init (&audio_config, audio_amplitude);

	assert (audio_config.adev[0].bits_per_sample == 8 || audio_config.adev[0].bits_per_sample == 16);
	assert (audio_config.adev[0].num_channels == 1 || audio_config.adev[0].num_channels == 2);
	assert (audio_config.adev[0].samples_per_sec >= MIN_SAMPLES_PER_SEC && audio_config.adev[0].samples_per_sec <= MAX_SAMPLES_PER_SEC);

/*
 * Initialize the transmit queue.
 */

	xmit_init (&audio_config, d_p_opt);

/*
 * If -x N option specified, transmit calibration tones for transmitter
 * audio level adjustment, up to 1 minute then quit.
 * a: Alternating mark/space tones
 * m: Mark tone (e.g. 1200Hz)
 * s: Space tone (e.g. 2200Hz)
 * p: Set PTT only.
 * A leading or trailing number is the channel.
 */

	if (x_opt_mode != ' ') {
	  if (audio_config.chan_medium[x_opt_chan] == MEDIUM_RADIO) {
		if (audio_config.achan[x_opt_chan].mark_freq
				&& audio_config.achan[x_opt_chan].space_freq) {
			int max_duration = 60;
			int n = audio_config.achan[x_opt_chan].baud * max_duration;

			text_color_set(DW_COLOR_INFO);
			ptt_set(OCTYPE_PTT, x_opt_chan, 1);

			switch (x_opt_mode) {
			default:
			case 'a':  // Alternating tones: -x a
				dw_printf("\nSending alternating mark/space calibration tones (%d/%dHz) on channel %d.\nPress control-C to terminate.\n",
						audio_config.achan[x_opt_chan].mark_freq,
						audio_config.achan[x_opt_chan].space_freq,
						x_opt_chan);
				while (n-- > 0) {
					tone_gen_put_bit(x_opt_chan, n & 1);
				}
				break;
			case 'm':  // "Mark" tone: -x m
				dw_printf("\nSending mark calibration tone (%dHz) on channel %d.\nPress control-C to terminate.\n",
						audio_config.achan[x_opt_chan].mark_freq,
						x_opt_chan);
				while (n-- > 0) {
					tone_gen_put_bit(x_opt_chan, 1);
				}
				break;
			case 's':  // "Space" tone: -x s
				dw_printf("\nSending space calibration tone (%dHz) on channel %d.\nPress control-C to terminate.\n",
						audio_config.achan[x_opt_chan].space_freq,
						x_opt_chan);
				while (n-- > 0) {
					tone_gen_put_bit(x_opt_chan, 0);
				}
				break;
			case 'p':  // Silence - set PTT only: -x p
				dw_printf("\nSending silence (Set PTT only) on channel %d.\nPress control-C to terminate.\n", x_opt_chan);
				SLEEP_SEC(max_duration);
				break;
			}

			ptt_set(OCTYPE_PTT, x_opt_chan, 0);
			text_color_set(DW_COLOR_INFO);
			exit(EXIT_SUCCESS);

		} else {
			text_color_set(DW_COLOR_ERROR);
			dw_printf("\nMark/Space frequencies not defined for channel %d. Cannot calibrate using this modem type.\n", x_opt_chan);
			text_color_set(DW_COLOR_INFO);
			exit(EXIT_FAILURE);
		}
	    } else {
		text_color_set(DW_COLOR_ERROR);
		dw_printf("\nChannel %d is not configured as a radio channel.\n", x_opt_chan);
		text_color_set(DW_COLOR_INFO);
		exit(EXIT_FAILURE);
	    }
	}


/*
 * Initialize the digipeater and IGate functions.
 */
	digipeater_init (&audio_config, &digi_config);
	igate_init (&audio_config, &igate_config, &digi_config, d_i_opt);
	cdigipeater_init (&audio_config, &cdigi_config);
	pfilter_init (&igate_config, d_f_opt);
	ax25_link_init (&misc_config);

/*
 * Provide the AGW & KISS socket interfaces for use by a client application.
 */
	server_init (&audio_config, &misc_config);
	kissnet_init (&misc_config);

#if (USE_AVAHI_CLIENT|USE_MACOS_DNSSD)
	if (misc_config.kiss_port > 0 && misc_config.dns_sd_enabled)
	  dns_sd_announce(&misc_config);
#endif

/*
 * Create a pseudo terminal and KISS TNC emulator.
 */
	kisspt_init (&misc_config);
	kissserial_init (&misc_config);
	kiss_frame_init (&audio_config);

/*
 * Open port for communication with GPS.
 */
	dwgps_init (&misc_config, d_g_opt);

	waypoint_init (&misc_config);  

/*
 * Enable beaconing.
 * Open log file first because "-dttt" (along with -l...) will
 * log the tracker beacon transmissions with fake channel 999.
 */

	log_init(misc_config.log_daily_names, misc_config.log_path);
	mheard_init (d_m_opt);
	beacon_init (&audio_config, &misc_config, &igate_config);


/*
 * Get sound samples and decode them.
 * Use hot attribute for all functions called for every audio sample.
 */

	recv_init (&audio_config);
	recv_process ();

	exit (EXIT_SUCCESS);
}


/*-------------------------------------------------------------------
 *
 * Name:        app_process_rec_frame
 *
 * Purpose:     This is called when we receive a frame with a valid 
 *		FCS and acceptable size.
 *
 * Inputs:	chan	- Audio channel number, 0 or 1.
 *		subchan	- Which modem caught it.  
 *			  Special case -1 for DTMF decoder.
 *		slice	- Slicer which caught it.
 *		pp	- Packet handle.
 *		alevel	- Audio level, range of 0 - 100.
 *				(Special case, use negative to skip
 *				 display of audio level line.
 *				 Use -2 to indicate DTMF message.)
 *		retries	- Level of bit correction used.
 *		spectrum - Display of how well multiple decoders did.
 *
 *
 * Description:	Print decoded packet.
 *		Optionally send to another application.
 *
 *--------------------------------------------------------------------*/

// TODO:  Use only one printf per line so output doesn't get jumbled up with stuff from other threads.

void app_process_rec_packet (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum)
{	
	
	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	char heard[AX25_MAX_ADDR_LEN];
	//int j;
	int h;
	char display_retries[32];				// Extra stuff before slice indicators.
								// Can indicate FX.25/IL2P or fix_bits.

	assert (chan >= 0 && chan < MAX_TOTAL_CHANS);		// TOTAL for virtual channels
	assert (subchan >= -2 && subchan < MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SLICERS);
	assert (pp != NULL);	// 1.1J+
     
	strlcpy (display_retries, "", sizeof(display_retries));

	switch (fec_type) {
	  case fec_type_fx25:
	    strlcpy (display_retries, " FX.25 ", sizeof(display_retries));
	    break;
	  case fec_type_il2p:
	    strlcpy (display_retries, " IL2P ", sizeof(display_retries));
	    break;
	  case fec_type_none:
	  default:
	    // Possible fix_bits indication.
	    if (audio_config.achan[chan].fix_bits != RETRY_NONE || audio_config.achan[chan].passall) {
	      assert (retries >= RETRY_NONE && retries <= RETRY_MAX);
	      snprintf (display_retries, sizeof(display_retries), " [%s] ", retry_text[(int)retries]);
	    }
	    break;
	}

	ax25_format_addrs (pp, stemp);

	info_len = ax25_get_info (pp, &pinfo);

	/* Print so we can see what is going on. */

	/* Display audio input level. */
        /* Who are we hearing?   Original station or digipeater. */

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

// The HEARD line.

	if (( ! q_h_opt ) && alevel.rec >= 0) {    /* suppress if "-q h" option */
// FIXME: rather than checking for ichannel, how about checking medium==radio
	 if (chan != audio_config.igate_vchannel) {	// suppress if from ICHANNEL
	  if (h != -1 && h != AX25_SOURCE) {
	    dw_printf ("Digipeater ");
	  }

	  char alevel_text[AX25_ALEVEL_TO_TEXT_SIZE];

	  ax25_alevel_to_text (alevel, alevel_text);

// Experiment: try displaying the DC bias.
// Should be 0 for soundcard but could show mistuning with SDR.

#if 0
	  char bias[16];
	  snprintf (bias, sizeof(bias), " DC%+d", multi_modem_get_dc_average (chan));
	  strlcat (alevel_text, bias, sizeof(alevel_text));
#endif

	  /* As suggested by KJ4ERJ, if we are receiving from */
	  /* WIDEn-0, it is quite likely (but not guaranteed), that */
	  /* we are actually hearing the preceding station in the path. */

	  if (h >= AX25_REPEATER_2 && 
	        strncmp(heard, "WIDE", 4) == 0 &&
	        isdigit(heard[4]) &&
	        heard[5] == '\0') {

	    char probably_really[AX25_MAX_ADDR_LEN];


	    ax25_get_addr_with_ssid(pp, h-1, probably_really);

	    dw_printf ("%s (probably %s) audio level = %s  %s  %s\n", heard, probably_really, alevel_text, display_retries, spectrum);

	  }
	  else if (strcmp(heard, "DTMF") == 0) {

	    dw_printf ("%s audio level = %s  tt\n", heard, alevel_text);
	  }
	  else {

	    dw_printf ("%s audio level = %s  %s  %s\n", heard, alevel_text, display_retries, spectrum);
	  }
	 }
	}

	/* Version 1.2:   Cranking the input level way up produces 199. */
	/* Keeping it under 100 gives us plenty of headroom to avoid saturation. */

	// TODO:  suppress this message if not using soundcard input.
	// i.e. we have no control over the situation when using SDR.

	if (alevel.rec > 110) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio input level is too high.  Reduce so most stations are around 50.\n");
	}
// FIXME: rather than checking for ichannel, how about checking medium==radio
	else if (alevel.rec < 5 && chan != audio_config.igate_vchannel) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio input level is too low.  Increase so most stations are around 50.\n");
	}


// Display non-APRS packets in a different color.

// Display subchannel only when multiple modems configured for channel.

// -1 for APRStt DTMF decoder.

	char ts[100];		// optional time stamp

	if (strlen(audio_config.timestamp_format) > 0) {
	  char tstmp[100];
	  timestamp_user_format (tstmp, sizeof(tstmp), audio_config.timestamp_format);
	  strlcpy (ts, " ", sizeof(ts));	// space after channel.
	  strlcat (ts, tstmp, sizeof(ts));
	}
	else {
	  strlcpy (ts, "", sizeof(ts));
	}

	if (subchan == -1) {
	  text_color_set(DW_COLOR_REC);
	  dw_printf ("[%d.dtmf%s] ", chan, ts);
	}
	else if (subchan == -2) {
	  text_color_set(DW_COLOR_REC);
	  dw_printf ("[%d.is%s] ", chan, ts);
	}
	else {
	  if (ax25_is_aprs(pp)) {
	    text_color_set(DW_COLOR_REC);
	  }
	  else {
	    text_color_set(DW_COLOR_DECODED);
	  }

	  if (audio_config.achan[chan].num_subchan > 1 && audio_config.achan[chan].num_slicers == 1) {
	    dw_printf ("[%d.%d%s] ", chan, subchan, ts);
	  }
	  else if (audio_config.achan[chan].num_subchan == 1 && audio_config.achan[chan].num_slicers > 1) {
	    dw_printf ("[%d.%d%s] ", chan, slice, ts);
	  }
	  else if (audio_config.achan[chan].num_subchan > 1 && audio_config.achan[chan].num_slicers > 1) {
	    dw_printf ("[%d.%d.%d%s] ", chan, subchan, slice, ts);
	  }
	  else {
	    dw_printf ("[%d%s] ", chan, ts);
	  }
	}

	dw_printf ("%s", stemp);			/* stations followed by : */

/* Demystify non-APRS.  Use same format for transmitted frames in xmit.c. */

	if ( ! ax25_is_aprs(pp)) {
	  ax25_frame_type_t ftype;
	  cmdres_t cr;
	  char desc[80];
	  int pf;
	  int nr;
	  int ns;

	  ftype = ax25_frame_type (pp, &cr, desc, &pf, &nr, &ns);

	  /* Could change by 1, since earlier call, if we guess at modulo 128. */
	  info_len = ax25_get_info (pp, &pinfo);

	  dw_printf ("(%s)", desc);
	  if (ftype == frame_type_U_XID) {
	    struct xid_param_s param;
	    char info2text[150];

	    xid_parse (pinfo, info_len, &param, info2text, sizeof(info2text));
	    dw_printf (" %s\n", info2text);
	  }
	  else {
	    ax25_safe_print ((char *)pinfo, info_len, ( ! ax25_is_aprs(pp)) && ( ! d_u_opt) );
	    dw_printf ("\n");
	  }
	}
	else {

	  // for APRS we generally want to display non-ASCII to see UTF-8.
	  // for other, probably want to restrict to ASCII only because we are
	  // more likely to have compressed data than UTF-8 text.

	  // TODO: Might want to use d_u_opt for transmitted frames too.

	  ax25_safe_print ((char *)pinfo, info_len, ( ! ax25_is_aprs(pp)) && ( ! d_u_opt) );
	  dw_printf ("\n");
	}


// Also display in pure ASCII if non-ASCII characters and "-d u" option specified.

	if (d_u_opt) {

	  unsigned char *p;
	  int n = 0;

	  for (p = pinfo; *p != '\0'; p++) {
	    if (*p >= 0x80) n++;
	  }

	  if (n > 0) {
	    text_color_set(DW_COLOR_DEBUG);
	    ax25_safe_print ((char *)pinfo, info_len, 1);
	    dw_printf ("\n");
	  }
	}

/* Optional hex dump of packet. */

	if (d_p_opt) {

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("------\n");
	  ax25_hex_dump (pp);
	  dw_printf ("------\n");
	}


/*
 * Decode the contents of UI frames and display in human-readable form.
 * Could be APRS or anything random for old fashioned packet beacons.
 *
 * Suppress printed decoding if "-q d" option used.
 */
	char ais_obj_packet[300];
	strcpy (ais_obj_packet, "");

	if (ax25_is_aprs(pp)) {

	  decode_aprs_t A;

	  // we still want to decode it for logging and other processing.
	  // Just be quiet about errors if "-qd" is set.

	  decode_aprs (&A, pp, q_d_opt, NULL);

	  if ( ! q_d_opt ) {

	    // Print it all out in human readable format unless "-q d" option used.

	    decode_aprs_print (&A);
	  }

	  /*
	   * Perform validity check on each address.
	   * This should print an error message if any issues.
	   */
	  (void)ax25_check_addresses(pp);

	  // Send to log file.

	  log_write (chan, &A, pp, alevel, retries);

	  // temp experiment.
	  //log_rr_bits (&A, pp);

	  // Add to list of stations heard over the radio.

	  mheard_save_rf (chan, &A, pp, alevel, retries);

// For AIS, we have an option to convert the NMEA format, in User Defined data,
// into an APRS "Object Report" and send that to the clients as well.

// FIXME: partial implementation.

	  static const char user_def_da[4] = { '{', USER_DEF_USER_ID, USER_DEF_TYPE_AIS, '\0' };

	  if (strncmp((char*)pinfo, user_def_da, 3) == 0) {

	    waypoint_send_ais((char*)pinfo + 3);

	    if (A_opt_ais_to_obj && A.g_lat != G_UNKNOWN && A.g_lon != G_UNKNOWN) {

	      char ais_obj_info[256];
	      (void)encode_object (A.g_name, 0, time(NULL),
	        A.g_lat, A.g_lon, 0,	// no ambiguity
		A.g_symbol_table, A.g_symbol_code,
		0, 0, 0, "",	// power, height, gain, direction.
	        // Unknown not handled properly.
		// Should encode_object take floating point here?
		(int)(A.g_course+0.5), (int)(DW_MPH_TO_KNOTS(A.g_speed_mph)+0.5),
		0, 0, 0, A.g_comment,	// freq, tone, offset
		ais_obj_info, sizeof(ais_obj_info));

	      snprintf (ais_obj_packet, sizeof(ais_obj_packet), "%s>%s%1d%1d:%s", A.g_src, APP_TOCALL, MAJOR_VERSION, MINOR_VERSION, ais_obj_info);

	      dw_printf ("[%d.AIS] %s\n", chan, ais_obj_packet);

	      // This will be sent to client apps after the User Defined Data representation.
	    }
	  }

	  // Convert to NMEA waypoint sentence if we have a location.

 	  if (A.g_lat != G_UNKNOWN && A.g_lon != G_UNKNOWN) {
	    waypoint_send_sentence (strlen(A.g_name) > 0 ? A.g_name : A.g_src, 
		A.g_lat, A.g_lon, A.g_symbol_table, A.g_symbol_code, 
		DW_FEET_TO_METERS(A.g_altitude_ft), A.g_course, DW_MPH_TO_KNOTS(A.g_speed_mph), 
		A.g_comment);
	  }
	}


/* Send to another application if connected. */
// TODO:  Put a wrapper around this so we only call one function to send by all methods.
// We see the same sequence in tt_user.c.

	int flen;
	unsigned char fbuf[AX25_MAX_PACKET_LEN];

	flen = ax25_pack(pp, fbuf);

	server_send_rec_packet (chan, pp, fbuf, flen);					// AGW net protocol
	kissnet_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);	// KISS TCP
	kissserial_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);	// KISS serial port
	kisspt_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, NULL, -1);	// KISS pseudo terminal

	if (A_opt_ais_to_obj && strlen(ais_obj_packet) != 0) {
	  packet_t ao_pp = ax25_from_text (ais_obj_packet, 1);
	  if (ao_pp != NULL) {
	    unsigned char ao_fbuf[AX25_MAX_PACKET_LEN];
	    int ao_flen = ax25_pack(ao_pp, ao_fbuf);

	    server_send_rec_packet (chan, ao_pp, ao_fbuf, ao_flen);
	    kissnet_send_rec_packet (chan, KISS_CMD_DATA_FRAME, ao_fbuf, ao_flen, NULL, -1);
	    kissserial_send_rec_packet (chan, KISS_CMD_DATA_FRAME, ao_fbuf, ao_flen, NULL, -1);
	    kisspt_send_rec_packet (chan, KISS_CMD_DATA_FRAME, ao_fbuf, ao_flen, NULL, -1);
	    ax25_delete (ao_pp);
	  }
	}

/*
 * If it is from the ICHANNEL, we are done.
 * Don't digipeat.  Don't IGate.
 * Don't do anything with it after printing and sending to client apps.
 */

	if (chan == audio_config.igate_vchannel) {
	    return;
	}

/* 
 * If it came from DTMF decoder (subchan == -1), send it to APRStt gateway.
 * Otherwise, it is a candidate for IGate and digipeater.
 *
 * It is also useful to have some way to simulate touch tone
 * sequences with BEACON sendto=R0 for testing.
 */

	if (subchan == -1) {		// from DTMF decoder
	  if (tt_config.gateway_enabled && info_len >= 2) {
	    aprs_tt_sequence (chan, (char*)(pinfo+1));
	  }
	}
	else if (*pinfo == 't' && info_len >= 2 && tt_config.gateway_enabled) {
				// For testing.
				// Would be nice to verify it was generated locally,
				// not received over the air.
	  aprs_tt_sequence (chan, (char*)(pinfo+1));
	}
	else { 
	
/*
 * Send to the IGate processing.
 * Use only those with correct CRC; We don't want to spread corrupted data!
 * Our earlier "fix bits" hack could allow corrupted information to get thru.
 * However, if it used FEC mode (FX.25. IL2P), we have much higher level of
 * confidence that it is correct.
 */
	  if (ax25_is_aprs(pp) && ( retries == RETRY_NONE || fec_type == fec_type_fx25 || fec_type == fec_type_il2p) ) {

	    igate_send_rec_packet (chan, pp);
	  }


/* Send out a regenerated copy. Applies to all types, not just APRS. */
/* This was an experimental feature never documented in the User Guide. */
/* Initial feedback was positive but it fell by the wayside. */
/* Should follow up with testers and either document this or clean out the clutter. */

	  digi_regen (chan, pp);


/*
 * Send to APRS digipeater.
 * Use only those with correct CRC; We don't want to spread corrupted data!
 * Our earlier "fix bits" hack could allow corrupted information to get thru.
 * However, if it used FEC mode (FX.25. IL2P), we have much higher level of
 * confidence that it is correct.
 */
	  if (ax25_is_aprs(pp) && ( retries == RETRY_NONE || fec_type == fec_type_fx25 || fec_type == fec_type_il2p) ) {

	    digipeater (chan, pp);
	  }

/*
 * Connected mode digipeater.
 * Use only those with correct CRC (or using FEC.)
 */

	  if (retries == RETRY_NONE || fec_type == fec_type_fx25 || fec_type == fec_type_il2p) {

	    cdigipeater (chan, pp);
	  }
	}

} /* end app_process_rec_packet */



/* Process control C and window close events. */

#if __WIN32__

static BOOL cleanup_win (int ctrltype)
{
	if (ctrltype == CTRL_C_EVENT || ctrltype == CTRL_CLOSE_EVENT) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("\nQRT\n");
	  log_term ();
	  ptt_term ();
	  waypoint_term ();
	  dwgps_term ();
	  SLEEP_SEC(1);
	  ExitProcess (0);
	}
	return (TRUE);
}


#else

static void cleanup_linux (int x)
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("\nQRT\n");
	log_term ();
	ptt_term ();
	dwgps_term ();
	SLEEP_SEC(1);
	exit(0);
}

#endif



static void usage (char **argv)
{
	text_color_set(DW_COLOR_ERROR);

	dw_printf ("\n");
	dw_printf ("Dire Wolf version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);
	dw_printf ("\n");
	dw_printf ("Usage: direwolf [options] [ - | stdin | UDP:nnnn ]\n");
	dw_printf ("Options:\n");
	dw_printf ("    -c fname       Configuration file name.\n");
	dw_printf ("    -l logdir      Directory name for log files.  Use . for current.\n");
	dw_printf ("    -r n           Audio sample rate, per sec.\n");
	dw_printf ("    -n n           Number of audio channels, 1 or 2.\n");
	dw_printf ("    -b n           Bits per audio sample, 8 or 16.\n");
	dw_printf ("    -B n           Data rate in bits/sec for channel 0.  Standard values are 300, 1200, 2400, 4800, 9600.\n");
	dw_printf ("                     300 bps defaults to AFSK tones of 1600 & 1800.\n");
	dw_printf ("                     1200 bps uses AFSK tones of 1200 & 2200.\n");
	dw_printf ("                     2400 bps uses QPSK based on V.26 standard.\n");
	dw_printf ("                     4800 bps uses 8PSK based on V.27 standard.\n");
	dw_printf ("                     9600 bps and up uses K9NG/G3RUH standard.\n");
	dw_printf ("                     AIS for ship Automatic Identification System.\n");
	dw_printf ("                     EAS for Emergency Alert System (EAS) Specific Area Message Encoding (SAME).\n");
	dw_printf ("    -g             Force G3RUH modem regardless of speed.\n");
	dw_printf ("    -j             2400 bps QPSK compatible with direwolf <= 1.5.\n");
	dw_printf ("    -J             2400 bps QPSK compatible with MFJ-2400.\n");
	dw_printf ("    -P xxx         Modem Profiles.\n");
	dw_printf ("    -A             Convert AIS positions to APRS Object Reports.\n");
	dw_printf ("    -D n           Divide audio sample rate by n for channel 0.\n");
	dw_printf ("    -X n           1 to enable FX.25 transmit.  16, 32, 64 for specific number of check bytes.\n");
	dw_printf ("    -I n           Enable IL2P transmit.  n=1 is recommended.  0 uses weaker FEC.\n");
	dw_printf ("    -i n           Enable IL2P transmit, inverted polarity.  n=1 is recommended.  0 uses weaker FEC.\n");
	dw_printf ("    -d             Debug options:\n");
	dw_printf ("       a             a = AGWPE network protocol client.\n");
	dw_printf ("       k             k = KISS serial port or pseudo terminal client.\n");
	dw_printf ("       n             n = KISS network client.\n");
	dw_printf ("       u             u = Display non-ASCII text in hexadecimal.\n");
	dw_printf ("       p             p = dump Packets in hexadecimal.\n");
	dw_printf ("       g             g = GPS interface.\n");
	dw_printf ("       w             w = Waypoints for Position or Object Reports.\n");
	dw_printf ("       t             t = Tracker beacon.\n");
	dw_printf ("       o             o = output controls such as PTT and DCD.\n");
	dw_printf ("       i             i = IGate.\n");
	dw_printf ("       m             m = Monitor heard station list.\n");
	dw_printf ("       f             f = packet Filtering.\n");
#if USE_HAMLIB
	dw_printf ("       h             h = hamlib increase verbose level.\n");
#endif
	dw_printf ("       x             x = FX.25 increase verbose level.\n");
	dw_printf ("       2             2 = IL2P.\n");
	dw_printf ("       d             d = APRStt (DTMF to APRS object translation).\n");
	dw_printf ("    -q             Quiet (suppress output) options:\n");
	dw_printf ("       h             h = Heard line with the audio level.\n");
	dw_printf ("       d             d = Decoding of APRS packets.\n");
	dw_printf ("       x             x = Silence FX.25 information.\n");
	dw_printf ("    -t n           Text colors.  0=disabled. 1=default.  2,3,4,... alternatives.\n");
	dw_printf ("                     Use 9 to test compatibility with your terminal.\n");
	dw_printf ("    -a n           Audio statistics interval in seconds.  0 to disable.\n");
#if __WIN32__
#else
	dw_printf ("    -p             Enable pseudo terminal for KISS protocol.\n");
#endif
	dw_printf ("    -x             Send Xmit level calibration tones.\n");
	dw_printf ("       a             a = Alternating mark/space tones.\n");
	dw_printf ("       m             m = Steady mark tone (e.g. 1200Hz).\n");
	dw_printf ("       s             s = Steady space tone (e.g. 2200Hz).\n");
	dw_printf ("       p             p = Silence (Set PTT only).\n");
	dw_printf ("        chan          Optionally add a number to specify radio channel.\n");
	dw_printf ("    -u             Print UTF-8 test string and exit.\n");
	dw_printf ("    -S             Print symbol tables and exit.\n");
	dw_printf ("    -T fmt         Time stamp format for sent and received frames.\n");
	dw_printf ("    -e ber         Receive Bit Error Rate (BER), e.g. 1e-5\n");
	dw_printf ("\n");

	dw_printf ("After any options, there can be a single command line argument for the source of\n");
	dw_printf ("received audio.  This can override the audio input specified in the configuration file.\n");
	dw_printf ("\n");
  
#if __WIN32__
	dw_printf ("Documentation can be found in the 'doc' folder\n");
#else
	// TODO: Could vary by platform and build options.
	dw_printf ("Documentation can be found in /usr/local/share/doc/direwolf\n");
#endif
	dw_printf ("or online at https://github.com/wb2osz/direwolf/tree/master/doc\n");
	dw_printf ("additional topics: https://github.com/wb2osz/direwolf-doc\n");
	text_color_set(DW_COLOR_INFO);
	exit (EXIT_FAILURE);
}


/* end direwolf.c */

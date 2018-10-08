//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015, 2016, 2017  John Langner, WB2OSZ
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
 *			AFSK modem using the "sound card."
 *			AX.25 encoder/decoder.
 *			APRS data encoder / decoder.
 *			APRS digipeater.
 *			KISS TNC emulator.
 *			APRStt (touch tone input) gateway
 *			Internet Gateway (IGate)
 *		
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
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#ifdef __OpenBSD__
#include <soundcard.h>
#elif __APPLE__
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


//static int idx_decoded = 0;

#if __WIN32__
static BOOL cleanup_win (int);
#else
static void cleanup_linux (int);
#endif

static void usage (char **argv);

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



int main (int argc, char *argv[])
{
	int err;
	//int eof;
	int j;
	char config_file[100];
	int xmit_calibrate_option = 0;
	int enable_pseudo_terminal = 0;
	struct digi_config_s digi_config;
	struct cdigi_config_s cdigi_config;
	struct igate_config_s igate_config;
	int r_opt = 0, n_opt = 0, b_opt = 0, B_opt = 0, D_opt = 0;	/* Command line options. */
	char P_opt[16];
	char l_opt_logdir[80];
	char L_opt_logfile[80];
	char input_file[80];
	char T_opt_timestamp[40];
	
	int t_opt = 1;		/* Text color option. */				
	int a_opt = 0;		/* "-a n" interval, in seconds, for audio statistics report.  0 for none. */

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
	int E_tx_opt = 0;		/* "-E n" Error rate % for clobbering trasmit frames. */
	int E_rx_opt = 0;		/* "-E Rn" Error rate % for clobbering receive frames. */

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
 */

	t_opt = 1;		/* 1 = normal, 0 = no text colors. */
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
	//dw_printf ("Dire Wolf version %d.%d (%s) Beta Test 4\n", MAJOR_VERSION, MINOR_VERSION, __DATE__);
	//dw_printf ("Dire Wolf DEVELOPMENT version %d.%d %s (%s)\n", MAJOR_VERSION, MINOR_VERSION, "C", __DATE__);
	dw_printf ("Dire Wolf version %d.%d\n", MAJOR_VERSION, MINOR_VERSION);


#if defined(ENABLE_GPSD) || defined(USE_HAMLIB) || defined(USE_CM108)
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
	dw_printf ("\n");
#endif


#if __WIN32__
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
 * Now, where can I find a Pentium 2 or earlier to test this?
 */

#if defined(__SSE__) && !defined(__APPLE__)
	int cpuinfo[4];
	__cpuid (cpuinfo, 0);
	if (cpuinfo[0] >= 1) {
	  __cpuid (cpuinfo, 1);
	  //dw_printf ("debug: cpuinfo = %x, %x, %x, %x\n", cpuinfo[0], cpuinfo[1], cpuinfo[2], cpuinfo[3]);
	  if ( ! ( cpuinfo[3] & (1 << 25))) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("------------------------------------------------------------------\n");
	    dw_printf ("This version requires a minimum of a Pentium 3 or equivalent.\n");
	    dw_printf ("If you are seeing this message, you are probably using a computer\n");
	    dw_printf ("from the previous century.  See comments in Makefile.win for\n");
	    dw_printf ("information on how you can recompile it for use with your antique.\n");
	    dw_printf ("------------------------------------------------------------------\n");
	  }
	}
	text_color_set(DW_COLOR_INFO);
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

          c = getopt_long(argc, argv, "P:B:D:c:pxr:b:n:d:q:t:Ul:L:Sa:E:T:",
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
	 
	    B_opt = atoi(optarg);
            if (B_opt < MIN_BAUD || B_opt > MAX_BAUD) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Use a more reasonable data baud rate in range of %d - %d.\n", MIN_BAUD, MAX_BAUD);
              exit (EXIT_FAILURE);
            }
            break;

	  case 'P':				/* -P for modem profile. */

	    //debug: dw_printf ("Demodulator profile set to \"%s\"\n", optarg);
	    strlcpy (P_opt, optarg, sizeof(P_opt)); 
	    break;	

          case 'D':				/* -D decrease AFSK demodulator sample rate */
	 
	    D_opt = atoi(optarg);
            if (D_opt < 1 || D_opt > 8) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Crazy value for -D. \n");
              exit (EXIT_FAILURE);
            }
            break;

          case 'x':				/* -x for transmit calibration tones. */

	    xmit_calibrate_option = 1;
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

          case '?':

            /* Unknown option message was already printed. */
            usage (argv);
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
	      default: break;
	     }
	    }
	    break;
	      
	  case 't':				/* Was handled earlier. */
	    break;


	  case 'U':				/* Print UTF-8 test and exit. */

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

          default:

            /* Should not be here. */
	    text_color_set(DW_COLOR_DEBUG);
            dw_printf("?? getopt returned character code 0%o ??\n", c);
            usage (argv);
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

	config_init (config_file, &audio_config, &digi_config, &cdigi_config, &tt_config, &igate_config, &misc_config);

	if (r_opt != 0) {
	  audio_config.adev[0].samples_per_sec = r_opt;
	}
	if (n_opt != 0) {
	  audio_config.adev[0].num_channels = n_opt;
	  if (n_opt == 2) {
	    audio_config.achan[1].valid = 1;
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
	  else {
            audio_config.achan[0].modem_type = MODEM_SCRAMBLE;
            audio_config.achan[0].mark_freq = 0;
            audio_config.achan[0].space_freq = 0;
	  }
	}

	audio_config.statistics_interval = a_opt;

	if (strlen(P_opt) > 0) { 
	  /* -P for modem profile. */
	  /* TODO: Not yet documented.  Should probably since it is consistent with atest. */
	  strlcpy (audio_config.achan[0].profiles, P_opt, sizeof(audio_config.achan[0].profiles)); 
	}	

	if (D_opt != 0) {
	    // Reduce audio sampling rate to reduce CPU requirements.
	    audio_config.achan[0].decimate = D_opt;
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
	  exit (1);
	}

/*
 * Initialize the demodulator(s) and HDLC decoder.
 */
	multi_modem_init (&audio_config);

/*
 * Initialize the touch tone decoder & APRStt gateway.
 */
	dtmf_init (&audio_config, audio_amplitude);
	aprs_tt_init (&tt_config);
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
 * If -x option specified, transmit alternating tones for transmitter
 * audio level adjustment, up to 1 minute then quit.
 * TODO:  enhance for more than one channel.
 */

	if (xmit_calibrate_option) {

	  int max_duration = 60;  /* seconds */
	  int n = audio_config.achan[0].baud * max_duration;
	  int chan = 0;
	
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("\nSending transmit calibration tones.  Press control-C to terminate.\n");

	  ptt_set (OCTYPE_PTT, chan, 1);
	  while (n-- > 0) {

	    tone_gen_put_bit (chan, n & 1);

	  }
	  ptt_set (OCTYPE_PTT, chan, 0);
	  exit (0);
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

void app_process_rec_packet (int chan, int subchan, int slice, packet_t pp, alevel_t alevel, retry_t retries, char *spectrum)
{	
	
	char stemp[500];
	unsigned char *pinfo;
	int info_len;
	char heard[AX25_MAX_ADDR_LEN];
	//int j;
	int h;
	char display_retries[32];

	assert (chan >= 0 && chan < MAX_CHANS);
	assert (subchan >= -1 && subchan < MAX_SUBCHANS);
	assert (slice >= 0 && slice < MAX_SLICERS);
	assert (pp != NULL);	// 1.1J+
     
	strlcpy (display_retries, "", sizeof(display_retries));
	if (audio_config.achan[chan].fix_bits != RETRY_NONE || audio_config.achan[chan].passall) {
	  snprintf (display_retries, sizeof(display_retries), " [%s] ", retry_text[(int)retries]);
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

	if (( ! q_h_opt ) && alevel.rec >= 0) {    /* suppress if "-q h" option */

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

	/* Version 1.2:   Cranking the input level way up produces 199. */
	/* Keeping it under 100 gives us plenty of headroom to avoid saturation. */

	// TODO:  suppress this message if not using soundcard input.
	// i.e. we have no control over the situation when using SDR.

	if (alevel.rec > 110) {

	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Audio input level is too high.  Reduce so most stations are around 50.\n");
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

	if (ax25_is_aprs(pp)) {

	  decode_aprs_t A;

	  // we still want to decode it for logging and other processing.
	  // Just be quiet about errors if "-qd" is set.

	  decode_aprs (&A, pp, q_d_opt);

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

	server_send_rec_packet (chan, pp, fbuf, flen);				// AGW net protocol
	kissnet_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, -1);	// KISS TCP
	kissserial_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, -1);	// KISS serial port
	kisspt_send_rec_packet (chan, KISS_CMD_DATA_FRAME, fbuf, flen, -1);	// KISS pseudo terminal

/* 
 * If it came from DTMF decoder, send it to APRStt gateway.
 * Otherwise, it is a candidate for IGate and digipeater.
 *
 * TODO: It might be useful to have some way to simulate touch tone
 * sequences with BEACON sendto=R... for testing.
 */
	if (subchan == -1) {
	  if (tt_config.gateway_enabled && info_len >= 2) {
	    aprs_tt_sequence (chan, (char*)(pinfo+1));
	  }
	}
	else { 
	
/* Send to Internet server if option is enabled. */
/* Consider only those with correct CRC. */

	  if (ax25_is_aprs(pp) && retries == RETRY_NONE) {

	    igate_send_rec_packet (chan, pp);
	  }


/* Send out a regenerated copy. Applies to all types, not just APRS. */
/* This was an experimental feature never documented in the User Guide. */
/* Initial feedback was positive but it fell by the wayside. */
/* Should follow up with testers and either document this or clean out the clutter. */

	  digi_regen (chan, pp);


/*
 * APRS digipeater.
 * Use only those with correct CRC; We don't want to spread corrupted data!
 */

	  if (ax25_is_aprs(pp) && retries == RETRY_NONE) {

	    digipeater (chan, pp);
	  }

/*
 * Connected mode digipeater.
 * Use only those with correct CRC.
 */

	  if (retries == RETRY_NONE) {

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
	dw_printf ("    -D n           Divide audio sample rate by n for channel 0.\n");
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
	dw_printf ("    -q             Quiet (suppress output) options:\n");
	dw_printf ("       h             h = Heard line with the audio level.\n");
	dw_printf ("       d             d = Decoding of APRS packets.\n");
	dw_printf ("    -t n           Text colors.  1=normal, 0=disabled.\n");
	dw_printf ("    -a n           Audio statistics interval in seconds.  0 to disable.\n");
#if __WIN32__
#else
	dw_printf ("    -p             Enable pseudo terminal for KISS protocol.\n");
#endif
	dw_printf ("    -x             Send Xmit level calibration tones.\n");
	dw_printf ("    -U             Print UTF-8 test string and exit.\n");
	dw_printf ("    -S             Print symbol tables and exit.\n");
	dw_printf ("    -T fmt         Time stamp format for sent and received frames.\n");
	dw_printf ("\n");

	dw_printf ("After any options, there can be a single command line argument for the source of\n");
	dw_printf ("received audio.  This can overrides the audio input specified in the configuration file.\n");
	dw_printf ("\n");
  
#if __WIN32__
#else
	dw_printf ("Complete documentation can be found in /usr/local/share/doc/direwolf.\n");
#endif
	exit (EXIT_FAILURE);
}



/* end direwolf.c */

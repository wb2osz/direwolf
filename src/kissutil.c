//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2017  John Langner, WB2OSZ
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
 * Module:      kissutil.c
 *
 * Purpose:   	Utility for talking to a KISS TNC.
 *		
 * Description:	Convert between KISS format and usual text representation.
 *		This might also serve as the starting point for an application
 *		that uses a KISS TNC.
 *		The TNC can be attached by TCP or a serial port.
 *
 * Usage:	kissutil  [ options ]
 *
 *		Default is to connect to localhost:8001.
 *		See the "usage" functions at the bottom for details.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__

#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this

#else 

#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>

#include "ax25_pad.h"
#include "textcolor.h"
#include "serial_port.h"
#include "kiss_frame.h"
#include "dwsock.h"
#include "audio.h"		// for DEFAULT_TXDELAY, etc.
#include "dtime_now.h"


// TODO:  define in one place, use everywhere.
#if __WIN32__
#define THREAD_F unsigned __stdcall
#else
#define THREAD_F void *
#endif

#if __WIN32__
#define DIR_CHAR "\\"
#else
#define DIR_CHAR "/"
#endif

static THREAD_F tnc_listen_net (void *arg);
static THREAD_F tnc_listen_serial (void *arg);

static void send_to_kiss_tnc (int chan, int cmd, char *data, int dlen);
static void hex_dump (unsigned char *p, int len);

static void usage(void);
static void usage2(void);




/* Obtained from the command line. */

static char hostname[50] = "localhost";		/* -h option. */
						/* DNS host name or IPv4 address. */
						/* Some of the code is there for IPv6 but */
						/* it needs more work. */
						/* Defaults to "localhost" if not specified. */

static char port[30] = "8001";			/* -p option. */
						/* If it begins with a digit, it is considered */
						/* a TCP port number at the hostname.  */
						/* Otherwise, we treat it as a serial port name. */

static int using_tcp = 1;			/* Are we using TCP or serial port for TNC? */
						/* Use corresponding one of the next two. */
						/* This is derived from the first character of port. */

static int server_sock = -1;			/* File descriptor for socket interface. */
						/* Set to -1 if not used. */
						/* (Don't use SOCKET type because it is unsigned.) */

static MYFDTYPE serial_fd = (MYFDTYPE)(-1);	/* Serial port handle. */

static int serial_speed = 9600;			/* -s option. */
						/* Serial port speed, bps. */

static int verbose = 0;				/* -v option. */
						/* Display the KISS protocol in hexadecimal for troubleshooting. */

static char transmit_from[120] = "";		/* -f option */
						/* When specified, files are read from this directory */
						/* rather than using stdin.  Each file is one or more */
						/* lines in the standard monitoring format. */

static char receive_output[120] = "";		/* -o option */
						/* When specified, each received frame is stored as a file */
						/* with a unique name here.  */
						/* Directory must already exist; we won't create it. */

static char timestamp_format[60] = "";		/* -T option */
						/* Precede received frames with timestamp. */
						/* Command line option uses "strftime" format string. */


#if __WIN32__
#define THREAD_F unsigned __stdcall
#else
#define THREAD_F void *
#endif

#if __WIN32__
	static HANDLE tnc_th;
#else
	static pthread_t tnc_tid;
#endif

static void process_input (char *stuff);

/* Trim any CR, LF from the end of line. */

static void trim (char *stuff)
{
	char *p;
	p = stuff + strlen(stuff) - 1;
	while (strlen(stuff) > 0 && (*p == '\r' || *p == '\n')) {
	  *p = '\0';
	  p--;
	}
} /* end trim */


/*------------------------------------------------------------------
 *
 * Name: 	main
 *
 * Purpose:   	Attach to KISS TNC and exchange information.
 *
 * Usage:	See "usage" functions at end.
 *
 *---------------------------------------------------------------*/

int main (int argc, char *argv[])
{
	text_color_init (0);	// Turn off text color.
				// It could interfere with trying to pipe stdout to some other application.

#if __WIN32__

#else
	int e;
	setlinebuf (stdout);		// TODO:  What is the Windows equivalent?
#endif

 
/*
 * Extract command line args.
 */
	while (1) {
          int option_index = 0;
	  int c;
          static struct option long_options[] = {
            //{"future1", 1, 0, 0},
            //{"future2", 0, 0, 0},
            //{"future3", 1, 0, 'c'},
            {0, 0, 0, 0}
          };

	  /* ':' following option character means arg is required. */

          c = getopt_long(argc, argv, "h:p:s:vf:o:T:",
			long_options, &option_index);
          if (c == -1)
            break;

          switch (c) {

            case 'h':				/* -h for hostname. */
	      strlcpy (hostname, optarg, sizeof(hostname));
              break;

            case 'p':				/* -p for port, either TCP or serial device. */  
	      strlcpy (port, optarg, sizeof(port));
              break;

            case 's':				/* -s for serial port speed. */
	      serial_speed = atoi(optarg);
              break;

	    case 'v':				/* -v for verbose. */
	      verbose++;
	      break;

            case 'f':				/* -f for transmit files directory. */  
	      strlcpy (transmit_from, optarg, sizeof(transmit_from));
              break;

            case 'o':				/* -o for receive output directory. */  
	      strlcpy (receive_output, optarg, sizeof(receive_output));
              break;

            case 'T':				/* -T for receive timestamp. */  
	      strlcpy (timestamp_format, optarg, sizeof(timestamp_format));
              break;

            case '?':
              /* Unknown option message was already printed. */
              usage ();
              break;

            default:
              /* Should not be here. */
	      text_color_set(DW_COLOR_DEBUG);
              dw_printf("?? getopt returned character code 0%o ??\n", c);
              usage ();
           }
	}  /* end while(1) for options */

	if (optind < argc) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf ("Warning: Unused command line arguments are ignored.\n");
 	}

/*
 * If receive queue directory was specified, make sure that it exists.
 */
	if (strlen(receive_output) > 0) {
	  struct stat s;

	  if (stat(receive_output, &s) == 0) {
	    if ( ! S_ISDIR(s.st_mode)) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf ("Receive queue location, %s, is not a directory.\n", receive_output);
	      exit (EXIT_FAILURE);
	    }
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
            dw_printf ("Receive queue location, %s, does not exist.\n", receive_output);
	    exit (EXIT_FAILURE);
	  }
	}

/* If port begins with digit, consider it to be TCP. */
/* Otherwise, treat as serial port name. */

	using_tcp = isdigit(port[0]);

#if __WIN32__
	if (using_tcp) {
	  tnc_th = (HANDLE)_beginthreadex (NULL, 0, tnc_listen_net, (void *)(ptrdiff_t)99, 0, NULL);
	}
	else {
	  tnc_th = (HANDLE)_beginthreadex (NULL, 0, tnc_listen_serial, (void *)99, 0, NULL);
	}
	if (tnc_th == NULL) {
	  printf ("Internal error: Could not create TNC listen thread.\n");
	  exit (EXIT_FAILURE);
	}
#else
	if (using_tcp) {
	  e = pthread_create (&tnc_tid, NULL, tnc_listen_net, (void *)(ptrdiff_t)99);
	}
	else {
	  e = pthread_create (&tnc_tid, NULL, tnc_listen_serial, (void *)(ptrdiff_t)99);
	}
	if (e != 0) {
	  perror("Internal error: Could not create TNC listen thread.");
	  exit (EXIT_FAILURE);
	}
#endif

// Give the threads a little while to open the TNC connection before trying to use it.
// This was a problem when the transmit queue already existed when starting up.

	SLEEP_MS (500);

/*
 * Process keyboard or other input source.
 */
	char stuff[AX25_MAX_PACKET_LEN];

	if (strlen(transmit_from) > 0) {
/*
 * Process and delete all files in specified directory.
 * When done, sleep for a second and try again.
 * This doesn't take them in any particular order.
 * A future enhancement might sort by name or timestamp.
 */
	  while (1) {
	    DIR *dp;
	    struct dirent *ep;

	    //text_color_set(DW_COLOR_DEBUG);
            //dw_printf("Get directory listing...\n");

	    dp = opendir (transmit_from);
	    if (dp != NULL) {
	      while ((ep = readdir(dp)) != NULL) {
	        char path [300];
	        FILE *fp;
	        if (ep->d_name[0] == '.')
	          continue;

	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("Processing %s for transmit...\n", ep->d_name);
		strlcpy (path, transmit_from, sizeof(path));
	        strlcat (path, DIR_CHAR, sizeof(path));
	        strlcat (path, ep->d_name, sizeof(path));
	        fp = fopen (path, "r");
		if (fp != NULL) {
	          while (fgets(stuff, sizeof(stuff), fp) != NULL) {         
	            trim (stuff);
	            text_color_set(DW_COLOR_DEBUG);
	            dw_printf ("%s\n", stuff);
		    // TODO: Don't delete file if errors encountered?
	            process_input (stuff);
	          }
	          fclose (fp);
		  unlink (path);
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
                  dw_printf("Can't open for read: %s\n", path);
	        }
	      }
	      closedir (dp);
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Can't access transmit queue directory %s.  Quitting.\n", transmit_from);
	      exit (EXIT_FAILURE);
	    }
	    SLEEP_SEC (1);
	  }
	}
	else {
/*
 * Using stdin.
 */
	  while (fgets(stuff, sizeof(stuff), stdin) != NULL) {
	    process_input (stuff);
	  }
	}

	return (EXIT_SUCCESS);

} /* end main */



/*-------------------------------------------------------------------
 *
 * Name:        process_input
 *
 * Purpose:     Process frames/commands from user, either interactively or from files.
 *
 * Inputs:	stuff		- A frame is in usual format like SOURCE>DEST,DIGI:whatever.
 *				  Commands begin with lower case letter.
 *				  Note that it can be modified by this function.
 *
 * Later Enhancement:	Return success/fail status.  The transmit queue processing might want
 *		to preserve files that were not processed as expected.
 *
 *--------------------------------------------------------------------*/

static int parse_number (char *str, int de_fault)
{
	int n;

	while (isspace(*str)) {
	  str++;
	}
	if (strlen(str) == 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Missing number for KISS command.  Using default %d.\n", de_fault);
	  return (de_fault);
	}
	n = atoi(str);
	if (n < 0 || n > 255) {		// must fit in a byte.
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Number for KISS command is out of range 0-255.  Using default %d.\n", de_fault);
	  return (de_fault);
	}
	return (n);
}

static void process_input (char *stuff)
{
	char *p;
	int chan = 0;

/*
 * Remove any end of line character(s).
 */
	trim (stuff);

/*
 * Optional prefix, like "[9]" or "[99]" to specify channel.
 */
	p = stuff;
	while (isspace(*p)) p++;
	if (*p == '[') {
	  p++;
	  if (p[1] == ']') {
	    chan = atoi(p);
	    p += 2;
	  }
	  else if (p[2] == ']') {
	    chan = atoi(p);
	    p += 3;
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR! One or two digit channel number and ] was expected after [ at beginning of line.\n");
	    usage2();
	    return;
	  }
	  if (chan < 0 || chan > 15) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR! KISS channel number must be in range of 0 thru 15.\n");
	    usage2();
	    return;
	  }
	  while (isspace(*p)) p++;
	}

/*
 * If it starts with upper case letter or digit, assume it is an AX.25 frame in monitor format.
 * Lower case is a command (e.g.  Persistence or set Hardware).
 * Anything else, print explanation of what is expected.
 */
	if (isupper(*p) || isdigit(*p)) {

	  // Parse the "TNC2 monitor format" and convert to AX.25 frame.

	  unsigned char frame_data[AX25_MAX_PACKET_LEN];
	  packet_t pp = ax25_from_text (p, 1);
	  if (pp != NULL) {
	    int frame_len = ax25_pack (pp, frame_data);
	    send_to_kiss_tnc (chan, KISS_CMD_DATA_FRAME, (char*)frame_data, frame_len);
	    ax25_delete (pp);
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR! Could not convert to AX.25 frame: %s\n", p);
	  }
	}
	else if (islower(*p)) {
	  char value;

	  switch (*p) {
	    case 'd':			// txDelay, 10ms units
	      value = parse_number(p+1, DEFAULT_TXDELAY);
	      send_to_kiss_tnc (chan, KISS_CMD_TXDELAY, &value, 1);
	      break;
	    case 'p':			// Persistence
	      value = parse_number(p+1, DEFAULT_PERSIST);
	      send_to_kiss_tnc (chan, KISS_CMD_PERSISTENCE, &value, 1);
	      break;
	    case 's':			// Slot time, 10ms units
	      value = parse_number(p+1,  DEFAULT_SLOTTIME);
	      send_to_kiss_tnc (chan, KISS_CMD_SLOTTIME, &value, 1);
	      break;
	    case 't':			// txTail, 10ms units
	      value = parse_number(p+1, DEFAULT_TXTAIL);
	      send_to_kiss_tnc (chan, KISS_CMD_TXTAIL, &value, 1);
	      break;
	    case 'f':			// Full duplex
	      value = parse_number(p+1, 0);
	      send_to_kiss_tnc (chan, KISS_CMD_FULLDUPLEX, &value, 1);
	      break;
	    case 'h':			// set Hardware
	      p++;
	      while (*p != '\0' && isspace(*p)) { p++; }
	      send_to_kiss_tnc (chan, KISS_CMD_SET_HARDWARE, p, strlen(p));
	      break;
	    default:
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid command. Must be one of d p s t f h.\n");
	      usage2 ();
	      break;
	  }
	}
	else {
	  usage2 ();
	}

} /* end process_input */


 

/*-------------------------------------------------------------------
 *
 * Name:        send_to_kiss_tnc
 *
 * Purpose:     Encapsulate the data/command, into a KISS frame, and send to the TNC.
 *
 * Inputs:	chan	- channel number.
 *
 *		cmd	- KISS_CMD_DATA_FRAME, KISS_CMD_SET_HARDWARE, etc.
 *
 *		data	- Information for KISS frame.
 *
 *		dlen	- Number of bytes in data.
 *
 * Description:	Encapsulate as KISS frame and send to TNC.
 *
 *--------------------------------------------------------------------*/

static void send_to_kiss_tnc (int chan, int cmd, char *data, int dlen)
{
	unsigned char temp[AX25_MAX_PACKET_LEN];	// We don't limit to 256 info bytes.
	unsigned char kissed[AX25_MAX_PACKET_LEN*2];
	int klen;

	if (chan < 0 || chan > 15) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Invalid channel %d - must be in range 0 to 15.\n", chan);
	  chan = 0;
	}
	if (cmd < 0 || cmd > 15) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Invalid command %d - must be in range 0 to 15.\n", cmd);
	  cmd = 0;
	}
	if (dlen < 0 || dlen > (int)(sizeof(temp)-1)) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("ERROR - Invalid data length %d - must be in range 0 to %d.\n", dlen, (int)(sizeof(temp)-1));
	  dlen = sizeof(temp)-1;
	}

	temp[0] = (chan << 4) | cmd;
	memcpy (temp+1, data, dlen);

	klen = kiss_encapsulate(temp, dlen+1, kissed);

	if (verbose) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("Sending to KISS TNC:\n");
	  hex_dump (kissed, klen);
	}

	if (using_tcp) {
	  int rc = SOCK_SEND(server_sock, (char*)kissed, klen);
	  if (rc != klen) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR writing KISS frame to socket.\n");
	  }
	}
	else {
	  int rc = serial_port_write (serial_fd, (char*)kissed, klen);
	  if (rc != klen) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("ERROR writing KISS frame to serial port.\n");
	    //dw_printf ("DEBUG wanted %d, got %d\n", klen, rc);
	  }
	}

} /* end send_to_kiss_tnc */


/*-------------------------------------------------------------------
 *
 * Name:        tnc_listen_net
 *
 * Purpose:     Connect to KISS TNC via TCP port.
 *		Print everything it sends to us.
 *
 * Inputs:	arg		- Currently not used.
 *
 * Global In:	host
 *		port
 *
 * Global Out:	server_sock	- Needed to send to the TNC.
 *
 *--------------------------------------------------------------------*/

static THREAD_F tnc_listen_net (void *arg)
{
	int err;
	char ipaddr_str[DWSOCK_IPADDR_LEN];  	// Text form of IP address.
	char data[4096];
	int allow_ipv6 = 0;		// Maybe someday.
	int debug = 0;
	int client = 0;			// Not used in this situation.
	kiss_frame_t kstate;

	memset (&kstate, 0, sizeof(kstate));

	err = dwsock_init ();
	if (err < 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Network interface failure.  Can't go on.\n");
	  exit (EXIT_FAILURE);
	}
	
/*
 * Connect to network KISS TNC.
 */
	// For the IGate we would loop around and try to reconnect if the TNC
	// goes away.  We should probably do the same here.

	server_sock = dwsock_connect (hostname, port, "TCP KISS TNC", allow_ipv6, debug, ipaddr_str);

	if (server_sock == -1) {
	  text_color_set(DW_COLOR_ERROR);
	  // Should have been a message already.  What else is there to say?
 	  exit (EXIT_FAILURE);
	}

/*
 * Print what we get from TNC.
 */
	int len;

	while ((len = SOCK_RECV (server_sock, (char*)(data), sizeof(data))) > 0) {
	  int j;
	  for (j = 0; j < len; j++) {
	  
	    // Feed in one byte at a time.
	    // kiss_process_msg is called when a complete frame has been accumulated.

	    // When verbose is specified, we get debug output like this:
	    //
	    // <<< Data frame from KISS client application, port 0, total length = 46
  	    // 000:  c0 00 82 a0 88 ae 62 6a e0 ae 84 64 9e a6 b4 ff  ......bj...d....
	    // ...
	    // It says "from KISS client application" because it was written
	    // on the assumption it was being used in only one direction.
	    // Not worried enough about it to do anything at this time.

	    kiss_rec_byte (&kstate, data[j], verbose, NULL, client, NULL);
	  }
	}

	text_color_set(DW_COLOR_ERROR);
	dw_printf ("Read error from TCP KISS TNC.  Terminating.\n");
 	exit (EXIT_FAILURE);

} /* end tnc_listen_net */


/*-------------------------------------------------------------------
 *
 * Name:        tnc_listen_serial
 *
 * Purpose:     Connect to KISS TNC via serial port.
 *		Print everything it sends to us.
 *
 * Inputs:	arg		- Currently not used.
 *
 * Global In:	port
 *		serial_speed
 *
 * Global Out:	serial_fd	- Need for sending to the TNC.
 *
 *--------------------------------------------------------------------*/

static THREAD_F tnc_listen_serial (void *arg)
{
	int client = 0;
	kiss_frame_t kstate;

	memset (&kstate, 0, sizeof(kstate));

	serial_fd = serial_port_open (port, serial_speed);

	if (serial_fd == MYFDERROR) {
	  text_color_set(DW_COLOR_ERROR);
 	  dw_printf("Unable to connect to KISS TNC serial port %s.\n", port);
#if __WIN32__
#else
	  // More detail such as "permission denied" or "no such device"
	  dw_printf("%s\n", strerror(errno));
#endif
	  exit (EXIT_FAILURE);
	}

/*
 * Read and print.
 */
	while (1) {
	  int ch;

	  ch = serial_port_get1(serial_fd);

	  if (ch < 0) {
 	    dw_printf("Read error from serial port KISS TNC.\n");
	    exit (EXIT_FAILURE);
	  }

	  // Feed in one byte at a time.
	  // kiss_process_msg is called when a complete frame has been accumulated.

	  kiss_rec_byte (&kstate, ch, verbose, NULL, client, NULL);
	}

} /* end tnc_listen_serial */



/*-------------------------------------------------------------------
 *
 * Name:        kiss_process_msg 
 *
 * Purpose:     Process a frame from the KISS TNC.
 *		This is called when a complete frame has been accumulated.
 *		In this case, we simply print it.
 *
 * Inputs:	kiss_msg	- Kiss frame with FEND and escapes removed.
 *				  The first byte contains channel and command.
 *
 *		kiss_len	- Number of bytes including the command.
 *
 *		debug		- Debug option is selected.
 *
 *		client		- Not used in this case.
 *
 *		sendfun		- Not used in this case.
 *
 *-----------------------------------------------------------------*/

void kiss_process_msg (unsigned char *kiss_msg, int kiss_len, int debug, struct kissport_status_s *kps, int client,
			void (*sendfun)(int chan, int kiss_cmd, unsigned char *fbuf, int flen, struct kissport_status_s *onlykps, int onlyclient))
{
	int chan;
	int cmd;
	packet_t pp;
	alevel_t alevel;

	chan = (kiss_msg[0] >> 4) & 0xf;
	cmd = kiss_msg[0] & 0xf;

	switch (cmd) 
	{
	  case KISS_CMD_DATA_FRAME:				/* 0 = Data Frame */

	    memset (&alevel, 0, sizeof(alevel));
	    pp = ax25_from_frame (kiss_msg+1, kiss_len-1, alevel);
	    if (pp == NULL) {
	       text_color_set(DW_COLOR_ERROR);
	       printf ("ERROR - Invalid KISS data frame from TNC.\n");
	    }
	    else {
	      char prefix[120];		// Channel and optional timestamp.
					// Like [0] or [2 12:34:56]

	      char addrs[AX25_MAX_ADDRS*AX25_MAX_ADDR_LEN];	// Like source>dest,digi,...,digi:
	      unsigned char *pinfo;
	      int info_len;

	      if (strlen(timestamp_format) > 0) {
	        char ts[100];
		timestamp_user_format (ts, sizeof(ts), timestamp_format);
	        snprintf (prefix, sizeof(prefix), "[%d %s]", chan, ts);
	      }
	      else {
	        snprintf (prefix, sizeof(prefix), "[%d]", chan);
	      }

	      ax25_format_addrs (pp, addrs);

	      info_len = ax25_get_info (pp, &pinfo);

	      text_color_set(DW_COLOR_REC);

	      dw_printf ("%s %s", prefix, addrs);		// [channel] Addresses followed by :

	      // Safe print will replace any unprintable characters with
	      // hexadecimal representation.

	      ax25_safe_print ((char *)pinfo, info_len, 0);
	      dw_printf ("\n");
#if __WIN32__
	      fflush (stdout);
#endif

/*
 * Add to receive queue directory if specified.
 * File name will be based on current local time.
 * If you want UTC, just set an environment variable like this:
 *
 *	TZ=UTC kissutil ...
 */
	      if (strlen(receive_output) > 0) {
		char fname [30];
	        char path [300];
	        FILE *fp;

	        timestamp_filename (fname, (int)sizeof(fname));

		strlcpy (path, receive_output, sizeof(path));
	        strlcat (path, DIR_CHAR, sizeof(path));
	        strlcat (path, fname, sizeof(path));

	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("Save received frame to %s\n", path);
	        fp = fopen (path, "w");
	        if (fp != NULL) {
	          fprintf (fp, "%s %s%s\n", prefix, addrs, pinfo);
	          fclose (fp);
	        }
	        else {
	          text_color_set(DW_COLOR_ERROR);
	          dw_printf ("Unable to open for write: %s\n", path);
	        }
	      }

	      ax25_delete (pp);
	    }
	    break;

          case KISS_CMD_SET_HARDWARE:			/* 6 = TNC specific */

	    kiss_msg[kiss_len] = '\0';
	    text_color_set(DW_COLOR_REC);
	    // Display as "h ..." for in/out symmetry.
	    // Use safe print here?
	    dw_printf ("[%d] h %s\n", chan, (char*)(kiss_msg+1));
	    break;

/*
 * The rest should only go TO the TNC and not come FROM it.
 */
          case KISS_CMD_TXDELAY:			/* 1 = TXDELAY */
          case KISS_CMD_PERSISTENCE:			/* 2 = Persistence */
          case KISS_CMD_SLOTTIME:			/* 3 = SlotTime */
          case KISS_CMD_TXTAIL:				/* 4 = TXtail */
          case KISS_CMD_FULLDUPLEX:			/* 5 = FullDuplex */
          case KISS_CMD_END_KISS:			/* 15 = End KISS mode, port should be 15. */
          default:
		
	    text_color_set(DW_COLOR_ERROR);
	    printf ("Unexpected KISS command %d, channel %d\n", cmd, chan);
	    break;
	}

} /* end kiss_process_msg */


// TODO:  We have multiple copies of this.  Move to some misc file.

void hex_dump (unsigned char *p, int len) 
{
	int n, i, offset;

	offset = 0;
	while (len > 0) {
	  n = len < 16 ? len : 16; 
	  printf ("  %03x: ", offset);
	  for (i=0; i<n; i++) {
	    printf (" %02x", p[i]);
	  }
	  for (i=n; i<16; i++) {
	    printf ("   ");
	  }
	  printf ("  ");
	  for (i=0; i<n; i++) {
	    printf ("%c", isprint(p[i]) ? p[i] : '.');
	  }
	  printf ("\n");
	  p += 16;
	  offset += 16;
	  len -= 16;
	}
}

static void usage(void)
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("\n");
	dw_printf ("kissutil  -  Utility for testing a KISS TNC.\n");
	dw_printf ("\n");	
	dw_printf ("Convert between KISS format and usual text representation.\n");
	dw_printf ("The TNC can be attached by TCP or a serial port.\n");
	dw_printf ("\n");
	dw_printf ("Usage:	kissutil  [ options ]\n");
	dw_printf ("\n");
 	dw_printf ("	-h	hostname of TCP KISS TNC, default localhost.\n");
 	dw_printf ("	-p	port, default 8001.\n");
	dw_printf ("		If it does not start with a digit, it is\n");
 	dw_printf ("		a serial port.  e.g.  /dev/ttyAMA0 or COM3.\n");
 	dw_printf ("	-s	Serial port speed, default 9600.\n");
	dw_printf ("	-v	Verbose.  Show the KISS frame contents.\n");
	dw_printf ("	-f	Transmit files directory.  Process and delete files here.\n");
	dw_printf ("	-o	Receive output queue directory.  Store received frames here.\n");
	dw_printf ("	-T	Precede received frames with 'strftime' format time stamp.\n");
	usage2();
	exit (EXIT_SUCCESS);
}

static void usage2 (void)
{
	text_color_set(DW_COLOR_INFO);
	dw_printf ("\n");
	dw_printf ("Input, starting with upper case letter or digit, is assumed\n");
	dw_printf ("to be an AX.25 frame in the usual TNC2 monitoring format.\n");
	dw_printf ("\n");
	dw_printf ("Input, starting with a lower case letter is a command.\n");
	dw_printf ("Whitespace, as shown in examples, is optional.\n");
	dw_printf ("\n");
	dw_printf ("	letter	meaning			example\n");
	dw_printf ("	------	-------			-------\n");
	dw_printf ("	d	txDelay, 10ms units	d 30\n");
	dw_printf ("	p	Persistence		p 63\n");
	dw_printf ("	s	Slot time, 10ms units	s 10\n");
	dw_printf ("	t	txTail, 10ms units	t 5\n");
	dw_printf ("	f	Full duplex		f 0\n");
	dw_printf ("	h	set Hardware 		h TNC:\n");
	dw_printf ("\n");
	dw_printf ("	Lines may be preceded by the form \"[9]\" to indicate a\n");
	dw_printf ("	channel other than the default 0.\n");
	dw_printf ("\n");
}

/* end kissutil.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2016, 2025, 2026  John Langner, WB2OSZ
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
 * Module:      tnctest.c
 *
 * Purpose:   	Test AX.25 connected mode between two TNCs.
 *		
 * Description:	The first TNC will connect to the second TNC and send a bunch of data.
 *		Proper transfer of data will be verified.
 *
 * Usage:	tnctest  [options]  port0=name0  port1=name1 
 *
 *		    options:
 *			-f filename	One way file transfer
 *				 	rather than builtin test.
 *
 *			-n n		Number of packets for bultin test.
 *
 *
 * Example:	tnctest  localhost:8000=direwolf  COM1=KPC-3+
 *
 *		Each port can have the following forms:
 *
 *		* host-name:tcp-port
 *		* ip-addr:tcp-port
 *		* tcp-port
 *		* serial port name (e.g.  COM1, /dev/ttyS0)
 *
 *---------------------------------------------------------------*/



/*
 * Native Windows:	Use the Winsock interface.
 * Linux:		Use the BSD socket interface.
 */

#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__

#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this

#else 

#include <stdlib.h>
#include <stddef.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
//#include <termios.h>
#include <sys/errno.h>
#endif

#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>


//#include "ax25_pad.h"
#include "textcolor.h"
#include "dtime_now.h"
#include "serial_port.h"


/* We don't deal with big-endian processors here. */
/* TODO: Use agwlib (which did not exist when this was written) */
/* rather than duplicating the effort here. */

struct agwpe_s {

  unsigned char portx;		/* 0 for first, 1 for second, etc. */
  unsigned char reserved1;
  unsigned char reserved2;
  unsigned char reserved3;

  unsigned char datakind;	/* message type, usually written as a letter. */
  unsigned char reserved4;
  unsigned char pid;
  unsigned char reserved5;

  char call_from[10];
  char call_to[10];
  int data_len;			/* Number of data bytes following. */
  int user_reserved;
};


#if __WIN32__
static unsigned __stdcall tnc_thread_net (void *arg);
static unsigned __stdcall tnc_thread_serial (void *arg);
#else
static void * tnc_thread_net (void *arg);
static void * tnc_thread_serial (void *arg);
#endif


static void tnc_connect (int from, int to);
static void tnc_disconnect (int from, int to);
static void tnc_send_data (int from, int to, char * data);
static void tnc_reset (int from, int to);

/*
 * Convert Internet address to text.
 * Can't use InetNtop because it is supported only on Windows Vista and later. 
 */

static char * ia_to_text (int  Family, void * pAddr, char * pStringBuf, size_t StringBufSize)
{
	struct sockaddr_in *sa4;
	struct sockaddr_in6 *sa6;

	switch (Family) {
	  case AF_INET:
	    sa4 = (struct sockaddr_in *)pAddr;
#if __WIN32__
	    snprintf (pStringBuf, StringBufSize, "%d.%d.%d.%d", sa4->sin_addr.S_un.S_un_b.s_b1,
						sa4->sin_addr.S_un.S_un_b.s_b2,
						sa4->sin_addr.S_un.S_un_b.s_b3,
						sa4->sin_addr.S_un.S_un_b.s_b4);
#else
	    inet_ntop (AF_INET, &(sa4->sin_addr), pStringBuf, StringBufSize);
#endif
	    break;
	  case AF_INET6:
	    sa6 = (struct sockaddr_in6 *)pAddr;
#if __WIN32__
	    snprintf (pStringBuf, StringBufSize, "%x:%x:%x:%x:%x:%x:%x:%x",
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[0]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[1]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[2]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[3]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[4]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[5]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[6]),
					ntohs(((unsigned short *)(&(sa6->sin6_addr)))[7]));
#else
	    inet_ntop (AF_INET6, &(sa6->sin6_addr), pStringBuf, StringBufSize);
#endif
	    break;
	  default:
	    snprintf (pStringBuf, StringBufSize, "Invalid address family!");
	}
	assert (strlen(pStringBuf) < StringBufSize);
	return pStringBuf;
}



/*------------------------------------------------------------------
 *
 * Name: 	main
 *
 * Purpose:   	Basic test for connected AX.25 data mode between TNCs.
 *
 * Usage:	Described above.
 *
 *---------------------------------------------------------------*/

#define MAX_TNC 2		// Just 2 for now.
				// Could be more later for multiple concurrent connections.

/* Obtained from the command line. */

static int num_tnc;				/* How many TNCs for this test? */
						/* Initially only 2 but long term we might */
						/* enhance it to allow multiple concurrent connections. */

static char hostname[MAX_TNC][50];		/* DNS host name or IPv4 address. */
						/* Some of the code is there for IPv6 but */
						/* needs more work. */
						/* Defaults to "localhost" if not specified. */

static char port[MAX_TNC][30];			/* If it begins with a digit, it is considered */
						/* a TCP port number at the hostname.  */
						/* Otherwise, we treat it as a serial port name. */

static char description[MAX_TNC][50];		/* Name used in the output. */

static int using_tcp[MAX_TNC];			/* Are we using TCP or serial port for each TNC? */
						/* Use corresponding one of the next two. */

static int server_sock[MAX_TNC];		/* File descriptor for AGW socket interface. */
						/* Set to -1 if not used. */
						/* (Don't use SOCKET type because it is unsigned.) */

static MYFDTYPE serial_fd[MAX_TNC];		/* Serial port handle. */

static volatile int busy[MAX_TNC];		/* True when TNC busy and can't accept more data. */
						/* For serial port, this is set by XON / XOFF characters. */

#define XOFF 0x13
#define XON  0x11

static char tnc_address[MAX_TNC][12];		/* Name of the TNC used in the frames.  Originally, this */
						/* was simply TNC0 and TNC1 but that can get hard to read */
						/* and confusing.   Later used DW0, DW1, for direwolf */
						/* so the direction of flow is easier to grasp. */

#if __WIN32__
	static HANDLE tnc_th[MAX_TNC];
#else
	static pthread_t tnc_tid[MAX_TNC];
#endif

#define LINE_WIDTH 80
//#define LINE_WIDTH 120			/* If I was more ambitious I might try to get */
						/* this from the terminal properties. */

static int column_width;			/* Line width divided by number of TNCs. */


/*
 * Current state for each TNC.
 */

volatile static int is_connected[MAX_TNC];		/* -1 = not yet available. */
							/* 0 = not connected. */
							/* 1 = connected. */

volatile static int have_cmd_prompt[MAX_TNC];		/* Set if "cmd:" was the last thing seen. */

volatile static int last_rec_seq[MAX_TNC];		/* Each data packet will contain a sequence number. */
						/* This is used to verify that all have been */
						/* received in the correct order. */

volatile static int stop_counting = 0;		/* A problem with a traditional TNC is that  */
						/* it's not always obvious what is data */
						/* and what is from the command interface. */
						/* For the file transfer test, special markers */
						/* are placed at start and end of text.  */
						/* This is set when the end of text marker is seen. */


/*
 * Start time so we can print relative elapsed time.
 */

static double start_dtime;

static void bidirectional_data (void);
static void send_file (void);

static char file_name[120];
static int max_count;		// for bidirectional data test.


/*****************************************************
 *
 * Main
 *
 ****************************************************/

int main (int argc, char *argv[])
{
	int j;
	int timeout;
	max_count = 9999;
	strlcpy (file_name, "", sizeof(file_name));

#if __WIN32__
#else
	int e;

 	setlinebuf (stdout);
#endif

	start_dtime = dtime_monotonic();

/*
 * Check for any command line options.
 */
	int opt;
	while ((opt = getopt(argc, argv, "f:n:")) != -1) {
          switch (opt) {
           case 'f':				/* -f one way file transfer */
	    strlcpy (file_name, optarg, sizeof(file_name));
            break;
	
           case 'n':				/* -n number of packets  */
	    max_count = atoi(optarg);
	    if (max_count < 1) max_count = 1;
	    if (max_count > 9999) max_count = 9999;  // Four digit limit!
            break;
	
	   default:
	    printf ("Invalid command line option %c", optopt);
	    exit (EXIT_FAILURE);
	    break;
	  }
	}
	

/*
 * Extract two TNC references from command line args.
 */
	num_tnc = argc - optind;

	if (num_tnc < 2 || num_tnc > MAX_TNC) {
	  printf ("Specify minimum 2, maximum %d TNCs on the command line.\n", MAX_TNC);
	  exit (EXIT_FAILURE);
	}

	column_width = LINE_WIDTH / num_tnc;
	
	for (j = 0; optind < argc; optind++, j++) {
	  char stemp[100];
	  char *p;

/* Each command line argument should be of the form "port=description." */

	  strlcpy (stemp, argv[optind], sizeof(stemp));
	  p = strtok (stemp, "=");
	  if (p == NULL) {
	    printf ("Internal error 1\n");
	    exit (EXIT_FAILURE);
	  }
	  strlcpy (hostname[j], "localhost", sizeof(hostname[j]));
	  strlcpy (port[j], p, sizeof(port[j]));
	  p = strtok (NULL, "=");
	  if (p == NULL) {
	    printf ("Missing description after %s\n", port[j]);
	    exit (EXIT_FAILURE);
	  }
	  strlcpy (description[j], p, sizeof(description[j]));


/* If the port contains ":" split it into hostname (or addr) and port number. */
/* Haven't thought about IPv6 yet. */

	  strlcpy (stemp, port[j], sizeof(stemp));

	  char *h;

	  h = strtok (stemp, ":");
	  if (h != NULL) {
	    p = strtok (NULL, ":");
	    if (p != NULL) {
	      strlcpy (hostname[j], h, sizeof(hostname[j]));
	      strlcpy (port[j], p, sizeof(port[j]));
	    }
	  }
	}

	for (j=0; j<num_tnc; j++) {
	  is_connected[j] = -1;			/* not yet available. */
	  last_rec_seq[j] = 0;
	  server_sock[j] = -1;
	}


	for (j=0; j<num_tnc; j++) {

/* If port begins with digit, consider it to be TCP. */
/* Otherwise, treat as serial port name. */

	  using_tcp[j] = isdigit(port[j][0]);

/* Addresses to use in the AX.25 frames. */

	  if (using_tcp[j]) {
	    snprintf (tnc_address[j], sizeof(tnc_address[j]), "DW%d", j);
	  }
	  else {
	    snprintf (tnc_address[j], sizeof(tnc_address[j]), "TNC%d", j);
	  }

#if __WIN32__
	  if (using_tcp[j]) {
	    tnc_th[j] = (HANDLE)_beginthreadex (NULL, 0, tnc_thread_net,  (void*)(ptrdiff_t)j, 0, NULL);
	  }
	  else {
	    tnc_th[j] = (HANDLE)_beginthreadex (NULL, 0, tnc_thread_serial,  (void*)(ptrdiff_t)j, 0, NULL);
	  }
	  if (tnc_th[j] == NULL) {
	    printf ("Internal error: Could not create TNC thread %d.\n", j);
	    exit (EXIT_FAILURE);
	  }
#else
	  if (using_tcp[j]) {
	    e = pthread_create (&tnc_tid[j], NULL, tnc_thread_net,  (void*)(ptrdiff_t)j);
	  }
	  else {
	    e = pthread_create (&tnc_tid[j], NULL, tnc_thread_serial,  (void*)(ptrdiff_t)j);
	  }
	  if (e != 0) {
	    perror("Internal error: Could not create TNC thread.");
	    exit (EXIT_FAILURE);
	  }
#endif
	}

/*
 * Wait until all TNCs are available.
 */

	int ready = 0;
	while ( ! ready) {
	
	  SLEEP_MS(100);
	  ready = 1;
	  for (j=0; j<num_tnc; j++) {
	    if (is_connected[j] < 0) ready = 0;
	  }
	}
	
	printf ("Andiamo!\n");


/*
 * First, establish a connection from TNC number 0 to the other(s).
 * Wait until successful.
 */

	printf ("Trying to establish connection...\n");

	tnc_connect (0, 1);

	timeout = 600;
	ready = 0;
	while ( ! ready && timeout > 0) {
	
	  SLEEP_MS(100);
	  timeout--;
	  ready = 1;
	  for (j=0; j<num_tnc; j++) {
	    if (is_connected[j] <= 0) ready = 0;
	  }
	}

	if (timeout == 0) {
	  printf ("ERROR: Gave up waiting for connect!\n");
	  tnc_disconnect (1,0);		// Tell other TNC.
	  SLEEP_MS(5000);
	  printf ("TEST FAILED!\n");
	  exit (EXIT_FAILURE);
	}

/*
 * Send data.
 */

	SLEEP_MS(2000);
	SLEEP_MS(2000);

	printf ("Send data...\n");

// We have two alternatives.
// Default: Interleaved data in both directions.
// File mode: Single direction.  Send a file

	if (strlen(file_name) > 0) {
	  send_file ();
	}
	else {
	  bidirectional_data();
	}
	exit (EXIT_SUCCESS);

} // end main


/****************************************************
 *
 * Name:	send_file
 *
 * Purpose:	Read a file and send in one direction only.
 *
 ****************************************************/

static void send_file (void)
{
	int j = 0;
	int timeout = 0;
	int send_count = 0;
	int errors = 0;
	FILE *fp;
	char stuff[256];

	fp = fopen (file_name, "r");
	if (fp == NULL) {
	  printf ("Can't open %s for read.\n", file_name);
	  exit (EXIT_FAILURE);
	}

	tnc_send_data (0, 1, "ZCZC\r");

	while (fgets(stuff, sizeof(stuff), fp) != NULL) {
	  // Change Linux new line to carriage return.
	  char *q = strrchr (stuff, '\n');
	  if (q != NULL) *q = '\r';
	  //if (strlen(stuff) == 0) strlcpy (stuff, "+", sizeof(stuff));

	  int throttle_timeout = 4 * 60;	// seconds

	  while (send_count > last_rec_seq[1] + 20 && throttle_timeout > 0) {
	    printf ("Throttle sending.  Send count %d.  Last recd %d.  Timeout %d.\n",
				send_count, last_rec_seq[1], throttle_timeout);
	    SLEEP_MS (2000);
	    throttle_timeout -= 2;
	  }
	  if (throttle_timeout > 0) {
	    send_count++;
	    tnc_send_data (0, 1, stuff);
	  }
	  else {
	    printf ("Throttle timeout error.\n");
	    break;
	  }
	}

	tnc_send_data (0, 1, "NNNN\r");
	fclose (fp);

/*
 * Hang around until we get last expected reply or there is too much time with no activity.
 */

	int no_activity = 0;

// Might be able to go back to 60 seconds after resolving excessive delays.
#define INACTIVE_TIMEOUT_F 120

	while (last_rec_seq[1] < send_count && no_activity < INACTIVE_TIMEOUT_F) {

	  SLEEP_MS(1000);
	  no_activity++;
	}

	if (last_rec_seq[1] == send_count) {
	  printf ("--------\nSUCCESS!  Got last expected reply.\n--------\n");
	}
	else {
	  printf ("ERROR: Timeout - No incoming activity for %d seconds.\n", no_activity);
	  errors++;
	}

/*
 * Did we get all expected replies?
 */
	if (last_rec_seq[1] != send_count) {
	  printf ("ERROR: Received only %d when we were expecting %d.\n", last_rec_seq[1], send_count);
	  errors++;
	}

/*
 * Ask for disconnect.  Wait until complete.
 */

	tnc_disconnect (0, 1);

	timeout = 200;		// 20 sec should be generous.
	int ready = 0;
	while ( ! ready && timeout > 0) {
	
	  SLEEP_MS(100);
	  timeout--;
	  ready = 1;
	  for (j=0; j<num_tnc; j++) {
	    if (is_connected[j] != 0) ready = 0;
	  }
	}

	if (timeout == 0) {
	  printf ("ERROR: Gave up waiting for disconnect!\n");
	  tnc_reset (1,0);		// Don't leave TNC in bad state for next time.
	  SLEEP_MS(10000);
	  errors++;
	}

	if (errors != 0) {
	  printf ("TEST FAILED!\n");
	  exit (EXIT_FAILURE);
	}

	printf ("Success!\n");

} // end send_file



/****************************************************
 *
 * Name:	Bidirectional Data
 *
 * Purpose:	Send data in both directions concurrently.
 *
 ****************************************************/

static void bidirectional_data (void)
{
	int j;
	int timeout;
	int send_count = 0;
	int burst_size = 1;
	int errors = 0;

	while ( send_count < max_count) {

	  char data[80];
	  int n;

	  for (n = 1; n <= burst_size && send_count < max_count; n++) {

	    send_count++;

	    // Don't let it get too far ahead.

	    int throttle_timeout = 4 * 60;	// seconds

	    while (send_count > last_rec_seq[1] + 20 && throttle_timeout > 0) {
	      printf ("Throttle sending.  Send count %d.  Last recd %d.  Timeout %d.\n",
				send_count, last_rec_seq[1], throttle_timeout);
	      SLEEP_MS (2000);
	      throttle_timeout -= 2;
	    }
	    if (throttle_timeout <= 0) {
	      printf ("Throttle timeout error.\n");
	      exit (1);
	    }

	    snprintf (data, sizeof(data), "%04d send data\r", send_count);
	    tnc_send_data (0, 1, data);
	  }

	  SLEEP_MS(3000 + 1000 * burst_size);
	  //SLEEP_MS(3000 + 500 * burst_size);		// OK for low error rate
	  //SLEEP_MS(3000 + 3000 * burst_size);
	  //SLEEP_MS(3000);

	  burst_size++;
	}

/*
 * Hang around until we get last expected reply or there is too much time with no activity.
 */

	int last0 = last_rec_seq[0];
	int last1 = last_rec_seq[1];
	int no_activity = 0;

#define INACTIVE_TIMEOUT_B 120 

	while (last_rec_seq[0] != max_count && no_activity < INACTIVE_TIMEOUT_B) {

	  SLEEP_MS(1000);
	  no_activity++;

	  if (last_rec_seq[0] > last0) {
	    last0 = last_rec_seq[0];
	    no_activity = 0;
	  }
	  if (last_rec_seq[1] > last1) {
	    last1 = last_rec_seq[1];
	    no_activity = 0;
	  }
	}

	if (last_rec_seq[0] == max_count) {
	  printf ("Got last expected reply.\n");
	}
	else {
	  printf ("ERROR: Timeout - No incoming activity for %d seconds.\n", no_activity);
	  errors++;
	}

/*
 * Did we get all expected replies?
 */
	if (last_rec_seq[0] != max_count) {
	  printf ("ERROR: Last received reply was %d when we were expecting %d.\n", last_rec_seq[0], max_count);
	  errors++;
	}

/*
 * Ask for disconnect.  Wait until complete.
 */

	tnc_disconnect (0, 1);

	timeout = 200;		// 20 sec should be generous.
	int ready = 0;
	while ( ! ready && timeout > 0) {
	
	  SLEEP_MS(100);
	  timeout--;
	  ready = 1;
	  for (j=0; j<num_tnc; j++) {
	    if (is_connected[j] != 0) ready = 0;
	  }
	}

	if (timeout == 0) {
	  printf ("ERROR: Gave up waiting for disconnect!\n");
	  tnc_reset (1,0);		// Don't leave TNC in bad state for next time.
	  SLEEP_MS(10000);
	  errors++;
	}

	if (errors != 0) {
	  printf ("TEST FAILED!\n");
	  exit (EXIT_FAILURE);
	}
	printf ("Success!\n");

} // end bidirectional_data


/*-------------------------------------------------------------------
 *
 * Name:        process_rec_data
 *
 * Purpose:     Look for our data with text sequence numbers, not to be 
 *		confused with the AX.25 I frame sequence numbers.
 *
 * Inputs:	my_index	- 0 for the call originator.
 *				  >1 for the other end which answers.
 *
 *		data		- Should look something like this:
 *				   9999 send data
 *				   9999 reply
 *
 * Global In/Out:	last_rec_seq[my_index]
 *
 * Description:	Look for expected format.
 *		Extract the sequence number.
 *		Verify that it is the next expected one.
 *		Update it.
 *
 *--------------------------------------------------------------------*/

void process_rec_data (int my_index, char *data)
{
	int n;

	assert (my_index >= 0 && my_index <= 1);

	// Status from OpenTNC, don't count as data.
	if (strncmp(data, "Inbound call, entering converse mode", 30) == 0) {
	  return;
	}

	if (strlen(file_name) > 0) {
	  if (strncmp(data, "ZCZC", 4) == 0) {
	    last_rec_seq[my_index] = 0;
	  }
	  else if (strncmp(data, "NNNN", 4) == 0) {
	    stop_counting = 1;
	  }
	  else {
	    // Just count lines.  We don't care about content.
	    if ( ! stop_counting) {
	      last_rec_seq[my_index]++;
	      //printf ("num recd %d\n", last_rec_seq[my_index]);
	    }
	  }
	  return;
	}
	if (isdigit(*data) && strncmp(data+4, " send", 5) == 0) {
	  if (my_index > 0) {
	    last_rec_seq[my_index]++;

	    n = atoi(data);
	    //printf ("debug: got %d from send\n", n);

	    if (n != last_rec_seq[my_index]) {
	      printf ("%*s%s: Received %d when %d was expected.\n", my_index*column_width, "", tnc_address[my_index], n, last_rec_seq[my_index]);
	      SLEEP_MS(10000);
	      printf ("TEST FAILED!\n");
	      exit (EXIT_FAILURE);
	    }
	  }
	}

	else if (isdigit(*data) && strncmp(data+4, " reply", 6) == 0) {
	  if (my_index == 0) {

	    // Here is something I was not expecting.
	    // A serial port TNC might combine multiple lines into a single frame.  e.g.
	    //	TNC1>DW0:(I cmd, ...)0003 reply<0x0d>0004 reply<0x0d>0005 reply<0x0d>0006 reply<0x0d>

	    for (char *d = data; strlen(d) >= 11 && isdigit(*d); d += 11) {
	      last_rec_seq[my_index]++;
	      n = atoi(d);
	      //printf ("\t\t\t\t\tdebug: got %d from reply\n", n);
	      if (n != last_rec_seq[my_index]) {
	        printf ("%*s%s: Received %d when %d was expected.\n", my_index*column_width, "",
			tnc_address[my_index], n, last_rec_seq[my_index]);
	        SLEEP_MS(10000);
	        printf ("TEST FAILED!\n");
	        exit (EXIT_FAILURE);
	      }
	    }
	  }
	}

#if 0	// This is for testing segmentation.

	else if (data[0] == 'A') {

	  if (strncmp(data, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", strlen(data)-1) != 0) {
	      printf ("%*s%s: Segmentation is broken.\n", my_index*column_width, "", tnc_address[my_index]);
	      SLEEP_MS(10000);
	      printf ("TEST FAILED!\n");
	      exit (EXIT_FAILURE);
	  }
	}
#endif
}



/*-------------------------------------------------------------------
 *
 * Name:        tnc_thread_net
 *
 * Purpose:     Establish connection with a TNC via network.
 *
 * Inputs:	arg		- My instance index, 0 thru MAX_TNC-1.
 *
 * Outputs:	packets		- Received packets are put in the corresponding column
 *				  and sent to a common function to check that they
 *				  all arrived in order.
 *
 * Global Out:	is_connected	- Updated when connected/disconnected notfications are received.
 *
 * Description:	Perform any necessary configuration for the TNC then wait
 *		for responses and process them.
 *
 *--------------------------------------------------------------------*/

#define MAX_HOSTS 30

#if __WIN32__
static unsigned __stdcall tnc_thread_net (void *arg)
#else
static void * tnc_thread_net (void *arg)	
#endif	
{
	int my_index = (int)(ptrdiff_t)arg;

	struct addrinfo hints;
	struct addrinfo *ai_head = NULL;
	struct addrinfo *ai;
	struct addrinfo *hosts[MAX_HOSTS];
	int num_hosts, n;
	int err;
	char ipaddr_str[46];		/* text form of IP address */
#if __WIN32__
	WSADATA wsadata;
#endif

	struct agwpe_s mon_cmd;
	char data[4096];
	double dnow;


#if DEBUGx
        printf ("DEBUG: tnc_thread_net %d start, port = '%s'\n", my_index, port[my_index]);
#endif

#if __WIN32__
	err = WSAStartup (MAKEWORD(2,2), &wsadata);
	if (err != 0) {
	    printf("WSAStartup failed: %d\n", err);
	    return (0);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
          printf("Could not find a usable version of Winsock.dll\n");
          WSACleanup();
	  //sleep (1);
          return (0);
	}
#endif

	memset (&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;	/* Allow either IPv4 or IPv6. */
	// hints.ai_family = AF_INET;	/* IPv4 only. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

/*
 * Connect to TNC server.
 */

	ai_head = NULL;
	err = getaddrinfo(hostname[my_index], port[my_index], &hints, &ai_head);
	if (err != 0) {
#if __WIN32__
	  printf ("Can't get address for server %s, err=%d\n", 
					hostname[my_index], WSAGetLastError());
#else 
	  printf ("Can't get address for server %s, %s\n", 
					hostname[my_index], gai_strerror(err));
#endif
	  freeaddrinfo(ai_head);
      	  exit (1);
	}

#if DEBUG_DNS
	printf ("getaddrinfo returns:\n");
#endif
	num_hosts = 0;
	for (ai = ai_head; ai != NULL; ai = ai->ai_next) {
#if DEBUG_DNS
	  ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	  printf ("    %s\n", ipaddr_str);
#endif
	  hosts[num_hosts] = ai;
	  if (num_hosts < MAX_HOSTS) num_hosts++;
	}

#if DEBUG_DNS
	printf ("addresses for hostname:\n");
	for (n=0; n<num_hosts; n++) {
	  ia_to_text (hosts[n]->ai_family, hosts[n]->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	  printf ("    %s\n", ipaddr_str);
	}
#endif

	// Try each address until we find one that is successful.

	for (n=0; n<num_hosts; n++) {
#if __WIN32__
	  SOCKET is;
#else
	  int is;
#endif
	  ai = hosts[n];

	  ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	  is = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
#if __WIN32__
	  if (is == INVALID_SOCKET) {
	    printf ("Socket creation failed, err=%d", WSAGetLastError());
	    WSACleanup();
	    is = -1;
	    continue;
	  }
#else
	  if (err != 0) {
	    printf ("Socket creation failed, err=%s", gai_strerror(err));
	    (void) close (is);
	    is = -1;
	    continue;
	  }
#endif

#ifndef DEBUG_DNS 
	  err = connect(is, ai->ai_addr, (int)ai->ai_addrlen);
#if __WIN32__
	  if (err == SOCKET_ERROR) {
#if DEBUGx
	    printf("Connect to %s on %s (%s), port %s failed.\n",
					description[my_index], hostname[my_index], ipaddr_str, port[my_index]);
#endif
	    closesocket (is);
	    is = -1;
	    continue;
	  }
#else
	  if (err != 0) {
#if DEBUGx
	    printf("Connect to %s on %s (%s), port %s failed.\n",
					description[my_index], hostname[my_index], ipaddr_str, port[my_index]);
#endif
	    (void) close (is);
	    is = -1;
	    continue;
	  }
	  int flag = 1;
	  err = setsockopt (is, IPPROTO_TCP, TCP_NODELAY, (void*)(long)(&flag), (socklen_t)sizeof(flag));
	  if (err < 0) {
	    printf("setsockopt TCP_NODELAY failed.\n");
	  }
#endif

/* Success. */


	  server_sock[my_index] = is;
#endif	  
	  break;
	}

	freeaddrinfo(ai_head);

	if (server_sock[my_index] == -1) {

 	  printf("TNC %d unable to connect to %s on %s (%s), port %s\n", 
			my_index, description[my_index], hostname[my_index], ipaddr_str, port[my_index] );
	  exit (1);
	}


#if 1		// Temp test just to get something.

/*
 * Send command to toggle reception of frames in raw format.
 */
	memset (&mon_cmd, 0, sizeof(mon_cmd));

	mon_cmd.datakind = 'k';

	SOCK_SEND(server_sock[my_index], (char*)(&mon_cmd), sizeof(mon_cmd));

#endif

/*
 * Send command to register my callsign for incoming connect request.
 * Not really needed when we initiate the connection.
 */

	memset (&mon_cmd, 0, sizeof(mon_cmd));

	mon_cmd.datakind = 'X';
	strlcpy (mon_cmd.call_from, tnc_address[my_index], sizeof(mon_cmd.call_from));

	SOCK_SEND(server_sock[my_index], (char*)(&mon_cmd), sizeof(mon_cmd));



/*
 * Inform main program and observer that we are ready to go.
 */
 	printf("TNC %d now available.  %s on %s (%s), port %s\n", 
			my_index, description[my_index], hostname[my_index], ipaddr_str, port[my_index] );
	is_connected[my_index] = 0;

/*
 * Print what we get from TNC.
 */

	while (1) {
	  int n;

	  n = SOCK_RECV (server_sock[my_index], (char*)(&mon_cmd), sizeof(mon_cmd));

	  if (n != sizeof(mon_cmd)) {
	    printf ("Read error, TNC %d received %d command bytes.\n", my_index, n);
	    exit (1);
	  }

#if DEBUGx
	  printf ("TNC %d received '%c' data, data_len = %d\n", 
			my_index, mon_cmd.datakind, mon_cmd.data_len);
#endif
	  assert (mon_cmd.data_len >= 0 && mon_cmd.data_len < (int)(sizeof(data)));

	  if (mon_cmd.data_len > 0) {

	    n = SOCK_RECV (server_sock[my_index], data, mon_cmd.data_len);

	    if (n != mon_cmd.data_len) {
	      printf ("Read error, TNC %d received %d data bytes.\n", my_index, n);
	      exit (1);
	    }
	    data[mon_cmd.data_len] = '\0';
	  }

/* 
 * What did we get?
 */

	  dnow = dtime_monotonic();

	  switch (mon_cmd.datakind) {

	    case 'C':					// AX.25 Connection Received

 	      printf("%*s[R %.3f] *** Connected to %s ***\n", my_index*column_width, "", dnow-start_dtime, mon_cmd.call_from);
	      is_connected[my_index] = 1;

	      break;

 	    case 'D':					// Connected AX.25 Data

		// I ran into the situation where a tradional style serial port TNC
		// was combining multiple lines (i.e. separated by CR) into a single
		// AX.25 frame.  Even worse, a line could be split across two frames.
		// We need treat the frames like a byte stream and extract lines.

	      {
	      static char linebuf[4096];
	      static int linelen = 0;

	      for (char *p = data; *p != '\0'; p++) {
	        linebuf[linelen++] = *p;
	        linebuf[linelen] = '\0';

	        if (*p == 0x0d) {

	          //printf ("N>>>my_index=%d\n", my_index);
	          printf("%*s[R %.3f] %s\n", my_index*column_width, "", dnow-start_dtime, linebuf);

	          process_rec_data (my_index, linebuf);


	          if (isdigit(linebuf[0]) && isdigit(linebuf[1]) && isdigit(linebuf[2]) && isdigit(linebuf[3]) &&
			strncmp(linebuf+4, " send", 5) == 0) {
	            // Expected message.   Make sure it is expected sequence and send reply.
	            int n = atoi(linebuf);
	            char reply[80];
	            snprintf (reply, sizeof(reply), "%04d reply\r", n);
	            tnc_send_data (my_index, 1 - my_index, reply);

		    // HACK!
		    // It gets very confusing because N(S) and N(R) are very close.
		    // Send a couple dozen I frames so they will be easier to distinguish visually.
		    // Currently don't have the same in serial port version.

		    // We change the length each time to test segmentation.
		    // Set PACLEN to some very small number like 5.

		    if (n == 1 && max_count > 1) {
	              int j;
	              for (j = 1; j <= 26; j++) {
	                snprintf (reply, sizeof(reply), "%.*s\r", j, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
	                tnc_send_data (my_index, 1 - my_index, reply);
	              }
	            }
	          }  // if 9999 send

	          linelen = 0;
	          linebuf[linelen] = '\0';

	        }  // Have full line
	      }  // For each data byte in received frame.
	      }  // case D

	      break;

 	    case 'd':					// Disconnected

 	      printf("%*s[R %.3f] *** Disconnected from %s ***\n", my_index*column_width, "", dnow-start_dtime, mon_cmd.call_from);
	      is_connected[my_index] = 0;

	      break;

	    case 'y':					// Outstanding frames waiting on a Port

 	      printf("%*s[R %.3f] *** Outstanding frames waiting %d ***\n", my_index*column_width, "", dnow-start_dtime, 123);  
// TODO outstanding_frames_waiting[my_index] = ...

	      break;

	    default:

 	      //printf("%*s[R %.3f] --- Ignoring cmd kind '%c' ---\n", my_index*column_width, "", dnow-start_dtime, mon_cmd.datakind);  

	      break;
	  }
	}

} /* end tnc_thread_net */





/*-------------------------------------------------------------------
 *
 * Name:        tnc_thread_serial
 *
 * Purpose:     Establish connection with a TNC via serial port.
 *
 * Inputs:	arg		- My instance index, 0 thru MAX_TNC-1.
 *
 * Outputs:	packets		- Received packets are put in the corresponding column
 *				  and sent to a common function to check that they
 *				  all arrived in order.
 *
 * Global Out:	is_connected	- Updated when connected/disconnected notfications are received.
 *
 * Description:	Perform any necessary configuration for the TNC then wait
 *		for responses and process them.
 *
 *--------------------------------------------------------------------*/


#if __WIN32__
typedef HANDLE MYFDTYPE;
#define MYFDERROR INVALID_HANDLE_VALUE
#else
typedef int MYFDTYPE;
#define MYFDERROR (-1)
#endif


#if __WIN32__
static unsigned __stdcall tnc_thread_serial (void *arg)
#else
static void * tnc_thread_serial (void *arg)	
#endif	
{
	int my_index = (int)(ptrdiff_t)arg;
	char cmd[80];

	serial_fd[my_index] = serial_port_open (port[my_index], 9600);

	if (serial_fd[my_index] == MYFDERROR) {
 	  printf("TNC %d unable to connect to %s on %s.\n", 
			my_index, description[my_index], port[my_index] );
	  exit (1);
	}


/*
 * Make sure we are in command mode.
 */

	strcpy (cmd, "\003\rreset\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (1000);

	strcpy (cmd, "echo on\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

// do any necessary set up here. such as setting mycall

	snprintf (cmd, sizeof(cmd), "mycall %s\r", tnc_address[my_index]);
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

// AX.25 version.

	strcpy (cmd, "ax25v 2.2\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

// TX window size.
// There doesn't seem to be a separate option for mod 128.
// Seems the max we can set it to is 4.

	strcpy (cmd, "maxframe 7\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

// Allow incoming connection request.

	strcpy (cmd, "conok on\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

// Hack to prevent multiple lines in one frame.

	//strcpy (cmd, "paclen 11\r");
	//serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	//SLEEP_MS (300);


// Don't want to stop tty output when typing begins.

	strcpy (cmd, "flow off\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (300);

	strcpy (cmd, "echo off\r");
	serial_port_write (serial_fd[my_index], cmd, strlen(cmd));
	SLEEP_MS (500);

/* Success. */

 	printf("TNC %d now available.  %s on %s\n", 
			my_index, description[my_index], port[my_index] );
	is_connected[my_index] = 0;


/*
 * Read and print.
 */

	while (1) {
	  int ch;
	  char result[500];
	  int len;
	  int done;

	  len = 0;
	  result[len] = '\0';
	  done = 0;

	  while ( ! done) {

	    ch = serial_port_get1(serial_fd[my_index]);

	    if (ch < 0) {
 	      printf("TNC %d fatal read error.\n", my_index);
	      exit (1);
	    }

	    if (ch == '\r') {
	      result[len++] = '\\';
	      result[len++] = 'r';
	      result[len] = '\0';
	      done = 1;
	    }
	    else if (ch == '\n') {
	      //result[len++] = '\\';
	      //result[len++] = 'n';
	      //result[len] = '\0';
	      done = 1;
	    }
	    else if (ch == XOFF) {
	      double dnow = dtime_monotonic();
	      printf("%*s[R %.3f] <XOFF>\n", my_index*column_width, "", dnow-start_dtime);
	      busy[my_index] = 1;
	    }
	    else if (ch == XON) {
	      double dnow = dtime_monotonic();
	      printf("%*s[R %.3f] <XON>\n", my_index*column_width, "", dnow-start_dtime);
	      busy[my_index] = 0;
	    }
	    else if (isprint(ch)) {
	      result[len++] = ch;
	      result[len] = '\0';
	    }
	    else {
	      char hex[12];

	      snprintf (hex, sizeof(hex), "<x%02x>", ch);
	      strlcat (result, hex, sizeof(result));
	      len = strlen(result);
	    }
	    if (strcmp(result, "cmd:") == 0) {
	      done = 1;
	      have_cmd_prompt[my_index] = 1;
	    }
	    else {
	      have_cmd_prompt[my_index] = 0;
	    }    
	  }

	  if (len > 0) {

	    double dnow = dtime_monotonic();

	    //printf ("S>> my_index=%d\n", my_index);
	    printf("%*s[R %.3f] %s\n", my_index*column_width, "", dnow-start_dtime, result);

	    if (strncmp(result, "*** CONNECTED", 13) == 0) {
	      is_connected[my_index] = 1;
	      //printf ("DEBUG: is_connected[%d] = 1\n", my_index);
	    }

	    if (strncmp(result, "*** DISCONNECTED", 16) == 0) {
	      is_connected[my_index] = 0;
	      //printf ("DEBUG: is_connected[%d] = 0\n", my_index);
	    }

	    if (strncmp(result, "Not while connected", 19) == 0) {
	      // Not expecting this.
	      // What to do?
	    }

	    // if (strncmp(result, "Inbound call, entering converse mode", 30) == 0) {
	    //   last_rec_seq[my_index] = -1;
	    // }

	    process_rec_data (my_index, result);

	    if (isdigit(result[0]) && isdigit(result[1]) && isdigit(result[2]) && isdigit(result[3]) &&
	        strncmp(result+4, " send", 5) == 0) {
	      // Expected message.   Make sure it is expected sequence and send reply.
	      int n = atoi(result);
	      char reply[80];
	      snprintf (reply, sizeof(reply), "%04d reply\r", n);
	      tnc_send_data (my_index, 1 - my_index, reply);
	    }

          }
	}

} /* end tnc_thread_serial */




static void tnc_connect (int from, int to)
{
	SLEEP_MS (3000);

	double dnow = dtime_monotonic();

 	printf("%*s[T %.3f] *** Send connect request ***\n", from*column_width, "", dnow-start_dtime);

	if (using_tcp[from]) {

//struct agwpe_s {	
//  short portx;			/* 0 for first, 1 for second, etc. */
//  short port_hi_reserved;	
//  short datakind;		/* message type */
//  short kind_hi;
//  char call_from[10];
//  char call_to[10];
//  int data_len;			/* Number of data bytes following. */
//  int user_reserved;
//};
	  struct agwpe_s cmd;

	  memset (&cmd, 0, sizeof(cmd));

	  cmd.datakind = 'C';
	  strlcpy (cmd.call_from, tnc_address[from], sizeof(cmd.call_from));
	  strlcpy (cmd.call_to, tnc_address[to], sizeof(cmd.call_to));

	  SOCK_SEND(server_sock[from], (char*)(&cmd), sizeof(cmd));
	}
	else {

	  char cmd[80];

	  if (! have_cmd_prompt[from]) {

	    SLEEP_MS (1500);
	    strcpy (cmd, "\003\003\003");
	    serial_port_write (serial_fd[from], cmd, strlen(cmd));
	    SLEEP_MS (1500);

	    strcpy (cmd, "\r");
	    serial_port_write (serial_fd[from], cmd, strlen(cmd));
	    SLEEP_MS (200);
	  }

	  snprintf (cmd, sizeof(cmd), "connect %s\r", tnc_address[to]);
	  serial_port_write (serial_fd[from], cmd, strlen(cmd));
	}

} /* end tnc_connect */


static void tnc_disconnect (int from, int to)
{
	double dnow = dtime_monotonic();

 	printf("%*s[T %.3f] *** Send disconnect request ***\n", from*column_width, "", dnow-start_dtime);

	if (using_tcp[from]) {

	  struct agwpe_s cmd;

	  memset (&cmd, 0, sizeof(cmd));

	  cmd.datakind = 'd';
	  strlcpy (cmd.call_from, tnc_address[from], sizeof(cmd.call_from));
	  strlcpy (cmd.call_to, tnc_address[to], sizeof(cmd.call_to));

	  SOCK_SEND(server_sock[from], (char*)(&cmd), sizeof(cmd));
	}
	else {

	  char cmd[80];

	  if (! have_cmd_prompt[from]) {

	    SLEEP_MS (1500);
	    strcpy (cmd, "\003\003\003");
	    serial_port_write (serial_fd[from], cmd, strlen(cmd));
	    SLEEP_MS (1500);

	    strcpy (cmd, "\r");
	    serial_port_write (serial_fd[from], cmd, strlen(cmd));
	    SLEEP_MS (200);
	  }

	  strcpy (cmd, "disconnect\r");
	  serial_port_write (serial_fd[from], cmd, strlen(cmd));
	}

} /* end tnc_disconnect */


static void tnc_reset (int from, int to)
{
	double dnow = dtime_monotonic();

 	printf("%*s[T %.3f] *** Send reset ***\n", from*column_width, "", dnow-start_dtime);

	if (using_tcp[from]) {


	}
	else {

	  char cmd[80];

	  SLEEP_MS (1500);
	  strcpy (cmd, "\003\003\003");
	  serial_port_write (serial_fd[from], cmd, strlen(cmd));
	  SLEEP_MS (1500);

	  strcpy (cmd, "\r");
	  serial_port_write (serial_fd[from], cmd, strlen(cmd));
	  SLEEP_MS (200);

	  strcpy (cmd, "reset\r");
	  serial_port_write (serial_fd[from], cmd, strlen(cmd));
	}

} /* end tnc_disconnect */



static void tnc_send_data (int from, int to, char * data)
{
	double dnow = dtime_monotonic();

 	printf("%*s[T %.3f] %s\n", from*column_width, "", dnow-start_dtime, data);

	if (using_tcp[from]) {

	  struct {
	    struct agwpe_s hdr;
	    char data[256];
	  } cmd;

	  memset (&cmd.hdr, 0, sizeof(cmd.hdr));

	  cmd.hdr.datakind = 'D';
	  cmd.hdr.pid = 0xf0;
	  strlcpy (cmd.hdr.call_from, tnc_address[from], sizeof(cmd.hdr.call_from));
	  strlcpy (cmd.hdr.call_to, tnc_address[to], sizeof(cmd.hdr.call_to));
	  cmd.hdr.data_len = strlen(data);
	  strlcpy (cmd.data, data, sizeof(cmd.data));

	  SOCK_SEND(server_sock[from], (char*)(&cmd), sizeof(cmd.hdr) + strlen(data));
	}
	else {

	  // The assumption is that we are in CONVERS mode.
	  // The data sould be terminated by carriage return.

	  int timeout = 600;	// 60 sec.  I've seen it take more than 20.
	  while (timeout > 0 && busy[from]) {
	    SLEEP_MS(100);
	    timeout--;
	  }
	  if (timeout == 0) {
	    printf ("ERROR: Gave up waiting while TNC busy.\n");
	    tnc_disconnect (0,1);
	    SLEEP_MS(5000);
	    printf ("TEST FAILED!\n");
	    exit (EXIT_FAILURE);
	  }
	  else {
	    serial_port_write (serial_fd[from], data, strlen(data));
	  }
	}

} /* end tnc_disconnect */




/* end tnctest.c */

//	******  PRELIMINARY - needs work  ******

#define DEBUG 1


//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//



#include "direwolf.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <getopt.h>

#include "ax25_pad.h"	
#include "textcolor.h"
#include "agwlib.h"			// Network TNC interface.


/*------------------------------------------------------------------
 *
 * Module:      appserver.c
 *
 * Purpose:   	Simple application server for connected mode AX.25.
 *
 *		This demonstrates how you can write a application that will wait for
 *		a connection from another station and respond to commands.
 *		It can be used as a starting point for developing your own applications.
 *		
 * Description:	This attaches to an instance of Dire Wolf via the AGW network interface.
 *		It processes commands from other radio stations and responds.
 *
 *---------------------------------------------------------------*/


static void usage()
{
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Usage: \n");
	      dw_printf (" \n");
	      dw_printf ("appserver  [ -h hostname ] [ -p port ] mycall  \n");
	      dw_printf (" \n");
	      dw_printf ("        -h    hostname for TNC.  Default is localhost. \n");
	      dw_printf (" \n");
 	      dw_printf ("        -p    tcp port for TNC.  Default is 8000. \n");
	      dw_printf (" \n");
	      dw_printf ("        mycall    is required because that is the callsign for  \n");
	      dw_printf ("                  which the TNC will accept connections. \n");
	      dw_printf (" \n");
	      exit (EXIT_FAILURE);
}





static char mycall[AX25_MAX_ADDR_LEN];	/* Callsign, with SSID, for the application. */
					/* Future?  Could have multiple applications, on the same */
					/* radio channel, each with its own SSID. */

static char tnc_hostname[80];		/* DNS host name or IPv4 address. */
					/* Some of the code is there for IPv6 but */
					/* needs more work. */
					/* Defaults to "localhost" if not specified. */

static char tnc_port[8];		/* a TCP port number.  Default 8000.  */



/*
 * Maintain information about connections from users which we will call "sessions."
 * It should be possible to have multiple users connected at the same time.
 *
 * This allows a "who" command to see who is currently connected and a place to keep
 * possible state information for each user.
 *
 * Each combination of channel & callsign is a separate session.
 * The same user (callsign), on a different channel, is a different session.
 */


struct session_s {

	char client_addr[AX25_MAX_ADDR_LEN];	// Callsign of other station.
						// Clear to mean this table entry is not in use.

	int channel;				// Radio channel.

	time_t login_time;			// Time when connection established.

// For the timing test.
// Send specified number of frames, optional length.
// When finished summarize with statistics.

	time_t tt_start_time;
	volatile int tt_count;			// Number to send.
	int tt_length;				// Bytes in info part.
	int tt_next;				// Next sequence to send.

	volatile int tx_queue_len;		// Number in transmit queue.  For flow control.
};

#define MAX_SESSIONS  12

static struct session_s session[MAX_SESSIONS];

static int find_session (int chan, char *addr, int create);
static void poll_timing_test (void);



/*------------------------------------------------------------------
 *
 * Name: 	main
 *
 * Purpose:   	Attach to Dire Wolf TNC, wait for requests from users.
 *
 * Usage:	Described above.
 *
 *---------------------------------------------------------------*/


int main (int argc, char *argv[])
{
	int c;
	char *p;

#if __WIN32__
	setvbuf(stdout, NULL, _IONBF, 0);
#else
 	setlinebuf (stdout);
#endif

	memset (session, 0, sizeof(session));

	strlcpy (tnc_hostname, "localhost", sizeof(tnc_hostname));
	strlcpy (tnc_port, "8000", sizeof(tnc_port));

/*
 * Extract command line args.   
 */

	while ((c = getopt (argc, argv, "h:p:")) != -1) {
	  switch (c) {

	    case 'h':
	      strlcpy (tnc_hostname, optarg, sizeof(tnc_hostname));
	      break;

	    case 'p':
	      strlcpy (tnc_port, optarg, sizeof(tnc_port));
	      break;

      	    default:
	      usage ();
	  }
 	}

	if (argv[optind] == NULL) {
	  usage ();
	}

	strlcpy (mycall, argv[optind], sizeof(mycall));

	// Force to upper case.
	for (p = mycall; *p != '\0'; p++) {
	  if (islower(*p)) {
	    *p = toupper(*p);
	  }
	}

/*
 * Establish a TCP socket to the network TNC.
 * It starts up a thread, which listens for messages from the TNC,
 * and calls the corresponding agw_cb_... callback functions.
 *
 * After attaching to the TNC, the specified init function is called.
 * We pass it to the library, rather than doing it here, so it can
 * repeated automatically if the TNC goes away and comes back again.
 * We need to reestablish what it knows about the application.
 */

	if (agwlib_init (tnc_hostname, tnc_port, agwlib_G_ask_port_information) != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not attach to network TNC %s:%s.\n", tnc_hostname, tnc_port);
	  exit (EXIT_FAILURE);
	}


/*
 * Send command to ask what channels are available.
 * The response will be handled by agw_cb_G_port_information.
 */
// FIXME:  Need to do this again if we lose TNC and reattach to it.

	///   should happen automatically now.   agwlib_G_ask_port_information ();


	while (1) {
	  SLEEP_SEC(1);		// other places based on 1 second assumption.
	  poll_timing_test ();
	}


} /* end main */



static void poll_timing_test (void) 
{
	int s;
	for (s = 0; s < MAX_SESSIONS; s++) {

	  if (session[s].tt_count == 0) {
	     continue;	// nothing to do
	  }
	  else if (session[s].tt_next <= session[s].tt_count) {
	    int rem = session[s].tt_count - session[s].tt_next + 1;  // remaining to send.
	    agwlib_Y_outstanding_frames_for_station (session[s].channel, mycall, session[s].client_addr);
	    SLEEP_MS(10);
	    if (session[s].tx_queue_len > 128) continue;  // enough queued up for now.
	    if (rem > 64) rem = 64;	// add no more than 64 at a time.
	    int i;
	    for (i = 0; i < rem; i++) {
	      char c = 'a';
	      char stuff[AX25_MAX_INFO_LEN+2];
	      snprintf (stuff, sizeof(stuff), "%06d ", session[s].tt_next);
	      int k;
	      for (k = strlen(stuff); k < session[s].tt_length - 1; k++) {
	        stuff[k] = c;
		c++;
		if (c == 'z' + 1) c = 'A';
	        if (c == 'Z' + 1) c = '0';
	        if (c == '9' + 1) c = 'a';
	      }
	      stuff[k++] = '\r';
	      stuff[k++] = '\0';
	      agwlib_D_send_connected_data (session[s].channel, 0xF0, mycall, session[s].client_addr, strlen(stuff), stuff);
	      session[s].tt_next++;
	    }
	  }
	  else {
	    // All done queuing up the packets.
	    // Wait until they have all been sent and ack'ed by other end.

	    agwlib_Y_outstanding_frames_for_station (session[s].channel, mycall, session[s].client_addr);
	    SLEEP_MS(10);

	    if (session[s].tx_queue_len > 0) continue;  // not done yet.

	    int elapsed = time(NULL) - session[s].tt_start_time;
	    if (elapsed <= 0) elapsed = 1;	// avoid divide by 0

	    int byte_count = session[s].tt_count * session[s].tt_length;
	    char summary[100];
	    snprintf (summary, sizeof(summary), "%d bytes in %d seconds, %d bytes/sec, efficiency %d%% at 1200, %d%% at 9600.\r",
					byte_count, elapsed, byte_count/elapsed, 
					byte_count * 8 * 100 / elapsed / 1200,
					byte_count * 8 * 100 / elapsed / 9600);

	    agwlib_D_send_connected_data (session[s].channel, 0xF0, mycall, session[s].client_addr, strlen(summary), summary);
	    session[s].tt_count = 0;	// all done.
	  }
	}

}  // end poll_timing_test



/*-------------------------------------------------------------------
 *
 * Name:        agw_cb_C_connection_received
 *
 * Purpose:     Callback for the "connection received" command from the TNC.
 *		
 * Inputs:	chan		- Radio channel, first is 0.
 *
 *		call_from	- Address of other station.
 *
 *		call_to		- Callsign I responded to.  (could be an alias.)
 *
 *		data_len	- Length of data field.
 *
 *		data		- Should look something like this for incoming:
 *					*** CONNECTED to Station xxx\r	
 *
 * Description:	Add to the sessions table.
 *
 *--------------------------------------------------------------------*/

/*-------------------------------------------------------------------
 *
 * Name:        on_C_connection_received
 *
 * Purpose:     Callback for the "connection received" command from the TNC.
 *		
 * Inputs:	chan		- Radio channel, first is 0.
 *
 *		call_from	- Address of other station.
 *
 *		call_to		- My call.
 *				  In the case of an incoming connect request (i.e. to
 *				  a server) this is the callsign I responded to.
 *				  It is possible to define additional aliases and respond
 *				  to any one of them.  It would be possible to have a server
 *				  that responds to multiple names and behaves differently
 *				  depending on the name.
 *
 *		incoming	- true(1) if other station made connect request.
 *				  false(0) if I made request and other statio accepted.
 *
 *		data		- Should look something like this for incoming:
 *					*** CONNECTED to Station xxx\r
 *				  and this for my request being accepted:
 *					*** CONNECTED With Station xxx\r
 *
 *		session_id	- Session id to be used in data transfer and
 *				  other control functions related to this connection.
 *				  Think of it like a file handle.  Once it is open
 *				  we usually don't care about the name anymore and
 *				  and just refer to the handle.  This is used to
 *				  keep track of multiple connections at the same
 *				  time.  e.g. a server could be handling multiple
 *				  clients at once on the same or different channels.
 *
 * Description:	Add to the table of clients.
 *
 *--------------------------------------------------------------------*/


// old void agw_cb_C_connection_received (int chan, char *call_from, char *call_to, int data_len, char *data)
void on_C_connection_received (int chan, char *call_from, char *call_to, int incoming, char *data)
{
	int s;
	char *p;
	char greeting[256];


	for (p = data; *p != '\0'; p++) {
	  if (! isprint(*p)) *p = '\0';		// Remove any \r character at end.
	}

	s = find_session (chan, call_from, 1);

	if (s >= 0) {

	  text_color_set(DW_COLOR_INFO);
 	  dw_printf ("Begin session %d: %s\n", s, data);

// Send greeting.

	  snprintf (greeting, sizeof(greeting), "Welcome!  Type ? for list of commands or HELP <command> for details.\r");
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	}
	else {

	  text_color_set(DW_COLOR_INFO);
 	  dw_printf ("Too many users already: %s\n", data);

// Sorry, too many users already.

	  snprintf (greeting, sizeof(greeting), "Sorry, maximum number of users has been exceeded.  Try again later.\r");
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);

	  // FIXME: Ideally we'd want to wait until nothing in the outgoing queue
	  // to that station so we know the rejection message was received.
	  SLEEP_SEC (10);
	  agwlib_d_disconnect (chan, mycall, call_from);
	}

} /* end agw_cb_C_connection_received */



/*-------------------------------------------------------------------
 *
 * Name:        agw_cb_d_disconnected
 *
 * Purpose:     Process the "disconnected" command from the TNC.
 *		
 * Inputs:	chan		- Radio channel.
 *
 *		call_from	- Address of other station.
 *
 *		call_to		- Callsign I responded to.  (could be aliases.)
 *
 *		data_len	- Length of data field.
 *
 *		data		- Should look something like one of these:
 *					*** DISCONNECTED RETRYOUT With xxx\r
 *					*** DISCONNECTED From Station xxx\r
 *
 * Description:	Remove from the sessions table.
 *
 *--------------------------------------------------------------------*/



void agw_cb_d_disconnected (int chan, char *call_from, char *call_to, int data_len, char *data)
{
	int s;
	char *p;

	s = find_session (chan, call_from, 0);

	for (p = data; *p != '\0'; p++) {
	  if (! isprint(*p)) *p = '\0';		// Remove any \r character at end.
	}

	text_color_set(DW_COLOR_INFO);
 	dw_printf ("End session %d: %s\n", s, data);

// Remove from session table.

	if (s >= 0) {
	  memset (&(session[s]), 0, sizeof(struct session_s));
	}

} /* end agw_cb_d_disconnected */



/*-------------------------------------------------------------------
 *
 * Name:        agw_cb_D_connected_data
 *
 * Purpose:     Process "connected ax.25 data" from the TNC.
 *		
 * Inputs:	chan		- Radio channel.
 *
 *		addr		- Address of other station.
 *
 *		msg		- What the user sent us.  Probably a command.
 *
 * Global In:	tnc_sock	- Socket for TNC.
 *
 * Description:	Remove from the session table.
 *
 *--------------------------------------------------------------------*/


void agw_cb_D_connected_data (int chan, char *call_from, char *call_to, int data_len, char *data)
{
	int s;
	char *p;
	char logit[AX25_MAX_INFO_LEN+100];
	char *pcmd;
	char *save;

	s = find_session (chan, call_from, 0);

	for (p = data; *p != '\0'; p++) {
	  if (! isprint(*p)) *p = '\0';		// Remove any \r character at end.
	}

	// TODO: Should timestamp to all output.

	snprintf (logit, sizeof(logit), "%d,%d,%s: %s\n", s, chan, call_from, data); 
	text_color_set(DW_COLOR_INFO);
 	dw_printf ("%s", logit);

	if (s < 0) {
	  // Uh oh. Data from some station when not connected.
	  text_color_set(DW_COLOR_ERROR);
 	  dw_printf ("Internal error.  Incoming data, no corresponding session.\n");
	  return;
	}

// Process the command from user.

	pcmd = strtok_r (data, " ", &save);
	if (pcmd == NULL || strlen(pcmd) == 0) {

	  char greeting[80];
	  strlcpy (greeting, "Type ? for list of commands or HELP <command> for details.\r", sizeof(greeting));
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	  return;
	}

	if (strcasecmp(pcmd, "who") == 0) {

// who - list people currently logged in.

	  int n;
	  char greeting[128];

	  snprintf (greeting, sizeof(greeting), "Session Channel User   Since\r");
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);

	  for (n = 0; n < MAX_SESSIONS; n++) {
	    if (session[n].client_addr[0]) {
// I think compiler is confused.  It says up to 520 characters can be written.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	      snprintf (greeting, sizeof(greeting), "  %2d       %d    %-9s [time later]\r", 
			n, session[n].channel, session[n].client_addr);
#pragma GCC diagnostic pop
	      agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	    }
	  }
	}
	else if (strcasecmp(pcmd, "test") == 0) {

// test - timing test
// Send specified number of frames with optional length.

	  char *pcount = strtok_r (NULL, " ", &save);
	  char *plength = strtok_r (NULL, " ", &save);

	  session[s].tt_start_time = time(NULL);
	  session[s].tt_next = 1;
	  session[s].tt_length = 256;
	  session[s].tt_count = 1;

	  if (plength != NULL) {
	    session[s].tt_length = atoi(plength);
	    if (session[s].tt_length < 16) session[s].tt_length = 16;
	    if (session[s].tt_length > AX25_MAX_INFO_LEN) session[s].tt_length = AX25_MAX_INFO_LEN;
	  }
	  if (pcount != NULL) {
	    session[s].tt_count = atoi(pcount);
	  }
	    
	  // The background polling will take it from here. 
	}
	else if (strcasecmp(pcmd, "bye") == 0) {

// bye - disconnect.

	  char greeting[80];
	  strlcpy (greeting, "Thank you folks for kindly droppin' in.  Y'all come on back now, ya hear?\r", sizeof(greeting));
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	  // Ideally we'd want to wait until nothing in the outgoing queue
	  // to that station so we know the message was received.
	  SLEEP_SEC (10);
	  agwlib_d_disconnect (chan, mycall, call_from);
	}
	else if (strcasecmp(pcmd, "help") == 0 || strcasecmp(pcmd, "?") == 0) {

// help.

	  char greeting[80];
	  strlcpy (greeting, "Help not yet available.\r", sizeof(greeting));
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	}
	else {

// command not recognized.

	  char greeting[80];
	  strlcpy (greeting, "Invalid command. Type ? for list of commands or HELP <command> for details.\r", sizeof(greeting));
	  agwlib_D_send_connected_data (chan, 0xF0, mycall, call_from, strlen(greeting), greeting);
	}

} /* end agw_cb_D_connected_data */




/*-------------------------------------------------------------------
 *
 * Name:        agw_cb_G_port_information
 *
 * Purpose:     Process the port information "radio channels available" response from the TNC.
 *		
 *
 * Inputs:	num_chan_avail		- Number of radio channels available.
 *
 *		chan_descriptions	- Array of string pointers to form "Port99 description".
 *					  Port1 is channel 0.
 *
 *--------------------------------------------------------------------*/

void agw_cb_G_port_information (int num_chan_avail, char *chan_descriptions[])
{
	char *p;
	int n;

	text_color_set(DW_COLOR_INFO);
 	dw_printf("TNC has %d radio channel%s available:\n", num_chan_avail, (num_chan_avail != 1) ? "s" : "");
	
	for (n = 0; n < num_chan_avail; n++) {

	  p = chan_descriptions[n];

	    // Expecting something like this:  "Port1 first soundcard mono"

	    if (strncasecmp(p, "Port", 4) == 0 && isdigit(p[4])) {
	    
	      int chan = atoi(p+4) - 1;	// "Port1" is our channel 0.
	      if (chan >= 0 && chan < MAX_CHANS) {

	        char *desc = p + 4;
	        while (*desc != '\0' && (*desc == ' ' || isdigit(*desc))) {
	          desc++;
	        }

	        text_color_set(DW_COLOR_INFO);
 	        dw_printf("  Channel %d: %s\n", chan, desc);

		// Later? Use 'g' to get speed and maybe other properties?
	        // Though I'm not sure why we would care here.

/*
 * Send command to register my callsign for incoming connect requests.
 */

	        agwlib_X_register_callsign (chan, mycall);

	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
 	        dw_printf("Radio channel number is out of bounds: %s\n", p);
	      }
	    }
	    else {
	      text_color_set(DW_COLOR_ERROR);
 	      dw_printf("Radio channel description not in expected format: %s\n", p);
	    }
	}

} /* end agw_cb_G_port_information */


/*-------------------------------------------------------------------
 *
 * Name:        agw_cb_Y_outstanding_frames_for_station
 *
 * Purpose:     Process the "disconnected" command from the TNC.
 *		
 * Inputs:	chan		- Radio channel.
 *
 *		call_from	- Should be my call.
 *
 *		call_to		- Callsign of other station.
 *
 *		frame_count
 *
 * Description:	Remove from the sessions table.
 *
 *--------------------------------------------------------------------*/



void agw_cb_Y_outstanding_frames_for_station (int chan, char *call_from, char *call_to, int frame_count)
{
	int s;

	s = find_session (chan, call_to, 0);

	text_color_set(DW_COLOR_DEBUG);		// FIXME temporary
 	dw_printf ("debug ----------------------> session %d, callback Y outstanding frame_count %d\n", s, frame_count);

// Update the transmit queue length

	if (s >= 0) {
	  session[s].tx_queue_len  = frame_count;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
 	  dw_printf ("Oops!  Did not expect to be here.\n");
	}

} /* end agw_cb_Y_outstanding_frames_for_station */



/*-------------------------------------------------------------------
 *
 * Name:        find_session
 *
 * Purpose:     Given a channel number and address (callsign), find existing
 *		table entry or create a new one.
 *
 * Inputs:	chan	- Radio channel number.
 *
 *		addr	- Address of station contacting us.
 *
 *		create	- If true, try create a new entry if not already there.
 *
 * Returns:	"session id" which is an index into "session" array or -1 for failure.
 *
 *--------------------------------------------------------------------*/

static int find_session (int chan, char *addr, int create)
{
 	int i;
	int s = -1;

// Is it there already?


//#if DEBUG
//
//	text_color_set(DW_COLOR_DEBUG);
// 	dw_printf("find_session (%d, %s, %d)\n", chan, addr, create);
//#endif

	for (i = 0; i < MAX_SESSIONS; i++) {
	  if (session[i].channel == chan && strcmp(session[i].client_addr, addr) == 0) {
	    s = i;
	    break;
	  }
	}

	if (s >= 0) return (s);

	if (! create) return (-1);

// No, and there is a request to add a new entry.
// See if we have any available space.

	s = -1;
	for (i = 0; i < MAX_SESSIONS; i++) {
	  if (strlen(session[i].client_addr) == 0) {
	    s = i;
	    break;
	  }
	}

	if (s < 0) return (-1);

	strlcpy (session[s].client_addr, addr, sizeof(session[s].client_addr));
	session[s].channel = chan;
	session[s].login_time = time(NULL);

	return (s);

} /* end find_session */


/* end appserver.c */

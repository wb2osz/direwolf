
//	****** PRELIMINARY - needs work ******

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//


/*------------------------------------------------------------------
 *
 * Module:      agwlib.c
 *
 * Purpose:   	Sample application Program Interface (API) to use network TNC with AGW protocol.
 *		
 * Input:	
 *
 * Outputs:	  
 *
 * Description:	This file contains functions to attach to a TNC over a TCP socket and send
 *		commands to it.   The current list includes some of the following:
 *
 *			'C'	Connect, Start an AX.25 Connection			
 *			'v'	Connect VIA, Start an AX.25 circuit thru digipeaters	
 *			'c'	Connection with non-standard PID			
 *			'D'	Send Connected Data					
 *			'd'	Disconnect, Terminate an AX.25 Connection		
 *			'X'	Register CallSign 		
 *			'x'	Unregister CallSign 		
 *			'R'	Request for version number.
 *			'G'	Ask about radio ports.
 *			'g'	Capabilities of a port.  
 *			'k'	Ask to start receiving RAW AX25 frames.
 *			'm'	Ask to start receiving Monitor AX25 frames.
 *			'V'	Transmit UI data frame.
 *			'H'	Report recently heard stations.  Not implemented yet in direwolf.
 *			'K'	Transmit raw AX.25 frame.		
 *			'y'	Ask Outstanding frames waiting on a Port   
 *			'Y'	How many frames waiting for transmit for a particular station 
 *
 *		
 *		The user supplied application must supply functions to handle or ignore
 *		messages that come from the TNC.  Common examples:
 *
 *			'C'	AX.25 Connection Received		
 *			'D'	Connected AX.25 Data			
 *			'd'	Disconnected				
 *			'R'	Reply to Request for version number.
 *			'G'	Reply to Ask about radio ports.
 *			'g'	Reply to capabilities of a port.  
 *			'K'	Received AX.25 frame in raw format.  (Enabled with 'k' command.)
 *			'U'	Received AX.25 frame in monitor format.  (Enabled with 'm' command.)
 *			'y'	Outstanding frames waiting on a Port   
 *			'Y'	How many frames waiting for transmit for a particular station 
 *			'C'	AX.25 Connection Received		
 *			'D'	Connected AX.25 Data			
 *			'd'	Disconnected				
 *
 *
 *
 * References:	AGWPE TCP/IP API Tutorial
 *		http://uz7ho.org.ua/includes/agwpeapi.htm
 *
 * Usage:	See  appclient.c and appserver.c  for examples of how to use this.
 *
 *---------------------------------------------------------------*/


#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this
#else 
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "textcolor.h"
#include "dwsock.h"		// socket helper functions.
#include "ax25_pad.h"		// forAX25_MAX_PACKET_LEN
#include "agwlib.h"




/*
 * Message header for AGW protocol.
 * Multibyte numeric values require rearranging for big endian cpu.
 */

/*
 * With MinGW version 4.6, obviously x86.
 * or Linux gcc version 4.9, Linux ARM.
 *
 *	$ gcc -E -dM - < /dev/null | grep END
 *	#define __ORDER_LITTLE_ENDIAN__ 1234
 *	#define __FLOAT_WORD_ORDER__ __ORDER_LITTLE_ENDIAN__
 *	#define __ORDER_PDP_ENDIAN__ 3412
 *	#define __ORDER_BIG_ENDIAN__ 4321
 *	#define __BYTE_ORDER__ __ORDER_LITTLE_ENDIAN__
 *
 * This is for standard OpenWRT on MIPS.
 *
 *	#define __ORDER_LITTLE_ENDIAN__ 1234
 *	#define __FLOAT_WORD_ORDER__ __ORDER_BIG_ENDIAN__
 *	#define __ORDER_PDP_ENDIAN__ 3412
 *	#define __ORDER_BIG_ENDIAN__ 4321
 *	#define __BYTE_ORDER__ __ORDER_BIG_ENDIAN__
 *
 * This was reported for an old Mac with PowerPC processor.
 * (Newer versions have x86.)
 *
 *	$ gcc -E -dM - < /dev/null | grep END
 *	#define __BIG_ENDIAN__ 1
 *	#define _BIG_ENDIAN 1
 */


#if defined(__BIG_ENDIAN__) || (defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)

// gcc >= 4.2 has __builtin_swap32() but might not be compatible with older versions or other compilers.

#define host2netle(x) ( (((x)>>24)&0x000000ff) | (((x)>>8)&0x0000ff00) | (((x)<<8)&0x00ff0000) | (((x)<<24)&0xff000000) )
#define netle2host(x) ( (((x)>>24)&0x000000ff) | (((x)>>8)&0x0000ff00) | (((x)<<8)&0x00ff0000) | (((x)<<24)&0xff000000) )

#else

#define host2netle(x) (x)
#define netle2host(x) (x)

#endif


struct agw_hdr_s {		/* Command header. */

  unsigned char portx;		/* 0 for first, 1 for second, etc. */
				/* Dire Wolf uses the term "channel" to avoid confusion with TCP ports */
				/* or other places port might be used. */
  unsigned char reserved1;
  unsigned char reserved2;
  unsigned char reserved3;

  unsigned char datakind;	/* Message type, usually written as a letter. */
  unsigned char reserved4;
  unsigned char pid;
  unsigned char reserved5;

  char call_from[10];

  char call_to[10];

  int data_len_NETLE;		/* Number of data bytes following. */
				/* _NETLE suffix is reminder to convert for network byte order. */

  int user_reserved_NETLE;
};


struct agw_cmd_s {		/* Complete command with header and data. */

  struct agw_hdr_s hdr;			/* Command header. */
  char data[AX25_MAX_PACKET_LEN];	/* Possible variable length data. */
};



/*-------------------------------------------------------------------
 *
 * Name:        agwlib_init
 *
 * Purpose:     Attach to TNC over TCP.
 *
 * Inputs:	host	- Host name or address.  Often "localhost".
 *
 *		port	- TCP port number as text.  Usually "8000".
 *
 *		init_func - Call this function after establishing communication
 *			with the TNC.  We put it here, so that it can be done
 *			again automatically if the TNC disappears and we
 *			reattach to it.
 *			It must return 0 for success.
 *			Can be NULL if not needed.
 *
 * Returns:	0 for success, -1 for failure.
 *
 * Description:	This starts up a thread which listens to the socket and
 *		dispatches the messages to the corresponding callback functions.
 *		It will also attempt to re-establish communication with the
 *		TNC if it goes away.
 *
 *--------------------------------------------------------------------*/


static char s_tnc_host[80];
static char s_tnc_port[8];
static int  s_tnc_sock;			// Socket handle or file descriptor.
static int (*s_tnc_init_func)(void);	// Call after establishing socket.


// TODO: define macros somewhere to hide platform specifics.

#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

#if __WIN32__
static HANDLE tnc_listen_th;
static THREAD_F tnc_listen_thread (void *arg);
#else
static pthread_t tnc_listen_tid;
static THREAD_F tnc_listen_thread (void *arg);	
#endif


int agwlib_init (char *host, char *port, int (*init_func)(void))
{
	char tncaddr[DWSOCK_IPADDR_LEN];
	int e;

	strlcpy (s_tnc_host, host, sizeof(s_tnc_host));
	strlcpy (s_tnc_port, port, sizeof(s_tnc_port));
	s_tnc_sock = -1;
	s_tnc_init_func = init_func;

	dwsock_init();

	s_tnc_sock = dwsock_connect (host, port, "TNC", 0, 0, tncaddr);

	if (s_tnc_sock == -1) {
	  return (-1);
	}


/*
 * Incoming messages are dispatched to application-supplied callback functions.
 * If the TNC disappears, try to reestablish communication.
 */


#if __WIN32__
	tnc_listen_th = (HANDLE)_beginthreadex (NULL, 0, tnc_listen_thread, (void *)NULL, 0, NULL);
	if (tnc_listen_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: Could not create TNC listening thread\n");
	  return (-1);
	}
#else
	e = pthread_create (&tnc_listen_tid, NULL, tnc_listen_thread, (void *)NULL);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Internal error: Could not create TNC listening thread");
	  return (-1);
	}
#endif

// TNC initialization if specified.

	if (s_tnc_init_func != NULL) {
	  e = (*s_tnc_init_func)();
	  return (e);
	}

	return (0);
}


/*-------------------------------------------------------------------
 *
 * Name:        tnc_listen_thread
 *
 * Purpose:     Listen for anything from TNC and process it.
 *		Reconnect if something goes wrong and we got disconnected.
 *
 * Inputs:	s_tnc_host
 *		s_tnc_port
 *
 * Outputs:	s_tnc_sock	- File descriptor for communicating with TNC.
 *				  Will be -1 if not connected.
 *
 *--------------------------------------------------------------------*/

static void process_from_tnc (struct agw_cmd_s *cmd);


#if __WIN32__
static unsigned __stdcall tnc_listen_thread (void *arg)
#else
static void * tnc_listen_thread (void *arg)	
#endif	
{
	char tncaddr[DWSOCK_IPADDR_LEN];

	struct agw_cmd_s cmd;

	while (1) {

/*
 * Connect to TNC if not currently connected.
 */

	  if (s_tnc_sock == -1) {

	    text_color_set(DW_COLOR_ERROR);
	    // I'm using the term "attach" here, in an attempt to
	    // avoid confusion with the AX.25 connect.
	    dw_printf ("Attempting to reattach to network TNC...\n");

	    s_tnc_sock = dwsock_connect (s_tnc_host, s_tnc_port, "TNC", 0, 0, tncaddr);

	    if (s_tnc_sock != -1) {
	      dw_printf ("Successfully reattached to network TNC.\n");

	      // Might need to run TNC initialization again.
	      // For example, a server would register its callsigns.

	      if (s_tnc_init_func != NULL) {
	        int e = (*s_tnc_init_func)();
	        (void) e;
	      }

	    }
	    SLEEP_SEC(5);
	  }
	  else {
	    int n = SOCK_RECV (s_tnc_sock, (char *)(&cmd.hdr), sizeof(cmd.hdr));

	    if (n == -1) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Lost communication with network TNC. Will try to reattach.\n");
	      dwsock_close (s_tnc_sock);
	      s_tnc_sock = -1;
	      continue;
	    }
	    else if (n != sizeof(cmd.hdr)) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Error reading message header from network TNC.\n");
	      dw_printf ("Tried to read %d bytes but got only %d.\n", (int)sizeof(cmd.hdr), n);
	      dw_printf ("Closing socket to TNC.   Will try to reattach.\n");
	      dwsock_close (s_tnc_sock);
	      s_tnc_sock = -1;
	      continue;
	    }

/*
 * Take some precautions to guard against bad data which could cause problems later.
 */
	    if (cmd.hdr.portx < 0 || cmd.hdr.portx >= MAX_CHANS) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid channel number, %d, in command '%c', from network TNC.\n",
			cmd.hdr.portx, cmd.hdr.datakind);
	      cmd.hdr.portx = 0;	// avoid subscript out of bounds, try to keep going.
	    }

/*
 * Call to/from fields are 10 bytes but contents must not exceed 9 characters.
 * It's not guaranteed that unused bytes will contain 0 so we
 * don't issue error message in this case. 
 */
	    cmd.hdr.call_from[sizeof(cmd.hdr.call_from)-1] = '\0';
	    cmd.hdr.call_to[sizeof(cmd.hdr.call_to)-1] = '\0';

/*
 * Following data must fit in available buffer.
 * Leave room for an extra nul byte terminator at end later.
 */

	    int data_len = netle2host(cmd.hdr.data_len_NETLE);

	    if (data_len < 0 || data_len > (int)(sizeof(cmd.data) - 1)) {

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("Invalid message from network TNC.\n");
	      dw_printf ("Data Length of %d is out of range.\n", data_len);
	
	      /* This is a bad situation. */
	      /* If we tried to read again, the header probably won't be there. */
	      /* No point in trying to continue reading.  */

	      dw_printf ("Closing connection to TNC.\n");
	      dwsock_close (s_tnc_sock);
	      s_tnc_sock = -1;
	      continue;
	    }

	    cmd.data[0] = '\0';

	    if (data_len > 0) {
	      n = SOCK_RECV (s_tnc_sock, cmd.data, data_len);
	      if (n != data_len) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("Error getting message data from network TNC.\n");
	        dw_printf ("Tried to read %d bytes but got only %d.\n", data_len, n);
	        dw_printf ("Closing socket to network TNC.\n\n");
	        dwsock_close (s_tnc_sock);
	        s_tnc_sock = -1;
	        continue;
	      }
	      if (n >= 0) {
		cmd.data[n] = '\0';	// Terminate so it can be used as a C string.
	      }

	      process_from_tnc (&cmd);

	    } // additional data after command header
	  } // s_tnc_sock != -1
	} // while (1)

	return (0);	// unreachable but shutup warning.

} // end tnc_listen_thread


/*
 * The user supplied application must supply functions to handle or ignore
 * messages that come from the TNC.
 */

static void process_from_tnc (struct agw_cmd_s *cmd)
{
	int data_len = netle2host(cmd->hdr.data_len_NETLE);
	//int session;


	switch (cmd->hdr.datakind) {

	  case 'C':						// AX.25 Connection Received
	    {	    
	      //agw_cb_C_connection_received (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, data_len, cmd->data);
	      // TODO:  compute session id
	      // There are two different cases to consider here.
	      if (strncmp(cmd->data, "*** CONNECTED To Station", 24) == 0) {
	        // Incoming: Other station initiated the connect request.
	        on_C_connection_received (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, 1, cmd->data);
	      }
	      else if (strncmp(cmd->data, "*** CONNECTED With Station", 26) == 0) {
	        // Outgoing: Other station accepted my connect request.
	        on_C_connection_received (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, 0, cmd->data);
	      }
	      else {
// TBD
	      }
	    }
	    break;

	  case 'D':						// Connected AX.25 Data	
	    // FIXME: should probably add pid here.		
	    agw_cb_D_connected_data (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, data_len, cmd->data);
	    break;

	  case 'd':						// Disconnected				
	    agw_cb_d_disconnected (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, data_len, cmd->data);
	    break;

 	  case 'R':						// Reply to Request for version number.
	    break;

	  case 'G':						// Port Information.
	    // Data part should be fields separated by semicolon.
	    // First field is number of ports (we call them channels).
	    // Other fields are of the form "Port99 comment" where first is number 1.
	    {
	    int num_chan = 1;		// FIXME: FIXME: actually parse it.
	    char *chans[20];
	    chans[0] = "Port1 blah blah";
	    chans[1] = "Port2 blah blah";
	    agw_cb_G_port_information (num_chan, chans);
	    }
	    break;

// TODO: Maybe fill in more someday.

	  case 'g':						// Reply to capabilities of a port.  
	    break;
	  case 'K':						// Received AX.25 frame in raw format. (Enabled with 'k' command.)
	    break;
	  case 'U':						// Received AX.25 frame in monitor format. (Enabled with 'm' command.)
	    break;
	  case 'y':						// Outstanding frames waiting on a Port   
	    break;

	  case 'Y':						// How many frames waiting for transmit for a particular station 
	    {
	    int *p = (int*)(cmd->data);
	    int frame_count = netle2host(*p);
	    agw_cb_Y_outstanding_frames_for_station (cmd->hdr.portx, cmd->hdr.call_from, cmd->hdr.call_to, frame_count);
	    }
	    break;

	  default:
	    break;
	}

} // end process_from_tnc



/*-------------------------------------------------------------------
 *
 * Name:        agwlib_X_register_callsign
 *
 * Purpose:     Tell TNC to accept incoming connect requests to given callsign.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		call_from	- My callsign or alias.
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 *--------------------------------------------------------------------*/

int agwlib_X_register_callsign (int chan, char *call_from)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'X';
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}


/*-------------------------------------------------------------------
 *
 * Name:        agwlib_x_unregister_callsign
 *
 * Purpose:     Tell TNC to stop accepting incoming connect requests to given callsign.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		call_from	- My callsign or alias.
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 * FIXME:	question do we need channel here?
 *
 *--------------------------------------------------------------------*/

int agwlib_x_unregister_callsign (int chan, char *call_from)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'x';
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}


/*-------------------------------------------------------------------
 *
 * Name:        agwlib_G_ask_port_information
 *
 * Purpose:     Tell TNC to stop accepting incoming connect requests to given callsign.
 *
 * Inputs:	call_from	- My callsign or alias.
 *
 * Returns:	0 for success, -1 for error.		TODO: all like this.
 *
 *--------------------------------------------------------------------*/

int agwlib_G_ask_port_information (void)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.datakind = 'G';
	int n = SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE));
	return (n > 0 ? 0 : -1);
}



/*-------------------------------------------------------------------
 *
 * Name:        agwlib_C_connect
 *
 * Purpose:     Tell TNC to start sequence for connecting to remote station.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		call_from	- My callsign.
 *
 *		call_to		- Callsign (or alias) of remote station.
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 * Description:	This only starts the sequence and does not wait.
 *		Success or failure will be indicated sometime later by ?
 *
 *--------------------------------------------------------------------*/

int agwlib_C_connect (int chan, char *call_from, char *call_to)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'C';
	cmd.hdr.pid = 0xF0;	// Shouldn't matter because this appears
				// only in Information frame, not connect sequence.
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	strlcpy (cmd.hdr.call_to, call_to, sizeof(cmd.hdr.call_to));
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}



/*-------------------------------------------------------------------
 *
 * Name:        agwlib_d_disconnect
 *
 * Purpose:     Tell TNC to disconnect from remote station.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		call_from	- My callsign.
 *
 *		call_to		- Callsign (or alias) of remote station.
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 * Description:	This only starts the sequence and does not wait.
 *		Success or failure will be indicated sometime later by ?
 *
 *--------------------------------------------------------------------*/

int agwlib_d_disconnect (int chan, char *call_from, char *call_to)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'd';
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	strlcpy (cmd.hdr.call_to, call_to, sizeof(cmd.hdr.call_to));
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}



/*-------------------------------------------------------------------
 *
 * Name:        agwlib_D_send_connected_data
 *
 * Purpose:     Send connected data to remote station.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		pid		- Protocol ID.  Normally 0xFo for Ax.25.
 *
 *		call_from	- My callsign.
 *
 *		call_to		- Callsign (or alias) of remote station.
 *	
 *		data_len	- Number of bytes for Information part.
 *
 *		data		- Content for Information part.
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 * Description:	This should only be done when we are known to have
 *		an established link to other station.
 *
 *--------------------------------------------------------------------*/

int agwlib_D_send_connected_data (int chan, int pid, char *call_from, char *call_to, int data_len, char *data)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'D';
	cmd.hdr.pid = pid;		// Normally 0xF0 but other special cases are possible.
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	strlcpy (cmd.hdr.call_to, call_to, sizeof(cmd.hdr.call_to));
	cmd.hdr.data_len_NETLE = host2netle(data_len);

// FIXME: DANGER possible buffer overflow, Need checking.

	assert (data_len <= sizeof(cmd.data));

	memcpy (cmd.data, data, data_len);
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}


/*-------------------------------------------------------------------
 *
 * Name:        agwlib_Y_outstanding_frames_for_station
 *
 * Purpose:     Ask how many frames remain to be sent to station on other end of link.
 *
 * Inputs:	chan		- Radio channel number, first is 0.
 *
 *		call_from	- My call [ or is it Station which initiated the link?  (sent SABM/SABME) ]
 *
 *		call_to		- Remote station call [ or is it Station which accepted the link? ]
 *
 * Returns:	Number of bytes sent for success, -1 for error.
 *
 * Description:	We expect to get a 'Y' frame response shortly.
 *
 *		This would be useful for a couple different purposes.
 *
 *		When sending bulk data, we want to keep a fair amount queued up to take
 *		advantage of large window sizes (MAXFRAME, EMAXFRAME).  On the other
 *		hand we don't want to get TOO far ahead when transferring a large file.
 *
 *		Before disconnecting from another station, it would be good to know
 *		that it actually received the last message we sent.  For this reason,
 *		I think it would be good for this to include frames that were 
 *		transmitted but not yet acknowledged.  (Even if it was transmitted once,
 *		it could still be transmitted again, if lost, so you could say it is
 *		still waiting for transmission.)
 *
 *		See server.c for a more precise definition of exactly how this is defined.
 *
 *--------------------------------------------------------------------*/

int agwlib_Y_outstanding_frames_for_station (int chan, char *call_from, char *call_to)
{
	struct agw_cmd_s cmd;

	memset (&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.portx = chan;
	cmd.hdr.datakind = 'Y';
	strlcpy (cmd.hdr.call_from, call_from, sizeof(cmd.hdr.call_from));
	strlcpy (cmd.hdr.call_to, call_to, sizeof(cmd.hdr.call_to));
	return (SOCK_SEND(s_tnc_sock, (char*)(&cmd), sizeof(cmd.hdr) + netle2host(cmd.hdr.data_len_NETLE)));
}



/* end agwlib.c */

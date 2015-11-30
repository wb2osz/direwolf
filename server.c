//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011, 2012, 2013, 2014, 2015  John Langner, WB2OSZ
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
 * Module:      server.c
 *
 * Purpose:   	Provide service to other applications via "AGW TCPIP Socket Interface".
 *		
 * Input:	
 *
 * Outputs:	  
 *
 * Description:	This provides a TCP socket for communication with a client application.
 *		It implements a subset of the AGW socket interface.
 *
 *		Commands from application recognized:
 *
 *			'R'	Request for version number.
 *				(See below for response.)
 *
 *			'G'	Ask about radio ports.
 *				(See below for response.)
 *
 *			'g'	Capabilities of a port.  (new in 0.8)
 *				(See below for response.)
 *
 *			'k'	Ask to start receiving RAW AX25 frames.
 *
 *			'm'	Ask to start receiving Monitor AX25 frames.
 *
 *			'V'	Transmit UI data frame.
 *				Generate audio for transmission.
 *
 *			'H'	Report recently heard stations.  Not implemented yet.
 *
 *			'K'	Transmit raw AX.25 frame.
 *		
 *			'X'	Register CallSign 
 *		
 *			'x'	Unregister CallSign 
 *		
 *			'y'	Ask Outstanding frames waiting on a Port   (new in 1.2)
 *		
 *			A message is printed if any others are received.
 *
 *			TODO: Should others be implemented?
 *				
 *
 *		Messages sent to client application:
 *
 *			'R'	Reply to Request for version number.
 *				Currently responds with major 1, minor 0.
 *
 *			'G'	Reply to Ask about radio ports.
 *
 *			'g'	Reply to capabilities of a port.  (new in 0.8)
 *
 *			'K'	Received AX.25 frame in raw format.
 *				(Enabled with 'k' command.)
 *
 *			'U'	Received AX.25 frame in monitor format.
 *				(Enabled with 'm' command.)
 *
 *			'y'	Outstanding frames waiting on a Port   (new in 1.2)
 *		
 *
 *
 * References:	AGWPE TCP/IP API Tutorial
 *		http://uz7ho.org.ua/includes/agwpeapi.htm
 *
 * 		Getting Started with Winsock
 *		http://msdn.microsoft.com/en-us/library/windows/desktop/bb530742(v=vs.85).aspx
 *
 *
 * Major change in 1.1:
 *
 *		Formerly a single client was allowed.
 *		Now we can have multiple concurrent clients.
 *
 *---------------------------------------------------------------*/


/*
 * Native Windows:	Use the Winsock interface.
 * Linux:		Use the BSD socket interface.
 * Cygwin:		Can use either one.
 */


#if __WIN32__
#include <winsock2.h>
#define _WIN32_WINNT 0x0501
#include <ws2tcpip.h>
#else 
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef __OpenBSD__
#include <errno.h>
#else
#include <sys/errno.h>
#endif
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "direwolf.h"
#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "server.h"



/*
 * Previously, we allowed only one network connection at a time to each port.
 * In version 1.1, we allow multiple concurrent client apps to connect.
 */

#define MAX_NET_CLIENTS 3

static int client_sock[MAX_NET_CLIENTS];	
					/* File descriptor for socket for */
					/* communication with client application. */
					/* Set to -1 if not connected. */
					/* (Don't use SOCKET type because it is unsigned.) */

static int enable_send_raw_to_client[MAX_NET_CLIENTS];
					/* Should we send received packets to client app in raw form? */
					/* Note that it starts as false for a new connection. */
					/* the client app must send a command to enable this. */

static int enable_send_monitor_to_client[MAX_NET_CLIENTS];
					/* Should we send received packets to client app in monitor form? */
					/* Note that it starts as false for a new connection. */
					/* the client app must send a command to enable this. */




// TODO:  define in one place, use everywhere.
// TODO:  Macro to terminate thread when no point to go on.

#if __WIN32__
#define THREAD_F unsigned __stdcall
#else 
#define THREAD_F void *
#endif

static THREAD_F connect_listen_thread (void *arg);
static THREAD_F cmd_listen_thread (void *arg);

/*
 * Message header for AGW protocol.
 * Assuming little endian such as x86 or ARM.
 * Byte swapping would be required for big endian cpu.
 */

#if __GNUC__
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This needs to be more portable to work on big endian.
#endif
#endif

struct agwpe_s {	
  short portx;			/* 0 for first, 1 for second, etc. */
  short port_hi_reserved;	
  short kind_lo;		/* message type */
  short kind_hi;
  char call_from[10];
  char call_to[10];
  int data_len;			/* Number of data bytes following. */
  int user_reserved;
};


/*-------------------------------------------------------------------
 *
 * Name:        debug_print 
 *
 * Purpose:     Print message to/from client for debugging.
 *
 * Inputs:	fromto		- Direction of message.
 *		client		- client number, 0 .. MAX_NET_CLIENTS-1
 *		pmsg		- Address of the message block.
 *		msg_len		- Length of the message.
 *
 *--------------------------------------------------------------------*/

static int debug_client = 0;		/* Debug option: Print information flowing from and to client. */

void server_set_debug (int n) 
{	
	debug_client = n;
}

void hex_dump (unsigned char *p, int len) 
{
	int n, i, offset;

	offset = 0;
	while (len > 0) {
	  n = len < 16 ? len : 16; 
	  dw_printf ("  %03x: ", offset);
	  for (i=0; i<n; i++) {
	    dw_printf (" %02x", p[i]);
	  }
	  for (i=n; i<16; i++) {
	    dw_printf ("   ");
	  }
	  dw_printf ("  ");
	  for (i=0; i<n; i++) {
	    dw_printf ("%c", isprint(p[i]) ? p[i] : '.');
	  }
	  dw_printf ("\n");
	  p += 16;
	  offset += 16;
	  len -= 16;
	}
}

typedef enum fromto_e { FROM_CLIENT=0, TO_CLIENT=1 } fromto_t;

static void debug_print (fromto_t fromto, int client, struct agwpe_s *pmsg, int msg_len)
{
	char direction [10];
	char datakind[80];
	const char *prefix [2] = { "<<<", ">>>" };

	switch (fromto) {

	  case FROM_CLIENT:
	    strlcpy (direction, "from", sizeof(direction));		/* from the client application */

	    switch (pmsg->kind_lo) {
	      case 'P': strlcpy (datakind, "Application Login",				sizeof(datakind)); break;
	      case 'X': strlcpy (datakind, "Register CallSign",				sizeof(datakind)); break;
	      case 'x': strlcpy (datakind, "Unregister CallSign",			sizeof(datakind)); break;
	      case 'G': strlcpy (datakind, "Ask Port Information",			sizeof(datakind)); break;
	      case 'm': strlcpy (datakind, "Enable Reception of Monitoring Frames",	sizeof(datakind)); break;
	      case 'R': strlcpy (datakind, "AGWPE Version Info",			sizeof(datakind)); break;
	      case 'g': strlcpy (datakind, "Ask Port Capabilities",			sizeof(datakind)); break;
	      case 'H': strlcpy (datakind, "Callsign Heard on a Port",			sizeof(datakind)); break;
	      case 'y': strlcpy (datakind, "Ask Outstanding frames waiting on a Port",	sizeof(datakind)); break;
	      case 'Y': strlcpy (datakind, "Ask Outstanding frames waiting for a connection", sizeof(datakind)); break;
	      case 'M': strlcpy (datakind, "Send UNPROTO Information",			sizeof(datakind)); break;
	      case 'C': strlcpy (datakind, "Connect, Start an AX.25 Connection",	sizeof(datakind)); break;
	      case 'D': strlcpy (datakind, "Send Connected Data",			sizeof(datakind)); break;
	      case 'd': strlcpy (datakind, "Disconnect, Terminate an AX.25 Connection",	sizeof(datakind)); break;
	      case 'v': strlcpy (datakind, "Connect VIA, Start an AX.25 circuit thru digipeaters", sizeof(datakind)); break;
	      case 'V': strlcpy (datakind, "Send UNPROTO VIA",				sizeof(datakind)); break;
	      case 'c': strlcpy (datakind, "Non-Standard Connections, Connection with PID", sizeof(datakind)); break;
	      case 'K': strlcpy (datakind, "Send data in raw AX.25 format",		sizeof(datakind)); break;
	      case 'k': strlcpy (datakind, "Activate reception of Frames in raw format", sizeof(datakind)); break;
	      default:  strlcpy (datakind, "**INVALID**",				sizeof(datakind)); break;
	    }
	    break;

	  case TO_CLIENT:
	  default:
	    strlcpy (direction, "to", sizeof(direction));	/* sent to the client application. */

	    switch (pmsg->kind_lo) {
	      case 'R': strlcpy (datakind, "Version Number",				sizeof(datakind)); break;
	      case 'X': strlcpy (datakind, "Callsign Registration",			sizeof(datakind)); break;
	      case 'G': strlcpy (datakind, "Port Information",				sizeof(datakind)); break;
	      case 'g': strlcpy (datakind, "Capabilities of a Port",			sizeof(datakind)); break;
	      case 'y': strlcpy (datakind, "Frames Outstanding on a Port",		sizeof(datakind)); break;
	      case 'Y': strlcpy (datakind, "Frames Outstanding on a Connection",	sizeof(datakind)); break;
	      case 'H': strlcpy (datakind, "Heard Stations on a Port",			sizeof(datakind)); break;
	      case 'C': strlcpy (datakind, "AX.25 Connection Received",			sizeof(datakind)); break;
	      case 'D': strlcpy (datakind, "Connected AX.25 Data",			sizeof(datakind)); break;
	      case 'M': strlcpy (datakind, "Monitored Connected Information",		sizeof(datakind)); break;
	      case 'S': strlcpy (datakind, "Monitored Supervisory Information",		sizeof(datakind)); break;
	      case 'U': strlcpy (datakind, "Monitored Unproto Information",		sizeof(datakind)); break;
	      case 'T': strlcpy (datakind, "Monitoring Own Information",		sizeof(datakind)); break;
	      case 'K': strlcpy (datakind, "Monitored Information in Raw Format",	sizeof(datakind)); break;
	      default:  strlcpy (datakind, "**INVALID**",				sizeof(datakind)); break;
	    }
	}

	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("\n");

	dw_printf ("%s %s %s AGWPE client application %d, total length = %d\n",
			prefix[(int)fromto], datakind, direction, client, msg_len);

	dw_printf ("\tportx = %d, port_hi_reserved = %d\n", pmsg->portx, pmsg->port_hi_reserved);
	dw_printf ("\tkind_lo = %d = '%c', kind_hi = %d\n", pmsg->kind_lo, pmsg->kind_lo, pmsg->kind_hi);
	dw_printf ("\tcall_from = \"%s\", call_to = \"%s\"\n", pmsg->call_from, pmsg->call_to);
	dw_printf ("\tdata_len = %d, user_reserved = %d, data =\n", pmsg->data_len, pmsg->user_reserved);

	hex_dump ((unsigned char*)pmsg + sizeof(struct agwpe_s), pmsg->data_len);

	if (msg_len < 36) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("AGWPE message length, %d, is shorter than minumum 36.\n", msg_len);
	}
	if (msg_len != pmsg->data_len + 36) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("AGWPE message length, %d, inconsistent with data length %d.\n", msg_len, pmsg->data_len);
	}

}

/*-------------------------------------------------------------------
 *
 * Name:        server_init
 *
 * Purpose:     Set up a server to listen for connection requests from
 *		an application such as Xastir.
 *
 * Inputs:	mc->agwpe_port	- TCP port for server.
 *				  Main program has default of 8000 but allows
 *				  an alternative to be specified on the command line
 *
 *				0 means disable.  New in version 1.2.
 *
 * Outputs:	
 *
 * Description:	This starts at least two threads:
 *		  *  one to listen for a connection from client app.
 *		  *  one or more to listen for commands from client app.
 *		so the main application doesn't block while we wait for these.
 *
 *--------------------------------------------------------------------*/

static struct audio_s *save_audio_config_p;


void server_init (struct audio_s *audio_config_p, struct misc_config_s *mc)
{
	int client;

#if __WIN32__
	HANDLE connect_listen_th;
	HANDLE cmd_listen_th[MAX_NET_CLIENTS];
#else
	pthread_t connect_listen_tid;
	pthread_t cmd_listen_tid[MAX_NET_CLIENTS];
	int e;
#endif
	int server_port = mc->agwpe_port;		/* Usually 8000 but can be changed. */


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("server_init ( %d )\n", server_port);
	debug_a = 1;
#endif

	save_audio_config_p = audio_config_p;

	for (client=0; client<MAX_NET_CLIENTS; client++) {
	  client_sock[client] = -1;
	  enable_send_raw_to_client[client] = 0;
	  enable_send_monitor_to_client[client] = 0;
	}

	if (server_port == 0) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Disabled AGW network client port.\n");
	  return;
	}


/*
 * This waits for a client to connect and sets an available client_sock[n].
 */
#if __WIN32__
	connect_listen_th = (HANDLE)_beginthreadex (NULL, 0, connect_listen_thread, (void *)(unsigned int)server_port, 0, NULL);
	if (connect_listen_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create AGW connect listening thread\n");
	  return;
	}
#else
	e = pthread_create (&connect_listen_tid, NULL, connect_listen_thread, (void *)(long)server_port);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create AGW connect listening thread");
	  return;
	}
#endif

/*
 * These read messages from client when client_sock[n] is valid.
 * Currently we start up a separate thread for each potential connection.
 * Possible later refinement.  Start one now, others only as needed.
 */
	for (client = 0; client < MAX_NET_CLIENTS; client++) {

#if __WIN32__
	  cmd_listen_th[client] = (HANDLE)_beginthreadex (NULL, 0, cmd_listen_thread, (void*)client, 0, NULL);
	  if (cmd_listen_th[client] == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create AGW command listening thread\n");
	    return;
	  }
#else
	  e = pthread_create (&cmd_listen_tid[client], NULL, cmd_listen_thread, (void *)(long)client);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Could not create AGW command listening thread");
	    return;
	  }
#endif
	}
}


/*-------------------------------------------------------------------
 *
 * Name:        connect_listen_thread
 *
 * Purpose:     Wait for a connection request from an application.
 *
 * Inputs:	arg		- TCP port for server.
 *				  Main program has default of 8000 but allows
 *				  an alternative to be specified on the command line
 *
 * Outputs:	client_sock	- File descriptor for communicating with client app.
 *
 * Description:	Wait for connection request from client and establish
 *		communication.
 *		Note that the client can go away and come back again and
 *		re-establish communication without restarting this application.
 *
 *--------------------------------------------------------------------*/

static THREAD_F connect_listen_thread (void *arg)
{
#if __WIN32__

	struct addrinfo hints;
	struct addrinfo *ai = NULL;
	int err;
	char server_port_str[12];

	SOCKET listen_sock;  
	WSADATA wsadata;

	snprintf (server_port_str, sizeof(server_port_str), "%d", (int)(long)arg);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("DEBUG: serverport = %d = '%s'\n", (int)(long)arg, server_port_str);
#endif
	err = WSAStartup (MAKEWORD(2,2), &wsadata);
	if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("WSAStartup failed: %d\n", err);
	    return (0);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Could not find a usable version of Winsock.dll\n");
          WSACleanup();
	  //sleep (1);
          return (0);
	}

	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	err = getaddrinfo(NULL, server_port_str, &hints, &ai);
	if (err != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf("getaddrinfo failed: %d\n", err);
	    //sleep (1);
	    WSACleanup();
	    return (0);
	}

	listen_sock= socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (listen_sock == INVALID_SOCKET) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("connect_listen_thread: Socket creation failed, err=%d", WSAGetLastError());
	  return (0);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
    	dw_printf("Binding to port %s ... \n", server_port_str);
#endif

	err = bind( listen_sock, ai->ai_addr, (int)ai->ai_addrlen);
	if (err == SOCKET_ERROR) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", WSAGetLastError());		// TODO: translate number to text?
	  dw_printf("Some other application is probably already using port %s.\n", server_port_str);
	  dw_printf("Try using a different port number with AGWPORT in the configuration file.\n");
          freeaddrinfo(ai);
          closesocket(listen_sock);
          WSACleanup();
          return (0);
        }

	freeaddrinfo(ai);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
 	dw_printf("opened socket as fd (%d) on port (%s) for stream i/o\n", listen_sock, server_port_str );
#endif

 	while (1) {

	  int client;
	  int c;
	  
	  client = -1;
	  for (c = 0; c < MAX_NET_CLIENTS && client < 0; c++) {
	    if (client_sock[c] <= 0) {
	      client = c;
	    }
	  }

/*
 * Listen for connection if we have not reached maximum.
 */
	  if (client >= 0) {

	    if(listen(listen_sock, MAX_NET_CLIENTS) == SOCKET_ERROR)
	    {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Listen failed with error: %d\n", WSAGetLastError());
	      return (0);
	    }
	
	    text_color_set(DW_COLOR_INFO);
            dw_printf("Ready to accept AGW client application %d on port %s ...\n", client, server_port_str);
         
            client_sock[client] = accept(listen_sock, NULL, NULL);

	    if (client_sock[client] == -1) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Accept failed with error: %d\n", WSAGetLastError());
              closesocket(listen_sock);
              WSACleanup();
              return (0);
            }

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("\nConnected to AGW client application %d ...\n\n", client);

/*
 * The command to change this is actually a toggle, not explicit on or off.
 * Make sure it has proper state when we get a new connection.
 */ 
	    enable_send_raw_to_client[client] = 0;
	    enable_send_monitor_to_client[client] = 0;
	  }
	  else {
	    SLEEP_SEC(1);	/* wait then check again if more clients allowed. */
	  }
 	}

#else		/* End of Windows case, now Linux */


    	struct sockaddr_in sockaddr; /* Internet socket address stuct */
    	socklen_t sockaddr_size = sizeof(struct sockaddr_in);
	int server_port = (int)(long)arg;
	int listen_sock;  
	int bcopt = 1;

	listen_sock= socket(AF_INET,SOCK_STREAM,0);
	if (listen_sock == -1) {
	  text_color_set(DW_COLOR_ERROR);
	  perror ("connect_listen_thread: Socket creation failed");
	  return (NULL);
	}

	/* Version 1.3 - as suggested by G8BPQ. */
	/* Without this, if you kill the application then try to run it */
	/* again quickly the port number is unavailable for a while. */
	/* Don't try doing the same thing On Windows; It has a different meaning. */
	/* http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t */

        setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&bcopt, 4);


    	sockaddr.sin_addr.s_addr = INADDR_ANY;
    	sockaddr.sin_port = htons(server_port);
    	sockaddr.sin_family = AF_INET;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
    	dw_printf("Binding to port %d ... \n", server_port);
#endif

        if (bind(listen_sock,(struct sockaddr*)&sockaddr,sizeof(sockaddr))  == -1) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", errno);
          dw_printf("%s\n", strerror(errno));
	  dw_printf("Some other application is probably already using port %d.\n", server_port);
	  dw_printf("Try using a different port number with AGWPORT in the configuration file.\n");
          return (NULL);
	}

	getsockname( listen_sock, (struct sockaddr *)(&sockaddr), &sockaddr_size);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
 	dw_printf("opened socket as fd (%d) on port (%d) for stream i/o\n", listen_sock, ntohs(sockaddr.sin_port) );
#endif

 	while (1) {

	  int client;
	  int c;
	  
	  client = -1;
	  for (c = 0; c < MAX_NET_CLIENTS && client < 0; c++) {
	    if (client_sock[c] <= 0) {
	      client = c;
	    }
	  }

	  if (client >= 0) {

	    if(listen(listen_sock,MAX_NET_CLIENTS) == -1)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      perror ("connect_listen_thread: Listen failed");
	      return (NULL);
	    }
	
	    text_color_set(DW_COLOR_INFO);
            dw_printf("Ready to accept AGW client application %d on port %d ...\n", client, server_port);
         
            client_sock[client] = accept(listen_sock, (struct sockaddr*)(&sockaddr),&sockaddr_size);

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("\nConnected to AGW client application %d...\n\n", client);

/*
 * The command to change this is actually a toggle, not explicit on or off.
 * Make sure it has proper state when we get a new connection.
 */ 
	    enable_send_raw_to_client[client] = 0;
	    enable_send_monitor_to_client[client] = 0;
	  }
	  else {
	    SLEEP_SEC(1);	/* wait then check again if more clients allowed. */
	  }
 	}
#endif
}


/*-------------------------------------------------------------------
 *
 * Name:        server_send_rec_packet
 *
 * Purpose:     Send a received packet to the client app.
 *
 * Inputs:	chan		- Channel number where packet was received.
 *				  0 = first, 1 = second if any.
 *
 *		pp		- Identifier for packet object.
 *
 *		fbuf		- Address of raw received frame buffer.
 *		flen		- Length of raw received frame.
 *		
 *
 * Description:	Send message to client if connected.
 *		Disconnect from client, and notify user, if any error.
 *
 *		There are two different formats:
 *			RAW - the original received frame.
 *			MONITOR - just the information part.
 *
 *--------------------------------------------------------------------*/


void server_send_rec_packet (int chan, packet_t pp, unsigned char *fbuf,  int flen)
{
	struct {	
	  struct agwpe_s hdr;
	  char data[1+AX25_MAX_PACKET_LEN];		
	} agwpe_msg;

	int err;
	int info_len;
	unsigned char *pinfo;
	int client;


/*
 * RAW format
 */
	for (client=0; client<MAX_NET_CLIENTS; client++) {

	  if (enable_send_raw_to_client[client] && client_sock[client] > 0){

	    memset (&agwpe_msg.hdr, 0, sizeof(agwpe_msg.hdr));

	    agwpe_msg.hdr.portx = chan;

	    agwpe_msg.hdr.kind_lo = 'K';

	    ax25_get_addr_with_ssid (pp, AX25_SOURCE, agwpe_msg.hdr.call_from);

	    ax25_get_addr_with_ssid (pp, AX25_DESTINATION, agwpe_msg.hdr.call_to);

	    agwpe_msg.hdr.data_len = flen + 1;

	    /* Stick in extra byte for the "TNC" to use. */

	    agwpe_msg.data[0] = 0;
	    memcpy (agwpe_msg.data + 1, fbuf, (size_t)flen);

	    if (debug_client) {
	      debug_print (TO_CLIENT, client, &agwpe_msg.hdr, sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len);
	    }

#if __WIN32__	
            err = send (client_sock[client], (char*)(&agwpe_msg), sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len, 0);
	    if (err == SOCKET_ERROR)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError %d sending message to AGW client application.  Closing connection.\n\n", WSAGetLastError());
	      closesocket (client_sock[client]);
	      client_sock[client] = -1;
	      WSACleanup();
	    }
#else
            err = write (client_sock[client], &agwpe_msg, sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len);
	    if (err <= 0)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError sending message to AGW client application.  Closing connection.\n\n");
	      close (client_sock[client]);
	      client_sock[client] = -1;    
	    }
#endif
	  }
	}


/* MONITOR format - only for UI frames. */

	for (client=0; client<MAX_NET_CLIENTS; client++) {
	
	  if (enable_send_monitor_to_client[client] && client_sock[client] > 0 
			&& ax25_get_control(pp) == AX25_UI_FRAME){

	    time_t clock;
	    struct tm *tm;

	    clock = time(NULL);
	    tm = localtime(&clock);	// TODO: should use localtime_r

	    memset (&agwpe_msg.hdr, 0, sizeof(agwpe_msg.hdr));

	    agwpe_msg.hdr.portx = chan;

	    agwpe_msg.hdr.kind_lo = 'U';

	    ax25_get_addr_with_ssid (pp, AX25_SOURCE, agwpe_msg.hdr.call_from);

	    ax25_get_addr_with_ssid (pp, AX25_DESTINATION, agwpe_msg.hdr.call_to);

	    info_len = ax25_get_info (pp, &pinfo);

	    /* http://uz7ho.org.ua/includes/agwpeapi.htm#_Toc500723812 */

	    /* Description mentions one CR character after timestamp but example has two. */
	    /* Actual observed cases have only one. */
	    /* Also need to add extra CR, CR, null at end. */
	    /* The documentation example includes these 3 extra in the Len= value */
	    /* but actual observed data uses only the packet info length. */

	    snprintf (agwpe_msg.data, sizeof(agwpe_msg.data), " %d:Fm %s To %s <UI pid=%02X Len=%d >[%02d:%02d:%02d]\r%s\r\r",
			chan+1, agwpe_msg.hdr.call_from, agwpe_msg.hdr.call_to,
			ax25_get_pid(pp), info_len, 
			tm->tm_hour, tm->tm_min, tm->tm_sec,
			pinfo);

	    agwpe_msg.hdr.data_len = strlen(agwpe_msg.data) + 1 /* include null */ ;

	    if (debug_client) {
	      debug_print (TO_CLIENT, client, &agwpe_msg.hdr, sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len);
	    }

#if __WIN32__	
            err = send (client_sock[client], (char*)(&agwpe_msg), sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len, 0);
	    if (err == SOCKET_ERROR)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError %d sending message to AGW client application %d.  Closing connection.\n\n", WSAGetLastError(), client);
	      closesocket (client_sock[client]);
	      client_sock[client] = -1;
	      WSACleanup();
	    }
#else
            err = write (client_sock[client], &agwpe_msg, sizeof(agwpe_msg.hdr) + agwpe_msg.hdr.data_len);
	    if (err <= 0)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError sending message to AGW client application %d.  Closing connection.\n\n", client);
	      close (client_sock[client]);
	      client_sock[client] = -1;    
	    }
#endif
	  }
	}

} /* server_send_rec_packet */



/*-------------------------------------------------------------------
 *
 * Name:        read_from_socket
 *
 * Purpose:     Read from socket until we have desired number of bytes.
 *
 * Inputs:	fd		- file descriptor.
 *		ptr		- address where data should be placed.
 *		len		- desired number of bytes.
 *
 * Description:	Just a wrapper for the "read" system call but it should
 *		never return fewer than the desired number of bytes.
 *
 *--------------------------------------------------------------------*/

static int read_from_socket (int fd, char *ptr, int len)
{
	int got_bytes = 0;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("read_from_socket (%d, %p, %d)\n", fd, ptr, len);
#endif
	while (got_bytes < len) {
	  int n;

#if __WIN32__

//TODO: any flags for send/recv?

	  n = recv (fd, ptr + got_bytes, len - got_bytes, 0);
#else
	  n = read (fd, ptr + got_bytes, len - got_bytes);
#endif

#if DEBUG
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("read_from_socket: n = %d\n", n);
#endif
	  if (n <= 0) {
	    return (n);
	  }

	  got_bytes += n;
	}
	assert (got_bytes >= 0 && got_bytes <= len);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("read_from_socket: return %d\n", got_bytes);
#endif
	return (got_bytes);
}


/*-------------------------------------------------------------------
 *
 * Name:        cmd_listen_thread
 *
 * Purpose:     Wait for command messages from an application.
 *
 * Inputs:	arg		- client number, 0 .. MAX_NET_CLIENTS-1
 *
 * Outputs:	client_sock[n]	- File descriptor for communicating with client app.
 *
 * Description:	Process messages from the client application.
 *		Note that the client can go away and come back again and
 *		re-establish communication without restarting this application.
 *
 *--------------------------------------------------------------------*/


static void send_to_client (int client, void *reply_p)
{
	struct agwpe_s *ph;
	int len;
#if __WIN32__     
#else
	int err;
#endif

	ph = (struct agwpe_s *) reply_p;	// Replies are often hdr + other stuff.

	len = sizeof(struct agwpe_s) + ph->data_len;

	/* Not sure what max data length might be. */

	if (ph->data_len < 0 || ph->data_len > 4096) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Invalid data length %d for AGW protocol message to client %d.\n", ph->data_len, client);
	  debug_print (TO_CLIENT, client, ph, len);
	}

	if (debug_client) {
	  debug_print (TO_CLIENT, client, ph, len);
	}

#if __WIN32__     
	send (client_sock[client], (char*)(ph), len, 0);
#else
	err = write (client_sock[client], ph, len);
#endif
}


static THREAD_F cmd_listen_thread (void *arg)
{
	int n;

	struct {
	  struct agwpe_s hdr;		/* Command header. */
	
	  char data[512];		/* Additional data used by some commands. */
					/* Maximum for 'V': 1 + 8*10 + 256 */
	} cmd;

	int client = (int)(long)arg;

	assert (client >= 0 && client < MAX_NET_CLIENTS);

	while (1) {

	  while (client_sock[client] <= 0) {
	    SLEEP_SEC(1);			/* Not connected.  Try again later. */
	  }

	  n = read_from_socket (client_sock[client], (char *)(&cmd.hdr), sizeof(cmd.hdr));
	  if (n != sizeof(cmd.hdr)) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nError getting message header from AGW client application %d.\n", client);
	    dw_printf ("Tried to read %d bytes but got only %d.\n", (int)sizeof(cmd.hdr), n);
	    dw_printf ("Closing connection.\n\n");
#if __WIN32__
	    closesocket (client_sock[client]);
#else
	    close (client_sock[client]);
#endif
	    client_sock[client] = -1;
	    continue;
	  }

/*
 * Take some precautions to guard against bad data
 * which could cause problems later.
 */

/*
 * Call to/from must not exceeed 9 characters.
 * It's not guaranteed that unused bytes will contain 0 so we
 * don't issue error message in this case. 
 */

	  cmd.hdr.call_from[sizeof(cmd.hdr.call_from)-1] = '\0';
	  cmd.hdr.call_to[sizeof(cmd.hdr.call_to)-1] = '\0';

/*
 * Following data must fit in available buffer.
 * Leave room for an extra nul byte terminator at end later.
 */

	  if (cmd.hdr.data_len < 0 || cmd.hdr.data_len > sizeof(cmd.data) - 1) {

	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("\nInvalid message from AGW client application %d.\n", client);
	    dw_printf ("Data Length of %d is out of range.\n", cmd.hdr.data_len);
	
	    /* This is a bad situation. */
	    /* If we tried to read again, the header probably won't be there. */
	    /* No point in trying to continue reading.  */

	    dw_printf ("Closing connection.\n\n");
#if __WIN32__
	    closesocket (client_sock[client]);
#else
	    close (client_sock[client]);
#endif
	    client_sock[client] = -1;
	    return (0);
	  }

	  cmd.data[0] = '\0';

	  if (cmd.hdr.data_len > 0) {
	    n = read_from_socket (client_sock[client], cmd.data, cmd.hdr.data_len);
	    if (n != cmd.hdr.data_len) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError getting message data from AGW client application %d.\n", client);
	      dw_printf ("Tried to read %d bytes but got only %d.\n", cmd.hdr.data_len, n);
	      dw_printf ("Closing connection.\n\n");
#if __WIN32__
	      closesocket (client_sock[client]);
#else
	      close (client_sock[client]);
#endif
	      client_sock[client] = -1;
	      return (0);
	    }
	    if (n > 0) {
		cmd.data[cmd.hdr.data_len] = '\0';
	    }
	  }

/*
 * print & process message from client.
 */

	  if (debug_client) {
	    debug_print (FROM_CLIENT, client, &cmd.hdr, sizeof(cmd.hdr) + cmd.hdr.data_len);
	  }

	  switch (cmd.hdr.kind_lo) {

	    case 'R':				/* Request for version number */
	      {
		struct {
		  struct agwpe_s hdr;
	 	  int major_version;
	 	  int minor_version;
		} reply;


	        memset (&reply, 0, sizeof(reply));
	        reply.hdr.kind_lo = 'R';
	        reply.hdr.data_len = sizeof(reply.major_version) + sizeof(reply.minor_version);
		assert (reply.hdr.data_len == 8);

		// Xastir only prints this and doesn't care otherwise.
		// APRSIS32 doesn't seem to care.
		// UI-View32 wants on 2000.15 or later.

	        reply.major_version = 2005;
	        reply.minor_version = 127;

		assert (sizeof(reply) == 44);

	        send_to_client (client, &reply);

	      }
	      break;

	    case 'G':				/* Ask about radio ports */

	      {
		struct {
		  struct agwpe_s hdr;
	 	  char info[200];
		} reply;


		int j, count;


	        memset (&reply, 0, sizeof(reply));
	        reply.hdr.kind_lo = 'G';


		// Xastir only prints this and doesn't care otherwise.
		// YAAC uses this to identify available channels.

		// The interface manual wants the first to be "Port1" 
		// so channel 0 corresponds to "Port1."
		// We can have gaps in the numbering.
		// I wonder what applications will think about that.

#if 1
		// No other place cares about total number.

		count = 0;
		for (j=0; j<MAX_CHANS; j++) {
	 	  if (save_audio_config_p->achan[j].valid) {
		    count++;
		  }
		}
		snprintf (reply.info, sizeof(reply.info), "%d;", count);

		for (j=0; j<MAX_CHANS; j++) {
	 	  if (save_audio_config_p->achan[j].valid) {
		    char stemp[100];
		    int a = ACHAN2ADEV(j);
		    // If I was really ambitious, some description could be provided.
		    static const char *names[8] = { "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth" };

		    if (save_audio_config_p->adev[a].num_channels == 1) {
		      snprintf (stemp, sizeof(stemp), "Port%d %s soundcard mono;", j+1, names[a]);
		      strlcat (reply.info, stemp, sizeof(reply.info));
		    }
		    else {
		      snprintf (stemp, sizeof(stemp), "Port%d %s soundcard %s;", j+1, names[a], j&1 ? "right" : "left");
		      strlcat (reply.info, stemp, sizeof(reply.info));
		    }
		  }
		}

#else
		if (num_channels == 1) {
		  snprintf (reply.info, sizeof(reply.info), "1;Port1 Single channel;");
		}
		else {
		  snprintf (reply.info, sizeof(reply.info), "2;Port1 Left channel;Port2 Right Channel;");
		}
#endif
	        reply.hdr.data_len = strlen(reply.info) + 1;

	        send_to_client (client, &reply);

	      }
	      break;


	    case 'g':				/* Ask about capabilities of a port. */

	      {
		struct {
		  struct agwpe_s hdr;
	 	  unsigned char on_air_baud_rate; 	/* 0=1200, 3=9600 */
		  unsigned char traffic_level;		/* 0xff if not in autoupdate mode */
		  unsigned char tx_delay;
		  unsigned char tx_tail;
		  unsigned char persist;
		  unsigned char slottime;
		  unsigned char maxframe;
		  unsigned char active_connections;
		  int how_many_bytes;
		} reply;


	        memset (&reply, 0, sizeof(reply));

		reply.hdr.portx = cmd.hdr.portx;	/* Reply with same port number ! */
	        reply.hdr.kind_lo = 'g';
	        reply.hdr.data_len = 12;

		// YAAC asks for this.
		// Fake it to keep application happy.

	        reply.on_air_baud_rate = 0;
		reply.traffic_level = 1;
		reply.tx_delay = 0x19;
		reply.tx_tail = 4;
		reply.persist = 0xc8;
		reply.slottime = 4;
		reply.maxframe = 7;
		reply.active_connections = 0;
		reply.how_many_bytes = 1;

		assert (sizeof(reply) == 48);

	        send_to_client (client, &reply);

	      }
	      break;


	    case 'H':				/* Ask about recently heard stations. */

	      {
#if 0						/* This information is not being collected. */
		struct {
		  struct agwpe_s hdr;
	 	  char info[100];
		} reply;


	        memset (&reply.hdr, 0, sizeof(reply.hdr));
	        reply.hdr.kind_lo = 'H';

		// TODO:  Implement properly.  

	        reply.hdr.portx = cmd.hdr.portx

	        strlcpy (reply.hdr.call_from, "WB2OSZ-15", sizeof(reply.hdr.call_from));

	        strlcpy (agwpe_msg.data, ..., sizeof(agwpe_msg.data));

	        reply.hdr.data_len = strlen(reply.info);

	        send_to_client (client, &reply);
#endif
	      }
	      break;
	    



	    case 'k':				/* Ask to start receiving RAW AX25 frames */

	      // Actually it is a toggle so we must be sure to clear it for a new connection.

	      enable_send_raw_to_client[client] = ! enable_send_raw_to_client[client];
	      break;

	    case 'm':				/* Ask to start receiving Monitor frames */

	      // Actually it is a toggle so we must be sure to clear it for a new connection.

	      enable_send_monitor_to_client[client] = ! enable_send_monitor_to_client[client];
	      break;


	    case 'V':				/* Transmit UI data frame (with digipeater path) */
	      {
	      	// Data format is:
	      	//	1 byte for number of digipeaters.
	      	//	10 bytes for each digipeater.
	      	//	data part of message.

	      	char stemp[AX25_MAX_PACKET_LEN+2];
		char *p;
		int ndigi;
		int k;
	      
		packet_t pp;

		// We have already verified these do not exceed 9 characters. (?)

	      	strlcpy (stemp, cmd.hdr.call_from, sizeof(stemp));
	      	strlcat (stemp, ">", sizeof(stemp));
	      	strlcat (stemp, cmd.hdr.call_to, sizeof(stemp));

		cmd.data[cmd.hdr.data_len] = '\0';
		ndigi = cmd.data[0];
		p = cmd.data + 1;

		for (k=0; k<ndigi; k++) {
		  strlcat (stemp, ",", sizeof(stemp));
		  strlcat (stemp, p, sizeof(stemp));
		  p += 10;
	        }
		strlcat (stemp, ":", sizeof(stemp));
		strlcat (stemp, p, sizeof(stemp));

	        //text_color_set(DW_COLOR_DEBUG);
		//dw_printf ("Transmit '%s'\n", stemp);

		pp = ax25_from_text (stemp, 1);


		if (pp == NULL) {
	          text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Failed to create frame from AGW 'V' message.\n");
		}
		else {

		  /* This goes into the low priority queue because it is an original. */

		  /* Note that the protocol has no way to set the "has been used" */
		  /* bits in the digipeater fields. */

		  /* This explains why the digipeating option is grayed out in */
		  /* xastir when using the AGW interface.  */
		  /* The current version uses only the 'V' message, not 'K' for transmitting. */

		  tq_append (cmd.hdr.portx, TQ_PRIO_1_LO, pp);

		}
	      }
	      
	      break;

	    case 'K':				/* Transmit raw AX.25 frame */
	      {
	      	// Message contains:
	      	//	port number for transmission.
	      	//	data length
	      	//	data which is raw ax.25 frame.
		//		
	      
		packet_t pp;
		alevel_t alevel;

		// Bug fix in version 1.1:
		//
		// The first byte of data is described as:
		//
		// 		the "TNC" to use
		//		00=Port 1
		//		16=Port 2
		//
		// I don't know what that means; we already a port number in the header.
		// Anyhow, the original code here added one to cmd.data to get the 
		// first byte of the frame.  Unfortunately, it did not subtract one from
		// cmd.hdr.data_len so we ended up sending an extra byte.

		memset (&alevel, 0xff, sizeof(alevel));
		pp = ax25_from_frame ((unsigned char *)cmd.data+1, cmd.hdr.data_len - 1, alevel);

		if (pp == NULL) {
	          text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Failed to create frame from AGW 'K' message.\n");
		}
		else {

		  /* How can we determine if it is an original or repeated message? */
		  /* If there is at least one digipeater in the frame, AND */
		  /* that digipeater has been used, it should go out quickly thru */
		  /* the high priority queue. */
		  /* Otherwise, it is an original for the low priority queue. */

		  if (ax25_get_num_repeaters(pp) >= 1 &&
		      ax25_get_h(pp,AX25_REPEATER_1)) {
		    tq_append (cmd.hdr.portx, TQ_PRIO_0_HI, pp);
		  }
		  else {
		    tq_append (cmd.hdr.portx, TQ_PRIO_1_LO, pp);
		  }
		}
	      }
	      
	      break;

	    case 'X':				/* Register CallSign  */

	      /* Send success status. */

	      {
		struct {
		  struct agwpe_s hdr;
		  char data;
		} reply;


	        memset (&reply, 0, sizeof(reply));
	        reply.hdr.kind_lo = 'X';
		memcpy (reply.hdr.call_from, cmd.hdr.call_from, sizeof(reply.hdr.call_from));
	        reply.hdr.data_len = 1;
		reply.data = 1;		/* success */
	
		// Version 1.0.
		// Previously used sizeof(reply) but compiler rounded it up to next byte boundary.
		// That's why more cumbersome size expression is used.

	        send_to_client (client, &reply);

	      }
	      break;

	    case 'x':				/* Unregister CallSign  */
	      /* No reponse is expected. */
	      break;

	    case 'C':				/* Connect, Start an AX.25 Connection  */
	    case 'v':	      			/* Connect VIA, Start an AX.25 circuit thru digipeaters */
	    case 'D': 				/* Send Connected Data */
	    case 'd': 				/* Disconnect, Terminate an AX.25 Connection */

	      // Version 1.0.  Better message instead of generic unexpected command.

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\n");
	      dw_printf ("Can't process command '%c' from AGW client app %d.\n", cmd.hdr.kind_lo, client);
	      dw_printf ("Connected packet mode is not implemented.\n");

	      break;


	    case 'M': 				/* Send UNPROTO Information (no digipeater path) */

		/* 
		Added in version 1.3.
		This is the same as 'V' except there is no provision for digipeaters.
		AGWterminal sends this for beacon or ask QRA.

		<<< Send UNPROTO Information from AGWPE client application 0, total length = 253
		        portx = 0, port_hi_reserved = 0
		        kind_lo = 77 = 'M', kind_hi = 0
		        call_from = "SV2AGW-1", call_to = "BEACON"
		        data_len = 217, user_reserved = 588, data =
		  000:  54 68 69 73 20 76 65 72 73 69 6f 6e 20 75 73 65  This version use
		  010:  73 20 74 68 65 20 6e 65 77 20 41 47 57 20 50 61  s the new AGW Pa
		  020:  63 6b 65 74 20 45 6e 67 69 6e 65 20 77 69 6e 73  cket Engine wins

		<<< Send UNPROTO Information from AGWPE client application 0, total length = 37
		        portx = 0, port_hi_reserved = 0
		        kind_lo = 77 = 'M', kind_hi = 0
		        call_from = "SV2AGW-1", call_to = "QRA"
		        data_len = 1, user_reserved = 32218432, data =
		  000:  0d                                               .

		There is also a report of it coming from UISS.

		<<< Send UNPROTO Information from AGWPE client application 0, total length = 50
			portx = 0, port_hi_reserved = 0
			kind_lo = 77 = 'M', kind_hi = 0
			call_from = "JH4XSY", call_to = "APRS"
			data_len = 14, user_reserved = 0, data =
		  000:  21 22 3c 43 2e 74 71 6c 48 72 71 21 21 5f        !"<C.tqlHrq!!_
		*/
	      {
	      
		int pid = cmd.hdr.kind_hi & 0xff;
		(void)(pid);
			/* The AGW protocol spec says, */
			/* "AX.25 PID 0x00 or 0xF0 for AX.25 0xCF NETROM and others" */

			/* BUG: In theory, the AX.25 PID octet should be set from this. */
			/* All examples seen (above) have 0. */
			/* The AX.25 protocol spec doesn't list 0 as a valid value. */
			/* We always send 0xf0, meaning no layer 3. */
			/* Maybe we should have an ax25_set_pid function for cases when */
			/* it is neither 0 nor 0xf0. */

	      	char stemp[AX25_MAX_PACKET_LEN];
		packet_t pp;

	      	strlcpy (stemp, cmd.hdr.call_from, sizeof(stemp));
	      	strlcat (stemp, ">", sizeof(stemp));
	      	strlcat (stemp, cmd.hdr.call_to, sizeof(stemp));

		cmd.data[cmd.hdr.data_len] = '\0';

		strlcat (stemp, ":", sizeof(stemp));
		strlcat (stemp, cmd.data, sizeof(stemp));

	        //text_color_set(DW_COLOR_DEBUG);
		//dw_printf ("Transmit '%s'\n", stemp);

		pp = ax25_from_text (stemp, 1);

		if (pp == NULL) {
	          text_color_set(DW_COLOR_ERROR);
		  dw_printf ("Failed to create frame from AGW 'M' message.\n");
		}
		else {
		  tq_append (cmd.hdr.portx, TQ_PRIO_1_LO, pp);
		}
	      }
	      break;


	    case 'y':				/* Ask Outstanding frames waiting on a Port  */

	      {
		struct {
		  struct agwpe_s hdr;
		  int data;			// Assuming little-endian architecture.
		} reply;


	        memset (&reply, 0, sizeof(reply));
		reply.hdr.portx = cmd.hdr.portx;	/* Reply with same port number */
	        reply.hdr.kind_lo = 'y';
	        reply.hdr.data_len = 4;
		reply.data = 0;		
	
	        if (cmd.hdr.portx >= 0 && cmd.hdr.portx < MAX_CHANS) {
		  reply.data = tq_count (cmd.hdr.portx, TQ_PRIO_0_HI) + tq_count (cmd.hdr.portx, TQ_PRIO_1_LO);
		}

	        send_to_client (client, &reply);
	      }
	      break;

	    default:

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("--- Unexpected Command from application %d using AGW protocol:\n", client);
	      debug_print (FROM_CLIENT, client, &cmd.hdr, sizeof(cmd.hdr) + cmd.hdr.data_len);

	      break;
	  }
	

	}

}

/* end server.c */

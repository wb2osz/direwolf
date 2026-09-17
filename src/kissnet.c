//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//    Copyright (C) 2011-2014, 2015, 2017, 2021, 2025  John Langner, WB2OSZ
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
 * Module:      kissnet.c
 *
 * Purpose:   	Provide service to other applications via KISS protocol via TCP socket.
 *		
 * Input:	
 *
 * Outputs:	  
 *
 * Description:	This provides a TCP socket for communication with a client application.
 *
 *		It implements the KISS TNS protocol as described in:
 *		http://www.ka9q.net/papers/kiss.html
 *
 * 		Briefly, a frame is composed of 
 *
 *			* FEND (0xC0)
 *			* Contents - with special escape sequences so a 0xc0
 *				byte in the data is not taken as end of frame.
 *				as part of the data.
 *			* FEND
 *
 *		The first byte of the frame contains:
 *	
 *			* port number in upper nybble.
 *			* command in lower nybble.
 *
 *	
 *		Commands from application recognized:
 *
 *			_0	Data Frame	AX.25 frame in raw format.
 *
 *			_1	TXDELAY		See explanation in xmit.c.
 *
 *			_2	Persistence	"	"
 *
 *			_3 	SlotTime	"	"
 *
 *			_4	TXtail		"	"
 *						Spec says it is obsolete but Xastir
 *						sends it and we respect it.
 *
 *			_5	FullDuplex	Ignored.
 *		
 *			_6	SetHardware	TNC specific.
 *			
 *			FF	Return		Exit KISS mode.  Ignored.
 *
 *
 *		Messages sent to client application:
 *
 *			_0	Data Frame	Received AX.25 frame in raw format.
 *
 *
 *		
 *
 * References:	Getting Started with Winsock
 *		http://msdn.microsoft.com/en-us/library/windows/desktop/bb530742(v=vs.85).aspx
 *
 * Future:	Originally we had:
 *			KISS over serial port.
 *			AGW over socket.
 *		This is the two of them munged together and we end up with duplicate code.
 *		It would have been better to separate out the transport and application layers.
 *		Maybe someday.
 *
 *---------------------------------------------------------------*/

/*
	Separate TCP ports per radio:

An increasing number of people are using multiple radios.
direwolf is capable of handling many radio channels and
provides cross-band repeating, etc.
Maybe a single stereo audio interface is used for 2 radios.

                   +------------+    tcp 8001, all channels
Radio A  --------  |            |  -------------------------- Application A
                   |  direwolf  |
Radio B  --------  |            |  -------------------------- Application B
                   +------------+    tcp 8001, all channels

The KISS protocol has a 4 bit field for the TNC port (which I prefer to
call channel because port has too many different meanings).
direwolf handles this fine.  However, most applications were written assuming
that a TNC could only talk to a single radio.  On reception, they ignore the
channel in the KISS frame.  For transmit, the channel is always set to 0.

Many people are using the work-around of two separate instances of direwolf.

                   +------------+    tcp 8001, KISS ch 0
Radio A  --------  |  direwolf  |  -------------------------- Application A
                   +------------+

                   +------------+    tcp 8002, KISS ch 0
Radio B  --------  |  direwolf  |  -------------------------- Application B
                   +------------+


Or they might be using a single application that knows how to talk to multiple
single port TNCs.  But they don't know how to multiplex multiple channels
thru a single KISS stream.

                   +------------+    tcp 8001, KISS ch 0
Radio A  --------  |  direwolf  |  ------------------------
                   +------------+                          \
                                                            -- Application
                   +------------+    tcp 8002, KISS ch 0   /
Radio B  --------  |  direwolf  |  ------------------------
                   +------------+

Using two different instances of direwolf means more complex configuration
and loss of cross-channel digipeating.  It is possible to use a stereo
audio interface but some ALSA magic is required to make it look like two
independent virtual mono interfaces.

In version 1.7, we add the capability of multiple KISS TCP ports, each for
a single radio channel.  e.g.

KISSPORT 8001 1
KISSPORT 8002 2

Now can use a single instance of direwolf.


                   +------------+    tcp 8001, KISS ch 0
Radio A  --------  |            |  -------------------------- Application A
                   |  direwolf  |
Radio B  --------  |            |  -------------------------- Application B
                   +------------+    tcp 8002, KISS ch 0

When receiving, the KISS channel is set to 0.
 - only radio channel 1 would be sent over tcp port 8001.
 - only radio channel 2 would be sent over tcp port 8001.

When transmitting, the KISS channel is ignored.
 - frames from tcp port 8001 are transmitted on radio channel 1.
 - frames from tcp port 8002 are transmitted on radio channel 2.

Of course, you could also use an application, capable of connecting to
multiple single radio TNCs.  Separate TCP ports actually go to the
same direwolf instance.

*/


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
#include <stddef.h>


#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "kissnet.h"
#include "kiss_frame.h"
#include "xmit.h"

void hex_dump (unsigned char *p, int len);	// This should be in a .h file.





// TODO:  define in one place, use everywhere.
#if __WIN32__
#define THREAD_F unsigned __stdcall
#else
#define THREAD_F void *
#endif

static THREAD_F connect_listen_thread (void *arg);
static THREAD_F kissnet_listen_thread (void *arg);


static struct misc_config_s *s_misc_config_p;


// Each TCP port has its own status block.
// There is a variable number so use a linked list.

static struct kissport_status_s *all_ports = NULL;

static int kiss_debug = 0;		/* Print information flowing from and to client. */

void kiss_net_set_debug (int n) 
{	
	kiss_debug = n;
}



/*-------------------------------------------------------------------
 *
 * Name:        kissnet_init
 *
 * Purpose:     Set up a server to listen for connection requests from
 *		an application such as Xastir or APRSIS32.
 *		This is called once from the main program.
 *
 * Inputs:	mc->kiss_port	- TCP port for server.
 *				0 means disable.  New in version 1.2.
 *
 * Outputs:	
 *
 * Description:	This starts two threads:
 *		  *  to listen for a connection from client app.
 *		  *  to listen for commands from client app.
 *		so the main application doesn't block while we wait for these.
 *
 *--------------------------------------------------------------------*/

static void kissnet_init_one (struct kissport_status_s *kps);

void kissnet_init (struct misc_config_s *mc)
{
	s_misc_config_p = mc;

	for (int i = 0; i < MAX_KISS_TCP_PORTS; i++) {
	  if (mc->kiss_port[i] != 0) {
	    struct kissport_status_s *kps = calloc(sizeof(struct kissport_status_s), 1);
	    if (kps == NULL) {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("FATAL ERROR: Out of memory.\n");
	      exit (EXIT_FAILURE);
	    }

	    kps->tcp_port = mc->kiss_port[i];
	    kps->chan = mc->kiss_chan[i];
	    kissnet_init_one (kps);

	    // Add to list.
	    kps->pnext = all_ports;
	    all_ports = kps;
	  }
	}
}


static void kissnet_init_one (struct kissport_status_s *kps)
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




#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_init ( tcp port %d, radio chan = %d )\n", kps->tcp_port, kps->chan);
#endif

	
	for (client=0; client<MAX_NET_CLIENTS; client++) {
	  kps->client_sock[client] = -1;
	  memset (&(kps->kf[client]), 0, sizeof(kps->kf[client]));
	}

	if (kps->tcp_port == 0) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Disabled KISS network client port.\n");
	  return;
	}
	
/*
 * This waits for a client to connect and sets client_sock[n].
 */
#if __WIN32__
	connect_listen_th = (HANDLE)_beginthreadex (NULL, 0, connect_listen_thread, (void *)kps, 0, NULL);
	if (connect_listen_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create KISS socket connect listening thread for tcp port %d, radio chan %d\n", kps->tcp_port, kps->chan);
	  return;
	}
#else
	e = pthread_create (&connect_listen_tid, NULL, connect_listen_thread, (void *)kps);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create KISS socket connect listening thread");
	  dw_printf ("for tcp port %d, radio chan %d\n", kps->tcp_port, kps->chan);
	  return;
	}
#endif

/*
 * These read messages from client when client_sock[n] is valid.
 * Currently we start up a separate thread for each potential connection.
 * Possible later refinement.  Start one now, others only as needed.
 */
	for (client = 0; client < MAX_NET_CLIENTS; client++) {

	  kps->arg2 = client;

#if __WIN32__
	  cmd_listen_th[client] = (HANDLE)_beginthreadex (NULL, 0, kissnet_listen_thread, (void*)kps, 0, NULL);
	  if (cmd_listen_th[client] == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create KISS command listening thread for client %d\n", client);
	    return;
	  }
#else
	  e = pthread_create (&(cmd_listen_tid[client]), NULL, kissnet_listen_thread, (void *)kps);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create KISS command listening thread for client %d\n", client);
	    // Replace add perror with better message handling.
	    perror("");
	    return;
	  }
#endif
	  // Wait for new thread to get content of arg2 before reusing it for the next thread create.

	  int timer = 0;
	  while (kps->arg2 >= 0) {
	    SLEEP_MS(10);
	    timer++;
	    if (timer > 100) {	// 1 second - thread did not start
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("KISS data listening thread did not start for tcp port %d, client slot %d\n", kps->tcp_port, client);
	      kps->arg2 = -1;		// Keep moving along.
	    }
	  }
	}
}


/*-------------------------------------------------------------------
 *
 * Name:        connect_listen_thread
 *
 * Purpose:     Wait for a connection request from an application.
 *
 * Inputs:	arg		- KISS port status block.
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
	struct kissport_status_s *kps = arg;

#if __WIN32__

	struct addrinfo hints;
	struct addrinfo *ai = NULL;
	int err;
	char tcp_port_str[12];

	SOCKET listen_sock;  
	WSADATA wsadata;

	snprintf (tcp_port_str, sizeof(tcp_port_str), "%d", kps->tcp_port);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("DEBUG: kissnet port = %d = '%s'\n", (int)(ptrdiff_t)arg, tcp_port_str);
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

	err = getaddrinfo(NULL, tcp_port_str, &hints, &ai);
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

	// Issue 620 - TCP send buffer might be too small.
	// Add option for experimenation.  More discussion in dlq.c.

	if (s_misc_config_p->tcp_wmem != 0) {
	  socklen_t optlen = sizeof(int);
	  int oldbufsiz = 0;
	  getsockopt (listen_sock, SOL_SOCKET, SO_SNDBUF, (void*)(&oldbufsiz), &optlen);
	  int newbufsiz = s_misc_config_p->tcp_wmem;
	  int err = setsockopt (listen_sock, SOL_SOCKET, SO_SNDBUF, (void*)(&newbufsiz), optlen);
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("TCP KISS send buffer size changed from %d to %d (%d)\n", oldbufsiz, newbufsiz, err);
	}

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf("Binding to port %s ... \n", tcp_port_str);
#endif

	err = bind( listen_sock, ai->ai_addr, (int)ai->ai_addrlen);
	if (err == SOCKET_ERROR) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", WSAGetLastError());		// TODO: provide corresponding text.
	  dw_printf("Some other application is probably already using port %s.\n", tcp_port_str);
	  dw_printf("Try using a different port number with KISSPORT in the configuration file.\n");
          freeaddrinfo(ai);
          closesocket(listen_sock);
          WSACleanup();
          return (0);
        }

	freeaddrinfo(ai);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf("opened KISS socket as fd (%d) on port (%s) for stream i/o\n", listen_sock, tcp_port_str );
#endif

 	while (1) {
  	 
	  int client;
	  int c;

	  client = -1;
	  for (c = 0; c < MAX_NET_CLIENTS && client < 0; c++) {
	    if (kps->client_sock[c] <= 0) {
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
	    if (kps->chan == -1) {
              dw_printf("Ready to accept KISS TCP client application %d on port %s ...\n", client, tcp_port_str);
	    }
	    else {
              dw_printf("Ready to accept KISS TCP client application %d on port %s (radio channel %d) ...\n", client, tcp_port_str, kps->chan);
	    }

            kps->client_sock[client] = accept(listen_sock, NULL, NULL);

	    if (kps->client_sock[client] == -1) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Accept failed with error: %d\n", WSAGetLastError());
              closesocket(listen_sock);
              WSACleanup();
              return (0);
            }

	    text_color_set(DW_COLOR_INFO);
	    if (kps->chan == -1) {
	      dw_printf("\nAttached to KISS TCP client application %d on port %s ...\n\n", client, tcp_port_str);
	    }
	    else {
	      dw_printf("\nAttached to KISS TCP client application %d on port %s (radio channel %d) ...\n\n", client, tcp_port_str, kps->chan);
	    }

	    // Reset the state and buffer.
	    memset (&(kps->kf[client]), 0, sizeof(kps->kf[client]));
	  }
	  else {
	    SLEEP_SEC(1);	/* wait then check again if more clients allowed. */
	  }
 	}


#else		/* End of Windows case, now Linux / Unix / Mac OSX. */


    	struct sockaddr_in sockaddr; /* Internet socket address struct */
    	socklen_t sockaddr_size = sizeof(struct sockaddr_in);
	int listen_sock;  
	int bcopt = 1;

	listen_sock= socket(AF_INET,SOCK_STREAM,0);
	if (listen_sock == -1) {
	  text_color_set(DW_COLOR_ERROR);
	  perror ("connect_listen_thread: Socket creation failed");
	  return (NULL);
	}

	// Issue 620 - TCP send buffer might be too small.
	// Add option for experimentation.  More discussion in dlq.c.

	if (s_misc_config_p->tcp_wmem != 0) {
	  socklen_t optlen = sizeof(int);
	  int oldbufsiz = 0;
	  getsockopt (listen_sock, SOL_SOCKET, SO_SNDBUF, (void*)(&oldbufsiz), &optlen);
	  int newbufsiz = s_misc_config_p->tcp_wmem;
	  int err = setsockopt (listen_sock, SOL_SOCKET, SO_SNDBUF, (void*)(&newbufsiz), optlen);
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("TCP KISS send buffer size changed from %d to %d (%d)\n", oldbufsiz, newbufsiz, err);
	}

	/* Version 1.3 - as suggested by G8BPQ. */
	/* Without this, if you kill the application then try to run it */
	/* again quickly the port number is unavailable for a while. */
	/* Don't try doing the same thing On Windows; It has a different meaning. */
	/* http://stackoverflow.com/questions/14388706/socket-options-so-reuseaddr-and-so-reuseport-how-do-they-differ-do-they-mean-t */

        setsockopt (listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&bcopt, 4);

    	sockaddr.sin_addr.s_addr = INADDR_ANY;
	sockaddr.sin_port = htons(kps->tcp_port);
    	sockaddr.sin_family = AF_INET;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf("Binding to port %d ... \n", kps->tcp_port);
#endif

        if (bind(listen_sock,(struct sockaddr*)&sockaddr,sizeof(sockaddr))  == -1) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", errno);	
          dw_printf("%s\n", strerror(errno));
	  dw_printf("Some other application is probably already using port %d.\n", kps->tcp_port);
	  dw_printf("Try using a different port number with KISSPORT in the configuration file.\n");
          return (NULL);
	}

	getsockname( listen_sock, (struct sockaddr *)(&sockaddr), &sockaddr_size);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
 	dw_printf("opened KISS TCP socket as fd (%d) on port (%d) for stream i/o\n", listen_sock, ntohs(sockaddr.sin_port) );
#endif

 	while (1) {

	  int client;
	  int c;

	  client = -1;
	  for (c = 0; c < MAX_NET_CLIENTS && client < 0; c++) {
	    if (kps->client_sock[c] <= 0) {
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
	    if (kps->chan == -1) {
              dw_printf("Ready to accept KISS TCP client application %d on port %d ...\n", client, kps->tcp_port);
	    }
	    else {
              dw_printf("Ready to accept KISS TCP client application %d on port %d (radio channel %d) ...\n", client, kps->tcp_port, kps->chan);
	    }

            kps->client_sock[client] = accept(listen_sock, (struct sockaddr*)(&sockaddr),&sockaddr_size);

	    text_color_set(DW_COLOR_INFO);
	    if (kps->chan == -1) {
	      dw_printf("\nAttached to KISS TCP client application %d on port %d ...\n\n", client, kps->tcp_port);
	    }
	    else {
	      dw_printf("\nAttached to KISS TCP client application %d on port %d (radio channel %d) ...\n\n", client, kps->tcp_port, kps->chan);
	    }

	    // Reset the state and buffer.
	    memset (&(kps->kf[client]), 0, sizeof(kps->kf[client]));
	  }
	  else {
	    SLEEP_SEC(1);	/* wait then check again if more clients allowed. */
	  }
 	}
#endif
}





/*-------------------------------------------------------------------
 *
 * Name:        kissnet_send_rec_packet
 *
 * Purpose:     Send a packet, received over the radio, to the client app.
 *
 * Inputs:	chan		- Channel number where packet was received.
 *				  0 = first, 1 = second if any.
 *
// TODO: add kiss_cmd
 *
 *		fbuf		- Address of raw received frame buffer
 *				  or a text string.
 *
 *		kiss_cmd	- Usually KISS_CMD_DATA_FRAME but we can also have
 *				  KISS_CMD_SET_HARDWARE when responding to a query.
 *
 *		flen		- Number of bytes for AX.25 frame.
 *				  When called from kiss_rec_byte, flen will be -1
 *				  indicating a text string rather than frame content.
 *				  This is used to fake out an application that thinks
 *				  it is using a traditional TNC and tries to put it
 *				  into KISS mode.
 *
 *		onlykps		- KISS TCP status block pointer or NULL.
 *
 *		onlyclient	- It is possible to have more than client attached
 *				  at the same time with TCP KISS.
 *				  Starting with version 1.7 we can have multiple TCP ports.
 *				  When a frame is received from the radio we normally want it
 *				  to go to all of the clients.
 *				  In this case specify NULL for onlykps and -1 tcp client.
 *				  When responding to a command from the client, we want
 *				  to send only to that one client app.  In this case
 *				  a non NULL kps and onlyclient >= 0.
 *
 * Description:	Send message to client(s) if connected.
 *		Disconnect from client, and notify user, if any error.
 *
 *--------------------------------------------------------------------*/

void kissnet_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf, int flen,
			struct kissport_status_s *onlykps, int onlyclient)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN];
	int kiss_len;
	int err;

// Something received over the radio would normally be sent to all attached clients.
// However, there are times we want to send a response only to a particular client.
// In the case of a serial port or pseudo terminal, there is only one potential client.
// so the response would be sent to only one place.  A new parameter has been added for this.

	for (struct kissport_status_s *kps = all_ports; kps != NULL; kps = kps->pnext) {

	  if (onlykps == NULL || kps == onlykps) {

	    for (int client = 0; client < MAX_NET_CLIENTS; client++) {

	      if (onlyclient == -1 || client == onlyclient) {

	        if (kps->client_sock[client] != -1) {

	          if (flen < 0) {

// A client app might think it is attached to a traditional TNC.
// It might try sending commands over and over again trying to get the TNC into KISS mode.
// We recognize this attempt and send it something to keep it happy.

	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("KISS TCP: Something unexpected from client application.\n");
	            dw_printf ("Is client app treating this like an old TNC with command mode?\n");
	            dw_printf ("This can be caused by the application sending commands to put a\n");
	            dw_printf ("traditional TNC into KISS mode.  It is usually a harmless warning.\n");
	            dw_printf ("For best results, configure for a KISS-only TNC to avoid this.\n");
	            dw_printf ("In the case of APRSISCE/32, use \"Simply(KISS)\" rather than \"KISS.\"\n");

	            flen = strlen((char*)fbuf);
	            if (kiss_debug) {
	              kiss_debug_print (TO_CLIENT, "Fake command prompt", fbuf, flen);
	            }
	            strlcpy ((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
	            kiss_len = strlen((char *)kiss_buff);
	          }
	          else {
	            unsigned char stemp[AX25_MAX_PACKET_LEN + 1];

	            assert (flen < (int)(sizeof(stemp)));

	            // New in 1.7.
	            // Previously all channels were sent to everyone.
	            // We now have tcp ports which carry only a single radio channel.
	            // The application will see KISS channel 0 regardless of the radio channel.

	            if (kps->chan == -1) {
	              // Normal case, all channels.
	              stemp[0] = (chan << 4) | kiss_cmd;
	            }
	            else if (kps->chan == chan) {
	              // Single radio channel for this port.  Application sees 0.
	              stemp[0] = (0 << 4) | kiss_cmd;
	            }
	            else {
	              // Skip it.
	              continue;
	            }

	            memcpy (stemp+1, fbuf, flen);

	            if (kiss_debug >= 2) {
	              /* AX.25 frame with the CRC removed. */
	              text_color_set(DW_COLOR_DEBUG);
	              dw_printf ("\n");
	              dw_printf ("Packet content before adding KISS framing and any escapes:\n");
	              hex_dump (fbuf, flen);
	            }

	            kiss_len = kiss_encapsulate (stemp, flen+1, kiss_buff);

	            /* This has the escapes and the surrounding FENDs. */

	            if (kiss_debug) {
	              kiss_debug_print (TO_CLIENT, NULL, kiss_buff, kiss_len);
	            }
	          }

#if __WIN32__	
                  err = SOCK_SEND(kps->client_sock[client], (char*)kiss_buff, kiss_len);
	          if (err == SOCKET_ERROR) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("\nError %d sending message to KISS client application %d on port %d.  Closing connection.\n\n", WSAGetLastError(), client, kps->tcp_port);
	            closesocket (kps->client_sock[client]);
	            kps->client_sock[client] = -1;
	            WSACleanup();
	          }
#else
                  err = SOCK_SEND (kps->client_sock[client], kiss_buff, kiss_len);
	          if (err <= 0) {
	            text_color_set(DW_COLOR_ERROR);
	            dw_printf ("\nError %d sending message to KISS client application %d on port %d.  Closing connection.\n\n", err, client, kps->tcp_port);
	            close (kps->client_sock[client]);
	            kps->client_sock[client] = -1;
	          }
#endif
	        } // frame length >= 0
	      } // if all clients or the one specifie
	    } // for each client on the tcp port
	  } // if all ports or the one specified
	} // for each tcp port
	
} /* end kissnet_send_rec_packet */


/*-------------------------------------------------------------------
 *
 * Name:        kissnet_copy
 *
 * Purpose:     Send data from one network KISS client to all others.
 *
 * Inputs:	in_msg		- KISS frame data without the framing or escapes.
 *				  The first byte is channel and command (should be data).
 *				  Caller no longer cares this byte.  We will clobber it here.
 *
 *		in_len 		- Number of bytes in above.
 *
 *		chan		- Channel.  Use this instead of first byte of in_msg.
 *
 *		cmd		- KISS command nybble.
 *				  Should be 0 because I'm expecting this only for data.
 *
 *		from_client	- Number of network (TCP) client instance.
 *				  Should be 0, 1, 2, ...
 *
 *
 * Global In:	kiss_copy	- From misc. configuration.
 *				  This enables the feature.
 *
 *
 * Description:	Send message to any attached network KISS clients, other than the one where it came from.
 *		Enable this by putting KISSCOPY in the configuration file.
 *		Note that this applies only to network (TCP) KISS clients, not serial port, or pseudo terminal.
 *
 *
 *--------------------------------------------------------------------*/


void kissnet_copy (unsigned char *in_msg, int in_len, int chan, int cmd, struct kissport_status_s *from_kps, int from_client)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN];
	int err;


	if (s_misc_config_p->kiss_copy) {

	  for (struct kissport_status_s *kps = all_ports; kps != NULL; kps = kps->pnext) {

	    for (int client = 0; client < MAX_NET_CLIENTS;  client++) {

	      if ( ! ( kps == from_kps && client == from_client ) ) {   // To all but origin.

		if (kps->client_sock[client] != -1) {

	          if (kps-> chan == -1 || kps->chan == chan) {

	            // Two different cases here:
	            //  - The TCP port allows all channels, or
	            //  - The TCP port allows only one channel.  In this case set KISS channel to 0.

	            if (kps->chan == -1) {
	              in_msg[0] = (chan << 4) | cmd;
	            }
	            else {
	              in_msg[0] = 0 | cmd;	// set channel to zero.
	            }

	            int kiss_len = kiss_encapsulate (in_msg, in_len, kiss_buff);

	            /* This has the escapes and the surrounding FENDs. */

	            if (kiss_debug) {
	              kiss_debug_print (TO_CLIENT, NULL, kiss_buff, kiss_len);
	            }

#if __WIN32__
                    err = SOCK_SEND(kps->client_sock[client], (char*)kiss_buff, kiss_len);
	            if (err == SOCKET_ERROR) {
	              text_color_set(DW_COLOR_ERROR);
	              dw_printf ("\nError %d copying message to KISS TCP port %d client %d application.  Closing connection.\n\n", WSAGetLastError(), kps->tcp_port, client);
	              closesocket (kps->client_sock[client]);
	              kps->client_sock[client] = -1;
	              WSACleanup();
	            }
#else
                    err = SOCK_SEND (kps->client_sock[client], kiss_buff, kiss_len);
	            if (err <= 0) {
	              text_color_set(DW_COLOR_ERROR);
	              dw_printf ("\nError copying message to KISS TCP port %d client %d application.  Closing connection.\n\n", kps->tcp_port, client);
	              close (kps->client_sock[client]);
	              kps->client_sock[client] = -1;
	            }
#endif
	          } // Channel is allowed on this port.
	        } // socket is open
	      } // if origin and destination different.
	    } // loop over all KISS network clients for one port.
	  } // loop over all KISS TCP ports
	} // Feature enabled.

} /* end kissnet_copy */



/*-------------------------------------------------------------------
 *
 * Name:        kissnet_listen_thread
 *
 * Purpose:     Wait for KISS messages from an application.
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


/* Return one byte (value 0 - 255) */


static int kiss_get (struct kissport_status_s *kps, int client)
{

	while (1) {

	  while (kps->client_sock[client] <= 0) {
	    SLEEP_SEC(1);			/* Not connected.  Try again later. */
	  }

	  /* Just get one byte at a time. */

	  unsigned char ch;
	  int n = SOCK_RECV (kps->client_sock[client], (char *)(&ch), 1);

	  if (n == 1) {
#if DEBUG9
	    dw_printf (log_fp, "%02x %c %c", ch, 
			isprint(ch) ? ch : '.' , 
			(isupper(ch>>1) || isdigit(ch>>1) || (ch>>1) == ' ') ? (ch>>1) : '.');
	    if (ch == FEND) fprintf (log_fp, "  FEND");
	    if (ch == FESC) fprintf (log_fp, "  FESC");
	    if (ch == TFEND) fprintf (log_fp, "  TFEND");
	    if (ch == TFESC) fprintf (log_fp, "  TFESC");
	    if (ch == '\r') fprintf (log_fp, "  CR");
	    if (ch == '\n') fprintf (log_fp, "  LF");
	    fprintf (log_fp, "\n");
	    if (ch == FEND) fflush (log_fp);
#endif
	    return(ch);	
	  }

          text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nKISS client application %d on TCP port %d has gone away.\n\n", client, kps->tcp_port);
#if __WIN32__
	  closesocket (kps->client_sock[client]);
#else
	  close (kps->client_sock[client]);
#endif
	  kps->client_sock[client] = -1;
	}
}



static THREAD_F kissnet_listen_thread (void *arg)
{
	struct kissport_status_s *kps = arg;

	int client = kps->arg2;
	assert (client >= 0 && client < MAX_NET_CLIENTS);

	kps->arg2 = -1;		// Indicates thread is running so
				// arg2 can be reused for the next one.

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_listen_thread ( tcp_port = %d, client = %d, socket fd = %d )\n", kps->tcp_port, client, kps->client_sock[client]);
#endif



// So why is kissnet_send_rec_packet mentioned here for incoming from the client app?
// The logic exists for the serial port case where the client might think it is
// attached to a traditional TNC.  It might try sending commands over and over again
// trying to get the TNC into KISS mode.  To keep it happy, we recognize this attempt
// and send it something to keep it happy.
// In the case of a serial port or pseudo terminal, there is only one potential client
// so the response would be sent to only one place.
// Starting in version 1.5, this now can have multiple attached clients.  We wouldn't
// want to send the response to all of them.   Actually, we should be providing only
// "Simply KISS" as some call it.


	while (1) {
	  unsigned char ch = kiss_get(kps, client);
	  kiss_rec_byte (&(kps->kf[client]), ch, kiss_debug, kps, client, kissnet_send_rec_packet);
	}  

#if __WIN32__
	return(0);
#else
	return (THREAD_F) 0;	/* Unreachable but avoids compiler warning. */
#endif

} /* end kissnet_listen_thread */

/* end kissnet.c */

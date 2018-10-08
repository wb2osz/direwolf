//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011-2014, 2015, 2017  John Langner, WB2OSZ
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


#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "kissnet.h"
#include "kiss_frame.h"
#include "xmit.h"

void hex_dump (unsigned char *p, int len);	// This should be in a .h file.


/*
 * Early on we allowed one AGW connection and one KISS TCP connection at a time.
 * In version 1.1, we allowed multiple concurrent client apps to attach with the AGW network protocol.
 * In Version 1.5, we do essentially the same here to allow multiple concurrent KISS TCP clients.
 * The default is a limit of 3 client applications at the same time.
 * You can increase the limit by changing the line below.
 * A larger number consumes more resources so don't go crazy by making it larger than needed.
 */

#define MAX_NET_CLIENTS 3

static int client_sock[MAX_NET_CLIENTS];
				/* File descriptor for socket for */
				/* communication with client application. */
				/* Set to -1 if not connected. */
				/* (Don't use SOCKET type because it is unsigned.) */

static kiss_frame_t kf[MAX_NET_CLIENTS];
				/* Accumulated KISS frame and state of decoder. */



// TODO:  define in one place, use everywhere.
#if __WIN32__
#define THREAD_F unsigned __stdcall
#else
#define THREAD_F void *
#endif

static THREAD_F connect_listen_thread (void *arg);
static THREAD_F kissnet_listen_thread (void *arg);



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
 *
 * Inputs:	mc->kiss_port	- TCP port for server.
 *				  Main program has default of 8000 but allows
 *				  an alternative to be specified on the command line
 *
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


void kissnet_init (struct misc_config_s *mc)
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
	int kiss_port = mc->kiss_port;		/* default 8001 but easily changed. */


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_init ( %d )\n", kiss_port);
#endif

	
	for (client=0; client<MAX_NET_CLIENTS; client++) {
	  client_sock[client] = -1;
	  memset (&(kf[client]), 0, sizeof(kf[client]));
	}

	if (kiss_port == 0) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Disabled KISS network client port.\n");
	  return;
	}
	
/*
 * This waits for a client to connect and sets client_sock[n].
 */
#if __WIN32__
	connect_listen_th = (HANDLE)_beginthreadex (NULL, 0, connect_listen_thread, (void *)kiss_port, 0, NULL);
	if (connect_listen_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create KISS socket connect listening thread\n");
	  return;
	}
#else
	e = pthread_create (&connect_listen_tid, NULL, connect_listen_thread, (void *)(long)kiss_port);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create KISS socket connect listening thread");
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
	  cmd_listen_th[client] = (HANDLE)_beginthreadex (NULL, 0, kissnet_listen_thread, (void*)client, 0, NULL);
	  if (cmd_listen_th[client] == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create KISS command listening thread for client %d\n", client);
	    return;
	  }
#else
	  e = pthread_create (&(cmd_listen_tid[client]), NULL, kissnet_listen_thread, (void *)(long)client);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Could not create KISS command listening thread for client %d\n", client);
	    // Replace add perror with better message handling.
	    perror("");
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
 *				  Main program has default of 8001 but allows
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
	char kiss_port_str[12];

	SOCKET listen_sock;  
	WSADATA wsadata;

	snprintf (kiss_port_str, sizeof(kiss_port_str), "%d", (int)(long)arg);
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("DEBUG: kissnet port = %d = '%s'\n", (int)(long)arg, kiss_port_str);
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

	err = getaddrinfo(NULL, kiss_port_str, &hints, &ai);
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
    	dw_printf("Binding to port %s ... \n", kiss_port_str);
#endif

	err = bind( listen_sock, ai->ai_addr, (int)ai->ai_addrlen);
	if (err == SOCKET_ERROR) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", WSAGetLastError());		// TODO: provide corresponding text.
	  dw_printf("Some other application is probably already using port %s.\n", kiss_port_str);
	  dw_printf("Try using a different port number with KISSPORT in the configuration file.\n");
          freeaddrinfo(ai);
          closesocket(listen_sock);
          WSACleanup();
          return (0);
        }

	freeaddrinfo(ai);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
 	dw_printf("opened KISS socket as fd (%d) on port (%s) for stream i/o\n", listen_sock, kiss_port_str );
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
            dw_printf("Ready to accept KISS TCP client application %d on port %s ...\n", client, kiss_port_str);
         
            client_sock[client] = accept(listen_sock, NULL, NULL);

	    if (client_sock[client] == -1) {
	      text_color_set(DW_COLOR_ERROR);
              dw_printf("Accept failed with error: %d\n", WSAGetLastError());
              closesocket(listen_sock);
              WSACleanup();
              return (0);
            }

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("\nAttached to KISS TCP client application %d ...\n\n", client);

	    // Reset the state and buffer.
	    memset (&(kf[client]), 0, sizeof(kf[client]));
	  }
	  else {
	    SLEEP_SEC(1);	/* wait then check again if more clients allowed. */
	  }
 	}


#else		/* End of Windows case, now Linux. */


    	struct sockaddr_in sockaddr; /* Internet socket address stuct */
    	socklen_t sockaddr_size = sizeof(struct sockaddr_in);
	int kiss_port = (int)(long)arg;
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
    	sockaddr.sin_port = htons(kiss_port);
    	sockaddr.sin_family = AF_INET;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
    	dw_printf("Binding to port %d ... \n", kiss_port);
#endif

        if (bind(listen_sock,(struct sockaddr*)&sockaddr,sizeof(sockaddr))  == -1) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Bind failed with error: %d\n", errno);	
          dw_printf("%s\n", strerror(errno));
	  dw_printf("Some other application is probably already using port %d.\n", kiss_port);
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
            dw_printf("Ready to accept KISS TCP client application %d on port %d ...\n", client, kiss_port);
         
            client_sock[client] = accept(listen_sock, (struct sockaddr*)(&sockaddr),&sockaddr_size);

	    text_color_set(DW_COLOR_INFO);
	    dw_printf("\nAttached to KISS TCP client application %d...\n\n", client);

	    // Reset the state and buffer.
	    memset (&(kf[client]), 0, sizeof(kf[client]));
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
 * Purpose:     Send a received packet to the client app.
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
 *		tcpclient	- It is possible to have more than client attached
 *				  at the same time with TCP KISS.
 *				  When a frame is received from the radio we want it
 *				  to go to all of the clients.  In this case specify -1.
 *				  When responding to a command from the client, we want
 *				  to send only to that one client app.  In this case
 *				  use the value 0 .. MAX_NET_CLIENTS-1.
 *
 * Description:	Send message to client(s) if connected.
 *		Disconnect from client, and notify user, if any error.
 *
 *--------------------------------------------------------------------*/


void kissnet_send_rec_packet (int chan, int kiss_cmd, unsigned char *fbuf, int flen, int tcpclient)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN];
	int kiss_len;
	int err;
	int first, last, client;

// Something received over the radio would be sent to all attached clients.
// However, there are times we want to send a response only to a particular client.
// In the case of a serial port or pseudo terminal, there is only one potential client.
// so the response would be sent to only one place.  A new parameter has been added for this.

	if (tcpclient >= 0 && tcpclient < MAX_NET_CLIENTS) {
	  first = tcpclient;
	  last = tcpclient;
	}
	else if (tcpclient == -1) {
	  first = 0;
	  last = MAX_NET_CLIENTS - 1;
	}
	else {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("KISS TCP: Internal error, kissnet_send_rec_packet, tcpclient = %d.\n", tcpclient);
	  return;
	}


	for (client = first; client <= last; client++) {

	  if (client_sock[client] != -1) {

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

	      stemp[0] = (chan << 4) | kiss_cmd;
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
            err = SOCK_SEND(client_sock[client], (char*)kiss_buff, kiss_len);
	    if (err == SOCKET_ERROR)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError %d sending message to KISS client %d application.  Closing connection.\n\n", WSAGetLastError(), client);
	      closesocket (client_sock[client]);
	      client_sock[client] = -1;
	      WSACleanup();
	    }
#else
            err = SOCK_SEND (client_sock[client], kiss_buff, kiss_len);
	    if (err <= 0)
	    {
	      text_color_set(DW_COLOR_ERROR);
	      dw_printf ("\nError sending message to KISS client %d application.  Closing connection.\n\n", client);
	      close (client_sock[client]);
	      client_sock[client] = -1;
	    }
#endif
	  }
	}
	
} /* end kissnet_send_rec_packet */



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


static int kiss_get (int client)
{
	unsigned char ch;
	int n;

	while (1) {

	  while (client_sock[client] <= 0) {
	    SLEEP_SEC(1);			/* Not connected.  Try again later. */
	  }

	  /* Just get one byte at a time. */

	  n = SOCK_RECV (client_sock[client], (char *)(&ch), 1);

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
	  dw_printf ("\nKISS client application %d has gone away.\n\n", client);
#if __WIN32__
	  closesocket (client_sock[client]);
#else
	  close (client_sock[client]);
#endif
	  client_sock[client] = -1;
	}
}



static THREAD_F kissnet_listen_thread (void *arg)
{
	unsigned char ch;
			

	int client = (int)(long)arg;

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_listen_thread ( client = %d, socket fd = %d )\n", client, client_sock[client]);
#endif

	assert (client >= 0 && client < MAX_NET_CLIENTS);


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
	  ch = kiss_get(client);
	  kiss_rec_byte (&(kf[client]), ch, kiss_debug, client, kissnet_send_rec_packet);
	}  

#if __WIN32__
	return(0);
#else
	return (THREAD_F) 0;	/* Unreachable but avoids compiler warning. */
#endif

} /* end kissnet_listen_thread */

/* end kissnet.c */

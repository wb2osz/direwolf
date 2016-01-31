//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2011-2014, 2015  John Langner, WB2OSZ
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
 *			0	Data Frame	AX.25 frame in raw format.
 *
 *			1	TXDELAY		See explanation in xmit.c.
 *
 *			2	Persistence	"	"
 *
 *			3 	SlotTime	"	"
 *
 *			4	TXtail		"	"
 *						Spec says it is obsolete but Xastir
 *						sends it and we respect it.
 *
 *			5	FullDuplex	Ignored.  Always full duplex.
 *		
 *			6	SetHardware	TNC specific.  Ignored.
 *			
 *			FF	Return		Exit KISS mode.  Ignored.
 *
 *
 *		Messages sent to client application:
 *
 *			0	Data Frame	Received AX.25 frame in raw format.
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


#include "direwolf.h"
#include "tq.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "audio.h"
#include "kissnet.h"
#include "kiss_frame.h"
#include "xmit.h"


static kiss_frame_t kf;		/* Accumulated KISS frame and state of decoder. */
				// TODO: multiple instances if multiple KISS network clients!


static int client_sock;		/* File descriptor for socket for */
				/* communication with client application. */
				/* Set to -1 if not connected. */
				/* (Don't use SOCKET type because it is unsigned.) */


static void * connect_listen_thread (void *arg);
static void * kissnet_listen_thread (void *arg);



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
#if __WIN32__
	HANDLE connect_listen_th;
	HANDLE cmd_listen_th;
#else
	pthread_t connect_listen_tid;
	pthread_t cmd_listen_tid;
#endif
	int e;
	int kiss_port = mc->kiss_port;


#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_init ( %d )\n", kiss_port);
#endif

	memset (&kf, 0, sizeof(kf));
	
	client_sock = -1;

	if (kiss_port == 0) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Disabled KISS network client port.\n");
	  return;
	}
	
/*
 * This waits for a client to connect and sets client_sock.
 */
#if __WIN32__
	connect_listen_th = _beginthreadex (NULL, 0, connect_listen_thread, (void *)kiss_port, 0, NULL);
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
 * This reads messages from client when client_sock is valid.
 */
#if __WIN32__
	cmd_listen_th = _beginthreadex (NULL, 0, kissnet_listen_thread, NULL, 0, NULL);
	if (cmd_listen_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Could not create KISS socket command listening thread\n");
	  return;
	}
#else
	e = pthread_create (&cmd_listen_tid, NULL, kissnet_listen_thread, NULL);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Could not create KISS socket command listening thread");
	  return;
	}
#endif
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

static void * connect_listen_thread (void *arg)
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
	    return (NULL);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Could not find a usable version of Winsock.dll\n");
          WSACleanup();
	  //sleep (1);
          return (NULL);
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
	    return (NULL);
	}

	listen_sock= socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
	if (listen_sock == INVALID_SOCKET) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("connect_listen_thread: Socket creation failed, err=%d", WSAGetLastError());
	  return (NULL);
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
          return (NULL);
        }

	freeaddrinfo(ai);

#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
 	dw_printf("opened KISS socket as fd (%d) on port (%s) for stream i/o\n", listen_sock, kiss_port_str );
#endif

 	while (1) {
  	 
	  while (client_sock > 0) {
	    SLEEP_SEC(1);			/* Already connected.  Try again later. */
	  }

#define QUEUE_SIZE 5

	  if(listen(listen_sock,QUEUE_SIZE) == SOCKET_ERROR)
	  {
	    text_color_set(DW_COLOR_ERROR);
            dw_printf("Listen failed with error: %d\n", WSAGetLastError());
	    return (NULL);
	  }
	
	  text_color_set(DW_COLOR_INFO);
          dw_printf("Ready to accept KISS client application on port %s ...\n", kiss_port_str);
         
          client_sock = accept(listen_sock, NULL, NULL);

	  if (client_sock == -1) {
	    text_color_set(DW_COLOR_ERROR);
            dw_printf("Accept failed with error: %d\n", WSAGetLastError());
            closesocket(listen_sock);
            WSACleanup();
            return (NULL);
          }

	  text_color_set(DW_COLOR_INFO);
	  dw_printf("\nConnected to KISS client application ...\n\n");

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
 	dw_printf("opened KISS socket as fd (%d) on port (%d) for stream i/o\n", listen_sock, ntohs(sockaddr.sin_port) );
#endif

 	while (1) {
  	 
	  while (client_sock > 0) {
	    SLEEP_SEC(1);			/* Already connected.  Try again later. */
	  }

#define QUEUE_SIZE 5

	  if(listen(listen_sock,QUEUE_SIZE) == -1)
	  {
	    text_color_set(DW_COLOR_ERROR);
	    perror ("connect_listen_thread: Listen failed");
	    return (NULL);
	  }
	
	  text_color_set(DW_COLOR_INFO);
          dw_printf("Ready to accept KISS client application on port %d ...\n", kiss_port);
         
          client_sock = accept(listen_sock, (struct sockaddr*)(&sockaddr),&sockaddr_size);

	  text_color_set(DW_COLOR_INFO);
	  dw_printf("\nConnected to KISS client application ...\n\n");

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
 *		fbuf		- Address of raw received frame buffer
 *				  or a text string.
 *
 *		flen		- Number of bytes for AX.25 frame.
 *				  or -1 for a text string.
 *		
 *
 * Description:	Send message to client if connected.
 *		Disconnect from client, and notify user, if any error.
 *
 *--------------------------------------------------------------------*/


void kissnet_send_rec_packet (int chan, unsigned char *fbuf, int flen)
{
	unsigned char kiss_buff[2 * AX25_MAX_PACKET_LEN];
	int kiss_len;
	int j;
	int err;


	if (client_sock == -1) {
	  return;
	}
	if (flen < 0) {
	  flen = strlen((char*)fbuf);
	  if (kiss_debug) {
	    kiss_debug_print (TO_CLIENT, "Fake command prompt", fbuf, flen);
	  }
	  strlcpy ((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
	  kiss_len = strlen((char *)kiss_buff);
	}
	else {


	  unsigned char stemp[AX25_MAX_PACKET_LEN + 1];
	 
	  assert (flen < sizeof(stemp));

	  stemp[0] = (chan << 4) + 0;
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
        err = send (client_sock, (char*)kiss_buff, kiss_len, 0);
	if (err == SOCKET_ERROR)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending message to KISS client application.  Closing connection.\n\n", WSAGetLastError());
	  closesocket (client_sock);
	  client_sock = -1;
	  WSACleanup();
	}
#else
        err = write (client_sock, kiss_buff, kiss_len);
	if (err <= 0)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError sending message to KISS client application.  Closing connection.\n\n");
	  close (client_sock);
	  client_sock = -1;    
	}
#endif
	
} /* end kissnet_send_rec_packet */



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
 * 		Not really needed for KISS because we are dealing with
 *		a stream of bytes rather than message blocks.
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
 * Name:        kissnet_listen_thread
 *
 * Purpose:     Wait for KISS messages from an application.
 *
 * Inputs:	arg		- Not used.
 *
 * Outputs:	client_sock	- File descriptor for communicating with client app.
 *
 * Description:	Process messages from the client application.
 *		Note that the client can go away and come back again and
 *		re-establish communication without restarting this application.
 *
 *--------------------------------------------------------------------*/


/* Return one byte (value 0 - 255) */


static int kiss_get (void)
{
	unsigned char ch;
	int n;

	while (1) {

	  while (client_sock <= 0) {
	    SLEEP_SEC(1);			/* Not connected.  Try again later. */
	  }

	  /* Just get one byte at a time. */

	  n = read_from_socket (client_sock, (char *)(&ch), 1);

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
	  dw_printf ("\nError reading KISS byte from clent application.  Closing connection.\n\n");
#if __WIN32__
	  closesocket (client_sock);
#else
	  close (client_sock);
#endif
	  client_sock = -1;
	}
}



static void * kissnet_listen_thread (void *arg)
{
	unsigned char ch;
			
#if DEBUG
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("kissnet_listen_thread ( socket = %d )\n", client_sock);
#endif

	while (1) {
	  ch = kiss_get();
	  kiss_rec_byte (&kf, ch, kiss_debug, kissnet_send_rec_packet);
	}  

	return (NULL);	/* to suppress compiler warning. */

} /* end kissnet_listen_thread */

/* end kissnet.c */

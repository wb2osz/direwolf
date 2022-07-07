//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2015  John Langner, WB2OSZ
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
 * Module:      ttcalc.c
 *
 * Purpose:   	Simple Touch Tone to Speech calculator.
 *		
 * Description:	Demonstration of how Dire Wolf can be used
 *		as a DTMF / Speech interface for ham radio applications.
 *
 * Usage:	Start up direwolf with configuration:
 *			- DTMF decoder enabled.
 *			- Text-to-speech enabled.
 *			- Listening to standard port 8000 for a client application.
 *
 *		Run this in a different window.
 *
 *		User sends formulas such as:
 *
 *			2 * 3 * 4 #
 *
 *		with the touch tone pad.
 *		The result is sent back with speech, e.g. "Twenty Four."
 *		
 *---------------------------------------------------------------*/


#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__

#include <winsock2.h>
#include <ws2tcpip.h>  		// _WIN32_WINNT must be set to 0x0501 before including this
#else 
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"


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


static int calculator (char *str);
static int connect_to_server (char *hostname, char *port);
static char * ia_to_text (int  Family, void * pAddr, char * pStringBuf, size_t StringBufSize);



/*------------------------------------------------------------------
 *
 * Name: 	main
 *
 *---------------------------------------------------------------*/



int main (int argc, char *argv[])
{

	int server_sock = -1;
	struct agwpe_s mon_cmd;
	char data[1024];
	char hostname[30] = "localhost";
	char port[10] = "8000";
	int err;

#if __WIN32__
#else
 	setlinebuf (stdout);
#endif

	assert (calculator("12a34#") == 46);
	assert (calculator("2*3A4#") == 10);
	assert (calculator("5*100A3#") == 503);
	assert (calculator("6a4*5#") == 50);

/*
 * Try to attach to Dire Wolf.
 */

	server_sock = connect_to_server (hostname, port);


	if (server_sock == -1) {
	  exit (1);
	}

/*
 * Send command to toggle reception of frames in raw format.
 *
 * Note: Monitor format is only for UI frames.
 */

	memset (&mon_cmd, 0, sizeof(mon_cmd));

	mon_cmd.kind_lo = 'k';

	err = SOCK_SEND (server_sock, (char*)(&mon_cmd), sizeof(mon_cmd));
	(void)err;

/*
 * Print all of the monitored packets.
 */

	while (1) {
	  int n;

	  n = SOCK_RECV (server_sock, (char*)(&mon_cmd), sizeof(mon_cmd));

	  if (n != sizeof(mon_cmd)) {
	    printf ("Read error, received %d command bytes.\n", n);
	    exit (1);
	  }

	  assert (mon_cmd.data_len >= 0 && mon_cmd.data_len < (int)(sizeof(data)));

	  if (mon_cmd.data_len > 0) {
	    n = SOCK_RECV (server_sock, data, mon_cmd.data_len);

	    if (n != mon_cmd.data_len) {
	      printf ("Read error, client received %d data bytes when %d expected.  Terminating.\n", n, mon_cmd.data_len);
	      exit (1);
	    }
	  }

/* 
 * Print it.
 */

	  if (mon_cmd.kind_lo == 'K') {
	    packet_t pp;
	    char *pinfo;
	    int info_len;
	    char result[400];
	    char *p;
	    alevel_t alevel;
	    int chan;

	    chan = mon_cmd.portx;
	    memset (&alevel, 0, sizeof(alevel));
	    pp = ax25_from_frame ((unsigned char *)(data+1), mon_cmd.data_len-1, alevel);
	    ax25_format_addrs (pp, result);
	    info_len = ax25_get_info (pp, (unsigned char **)(&pinfo));
	    pinfo[info_len] = '\0';
	    strlcat (result, pinfo, sizeof(result));
	    for (p=result; *p!='\0'; p++) {
	      if (! isprint(*p)) *p = ' ';
	    }

	    printf ("[%d] %s\n", chan, result);

	    	
/*
 * Look for Special touch tone packet with "t" in first position of the Information part.
 */

	    if (*pinfo == 't') {
	
	      int n;
	      char reply_text[200];
	      packet_t reply_pp;
	      struct {
	        struct agwpe_s hdr;
		char extra;
	        unsigned char frame[AX25_MAX_PACKET_LEN];
	      } xmit_raw;

/*
 * Send touch tone sequence to calculator and get the answer.
 *
 * Put your own application here instead.  Here are some ideas:
 *
 *  http://www.tapr.org/pipermail/aprssig/2015-January/044069.html
 */
	      n = calculator (pinfo+1);
	      printf ("\nCalculator returns %d\n\n", n);

/*
 * Convert to AX.25 frame.
 * Notice that the special destination will cause it to be spoken.
 */
	      snprintf (reply_text, sizeof(reply_text), "N0CALL>SPEECH:%d", n);
	      reply_pp = ax25_from_text(reply_text, 1);

/*
 * Send it to the TNC.
 * In this example we are transmitting speech on the same channel
 * where the tones were heard.  We could also send AX.25 frames to 
 * other radio channels.
 */
	      memset (&xmit_raw, 0, sizeof(xmit_raw));

	      xmit_raw.hdr.portx = chan;
	      xmit_raw.hdr.kind_lo = 'K';
	      xmit_raw.hdr.data_len = 1 + ax25_pack (reply_pp, xmit_raw.frame);

	      err = SOCK_SEND (server_sock, (char*)(&xmit_raw), sizeof(xmit_raw.hdr)+xmit_raw.hdr.data_len);
	      ax25_delete (reply_pp);
	    }

    
	    ax25_delete (pp);

	  }
	}

} /* main */


/*------------------------------------------------------------------
 *
 * Name: 	calculator
 * 
 * Purpose:	Simple calculator to demonstrate Touch Tone to Speech 
 *		application tool kit.
 *
 * Inputs:	str	- Sequence of touch tone characters: 0-9 A-D * #
 *			  It should be terminated with #.
 *
 * Returns:	Numeric result of calculation.
 *
 * Description:	This is a simple calculator that recognizes 
 *			numbers,
 *			* for multiply 
 *			A for add
 *			# for equals result
 *
 *		Adding functions to B, C, and D is left as an
 *		exercise for the reader.
 *
 * Examples:	2 * 3 A 4 #			Ten
 *		5 * 1 0 0 A 3 #			Five Hundred Three
 *
 *---------------------------------------------------------------*/

#define DO_LAST_OP \
	switch (lastop) { \
	  case NONE: result  = num; num = 0; break; \
	  case ADD:  result += num; num = 0; break; \
	  case SUB:  result -= num; num = 0; break; \
	  case MUL:  result *= num; num = 0; break; \
	  case DIV:  result /= num; num = 0; break; \
	}

static int calculator (char *str) 
{
	int result;
	int num;
	enum { NONE, ADD, SUB, MUL, DIV } lastop;
	char *p;

	result = 0;
	num = 0;
	lastop = NONE;

	for (p = str; *p != '\0'; p++) {
	  if (isdigit(*p)) {
	    num = num * 10 + *p - '0';
	  }
	  else if (*p == '*') {
	    DO_LAST_OP;
	    lastop = MUL;
	  }
	  else if (*p == 'A' || *p == 'a') {
	    DO_LAST_OP;
	    lastop = ADD;
	  }
	  else if (*p == '#') {
	    DO_LAST_OP;
	    return (result);
	  }
	}
	return (result);  // not expected.
}


/*------------------------------------------------------------------
 *
 * Name: 	connect_to_server
 * 
 * Purpose:	Connect to Dire Wolf TNC server.
 *
 * Inputs:	hostname
 *		port
 *
 * Returns:	File descriptor or -1 for error.
 *
 *---------------------------------------------------------------*/

static int connect_to_server (char *hostname, char *port)
{


#if __WIN32__
#else
	//int e;
#endif

#define MAX_HOSTS 30

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
/* 
 * File descriptor for socket to server. 
 * Set to -1 if not connected. 
 * (Don't use SOCKET type because it is unsigned.) 
*/
	int server_sock = -1;	

#if __WIN32__
	err = WSAStartup (MAKEWORD(2,2), &wsadata);
	if (err != 0) {
	    printf("WSAStartup failed: %d\n", err);
	    exit (1);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
          printf("Could not find a usable version of Winsock.dll\n");
          WSACleanup();
          exit (1);
	}
#endif

	memset (&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;	/* Allow either IPv4 or IPv6. */
	// hints.ai_family = AF_INET;	/* IPv4 only. */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

/*
 * Connect to specified hostname & port.
 */

	ai_head = NULL;
	err = getaddrinfo(hostname, port, &hints, &ai_head);
	if (err != 0) {
#if __WIN32__
	  printf ("Can't get address for server %s, err=%d\n", hostname, WSAGetLastError());
#else 
	  printf ("Can't get address for server %s, %s\n", hostname, gai_strerror(err));
#endif
	  freeaddrinfo(ai_head);
      	  exit (1);
	}


	num_hosts = 0;
	for (ai = ai_head; ai != NULL; ai = ai->ai_next) {

	  hosts[num_hosts] = ai;
	  if (num_hosts < MAX_HOSTS) num_hosts++;
	}

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

	  err = connect(is, ai->ai_addr, (int)ai->ai_addrlen);
#if __WIN32__
	  if (err == SOCKET_ERROR) {
	    closesocket (is);
	    is = -1;
	    continue;
	  }
#else
	  if (err != 0) {
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

/* 
 * Success. 
 */

 	  printf("Client app now connected to %s (%s), port %s\n", hostname, ipaddr_str, port);
	  server_sock = is;
	  break;
	}

	freeaddrinfo(ai_head);

	if (server_sock == -1) {
 	  printf("Unnable to connect to %s (%s), port %s\n", hostname, ipaddr_str, port);
	 
	}

	return (server_sock);

} /* end connect_to_server */


/*------------------------------------------------------------------
 *
 * Name: 	ia_to_text
 * 
 * Purpose:	Convert Internet address to text.
 *		Can't use InetNtop because it is supported only 
 *		on Windows Vista and later. 
 *
 *---------------------------------------------------------------*/


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

	return pStringBuf;

} /* end ia_to_text */


/* end ttcalc.c */

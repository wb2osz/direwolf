
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
 * Module:      dwsock.c
 *
 * Purpose:   	Functions for TCP sockets.
 *		
 * Description:	These are used for connecting between different applications,
 *		possibly on different hosts.
 *
 * New in version 1.5:
 *		Duplicate code already exists in multiple places and I was about
 *		to add another one.  Instead, we will gather the common code here
 *		instead of having yet another copy.
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
#include <fcntl.h>
//#include <termios.h>
#include <errno.h>

#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "textcolor.h"
#include "dwsock.h"

static void shuffle (struct addrinfo *host[], int nhosts);


/*-------------------------------------------------------------------
 *
 * Name:        dwsock_init
 *
 * Purpose:     Preparation before using socket interface. 
 *
 * Inputs:	none
 *
 * Returns:	0 for success, -1 for error.
 *
 * Errors:	Message is printed.  I've never seen it fail.
 *
 * Description:	Doesn't do anything for Linux.
 *
 * TODO:	Use this instead of own copy in aclients.c
 * TODO:	Use this instead of own copy in appserver.c
 * TODO:	Use this instead of own copy in audio_win.c
 * TODO:	Use this instead of own copy in igate.c
 * TODO:	Use this instead of own copy in kissnet.c
 * TODO:	Use this instead of own copy in kissutil.c
 * TODO:	Use this instead of own copy in server.c
 * TODO:	Use this instead of own copy in tnctest.c
 * TODO:	Use this instead of own copy in ttcalc.c
 *
 *--------------------------------------------------------------------*/

int dwsock_init(void)
{
#if __WIN32__
	WSADATA wsadata;
	int err;

	err = WSAStartup (MAKEWORD(2,2), &wsadata);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf("WSAStartup failed, error: %d\n", err);
	  return (-1);
	}

	if (LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) {
	  text_color_set(DW_COLOR_ERROR);
          dw_printf("Could not find a usable version of Winsock.dll\n");
          WSACleanup();
          return (-1);
	}
#endif
	return (0);

} /* end dwsock_init */



/*-------------------------------------------------------------------
 *
 * Name:        sock_connect
 *
 * Purpose:     Connect to given host / port.  
 *
 * Inputs:	hostname	- Host name or IP address.
 *
 *		port		- TCP port as text string.
 *
 *		description	- Description of the remote server to be used in error message.
 *				  e.g.   "APRS-IS (Igate) Server" or "TCP KISS TNC".
 *
 *		allow_ipv6	- True to allow IPv6.  Otherwise only IPv4.
 *
 *		debug		- Print debugging information.
 *
 * Outputs:	ipaddr_str	- The IP address, in text form, is placed here in case
 *				  the caller wants it.  Should be DWSOCK_IPADDR_LEN bytes.
 *
 * Returns:	Socket Handle / file descriptor or -1 for error.
 *
 * Errors:	(1) Can't find address for given host name.
 *
 *			Print error and return -1.
 *
 *		(2) Can't connect to one of the address(es).
 *
 *		 	Silently try the next one.
 *
 *		(3) Can't connect to any of the address(es).
 *
 *		Nothing is printed for success.  The caller might do that
 *		to provide confirmation on what is happening.
 *
 *--------------------------------------------------------------------*/

int dwsock_connect (char *hostname, char *port, char *description, int allow_ipv6, int debug, char ipaddr_str[DWSOCK_IPADDR_LEN])
{
#define MAX_HOSTS 50

	struct addrinfo hints;
	struct addrinfo *ai_head = NULL;
	struct addrinfo *ai;
	struct addrinfo *hosts[MAX_HOSTS];
	int num_hosts, n;
	int err;
	int server_sock = -1;

	strlcpy (ipaddr_str, "???", DWSOCK_IPADDR_LEN);
	memset (&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;	/* Allow either IPv4 or IPv6. */
	if ( ! allow_ipv6) {
	  hints.ai_family = AF_INET;	/* IPv4 only. */
	}
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

/*
 * First, we need to look up the DNS name to get IP address.
 * It is possible to have multiple addresses.
 */

	ai_head = NULL;
	err = getaddrinfo(hostname, port, &hints, &ai_head);
	if (err != 0) {
	  text_color_set(DW_COLOR_ERROR);
#if __WIN32__
	  dw_printf ("Can't get address for %s, %s, err=%d\n", 
					description, hostname, WSAGetLastError());
#else 
	  dw_printf ("Can't get address for %s, %s, %s\n", 
					description, hostname, gai_strerror(err));
#endif
	  freeaddrinfo(ai_head);
      	  return (-1);
	}

	if (debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("getaddrinfo returns:\n");
	}

	num_hosts = 0;
	for (ai = ai_head; ai != NULL; ai = ai->ai_next) {

	  if (debug) {
	    text_color_set(DW_COLOR_DEBUG);
	    dwsock_ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, DWSOCK_IPADDR_LEN);
	    dw_printf ("    %s\n", ipaddr_str);
	  }

	  hosts[num_hosts] = ai;
	  if (num_hosts < MAX_HOSTS) num_hosts++;
	}

	shuffle (hosts, num_hosts);

	if (debug) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("addresses for hostname:\n");
	  for (n=0; n<num_hosts; n++) {
	    dwsock_ia_to_text (hosts[n]->ai_family, hosts[n]->ai_addr, ipaddr_str, DWSOCK_IPADDR_LEN);
	    dw_printf ("    %s\n", ipaddr_str);
	  }
	}

/*
 * Try each address until we find one that is successful.
 */
	for (n = 0; n < num_hosts; n++) {
#if __WIN32__
	  SOCKET is;
#else
	  int is;
#endif
	  ai = hosts[n];

	  dwsock_ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, DWSOCK_IPADDR_LEN);
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
					description, hostname, ipaddr_str, port);
#endif
	    closesocket (is);
	    is = -1;
	    continue;
	  }
#else
	  if (err != 0) {
#if DEBUGx
	    printf("Connect to %s on %s (%s), port %s failed.\n",
					description, hostname, ipaddr_str, port);
#endif
	    (void) close (is);
	    is = -1;
	    continue;
	  }

	  /* IGate documentation says to use no delay.  */
	  /* Does it really make a difference? */
	  int flag = 1;
	  err = setsockopt (is, IPPROTO_TCP, TCP_NODELAY, (void*)(long)(&flag), (socklen_t)sizeof(flag));
	  if (err < 0) {
	    printf("setsockopt TCP_NODELAY failed.\n");
	  }
#endif

/* Success. */


	  server_sock = is;
#endif	  
	  break;
	}

	freeaddrinfo(ai_head);

// no, caller should handle this.
// function should be generally be silent unless debug option.

	if (server_sock == -1) {
	  text_color_set(DW_COLOR_ERROR);
 	  dw_printf("Unable to connect to %s at %s (%s), port %s\n", 
			description, hostname, ipaddr_str, port );
	}

	return (server_sock);

} /* end dwsock_connect */



/*-------------------------------------------------------------------
 *
 * Name:        dwsock_bind
 *
 * Purpose:     We also have a bunch of duplicate code for the server side.
 *
 * Inputs:	
 *
 * TODO:	Use this instead of own copy in audio.c
 * TODO:	Use this instead of own copy in audio_portaudio.c
 * TODO:	Use this instead of own copy in audio_win.c
 * TODO:	Use this instead of own copy in kissnet.c
 * TODO:	Use this instead of own copy in server.c
 *	
 *--------------------------------------------------------------------*/

// Not implemented yet.


/*
 * Addresses don't get mixed up very well.
 * IPv6 always shows up last so we'd probably never
 * end up using any of them for APRS-IS server.
 * Add our own shuffle.
 */

static void shuffle (struct addrinfo *host[], int nhosts)
{
        int j, k;

        assert (RAND_MAX >= nhosts);  /* for % to work right */

        if (nhosts < 2) return;

        srand (time(NULL));

        for (j=0; j<nhosts; j++) {
          k = rand() % nhosts;
          assert (k >=0 && k<nhosts);
          if (j != k) {
            struct addrinfo *temp;
            temp = host[j]; host[j] = host[k]; host[k] = temp;
          }
        }
}


/*-------------------------------------------------------------------
 *
 * Name:        dwsock_ia_to_text
 *
 * Purpose:     Convert binary IP Address to text form.
 *
 * Inputs:	Family		- AF_INET or AF_INET6.
 *
 *		pAddr		- Pointer to the IP Address storage location.
 *
 *		StringBufSize	- Number of bytes in pStringBuf.
 *
 * Outputs:	pStringBuf	- Text result is placed here.
 *
 * Returns:	pStringBuf
 *
 * Description:	Can't use InetNtop because it is supported only on Windows Vista and later.
 * 		At one time Dire Wolf worked on Win XP.  Haven't tried it for years.
 * 		Maybe some other dependency on a newer OS version has crept in.
 *
 * TODO:	Use this instead of own copy in aclients.c
 * TODO:	Use this instead of own copy in appserver.c
 * TODO:	Use this instead of own copy in igate.c
 * TODO:	Use this instead of own copy in tnctest.c
 * TODO:	Use this instead of own copy in ttcalc.c
 *	
 *--------------------------------------------------------------------*/

char *dwsock_ia_to_text (int  Family, void * pAddr, char * pStringBuf, size_t StringBufSize)
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

} /* end dwsock_ia_to_text */


void dwsock_close (int fd)
{
#if __WIN32__
	      closesocket (fd);
#else
	      close (fd);
#endif
}




/* end dwsock.c */

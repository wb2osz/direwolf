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
 * Module:      aclients.c
 *
 * Purpose:   	Multiple concurrent APRS clients for comparing 
 *		TNC demodulator performance.
 *		
 * Description:	Establish connection with multiple servers and 
 *		compare results side by side.
 *
 * Usage:	aclients port1=name1 port2=name2 ...
 *
 * Example:	aclients  8000=AGWPE  192.168.1.64:8002=DireWolf  COM1=D710A
 *
 *		This will connect to multiple physical or virtual
 *		TNCs, read packets from them, and display results.
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
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <time.h>


#include "ax25_pad.h"
#include "textcolor.h"
#include "version.h"


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


#if __WIN32__
static unsigned __stdcall client_thread_net (void *arg);
static unsigned __stdcall client_thread_serial (void *arg);
#else
static void * client_thread_net (void *arg);
static void * client_thread_serial (void *arg);
#endif



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
 * Purpose:   	Start up multiple client threads listening to different
 *		TNCs.   Print packets.  Tally up statistics.
 *
 * Usage:	Described above.
 *
 *---------------------------------------------------------------*/

#define MAX_CLIENTS 6

/* Obtained from the command line. */

static int num_clients;

static char hostname[MAX_CLIENTS][50];		/* DNS host name or IPv4 address. */
						/* Some of the code is there for IPv6 but */
						/* needs more work. */
						/* Defaults to "localhost" if not specified. */

static char port[MAX_CLIENTS][30];		/* If it begins with a digit, it is considered */
						/* a TCP port number at the hostname.  */
						/* Otherwise, we treat it as a serial port name. */

static char description[MAX_CLIENTS][50];	/* Name used in the output. */


#if __WIN32__
	static HANDLE client_th[MAX_CLIENTS];
#else
	static pthread_t client_tid[MAX_CLIENTS];
#endif

#define LINE_WIDTH 120
static int column_width;
static char packets[LINE_WIDTH+4];
static int packet_count[MAX_CLIENTS];


//#define PRINT_MINUTES 2

#define PRINT_MINUTES 30



int main (int argc, char *argv[])
{
	int j;
	time_t start_time, now, next_print_time;

#if __WIN32__
#else
	int e;

 	setlinebuf (stdout);
#endif

/*
 * Extract command line args.
 */
	num_clients = argc - 1;

	if (num_clients < 1 || num_clients > MAX_CLIENTS) {
	  printf ("Specify up to %d TNCs on the command line.\n", MAX_CLIENTS);
	  exit (1);
	}

	column_width = LINE_WIDTH / num_clients;

	for (j=0; j<num_clients; j++) {
	  char stemp[100];
	  char *p;

/* Each command line argument should be of the form "port=description." */

	  strlcpy (stemp, argv[j+1], sizeof(stemp));
	  p = strtok (stemp, "=");
	  if (p == NULL) {
	    printf ("Internal error 1\n");
	    exit (1);
	  }
	  strlcpy (hostname[j], "localhost", sizeof(hostname[j]));
	  strlcpy (port[j], p, sizeof(port[j]));
	  p = strtok (NULL, "=");
	  if (p == NULL) {
	    printf ("Missing description after %s\n", port[j]);
	    exit (1);
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

	//printf ("_WIN32_WINNT = %04x\n", _WIN32_WINNT);
	//for (j=0; j<num_clients; j++) {
	//  printf ("%s,%s,%s\n", hostname[j], port[j], description[j]);
	//}

	memset (packets, ' ', (size_t)LINE_WIDTH);
	packets[LINE_WIDTH] = '\0';

	for (j=0; j<num_clients; j++) {
	  packet_count[j] = 0;
	}


	for (j=0; j<num_clients; j++) {

/* If port begins with digit, consider it to be TCP. */
/* Otherwise, treat as serial port name. */

#if __WIN32__
	  if (isdigit(port[j][0])) {
	    client_th[j] = (HANDLE)_beginthreadex (NULL, 0, client_thread_net, (void *)(ptrdiff_t)j, 0, NULL);
	  }
	  else {
	    client_th[j] = (HANDLE)_beginthreadex (NULL, 0, client_thread_serial, (void *)(ptrdiff_t)j, 0, NULL);
	  }
	  if (client_th[j] == NULL) {
	    printf ("Internal error: Could not create client thread %d.\n", j);
	    exit (1);
	  }
#else
	  if (isdigit(port[j][0])) {
	    e = pthread_create (&client_tid[j], NULL, client_thread_net, (void *)(ptrdiff_t)j);
	  }
	  else {
	    e = pthread_create (&client_tid[j], NULL, client_thread_serial, (void *)(ptrdiff_t)j);
	  }
	  if (e != 0) {
	    perror("Internal error: Could not create client thread.");
	    exit (1);
	  }
#endif
	}

	start_time = time(NULL);
	next_print_time = start_time + (PRINT_MINUTES) * 60;

/*
 * Print results from clients. 
 */
	while (1) {
	  int k;
	  int something;	

	  SLEEP_MS(100);
	  
	  something = 0;
	  for (k=0; k<LINE_WIDTH && ! something; k++) {
	    if (packets[k] != ' ') {
	      something = 1;
	    }
	  }
	  if (something) {
	    /* time for others to catch up. */
	    SLEEP_MS(200);

	    printf ("%s\n", packets);
	    memset (packets, ' ', (size_t)LINE_WIDTH);	
	  }

	  now = time(NULL);
	  if (now >= next_print_time) {
	    next_print_time = now + (PRINT_MINUTES) * 60;
	
	    printf ("\nTotals after %d minutes", (int)((now - start_time) / 60));

	    for (j=0; j<num_clients; j++) {
	      printf (", %s %d", description[j], packet_count[j]);
	    }
	    printf ("\n\n");
	  }
	}


	return 0;  // unreachable
}




/*-------------------------------------------------------------------
 *
 * Name:        client_thread_net
 *
 * Purpose:     Establish connection with a TNC via network.
 *
 * Inputs:	arg		- My instance index, 0 thru MAX_CLIENTS-1.
 *
 * Outputs:	packets		- Received packets are put in the corresponding column.
 *
 *--------------------------------------------------------------------*/

#define MAX_HOSTS 30

#if __WIN32__
static unsigned __stdcall client_thread_net (void *arg)
#else
static void * client_thread_net (void *arg)	
#endif	
{
	int my_index;
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
	struct agwpe_s mon_cmd;
	char data[1024];
	int use_chan = -1;


	my_index = (int)(ptrdiff_t)arg;

#if DEBUGx
        printf ("DEBUG: client_thread_net %d start, port = '%s'\n", my_index, port[my_index]);
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

 	  printf("Client %d now connected to %s on %s (%s), port %s\n", 
			my_index, description[my_index], hostname[my_index], ipaddr_str, port[my_index] );

	  server_sock = is;
#endif	  
	  break;
	}

	freeaddrinfo(ai_head);

	if (server_sock == -1) {

 	  printf("Client %d unable to connect to %s on %s (%s), port %s\n", 
			my_index, description[my_index], hostname[my_index], ipaddr_str, port[my_index] );
	  exit (1);
	}

/*
 * Send command to toggle reception of frames in raw format.
 *
 * Note: Monitor format is only for UI frames.
 * It also discards the via path.
 */

	memset (&mon_cmd, 0, sizeof(mon_cmd));

	mon_cmd.kind_lo = 'k';

	SOCK_SEND (server_sock, (char*)(&mon_cmd), sizeof(mon_cmd));

/*
 * Print all of the monitored packets.
 */

	while (1) {
	  int n;

	  n = SOCK_RECV (server_sock, (char*)(&mon_cmd), sizeof(mon_cmd));

	  if (n != sizeof(mon_cmd)) {
	    printf ("Read error, client %d received %d command bytes.  Terminating.\n", my_index, n);
	    exit (1);
	  }

#if DEBUGx
	  printf ("client %d received '%c' data, data_len = %d\n", 
			my_index, mon_cmd.kind_lo, mon_cmd.data_len);
#endif
	  assert (mon_cmd.data_len >= 0 && mon_cmd.data_len < (int)(sizeof(data)));

	  if (mon_cmd.data_len > 0) {
	    n = SOCK_RECV (server_sock, data, mon_cmd.data_len);

	    if (n != mon_cmd.data_len) {
	      printf ("Read error, client %d received %d data bytes.\n", my_index, n);
	      exit (1);
	    }
	  }

/* 
 * Print it and add to counter.
 * The AGWPE score was coming out double the proper value because 
 * we were getting the same thing from ports 2 and 3.
 * 'use_chan' is the first channel we hear from.
 * Listen only to that one.
 */

	  if (mon_cmd.kind_lo == 'K' && (use_chan == -1 || use_chan == mon_cmd.portx)) {
	    packet_t pp;
	    char *pinfo;
	    int info_len;
	    char result[400];
	    char *p;
	    int col, len;
	    alevel_t alevel;

	    //printf ("server %d, portx = %d\n", my_index, mon_cmd.portx);

	    use_chan = mon_cmd.portx;
	    memset (&alevel, 0xff, sizeof(alevel));
	    pp = ax25_from_frame ((unsigned char *)(data+1), mon_cmd.data_len-1, alevel);
	    assert (pp != NULL);
	    ax25_format_addrs (pp, result);
	    info_len = ax25_get_info (pp, (unsigned char **)(&pinfo));
	    pinfo[info_len] = '\0';
	    strlcat (result, pinfo, sizeof(result));
	    for (p=result; *p!='\0'; p++) {
	      if (! isprint(*p)) *p = ' ';
	    }
#if DEBUGx
	    printf ("[%d] %s\n", my_index, result);
#endif
	    col = column_width * my_index;
	    len = strlen(result);
#define MARGIN 3
	    if (len > column_width - 3) {
	      len = column_width - 3;
	    }
	    if (packets[col] == ' ') {
	      memcpy (packets+col, result, (size_t)len);
	    }
	    else {
	      memcpy (packets+col, "OVERRUN!    ", (size_t)10);
	    }
	    	    
	    ax25_delete (pp);
	    packet_count[my_index]++;
	  }
	}

} /* end client_thread_net */





/*-------------------------------------------------------------------
 *
 * Name:        client_thread_serial
 *
 * Purpose:     Establish connection with a TNC via serial port.
 *
 * Inputs:	arg		- My instance index, 0 thru MAX_CLIENTS-1.
 *
 * Outputs:	packets		- Received packets are put in the corresponding column.
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
static unsigned __stdcall client_thread_serial (void *arg)
#else
static void * client_thread_serial (void *arg)	
#endif	
{
	int my_index = (int)(ptrdiff_t)arg;

#if __WIN32__

	MYFDTYPE fd;
	DCB dcb;
	int ok;

	// Bug: Won't work for ports above COM9.
	// http://support.microsoft.com/kb/115831

	fd = CreateFile(port[my_index], GENERIC_READ | GENERIC_WRITE, 
			0, NULL, OPEN_EXISTING, 0, NULL);

	if (fd == MYFDERROR) {
 	  printf("Client %d unable to connect to %s on %s.\n", 
			my_index, description[my_index], port[my_index] );
	  exit (1);
	}

	/* Reference: http://msdn.microsoft.com/en-us/library/windows/desktop/aa363201(v=vs.85).aspx */

	memset (&dcb, 0, sizeof(dcb));
	dcb.DCBlength = sizeof(DCB);

	ok = GetCommState (fd, &dcb);
	if (! ok) {
	  printf ("GetCommState failed.\n");
	}

	/* http://msdn.microsoft.com/en-us/library/windows/desktop/aa363214(v=vs.85).aspx */

	dcb.DCBlength = sizeof(DCB);
	dcb.BaudRate = 9600;
	dcb.fBinary = 1;
	dcb.fParity = 0;
	dcb.fOutxCtsFlow = 0;
	dcb.fOutxDsrFlow = 0;
	dcb.fDtrControl = 0;
	dcb.fDsrSensitivity = 0;
	dcb.fOutX = 0;
	dcb.fInX = 0;
	dcb.fErrorChar = 0;
	dcb.fNull = 0;		/* Don't drop nul characters! */
	dcb.fRtsControl = 0;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;

	ok = SetCommState (fd, &dcb);
	if (! ok) {
	  printf ("SetCommState failed.\n");
	}

#else

/* Linux version. */

	int fd;
	struct termios ts;
	int e;

	fd = open (port[my_index], O_RDWR);

	if (fd == MYFDERROR) {
 	  printf("Client %d unable to connect to %s on %s.\n", 
			my_index, description[my_index], port[my_index] );
	  exit (1);
	}

	e = tcgetattr (fd, &ts);
	if (e != 0) { perror ("nm tcgetattr"); }

	cfmakeraw (&ts);
	
	// TODO: speed?
	ts.c_cc[VMIN] = 1;	/* wait for at least one character */
	ts.c_cc[VTIME] = 0;	/* no fancy timing. */

	e = tcsetattr (fd, TCSANOW, &ts);
	if (e != 0) { perror ("nm tcsetattr"); }
#endif


/* Success. */

 	printf("Client %d now connected to %s on %s\n", 
			my_index, description[my_index], port[my_index] );

/*
 * Assume we are already in monitor mode.
 */


/*
 * Print all of the monitored packets.
 */

	while (1) {
	  unsigned char ch;
	  char result[500];
	  int col, len;
	  int done;
	  char *p;

	  len = 0;
	  done = 0;

	  while ( ! done) {

#if __WIN32__
	    DWORD n;

	    if (! ReadFile (fd, &ch, 1, &n, NULL)) {
	      printf ("Read error on %s.\n", description[my_index]);
	      CloseHandle (fd);
	      exit (1);
	    }
	  
#else
	    int n;

	    if ( ( n = read(fd, & ch, 1)) < 0) {
	      printf ("Read error on %s.\n", description[my_index]);
	      close (fd);
	      exit (1);
	    }
#endif
	    if (n == 1) {

/*
 * Try to build one line for each packet.
 * The KPC3+ breaks a packet into two lines like this:
 *
 *	KB1ZXL-1>T2QY5P,W1MHL*,WIDE2-1: <<UI>>:
 *	`c0+!h4>/]"4a}146.520MHz Listening, V-Alert & WLNK-1=
 *
 *	N8VIM>BEACON,W1XM,WB2OSZ-1,WIDE2*: <UI>:
 * 	!4240.85N/07133.99W_PHG72604/ Pepperell, MA. WX. 442.9+ PL100
 *
 * Don't know why some are <<UI>> and some <UI>.
 *
 * Anyhow, ignore the return character if preceded by >:
 */
	      if (ch == '\r') { 
	        if (len >= 10 && result[len-2] == '>' && result[len-1] == ':') {
	          continue;
	        }
	        done = 1; 
	        continue; 
	      }
	      if (ch == '\n') continue;
	      result[len++] = ch;
	    }
	  }
	  result[len] = '\0';

/* 
 * Print it and add to counter.
 */
	 if (len > 0) {
	  /* Blank any unprintable characters. */
	  for (p=result; *p!='\0'; p++) {
	    if (! isprint(*p)) *p = ' ';
	  }
#if DEBUGx
	  printf ("[%d] %s\n", my_index, result);
#endif
	  col = column_width * my_index;
	  if (len > column_width - 3) {
	    len = column_width - 3;
	  }
	  if (packets[col] == ' ') {
	    memcpy (packets+col, result, (size_t)len);
	  }
	  else {
	    memcpy (packets+col, "OVERRUN!    ", (size_t)10);
	  }
	  packet_count[my_index]++;
         }
	}

} /* end client_thread_serial */

/* end aclients.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015, 2016  John Langner, WB2OSZ
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
 * Module:      igate.c
 *
 * Purpose:   	IGate client.
 *		
 * Description:	Establish connection with a tier 2 IGate server
 *		and relay packets between RF and Internet.
 *
 * References:	APRS-IS (Automatic Packet Reporting System-Internet Service)
 *		http://www.aprs-is.net/Default.aspx
 *
 *		APRS iGate properties
 *		http://wiki.ham.fi/APRS_iGate_properties
 *
 *		SATgate mode.
 *		http://www.tapr.org/pipermail/aprssig/2016-January/045283.html
 *
 *---------------------------------------------------------------*/

/*------------------------------------------------------------------
 *
 * From http://windows.microsoft.com/en-us/windows7/ipv6-frequently-asked-questions
 * 
 * How can I enable IPv6?
 * Follow these steps:
 * 
 * Open Network Connections by clicking the Start button, and then clicking 
 * Control Panel. In the search box, type adapter, and then, under Network 
 * and Sharing Center, click View network connections.
 * 
 * Right-click your network connection, and then click Properties.   
 * If you're prompted for an administrator password or confirmation, type 
 * the password or provide confirmation.
 *
 * Select the check box next to Internet Protocol Version 6 (TCP/IPv6).
 *
 *---------------------------------------------------------------*/

/*
 * Native Windows:	Use the Winsock interface.
 * Linux:		Use the BSD socket interface.
 * Cygwin:		Can use either one.
 */


#if __WIN32__

/* The goal is to support Windows XP and later. */

#include <winsock2.h>
// default is 0x0400
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0501	/* Minimum OS version is XP. */
//#define _WIN32_WINNT 0x0502	/* Minimum OS version is XP with SP2. */
//#define _WIN32_WINNT 0x0600	/* Minimum OS version is Vista. */
#include <ws2tcpip.h>
#else 
#include <stdlib.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include "direwolf.h"
#include "ax25_pad.h"
#include "textcolor.h"
#include "version.h"
#include "digipeater.h"
#include "tq.h"
#include "igate.h"
#include "latlong.h"
#include "pfilter.h"
#include "dtime_now.h"


#if __WIN32__
static unsigned __stdcall connnect_thread (void *arg);
static unsigned __stdcall igate_recv_thread (void *arg);
static unsigned __stdcall satgate_delay_thread (void *arg);
#else
static void * connnect_thread (void *arg);
static void * igate_recv_thread (void *arg);
static void * satgate_delay_thread (void *arg);
#endif


static dw_mutex_t dp_mutex;				/* Critical section for delayed packet queue. */
static packet_t dp_queue_head;

static void satgate_delay_packet (packet_t pp, int chan);
static void send_packet_to_server (packet_t pp, int chan);
static void send_msg_to_server (const char *msg);
static void xmit_packet (char *message, int chan);

static void rx_to_ig_init (void);
static void rx_to_ig_remember (packet_t pp);
static int rx_to_ig_allow (packet_t pp);

static void ig_to_tx_init (void);
static int ig_to_tx_allow (packet_t pp, int chan);


/* 
 * File descriptor for socket to IGate server. 
 * Set to -1 if not connected. 
 * (Don't use SOCKET type because it is unsigned.) 
*/

static volatile int igate_sock = -1;	

/*
 * After connecting to server, we want to make sure
 * that the login sequence is sent first.
 * This is set to true after the login is complete.
 */

static volatile int ok_to_send = 0;




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
	//assert (strlen(pStringBuf) < StringBufSize);
	return pStringBuf;
}


#if ITEST

/* For unit testing. */

int main (int argc, char *argv[])
{
	struct audio_s audio_config;
	struct igate_config_s igate_config;
	struct digi_config_s digi_config;
	packet_t pp;

	memset (&audio_config, 0, sizeof(audio_config));
	audio_config.adev[0].num_chans = 2;
	strlcpy (audio_config.achan[0].mycall, "WB2OSZ-1", sizeof(audio_config.achan[0].mycall));
	strlcpy (audio_config.achan[1].mycall, "WB2OSZ-2", sizeof(audio_config.achan[0].mycall));

	memset (&igate_config, 0, sizeof(igate_config));

	strlcpy (igate_config.t2_server_name, "localhost", sizeof(igate_config.t2_server_name));
	igate_config.t2_server_port = 14580;
	strlcpy (igate_config.t2_login, "WB2OSZ-JL", sizeof(igate_config.t2_login));
	strlcpy (igate_config.t2_passcode, "-1", sizeof(igate_config.t2_passcode));
	igate_config.t2_filter = strdup ("r/1/2/3");
	
	igate_config.tx_chan = 0;
	strlcpy (igate_config.tx_via, ",WIDE2-1", sizeof(igate_config.tx_via));
	igate_config.tx_limit_1 = 3;
	igate_config.tx_limit_5 = 5;

	memset (&digi_config, 0, sizeof(digi_config));

	igate_init(&igate_config, &digi_config);

	while (igate_sock == -1) {
	  SLEEP_SEC(1);
	}

	SLEEP_SEC (2);
	pp = ax25_from_text ("A>B,C,D:Ztest message 1", 0);
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

	SLEEP_SEC (2);
	pp = ax25_from_text ("A>B,C,D:Ztest message 2", 0);
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

	SLEEP_SEC (2);
	pp = ax25_from_text ("A>B,C,D:Ztest message 2", 0);   /* Should suppress duplicate. */
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

	SLEEP_SEC (2);
	pp = ax25_from_text ("A>B,TCPIP,D:ZShould drop this due to path", 0);
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

	SLEEP_SEC (2);
	pp = ax25_from_text ("A>B,C,D:?Should drop query", 0);
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

	SLEEP_SEC (5);
	pp = ax25_from_text ("A>B,C,D:}E>F,G*,H:Zthird party stuff", 0);
	igate_send_rec_packet (0, pp);
	ax25_delete (pp);

#if 1
	while (1) {
	  SLEEP_SEC (20);
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Send received packet\n");
	  send_msg_to_server ("W1ABC>APRS:?");
	}
#endif
	return 0;
}

#endif


/*
 * Global stuff (to this file)
 *
 * These are set by init function and need to 
 * be kept around in case connection is lost and
 * we need to reestablish the connection later.
 */


static struct audio_s		*save_audio_config_p;
static struct igate_config_s	*save_igate_config_p;
static struct digi_config_s 	*save_digi_config_p;
static int 			s_debug;


/*
 * Statistics.  
 * TODO: need print function.
 */

static int stats_failed_connect;	/* Number of times we tried to connect to */
					/* a server and failed.  A small number is not */
					/* a bad thing.  Each name should have a bunch */
					/* of addresses for load balancing and */
					/* redundancy. */

static int stats_connects;		/* Number of successful connects to a server. */
					/* Normally you'd expect this to be 1.  */
					/* Could be larger if one disappears and we */
					/* try again to find a different one. */

static time_t stats_connect_at;		/* Most recent time connection was established. */
					/* can be used to determine elapsed connect time. */

static int stats_rf_recv_packets;	/* Number of candidate packets from the radio. */

static int stats_rx_igate_packets;	/* Number of packets passed along to the IGate */
					/* server after filtering. */

static int stats_uplink_bytes;		/* Total number of bytes sent to IGate server */
					/* including login, packets, and hearbeats. */

static int stats_downlink_bytes;	/* Total number of bytes from IGate server including */
					/* packets, heartbeats, other messages. */

static int stats_tx_igate_packets;	/* Number of packets from IGate server. */

static int stats_rf_xmit_packets;	/* Number of packets passed along to radio */
					/* after rate limiting or other restrictions. */

/* We have some statistics.  What do we do with them?


	IGate stations often send packets like this:

	<IGATE MSG_CNT=1238 LOC_CNT=0 FILL_CNT=0
	<IGATE,MSG_CNT=1,LOC_CNT=25
	<IGATE,MSG_CNT=0,LOC_CNT=46,DIR_CNT=13,RF_CNT=49,RFPORT_ID=0

	What does it all mean?
	Why do some have spaces instead of commas between the capabilities?

	The APRS Protocol Reference ( http://www.aprs.org/doc/APRS101.PDF ),
	section 15, briefly discusses station capabilities and gives the example
	IGATE,MSG_CNT=n,LOC_CNT=n

	IGate Design ( http://www.aprs-is.net/IGating.aspx ) barely mentions
	<IGATE,MSG_CNT=n,LOC_CNT=n

	This leaves many questions.  Does "number of messages transmitted" mean only
	the APRS "Message" (data type indicator ":") or does it mean any type of
	APRS packet?   What are "local" stations?   Those we hear directly without
	going thru a digipeater?

	What are DIR_CNT, RF_CNT, and so on?

	Are the counts since the system started up or are they for some interval?

*/


/*-------------------------------------------------------------------
 *
 * Name:        igate_init
 *
 * Purpose:     One time initialization when main application starts up.
 *
 * Inputs:	p_audio_config	- Audio channel configuration.  All we care about is:
 *				  - Number of radio channels.
 *				  - Radio call and SSID for each channel.
 *
 *		p_igate_config	- IGate configuration.
 *
 *		p_digi_config	- Digipeater configuration.  
 *				  All we care about here is the packet filtering options.
 *
 *		debug_level	- 0  print packets FROM APRS-IS,
 *				     establishing connection with sergver, and
 *				     and anything rejected by client side filtering.
 *				  1  plus packets sent TO server or why not.
 *				  2  plus duplicate detection overview.
 *				  3  plus duplicate detection details.
 *
 * Description:	This starts two threads:
 *
 *		  *  to establish and maintain a connection to the server.
 *		  *  to listen for packets from the server.
 *
 *--------------------------------------------------------------------*/


void igate_init (struct audio_s *p_audio_config, struct igate_config_s *p_igate_config, struct digi_config_s *p_digi_config, int debug_level)
{
#if __WIN32__
	HANDLE connnect_th;
	HANDLE cmd_recv_th;
	HANDLE satgate_delay_th;
#else
	pthread_t connect_listen_tid;
	pthread_t cmd_listen_tid;
	pthread_t satgate_delay_tid;
	int e;
#endif
	s_debug = debug_level;
	dp_queue_head = NULL;

#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("igate_init ( %s, %d, %s, %s, %s )\n", 
				p_igate_config->t2_server_name, 
				p_igate_config->t2_server_port, 
				p_igate_config->t2_login, 
				p_igate_config->t2_passcode, 
				p_igate_config->t2_filter);
#endif

/*
 * Save the arguments for later use.
 */
	save_audio_config_p = p_audio_config;
	save_igate_config_p = p_igate_config;
	save_digi_config_p = p_digi_config;

	stats_failed_connect = 0;	
	stats_connects = 0;		
	stats_connect_at = 0;		
	stats_rf_recv_packets = 0;	
	stats_rx_igate_packets = 0;	
	stats_uplink_bytes = 0;		
	stats_downlink_bytes = 0;	
	stats_tx_igate_packets = 0;	
	stats_rf_xmit_packets = 0;
	
	rx_to_ig_init ();
	ig_to_tx_init ();


/*
 * Continue only if we have server name, login, and passcode.
 */
	if (strlen(p_igate_config->t2_server_name) == 0 ||
	    strlen(p_igate_config->t2_login) == 0 ||
	    strlen(p_igate_config->t2_passcode) == 0) {
	  return;
	}

/*
 * This connects to the server and sets igate_sock.
 * It also sends periodic messages to say I'm still alive.
 */

#if __WIN32__
	connnect_th = (HANDLE)_beginthreadex (NULL, 0, connnect_thread, (void *)NULL, 0, NULL);
	if (connnect_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: Could not create IGate connection thread\n");
	  return;
	}
#else
	e = pthread_create (&connect_listen_tid, NULL, connnect_thread, (void *)NULL);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Internal error: Could not create IGate connection thread");
	  return;
	}
#endif

/*
 * This reads messages from client when igate_sock is valid.
 */

#if __WIN32__
	cmd_recv_th = (HANDLE)_beginthreadex (NULL, 0, igate_recv_thread, NULL, 0, NULL);
	if (cmd_recv_th == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Internal error: Could not create IGate reading thread\n");
	  return;
	}
#else
	e = pthread_create (&cmd_listen_tid, NULL, igate_recv_thread, NULL);
	if (e != 0) {
	  text_color_set(DW_COLOR_ERROR);
	  perror("Internal error: Could not create IGate reading thread");
	  return;
	}
#endif

/*
 * This lets delayed packets continue after specified amount of time.
 */

	if (p_igate_config->satgate_delay > 0) {
#if __WIN32__
	  satgate_delay_th = (HANDLE)_beginthreadex (NULL, 0, satgate_delay_thread, NULL, 0, NULL);
	  if (satgate_delay_th == NULL) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Internal error: Could not create SATgate delay thread\n");
	    return;
	  }
#else
	  e = pthread_create (&satgate_delay_tid, NULL, satgate_delay_thread, NULL);
	  if (e != 0) {
	    text_color_set(DW_COLOR_ERROR);
	    perror("Internal error: Could not create SATgate delay thread");
	    return;
	  }
#endif
	  dw_mutex_init(&dp_mutex);
	}

} /* end igate_init */


/*-------------------------------------------------------------------
 *
 * Name:        connnect_thread
 *
 * Purpose:     Establish connection with IGate server.
 *		Send periodic heartbeat to keep keep connection active.
 *		Reconnect if something goes wrong and we got disconnected.
 *
 * Inputs:	arg		- Not used.
 *
 * Outputs:	igate_sock	- File descriptor for communicating with client app.
 *				  Will be -1 if not connected.
 *
 * References:	TCP client example.
 *		http://msdn.microsoft.com/en-us/library/windows/desktop/ms737591(v=vs.85).aspx
 *
 *		Linux IPv6 HOWTO
 *		http://www.tldp.org/HOWTO/Linux+IPv6-HOWTO/
 *
 *--------------------------------------------------------------------*/

/*
 * Addresses don't get mixed up very well.
 * IPv6 always shows up last so we'd probably never
 * end up using any of them.  Use our own shuffle.
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

#define MAX_HOSTS 50




#if __WIN32__
static unsigned __stdcall connnect_thread (void *arg)
#else
static void * connnect_thread (void *arg)	
#endif	
{
	struct addrinfo hints;
	struct addrinfo *ai_head = NULL;
	struct addrinfo *ai;
	struct addrinfo *hosts[MAX_HOSTS];
	int num_hosts, n;
	int err;
	char server_port_str[12];	/* text form of port number */
	char ipaddr_str[46];		/* text form of IP address */
#if __WIN32__
	WSADATA wsadata;
#endif

	snprintf (server_port_str, sizeof(server_port_str), "%d", save_igate_config_p->t2_server_port);
#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
        dw_printf ("DEBUG: igate connect_thread start, port = %d = '%s'\n", save_igate_config_p->t2_server_port, server_port_str);
#endif

#if __WIN32__
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
#endif

	memset (&hints, 0, sizeof(hints));

	hints.ai_family = AF_UNSPEC;	/* Allow either IPv4 or IPv6. */

	// IPv6 is half baked on Windows XP.
	// We might need to leave out IPv6 support for Windows version.
	// hints.ai_family = AF_INET;	/* IPv4 only. */

#if IPV6_ONLY
	/* IPv6 addresses always show up at end of list. */
	/* Force use of them for testing. */
	hints.ai_family = AF_INET6;	/* IPv6 only */
#endif					
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;


/*
 * Repeat forever.
 */

	while (1) {

/*
 * Connect to IGate server if not currently connected.
 */

	  if (igate_sock == -1) {

	    SLEEP_SEC (5);

	    ai_head = NULL;
	    err = getaddrinfo(save_igate_config_p->t2_server_name, server_port_str, &hints, &ai_head);
	    if (err != 0) {
	      text_color_set(DW_COLOR_ERROR);
#if __WIN32__
	      dw_printf ("Can't get address for IGate server %s, err=%d\n", 
					save_igate_config_p->t2_server_name, WSAGetLastError());
#else 
	      dw_printf ("Can't get address for IGate server %s, %s\n", 
					save_igate_config_p->t2_server_name, gai_strerror(err));
#endif
	      freeaddrinfo(ai_head);

	      continue;
	    }

#if DEBUG_DNS
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("getaddrinfo returns:\n");
#endif
	    num_hosts = 0;
	    for (ai = ai_head; ai != NULL; ai = ai->ai_next) {
#if DEBUG_DNS
	      text_color_set(DW_COLOR_DEBUG);
	      ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	      dw_printf ("    %s\n", ipaddr_str);
#endif
	      hosts[num_hosts] = ai;
	      if (num_hosts < MAX_HOSTS) num_hosts++;
	    }

	    // We can get multiple addresses back for the host name.
	    // These should be somewhat randomized for load balancing. 
	    // It turns out the IPv6 addresses are always at the 
	    // end for both Windows and Linux.   We do our own shuffling
	    // to mix them up better and give IPv6 a chance. 

	    shuffle (hosts, num_hosts);

#if DEBUG_DNS
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("after shuffling:\n");
	    for (n=0; n<num_hosts; n++) {
	      ia_to_text (hosts[n]->ai_family, hosts[n]->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	      dw_printf ("    %s\n", ipaddr_str);
	    }
#endif

	    // Try each address until we find one that is successful.

	    for (n=0; n<num_hosts; n++) {
	      int is;

	      ai = hosts[n];

	      ia_to_text (ai->ai_family, ai->ai_addr, ipaddr_str, sizeof(ipaddr_str));
	      is = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
#if __WIN32__
	      if (is == INVALID_SOCKET) {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("IGate: Socket creation failed, err=%d", WSAGetLastError());
	        WSACleanup();
	        is = -1;
		stats_failed_connect++;
	        continue;
	      }
#else
	      if (err != 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf("Connect to IGate server %s (%s) failed.\n\n",
					save_igate_config_p->t2_server_name, ipaddr_str);
	        (void) close (is);
	        is = -1;
		stats_failed_connect++;
	        continue;
	      }
#endif

#ifndef DEBUG_DNS 
	      err = connect(is, ai->ai_addr, (int)ai->ai_addrlen);
#if __WIN32__
	      if (err == SOCKET_ERROR) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf("Connect to IGate server %s (%s) failed.\n\n",
					save_igate_config_p->t2_server_name, ipaddr_str);
	        closesocket (is);
	        is = -1;
		stats_failed_connect++; 
	        continue;
	      }
	      // TODO: set TCP_NODELAY?
#else
	      if (err != 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf("Connect to IGate server %s (%s) failed.\n\n",
					save_igate_config_p->t2_server_name, ipaddr_str);
	        (void) close (is);
	        is = -1;
		stats_failed_connect++;
	        continue;
	      }
	      /* IGate documentation says to use it.  */
	      /* Does it really make a difference for this application? */
	      int flag = 1;
	      err = setsockopt (is, IPPROTO_TCP, TCP_NODELAY, (void*)(long)(&flag), sizeof(flag));
	      if (err < 0) {
	        text_color_set(DW_COLOR_INFO);
	        dw_printf("setsockopt TCP_NODELAY failed.\n");
	      }
#endif
	      stats_connects++;
	      stats_connect_at = time(NULL);

/* Success. */

	      text_color_set(DW_COLOR_INFO);
 	      dw_printf("\nNow connected to IGate server %s (%s)\n", save_igate_config_p->t2_server_name, ipaddr_str );
	      if (strchr(ipaddr_str, ':') != NULL) {
	      	dw_printf("Check server status here http://[%s]:14501\n\n", ipaddr_str);
	      }
	      else {
	        dw_printf("Check server status here http://%s:14501\n\n", ipaddr_str);
	      }

/* 
 * Set igate_sock so everyone else can start using it. 
 * But make the Rx -> Internet messages wait until after login.
 */

	      ok_to_send = 0;
	      igate_sock = is;
#endif	  
	      break;
	    }

	    freeaddrinfo(ai_head);

	    if (igate_sock != -1) {
	      char stemp[256];

/* 
 * Send login message.
 * Software name and version must not contain spaces.
 */

	      SLEEP_SEC(3);
	      snprintf (stemp, sizeof(stemp), "user %s pass %s vers Dire-Wolf %d.%d", 
			save_igate_config_p->t2_login, save_igate_config_p->t2_passcode,
			MAJOR_VERSION, MINOR_VERSION);
	      if (save_igate_config_p->t2_filter != NULL) {
	        strlcat (stemp, " filter ", sizeof(stemp));
	        strlcat (stemp, save_igate_config_p->t2_filter, sizeof(stemp));
	      }
	      send_msg_to_server (stemp);

/* Delay until it is ok to start sending packets. */

	      SLEEP_SEC(7);
	      ok_to_send = 1;
	    }
	  }

/*
 * If connected to IGate server, send heartbeat periodically to keep connection active.
 */
	  if (igate_sock != -1) {
	    SLEEP_SEC(10);
	  }
	  if (igate_sock != -1) {
	    SLEEP_SEC(10);
	  }
	  if (igate_sock != -1) {
	    SLEEP_SEC(10);
	  }


	  if (igate_sock != -1) {

	    char heartbeat[10];

	    strlcpy (heartbeat, "#", sizeof(heartbeat));

	    /* This will close the socket if any error. */
	    send_msg_to_server (heartbeat);

	  }
	}
} /* end connnect_thread */




/*-------------------------------------------------------------------
 *
 * Name:        igate_send_rec_packet
 *
 * Purpose:     Send a packet to the IGate server
 *
 * Inputs:	chan	- Radio channel it was received on.
 *
 *		recv_pp	- Pointer to packet object.
 *			  *** CALLER IS RESPONSIBLE FOR DELETING IT! **
 *		
 *
 * Description:	Send message to IGate Server if connected.
 *
 * Assumptions:	(1) Caller has already verified it is an APRS packet.
 *		i.e. control = 3 for UI frame, protocol id = 0xf0 for no layer 3
 *
 *		(2) This is being called only for packets received with
 *		a correct CRC.  We don't want to propagate corrupted data.
 *
 *--------------------------------------------------------------------*/

#define IGATE_MAX_MSG 520	/* Message to IGate max 512 characters. */

void igate_send_rec_packet (int chan, packet_t recv_pp)
{
	packet_t pp;
	int n;
	unsigned char *pinfo;
	char *p;
	int info_len;
	

	if (igate_sock == -1) {
	  return;	/* Silently discard if not connected. */
	}

	if ( ! ok_to_send) {
	  return;	/* Login not complete. */
	}

/*
 * Check for filtering from specified channel to the IGate server.
 */

	if (save_digi_config_p->filter_str[chan][MAX_CHANS] != NULL) {

	  if (pfilter(chan, MAX_CHANS, save_digi_config_p->filter_str[chan][MAX_CHANS], recv_pp) != 1) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Packet from channel %d to IGate was rejected by filter: %s\n", chan, save_digi_config_p->filter_str[chan][MAX_CHANS]);

	    return;
	  }
	}


	/* Gather statistics. */

	stats_rf_recv_packets++;

/*
 * First make a copy of it because it might be modified in place.
 */

	pp = ax25_dup (recv_pp);
	assert (pp != NULL);

/*
 * Third party frames require special handling to unwrap payload.
 */
	while (ax25_get_dti(pp) == '}') {
	  packet_t inner_pp;

	  for (n = 0; n < ax25_get_num_repeaters(pp); n++) {
	    char via[AX25_MAX_ADDR_LEN];	/* includes ssid. Do we want to ignore it? */

	    ax25_get_addr_with_ssid (pp, n + AX25_REPEATER_1, via);

	    if (strcmp(via, "TCPIP") == 0 ||
	        strcmp(via, "TCPXX") == 0 ||
	        strcmp(via, "RFONLY") == 0 ||
	        strcmp(via, "NOGATE") == 0) {

	      if (s_debug >= 1) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("Rx IGate: Do not relay with %s in path.\n", via);
	      }

	      ax25_delete (pp);
	      return;
	    }
	  }

	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Unwrap third party message.\n");
	  }

	  inner_pp = ax25_unwrap_third_party(pp);
	  if (inner_pp == NULL) {
	    ax25_delete (pp);
	    return;
	  }
	  ax25_delete (pp);
	  pp = inner_pp;
	}

/* 
 * Do not relay packets with TCPIP, TCPXX, RFONLY, or NOGATE in the via path.
 */
	for (n = 0; n < ax25_get_num_repeaters(pp); n++) {
	  char via[AX25_MAX_ADDR_LEN];	/* includes ssid. Do we want to ignore it? */

	  ax25_get_addr_with_ssid (pp, n + AX25_REPEATER_1, via);

	  if (strcmp(via, "TCPIP") == 0 ||
	      strcmp(via, "TCPXX") == 0 ||
	      strcmp(via, "RFONLY") == 0 ||
	      strcmp(via, "NOGATE") == 0) {

	    if (s_debug >= 1) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Rx IGate: Do not relay with %s in path.\n", via);
	    }

	    ax25_delete (pp);
	    return;
	  }
	}

/*
 * Do not relay generic query.
 */
	if (ax25_get_dti(pp) == '?') {
	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Do not relay generic query.\n");
	  }
	  ax25_delete (pp);
	  return;
	}


/*
 * Cut the information part at the first CR or LF.
 */

	info_len = ax25_get_info (pp, &pinfo);
	(void)(info_len);

	if ((p = strchr ((char*)pinfo, '\r')) != NULL) {
	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Truncated information part at CR.\n");
	  }
          *p = '\0';
	}

	if ((p = strchr ((char*)pinfo, '\n')) != NULL) {
	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Truncated information part at LF.\n");
	  }
          *p = '\0';
	}


/*
 * Someone around here occasionally sends a packet with no information part.
 */
	if (strlen((char*)pinfo) == 0) {

	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Information part length is zero.\n");
	  }
	  ax25_delete (pp);
	  return;
	}

// TODO: Should we drop raw touch tone data object type generated here?


/*
 * If the SATgate mode is enabled, see if it should be delayed.
 * The rule is if we hear it directly and it has at least one
 * digipeater so there is potential of being re-transmitted.
 * (Digis are all unused if we are hearing it directly from source.)
 */
	if (save_igate_config_p->satgate_delay > 0 &&
	    ax25_get_heard(pp) == AX25_SOURCE &&
	    ax25_get_num_repeaters(pp) > 0) {

	  satgate_delay_packet (pp, chan);
	}
	else {
	  send_packet_to_server (pp, chan);
	}

} /* end igate_send_rec_packet */



/*-------------------------------------------------------------------
 *
 * Name:        send_packet_to_server
 *
 * Purpose:     Convert to text and send to the IGate server.
 *
 * Inputs:	pp 	- Packet object.
 *
 *		chan	- Radio channel where it was received.
 *
 * Description:	Duplicate detection is handled here.
 *		Suppress if same was sent recently.
 *
 *--------------------------------------------------------------------*/

static void send_packet_to_server (packet_t pp, int chan)
{
	unsigned char *pinfo;
	int info_len;
	char msg[IGATE_MAX_MSG];


	info_len = ax25_get_info (pp, &pinfo);
	(void)(info_len);

/*
 * Do not relay if a duplicate of something sent recently.
 */

	if ( ! rx_to_ig_allow(pp)) {
	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Drop duplicate of same packet seen recently.\n");
	  }
	  ax25_delete (pp);
	  return;
	}

/* 
 * Finally, append ",qAR," and my call to the path.
 */

	ax25_format_addrs (pp, msg);
	msg[strlen(msg)-1] = '\0';    /* Remove trailing ":" */
	strlcat (msg, ",qAR,", sizeof(msg));
	strlcat (msg, save_audio_config_p->achan[chan].mycall, sizeof(msg));
	strlcat (msg, ":", sizeof(msg));
	strlcat (msg, (char*)pinfo, sizeof(msg));

	send_msg_to_server (msg);
	stats_rx_igate_packets++;

/*
 * Remember what was sent to avoid duplicates in near future.
 */
	rx_to_ig_remember (pp);

	ax25_delete (pp);

} /* end send_packet_to_server */



/*-------------------------------------------------------------------
 *
 * Name:        send_msg_to_server
 *
 * Purpose:     Send to the IGate server.
 *		This one function should be used for login, hearbeats,
 *		and packets.
 *
 * Inputs:	imsg	- Message.  We will add CR/LF.
 *		
 *
 * Description:	Send message to IGate Server if connected.
 *		Disconnect from server, and notify user, if any error.
 *
 *--------------------------------------------------------------------*/


static void send_msg_to_server (const char *imsg)
{
	int err;
	char stemp[IGATE_MAX_MSG];

	if (igate_sock == -1) {
	  return;	/* Silently discard if not connected. */
	}

	strlcpy(stemp, imsg, sizeof(stemp));

	if (s_debug >= 1) {
	  text_color_set(DW_COLOR_XMIT);
	  dw_printf ("[rx>ig] ");
	  ax25_safe_print (stemp, strlen(stemp), 0);
	  dw_printf ("\n");
	}

	strlcat (stemp, "\r\n", sizeof(stemp));

	stats_uplink_bytes += strlen(stemp);

#if __WIN32__	
        err = send (igate_sock, stemp, strlen(stemp), 0);
	if (err == SOCKET_ERROR)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending message to IGate server.  Closing connection.\n\n", WSAGetLastError());
	  //dw_printf ("DEBUG: igate_sock=%d, line=%d\n", igate_sock, __LINE__);
	  closesocket (igate_sock);
	  igate_sock = -1;
	  WSACleanup();
	}
#else
        err = write (igate_sock, stemp, strlen(stemp));
	if (err <= 0)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError sending message to IGate server.  Closing connection.\n\n");
	  close (igate_sock);
	  igate_sock = -1;    
	}
#endif
	
} /* end send_msg_to_server */


/*-------------------------------------------------------------------
 *
 * Name:        get1ch
 *
 * Purpose:     Read one byte from socket.
 *
 * Inputs:	igate_sock	- file handle for socket.
 *
 * Returns:	One byte from stream.
 *		Waits and tries again later if any error.
 *
 *
 *--------------------------------------------------------------------*/

static int get1ch (void)
{
	unsigned char ch;
	int n;

	while (1) {

	  while (igate_sock == -1) {
	    SLEEP_SEC(5);			/* Not connected.  Try again later. */
	  }

	  /* Just get one byte at a time. */
	  // TODO: might read complete packets and unpack from own buffer
	  // rather than using a system call for each byte.

#if __WIN32__
	  n = recv (igate_sock, (char*)(&ch), 1, 0);
#else
	  n = read (igate_sock, &ch, 1);
#endif

	  if (n == 1) {
#if DEBUG9
	    dw_printf (log_fp, "%02x %c %c", ch, 
			isprint(ch) ? ch : '.' , 
			(isupper(ch>>1) || isdigit(ch>>1) || (ch>>1) == ' ') ? (ch>>1) : '.');
	    if (ch == '\r') fprintf (log_fp, "  CR");
	    if (ch == '\n') fprintf (log_fp, "  LF");
	    fprintf (log_fp, "\n");
#endif
	    return(ch);	
	  }

          text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError reading from IGate server.  Closing connection.\n\n");
#if __WIN32__
	  closesocket (igate_sock);
#else
	  close (igate_sock);
#endif
	  igate_sock = -1;
	}

} /* end get1ch */




/*-------------------------------------------------------------------
 *
 * Name:        igate_recv_thread
 *
 * Purpose:     Wait for messages from IGate Server.
 *
 * Inputs:	arg		- Not used.
 *
 * Outputs:	igate_sock	- File descriptor for communicating with client app.
 *
 * Description:	Process messages from the IGate server.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall igate_recv_thread (void *arg)
#else
static void * igate_recv_thread (void *arg)
#endif
{
	unsigned char ch;
	unsigned char message[1000];  // Spec says max 500 or so.
	int len;
	
			
#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("igate_recv_thread ( socket = %d )\n", igate_sock);
#endif

	while (1) {

	  len = 0;

	  do
	  {
	    ch = get1ch();
	    stats_downlink_bytes++;

	    if (len < sizeof(message)) 
	    {
	      message[len] = ch;
	    }
	    len++;
	    
	  } while (ch != '\n');

/*
 * We have a complete message terminated by LF.
 *
 * Remove CR LF from end.
 * This is a record separator for the protocol, not part of the data.
 * Should probably have an error if we don't have this.
 */
	  if (len >=2 && message[len-1] == '\n') { message[len-1] = '\0'; len--; }
	  if (len >=1 && message[len-1] == '\r') { message[len-1] = '\0'; len--; }

/*
 * I've seen a case where the original RF packet had a trailing CR but
 * after someone else sent it to the server and it came back to me, that
 * CR was now a trailing space.
 * At first I was tempted to trim a trailing space as well.
 * By fixing this one case it might corrupt the data in other cases.
 * We compensate for this by ignoring trailing spaces when performing
 * the duplicate detection and removal.
 */

/*
 * I've also seen a multiple trailing spaces like this.
 * Notice how safe_print shows a trailing space in hexadecimal to make it obvious.
 *
 * W1CLA-1>APVR30,TCPIP*,qAC,T2TOKYO3:;IRLP-4942*141503z4218.46NI07108.24W0446325-146IDLE    <0x20>
 */

	  if (len == 0) 
	  {
/* 
 * Discard if zero length. 
 */
	  }
	  else if (message[0] == '#') {
/*
 * Heartbeat or other control message.
 *
 * Print only if within seconds of logging in.
 * That way we can see login confirmation but not 
 * be bothered by the heart beat messages.
 */

	    if ( ! ok_to_send) {
	      text_color_set(DW_COLOR_REC);
	      dw_printf ("[ig] ");
	      ax25_safe_print ((char *)message, len, 0);
	      dw_printf ("\n");
	    }
	  }
	  else 
	  {
/*
 * Convert to third party packet and transmit.
 *
 * Future: might have ability to configure multiple transmit
 * channels, each with own client side filtering and via path.
 * Loop here over all configured channels.
 */
	    text_color_set(DW_COLOR_REC);
	    dw_printf ("\n[ig>tx] ");		// formerly just [ig]
	    ax25_safe_print ((char *)message, len, 0);
	    dw_printf ("\n");

	    int to_chan = save_igate_config_p->tx_chan;

	    if (to_chan >= 0) {
	      xmit_packet ((char*)message, to_chan);
	    }
	  }

	}  /* while (1) */
	return (0);

} /* end igate_recv_thread */



/*-------------------------------------------------------------------
 *
 * Name:        satgate_delay_packet
 *
 * Purpose:     Put packet into holding area for a while rather than
 *		sending it immediately to the IS server.
 *
 * Inputs:	pp	- Packet object.
 *
 *		chan	- Radio channel where received.
 *
 * Outputs:	Appended to queue.
 *
 * Description:	If we hear a packet directly and the same one digipeated,
 *		we only send the first to the APRS IS due to duplicate removal.
 *		It may be desirable to favor the digipeated packet over the
 *		original.  For this situation, we have an option which delays
 *		a packet if we hear it directly and the via path is not empty.
 *		We know we heard it directly if none of the digipeater
 *		addresses have been used.
 *		This way the digipeated packet will go first.
 *		The original is sent about 10 seconds later.
 *		Duplicate removal will drop the original if there is no
 *		corresponding digipeated version.
 *
 *--------------------------------------------------------------------*/

static void satgate_delay_packet (packet_t pp, int chan)
{
	packet_t pnext, plast;


	//if (s_debug >= 1) {
	  text_color_set(DW_COLOR_INFO);
	  dw_printf ("Rx IGate: SATgate mode, delay packet heard directly.\n");
	//}

	ax25_set_release_time (pp, dtime_now() + save_igate_config_p->satgate_delay);
//TODO: save channel too.

	dw_mutex_lock (&dp_mutex);

	if (dp_queue_head == NULL) {
	  dp_queue_head = pp;
	}
	else {
	  plast = dp_queue_head;
	  while ((pnext = ax25_get_nextp(plast)) != NULL) {
	    plast = pnext;
	  }
	  ax25_set_nextp (plast, pp);
	}

	dw_mutex_unlock (&dp_mutex);

} /* end satgate_delay_packet */



/*-------------------------------------------------------------------
 *
 * Name:        satgate_delay_thread
 *
 * Purpose:     Release packet when specified release time has arrived.
 *
 * Inputs:	dp_queue_head	- Queue of packets.
 *
 * Outputs:	Sent to APRS IS.
 *
 * Description:	For simplicity we'll just poll each second.
 *		Release the packet when its time has arrived.
 *
 *--------------------------------------------------------------------*/

#if __WIN32__
static unsigned __stdcall satgate_delay_thread (void *arg)
#else
static void * satgate_delay_thread (void *arg)
#endif
{
	double release_time;
	int chan = 0;				// TODO:  get receive channel somehow.
						// only matters if multi channel with different names.
		
	while (1) {
	  SLEEP_SEC (1);

/* Don't need critical region just to peek */

	  if (dp_queue_head != NULL) {

	    double now = dtime_now();

	    release_time = ax25_get_release_time (dp_queue_head);

#if 0
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("SATgate:  %.1f sec remaining\n", release_time - now);
#endif
	    if (now > release_time) {
	      packet_t pp;

	      dw_mutex_lock (&dp_mutex);

	      pp = dp_queue_head;
	      dp_queue_head = ax25_get_nextp(pp);

	      dw_mutex_unlock (&dp_mutex);
	      ax25_set_nextp (pp, NULL);

	      send_packet_to_server (pp, chan);
	    }
	  }  /* if something in queue */
	}  /* while (1) */
	return (0);

} /* end satgate_delay_thread */


/*-------------------------------------------------------------------
 *
 * Name:        xmit_packet
 *
 * Purpose:     Convert text string, from IGate server, to third party
 *		packet and send to transmit queue.
 *
 * Inputs:	message		- As sent by the server.  
 *				  Any trailing CRLF should have been removed.
 *				  Typical examples:
 *
 *				KA1BTK-5>APDR13,TCPIP*,qAC,T2IRELAND:=4237.62N/07040.68W$/A=-00054 http://aprsdroid.org/
 *				N1HKO-10>APJI40,TCPIP*,qAC,N1HKO-JS:<IGATE,MSG_CNT=0,LOC_CNT=0
 *				K1RI-2>APWW10,WIDE1-1,WIDE2-1,qAS,K1RI:/221700h/9AmA<Ct3_ sT010/002g005t045r000p023P020h97b10148
 *				KC1BOS-2>T3PQ3S,WIDE1-1,WIDE2-1,qAR,W1TG-1:`c)@qh\>/"50}TinyTrak4 Mobile
 *
 *				  Notice how the final address in the header might not
 *				  be a valid AX.25 address.  We see a 9 character address
 *				  (with no ssid) and an ssid of two letters.
 *				  We don't care because we end up discarding them before
 *				  repackaging to go over the radio.
 *
 *				  The "q construct"  ( http://www.aprs-is.net/q.aspx ) provides
 *				  a clue about the journey taken but I don't think we care here.
 *
 *		to_chan		- Radio channel for transmitting.
 *
 *--------------------------------------------------------------------*/

static void xmit_packet (char *message, int to_chan)
{
	packet_t pp3;
	char payload[AX25_MAX_PACKET_LEN];	/* what is max len? */
	char *pinfo = NULL;
	int info_len;

	assert (to_chan >= 0 && to_chan < MAX_CHANS);


/*
 * Try to parse it into a packet object.
 * This will contain "q constructs" and we might see an address
 * with two alphnumeric characters in the SSID so we must use
 * the non-strict parsing.
 *
 * Bug:  Up to 8 digipeaters are allowed in radio format.
 * There is a potential of finding a larger number here.
 */
	pp3 = ax25_from_text(message, 0);
	if (pp3 == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tx IGate: Could not parse message from server.\n");
	  dw_printf ("%s\n", message);
	  return;
	}


/*
 * Apply our own packet filtering if configured.
 * Do we want to do this before or after removing the VIA path?
 * I suppose by doing it first, we have the possibility of
 * filtering by stations along the way or the q construct.
 */

	assert (to_chan >= 0 && to_chan < MAX_CHANS);

	if (save_digi_config_p->filter_str[MAX_CHANS][to_chan] != NULL) {

	  if (pfilter(MAX_CHANS, to_chan, save_digi_config_p->filter_str[MAX_CHANS][to_chan], pp3) != 1) {

	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Packet from IGate to channel %d was rejected by filter: %s\n", to_chan, save_digi_config_p->filter_str[MAX_CHANS][to_chan]);

	    ax25_delete (pp3);
	    return;
	  }
	}


/*
 * Remove the VIA path.
 *
 * For example, we might get something like this from the server.
 *	K1USN-1>APWW10,TCPIP*,qAC,N5JXS-F1:T#479,100,048,002,500,000,10000000<0x0d><0x0a>
 *
 * We want to reduce it to this before wrapping it as third party traffic.
 *	K1USN-1>APWW10:T#479,100,048,002,500,000,10000000<0x0d><0x0a>
 */

	while (ax25_get_num_repeaters(pp3) > 0) {
	  ax25_remove_addr (pp3, AX25_REPEATER_1);
	}


/* 
 * Replace the VIA path with TCPIP and my call.
 * Mark my call as having been used.
 */
	ax25_set_addr (pp3, AX25_REPEATER_1, "TCPIP");
	ax25_set_h (pp3, AX25_REPEATER_1);
	ax25_set_addr (pp3, AX25_REPEATER_2, save_audio_config_p->achan[to_chan].mycall);
	ax25_set_h (pp3, AX25_REPEATER_2);

/*
 * Convert to text representation.
 */
	memset (payload, 0, sizeof(payload));

	ax25_format_addrs (pp3, payload);
	info_len = ax25_get_info (pp3, (unsigned char **)(&pinfo));
	(void)(info_len);
	strlcat (payload, pinfo, sizeof(payload));
#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("Tx IGate: payload=%s\n", payload);
#endif
	
/*
 * Encapsulate for sending over radio if no reason to drop it.
 */
	if (ig_to_tx_allow (pp3, to_chan)) {
	  char radio [500];
	  packet_t pradio;

	  snprintf (radio, sizeof(radio), "%s>%s%d%d%s:}%s",
				save_audio_config_p->achan[to_chan].mycall,
				APP_TOCALL, MAJOR_VERSION, MINOR_VERSION,
				save_igate_config_p->tx_via,
				payload);

	  pradio = ax25_from_text (radio, 1);

	  /* Oops.  Didn't have a check for NULL here. */
	  /* Could this be the cause of rare and elusive crashes in 1.2? */

	  if (pradio != NULL) {

	    stats_tx_igate_packets++;

#if ITEST
	    text_color_set(DW_COLOR_XMIT);
	    dw_printf ("Xmit: %s\n", radio);
	    ax25_delete (pradio);
#else
	    /* This consumes packet so don't reference it again! */
	    tq_append (to_chan, TQ_PRIO_1_LO, pradio);
#endif
	    stats_rf_xmit_packets++;
	    ig_to_tx_remember (pp3, save_igate_config_p->tx_chan, 0);	// correct. version before encapsulating it.
	  }
	  else {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf ("Received invalid packet from IGate.\n");
	    dw_printf ("%s\n", payload);	
	    dw_printf ("Will not attempt to transmit third party packet.\n");
	    dw_printf ("%s\n", radio);	
	  }

	}

	ax25_delete (pp3);

} /* end xmit_packet */



/*-------------------------------------------------------------------
 *
 * Name:        rx_to_ig_remember
 *
 * Purpose:     Keep a record of packets sent to the IGate server
 *		so we don't send duplicates within some set amount of time.
 *
 * Inputs:	pp	- Pointer to a packet object.
 *
 *-------------------------------------------------------------------
 *
 * Name:	rx_to_ig_allow
 * 
 * Purpose:	Check whether this is a duplicate of another
 *		recently received from RF and sent to the Server
 *
 * Input:	pp	- Pointer to packet object.
 *		
 * Returns:	True if it is OK to send.
 *		
 *-------------------------------------------------------------------
 *
 * Description: These two functions perform the final stage of filtering
 *		before sending a received (from radio) packet to the IGate server.
 *
 *		rx_to_ig_remember must be called for every packet sent to the server.
 *
 *		rx_to_ig_allow decides whether this should be allowed thru
 *		based on recent activity.  We will drop the packet if it is a
 *		duplicate of another sent recently.
 *
 *		Rather than storing the entire packet, we just keep a CRC to 
 *		reduce memory and processing requirements.  We do the same in
 *		the digipeater function to suppress duplicates.
 *
 *		There is a 1 / 65536 chance of getting a false positive match
 *		which is good enough for this application.
 *
 *--------------------------------------------------------------------*/

#define RX2IG_DEDUPE_TIME 60		/* Do not send duplicate within 60 seconds. */
#define RX2IG_HISTORY_MAX 30		/* Remember the last 30 sent to IGate server. */

static int rx2ig_insert_next;
static time_t rx2ig_time_stamp[RX2IG_HISTORY_MAX];
static unsigned short rx2ig_checksum[RX2IG_HISTORY_MAX];

static void rx_to_ig_init (void)
{
	int n;
	for (n=0; n<RX2IG_HISTORY_MAX; n++) {
	  rx2ig_time_stamp[n] = 0;
	  rx2ig_checksum[n] = 0;
	}
	rx2ig_insert_next = 0;
}
	

static void rx_to_ig_remember (packet_t pp)
{

       	rx2ig_time_stamp[rx2ig_insert_next] = time(NULL);
        rx2ig_checksum[rx2ig_insert_next] = ax25_dedupe_crc(pp);

	if (s_debug >= 3) {
	  char src[AX25_MAX_ADDR_LEN];
	  char dest[AX25_MAX_ADDR_LEN];
	  unsigned char *pinfo;
	  int info_len;

	  ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	  ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);
	  info_len = ax25_get_info (pp, &pinfo);

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rx_to_ig_remember [%d] = %d %d \"%s>%s:%s\"\n",
			rx2ig_insert_next,
			(int)(rx2ig_time_stamp[rx2ig_insert_next]),
			rx2ig_checksum[rx2ig_insert_next],
			src, dest, pinfo);
	}

        rx2ig_insert_next++;
        if (rx2ig_insert_next >= RX2IG_HISTORY_MAX) {
          rx2ig_insert_next = 0;
        }
}

static int rx_to_ig_allow (packet_t pp)
{
	unsigned short crc = ax25_dedupe_crc(pp);
	time_t now = time(NULL);
	int j;

	if (s_debug >= 2) {
	  char src[AX25_MAX_ADDR_LEN];
	  char dest[AX25_MAX_ADDR_LEN];
	  unsigned char *pinfo;
	  int info_len;

	  ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	  ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);
	  info_len = ax25_get_info (pp, &pinfo);

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rx_to_ig_allow? %d \"%s>%s:%s\"\n", crc, src, dest, pinfo);
	}

	for (j=0; j<RX2IG_HISTORY_MAX; j++) {
	  if (rx2ig_checksum[j] == crc && rx2ig_time_stamp[j] >= now - RX2IG_DEDUPE_TIME) {
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      // could be multiple entries and this might not be the most recent.
	      dw_printf ("rx_to_ig_allow? NO. Seen %d seconds ago.\n", (int)(now - rx2ig_time_stamp[j]));
	    }
	    return 0;
	  }
	}

	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rx_to_ig_allow? YES\n");
	}
	return 1;

} /* end rx_to_ig_allow */



/*-------------------------------------------------------------------
 *
 * Name:        ig_to_tx_remember
 *
 * Purpose:     Keep a record of packets sent from IGate server to radio transmitter
 *		so we don't send duplicates within some set amount of time.
 *
 * Inputs:	pp	- Pointer to a packet object.
 *
 *		chan	- Channel number where it is being transmitted.
 *			  Duplicate detection needs to be separate for each radio channel.
 *
 *		bydigi	- True if transmitted by digipeater function.  False for IGate.
 *			  Why do we care about digpeating here?  See discussion below.
 *
 *------------------------------------------------------------------------------
 *
 * Name:	ig_to_tx_allow
 * 
 * Purpose:	Check whether this is a duplicate of another sent recently
 *		or if we exceed the transmit rate limits.
 *
 * Input:	pp	- Pointer to packet object.
 *
 *		chan	- Radio channel number where we want to transmit.
 *		
 * Returns:	True if it is OK to send.
 *		
 *------------------------------------------------------------------------------
 *
 * Description: These two functions perform the final stage of filtering
 *		before sending a packet from the IGate server to the radio.
 *
 *		ig_to_tx_remember must be called for every packet, from the IGate 
 *		server, sent to the radio transmitter.
 *
 *		ig_to_tx_allow decides whether this should be allowed thru
 *		based on recent activity.  We will drop the packet if it is a
 *		duplicate of another sent recently.
 *
 *		This is the essentially the same as the pair of functions
 *		above with one addition restriction.  
 *
 *		The typical residential Internet connection is around 10,000
 *		to 50,000 times faster than the radio links we are using.  It would
 *		be easy to completely saturate the radio channel if we are
 *		not careful.
 *
 *		Besides looking for duplicates, this will also tabulate the 
 *		number of packets sent during the past minute and past 5
 *		minutes and stop sending if a limit is reached.
 *
 * More Discussion:
 *
 *		Consider the following example.
 *		I hear a packet from W1TG-1 three times over the radio then get the
 *		(almost) same thing twice from APRS-IS.
 *
 *
 *		Digipeater N3LEE-10 audio level = 23(10/6)   [NONE]   __|||||||
 *		[0.5] W1TG-1>APU25N,N3LEE-10*,WIDE2-1:<IGATE,MSG_CNT=30,LOC_CNT=61<0x0d>
 *		Station Capabilities, Ambulance, UIview 32 bit apps
 *		IGATE,MSG_CNT=30,LOC_CNT=61
 *
 *		[0H] W1TG-1>APU25N,N3LEE-10,WB2OSZ-14*:<IGATE,MSG_CNT=30,LOC_CNT=61<0x0d>
 *
 *		Digipeater WIDE2 (probably N3LEE-4) audio level = 22(10/6)   [NONE]   __|||||||
 *		[0.5] W1TG-1>APU25N,N3LEE-10,N3LEE-4,WIDE2*:<IGATE,MSG_CNT=30,LOC_CNT=61<0x0d>
 *		Station Capabilities, Ambulance, UIview 32 bit apps
 *		IGATE,MSG_CNT=30,LOC_CNT=61
 *
 *		Digipeater WIDE2 (probably AB1OC-10) audio level = 31(14/11)   [SINGLE]   ____:____
 *		[0.4] W1TG-1>APU25N,N3LEE-10,AB1OC-10,WIDE2*:<IGATE,MSG_CNT=30,LOC_CNT=61<0x0d>
 *		Station Capabilities, Ambulance, UIview 32 bit apps
 *		IGATE,MSG_CNT=30,LOC_CNT=61
 *
 *		[ig] W1TG-1>APU25N,WIDE2-2,qAR,W1GLO-11:<IGATE,MSG_CNT=30,LOC_CNT=61
 *		[0L] WB2OSZ-14>APDW13,WIDE1-1:}W1TG-1>APU25N,TCPIP,WB2OSZ-14*:<IGATE,MSG_CNT=30,LOC_CNT=61
 *
 *		[ig] W1TG-1>APU25N,K1FFK,WIDE2*,qAR,WB2ZII-15:<IGATE,MSG_CNT=30,LOC_CNT=61<0x20>
 *		[0L] WB2OSZ-14>APDW13,WIDE1-1:}W1TG-1>APU25N,TCPIP,WB2OSZ-14*:<IGATE,MSG_CNT=30,LOC_CNT=61<0x20>
 *
 *
 *		The first one gets retransmitted by digipeating.
 *
 *		Why are we getting the same thing twice from APRS-IS?  Shouldn't remove duplicates?
 *		Look closely.  The original packet, on RF, had a CR character at the end.
 *		At first I thought duplicate removal was broken but it turns out they
 *		are not exactly the same.
 *
 *		The receive IGate spec says a packet should be cut at a CR.
 *		In one case it is removed as expected   In another case, it is replaced by a trailing
 *		space character.  Maybe someone thought non printable characters should be
 *		replaced by spaces???
 *
 *		At first I was tempted to remove any trailing spaces to make up for the other
 *		IGate adding it.  Two wrongs don't make a right.   Trailing spaces are not that
 *		rare and removing them would corrupt the data.  My new strategy is for
 *		the duplicate detection compare to ignore trailing space, CR, and LF.
 *
 *		We already transmitted the same thing by the digipeater function so this should
 *		also go into memory for avoiding duplicates out of the transmit IGate.
 *
 * Future:
 *		Should the digipeater function avoid transmitting something if it
 *		was recently transmitted by the IGate funtion?
 *		This code is pretty much the same as dedupe.c. Maybe it could all
 *		be combined into one.  Need to ponder this some more.
 * 
 *--------------------------------------------------------------------*/

/*
Here is another complete example, with the "-diii" debugging option to show details.


We receive the signal directly from the source: (zzz.log 1011)

	N1ZKO-7 audio level = 33(16/10)   [NONE]   ___||||||
	[0.5] N1ZKO-7>T2TS7X,WIDE1-1,WIDE2-1:`c6wl!i[/>"4]}[scanning]=<0x0d>
	MIC-E, Human, Kenwood TH-D72, In Service
	N 42 43.7800, W 071 26.9100, 0 MPH, course 177, alt 230 ft
	[scanning]

We did not send it to the IS server recently.

	Rx IGate: Truncated information part at CR.
	rx_to_ig_allow? 57185 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]="
	rx_to_ig_allow? YES

Send it now and remember that fact.

	[rx>ig] N1ZKO-7>T2TS7X,WIDE1-1,WIDE2-1,qAR,WB2OSZ-14:`c6wl!i[/>"4]}[scanning]=
	rx_to_ig_remember [21] = 1447683040 57185 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]="

Digipeat it.  Notice how it has a trailing CR.
TODO:  Why is the CRC different?  Content looks the same.

	ig_to_tx_remember [38] = ch0 d1 1447683040 27598 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]="
	[0H] N1ZKO-7>T2TS7X,WB2OSZ-14*,WIDE2-1:`c6wl!i[/>"4]}[scanning]=<0x0d>

Now we hear it again, thru a digipeater.
Not sure who.   Was it UNCAN or was it someone else who doesn't use tracing?
See my rant in the User Guide about this.

	Digipeater WIDE2 (probably UNCAN) audio level = 30(15/10)   [NONE]   __|||::__
	[0.4] N1ZKO-7>T2TS7X,KB1POR-2,UNCAN,WIDE2*:`c6wl!i[/>"4]}[scanning]=<0x0d>
	MIC-E, Human, Kenwood TH-D72, In Service
	N 42 43.7800, W 071 26.9100, 0 MPH, course 177, alt 230 ft
	[scanning]

Was sent to server recently so don't do it again.

	Rx IGate: Truncated information part at CR.
	rx_to_ig_allow? 57185 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]="
	rx_to_ig_allow? NO. Seen 1 seconds ago.
	Rx IGate: Drop duplicate of same packet seen recently.

We hear it a third time, by a different digipeater.

	Digipeater WIDE1 (probably N3LEE-10) audio level = 23(12/6)   [NONE]   __|||||||
	[0.5] N1ZKO-7>T2TS7X,N3LEE-10,WIDE1*,WIDE2-1:`c6wl!i[/>"4]}[scanning]=<0x0d>
	MIC-E, Human, Kenwood TH-D72, In Service
	N 42 43.7800, W 071 26.9100, 0 MPH, course 177, alt 230 ft
	[scanning]

It's a duplicate, so don't send to server.

	Rx IGate: Truncated information part at CR.
	rx_to_ig_allow? 57185 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]="
	rx_to_ig_allow? NO. Seen 2 seconds ago.
	Rx IGate: Drop duplicate of same packet seen recently.
	Digipeater: Drop redundant packet to channel 0.

The server sends it to us.
NOTICE: The CR at the end has been replaced by a space.

	[ig>tx] N1ZKO-7>T2TS7X,K1FFK,WA2MJM-15*,qAR,WB2ZII-15:`c6wl!i[/>"4]}[scanning]=<0x20>

Should we transmit it?
No, we sent it recently by the digipeating function (note "bydigi=1").

	DEBUG:  ax25_dedupe_crc ignoring trailing space.
	ig_to_tx_allow? ch0 27598 "N1ZKO-7>T2TS7X:`c6wl!i[/>"4]}[scanning]= "
	ig_to_tx_allow? NO. Sent 4 seconds ago. bydigi=1
	Tx IGate: Drop duplicate packet transmitted recently.
	[0L] WB2OSZ-14>APDW13,WIDE1-1:}W1AST>TRPR4T,TCPIP,WB2OSZ-14*:`d=Ml!3>/"4N}
	[rx>ig] #
*/


#define IG2TX_DEDUPE_TIME 60		/* Do not send duplicate within 60 seconds. */
#define IG2TX_HISTORY_MAX 50		/* Remember the last 50 sent from server to radio. */

/* Ideally this should be a critical region because */
/* it is being written by two threads but I'm not that concerned. */

static int ig2tx_insert_next;
static time_t ig2tx_time_stamp[IG2TX_HISTORY_MAX];
static unsigned short ig2tx_checksum[IG2TX_HISTORY_MAX];
static unsigned char ig2tx_chan[IG2TX_HISTORY_MAX];
static unsigned short ig2tx_bydigi[IG2TX_HISTORY_MAX];

static void ig_to_tx_init (void)
{
	int n;
	for (n=0; n<IG2TX_HISTORY_MAX; n++) {
	  ig2tx_time_stamp[n] = 0;
	  ig2tx_checksum[n] = 0;
	  ig2tx_chan[n] = 0xff;
	  ig2tx_bydigi[n] = 0;
	}
	ig2tx_insert_next = 0;
}
	

void ig_to_tx_remember (packet_t pp, int chan, int bydigi)
{
	time_t now = time(NULL);
	unsigned short crc = ax25_dedupe_crc(pp);

	if (s_debug >= 3) {
	  char src[AX25_MAX_ADDR_LEN];
	  char dest[AX25_MAX_ADDR_LEN];
	  unsigned char *pinfo;
	  int info_len;

	  ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	  ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);
	  info_len = ax25_get_info (pp, &pinfo);

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ig_to_tx_remember [%d] = ch%d d%d %d %d \"%s>%s:%s\"\n",
			ig2tx_insert_next,
			chan, bydigi,
			(int)(now), crc,
			src, dest, pinfo);
	}

	ig2tx_time_stamp[ig2tx_insert_next] = now;
	ig2tx_checksum[ig2tx_insert_next] = crc;
	ig2tx_chan[ig2tx_insert_next] = chan;
	ig2tx_bydigi[ig2tx_insert_next] = bydigi;

        ig2tx_insert_next++;
        if (ig2tx_insert_next >= IG2TX_HISTORY_MAX) {
          ig2tx_insert_next = 0;
        }
}

static int ig_to_tx_allow (packet_t pp, int chan)
{
	unsigned short crc = ax25_dedupe_crc(pp);
	time_t now = time(NULL);
	int j;
	int count_1, count_5;

	if (s_debug >= 2) {
	  char src[AX25_MAX_ADDR_LEN];
	  char dest[AX25_MAX_ADDR_LEN];
	  unsigned char *pinfo;
	  int info_len;

	  ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	  ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);
	  info_len = ax25_get_info (pp, &pinfo);

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ig_to_tx_allow? ch%d %d \"%s>%s:%s\"\n", chan, crc, src, dest, pinfo);
	}

	/* Consider transmissions on this channel only by either digi or IGate. */

	for (j=0; j<IG2TX_HISTORY_MAX; j++) {
	  if (ig2tx_checksum[j] == crc && ig2tx_chan[j] == chan && ig2tx_time_stamp[j] >= now - IG2TX_DEDUPE_TIME) {
	    if (s_debug >= 2) {
	      text_color_set(DW_COLOR_DEBUG);
	      // could be multiple entries and this might not be the most recent.
	      dw_printf ("ig_to_tx_allow? NO. Sent %d seconds ago. bydigi=%d\n", (int)(now - ig2tx_time_stamp[j]), ig2tx_bydigi[j]);
	    }
	    text_color_set(DW_COLOR_INFO);
	    dw_printf ("Tx IGate: Drop duplicate packet transmitted recently.\n");
	    return 0;
	  }
	}

	/* IGate transmit counts must not include digipeater transmissions. */

	count_1 = 0;
	count_5 = 0;
	for (j=0; j<IG2TX_HISTORY_MAX; j++) {
	  if (ig2tx_chan[j] == chan && ig2tx_bydigi[j] == 0) {
	    if (ig2tx_time_stamp[j] >= now - 60) count_1++;
	    if (ig2tx_time_stamp[j] >= now - 300) count_5++;
	  }
	}

	if (count_1 >= save_igate_config_p->tx_limit_1) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tx IGate: Already transmitted maximum of %d packets in 1 minute.\n", save_igate_config_p->tx_limit_1);
	  return 0;
	}
	if (count_5 >= save_igate_config_p->tx_limit_5) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tx IGate: Already transmitted maximum of %d packets in 5 minutes.\n", save_igate_config_p->tx_limit_5);
	  return 0;
	}

	if (s_debug >= 2) {
	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ig_to_tx_allow? YES\n");
	}

	return 1;

} /* end ig_to_tx_allow */

/* end igate.c */

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2013, 2014, 2015, 2016, 2023  John Langner, WB2OSZ
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
 *		(now gone but you can find a copy here:)
 *		https://web.archive.org/web/20120503201832/http://wiki.ham.fi/APRS_iGate_properties
 *
 *		Notes to iGate developers
 *		https://github.com/hessu/aprsc/blob/master/doc/IGATE-HINTS.md#igates-dropping-duplicate-packets-unnecessarily
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

#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__

/* The goal is to support Windows XP and later. */

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
#include "dlq.h"
#include "igate.h"
#include "latlong.h"
#include "pfilter.h"
#include "dtime_now.h"
#include "mheard.h"



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
static void send_msg_to_server (const char *msg, int msg_len);
static void maybe_xmit_packet_from_igate (char *message, int chan);

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

// TODO:  Add to automated tests.

/* For unit testing. */

int main (int argc, char *argv[])
{
	struct audio_s audio_config;
	struct igate_config_s igate_config;
	struct digi_config_s digi_config;
	packet_t pp;

	memset (&audio_config, 0, sizeof(audio_config));
	audio_config.adev[0].num_channels = 2;
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

	igate_init(&audio_config, &igate_config, &digi_config, 0);

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
	  send_msg_to_server ("W1ABC>APRS:?", strlen("W1ABC>APRS:?"));
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
 * Statistics for IGate function.
 * Note that the RF related counters are just a subset of what is happening on radio channels.
 *
 * TODO: should have debug option to print these occasionally.
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
					/* This is not the total number of AX.25 frames received */
					/* over the radio; only APRS packets get this far. */

static int stats_uplink_packets;	/* Number of packets passed along to the IGate */
					/* server after filtering. */

static int stats_uplink_bytes;		/* Total number of bytes sent to IGate server */
					/* including login, packets, and heartbeats. */

static int stats_downlink_bytes;	/* Total number of bytes from IGate server including */
					/* packets, heartbeats, other messages. */

static int stats_downlink_packets;	/* Number of packets from IGate server for possible transmission. */
					/* Fewer might be transmitted due to filtering or rate limiting. */

static int stats_rf_xmit_packets;	/* Number of packets passed along to radio, for the IGate function, */
					/* after filtering, rate limiting, or other restrictions. */
					/* Number of packets transmitted for beacons, digipeating, */
					/* or client applications are not included here. */

static int stats_msg_cnt;		/* Number of "messages" transmitted.  Subset of above. */
					/* A "message" has the data type indicator of ":" and it is */
					/* not the special case of telemetry metadata. */


/*
 * Make some of these available for IGate statistics beacon like
 *
 *	WB2OSZ>APDW14,WIDE1-1:<IGATE,MSG_CNT=2,PKT_CNT=0,DIR_CNT=10,LOC_CNT=35,RF_CNT=45
 *
 * MSG_CNT is only "messages."   From original spec.
 * PKT_CNT is other (non-message) packets.  Followed precedent of APRSISCE32.
 */

int igate_get_msg_cnt (void) {
	return (stats_msg_cnt);
}

int igate_get_pkt_cnt (void) {
	return (stats_rf_xmit_packets - stats_msg_cnt);
}

int igate_get_upl_cnt (void) {
	return (stats_uplink_packets);
}

int igate_get_dnl_cnt (void) {
	return (stats_downlink_packets);
}



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
	stats_uplink_packets = 0;
	stats_uplink_bytes = 0;
	stats_downlink_bytes = 0;
	stats_downlink_packets = 0;
	stats_rf_xmit_packets = 0;
	stats_msg_cnt = 0;
	
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
	e = pthread_create (&connect_listen_tid, NULL, connnect_thread, NULL);
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
	      send_msg_to_server (stemp, strlen(stemp));

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
	    send_msg_to_server (heartbeat, strlen(heartbeat));

	  }
	}

	exit(0);	// Unreachable but stops compiler from complaining
			// about function not returning a value.
} /* end connnect_thread */




/*-------------------------------------------------------------------
 *
 * Name:        igate_send_rec_packet
 *
 * Purpose:     Send a packet to the IGate server
 *
 * Inputs:	chan	- Radio channel it was received on.
 *			  This is required for the RF>IS filtering.
 *		          Beaconing (sendto=ig, chan=-1) and a client app sending
 *			  to ICHANNEL should bypass the filtering.
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

#define IGATE_MAX_MSG 512	/* "All 'packets' sent to APRS-IS must be in the TNC2 format terminated */
				/* by a carriage return, line feed sequence. No line may exceed 512 bytes */
				/* including the CR/LF sequence." */

void igate_send_rec_packet (int chan, packet_t recv_pp)
{
	packet_t pp;
	int n;
	unsigned char *pinfo;
	int info_len;
	

	if (igate_sock == -1) {
	  return;	/* Silently discard if not connected. */
	}

	if ( ! ok_to_send) {
	  return;	/* Login not complete. */
	}

	/* Gather statistics. */

	stats_rf_recv_packets++;

/*
 * Check for filtering from specified channel to the IGate server.
 *
 * Should we do this after unwrapping the payload from a third party packet?
 * In my experience, third party packets have only been seen coming from IGates.
 * In that case, the payload will have TCPIP in the path and it will be dropped.
 */

// Apply RF>IS filtering only if it same from a radio channel.
// Beacon will be channel -1.
// Client app to ICHANNEL is outside of radio channel range.

	if (chan >= 0 && chan < MAX_CHANS && 		// in radio channel range
		save_digi_config_p->filter_str[chan][MAX_CHANS] != NULL) {

	  if (pfilter(chan, MAX_CHANS, save_digi_config_p->filter_str[chan][MAX_CHANS], recv_pp, 1) != 1) {

	    // Is this useful troubleshooting information or just distracting noise?
	    // Originally this was always printed but there was a request to add a "quiet" option to suppress this.
	    // version 1.4: Instead, make the default off and activate it only with the debug igate option.

	    if (s_debug >= 1) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Packet from channel %d to IGate was rejected by filter: %s\n", chan, save_digi_config_p->filter_str[chan][MAX_CHANS]);
	    }
	    return;
	  }
	}

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
 * This is required because CR/LF is used as record separator when sending to server.
 * Do NOT trim trailing spaces.
 * Starting in 1.4 we preserve any nul characters in the information part.
 */

	if (ax25_cut_at_crlf (pp) > 0) {
	  if (s_debug >= 1) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("Rx IGate: Truncated information part at CR.\n");
	  }
	}

	info_len = ax25_get_info (pp, &pinfo);


/*
 * Someone around here occasionally sends a packet with no information part.
 */
	if (info_len == 0) {

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

/*
 * We will often see the same packet multiple times close together due to digipeating.
 * The consensus seems to be that we should just send the first and drop the later duplicates.
 * There is some dissent on this issue. http://www.tapr.org/pipermail/aprssig/2016-July/045907.html
 * There could be some value to sending them all to provide information about digipeater paths.
 * However, the servers should drop all duplicates so we wasting everyone's time but sending duplicates.
 * If you feel strongly about this issue, you could remove the following section.
 * Currently rx_to_ig_allow only checks for recent duplicates.
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

/*
 * It seems that the specification has changed recently.
 * http://www.tapr.org/pipermail/aprssig/2016-December/046456.html
 *
 * We can see the history at the Internet Archive Wayback Machine.
 *
 * http://www.aprs-is.net/Connecting.aspx
 *	captured Oct 19, 2016:
 *		... Only the qAR construct may be generated by a client (IGate) on APRS-IS.
 * 	Captured Dec 1, 2016:
 *		... Only the qAR and qAO constructs may be generated by a client (IGate) on APRS-IS.
 *
 * http://www.aprs-is.net/q.aspx
 *	Captured April 23, 2016:
 *		(no mention of client generating qAO.)
 *	Captured July 19, 2016:
 *		qAO - (letter O) Packet is placed on APRS-IS by a receive-only IGate from RF.
 *		The callSSID following the qAO is the callSSID of the IGate. Note that receive-only
 *		IGates are discouraged on standard APRS frequencies. Please consider a bidirectional
 *		IGate that only gates to RF messages for stations heard directly.
 */

	ax25_format_addrs (pp, msg);
	msg[strlen(msg)-1] = '\0';    /* Remove trailing ":" */

	if (save_igate_config_p->tx_chan >= 0) {
	  strlcat (msg, ",qAR,", sizeof(msg));
	}
	else {
	  strlcat (msg, ",qAO,", sizeof(msg));		// new for version 1.4.
	}

	strlcat (msg, save_audio_config_p->achan[chan].mycall, sizeof(msg));
	strlcat (msg, ":", sizeof(msg));



// It was reported that APRS packets, containing a nul byte in the information part,
// are being truncated.  https://github.com/wb2osz/direwolf/issues/84
//
// One might argue that the packets are invalid and the proper behavior would be
// to simply discard them, the same way we do if the CRC is bad.  One might argue
// that we should simply pass along whatever we receive even if we don't like it.
// We really shouldn't modify it and make the situation even worse.
//
// Chapter 5 of the APRS spec ( http://www.aprs.org/doc/APRS101.PDF ) says:
//
// 	"The comment may contain any printable ASCII characters (except | and ~,
// 	which are reserved for TNC channel switching)."
//
// "Printable" would exclude character values less than space (00100000), e.g.
// tab, carriage return, line feed, nul.  Sometimes we see carriage return
// (00001010) at the end of APRS packets.   This would be in violation of the
// specification.
//
// The MIC-E position format can have non printable characters (0x1c ... 0x1f, 0x7f)
// in the information part.  An unfortunate decision, but it is not in the comment part.
//
// The base 91 telemetry format (http://he.fi/doc/aprs-base91-comment-telemetry.txt ),
// which is not part of the APRS spec, uses the | character in the comment to delimit encoded
// telemetry data.   This would be in violation of the original spec.  No one cares.
//
// The APRS Spec Addendum 1.2 Proposals ( http://www.aprs.org/aprs12/datum.txt)
// adds use of UTF-8 (https://en.wikipedia.org/wiki/UTF-8 )for the free form text in
// messages and comments. It can't be used in the fixed width fields.
//
// Non-ASCII characters are represented by multi-byte sequences.  All bytes in these
// multi-byte sequences have the most significant bit set to 1.  Using UTF-8 would not
// add any nul (00000000) bytes to the stream.
//
// Based on all of that, we would not expect to see a nul character in the information part.
//
// There are two known cases where we can have a nul character value.
//
// * The Kenwood TM-D710A sometimes sends packets like this:
//
// 	VA3AJ-9>T2QU6X,VE3WRC,WIDE1,K8UNS,WIDE2*:4P<0x00><0x0f>4T<0x00><0x0f>4X<0x00><0x0f>4\<0x00>`nW<0x1f>oS8>/]"6M}driving fast= 
// 	K4JH-9>S5UQ6X,WR4AGC-3*,WIDE1*:4P<0x00><0x0f>4T<0x00><0x0f>4X<0x00><0x0f>4\<0x00>`jP}l"&>/]"47}QRV from the EV =
//
//   Notice that the data type indicator of "4" is not valid.  If we remove
//   4P<0x00><0x0f>4T<0x00><0x0f>4X<0x00><0x0f>4\<0x00>   we are left with a good MIC-E format.
//   This same thing has been observed from others and is intermittent.
//
// * AGW Tracker can send UTF-16 if an option is selected.  This can introduce nul bytes.
//   This is wrong, it should be using UTF-8.
//
// Rather than using strlcat here, we need to use memcpy and maintain our
// own lengths, being careful to avoid buffer overflow.

	int msg_len = strlen(msg);	// What we have so far before info part.

	if (info_len > IGATE_MAX_MSG - msg_len - 2) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Rx IGate: Too long. Truncating.\n");
	  info_len = IGATE_MAX_MSG - msg_len - 2;
	}
	if (info_len > 0) {
	  memcpy (msg + msg_len, pinfo, info_len);
	  msg_len += info_len;
	}

	send_msg_to_server (msg, msg_len);
	stats_uplink_packets++;

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
 * Purpose:     Send something to the IGate server.
 *		This one function should be used for login, heartbeats,
 *		and packets.
 *
 * Inputs:	imsg	- Message.  We will add CR/LF here.
 *		
 *		imsg_len - Length of imsg in bytes.
 *			  It could contain nul characters so we can't
 *			  use the normal C string functions.
 *
 * Description:	Send message to IGate Server if connected.
 *		Disconnect from server, and notify user, if any error.
 *		Should use a word other than message because that has
 *		a specific meaning for APRS.
 *
 *--------------------------------------------------------------------*/


static void send_msg_to_server (const char *imsg, int imsg_len)
{
	int err;
	char stemp[IGATE_MAX_MSG+1];
	int stemp_len;

	if (igate_sock == -1) {
	  return;	/* Silently discard if not connected. */
	}

	stemp_len = imsg_len;
	if (stemp_len + 2 > IGATE_MAX_MSG) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Rx IGate: Too long. Truncating.\n");
	  stemp_len = IGATE_MAX_MSG - 2;
	}

	memcpy (stemp, imsg, stemp_len);

	if (s_debug >= 1) {
	  text_color_set(DW_COLOR_XMIT);
	  dw_printf ("[rx>ig] ");
	  ax25_safe_print (stemp, stemp_len, 0);
	  dw_printf ("\n");
	}

	stemp[stemp_len++] = '\r';
	stemp[stemp_len++] = '\n';
	stemp[stemp_len] = '\0';

	stats_uplink_bytes += stemp_len;


#if __WIN32__	
        err = SOCK_SEND (igate_sock, stemp, stemp_len);
	if (err == SOCKET_ERROR)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError %d sending to IGate server.  Closing connection.\n\n", WSAGetLastError());
	  //dw_printf ("DEBUG: igate_sock=%d, line=%d\n", igate_sock, __LINE__);
	  closesocket (igate_sock);
	  igate_sock = -1;
	  WSACleanup();
	}
#else
        err = SOCK_SEND (igate_sock, stemp, stemp_len);
	if (err <= 0)
	{
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("\nError sending to IGate server.  Closing connection.\n\n");
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

	  n = SOCK_RECV (igate_sock, (char*)(&ch), 1);

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
	unsigned char message[1000];  // Spec says max 512.
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

	    // I never expected to see a nul character but it can happen.
	    // If found, change it to <0x00> and ax25_from_text will change it back to a single byte.
	    // Along the way we can use the normal C string handling.

	    if (ch == 0 && len < (int)(sizeof(message)) - 5) {
	      message[len++] = '<';
	      message[len++] = '0';
	      message[len++] = 'x';
	      message[len++] = '0';
	      message[len++] = '0';
	      message[len++] = '>';
	    }
	    else if (len < (int)(sizeof(message)))
	    {
	      message[len++] = ch;
	    }
	    
	  } while (ch != '\n');

	  message[sizeof(message)-1] = '\0';

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
 *
 * At first I was tempted to trim a trailing space as well.
 * By fixing this one case it might corrupt the data in other cases.
 * We compensate for this by ignoring trailing spaces when performing
 * the duplicate detection and removal.
 *
 * We need to transmit exactly as we get it.
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
 * If so, loop here over all configured channels.
 */
	    text_color_set(DW_COLOR_REC);
	    dw_printf ("\n[ig>tx] ");		// formerly just [ig]
	    ax25_safe_print ((char *)message, len, 0);
	    dw_printf ("\n");

	    if ((int)strlen((char*)message) != len) {

	      // Invalid.  Either drop it or pass it along as-is.  Don't change.

	      text_color_set(DW_COLOR_ERROR);
	      dw_printf("'nul' character found in packet from IS.  This should never happen.\n");
	      dw_printf("The source station is probably transmitting with defective software.\n");

	      //if (strcmp((char*)pinfo, "4P") == 0) {
	      //  dw_printf("The TM-D710 will do this intermittently.  A firmware upgrade is needed to fix it.\n");
	      //}
	    }

/*
 * Record that we heard from the source address.
 */
	    mheard_save_is ((char *)message);

	    stats_downlink_packets++;

/*
 * Possibly transmit if so configured.
 */
	    int to_chan = save_igate_config_p->tx_chan;

	    if (to_chan >= 0) {
	      maybe_xmit_packet_from_igate ((char*)message, to_chan);
	    }


/*
 * New in 1.7:  If ICHANNEL was specified, send packet to client app as specified channel.
 */
	    if (save_audio_config_p->igate_vchannel >= 0) {

	      int ichan = save_audio_config_p->igate_vchannel;

	      // My original poorly thoughtout idea was to parse it into a packet object,
	      // using the non-strict option, and send to the client app.
	      //
	      // A lot of things can go wrong with that approach.

	      // (1)  Up to 8 digipeaters are allowed in radio format.
	      //      There is a potential of finding a larger number here.
	      //
	      // (2)  The via path can have names that are not valid in the radio format.
	      //      e.g.  qAC, T2HAKATA, N5JXS-F1.
	      //      Non-strict parsing would force uppercase, truncate names too long,
	      //      and drop unacceptable SSIDs.
	      //
	      // (3) The source address could be invalid for the RF address format.
	      //     e.g.  WHO-IS>APJIW4,TCPIP*,qAC,AE5PL-JF::ZL1JSH-9 :Charles Beadfield/New Zealand{583
	      //     That is essential information that we absolutely need to preserve.
	      //
	      // I think the only correct solution is to apply a third party header
	      // wrapper so the original contents are preserved.  This will be a little
	      // more work for the application developer.  Search for ":}" and use only
	      // the part after that.  At this point, I don't see any value in encoding
	      // information in the source/destination so I will just use "X>X:}" as a prefix

	      char stemp[AX25_MAX_INFO_LEN];
	      strlcpy (stemp, "X>X:}", sizeof(stemp));
	      strlcat (stemp, (char*)message, sizeof(stemp));

	      packet_t pp3 = ax25_from_text(stemp, 0);	// 0 means not strict
	      if (pp3 != NULL) {

	        alevel_t alevel;
	        memset (&alevel, 0, sizeof(alevel));
	        alevel.mark = -2;	// FIXME: Do we want some other special case?
	        alevel.space = -2;

	        int subchan = -2;	// FIXME: -1 is special case for APRStt.
					// See what happens with -2 and follow up on this.
					// Do we need something else here?
		int slice = 0;
	        fec_type_t fec_type = fec_type_none;
	        char spectrum[] = "APRS-IS";
	        dlq_rec_frame (ichan, subchan, slice, pp3, alevel, fec_type, RETRY_NONE, spectrum);
	      }
	      else {
	        text_color_set(DW_COLOR_ERROR);
	        dw_printf ("ICHANNEL %d: Could not parse message from APRS-IS server.\n", ichan);
	        dw_printf ("%s\n", message);
	      }
	    }  // end ICHANNEL option
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
 *
 *		This was an idea that came up in one of the discussion forums.
 *		I rushed in without thinking about it very much.
 *
 * 		In retrospect, I don't think this was such a good idea.
 *		It would be of value only if there is no other IGate nearby
 *		that would report on the original transmission.
 *		I wonder if anyone would notice if this silently disappeared.
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
 * Name:        maybe_xmit_packet_from_igate
 *
 * Purpose:     Convert text string, from IGate server, to third party
 *		packet and send to transmit queue if appropriate.
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
 *				  This is interesting because the source is not a valid AX.25 address.
 *				  Non-RF stations can have 2 alphanumeric characters for SSID.
 *				  In this example, the WHO-IS server is responding to a message.
 *
 *				WHO-IS>APJIW4,TCPIP*,qAC,AE5PL-JF::ZL1JSH-9 :Charles Beadfield/New Zealand{583
 *
 *
 *				  Notice how the final digipeater address, in the header, might not
 *				  be a valid AX.25 address.  We see a 9 character address
 *				  (with no ssid) and an ssid of two letters.
 *				  We don't care because we end up discarding them before
 *				  repackaging to go over the radio.
 *
 *				  The "q construct"  ( http://www.aprs-is.net/q.aspx ) provides
 *				  a clue about the journey taken. "qAX" means that the station sending
 *				  the packet to the server did not login properly as a ham radio
 *				  operator so we don't want to put this on to RF.
 *
 *		to_chan		- Radio channel for transmitting.
 *
 *--------------------------------------------------------------------*/


// It is unforunate that the : data type indicator (DTI) was overloaded with
// so many different meanings.  Simply looking at the DTI is not adequate for
// determining whether a packet is a message.
// We need to exclude the other special cases of telemetry metadata,
// bulletins, and weather bulletins.

static int is_message_message (char *infop)
{
	if (*infop != ':') return (0);
	if (strlen(infop) < 11) return (0);	// too short for : addressee :
	if (strlen(infop) >= 16) {
	  if (strncmp(infop+10, ":PARM.", 6) == 0) return (0);
	  if (strncmp(infop+10, ":UNIT.", 6) == 0) return (0);
	  if (strncmp(infop+10, ":EQNS.", 6) == 0) return (0);
	  if (strncmp(infop+10, ":BITS.", 6) == 0) return (0);
	}
	if (strlen(infop) >= 4) {
	  if (strncmp(infop+1, "BLN", 3) == 0) return (0);
	  if (strncmp(infop+1, "NWS", 3) == 0) return (0);
	  if (strncmp(infop+1, "SKY", 3) == 0) return (0);
	  if (strncmp(infop+1, "CWA", 3) == 0) return (0);
	  if (strncmp(infop+1, "BOM", 3) == 0) return (0);
	}
	return (1);		// message, including ack, rej
}


static void maybe_xmit_packet_from_igate (char *message, int to_chan)
{
	int n;

	assert (to_chan >= 0 && to_chan < MAX_CHANS);

/*
 * Try to parse it into a packet object; we need this for the packet filtering.
 *
 * We use the non-strict option because there the via path can have:
 *	- station names longer than 6.
 *	- alphanumeric SSID.
 *	- lower case for "q constructs.
 * We don't care about any of those because the via path will be discarded anyhow.
 *
 * The other issue, that I did not think of originally, is that the "source"
 * address might not conform to AX.25 restrictions when it originally came
 * from a non-RF source.  For example an APRS "message" might be sent to the
 * "WHO-IS" server, and the reply message would have that for the source address.
 *
 * Originally, I used the source address from the packet object but that was
 * missing the alphanumeric SSID.  This needs to be done differently.
 *
 * Potential Bug:  Up to 8 digipeaters are allowed in radio format.
 * Is there a possibility of finding a larger number here?
 */
	packet_t pp3 = ax25_from_text(message, 0);
	if (pp3 == NULL) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tx IGate: Could not parse message from server.\n");
	  dw_printf ("%s\n", message);
	  return;
	}

// Issue 408: The source address might not be valid AX.25 because it
// came from a non-RF station.  e.g.  some server responding to a message.
// We need to take source address from original rather than extracting it
// from the packet object.

	char src[AX25_MAX_ADDR_LEN];		/* Source address. */
	memset (src, 0, sizeof(src));
	memcpy (src, message, sizeof(src)-1);
	char *gt = strchr(src, '>');
	if (gt != NULL) {
	    *gt = '\0';
	}

/*
 * Drop if path contains:
 *	NOGATE or RFONLY - means IGate should not pass them.
 *	TCPXX or qAX - means it came from somewhere that did not identify itself correctly.
 */
	for (n = 0; n < ax25_get_num_repeaters(pp3); n++) {
	  char via[AX25_MAX_ADDR_LEN];	/* includes ssid. Do we want to ignore it? */

	  ax25_get_addr_with_ssid (pp3, n + AX25_REPEATER_1, via);

	  if (strcmp(via, "qAX") == 0 ||		// qAX deprecated. http://www.aprs-is.net/q.aspx
	      strcmp(via, "TCPXX") == 0 ||		// TCPXX deprecated.
	      strcmp(via, "RFONLY") == 0 ||
	      strcmp(via, "NOGATE") == 0) {

	    if (s_debug >= 1) {
	      text_color_set(DW_COLOR_DEBUG);
	      dw_printf ("Tx IGate: Do not transmit with %s in path.\n", via);
	    }

	    ax25_delete (pp3);
	    return;
	  }
	}

/*
 * Apply our own packet filtering if configured.
 * Do we want to do this before or after removing the VIA path?
 * I suppose by doing it first, we have the possibility of
 * filtering by stations along the way or the q construct.
 */

	assert (to_chan >= 0 && to_chan < MAX_CHANS);


/*
 * We have a rather strange special case here.
 * If we recently transmitted a 'message' from some station,
 * send the position of the message sender when it comes along later.
 *
 * Some refer to this as a "courtesy posit report" but I don't
 * think that is an official term.
 *
 * If we have a position report, look up the sender and see if we should
 * bypass the normal filtering.
 *
 * Reference:  https://www.aprs-is.net/IGating.aspx
 *
 *	"Passing all message packets also includes passing the sending station's position
 *	along with the message. When APRS-IS was small, we did this using historical position
 *	packets. This has become problematic as it introduces historical data on to RF. 
 *	The IGate should note the station(s) it has gated messages to RF for and pass
 *	the next position packet seen for that station(s) to RF."
 */

// TODO: Not quite this simple.  Should have a function to check for position.
// $ raw gps could be a position.  @ could be weather data depending on symbol.

	char *pinfo = NULL;
	int info_len = ax25_get_info (pp3, (unsigned char **)(&pinfo));

	int msp_special_case = 0;

	if (info_len >= 1 && strchr("!=/@'`", *pinfo) != NULL) {

	  int n = mheard_get_msp(src);

	  if (n > 0) {

	    msp_special_case = 1;

	    if (s_debug >= 1) {
	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Special case, allow position from message sender %s, %d remaining.\n", src, n - 1);
	    }

	    mheard_set_msp (src, n - 1);
	  }
	}

	if ( ! msp_special_case) {

	  if (save_digi_config_p->filter_str[MAX_CHANS][to_chan] != NULL) {

	    if (pfilter(MAX_CHANS, to_chan, save_digi_config_p->filter_str[MAX_CHANS][to_chan], pp3, 1) != 1) {

	      // Previously there was a debug message here about the packet being dropped by filtering.
	      // This is now handled better by the "-df" command line option for filtering details.

	      ax25_delete (pp3);
	      return;
	    }
	  }
	}


/*
 * We want to discard the via path, as received from the APRS-IS, then
 * replace it with TCPIP and our own call, marked as used.
 *
 *
 * For example, we might get something like this from the server.
 *	K1USN-1>APWW10,TCPIP*,qAC,N5JXS-F1:T#479,100,048,002,500,000,10000000
 *
 * We want to transform it to this before wrapping it as third party traffic.
 *	K1USN-1>APWW10,TCPIP,mycall*:T#479,100,048,002,500,000,10000000
 */

/*
 * These are typical examples where we see TCPIP*,qAC,<server>
 *
 *	N3LLO-4>APRX28,TCPIP*,qAC,T2NUENGLD:T#474,21.4,0.3,114.0,4.0,0.0,00000000
 *	N1WJO>APWW10,TCPIP*,qAC,T2MAINE:)147.120!4412.27N/07033.27WrW1OCA repeater136.5 Tone Norway Me
 *	AB1OC-10>APWW10,TCPIP*,qAC,T2IAD2:=4242.70N/07135.41W#(Time 0:00:00)!INSERVICE!!W60!
 *
 * But sometimes we get a different form:
 *
 *	N1YG-1>T1SY9P,WIDE1-1,WIDE2-2,qAR,W2DAN-15:'c&<0x7f>l <0x1c>-/>
 *	W1HS-8>TSSP9T,WIDE1-1,WIDE2-1,qAR,N3LLO-2:`d^Vl"W>/'"85}|*&%_'[|!wLK!|3
 *	N1RCW-1>APU25N,MA2-2,qAR,KA1VCQ-1:=4140.41N/07030.21W-Home Station/Fill-in Digi {UIV32N}
 *	N1IEJ>T4PY3U,W1EMA-1,WIDE1*,WIDE2-2,qAR,KD1KE:`a5"l!<0x7f>-/]"4f}Retired & Busy=
 *
 * Oh!  They have qAR rather than qAC.  What does that mean?
 * From  http://www.aprs-is.net/q.aspx
 *
 *	qAC - Packet was received from the client directly via a verified connection (FROMCALL=login).
 *		The callSSID following the qAC is the server's callsign-SSID.
 *
 *	qAR - Packet was received directly (via a verified connection) from an IGate using the ,I construct.
 *		The callSSID following the qAR it the callSSID of the IGate.
 *
 * What is the ",I" construct?
 * Do we care here?
 * Is it something new and improved that we should be using in the other direction?
 */

	char payload[AX25_MAX_PACKET_LEN];

	char dest[AX25_MAX_ADDR_LEN];		/* Destination field. */
	ax25_get_addr_with_ssid (pp3, AX25_DESTINATION, dest);
	snprintf (payload, sizeof(payload), "%s>%s,TCPIP,%s*:%s",
				src, dest, save_audio_config_p->achan[to_chan].mycall, pinfo);


#if DEBUGx
	text_color_set(DW_COLOR_DEBUG);
	dw_printf ("Tx IGate: DEBUG payload=%s\n", payload);
#endif


	
/*
 * Encapsulate for sending over radio if no reason to drop it.
 */

/*
 * We don't want to suppress duplicate "messages" within a short time period.
 * Suppose we transmitted a "message" for some station and it did not respond with an ack.
 * 25 seconds later the sender retries.  Wouldn't we want to pass along that retry?
 *
 * "Messages" get preferential treatment because they are high value and very rare.
 *	-> Bypass the duplicate suppression.
 *	-> Raise the rate limiting value.
 */
	if (ig_to_tx_allow (pp3, to_chan)) {
	  char radio [2400];
	  snprintf (radio, sizeof(radio), "%s>%s%d%d%s:}%s",
				save_audio_config_p->achan[to_chan].mycall,
				APP_TOCALL, MAJOR_VERSION, MINOR_VERSION,
				save_igate_config_p->tx_via,
				payload);

	  packet_t pradio = ax25_from_text (radio, 1);
	  if (pradio != NULL) {

#if ITEST
	    text_color_set(DW_COLOR_XMIT);
	    dw_printf ("Xmit: %s\n", radio);
	    ax25_delete (pradio);
#else
	    /* This consumes packet so don't reference it again! */
	    tq_append (to_chan, TQ_PRIO_1_LO, pradio);
#endif
	    stats_rf_xmit_packets++;		// Any type of packet.

	    if (is_message_message(pinfo)) {

// We transmitted a "message."  Telemetry metadata is excluded.
// Remember to pass along address of the sender later.

	      stats_msg_cnt++;			// Update statistics.

	      mheard_set_msp (src, save_igate_config_p->igmsp);
	    }

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

} /* end maybe_xmit_packet_from_igate */



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
 *
 * Original thinking:
 *
 *		Occasionally someone will get on one of the discussion groups and say:
 *		I don't think my IGate is working.  I look at packets, from local stations,
 *		on aprs.fi or findu.com, and they are always through some other IGate station,
 *		never mine.
 *		Then someone has to explain, this is not a valid strategy for analyzing
 *		everything going thru the network.   The APRS-IS servers drop duplicate
 *		packets (ignoring the via path) within a 30 second period.  If some
 *		other IGate gets the same thing there a millisecond faster than you,
 *		the one you send is discarded.
 *		In this scenario, it would make sense to perform additional duplicate
 *		suppression before forwarding RF packets to the Server.
 *		I don't recall if I saw some specific recommendation to do this or if
 *		it just seemed like the obvious thing to do to avoid sending useless
 *		stuff that would just be discarded anyhow.  It seems others came to the
 *		same conclusion.  http://www.tapr.org/pipermail/aprssig/2016-July/045907.html
 *
 * Version 1.5:	Rethink strategy.
 *
 *		Issue 85, https://github.com/wb2osz/direwolf/issues/85 ,
 *		got me thinking about this some more.  Sending more information will
 *		allow the APRS-IS servers to perform future additional network analysis.
 *		To make a long story short, the RF>IS direction duplicate checking
 *		is now disabled.   The code is still there in case I change my mind
 *		and want to add a configuration option to allow it.  The dedupe
 *		time is set to 0 which means don't do the checking.
 *
 *--------------------------------------------------------------------*/

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

// No need to save the information if we are not doing duplicate checking.

	if (save_igate_config_p->rx2ig_dedupe_time == 0) {
	  return;
	}

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
	  (void)info_len;

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
	  (void)info_len;

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("rx_to_ig_allow? %d \"%s>%s:%s\"\n", crc, src, dest, pinfo);
	}


// Do we have duplicate checking at all in the RF>IS direction?

	if (save_igate_config_p->rx2ig_dedupe_time == 0) {
	  if (s_debug >= 2) {
	    text_color_set(DW_COLOR_DEBUG);
	    dw_printf ("rx_to_ig_allow? YES, no dedupe checking\n");
	  }
	  return 1;
	}

// Yes, check for duplicates within certain time.

	for (j=0; j<RX2IG_HISTORY_MAX; j++) {
	  if (rx2ig_checksum[j] == crc && rx2ig_time_stamp[j] >= now - save_igate_config_p->rx2ig_dedupe_time) {
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
 *		above, for RF to IS, with one additional restriction.
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
 *		>>> The receive IGate spec says a packet should be cut at a CR. <<<
 *
 *		In one case it is removed as expected   In another case, it is replaced by a trailing
 *		space character.  Maybe someone thought non printable characters should be
 *		replaced by spaces???  (I have since been told someone thought it would be a good
 *		idea to replace unprintable characters with spaces.  How's that working out for MIC-E position???)
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
 *		was recently transmitted by the IGate function?
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
	  (void)info_len;

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
	int increase_limit;

	unsigned char *pinfo;
	int info_len;

	info_len = ax25_get_info (pp, &pinfo);
	(void)info_len;

	if (s_debug >= 2) {
	  char src[AX25_MAX_ADDR_LEN];
	  char dest[AX25_MAX_ADDR_LEN];

	  ax25_get_addr_with_ssid(pp, AX25_SOURCE, src);
	  ax25_get_addr_with_ssid(pp, AX25_DESTINATION, dest);

	  text_color_set(DW_COLOR_DEBUG);
	  dw_printf ("ig_to_tx_allow? ch%d %d \"%s>%s:%s\"\n", chan, crc, src, dest, pinfo);
	}

	/* Consider transmissions on this channel only by either digi or IGate. */

	for (j=0; j<IG2TX_HISTORY_MAX; j++) {
	  if (ig2tx_checksum[j] == crc && ig2tx_chan[j] == chan && ig2tx_time_stamp[j] >= now - IG2TX_DEDUPE_TIME) {

	    /* We have a duplicate within some time period. */

	    if (is_message_message((char*)pinfo)) {

	      /* I think I want to avoid the duplicate suppression for "messages." */
	      /* Suppose we transmit a message from station X and it doesn't get an ack back. */
	      /* Station X then sends exactly the same thing 20 seconds later.  */
	      /* We don't want to suppress the retry. */

	      if (s_debug >= 2) {
	        text_color_set(DW_COLOR_DEBUG);
	        dw_printf ("ig_to_tx_allow? Yes for duplicate message sent %d seconds ago. bydigi=%d\n", (int)(now - ig2tx_time_stamp[j]), ig2tx_bydigi[j]);
	      }
	    }
	    else {

	      /* Normal (non-message) case. */

	      if (s_debug >= 2) {
	        text_color_set(DW_COLOR_DEBUG);
	        // could be multiple entries and this might not be the most recent.
	        dw_printf ("ig_to_tx_allow? NO. Duplicate sent %d seconds ago. bydigi=%d\n", (int)(now - ig2tx_time_stamp[j]), ig2tx_bydigi[j]);
	      }

	      text_color_set(DW_COLOR_INFO);
	      dw_printf ("Tx IGate: Drop duplicate packet transmitted recently.\n");
	      return 0;
	    }
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

	/* "Messages" (special APRS data type ":") are intentional and more */
	/* important than all of the other mostly repetitive useless junk */
	/* flowing thru here.  */
	/* It would be unfortunate to discard a message because we already */
	/* hit our limit.  I don't want to completely eliminate limiting for */
	/* messages, in case something goes terribly wrong, but we can triple */
	/* the normal limit for them. */

	increase_limit = 1;
	if (is_message_message((char*)pinfo)) {
	  increase_limit = 3;
	}

	if (count_1 >= save_igate_config_p->tx_limit_1 * increase_limit) {
	  text_color_set(DW_COLOR_ERROR);
	  dw_printf ("Tx IGate: Already transmitted maximum of %d packets in 1 minute.\n", save_igate_config_p->tx_limit_1);
	  return 0;
	}
	if (count_5 >= save_igate_config_p->tx_limit_5 * increase_limit) {
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

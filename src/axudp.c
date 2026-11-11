//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
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
 * Module:      axudp.c
 *
 * Purpose:     Send received frames as AXUDP v2 datagrams so Dire Wolf can
 *		replace the dxlAPRS afskmodem in front of udpbox / udpgate4.
 *
 * Description:	Configured with "AXUDP <chan> <host>:<port>".
 *
 *		Datagram layout, as produced by dxlAPRS "afskmodem -L":
 *
 *		  0x01, '0', "V<level> Q<quality> ", 0x00,
 *		  AX.25 frame without FCS,
 *		  2 byte CRC (low byte first) over everything before it.
 *
 *		V - Receive audio level in dB on the afskmodem scale.
 *		    afskmodem reports 20*log10(peak-to-peak sample value) - 96.5.
 *		    Dire Wolf alevel.rec is peak-to-peak / 327.68.
 *
 *		Q - Decoder margin in percent.  This is NOT the afskmodem
 *		    bit jitter quality.  It is the share of the parallel
 *		    demodulators / slicers that decoded the frame, with
 *		    reduced weight for frames that needed bit fixing.
 *
 *---------------------------------------------------------------*/

#include "direwolf.h"		// Sets _WIN32_WINNT for XP API level needed by ws2tcpip.h

#if __WIN32__
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "textcolor.h"
#include "ax25_pad.h"
#include "axudp.h"

#ifndef MAX_TOTAL_CHANS
#define MAX_TOTAL_CHANS MAX_CHANS	// Before version 1.7.
#endif

#if __WIN32__
typedef SOCKET axudp_sock_t;
#define AXUDP_NO_SOCK INVALID_SOCKET
#else
typedef int axudp_sock_t;
#define AXUDP_NO_SOCK (-1)
#endif

static axudp_sock_t axudp_sock = AXUDP_NO_SOCK;
static int axudp_enabled[MAX_TOTAL_CHANS];
static struct sockaddr_in axudp_dest[MAX_TOTAL_CHANS];

static unsigned char crcl[256], crch[256];
static int crc_ready = 0;


static void gen_crc_tab (void)
{
	unsigned c, crc;
	int i;

	for (c = 0; c < 256; c++) {
	  crc = 255 - c;
	  for (i = 0; i < 8; i++) {
	    if (crc & 1) crc = (crc >> 1) ^ 0x8408;
	    else crc = crc >> 1;
	  }
	  crcl[c] = crc & 0xff;
	  crch[c] = (255 - (crc >> 8)) & 0xff;
	}
	crc_ready = 1;
}


static void app_crc (unsigned char *b, int size)
{
	unsigned char l = 0, h = 0, x;
	int i;

	for (i = 0; i < size; i++) {
	  x = b[i] ^ l;
	  l = crcl[x] ^ h;
	  h = crch[x];
	}
	b[size] = l;
	b[size + 1] = h;
}


/*-------------------------------------------------------------------
 *
 * Name:        axudp_config
 *
 * Purpose:     Enable AXUDP output for a channel.
 *
 * Inputs:	chan	 - Channel number.
 *		hostport - "host:port", host may be a name or IPv4 address.
 *
 * Returns:	0 for success, -1 for error.
 *
 *--------------------------------------------------------------------*/

int axudp_config (int chan, const char *hostport)
{
	char host[256];
	const char *colon;
	int port;
	struct addrinfo hints, *ai;

	if (chan < 0 || chan >= MAX_TOTAL_CHANS) return (-1);

	colon = strrchr (hostport, ':');
	if (colon == NULL || colon == hostport || (size_t)(colon - hostport) >= sizeof(host)) return (-1);
	memcpy (host, hostport, colon - hostport);
	host[colon - hostport] = '\0';
	port = atoi (colon + 1);
	if (port < 1 || port > 65535) return (-1);

	if (axudp_sock == AXUDP_NO_SOCK) {
#if __WIN32__
	  WSADATA wsadata;
	  if (WSAStartup (MAKEWORD(2,2), &wsadata) != 0) return (-1);
#endif
	  axudp_sock = socket (AF_INET, SOCK_DGRAM, 0);
	  if (axudp_sock == AXUDP_NO_SOCK) return (-1);
	}
	memset (&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	if (getaddrinfo (host, NULL, &hints, &ai) != 0 || ai == NULL) return (-1);
	memcpy (&axudp_dest[chan], ai->ai_addr, sizeof(axudp_dest[chan]));
	freeaddrinfo (ai);
	axudp_dest[chan].sin_port = htons (port);

	if ( ! crc_ready) gen_crc_tab ();
	axudp_enabled[chan] = 1;
	return (0);
}


static int quality_percent (retry_t retries, const char *spectrum)
{
	int n = spectrum != NULL ? (int)strlen (spectrum) : 0;
	int i;
	double q, sum;

	if (n == 0) {
	  q = 100.0 - 20.0 * (int)retries;
	}
	else {
	  sum = 0;
	  for (i = 0; i < n; i++) {
	    char c = spectrum[i];
	    if (c == '|' || (c >= '0' && c <= '9') || c == '+') sum += 1.0;	// no fix needed, or FX.25 / IL2P
	    else if (c == ':') sum += 0.5;					// single bit fixed
	    else if (c == '.') sum += 0.25;					// more bits fixed
	  }
	  q = 100.0 * sum / n;
	}
	if (q < 1) q = 1;
	if (q > 100) q = 100;
	return ((int)(q + 0.5));
}


/*-------------------------------------------------------------------
 *
 * Name:        axudp_send_rec_packet
 *
 * Purpose:     Send a received frame to the AXUDP destination of its channel.
 *
 * Inputs:	chan	 - Channel number where frame was received.
 *		fbuf	 - AX.25 frame without FCS.
 *		flen	 - Number of bytes in fbuf.
 *		alevel	 - Audio level.
 *		retries	 - Level of bit correction used.
 *		spectrum - Display of how well multiple decoders did.
 *
 *--------------------------------------------------------------------*/

void axudp_send_rec_packet (int chan, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries, const char *spectrum)
{
	unsigned char b[AX25_MAX_PACKET_LEN + 64];
	int p;

	if (chan < 0 || chan >= MAX_TOTAL_CHANS || ! axudp_enabled[chan]) return;
	if (flen <= 0 || flen > AX25_MAX_PACKET_LEN) return;

	b[0] = 0x01;
	b[1] = '0';
	p = 2;
	if (alevel.rec > 0) {
	  p += snprintf ((char *)b + p, 24, "V%d ", (int)lround (20.0 * log10 ((double)alevel.rec) - 46.2));
	}
	p += snprintf ((char *)b + p, 24, "Q%d ", quality_percent (retries, spectrum));
	b[p++] = 0;
	memcpy (b + p, fbuf, flen);
	p += flen;
	app_crc (b, p);

	if (sendto (axudp_sock, (const char *)b, p + 2, 0, (struct sockaddr *)&axudp_dest[chan], sizeof(axudp_dest[chan])) < 0) {
	  text_color_set (DW_COLOR_ERROR);
	  dw_printf ("AXUDP: sendto failed for channel %d.\n", chan);
	}
}

/* end axudp.c */

/*
 * Does send() on a blocking socket with SO_SNDTIMEO return a PARTIAL count?
 *
 * This is the exact situation client_send_thread() is in after the patch:
 * a stalled peer, a full socket buffer, and a 10s send timeout.
 *
 * server_send_* enqueues ~300 byte AGWPE frames; the send thread does
 *     err = SOCK_SEND(fd, pitem->data, pitem->len);
 *     if (err <= 0) { disconnect }
 * so a return of 0 < err < len is silently treated as full success.
 */
#ifndef FRAMESZ
#define FRAMESZ 300
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int main(void)
{
	int lsock = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = 0;
	bind(lsock, (struct sockaddr *)&sa, sizeof(sa));
	socklen_t sl = sizeof(sa);
	getsockname(lsock, (struct sockaddr *)&sa, &sl);
	listen(lsock, 1);

	/* "Client" that never reads, with a small receive buffer. */
	int c = socket(AF_INET, SOCK_STREAM, 0);
	int rcv = 4096;
	setsockopt(c, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
	connect(c, (struct sockaddr *)&sa, sizeof(sa));

	int s = accept(lsock, NULL, NULL);
	int snd = 8192;
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, &snd, sizeof(snd));

	/* Same 10s timeout the patch installs on accepted sockets. */
	struct timeval tv = { 10, 0 };
	setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	unsigned char buf[FRAMESZ];
	memset(buf, 'A', sizeof(buf));

	printf("sending 300-byte frames to a peer that never reads...\n");

	for (int i = 0; i < 10000; i++) {
		int n = send(s, buf, sizeof(buf), MSG_NOSIGNAL);
		if (n < 0) {
			printf("frame %d: send() = -1, errno=%d (%s)\n",
			       i, errno, strerror(errno));
			printf("  -> the patch's `if (err <= 0)` catches this. OK.\n");
			break;
		}
		if (n != (int)sizeof(buf)) {
			printf("frame %d: send() = %d of %zu  *** PARTIAL ***\n",
			       i, n, sizeof(buf));
			printf("  -> `if (err <= 0)` does NOT catch this;\n");
			printf("     %zu bytes of this AGWPE frame are dropped and the\n",
			       sizeof(buf) - n);
			printf("     client's byte stream is desynchronized.\n");
			break;
		}
	}

	close(c);
	close(s);
	close(lsock);
	return 0;
}

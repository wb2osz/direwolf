
/* dwsock.h - Socket helper functions. */

#ifndef DWSOCK_H
#define DWSOCK_H 1

#define DWSOCK_IPADDR_LEN 48		// Size of string to hold IPv4 or IPv6 address.
					// I think 40 would be adequate but we'll make
					// it a little larger just to be safe.
					// Use INET6_ADDRSTRLEN (from netinet/in.h) instead?

int dwsock_init (void);

int dwsock_connect (char *hostname, char *port, char *description, int allow_ipv6, int debug, char ipaddr_str[DWSOCK_IPADDR_LEN]);
								/* ipaddr_str needs to be at least SOCK_IPADDR_LEN bytes */

char *dwsock_ia_to_text (int  Family, void * pAddr, char * pStringBuf, size_t StringBufSize);

void dwsock_close (int fd);

#endif

/* sock.h - Socket helper functions. */

#ifndef SOCK_H
#define SOCK_H 1

#define SOCK_IPADDR_LEN 48		// Size of string to hold IPv4 or IPv6 address.
					// I think 40 would be adequate but we'll make
					// it a little larger just to be safe.
					// Use INET6_ADDRSTRLEN (from netinet/in.h) instead?

int sock_init (void);

int sock_connect (char *hostname, char *port, char *description, int allow_ipv6, int debug, char *ipaddr_str);
								/* ipaddr_str needs to be at least SOCK_IPADDR_LEN bytes */

char *sock_ia_to_text (int  Family, void * pAddr, char * pStringBuf, size_t StringBufSize);

#endif
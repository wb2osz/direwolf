/* sock_utils.h - Non-blocking send and keepalive helpers for TCP client sockets.
 *
 * Include this AFTER the platform socket headers (<sys/socket.h> on Unix,
 * <winsock2.h> on Windows) since the inline functions reference socket types.
 */

#ifndef SOCK_UTILS_H
#define SOCK_UTILS_H 1

/*------------------------------------------------------------------
 * SOCK_SEND_NOWAIT  --  non-blocking send
 *
 * Returns immediately with an error if the kernel TCP send buffer is
 * full, instead of blocking until space becomes available.  Use on
 * any path that must not stall recv_process (the sole DLQ consumer).
 *
 * Linux/macOS: MSG_DONTWAIT flag does this in one call.
 * Windows:     A zero-timeout select() checks writability first.
 *------------------------------------------------------------------*/

#if __WIN32__

static inline int sock_send_nowait (SOCKET s, const char *data, int size) {
    fd_set wfds;
    struct timeval tv;
    FD_ZERO(&wfds);
    FD_SET(s, &wfds);
    tv.tv_sec  = 0;
    tv.tv_usec = 0;
    if (select(0, NULL, &wfds, NULL, &tv) <= 0) {
        WSASetLastError(WSAEWOULDBLOCK);
        return SOCKET_ERROR;
    }
    return send(s, data, size, 0);
}
#define SOCK_SEND_NOWAIT(s,data,size) sock_send_nowait((SOCKET)(s),(const char*)(data),(size))

#elif __APPLE__
#define SOCK_SEND_NOWAIT(s,data,size) send(s,data,size, MSG_DONTWAIT)
#else
#define SOCK_SEND_NOWAIT(s,data,size) send(s,data,size, MSG_NOSIGNAL|MSG_DONTWAIT)
#endif


/*------------------------------------------------------------------
 * SOCK_SEND_IS_TRANSIENT  --  true when the last SOCK_SEND_NOWAIT
 * failed only because the kernel TCP send buffer was momentarily
 * full (EAGAIN / EWOULDBLOCK / WSAEWOULDBLOCK).
 *
 * Use this to distinguish "buffer momentarily full, drop this frame
 * and keep the connection open" from a hard error that means the
 * remote end has gone away and the socket should be closed.
 *
 * Truly frozen clients are handled by the SO_KEEPALIVE probes set
 * with SOCK_SET_KEEPALIVE: when the OS declares the connection dead
 * the next send will return a hard error and the socket will be
 * closed normally.
 *------------------------------------------------------------------*/

#if __WIN32__
#define SOCK_SEND_IS_TRANSIENT() (WSAGetLastError() == WSAEWOULDBLOCK)
#else
#define SOCK_SEND_IS_TRANSIENT() (errno == EAGAIN || errno == EWOULDBLOCK)
#endif


/*------------------------------------------------------------------
 * SOCK_SET_KEEPALIVE  --  enable TCP keepalives on an accepted socket
 *
 * Detects stale connections (e.g. client laptop suspended without
 * cleanly closing the TCP session) within ~2 minutes instead of the
 * OS default of 2 hours.
 *
 * Settings: 60 s idle before first probe, 10 s between probes, 6
 * probes → connection declared dead after ~120 s of silence.
 *
 * TCP_KEEPIDLE / TCP_KEEPINTVL / TCP_KEEPCNT are Linux/macOS
 * extensions guarded with #ifdef so the code still compiles on
 * platforms that lack them (they just use the OS defaults).
 * On Windows the equivalent is set via WSAIoctl(SIO_KEEPALIVE_VALS).
 *------------------------------------------------------------------*/

#if __WIN32__

#include <mstcpip.h>
static inline void sock_set_keepalive (SOCKET s) {
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char *)&on, sizeof(on));
    struct tcp_keepalive ka;
    ka.onoff             = 1;
    ka.keepalivetime     = 60000;   /* ms before first probe */
    ka.keepaliveinterval = 10000;   /* ms between probes     */
    DWORD ret = 0;
    WSAIoctl(s, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), NULL, 0, &ret, NULL, NULL);
}
#define SOCK_SET_KEEPALIVE(s) sock_set_keepalive((SOCKET)(s))

#else

static inline void sock_set_keepalive (int s) {
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
#ifdef TCP_KEEPIDLE
    int idle = 60;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE,  &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    int intvl = 10;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    int cnt = 6;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT,   &cnt, sizeof(cnt));
#endif
}
#define SOCK_SET_KEEPALIVE(s) sock_set_keepalive(s)

#endif  /* __WIN32__ */

#endif  /* SOCK_UTILS_H */

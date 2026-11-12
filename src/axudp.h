/* axudp.h - AXUDP v2 output (dxlAPRS compatible) for received frames. */

#ifndef AXUDP_H
#define AXUDP_H 1

#include "audio.h"

int axudp_config (int chan, const char *hostport);

void axudp_send_rec_packet (int chan, unsigned char *fbuf, int flen, alevel_t alevel, retry_t retries, const char *spectrum);

#endif

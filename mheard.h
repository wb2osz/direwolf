

/* mheard.h */

#include "decode_aprs.h"	// for decode_aprs_t


void mheard_init (int debug);

void mheard_save_rf (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries);

void mheard_save_is (char *ptext);

int mheard_count (int max_hops, int time_limit);

int mheard_was_recently_nearby (char *role, char *callsign, int time_limit, int max_hops, double dlat, double dlon, double km);

void mheard_set_msp (char *callsign, int num);

int mheard_get_msp (char *callsign);
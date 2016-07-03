

/* mheard.h */

void mheard_init (int debug);

void mheard_save (int chan, decode_aprs_t *A, packet_t pp, alevel_t alevel, retry_t retries);

int mheard_count (int max_hops, int time_limit);


void nettnc_init (struct audio_s *pa);

int nettnc_attach (int chan, char *host, int port);

void nettnc_send_packet (int chan, packet_t pp);
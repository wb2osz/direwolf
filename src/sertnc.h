

void sertnc_init (struct audio_s *pa);

int sertnc_attach (int chan, char *devicename, int baud);

void sertnc_send_packet (int chan, packet_t pp);
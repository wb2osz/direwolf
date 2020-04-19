

void ais_to_nmea (unsigned char *ais, int ais_len, char *nema, int nema_size);

int ais_parse (char *sentence, int quiet, char *descr, int descr_size, char *mssi, int mssi_size, double *odlat, double *odlon, float *ofknots, float *ofcourse);
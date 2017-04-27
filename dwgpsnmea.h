
/* dwgpsnmea.h   -   For reading NMEA sentences over serial port */



#ifndef DWGPSNMEA_H
#define DWGPSNMEA_H 1

#include "dwgps.h"		/* for dwfix_t */
#include "config.h"
#include "serial_port.h"	/* for MYFDTYPE */


int dwgpsnmea_init (struct misc_config_s *pconfig, int debug);

MYFDTYPE dwgpsnmea_get_fd(char *wp_port_name, int speed);

void dwgpsnmea_term (void);


dwfix_t dwgpsnmea_gprmc (char *sentence, int quiet, double *odlat, double *odlon, float *oknots, float *ocourse);

dwfix_t dwgpsnmea_gpgga (char *sentence, int quiet, double *odlat, double *odlon, float *oalt, int *onsat);


#endif


/* end dwgpsnmea.h */




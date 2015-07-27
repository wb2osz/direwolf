
/* dwgps.h */


int dwgps_init (void);

int dwgps_read (double *plat, double *plon, float *pspeed, float *pcourse, float *palt);

void dwgps_term (void);


/* end dwgps.h */





/* dwgpsd.h   -   For communicating with daemon */



#ifndef DWGPSD_H
#define DWGPSD_H 1

#include "config.h"


int dwgpsd_init (struct misc_config_s *pconfig, int debug);

void dwgpsd_term (void);

#endif


/* end dwgpsd.h */




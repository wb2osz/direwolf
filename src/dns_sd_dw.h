/*-------------------------------------------------------------------
 *
 * Name:	dns_sd_dw.h
 *
 * Purpose:	Header file for announcing DNS-SD services
 *
 *------------------------------------------------------------------*/

#if (USE_AVAHI_CLIENT | USE_MACOS_DNSSD)

#include "config.h"

// DNS-SD service types
#define DNS_SD_TYPE_AGWPE "_agwpe._tcp"
#define DNS_SD_TYPE_KISS  "_kiss-tnc._tcp"

// DNS-SD service type names
#define DNS_SD_TYPE_NAME_AGWPE "AGWPE"
#define DNS_SD_TYPE_NAME_KISS "KISS TCP"

// Temporary until both Linux and Mac are converted
#define DNS_SD_SERVICE DNS_SD_TYPE_KISS

void dns_sd_announce (struct misc_config_s *mc);
void dns_sd_term (void);

#endif // USE_AVAHI_CLIENT | USE_MACOS_DNSSD

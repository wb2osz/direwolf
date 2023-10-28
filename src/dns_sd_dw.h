
#if (USE_AVAHI_CLIENT|USE_MACOS_DNSSD)

#include "config.h"

#define DNS_SD_SERVICE "_kiss-tnc._tcp"

void dns_sd_announce (struct misc_config_s *mc);

#endif // USE_AVAHI_CLIENT

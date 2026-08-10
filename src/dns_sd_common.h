/*-------------------------------------------------------------------
 *
 * Name:	dns_sd_common.h
 *
 * Purpose:	Header file for common DNS-SD values, types, and functions
 *
 *------------------------------------------------------------------*/

#if (USE_AVAHI_CLIENT | USE_MACOS_DNSSD)

// One for AGWPE, remainder for KISS
#define MAX_DNS_SD_SERVICES (1 + MAX_KISS_TCP_PORTS)

typedef struct dns_sd_service_s {
    int port;
    int channel;
    char *name;
} dns_sd_service_t;

int dns_sd_service_count(struct misc_config_s *mc);
dns_sd_service_t *dns_sd_create_context(struct misc_config_s *mc);

#endif // USE_AVAHI_CLIENT | USE_MACOS_DNSSD

//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2020  Heikki Hannikainen, OH7LZB
//    Copyright (C) 2025  Martin F N Cooper, KD6YAM
//
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.

/*------------------------------------------------------------------
 *
 * Module:      dns_sd_common.c
 *
 * Purpose:   	Announce the KISS over TCP service using DNS-SD, common functions
 *
 * Description:
 *
 *     Most people have typed in enough IP addresses and ports by now, and
 *     would rather just select an available TNC that is automatically
 *     discovered on the local network.  Even more so on a mobile device
 *     such an Android or iOS phone or tablet.
 *
 *     This module contains common functions needed on Linux and MacOS.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "dns_sd_dw.h"
#include "dns_sd_common.h"

#define SERVICE_BASE_NAME "Dire Wolf"


/*------------------------------------------------------------------------------
 *
 * Name:	dns_sd_service_count
 *
 * Purpose:	Determine the number of services that are configured and will
 *		thus be announced.
 *
 * Inputs:	mc	- Dire Wolf misc config as read from the config file.
 *
 * Returns:	Count of services to be announced.
 *
 * Description:	Counts the number of AGWPE and KISS TCP services that have a
 *		non-zero port number, meaning that they should be announced via
 *		DNS-SD. This is useful for determining whether or not there is
 *		anything that we need to do.
 *
 *------------------------------------------------------------------------------*/

int dns_sd_service_count(struct misc_config_s *mc)
{
    int count = 0;

    if (mc->agwpe_port != 0)
        count++;

    for (int i = 0; i < MAX_KISS_TCP_PORTS; i++) {
        if (mc->kiss_port[i] != 0)
            count++;
    }

    return count;
}


/*------------------------------------------------------------------------------
 *
 * Name:	make_service_name
 *
 * Purpose:	Create a full service name based on the provided components.
 *
 * Inputs:	basename	- Base service name. Defaults to "Dire Wolf".
 *
 *		hostname	- Host name if available, else empty string.
 *
 *		channel		- Channel number, or -1 for default.
 *
 * Returns:	A full service name suitable for DNS-SD.
 *		It is the caller's responsibility to free this.
 *
 * Description:	Constructs a full service name for an AGWPE or KISS service.
 *      	A typical name including all components might look like
 *		"Dire Wolf channel 2 on myhost". Channel is only relevant for
 *		KISS services.
 *
 *------------------------------------------------------------------------------*/

static char *make_service_name (char *basename, char *hostname, int channel)
{
    char sname[128];
    char temp[64];

    if (basename[0]) {
        strlcpy(sname, basename, sizeof(sname));
    } else {
        strcpy(sname, "Dire Wolf");
    }

    if (channel != -1) {
        snprintf(temp, sizeof(temp), " channel %i", channel);
        strlcat(sname, temp, sizeof(sname));
    }

    if (hostname[0]) {
        snprintf(temp, sizeof(temp), " on %s", hostname);
        strlcat(sname, temp, sizeof(sname));
    }

    return strdup(sname);
}


/*------------------------------------------------------------------------------
 *
 * Name:	dns_sd_create_context
 *
 * Purpose:	Allocate and populate an array of common attributes for each of
 *		the DNS-SD services to be announced. This includes constructing
 *		a unique name for each service.
 *
 * Inputs:	mc	- Dire Wolf misc config as read from the config file.
 *
 * Returns:	An array of dns_sd_service_t, of length MAX_DNS_SD_SERVICES.
 *		It is the caller's responsibility to free this.
 *
 * Description:	The port and channel are saved, and a name created from a base
 *		name provided in the config, or a constant if none is provided.
 *		The name includes the channel, if appropriate, and the hostname
 *              if available.
 *
 *		The first entry in the array is for AGWPE. The remainder are
 *		for however many KISS TCP ports are configured.
 *
 *------------------------------------------------------------------------------*/

dns_sd_service_t *dns_sd_create_context (struct misc_config_s *mc)
{
    dns_sd_service_t *ctx;
    char hostname[51];
    int i, j;
    
    int err = gethostname(hostname, sizeof(hostname));
    if (err == 0) {
        hostname[sizeof(hostname)-1] = '\0';

        // on some systems, an FQDN is returned; remove domain part
        char *dot = strchr(hostname, '.');
        if (dot)
            *dot = 0;
    } else
        hostname[0] = '\0';

    ctx = (dns_sd_service_t *)calloc(sizeof(dns_sd_service_t), MAX_DNS_SD_SERVICES);

    if (mc->agwpe_port != 0) {
        ctx[0].port = mc->agwpe_port;
        ctx[0].channel = -1;
        ctx[0].name = make_service_name(mc->dns_sd_name, hostname, -1);
    }
    for (i = 0, j = 1; i < MAX_KISS_TCP_PORTS; i++) {
        if (mc->kiss_port[i] != 0) {
            ctx[j].port = mc->kiss_port[i];
            ctx[j].channel = mc->kiss_chan[i];
            ctx[j].name = make_service_name(mc->dns_sd_name, hostname, mc->kiss_chan[i]);
            j++;
        }
    }

    return ctx;
}

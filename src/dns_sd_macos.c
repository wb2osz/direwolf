//
//    This file is part of Dire Wolf, an amateur radio packet TNC.
//
//    Copyright (C) 2020  Heikki Hannikainen, OH7LZB
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
 * Module:      dns_sd_macos.c
 *
 * Purpose:   	Announce the KISS over TCP service using MacOS dns-sd
 *
 * Description:
 *
 *     Most people have typed in enough IP addresses and ports by now, and
 *     would rather just select an available TNC that is automatically
 *     discovered on the local network.  Even more so on a mobile device
 *     such an Android or iOS phone or tablet.
 *
 *     On MacOs, the announcement can be made through dns-sd.
 */

#ifdef USE_MACOS_DNSSD

#include <string.h>
#include <dns_sd.h>
#include <arpa/inet.h>

#include "dns_sd_dw.h"
#include "dns_sd_common.h"
#include "textcolor.h"

static char *name = NULL;

static void registerServiceCallBack(DNSServiceRef sdRef, DNSServiceFlags flags, DNSServiceErrorType errorCode,
                              const char* name, const char* regType, const char* domain, void* context)
{
	if (errorCode == kDNSServiceErr_NoError) {
		text_color_set(DW_COLOR_INFO);
		dw_printf("DNS-SD: Successfully registered '%s'\n", name);
	} else {
		text_color_set(DW_COLOR_ERROR);
		dw_printf("DNS-SD: Failed to register '%s': %d\n", name, errorCode);
	}
}

void dns_sd_announce (struct misc_config_s *mc)
{
	//int kiss_port = mc->kiss_port;	// now an array.
	int kiss_port = mc->kiss_port[0];	// FIXME:  Quick hack until I can handle multiple TCP ports properly.

	if (mc->dns_sd_name[0]) {
		name = strdup(mc->dns_sd_name);
	} else {
		name = dns_sd_default_service_name();
	}

	uint16_t port_nw = htons(kiss_port);

	DNSServiceRef registerRef;
	DNSServiceErrorType err = DNSServiceRegister(
		&registerRef, 0, 0, name, DNS_SD_SERVICE, NULL, NULL,
		port_nw, 0, NULL, registerServiceCallBack, NULL);

	if (err == kDNSServiceErr_NoError) {
		text_color_set(DW_COLOR_INFO);
		dw_printf("DNS-SD: Announcing KISS TCP on port %d as '%s'\n", kiss_port, name);
        } else {
		text_color_set(DW_COLOR_ERROR);
		dw_printf("DNS-SD: Failed to announce '%s': %d\n", name, err);
        }
}

#endif // USE_MACOS_DNSSD



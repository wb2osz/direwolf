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


#include <stdio.h>
#include <unistd.h>
#include <string.h>

/* Get a default service name to publish. By default,
 * "Dire Wolf on <hostname>", or just "Dire Wolf" if hostname cannot
 * be obtained.
 */
char *dns_sd_default_service_name(void)
{
    char hostname[51];
    char sname[64];
    
    int i = gethostname(hostname, sizeof(hostname));
    if (i == 0) {
        hostname[sizeof(hostname)-1] = 0;

        // on some systems, an FQDN is returned; remove domain part
        char *dot = strchr(hostname, '.');
        if (dot)
            *dot = 0;

        snprintf(sname, sizeof(sname), "Dire Wolf on %s", hostname);
        return strdup(sname);
    }
    
    return strdup("Dire Wolf");
}


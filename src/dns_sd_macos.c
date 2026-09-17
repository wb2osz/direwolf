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
#include <errno.h>
#include <time.h>

#include "dns_sd_dw.h"
#include "dns_sd_common.h"
#include "textcolor.h"


// We don't really want select() to timeout, hence the very large number
#define SELECT_TIMEOUT 100000000

// Extended context for the Mac DNS-SD API
typedef struct dns_sd_services_s {
    dns_sd_service_t *ctx;
    DNSServiceRef    sd_ref[MAX_DNS_SD_SERVICES];
    int              sd_fd[MAX_DNS_SD_SERVICES];
} dns_sd_services_t;

// Thread required to receive events from the DNS-SD daemon
static pthread_t event_thread;

// Pipe fds to allow for a graceful exit
static int stop_fd[2] = {-1, -1};


/*------------------------------------------------------------------------------
 *
 * Name:	process_events
 *
 * Purpose:	Thread function to process events from the DNS-SD daemon.
 *
 * Inputs:	arg	- Extended context with all info required to process
 *			  events for any announced services.
 *
 * Description:	Obtains a set of file descriptors, one per announced service,
 *		and creates a pipe to allow for a graceful exit. Waits for
 *		notification from the DNS-SD daemon, and processes any events
 *		received. Removes any announced services on completion.
 *
 *		This function exits normally when the special stop_fd is ready
 *		for reading, which happens when the associated pipe is written.
 *		It may also exit abnormally if an error is encountered.
 *
 *------------------------------------------------------------------------------*/

static void *process_events (void *arg)
{
    dns_sd_services_t *svcs = (dns_sd_services_t *) arg;
    int i, last_fd, result;
    int stop_now = 0;

    // Populate the fds
    for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (svcs->sd_ref[i] != NULL) {
            svcs->sd_fd[i] = DNSServiceRefSockFD(svcs->sd_ref[i]);
            last_fd = svcs->sd_fd[i];
        }
    }

    // Create a pipe to allow for a graceful exit
    result = pipe(stop_fd);
    if (result == 0) {
        last_fd = stop_fd[1];
    } else {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("pipe() returned %d errno %d: %s\n", result, errno, strerror(errno));
    }

    fd_set readfds;
    struct timeval timeout;

    while (!stop_now) {
        // Prepare the set of file descriptors
        FD_ZERO(&readfds);
        FD_SET(stop_fd[0], &readfds);
        for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
            if (svcs->sd_fd[i] > 0) {
                FD_SET(svcs->sd_fd[i], &readfds);
            }
        }

        timeout.tv_sec = SELECT_TIMEOUT;
        timeout.tv_usec = 0;

        // Wait for something to happen
        result = select(last_fd + 1, &readfds, NULL, NULL, &timeout);
        if (result > 0) {
            DNSServiceErrorType err;

            // If the pipe was written to, it's time to exit
            if (FD_ISSET(stop_fd[0], &readfds)) {
                stop_now = 1;
                break;
            }
            // Check for services with events
            for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
                if (svcs->sd_fd[i] > 0 && FD_ISSET(svcs->sd_fd[i], &readfds)) {
                    err = DNSServiceProcessResult(svcs->sd_ref[i]);
                    if (err != kDNSServiceErr_NoError) {
                        text_color_set(DW_COLOR_ERROR);
                        dw_printf("Error from the API: %i for index %i\n", err, i);
                        stop_now = 1; // but continue to process remaining fds
                    }
                }
            }
        } else {
            text_color_set(DW_COLOR_ERROR);
            dw_printf("select() returned %d errno %d: %s\n", result, errno, strerror(errno));
            if (errno != EINTR)
                stop_now = 1;
        }
    }

    // Clean up
    for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (svcs->sd_ref[i] != NULL) {
            DNSServiceRefDeallocate(svcs->sd_ref[i]);
        }
    }
    for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (svcs->ctx[i].name)
            free(svcs->ctx[i].name);
    }
    free(svcs->ctx);
    free(svcs);
    close(stop_fd[0]);
    close(stop_fd[1]);

    return NULL;
}


/*------------------------------------------------------------------------------
 *
 * Name:	registration_callback
 *
 * Purpose:	Called when the registration for a service completes or fails.
 *
 * Inputs:	sdRef	        - Service reference initialized upon registration.
 *
 *		flags           - Unused in this implementation.
 *
 *		errorCode       - Indicates success, or type of failure if
 *				  registration failed.
 *
 *		name		- Name of service registered. It is possible
 *                                for this to differ from the name we created,
 *                                since name a conflict is resolved by the system
 *                                and a new name created on our behalf.
 *
 *		regType         - Type of service registered. This will be either
 *				  DNS_SD_TYPE_AGWPE or DNS_SD_TYPE_KISS.
 *
 *		domain		- Domain on which the service is registered.
 *				  Always the default domain in this implementation.
 *
 *		context		- The context for this service. An instance of
 *				  dns_sd_service_t.
 *
 * Description:	This callback is invoked within the event processing thread
 *		each time a service is registered, successfully or not. At
 *		this time, it is used only to indicate to the user whether or
 *		the service was registered successfully.
 *
 *------------------------------------------------------------------------------*/

static void registration_callback (DNSServiceRef sdRef, DNSServiceFlags flags,
        DNSServiceErrorType errorCode, const char* name, const char* regType,
        const char* domain, void* context)
{
    char *svc_type = (char*) regType;

    if (strncmp(regType, DNS_SD_TYPE_AGWPE, strlen(DNS_SD_TYPE_AGWPE)) == 0)
        svc_type = DNS_SD_TYPE_NAME_AGWPE;
    else if (strncmp(regType, DNS_SD_TYPE_KISS, strlen(DNS_SD_TYPE_KISS)) == 0)
        svc_type = DNS_SD_TYPE_NAME_KISS;

    if (errorCode == kDNSServiceErr_NoError) {
        text_color_set(DW_COLOR_INFO);
        dw_printf("DNS-SD: Successfully registered %s service '%s'\n", svc_type, name);
    } else {
        text_color_set(DW_COLOR_ERROR);
        dw_printf("DNS-SD: Failed to register %s service '%s': %d\n", svc_type, name, errorCode);
    }
}


/*------------------------------------------------------------------------------
 *
 * Name:	dns_sd_announce
 *
 * Purpose:	Announce all configured AGWPE and KISS TCP services via DNS
 *		Service Discovery.
 *
 * Inputs:	mc	- Dire Wolf misc config as read from the config file.
 *
 * Description:	Register all configured AGWPE and KISS TCP services, and start
 *		a thread to watch for events that apply to those services.
 *              The thread is required for our registration callback to be
 *		invoked.
 *
 *------------------------------------------------------------------------------*/

void dns_sd_announce (struct misc_config_s *mc)
{
    // If there are no services to announce, we're done
    if (dns_sd_service_count(mc) == 0)
        return;

    DNSServiceRef sdRef;
    DNSServiceErrorType err;
    dns_sd_service_t *ctx;
    dns_sd_services_t *svcs;

    ctx = dns_sd_create_context(mc);
    svcs = (dns_sd_services_t *)calloc(sizeof(dns_sd_services_t), 1);
    svcs->ctx = ctx;

    for (int i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (ctx[i].port == 0)
	    continue;
        err = DNSServiceRegister(
            &sdRef,
            0,                  // no flags
            0,                  // all interfaces
            ctx[i].name,
            i == 0 ? DNS_SD_TYPE_AGWPE : DNS_SD_TYPE_KISS,
            NULL,               // default domain(s)
            NULL,               // default hostname(s)
            htons(ctx[i].port),
            0,                  // no txt record
            NULL,               // no txt record
            registration_callback,
            (void *)&ctx[i]
        );

        if (err == kDNSServiceErr_NoError) {
            svcs->sd_ref[i] = sdRef;
            text_color_set(DW_COLOR_INFO);
            dw_printf("DNS-SD: Announcing %s on port %d as '%s'\n",
                i == 0 ? DNS_SD_TYPE_NAME_AGWPE : DNS_SD_TYPE_NAME_KISS,
                ctx[i].port, ctx[i].name);
        } else {
            text_color_set(DW_COLOR_ERROR);
            dw_printf("DNS-SD: Failed to announce '%s': %d\n", ctx[i].name, err);
        }
    }

    pthread_create(&event_thread, NULL, &process_events, (void *)svcs);
}


/*------------------------------------------------------------------------------
 *
 * Name:	dns_sd_term
 *
 * Purpose:	Gracefully shut down the event processing thread and remove all
 *		service registrations.
 *
 * Description:	By writing to the stop_fd pipe, select() in the event processing
 *		thread will wake up, and the thread will recognize that it should
 *		exit after cleaning up registered services.
 *
 *------------------------------------------------------------------------------*/

void dns_sd_term (void) {
    if (stop_fd[1] != -1) {
        int val = 1;

        write(stop_fd[1], &val, sizeof(val));
    }
}

#endif // USE_MACOS_DNSSD

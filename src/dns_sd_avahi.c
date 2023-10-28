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
 * Module:      dns_sd_avahi.c
 *
 * Purpose:   	Announce the KISS over TCP service using DNS-SD via Avahi
 *
 * Description:
 *
 *     Most people have typed in enough IP addresses and ports by now, and
 *     would rather just select an available TNC that is automatically
 *     discovered on the local network.  Even more so on a mobile device
 *     such an Android or iOS phone or tablet.
 *
 *     On Linux, the announcement can be made through Avahi, the mDNS
 *     framework commonly deployed on Linux systems.
 *
 *     This is largely based on the publishing example of the Avahi library.
 */

#ifdef USE_AVAHI_CLIENT

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>

#include "dns_sd_dw.h"
#include "dns_sd_common.h"
#include "textcolor.h"

static AvahiEntryGroup *group = NULL;
static AvahiSimplePoll *simple_poll = NULL;
static AvahiClient *client = NULL;
static char *name = NULL;
static int kiss_port = 0;

pthread_t avahi_thread;

static void create_services(AvahiClient *c);

#define PRINT_PREFIX "DNS-SD: Avahi: "

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, AVAHI_GCC_UNUSED void *userdata)
{
    assert(g == group || group == NULL);
    group = g;
    
    /* Called whenever the entry group state changes */
    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            text_color_set(DW_COLOR_INFO);
            dw_printf(PRINT_PREFIX "Service '%s' successfully registered.\n", name);
            break;
        case AVAHI_ENTRY_GROUP_COLLISION: {
            char *n;
            /* A service name collision with a remote service
             * happened. Let's pick a new name. */
            n = avahi_alternative_service_name(name);
            avahi_free(name);
            name = n;
            text_color_set(DW_COLOR_INFO);
            dw_printf(PRINT_PREFIX "Service name collision, renaming service to '%s'\n", name);
            /* And recreate the services */
            create_services(avahi_entry_group_get_client(g));
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE:
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Entry group failure: %s\n", avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
            /* Some kind of failure happened while we were registering our services */
            avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}

static void create_services(AvahiClient *c)
{
    char *n;
    int ret;
    assert(c);
    /* If this is the first time we're called, let's create a new
     * entry group if necessary */
    if (!group) {
        if (!(group = avahi_entry_group_new(c, entry_group_callback, NULL))) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "avahi_entry_group_new() failed: %s\n", avahi_strerror(avahi_client_errno(c)));
            goto fail;
        }
    } else {
        avahi_entry_group_reset(group);
    }

    /* If the group is empty (either because it was just created, or
     * because it was reset previously, add our entries.  */
    if (avahi_entry_group_is_empty(group)) {
        text_color_set(DW_COLOR_INFO);
        dw_printf(PRINT_PREFIX "Announcing KISS TCP on port %d as '%s'\n", kiss_port, name);

        /* Announce with AVAHI_PROTO_INET instead of AVAHI_PROTO_UNSPEC, since Dire Wolf currently
         * only listens on IPv4.
         */

        if ((ret = avahi_entry_group_add_service(group, AVAHI_IF_UNSPEC, AVAHI_PROTO_INET, 0, name, DNS_SD_SERVICE, NULL, NULL, kiss_port, NULL)) < 0) {
            if (ret == AVAHI_ERR_COLLISION)
                goto collision;
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Failed to add _kiss-tnc._tcp service: %s\n", avahi_strerror(ret));
            goto fail;
        }

        /* Tell the server to register the service */
        if ((ret = avahi_entry_group_commit(group)) < 0) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Failed to commit entry group: %s\n", avahi_strerror(ret));
            goto fail;
        }
    }
    return;

collision:
    /* A service name collision with a local service happened. Let's
     * pick a new name */
    n = avahi_alternative_service_name(name);
    avahi_free(name);
    name = n;
    text_color_set(DW_COLOR_INFO);
    dw_printf(PRINT_PREFIX "Service name collision, renaming service to '%s'\n", name);
    avahi_entry_group_reset(group);
    create_services(c);
    return;

fail:
    avahi_simple_poll_quit(simple_poll);
}

static void client_callback(AvahiClient *c, AvahiClientState state, AVAHI_GCC_UNUSED void * userdata)
{
    assert(c);
    /* Called whenever the client or server state changes */
    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            /* The server has startup successfully and registered its host
             * name on the network, so it's time to create our services */
            create_services(c);
            break;
        case AVAHI_CLIENT_FAILURE:
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Client failure: %s\n", avahi_strerror(avahi_client_errno(c)));
            avahi_simple_poll_quit(simple_poll);
            break;
        case AVAHI_CLIENT_S_COLLISION:
            /* Let's drop our registered services. When the server is back
             * in AVAHI_SERVER_RUNNING state we will register them
             * again with the new host name. */
        case AVAHI_CLIENT_S_REGISTERING:
            /* The server records are now being established. This
             * might be caused by a host name change. We need to wait
             * for our own records to register until the host name is
             * properly esatblished. */
            if (group)
                avahi_entry_group_reset(group);
            break;
        case AVAHI_CLIENT_CONNECTING:
            ;
    }
}

static void cleanup(void)
{
        /* Cleanup things */
        if (client)
            avahi_client_free(client);

        if (simple_poll)
            avahi_simple_poll_free(simple_poll);

        avahi_free(name);
}


static void *avahi_mainloop(void *arg)
{
        /* Run the main loop */
        avahi_simple_poll_loop(simple_poll);

        cleanup();

        return NULL;
}

void dns_sd_announce (struct misc_config_s *mc)
{
	text_color_set(DW_COLOR_DEBUG);
	//kiss_port = mc->kiss_port;	// now an array.
	kiss_port = mc->kiss_port[0];	// FIXME:  Quick hack until I can handle multiple TCP ports properly.

	int error;

	/* Allocate main loop object */
	if (!(simple_poll = avahi_simple_poll_new())) {
	    text_color_set(DW_COLOR_ERROR);
	    dw_printf(PRINT_PREFIX "Failed to create Avahi simple poll object.\n");
	    goto fail;
        }

        if (mc->dns_sd_name[0]) {
            name = avahi_strdup(mc->dns_sd_name);
        } else {
            name = dns_sd_default_service_name();
        }

        /* Allocate a new client */
        client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, NULL, &error);

        /* Check whether creating the client object succeeded */
        if (!client) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Failed to create Avahi client: %s\n", avahi_strerror(error));
            goto fail;
        }

        pthread_create(&avahi_thread, NULL, &avahi_mainloop, NULL);

        return;

fail:
        cleanup();
}

#endif // USE_AVAHI_CLIENT


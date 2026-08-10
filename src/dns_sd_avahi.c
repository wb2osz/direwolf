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

pthread_t avahi_thread;

static void create_services(AvahiClient *c, dns_sd_service_t *ctx);

#define PRINT_PREFIX "DNS-SD: Avahi: "


/*------------------------------------------------------------------------------
 *
 * Name:	rename_all_services
 *
 * Purpose:	Rename each service, using avahi_alternative_service_name() to
 *		obtain a new name.
 *
 * Inputs:	ctx	- Context info for all of our services.
 *
 * Description:	This function is used when we know there is a name conflict for
 *		at least one service in the group, but not which one. Thus we
 *		update the names for all services to cover all possibilities.
 *
 *------------------------------------------------------------------------------*/

static void rename_all_services(dns_sd_service_t *ctx)
{
    for (int i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (ctx[i].name) {
            char *prev_name = ctx[i].name;
            ctx[i].name = avahi_alternative_service_name(prev_name);
            avahi_free(prev_name);
        }
    }
}


/*------------------------------------------------------------------------------
 *
 * Name:	entry_group_callback
 *
 * Purpose:	Called whenever the entry group changes state.
 *
 * Inputs:	g		- Group on which state changes are occurring.
 *				  This function may be called before our global
 *				  'group' value has been set, so we must use the
 *				  value passed in to reference our group.
 *
 *		state		- An enumeration value indicating the new state.
 *
 *		userdata	- Context info for our services.
 *
 * Description:	Here we are notified when all of the services in the group have
 *		been published, so that we can report that to the user. We could
 *		report the success of each service individually, but since success
 *		or failure applies on a group all-or-nothing basis, we report only
 *		collective success.
 *
 *		We may also be notified of a service name collision here. The
 *		Avahi API does not provide a way for us to know to which service
 *		that applies. Consequently all services must be renamed and the
 *		group effectively recreated.
 *
 *------------------------------------------------------------------------------*/

static void entry_group_callback(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata)
{
    assert(g == group || group == NULL);
    group = g;
    
    dns_sd_service_t *ctx = (dns_sd_service_t *)userdata;

    switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED :
            /* The entry group has been established successfully */
            text_color_set(DW_COLOR_INFO);
            dw_printf(PRINT_PREFIX "Successfully registered all services.\n");
            break;
        case AVAHI_ENTRY_GROUP_COLLISION: {
            /* A service name collision with a remote service happened. We are
             * not informed of which name has a collision, so we need to rename
             * all of them to be sure we catch the offending name. */
            text_color_set(DW_COLOR_INFO);
            dw_printf(PRINT_PREFIX "Service name collision, renaming services'\n");
            rename_all_services(ctx);
            /* And recreate the services */
            create_services(avahi_entry_group_get_client(g), ctx);
            break;
        }
        case AVAHI_ENTRY_GROUP_FAILURE:
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Entry group failure: %s\n",
                avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
            /* Some kind of failure happened while we were registering our services */
            dns_sd_term();
            break;
        case AVAHI_ENTRY_GROUP_UNCOMMITED:
        case AVAHI_ENTRY_GROUP_REGISTERING:
            ;
    }
}


/*------------------------------------------------------------------------------
 *
 * Name:	create_service
 *
 * Purpose:	Creates one service and adds it to the Avahi entry group.
 *
 * Inputs:	group		- The Avahi entry group to which the service
 *				  should be added.
 *
 *		ctx		- Context info for the service.
 *
 *		is_agwpe	- Whether to create an AGWPE service (non-zero)
 *				  or a KISS TCP one (zero).
 *
 * Description:	Creates a single service as specified. Handles service name
 *		collisions by repeatedly retrying with alternative names provided
 *		by Avahi. Although there are other ways in which the Avahi API
 *		could notify us of name conflicts, this appears to be the one
 *		that is presented when conflicts arise through, for example,
 *		multiple instances of Dire Wolf started on the same system.
 *
 *------------------------------------------------------------------------------*/

static int create_service(AvahiEntryGroup *group, dns_sd_service_t *ctx, int is_agwpe)
{
    text_color_set(DW_COLOR_INFO);
    dw_printf(PRINT_PREFIX "Announcing %s on port %d as '%s'\n",
        is_agwpe ? DNS_SD_TYPE_NAME_AGWPE : DNS_SD_TYPE_NAME_KISS, ctx->port, ctx->name);

    /* Announce with AVAHI_PROTO_INET instead of AVAHI_PROTO_UNSPEC, since Dire Wolf currently
     * only listens on IPv4.
     */

    int error = AVAHI_OK;

    do {
        error = avahi_entry_group_add_service(
            group,              // entry group
            AVAHI_IF_UNSPEC,    // all interfaces
            AVAHI_PROTO_INET,   // IPv4 only
            0,                  // no flags
            ctx->name,          // service name
            is_agwpe ? DNS_SD_TYPE_AGWPE : DNS_SD_TYPE_KISS,
            NULL,               // default domain(s)
            NULL,               // default hostname(s)
            ctx->port,          // service port
            NULL                // (undocumented)
            );

        if (error == AVAHI_ERR_COLLISION) {
            char *prev_name = ctx->name;
            ctx->name = avahi_alternative_service_name(prev_name);
            text_color_set(DW_COLOR_INFO);
            dw_printf(PRINT_PREFIX "Service name collision, renaming '%s' to '%s'\n", prev_name, ctx->name);
            avahi_free(prev_name);
        }
    } while (error == AVAHI_ERR_COLLISION);

    if (error != AVAHI_OK) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf(PRINT_PREFIX "Failed to add %s service: %s\n",
            is_agwpe ? DNS_SD_TYPE_NAME_AGWPE : DNS_SD_TYPE_NAME_KISS, avahi_strerror(error));
    }

    return error;
}


/*------------------------------------------------------------------------------
 *
 * Name:	create_services
 *
 * Purpose:	Creates all of our services and causes them to be published.
 *
 * Inputs:	c	- Client through which to create services.
 *
 *		ctx	- Context info for our services.
 *
 * Description:	First, we create an entry group which will contain all of our
 *		services. This is required by the Avahi API, and provides a means
 *		of managing the set of services. Then we create each service and
 *		group. Finally, we commit the changes, which causes all of the
 *		services in the group to be published.
 *
 *------------------------------------------------------------------------------*/

static void create_services(AvahiClient *c, dns_sd_service_t *ctx)
{
    int result;

    assert(c);

    /* If this is the first time we're called, let's create a new
     * entry group if necessary */
    if (!group) {
        if (!(group = avahi_entry_group_new(c, entry_group_callback, (void *)ctx))) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "avahi_entry_group_new() failed: %s\n", avahi_strerror(avahi_client_errno(c)));
            dns_sd_term();
            return;
        }
    } else {
        avahi_entry_group_reset(group);
    }

    /* If the group is empty (either because it was just created, or
     * because it was reset previously, add our entries.  */
    if (avahi_entry_group_is_empty(group)) {
        /* Add each individual service. */
        for (int i = 0; i < MAX_DNS_SD_SERVICES; i++) {
            if (ctx[i].port == 0)
                continue;
            result = create_service(group, &ctx[i], i == 0);
            /* Collisions are handled within create_service(), so an error here
             * is something else, almost certainly fatal to registration as a
             * whole, so bail out and give up. */
            if (result != AVAHI_OK)
                break;
        }

        if (result != AVAHI_OK) {
            dns_sd_term();
            return;
        }

        /* Publish all services in the group. */
        result = avahi_entry_group_commit(group);
        if (result != AVAHI_OK) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Failed to commit entry group: %s\n", avahi_strerror(result));
            dns_sd_term();
            return;
        }
    }
}


/*------------------------------------------------------------------------------
 *
 * Name:	client_callback
 *
 * Purpose:	Called whenever the client or its corresponding server changes
 *		state.
 *
 * Inputs:	c		- Client on which state changes are occurring.
 *				  This function may be called before our global
 *				  'client' value has been set, so we must use the
 *				  value passed in to reference our client.
 *
 *		state		- An enumeration value indicating the new state.
 *
 *		userdata	- Context info for our services.
 *
 * Description:	Here we are notified when the server is ready, and thus we can
 *		register our services. We may also be notified of name collisions
 *		or client failure.
 *
 *------------------------------------------------------------------------------*/

static void client_callback(AvahiClient *c, AvahiClientState state, void * userdata)
{
    assert(c);

    dns_sd_service_t *ctx = (dns_sd_service_t *)userdata;

    switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            /* The server has startup successfully and registered its host
             * name on the network, so it's time to create our services */
            create_services(c, ctx);
            break;
        case AVAHI_CLIENT_FAILURE:
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Client failure: %s\n", avahi_strerror(avahi_client_errno(c)));
            dns_sd_term();
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


/*------------------------------------------------------------------------------
 *
 * Name:	cleanup
 *
 * Purpose:	Called on exit (successful or otherwise) to release Avahi
 *		resources and free our own context data.
 *
 * Inputs:	ctx	- Context info for all of our announced services.
 *
 * Description:	Frees Avahi resources and then our own context. Note that the
 *		order of calls here is important. Some of the Avahi objects
 *		keep references to others (e.g. group holds a reference to client),
 *		such that freeing them in the wrong order can cause a segfault.
 *
 *------------------------------------------------------------------------------*/

static void cleanup(dns_sd_service_t *ctx)
{
    if (group) {
        avahi_entry_group_free(group);
        group = NULL;
    }

    if (client) {
        avahi_client_free(client);
        client = NULL;
    }

    if (simple_poll) {
        avahi_simple_poll_free(simple_poll);
        simple_poll = NULL;
    }

    for (int i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (ctx[i].name) {
            avahi_free(ctx[i].name);
        }
    }

    free(ctx);
}


/*------------------------------------------------------------------------------
 *
 * Name:	avahi_mainloop
 *
 * Purpose:	Thread function to process events from the Avahi daemon.
 *
 * Inputs:	arg	- Context with info on all of our announced services.
 *			  Needed here only so that we can clean up properly
 *			  when we're done.
 *
 * Description:	Starts a standard Avahi "simple poll" loop that will cause
 *		our client and group callbacks to be invoked at the appropriate
 *		time. The loop will exit when the avahi_simple_poll_quit()
 *		function is called elsewhere. We then clean up our context.
 *
 *------------------------------------------------------------------------------*/

static void *avahi_mainloop(void *arg)
{
    dns_sd_service_t *ctx = (dns_sd_service_t *) arg;

    /* Run the main loop */
    avahi_simple_poll_loop(simple_poll);

    cleanup(ctx);

    return NULL;
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
 *		a polling loop to watch for events that apply to those services.
 *
 *------------------------------------------------------------------------------*/

void dns_sd_announce (struct misc_config_s *mc)
{
    // If there are no services to announce, we're done
    if (dns_sd_service_count(mc) == 0)
        return;

    dns_sd_service_t *ctx;
    int i;

    ctx = dns_sd_create_context(mc);

    /* It is possible that we may need to call avahi_alternative_service_name()
     * one or more times to resolve service name conflicts. That function will
     * allocate a new name that must later be freed using avahi_free(). Here we
     * need to reallocate our initial names using avahi_strdup() to ensure that
     * calling avahi_free() on them later won't be a problem. */
    for (i = 0; i < MAX_DNS_SD_SERVICES; i++) {
        if (ctx[i].name) {
            char *prev_name = ctx[i].name;
            ctx[i].name = avahi_strdup(prev_name);
            free(prev_name);
        }
    }

    int error = 0;

    /* Allocate main loop object */
    if (!(simple_poll = avahi_simple_poll_new())) {
        text_color_set(DW_COLOR_ERROR);
        dw_printf(PRINT_PREFIX "Failed to create Avahi simple poll object.\n");
        error = 1;
    }

    /* Allocate a new client */
    if (!error) {
        client = avahi_client_new(avahi_simple_poll_get(simple_poll), 0, client_callback, ctx, &error);
        if (!client) {
            text_color_set(DW_COLOR_ERROR);
            dw_printf(PRINT_PREFIX "Failed to create Avahi client: %s\n", avahi_strerror(error));
        }
    }

    if (!error) {
        /* Start the main loop */
        pthread_create(&avahi_thread, NULL, &avahi_mainloop, (void *) ctx);
    } else {
        cleanup(ctx);
    }
}


/*------------------------------------------------------------------------------
 *
 * Name:	dns_sd_term
 *
 * Purpose:	Gracefully shut down the event processing thread and remove all
 *		service registrations.
 *
 * Description:	By telling the simple_poll to quit, our thread function will
 *		continue beyond the polling loop and invoke our cleanup code
 *		when it's ready.
 *
 *------------------------------------------------------------------------------*/

void dns_sd_term (void) {
    if (simple_poll)
        avahi_simple_poll_quit(simple_poll);
}

#endif // USE_AVAHI_CLIENT

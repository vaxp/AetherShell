/*
 * cc-network.c — Network init/cleanup shim for the control-center.
 *
 * Migrated from network-client (venom_network daemon) to network-actions
 * (direct NetworkManager D-Bus).  network_watch_active_wifi() is started
 * by the wifi-indicator builtin widget; nothing extra is needed here.
 */

#include "cc-network.h"
#include "network-actions.h"

void cc_network_init(void)
{
    /* network-actions connects to NetworkManager directly; no explicit
     * init is required — each function opens its own D-Bus connection
     * on first use. */
}

void cc_network_cleanup(void)
{
    /* Nothing to tear down. */
}

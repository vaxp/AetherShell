/*
 * cc-ethernet.h
 *
 * Ethernet logic for Control Center (enable/disable).
 * Requires cc_network_init() to be called before use.
 */

#ifndef CC_ETHERNET_H
#define CC_ETHERNET_H

#include <glib.h>

typedef struct _GtkWidget GtkWidget;

const char *cc_ethernet_subtitle_text(void);
gboolean cc_ethernet_is_enabled(void);

void cc_ethernet_attach(GtkWidget *eth_button);

#endif /* CC_ETHERNET_H */


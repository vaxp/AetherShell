/*
 * cc-wifi.h
 *
 * WiFi logic for Control Center (toggle + popup menu).
 * Requires cc_network_init() to be called before use.
 */

#ifndef CC_WIFI_H
#define CC_WIFI_H

#include <glib.h>

typedef struct _GtkWidget GtkWidget;
typedef struct _GdkEventButton GdkEventButton;

const char *cc_wifi_subtitle_text(void);
gboolean cc_wifi_is_enabled(void);

void cc_wifi_attach(GtkWidget *wifi_button);

#endif /* CC_WIFI_H */


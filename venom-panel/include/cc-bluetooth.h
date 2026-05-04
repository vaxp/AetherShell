/*
 * cc-bluetooth.h
 *
 * Bluetooth logic for Control Center (power toggle).
 * Requires cc_network_init() to be called before use.
 */

#ifndef CC_BLUETOOTH_H
#define CC_BLUETOOTH_H

#include <glib.h>

typedef struct _GtkWidget GtkWidget;

const char *cc_bluetooth_subtitle_text(void);
gboolean cc_bluetooth_is_powered(void);

void cc_bluetooth_attach(GtkWidget *bt_button);

#endif /* CC_BLUETOOTH_H */


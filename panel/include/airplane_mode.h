#ifndef AIRPLANE_MODE_H
#define AIRPLANE_MODE_H

#include <glib.h>

typedef void (*AirplaneModeStateCallback)(gboolean active, gpointer user_data);

/* Initialize airplane mode state tracking */
void airplane_mode_init(AirplaneModeStateCallback cb, gpointer user_data);

/* Toggle airplane mode on and off */
void airplane_mode_toggle(void);

/* Get current airplane mode state */
gboolean airplane_mode_is_active(void);

#endif /* AIRPLANE_MODE_H */

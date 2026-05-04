#ifndef PANEL_POWER_H
#define PANEL_POWER_H

#include <gtk/gtk.h>

GtkWidget *panel_power_widget_new(void);
void panel_power_prepare_reload(void);
void panel_power_cleanup(void);

#endif /* PANEL_POWER_H */


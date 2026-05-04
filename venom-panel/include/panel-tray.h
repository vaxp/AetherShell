#ifndef PANEL_TRAY_H
#define PANEL_TRAY_H

#include <gtk/gtk.h>

GtkWidget *panel_tray_widget_new(void);
void panel_tray_prepare_reload(void);
void panel_tray_cleanup(void);

#endif /* PANEL_TRAY_H */


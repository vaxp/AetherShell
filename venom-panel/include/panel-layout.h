#ifndef PANEL_LAYOUT_H
#define PANEL_LAYOUT_H

#include <gtk/gtk.h>

void panel_layout_load(GtkWidget *hbox);
void panel_layout_enable_live_reload(void);
void panel_layout_cleanup(void);

#endif /* PANEL_LAYOUT_H */


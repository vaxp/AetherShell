#ifndef DESKTOP_WINDOW_H
#define DESKTOP_WINDOW_H

#include <gtk/gtk.h>

extern GtkWidget *main_window;
extern GtkWidget *icon_layout;
extern int screen_w;
extern int screen_h;

void init_main_window(void);
gboolean desktop_has_available_monitor(void);

#endif /* DESKTOP_WINDOW_H */

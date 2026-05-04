#ifndef PANEL_BUILTINS_H
#define PANEL_BUILTINS_H

#include <gtk/gtk.h>

GtkWidget *panel_builtin_create(const char *name);

void panel_builtins_prepare_reload(void);
void panel_builtins_cleanup(void);

#endif /* PANEL_BUILTINS_H */


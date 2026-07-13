/*
 * wallpaper.h
 * Header file for wallpaper and window management
 */

#ifndef WALLPAPER_H
#define WALLPAPER_H

#include <gtk/gtk.h>

/* Global Variables */
extern GtkWidget *main_window;
extern GtkWidget *icon_layout;
extern int screen_w;
extern int screen_h;

/* Function Declarations */
void init_main_window(void);
void load_saved_wallpaper(void);
void init_wallpaper_monitor(void);
gboolean on_layout_draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data);
gboolean desktop_has_available_monitor(void);

#endif /* WALLPAPER_H */

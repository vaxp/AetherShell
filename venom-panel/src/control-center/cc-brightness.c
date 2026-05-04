/*
 * cc-brightness.c — Brightness slider in the control-center.
 *
 * Uses brightness-manager (systemd-logind + sysfs) instead of the old
 * multi-backend brightness-manager.  The slider now reflects external
 * changes (keyboard shortcuts, etc.) via the BrightnessChangedCallback.
 */

#include <gtk/gtk.h>

#include "cc-brightness.h"
#include "brightness-manager.h"

static GtkWidget *g_brightness_scale = NULL;

/* Called when brightness changes externally (keyboard fn keys, etc.) */
static void on_brightness_changed_externally(int percent, gpointer user_data)
{
    (void)user_data;
    if (!g_brightness_scale) return;

    /* Block our own signal so we don't loop back into brightness_set() */
    g_signal_handlers_block_matched(g_brightness_scale,
                                    G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                    G_CALLBACK(gtk_range_set_value), NULL);
    gtk_range_set_value(GTK_RANGE(g_brightness_scale), percent);
    g_signal_handlers_unblock_matched(g_brightness_scale,
                                      G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                      G_CALLBACK(gtk_range_set_value), NULL);
}

void cc_brightness_init(void)
{
    brightness_init(on_brightness_changed_externally, NULL);
}

void cc_brightness_cleanup(void)
{
    g_brightness_scale = NULL;
}

static void cc_brightness_on_scale_changed(GtkRange *range, gpointer data)
{
    (void)data;
    int percent = (int)gtk_range_get_value(range);
    brightness_set(percent);
}

GtkWidget *cc_brightness_create_scale(void)
{
    int cur = brightness_get();
    if (cur < 0) cur = 50;

    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 100, 1);
    gtk_range_set_value(GTK_RANGE(scale), cur);
    gtk_scale_set_draw_value(GTK_SCALE(scale), FALSE);
    gtk_widget_set_hexpand(scale, TRUE);

    g_brightness_scale = scale;
    g_object_add_weak_pointer(G_OBJECT(scale), (gpointer *)&g_brightness_scale);

    g_signal_connect(scale, "value-changed",
                     G_CALLBACK(cc_brightness_on_scale_changed), NULL);
    return scale;
}

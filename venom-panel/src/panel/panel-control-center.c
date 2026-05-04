#include <gtk/gtk.h>

#include "panel-control-center.h"
#include "control-center.h"

static GtkWidget *control_center_window = NULL;

static GtkWidget *create_control_center_icon(void)
{
    GtkWidget *image;
    gchar *icon_path = g_build_filename(g_get_current_dir(), "control-center-icon.svg", NULL);
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_path, 22, 22, TRUE, &error);

    if (pixbuf) {
        image = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);
    } else {
        if (error) {
            g_warning("[ControlCenter] Failed to load %s: %s", icon_path, error->message);
            g_error_free(error);
        }
        image = gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    }

    g_free(icon_path);
    return image;
}

static void toggle_control_center(GtkWidget *button, gpointer data)
{
    (void)button;
    (void)data;
    if (control_center_window && gtk_widget_get_visible(control_center_window)) {
        gtk_widget_hide(control_center_window);
        return;
    }

    if (!control_center_window) control_center_window = create_control_center();
    gtk_widget_show_all(control_center_window);
    gtk_window_present(GTK_WINDOW(control_center_window));
}

GtkWidget *panel_control_center_button_new(void)
{
    GtkWidget *cc_btn = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(cc_btn), create_control_center_icon());
    gtk_button_set_relief(GTK_BUTTON(cc_btn), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(cc_btn), "control-center-button");
    g_signal_connect(cc_btn, "clicked", G_CALLBACK(toggle_control_center), NULL);
    return cc_btn;
}

void panel_control_center_cleanup(void)
{
    if (control_center_window) {
        gtk_widget_destroy(control_center_window);
        control_center_window = NULL;
    }
}

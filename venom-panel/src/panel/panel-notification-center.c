#include <gtk/gtk.h>

#include "panel-notification-center.h"
#include "notification-center.h"

static GtkWidget *notification_center_window = NULL;

static void toggle_notification_center(GtkWidget *button, gpointer data)
{
    (void)button;
    (void)data;
    if (notification_center_window && gtk_widget_get_visible(notification_center_window)) {
        gtk_widget_hide(notification_center_window);
        return;
    }

    if (!notification_center_window) notification_center_window = create_notification_center();
    gtk_widget_show_all(notification_center_window);
    gtk_window_present(GTK_WINDOW(notification_center_window));
}

GtkWidget *panel_notification_center_button_new(void)
{
    GtkWidget *btn = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(btn),
                      gtk_image_new_from_icon_name("notifications-symbolic",
                                                   GTK_ICON_SIZE_LARGE_TOOLBAR));
    gtk_widget_set_tooltip_text(btn, "Notifications");
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "notification-center-button");
    g_signal_connect(btn, "clicked", G_CALLBACK(toggle_notification_center), NULL);
    return btn;
}

void panel_notification_center_cleanup(void)
{
    if (notification_center_window) {
        gtk_widget_destroy(notification_center_window);
        notification_center_window = NULL;
    }
}


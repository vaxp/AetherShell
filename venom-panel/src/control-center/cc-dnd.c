/*
 * cc-dnd.c
 */

#include <gtk/gtk.h>

#include "cc-dnd.h"
#include "notification-client.h"

static void cc_dnd_update_button_ui(GtkWidget *button, gboolean enabled)
{
    if (!button) return;

    GtkWidget *label = g_object_get_data(G_OBJECT(button), "subtitle_label");
    if (label) gtk_label_set_text(GTK_LABEL(label), enabled ? "On" : "Off");

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    if (enabled) gtk_style_context_add_class(ctx, "active");
    else gtk_style_context_remove_class(ctx, "active");
}

void cc_dnd_init(void)
{
    notification_client_init();
}

void cc_dnd_cleanup(void)
{
    notification_client_cleanup();
}

gboolean cc_dnd_is_enabled(void)
{
    return notification_client_get_dnd();
}

const char *cc_dnd_subtitle_text(void)
{
    return notification_client_get_dnd() ? "On" : "Off";
}

static void cc_dnd_on_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    gboolean new_state = !notification_client_get_dnd();
    notification_client_set_dnd(new_state);
    cc_dnd_update_button_ui(GTK_WIDGET(button), new_state);
}

static void cc_dnd_on_changed(gboolean enabled, gpointer data)
{
    GtkWidget *button = GTK_WIDGET(data);
    cc_dnd_update_button_ui(button, enabled);
}

static void cc_dnd_on_button_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)user_data;
    notification_client_on_dnd_change(NULL, widget);
}

void cc_dnd_attach(GtkWidget *dnd_button)
{
    if (!dnd_button) return;

    g_signal_connect(dnd_button, "clicked", G_CALLBACK(cc_dnd_on_clicked), NULL);
    g_signal_connect(dnd_button, "destroy", G_CALLBACK(cc_dnd_on_button_destroy), NULL);
    notification_client_on_dnd_change(cc_dnd_on_changed, dnd_button);

    cc_dnd_update_button_ui(dnd_button, notification_client_get_dnd());
}

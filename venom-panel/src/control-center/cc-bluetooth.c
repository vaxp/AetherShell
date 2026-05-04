/*
 * cc-bluetooth.c — Bluetooth toggle button in the control-center.
 *
 * Migrated from network-client (venom_network daemon) to bluetooth-manager
 * (direct BlueZ D-Bus via bluetooth_manager.h).
 */

#include <gtk/gtk.h>

#include "cc-bluetooth.h"
#include "bluetooth-manager.h"

static void cc_bluetooth_update_button_ui(GtkWidget *button)
{
    if (!button) return;

    gboolean powered = bluetooth_is_powered();

    GtkWidget *label = g_object_get_data(G_OBJECT(button), "subtitle_label");
    if (label)
        gtk_label_set_text(GTK_LABEL(label), cc_bluetooth_subtitle_text());

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    if (powered)
        gtk_style_context_add_class(ctx, "active");
    else
        gtk_style_context_remove_class(ctx, "active");
}

const char *cc_bluetooth_subtitle_text(void)
{
    return bluetooth_is_powered() ? "On" : "Off";
}

gboolean cc_bluetooth_is_powered(void)
{
    return bluetooth_is_powered();
}

static void cc_bluetooth_on_state_changed(gboolean powered, gpointer user_data)
{
    GtkWidget *button = GTK_WIDGET(user_data);
    if (!button) return;
    cc_bluetooth_update_button_ui(button);
}

static void cc_bluetooth_on_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    bluetooth_toggle();
    cc_bluetooth_update_button_ui(GTK_WIDGET(button));
}

void cc_bluetooth_attach(GtkWidget *bt_button)
{
    if (!bt_button) return;

    /* Register state-change callback so button updates automatically */
    bluetooth_init(cc_bluetooth_on_state_changed, bt_button);

    g_signal_connect(bt_button, "clicked", G_CALLBACK(cc_bluetooth_on_clicked), NULL);
    cc_bluetooth_update_button_ui(bt_button);
}

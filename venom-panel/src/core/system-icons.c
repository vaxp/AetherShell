/*
 * system-icons.c — Compact system icon tray.
 *
 * Contains only the battery widget now.
 * WiFi and Bluetooth are handled by the dedicated builtin indicators
 * (wifi / bluetooth) which have full popup functionality.
 */

#include "system-icons.h"
#include "battery_indicator.h"

GtkWidget *create_system_icons(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    /* Battery widget (UPower-based, with popup) */
    GtkWidget *batt_widget = get_battery_widget();
    gtk_box_pack_start(GTK_BOX(box), batt_widget, FALSE, FALSE, 0);

    GtkStyleContext *context = gtk_widget_get_style_context(box);
    gtk_style_context_add_class(context, "system-icons");

    return box;
}


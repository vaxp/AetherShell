/*
 * cc-ethernet.c — Ethernet toggle button in the control-center.
 *
 * Migrated from ethernet.h (custom NM wrapper) to network-actions.h.
 * network_toggle_ethernet() and network_init_state() provide the
 * same functionality without the old daemon dependency.
 */

#include <gtk/gtk.h>

#include "cc-ethernet.h"
#include "network-actions.h"

/* ─── State ──────────────────────────────────────────────────────────────── */

static gboolean g_eth_connected = FALSE;

static void cc_ethernet_apply_state(GtkWidget *button, gboolean connected)
{
    if (!button) return;

    g_eth_connected = connected;

    GtkWidget *label = g_object_get_data(G_OBJECT(button), "subtitle_label");
    if (label)
        gtk_label_set_text(GTK_LABEL(label), connected ? "On" : "Off");

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    if (connected) gtk_style_context_add_class(ctx, "active");
    else           gtk_style_context_remove_class(ctx, "active");
}

/* ─── State init callback ────────────────────────────────────────────────── */

static GtkWidget *g_eth_button = NULL;

static void on_eth_state_changed(gboolean connected)
{
    if (g_eth_button)
        cc_ethernet_apply_state(g_eth_button, connected);
}

/* ─── Public helpers ─────────────────────────────────────────────────────── */

const char *cc_ethernet_subtitle_text(void)
{
    return g_eth_connected ? "On" : "Off";
}

gboolean cc_ethernet_is_enabled(void)
{
    return g_eth_connected;
}

/* ─── Button click ───────────────────────────────────────────────────────── */

static void cc_ethernet_on_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    network_toggle_ethernet();
    /* State will be updated via the callback; do an optimistic UI flip */
    cc_ethernet_apply_state(GTK_WIDGET(button), !g_eth_connected);
}

void cc_ethernet_attach(GtkWidget *eth_button)
{
    if (!eth_button) return;

    g_eth_button = eth_button;
    g_object_add_weak_pointer(G_OBJECT(eth_button), (gpointer *)&g_eth_button);

    /* Register state watchers — will call on_eth_state_changed immediately */
    network_init_state(NULL, on_eth_state_changed);

    g_signal_connect(eth_button, "clicked", G_CALLBACK(cc_ethernet_on_clicked), NULL);
    cc_ethernet_apply_state(eth_button, g_eth_connected);
}

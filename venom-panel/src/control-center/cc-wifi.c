/*
 * cc-wifi.c — WiFi toggle button in the control-center.
 *
 * Migrated from network-client (venom_network daemon) to network-actions
 * (direct NetworkManager D-Bus).
 *
 * API mapping:
 *   wifi_client_is_enabled()          → network_toggle_wifi() + active poller
 *   wifi_client_get_ssid()            → WifiActiveInfo.ssid from watcher
 *   wifi_client_get_networks()        → network_wifi_scan()
 *   wifi_client_connect(ssid, pass)   → network_wifi_connect()
 *   wifi_client_disconnect()          → network_toggle_wifi() (toggle off)
 */

#include <gtk/gtk.h>

#include "cc-wifi.h"
#include "network-actions.h"

/* ─── State ──────────────────────────────────────────────────────────────── */

static GtkWidget *g_wifi_button      = NULL;
static gboolean   g_wifi_enabled     = FALSE;
static gchar     *g_current_ssid     = NULL;  /* owned, may be NULL */

/* ─── Active-WiFi poller callback ────────────────────────────────────────── */

static void on_active_wifi_changed(WifiActiveInfo *info, gpointer user_data)
{
    (void)user_data;

    g_free(g_current_ssid);
    g_current_ssid = NULL;

    if (info && info->ssid && info->ssid[0]) {
        g_wifi_enabled   = TRUE;
        g_current_ssid   = g_strdup(info->ssid);
    } else {
        g_wifi_enabled   = FALSE;
    }

    if (g_wifi_button) {
        GtkWidget *label = g_object_get_data(G_OBJECT(g_wifi_button), "subtitle_label");
        if (label) gtk_label_set_text(GTK_LABEL(label), cc_wifi_subtitle_text());

        GtkStyleContext *ctx = gtk_widget_get_style_context(g_wifi_button);
        if (g_wifi_enabled) gtk_style_context_add_class(ctx, "active");
        else                gtk_style_context_remove_class(ctx, "active");
    }
}

static void cc_wifi_update_button_ui(GtkWidget *button)
{
    if (!button) return;
    GtkWidget *label = g_object_get_data(G_OBJECT(button), "subtitle_label");
    if (label) gtk_label_set_text(GTK_LABEL(label), cc_wifi_subtitle_text());

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    if (g_wifi_enabled) gtk_style_context_add_class(ctx, "active");
    else                gtk_style_context_remove_class(ctx, "active");
}

/* ─── Public helpers ─────────────────────────────────────────────────────── */

const char *cc_wifi_subtitle_text(void)
{
    if (!g_wifi_enabled)     return "Off";
    if (g_current_ssid && g_current_ssid[0]) return g_current_ssid;
    return "Not Connected";
}

gboolean cc_wifi_is_enabled(void)
{
    return g_wifi_enabled;
}

/* ─── Network scan / connect menu ────────────────────────────────────────── */

/* Local wrapper that matches WifiNetwork from network-actions */
static void cc_wifi_free_network(WifiNetwork *net)
{
    if (!net) return;
    g_free(net->ssid);
    g_free(net->bssid);
    g_free(net);
}

/* Password dialog */
static gchar *cc_wifi_show_password_dialog(GtkWidget *parent, const gchar *ssid)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Enter Password",
                                                    GTK_WINDOW(gtk_widget_get_toplevel(parent)),
                                                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Cancel",  GTK_RESPONSE_CANCEL,
                                                    "_Connect", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 15);

    gchar *markup = g_strdup_printf("<b>%s</b>", ssid);
    GtkWidget *ssid_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(ssid_label), markup);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(content), ssid_label, FALSE, FALSE, 5);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Password");
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);

    GtkWidget *show_pass = gtk_check_button_new_with_label("Show password");
    g_signal_connect_swapped(show_pass, "toggled", G_CALLBACK(gtk_entry_set_visibility), entry);
    gtk_box_pack_start(GTK_BOX(content), show_pass, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);

    gchar *password = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        password = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

    gtk_widget_destroy(dialog);
    return password;
}

/* Called after scan completes */
typedef struct { GtkWidget *button; GdkEventButton *event; } ScanCtx;

static void on_scan_done(GList *networks, gpointer user_data)
{
    ScanCtx *ctx = user_data;
    GtkWidget *button = ctx ? ctx->button : NULL;
    GdkEventButton *event = ctx ? ctx->event : NULL;
    g_free(ctx);

    GtkWidget *menu = gtk_menu_new();

    if (!networks) {
        GtkWidget *item = gtk_menu_item_new_with_label("No networks found");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    } else {
        for (GList *l = networks; l; l = l->next) {
            WifiNetwork *net = l->data;
            if (!net) continue;

            const gchar *strength_icon =
                net->strength < 25 ? "▁" :
                net->strength < 50 ? "▂" :
                net->strength < 75 ? "▃" : "📶";

            gchar *label_text = g_strdup_printf("%s %s%s%s",
                                                strength_icon,
                                                net->ssid ? net->ssid : "—",
                                                net->secured   ? " 🔒" : "",
                                                net->connected ? " ✓"  : "");

            GtkWidget *item = gtk_menu_item_new_with_label(label_text);
            g_free(label_text);

            g_object_set_data_full(G_OBJECT(item), "wifi-net", net,
                                   (GDestroyNotify)cc_wifi_free_network);
            g_signal_connect(item, "activate", G_CALLBACK(
                ({
                    void _act(GtkMenuItem *mi, gpointer ud) {
                        (void)ud;
                        WifiNetwork *n = g_object_get_data(G_OBJECT(mi), "wifi-net");
                        if (!n) return;
                        if (n->connected) {
                            network_toggle_wifi();
                        } else if (n->secured) {
                            gchar *pw = cc_wifi_show_password_dialog(GTK_WIDGET(mi), n->ssid);
                            if (pw) {
                                network_wifi_connect(n->ssid, n->bssid, pw, NULL, NULL);
                                g_free(pw);
                            }
                        } else {
                            network_wifi_connect(n->ssid, n->bssid, NULL, NULL, NULL);
                        }
                        if (g_wifi_button) cc_wifi_update_button_ui(g_wifi_button);
                    }
                    _act;
                })), NULL);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
        g_list_free(networks);   /* items own the WifiNetwork* now */
    }

    gtk_widget_show_all(menu);
    if (button && event)
        gtk_menu_popup_at_widget(GTK_MENU(menu), button,
                                 GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST,
                                 (GdkEvent *)event);
}

static void cc_wifi_show_popup_menu(GtkWidget *button, GdkEventButton *event)
{
    ScanCtx *ctx = g_new0(ScanCtx, 1);
    ctx->button = button;
    ctx->event  = event;
    network_wifi_scan(on_scan_done, ctx);
}

/* ─── Button signals ─────────────────────────────────────────────────────── */

static gboolean cc_wifi_on_button_press(GtkWidget *button, GdkEventButton *event, gpointer data)
{
    (void)data;
    if (event->button == 3) {
        cc_wifi_show_popup_menu(button, event);
        return TRUE;
    }
    return FALSE;
}

static void cc_wifi_on_clicked(GtkButton *button, gpointer data)
{
    (void)data;
    network_toggle_wifi();
    cc_wifi_update_button_ui(GTK_WIDGET(button));
}

void cc_wifi_attach(GtkWidget *wifi_button)
{
    if (!wifi_button) return;
    g_wifi_button = wifi_button;
    g_object_add_weak_pointer(G_OBJECT(wifi_button), (gpointer *)&g_wifi_button);

    /* Start polling the active WiFi so our state variables stay current */
    network_watch_active_wifi(on_active_wifi_changed, NULL);

    g_signal_connect(wifi_button, "clicked",            G_CALLBACK(cc_wifi_on_clicked),      NULL);
    g_signal_connect(wifi_button, "button-press-event", G_CALLBACK(cc_wifi_on_button_press), NULL);

    cc_wifi_update_button_ui(wifi_button);
}

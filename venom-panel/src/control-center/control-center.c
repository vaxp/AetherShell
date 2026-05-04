/*
 * control-center.c
 *
 * venom-style Control Center for Linux panel.
 *
 * UI redesign goal: match a macOS-like Control Center layout:
 *   - Compound card: Network / Bluetooth / Settings
 *   - Do Not Disturb tile
 *   - Night Color + Light Theme tiles
 *   - Volume + Brightness slider cards
 *   - Media (MPRIS) card
 */

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

/* NetworkManager & PulseAudio headers */
#include <NetworkManager.h>
/* Note: We use pactl via spawn for non-blocking UI, 
   but we keep the header if you want to extend functionality later */
#include <pulse/pulseaudio.h> 

#include "panel-geometry.h"
#include "window-backend.h"
#include "cc-mpris.h"
#include "cc-network.h"
#include "cc-wifi.h"
#include "cc-bluetooth.h"
#include "cc-ethernet.h"
#include "cc-brightness.h"
#include "cc-audio.h"
#include "cc-shot.h"
#include "cc-settings.h"
#include "power_profile.h"

// #include "control-center.h" /* Uncomment if you have this header */

/* Forward declarations */
static void on_toggle_active(GtkButton *button, gpointer data);


/* =====================================================================
 * BRIGHTNESS - Works if user has write permissions to sysfs
 * Tip: For vaxp-os, create a udev rule to allow group 'video' to write here.
 * ===================================================================== */

/* Notifications UI removed from Control Center redesign.
 * (DND mode is still supported via notification-client.) */

/* =====================================================================
 * UI COMPONENTS (Redesign)
 * ===================================================================== */

static void toggle_style_class(GtkWidget *w, const char *klass, gboolean on)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context(w);
    if (on) gtk_style_context_add_class(ctx, klass);
    else gtk_style_context_remove_class(ctx, klass);
}

typedef struct {
    GtkWidget *button;
    GtkWidget *icon;
    GtkWidget *subtitle;
    gchar *active_profile;
} PowerProfilesUI;

static void power_profiles_set_active(PowerProfilesUI *ui, const char *profile)
{
    if (!ui) return;

    g_free(ui->active_profile);
    ui->active_profile = g_strdup(profile ? profile : "balanced");

    if (ui->subtitle) {
        if (g_strcmp0(ui->active_profile, "performance") == 0) gtk_label_set_text(GTK_LABEL(ui->subtitle), "Performance");
        else if (g_strcmp0(ui->active_profile, "power-saver") == 0) gtk_label_set_text(GTK_LABEL(ui->subtitle), "Power Saver");
        else gtk_label_set_text(GTK_LABEL(ui->subtitle), "Balanced");
    }

    if (ui->icon) {
        const char *icon_name = "power-profile-balanced-symbolic";
        if (g_strcmp0(ui->active_profile, "performance") == 0) icon_name = "power-profile-performance-symbolic";
        else if (g_strcmp0(ui->active_profile, "power-saver") == 0) icon_name = "power-profile-power-saver-symbolic";
        gtk_image_set_from_icon_name(GTK_IMAGE(ui->icon), icon_name, GTK_ICON_SIZE_BUTTON);
    }
}

static void on_power_profile_changed(const char *profile, gpointer user_data)
{
    power_profiles_set_active((PowerProfilesUI *)user_data, profile);
}

static void on_power_profile_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    PowerProfilesUI *ui = (PowerProfilesUI *)user_data;
    if (!ui) return;

    const char *cur = ui->active_profile ? ui->active_profile : "balanced";
    const char *next = "balanced";
    if (g_strcmp0(cur, "balanced") == 0) next = "performance";
    else if (g_strcmp0(cur, "performance") == 0) next = "power-saver";
    else next = "balanced";
    power_profile_set(next);
}

static void on_power_profiles_card_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    (void)user_data;
    power_profile_cleanup();
}

static void power_profiles_ui_free(gpointer data)
{
    PowerProfilesUI *ui = (PowerProfilesUI *)data;
    if (!ui) return;
    g_free(ui->active_profile);
    g_free(ui);
}

static GtkWidget *create_cc_row_button(const char *icon_name,
                                       const char *title,
                                       const char *subtitle,
                                       const char *icon_color_class,
                                       gboolean active)
{
    GtkWidget *button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(button, FALSE);

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    gtk_style_context_add_class(ctx, "cc-row-button");
    if (active) gtk_style_context_add_class(ctx, "active");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_container_add(GTK_CONTAINER(button), box);

    GtkWidget *icon_circle = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(icon_circle), TRUE);
    gtk_widget_set_size_request(icon_circle, 30, 30);
    gtk_widget_set_valign(icon_circle, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_circle), "cc-icon-circle");
    if (icon_color_class && icon_color_class[0]) {
        gtk_style_context_add_class(gtk_widget_get_style_context(icon_circle), icon_color_class);
    }

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "cc-icon-contrast");
    gtk_container_add(GTK_CONTAINER(icon_circle), icon);
    gtk_box_pack_start(GTK_BOX(box), icon_circle, FALSE, FALSE, 0);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE, 0);

    GtkWidget *title_label = gtk_label_new(title);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(title_label), "cc-row-title");
    gtk_box_pack_start(GTK_BOX(text_box), title_label, FALSE, FALSE, 0);

    GtkWidget *subtitle_label = gtk_label_new(subtitle);
    gtk_widget_set_halign(subtitle_label, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle_label), "cc-row-subtitle");
    gtk_box_pack_start(GTK_BOX(text_box), subtitle_label, FALSE, FALSE, 0);

    g_object_set_data(G_OBJECT(button), "subtitle_label", subtitle_label);
    return button;
}

static GtkWidget *create_cc_small_tile(const char *icon_name, const char *title, gboolean active)
{
    GtkWidget *button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(button, FALSE);

    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    gtk_style_context_add_class(ctx, "cc-small-tile");
    if (active) gtk_style_context_add_class(ctx, "active");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_container_add(GTK_CONTAINER(button), box);

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);

    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "cc-small-tile-label");
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    return button;
}

static GtkWidget *create_cc_slider_card(const char *title,
                                        const char *icon_name,
                                        GtkWidget *scale)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "cc-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "cc-slider-card");

    GtkWidget *header = gtk_label_new(title);
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_widget_set_margin_start(header, 14);
    gtk_widget_set_margin_end(header, 14);
    gtk_widget_set_margin_top(header, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "cc-card-title");
    gtk_box_pack_start(GTK_BOX(card), header, FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(row, 14);
    gtk_widget_set_margin_end(row, 14);
    gtk_widget_set_margin_bottom(row, 12);
    gtk_box_pack_start(GTK_BOX(card), row, FALSE, FALSE, 0);

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);

    if (!scale) scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_style_context_add_class(gtk_widget_get_style_context(scale), "cc-slider");

    gtk_box_pack_start(GTK_BOX(row), scale, TRUE, TRUE, 0);
    return card;
}

static void on_toggle_active(GtkButton *button, gpointer data)
{
    (void)data;
    GtkStyleContext *ctx = gtk_widget_get_style_context(GTK_WIDGET(button));
    gboolean is_active = gtk_style_context_has_class(ctx, "active");
    toggle_style_class(GTK_WIDGET(button), "active", !is_active);
}

static GtkWidget *create_compound_connectivity_card(void)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkStyleContext *card_ctx = gtk_widget_get_style_context(card);
    gtk_style_context_add_class(card_ctx, "cc-card");
    gtk_style_context_add_class(card_ctx, "cc-compound-card");

    GtkWidget *net = create_cc_row_button("network-wireless-symbolic",
                                          "Network",
                                          cc_wifi_subtitle_text(),
                                          "cc-blue",
                                          cc_wifi_is_enabled());
    cc_wifi_attach(net);
    gtk_box_pack_start(GTK_BOX(card), net, FALSE, FALSE, 0);

    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sep1), "cc-divider");
    gtk_box_pack_start(GTK_BOX(card), sep1, FALSE, FALSE, 0);

    GtkWidget *bt = create_cc_row_button("bluetooth-symbolic",
                                         "Bluetooth",
                                         cc_bluetooth_subtitle_text(),
                                         "cc-blue",
                                         cc_bluetooth_is_powered());
    cc_bluetooth_attach(bt);
    gtk_box_pack_start(GTK_BOX(card), bt, FALSE, FALSE, 0);

    GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_style_context_add_class(gtk_widget_get_style_context(sep2), "cc-divider");
    gtk_box_pack_start(GTK_BOX(card), sep2, FALSE, FALSE, 0);

    GtkWidget *eth = create_cc_row_button("network-wired-symbolic",
                                          "Ethernet",
                                          cc_ethernet_subtitle_text(),
                                          "cc-blue",
                                          cc_ethernet_is_enabled());
    cc_ethernet_attach(eth);
    gtk_box_pack_start(GTK_BOX(card), eth, FALSE, FALSE, 0);

    return card;
}

static GtkWidget *create_power_profiles_card(void)
{
    PowerProfilesUI *ui = g_new0(PowerProfilesUI, 1);

    GtkWidget *card = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(card), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(card, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "cc-card");
    gtk_style_context_add_class(gtk_widget_get_style_context(card), "cc-row-button");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);
    gtk_container_add(GTK_CONTAINER(card), box);

    GtkWidget *icon_circle = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(icon_circle), TRUE);
    gtk_widget_set_size_request(icon_circle, 34, 34);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_circle), "cc-icon-circle");
    gtk_style_context_add_class(gtk_widget_get_style_context(icon_circle), "cc-blue");
    GtkWidget *icon = gtk_image_new_from_icon_name("power-profile-balanced-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "cc-icon-contrast");
    gtk_container_add(GTK_CONTAINER(icon_circle), icon);
    gtk_box_pack_start(GTK_BOX(box), icon_circle, FALSE, FALSE, 0);

    GtkWidget *text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text_box, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), text_box, TRUE, TRUE, 0);

    GtkWidget *title = gtk_label_new("Power Mode");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "cc-row-title");
    gtk_box_pack_start(GTK_BOX(text_box), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = gtk_label_new("Balanced");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "cc-row-subtitle");
    gtk_box_pack_start(GTK_BOX(text_box), subtitle, FALSE, FALSE, 0);

    ui->button = card;
    ui->icon = icon;
    ui->subtitle = subtitle;

    g_signal_connect(card, "clicked", G_CALLBACK(on_power_profile_clicked), ui);
    g_object_set_data_full(G_OBJECT(card), "power-profiles-ui", ui, power_profiles_ui_free);
    g_signal_connect(card, "destroy", G_CALLBACK(on_power_profiles_card_destroy), NULL);
    power_profile_init(on_power_profile_changed, ui);
    power_profiles_set_active(ui, "balanced");

    return card;
}

static GtkWidget *create_media_card(GtkWidget *window_for_store)
{
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    GtkStyleContext *ctx = gtk_widget_get_style_context(card);
    gtk_style_context_add_class(ctx, "cc-card");
    gtk_style_context_add_class(ctx, "cc-media-card");

    GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(inner, 14);
    gtk_widget_set_margin_end(inner, 14);
    gtk_widget_set_margin_top(inner, 12);
    gtk_widget_set_margin_bottom(inner, 12);
    gtk_box_pack_start(GTK_BOX(card), inner, FALSE, FALSE, 0);

    GtkWidget *art_frame = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(art_frame), TRUE);
    gtk_widget_set_size_request(art_frame, 64, 64);
    gtk_style_context_add_class(gtk_widget_get_style_context(art_frame), "cc-art");

    GtkWidget *art = gtk_image_new_from_icon_name("multimedia-player-symbolic", GTK_ICON_SIZE_DIALOG);
    gtk_container_add(GTK_CONTAINER(art_frame), art);
    gtk_box_pack_start(GTK_BOX(inner), art_frame, FALSE, FALSE, 0);

    GtkWidget *text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(text, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(inner), text, TRUE, TRUE, 0);

    GtkWidget *title = gtk_label_new("Nothing Playing");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "cc-media-title");
    gtk_box_pack_start(GTK_BOX(text), title, FALSE, FALSE, 0);

    GtkWidget *artist = gtk_label_new("—");
    gtk_widget_set_halign(artist, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(artist), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(artist), "cc-media-artist");
    gtk_box_pack_start(GTK_BOX(text), artist, FALSE, FALSE, 0);

    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_valign(controls, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(inner), controls, FALSE, FALSE, 0);

    GtkWidget *btn_prev = gtk_button_new_from_icon_name("media-skip-backward-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_play = gtk_button_new_from_icon_name("media-playback-start-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_next = gtk_button_new_from_icon_name("media-skip-forward-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief(GTK_BUTTON(btn_prev), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(btn_play), GTK_RELIEF_NONE);
    gtk_button_set_relief(GTK_BUTTON(btn_next), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(btn_prev, FALSE);
    gtk_widget_set_can_focus(btn_play, FALSE);
    gtk_widget_set_can_focus(btn_next, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_prev), "cc-media-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_play), "cc-media-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn_next), "cc-media-btn");

    gtk_box_pack_start(GTK_BOX(controls), btn_prev, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), btn_play, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(controls), btn_next, FALSE, FALSE, 0);

    cc_mpris_attach(window_for_store, title, artist, art, btn_prev, btn_play, btn_next);

    return card;
}

/* =====================================================================
 * MAIN CONTENT
 * ===================================================================== */

static GtkWidget* create_controls_content(GtkWidget *window_for_store)
{
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(root, 16);
    gtk_widget_set_margin_bottom(root, 16);
    gtk_widget_set_margin_start(root, 16);
    gtk_widget_set_margin_end(root, 16);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_box_pack_start(GTK_BOX(root), grid, FALSE, FALSE, 0);

    GtkWidget *compound = create_compound_connectivity_card();
    gtk_widget_set_hexpand(compound, TRUE);
    gtk_grid_attach(GTK_GRID(grid), compound, 0, 0, 1, 2);

    GtkWidget *pwr = create_power_profiles_card();
    gtk_widget_set_hexpand(pwr, TRUE);
    gtk_grid_attach(GTK_GRID(grid), pwr, 1, 0, 1, 1);

    GtkWidget *small_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *settings = create_cc_small_tile("emblem-system-symbolic", "Settings", FALSE);
    GtkWidget *screenshot = create_cc_small_tile("screenshot", "screenshot", FALSE);
    g_signal_connect(settings, "clicked", G_CALLBACK(cc_settings_open), NULL);
    g_signal_connect(screenshot, "clicked", G_CALLBACK(on_toggle_active), NULL);
    gtk_box_pack_start(GTK_BOX(small_row), settings, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(small_row), screenshot, TRUE, TRUE, 0);
    gtk_grid_attach(GTK_GRID(grid), small_row, 1, 1, 1, 1);

    GtkWidget *vol = create_cc_slider_card("Volume", "audio-volume-high-symbolic", cc_audio_create_volume_scale());
    GtkWidget *bri = create_cc_slider_card("Display Brightness", "display-brightness-symbolic", cc_brightness_create_scale());
    gtk_box_pack_start(GTK_BOX(root), vol, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), bri, FALSE, FALSE, 0);

    GtkWidget *media = create_media_card(window_for_store);
    gtk_box_pack_start(GTK_BOX(root), media, FALSE, FALSE, 0);

    return root;
}

static void cc_position_control_center(GtkWidget *window)
{
    if (!GTK_IS_WINDOW(window)) return;

    GdkDisplay *display = gdk_display_get_default();
    if (!display) return;

    GdkMonitor *monitor = NULL;
    GdkSeat *seat = gdk_display_get_default_seat(display);
    if (seat) {
        GdkDevice *pointer = gdk_seat_get_pointer(seat);
        if (pointer) {
            gint px = 0, py = 0;
            gdk_device_get_position(pointer, NULL, &px, &py);
            monitor = gdk_display_get_monitor_at_point(display, px, py);
        }
    }
    if (!monitor) monitor = gdk_display_get_primary_monitor(display);
    if (!monitor) return;

    GdkRectangle workarea = {0};
#if GTK_CHECK_VERSION(3, 22, 0)
    gdk_monitor_get_workarea(monitor, &workarea);
#else
    gdk_monitor_get_geometry(monitor, &workarea);
#endif

    int win_w = gtk_widget_get_allocated_width(window);
    int win_h = gtk_widget_get_allocated_height(window);
    if (win_w <= 1 || win_h <= 1) {
        gtk_window_get_size(GTK_WINDOW(window), &win_w, &win_h);
    }
    if (win_w <= 1) win_w = 360;
    if (win_h <= 1) win_h = 500;

    const int margin = 16;
    const int panel_gap = 2;
    const int panel_height = 25; /* fallback when panel doesn't reserve workarea */
    int x = workarea.x + workarea.width - win_w - margin;
    int y = workarea.y + margin + panel_height + panel_gap;
    if (panel_geometry_get_config_edge() == PANEL_EDGE_BOTTOM) {
        y = workarea.y + workarea.height - win_h - margin - panel_height - panel_gap;
    }

    if (x < workarea.x + margin) x = workarea.x + margin;
    if (y < workarea.y + margin) y = workarea.y + margin;
    if (x + win_w > workarea.x + workarea.width - margin) x = workarea.x + workarea.width - win_w - margin;
    if (y + win_h > workarea.y + workarea.height - margin) y = workarea.y + workarea.height - win_h - margin;
    if (x < workarea.x) x = workarea.x;
    if (y < workarea.y) y = workarea.y;

    gtk_window_move(GTK_WINDOW(window), x, y);
}

static gboolean cc_deferred_position(gpointer data)
{
    GtkWidget *window = GTK_WIDGET(data);
    cc_position_control_center(window);
    g_object_unref(window);
    return G_SOURCE_REMOVE;
}

static void on_cc_realize(GtkWidget *widget, gpointer data)
{
    (void)data;
    g_idle_add(cc_deferred_position, g_object_ref(widget));
}

static void on_cc_show(GtkWidget *widget, gpointer data)
{
    (void)data;
    g_idle_add(cc_deferred_position, g_object_ref(widget));
}

/* =====================================================================
 * MAIN WINDOW
 * ===================================================================== */

GtkWidget *create_control_center(void)
{
    /* Initialize services */
    cc_brightness_init();
    cc_network_init();
    cc_shot_init();
    cc_audio_init();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    /* Transparency + solid background via draw callback */
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(window, visual);
        gtk_widget_set_app_paintable(window, TRUE);
    }

    /* Draw the semi-transparent background (CSS alone can't fill it when
     * app_paintable is TRUE — we must paint it ourselves first) */
    g_signal_connect(window, "draw", G_CALLBACK(
        ({
            gboolean _draw(GtkWidget *w, cairo_t *cr, gpointer ud) {
                (void)w; (void)ud;
                cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);
                cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
                cairo_paint(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                return FALSE;
            }
            _draw;
        })), NULL);

    gtk_window_set_title(GTK_WINDOW(window), "Control Center");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 500);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);

    /* ── Window backend: X11 → override_redirect popup
     *                    Wayland → layer-shell TOP, no keyboard grab ── */
    panel_window_backend_init_popup(GTK_WINDOW(window),
                                   "venom-control-center",
                                   GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU,
                                   GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    if (panel_window_backend_is_wayland()) {
        panel_window_backend_anchor_popup_to_panel(
            GTK_WINDOW(window),
            GTK_LAYER_SHELL_EDGE_RIGHT, 8);
    } else {
        /* X11: use the existing deferred-position logic (gtk_window_move) */
        g_signal_connect(window, "realize", G_CALLBACK(on_cc_realize), NULL);
        g_signal_connect(window, "show",    G_CALLBACK(on_cc_show),    NULL);
    }

    GtkWidget *content = create_controls_content(window);
    gtk_container_add(GTK_CONTAINER(window), content);

    /* CSS Class for styling */
    GtkStyleContext *ctx = gtk_widget_get_style_context(window);
    gtk_style_context_add_class(ctx, "control-center");

    /* Cleanup on window destroy */
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(cc_brightness_cleanup), NULL);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(cc_network_cleanup), NULL);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(cc_shot_cleanup), NULL);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(cc_audio_cleanup), NULL);
    g_signal_connect_swapped(window, "destroy", G_CALLBACK(power_profile_cleanup), NULL);

    return window;
}

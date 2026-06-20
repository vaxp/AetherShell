/*
 * net-speed.c — Network Speed Plugin
 * Updated to AetherPanelPluginAPIv3
 *
 * Displays animated RX/TX chart + live speed labels in the panel bar.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "panel-plugin-api.h"   /* ← AetherPanelPluginAPIv3 */

#define UPDATE_INTERVAL_MS  1000
#define HISTORY_LEN         60

/* ── Plugin theme (dark glassmorphism, cyan accent) ───────────────────── */
static const AetherPluginTheme net_theme = AETHER_THEME_DARK(
    "net-speed-window",   /* window_css_id  */
    "net-speed-outer",    /* outer_css_id   */
    0.2, 0.8, 1.0,        /* root: cyan     */
    200                   /* min_width px   */
);

/* ── Plugin visuals (panel-bar slot appearance) ───────────────────────── */
static const AetherPluginVisuals net_visuals = {
    .bg_type      = AETHER_PLUGIN_BG_INHERIT,
    .border_enabled = FALSE,
    .shadow_enabled = FALSE,
};

/* =========================================================================
 * Per-instance data
 * ========================================================================= */
typedef struct {
    GtkWidget *chart_area;
    GtkWidget *rx_label;
    GtkWidget *tx_label;

    /* Ring buffer for history (bytes per second) */
    double rx_hist[HISTORY_LEN];
    double tx_hist[HISTORY_LEN];
    int    hist_idx;
    guint  timer_id;

    double max_speed; /* Dynamically scales the chart */

    /* Previous totals */
    unsigned long long last_rx;
    unsigned long long last_tx;
} NetMonitorData;

/* =========================================================================
 * Helpers
 * ========================================================================= */

/* Reads total rx/tx bytes across all real interfaces */
static void get_net_bytes(unsigned long long *rx_out, unsigned long long *tx_out)
{
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) { *rx_out = 0; *tx_out = 0; return; }

    char line[512];
    unsigned long long total_rx = 0, total_tx = 0;

    /* Skip first two header lines */
    if (fgets(line, sizeof(line), fp)) {}
    if (fgets(line, sizeof(line), fp)) {}

    while (fgets(line, sizeof(line), fp)) {
        char               iface[32];
        unsigned long long rx, tx, dummy;

        if (sscanf(line,
                   " %31[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   iface, &rx,
                   &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
                   &tx) >= 10)
        {
            if (strncmp(iface, "lo", 2) != 0) {   /* ignore loopback */
                total_rx += rx;
                total_tx += tx;
            }
        }
    }
    fclose(fp);
    *rx_out = total_rx;
    *tx_out = total_tx;
}

/* Format bytes/sec → human-readable string */
static void format_speed(double bps, char *out, size_t max_len)
{
    if (bps >= 1024.0 * 1024.0)
        snprintf(out, max_len, "%.1f M/s", bps / (1024.0 * 1024.0));
    else if (bps >= 1024.0)
        snprintf(out, max_len, "%.0f K/s", bps / 1024.0);
    else
        snprintf(out, max_len, "%.0f B/s", bps);
}

/* =========================================================================
 * Drawing
 * ========================================================================= */
static gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    NetMonitorData *data   = (NetMonitorData *)user_data;
    int             width  = gtk_widget_get_allocated_width(widget);
    int             height = gtk_widget_get_allocated_height(widget);

    /* Background */
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.6);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* Dynamic max */
    double local_max = 1024.0 * 100.0;
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (data->rx_hist[i] > local_max) local_max = data->rx_hist[i];
        if (data->tx_hist[i] > local_max) local_max = data->tx_hist[i];
    }
    if (local_max > data->max_speed)
        data->max_speed = local_max;
    else
        data->max_speed = data->max_speed * 0.95 + local_max * 0.05;

    /* Macro: draw one history polyline (optionally filled) */
#define DRAW_LINE(hist_array, r, g, b, is_fill) do {                       \
        cairo_set_line_width(cr, 1.0);                                      \
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);                     \
        double dx = (double)width / (HISTORY_LEN - 1);                     \
        if (is_fill) cairo_move_to(cr, 0, height);                         \
        for (int i = 0; i < HISTORY_LEN; i++) {                            \
            int    idx = (data->hist_idx + i) % HISTORY_LEN;               \
            double val = hist_array[idx] / data->max_speed;                 \
            if (val > 1.0) val = 1.0;                                       \
            double x   = i * dx;                                            \
            double y   = height - (val * height);                           \
            if (i == 0 && !is_fill) cairo_move_to(cr, x, y);               \
            else cairo_line_to(cr, x, y);                                   \
        }                                                                   \
        if (is_fill) {                                                      \
            cairo_line_to(cr, width, height);                               \
            cairo_set_source_rgba(cr, r, g, b, 0.2);                       \
            cairo_fill(cr);                                                 \
        } else {                                                            \
            cairo_set_source_rgba(cr, r, g, b, 1.0);                       \
            cairo_stroke(cr);                                               \
        }                                                                   \
    } while (0)

    /* TX line — upload (orange/red) */
    DRAW_LINE(data->tx_hist, 1.0, 0.4, 0.2, 0);

    /* RX fill then outline — download (cyan) */
    DRAW_LINE(data->rx_hist, 0.2, 0.8, 1.0, 1);
    DRAW_LINE(data->rx_hist, 0.2, 0.8, 1.0, 0);

#undef DRAW_LINE

    return FALSE;
}

/* =========================================================================
 * Timer callback — reads /proc/net/dev and refreshes the UI
 * ========================================================================= */
static gboolean update_net_stats(gpointer user_data)
{
    NetMonitorData *data = (NetMonitorData *)user_data;
    if (!GTK_IS_WIDGET(data->chart_area)) return G_SOURCE_REMOVE;

    unsigned long long cur_rx = 0, cur_tx = 0;
    get_net_bytes(&cur_rx, &cur_tx);

    double rx_speed = 0.0, tx_speed = 0.0;
    if (data->last_rx > 0 || data->last_tx > 0) {
        if (cur_rx > data->last_rx) rx_speed = (double)(cur_rx - data->last_rx);
        if (cur_tx > data->last_tx) tx_speed = (double)(cur_tx - data->last_tx);
    }
    data->last_rx = cur_rx;
    data->last_tx = cur_tx;

    /* Update labels */
    char rx_fmt[32], tx_fmt[32], rx_txt[128], tx_txt[128];
    format_speed(rx_speed, rx_fmt, sizeof(rx_fmt));
    format_speed(tx_speed, tx_fmt, sizeof(tx_fmt));
    snprintf(rx_txt, sizeof(rx_txt), "<span font_size='x-small'>▼ %s</span>", rx_fmt);
    snprintf(tx_txt, sizeof(tx_txt), "<span font_size='x-small'>▲ %s</span>", tx_fmt);
    gtk_label_set_markup(GTK_LABEL(data->rx_label), rx_txt);
    gtk_label_set_markup(GTK_LABEL(data->tx_label), tx_txt);

    /* Push into ring buffer */
    data->rx_hist[data->hist_idx] = rx_speed;
    data->tx_hist[data->hist_idx] = tx_speed;
    data->hist_idx = (data->hist_idx + 1) % HISTORY_LEN;

    gtk_widget_queue_draw(data->chart_area);
    return G_SOURCE_CONTINUE;
}

/* =========================================================================
 * Widget lifecycle
 * ========================================================================= */
static void on_widget_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    NetMonitorData *data = (NetMonitorData *)user_data;
    if (data->timer_id > 0)
        g_source_remove(data->timer_id);
    g_free(data);
}

/* create_widget — called by the panel (AetherPanelContext provided) */
static GtkWidget *create_widget(AetherPanelContext *ctx)
{
    (void)ctx;   /* not used by this plugin yet, reserved for future use */

    NetMonitorData *data = g_new0(NetMonitorData, 1);
    data->max_speed = 1024.0 * 100.0; /* start at 100 KB/s to prevent jitter */

    /* ── Root container ──────────────────────────────────────────────── */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(main_box, 4);
    gtk_widget_set_margin_end(main_box, 4);

    /* ── Mini chart ──────────────────────────────────────────────────── */
    data->chart_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->chart_area, 40, 24);
    gtk_widget_set_valign(data->chart_area, GTK_ALIGN_CENTER);
    g_signal_connect(data->chart_area, "draw", G_CALLBACK(on_draw_chart), data);
    gtk_box_pack_start(GTK_BOX(main_box), data->chart_area, FALSE, FALSE, 0);

    /* ── Labels ──────────────────────────────────────────────────────── */
    GtkWidget *labels_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(labels_box, GTK_ALIGN_CENTER);

    data->rx_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(data->rx_label),
                         "<span font_size='x-small'>▼ 0 B/s</span>");
    gtk_widget_set_size_request(data->rx_label, 55, -1);
    gtk_label_set_xalign(GTK_LABEL(data->rx_label), 0.0);

    data->tx_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(data->tx_label),
                         "<span font_size='x-small'>▲ 0 B/s</span>");
    gtk_widget_set_size_request(data->tx_label, 55, -1);
    gtk_label_set_xalign(GTK_LABEL(data->tx_label), 0.0);

    gtk_box_pack_start(GTK_BOX(labels_box), data->rx_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_box), data->tx_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), labels_box, FALSE, FALSE, 0);

    /* ── Bootstrap ───────────────────────────────────────────────────── */
    get_net_bytes(&data->last_rx, &data->last_tx);
    update_net_stats(data);

    data->timer_id = g_timeout_add(UPDATE_INTERVAL_MS, update_net_stats, data);
    g_signal_connect(main_box, "destroy", G_CALLBACK(on_widget_destroy), data);

    gtk_widget_show_all(main_box);
    return main_box;
}

/* destroy_widget — optional but good practice */
static void destroy_widget(GtkWidget *widget)
{
    /* The "destroy" signal handler (on_widget_destroy) already cleans up.
     * This hook exists so the panel can call it explicitly if needed. */
    gtk_widget_destroy(widget);
}

/* =========================================================================
 * Config schema — exported so the Settings UI can enumerate our options
 * ========================================================================= */
static const char *cfg_keys[]     = { "refresh_ms", NULL };
static const char *cfg_defaults[] = { "1000",       NULL };

static void get_config_schema(const char ***keys, const char ***defaults)
{
    *keys     = cfg_keys;
    *defaults = cfg_defaults;
}

/*
 * on_config_changed — react to live config updates from panel.conf.
 * Currently only "refresh_ms" is handled; unknown keys are silently ignored.
 */
static void on_config_changed(GtkWidget *widget,
                               const char *key,
                               const char *value)
{
    if (!key || !value) return;

    if (strcmp(key, "refresh_ms") == 0) {
        /*
         * To honour a new interval we'd need access to the NetMonitorData
         * pointer.  The cleanest approach is to store it as widget data.
         */
        NetMonitorData *data =
            (NetMonitorData *)g_object_get_data(G_OBJECT(widget),
                                                "net-monitor-data");
        if (!data) return;

        guint new_ms = (guint)atoi(value);
        if (new_ms < 100) new_ms = 100;   /* hard floor */

        if (data->timer_id > 0)
            g_source_remove(data->timer_id);

        data->timer_id = g_timeout_add(new_ms, update_net_stats, data);
    }
}

/* ── Theme getter ─────────────────────────────────────────────────────── */
static const AetherPluginTheme *get_theme(void)
{
    return &net_theme;
}

/* =========================================================================
 * Plugin entry point  —  aether_panel_plugin_init_v3()
 * ========================================================================= */
AetherPanelPluginAPIv3 *aether_panel_plugin_init_v3(void)
{
    static AetherPanelPluginAPIv3 api = {
        .api_version = AETHER_PANEL_PLUGIN_API_VERSION,   /* 3 */
        .struct_size = sizeof(AetherPanelPluginAPIv3),

        /* ── Identity ─────────────────────────────────────────────── */
        .name        = "Network Speed",
        .description = "Displays animated network RX/TX graph and speeds.",
        .author      = "Venom",
        .version     = "3.0.0",
        .icon_name   = "network-transmit-receive-symbolic",

        /* ── Layout hints ─────────────────────────────────────────── */
        .zone      = AETHER_PLUGIN_ZONE_RIGHT,
        .priority  = 0,
        .expand    = FALSE,
        .padding   = 6,
        .min_width = 0,
        .max_width = -1,

        /* ── Visuals ──────────────────────────────────────────────── */
        .visuals = {
            .bg_type        = AETHER_PLUGIN_BG_INHERIT,
            .border_enabled = FALSE,
            .shadow_enabled = FALSE,
        },

        /* ── Behaviour ────────────────────────────────────────────── */
        .singleton   = FALSE,
        .watchdog_ms = 2000,   /* kill create_widget() if it hangs > 2 s */

        /* ── Lifecycle ────────────────────────────────────────────── */
        .create_widget  = create_widget,
        .destroy_widget = destroy_widget,

        /* ── Integration ──────────────────────────────────────────── */
        .create_popover  = NULL,   /* no popup window */
        .get_context_menu = NULL,  /* no right-click menu */

        /* ── Events & config ──────────────────────────────────────── */
        .on_system_event  = NULL,          /* no special handling needed */
        .on_config_changed = on_config_changed,
        .get_config_schema = get_config_schema,

        /* ── Theme ────────────────────────────────────────────────── */
        .get_theme = get_theme,
    };

    return &api;
}
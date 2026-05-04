#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "venom-panel-plugin-api.h"

#define UPDATE_INTERVAL_MS 1000
#define HISTORY_LEN 60

typedef struct {
    GtkWidget *chart_area;
    GtkWidget *rx_label;
    GtkWidget *tx_label;
    
    /* Ring buffer for history (bytes per second) */
    double rx_hist[HISTORY_LEN];
    double tx_hist[HISTORY_LEN];
    int hist_idx;
    guint timer_id;
    
    double max_speed; /* Dynamically scales the chart */
    
    /* Previous totals */
    unsigned long long last_rx;
    unsigned long long last_tx;
} NetMonitorData;

/* Reads total rx/tx bytes across all real interfaces */
static void get_net_bytes(unsigned long long *rx_out, unsigned long long *tx_out) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) {
        *rx_out = 0; *tx_out = 0;
        return;
    }

    char line[512];
    unsigned long long total_rx = 0, total_tx = 0;
    
    /* Skip first two header lines */
    if (fgets(line, sizeof(line), fp)) {}
    if (fgets(line, sizeof(line), fp)) {}
    
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        unsigned long long rx, tx;
        unsigned long long dummy;
        
        if (sscanf(line, " %31[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                   iface, &rx, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &tx) >= 10) {
            
            /* Ignore loopback */
            if (strncmp(iface, "lo", 2) != 0) {
                total_rx += rx;
                total_tx += tx;
            }
        }
    }
    fclose(fp);
    
    *rx_out = total_rx;
    *tx_out = total_tx;
}

/* Format bytes/sec to human readable string (KB/s, MB/s) */
static void format_speed(double bytes_per_sec, char *out, size_t max_len) {
    if (bytes_per_sec >= 1024.0 * 1024.0) {
        snprintf(out, max_len, "%.1f M/s", bytes_per_sec / (1024.0 * 1024.0));
    } else if (bytes_per_sec >= 1024.0) {
        snprintf(out, max_len, "%.0f K/s", bytes_per_sec / 1024.0);
    } else {
        snprintf(out, max_len, "%.0f B/s", bytes_per_sec);
    }
}

/* Drawing function for the chart */
static gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    NetMonitorData *data = (NetMonitorData*)user_data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background (dark rounded rectangle) */
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* Dynamic scaling */
    double local_max = 1024.0 * 100.0; /* Minimum scale: 100 KB/s */
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (data->rx_hist[i] > local_max) local_max = data->rx_hist[i];
        if (data->tx_hist[i] > local_max) local_max = data->tx_hist[i];
    }
    
    /* Smooth scaling logic: move towards local_max but slowly down, fast up */
    if (local_max > data->max_speed) {
        data->max_speed = local_max;
    } else {
        data->max_speed = data->max_speed * 0.95 + local_max * 0.05;
    }

    /* Helper macro to draw a history line */
    #define DRAW_LINE(hist_array, r, g, b, is_fill) do { \
        cairo_set_source_rgba(cr, r, g, b, 1.0); \
        cairo_set_line_width(cr, 1.0); \
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); \
        double dx = (double)width / (HISTORY_LEN - 1); \
        if (is_fill) cairo_move_to(cr, 0, height); \
        for (int i = 0; i < HISTORY_LEN; i++) { \
            int data_idx = (data->hist_idx + i) % HISTORY_LEN; \
            double val = hist_array[data_idx] / data->max_speed; \
            if (val > 1.0) val = 1.0; \
            double x = i * dx; \
            double y = height - (val * height); \
            if (i == 0 && !is_fill) cairo_move_to(cr, x, y); \
            else cairo_line_to(cr, x, y); \
        } \
        if (is_fill) { \
            cairo_line_to(cr, width, height); \
            cairo_set_source_rgba(cr, r, g, b, 0.2); \
            cairo_fill(cr); \
        } else { \
            cairo_stroke(cr); \
        } \
    } while(0)

    /* Draw TX line (Upload - Red/Orange) */
    DRAW_LINE(data->tx_hist, 1.0, 0.4, 0.2, 0);
    
    /* Draw RX line (Download - Cyan/Blue) - Filled for depth */
    DRAW_LINE(data->rx_hist, 0.2, 0.8, 1.0, 1);
    /* Draw RX outline */
    DRAW_LINE(data->rx_hist, 0.2, 0.8, 1.0, 0);

    return FALSE;
}

/* Background task */
static gboolean update_net_stats(gpointer user_data) {
    NetMonitorData *data = (NetMonitorData*)user_data;
    if (!GTK_IS_WIDGET(data->chart_area)) return G_SOURCE_REMOVE; /* Dead */

    unsigned long long current_rx = 0, current_tx = 0;
    get_net_bytes(&current_rx, &current_tx);

    /* Calculate speed (bytes per second) */
    double rx_speed = 0.0, tx_speed = 0.0;
    if (data->last_rx > 0 || data->last_tx > 0) {
        if (current_rx > data->last_rx) rx_speed = (double)(current_rx - data->last_rx);
        if (current_tx > data->last_tx) tx_speed = (double)(current_tx - data->last_tx);
    }
    
    data->last_rx = current_rx;
    data->last_tx = current_tx;

    /* Update textual labels */
    char rx_txt[128], tx_txt[128];
    char rx_fmt[32], tx_fmt[32];
    format_speed(rx_speed, rx_fmt, sizeof(rx_fmt));
    format_speed(tx_speed, tx_fmt, sizeof(tx_fmt));
    
    snprintf(rx_txt, sizeof(rx_txt), "<span font_size='x-small'>▼ %s</span>", rx_fmt);
    snprintf(tx_txt, sizeof(tx_txt), "<span font_size='x-small'>▲ %s</span>", tx_fmt);
    
    gtk_label_set_markup(GTK_LABEL(data->rx_label), rx_txt);
    gtk_label_set_markup(GTK_LABEL(data->tx_label), tx_txt);

    /* Update history buffer */
    data->rx_hist[data->hist_idx] = rx_speed;
    data->tx_hist[data->hist_idx] = tx_speed;
    data->hist_idx = (data->hist_idx + 1) % HISTORY_LEN;

    /* Queue redraw for the chart */
    gtk_widget_queue_draw(data->chart_area);

    return G_SOURCE_CONTINUE;
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    NetMonitorData *data = (NetMonitorData *)user_data;
    if (data->timer_id > 0) {
        g_source_remove(data->timer_id);
    }
    g_free(data);
}

/* Construct the UI */
static GtkWidget* create_net_monitor_widget(void) {
    NetMonitorData *data = g_new0(NetMonitorData, 1);
    data->max_speed = 1024.0 * 100.0; /* start assuming 100KB/s max to prevent huge jitter */
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(main_box, 4);
    gtk_widget_set_margin_end(main_box, 4);

    /* --- Chart Area --- */
    data->chart_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->chart_area, 40, 24);
    gtk_widget_set_valign(data->chart_area, GTK_ALIGN_CENTER);
    g_signal_connect(data->chart_area, "draw", G_CALLBACK(on_draw_chart), data);
    gtk_box_pack_start(GTK_BOX(main_box), data->chart_area, FALSE, FALSE, 0);

    /* --- Labels Box (Stacked vertically) --- */
    GtkWidget *labels_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(labels_box, GTK_ALIGN_CENTER);

    /* RX Label (Download - Cyan) */
    data->rx_label = gtk_label_new("▼ 0 B/s");
    gtk_label_set_markup(GTK_LABEL(data->rx_label), "<span font_size='x-small'>▼ 0 B/s</span>");
    gtk_widget_set_size_request(data->rx_label, 55, -1);
    gtk_label_set_xalign(GTK_LABEL(data->rx_label), 0.0);
    
    /* TX Label (Upload - Red/Orange) */
    data->tx_label = gtk_label_new("▲ 0 B/s");
    gtk_label_set_markup(GTK_LABEL(data->tx_label), "<span font_size='x-small'>▲ 0 B/s</span>");
    gtk_widget_set_size_request(data->tx_label, 55, -1);
    gtk_label_set_xalign(GTK_LABEL(data->tx_label), 0.0);
    
    gtk_box_pack_start(GTK_BOX(labels_box), data->rx_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_box), data->tx_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), labels_box, FALSE, FALSE, 0);

    /* Initialize with dummy fast reading */
    get_net_bytes(&data->last_rx, &data->last_tx);
    update_net_stats(data);

    /* Attach timer */
    data->timer_id = g_timeout_add(UPDATE_INTERVAL_MS, update_net_stats, data);
    g_signal_connect(main_box, "destroy", G_CALLBACK(on_widget_destroy), data);

    gtk_widget_show_all(main_box);
    return main_box;
}

VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "Network Speed";
    api.description   = "Displays animated network RX/TX graph and speeds.";
    api.author        = "Venom";
    api.expand        = FALSE;
    api.padding       = 6;
    api.create_widget = create_net_monitor_widget;
    return &api;
}

VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size = sizeof(VenomPanelPluginAPIv2),
        .name = "Network Speed",
        .description = "Displays animated network RX/TX graph and speeds.",
        .author = "Venom",
        .zone = VENOM_PLUGIN_ZONE_RIGHT,
        .priority = 0,
        .expand = FALSE,
        .padding = 6,
        .create_widget = create_net_monitor_widget,
        .destroy_widget = NULL,
    };
    return &api;
}

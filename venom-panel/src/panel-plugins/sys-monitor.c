#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "venom-panel-plugin-api.h"

#define UPDATE_INTERVAL_MS 1000
#define HISTORY_LEN 60

typedef struct {
    GtkWidget *chart_area;
    GtkWidget *cpu_label;
    GtkWidget *ram_label;
    
    /* Ring buffer for history */
    double cpu_hist[HISTORY_LEN];
    double ram_hist[HISTORY_LEN];
    int hist_idx;
    guint timer_id;
    
    /* For CPU calc */
    unsigned long long last_total;
    unsigned long long last_idle;
} SysMonitorData;

/* Reads CPU usage percentage */
static double get_cpu_usage(SysMonitorData *data) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return 0.0;
    
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    char buffer[256];
    
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return 0.0;
    }
    fclose(fp);
    
    if (sscanf(buffer, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice) < 10) {
        return 0.0;
    }
    
    unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long current_idle = idle + iowait;

    unsigned long long total_diff = total - data->last_total;
    unsigned long long idle_diff = current_idle - data->last_idle;

    data->last_total = total;
    data->last_idle = current_idle;

    if (total_diff == 0) return 0.0;
    
    double usage = (double)(total_diff - idle_diff) / total_diff;
    return usage * 100.0;
}

/* Reads RAM usage completely natively */
static void get_ram_usage(double *percent, char *text_out, size_t max_len) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) {
        snprintf(text_out, max_len, "RAM Error");
        *percent = 0.0;
        return;
    }

    char line[256];
    unsigned long long mem_total = 0, mem_available = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line, "MemTotal: %llu kB", &mem_total);
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line, "MemAvailable: %llu kB", &mem_available);
        }
        
        if (mem_total > 0 && mem_available > 0) break;
    }
    fclose(fp);

    if (mem_total == 0) {
        snprintf(text_out, max_len, "?");
        *percent = 0.0;
        return;
    }

    unsigned long long mem_used = mem_total - mem_available;
    *percent = ((double)mem_used / mem_total) * 100.0;
    
    double used_gb = (double)mem_used / (1024.0 * 1024.0);
    snprintf(text_out, max_len, "%.1fG", used_gb);
}

/* Drawing function for the chart */
static gboolean on_draw_chart(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    SysMonitorData *data = (SysMonitorData*)user_data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background (dark rounded rectangle) */
    cairo_set_source_rgba(cr, 0.1, 0.1, 0.1, 0.6);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* Helper macro to draw a history line */
    #define DRAW_LINE(hist_array, r, g, b) do { \
        cairo_set_source_rgba(cr, r, g, b, 1.0); \
        cairo_set_line_width(cr, 1.5); \
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); \
        double dx = (double)width / (HISTORY_LEN - 1); \
        for (int i = 0; i < HISTORY_LEN; i++) { \
            int data_idx = (data->hist_idx + i) % HISTORY_LEN; \
            double val = hist_array[data_idx] / 100.0; /* 0.0 to 1.0 */ \
            double x = i * dx; \
            double y = height - (val * height); \
            if (i == 0) cairo_move_to(cr, x, y); \
            else cairo_line_to(cr, x, y); \
        } \
        cairo_stroke(cr); \
    } while(0)

    /* Draw RAM line (Blue-ish) */
    DRAW_LINE(data->ram_hist, 0.2, 0.6, 1.0);
    
    /* Draw CPU line (Green/Yellow-ish) */
    DRAW_LINE(data->cpu_hist, 0.5, 0.9, 0.2);

    return FALSE;
}

/* Background task */
static gboolean update_sys_stats(gpointer user_data) {
    SysMonitorData *data = (SysMonitorData*)user_data;
    if (!GTK_IS_WIDGET(data->chart_area)) return G_SOURCE_REMOVE; /* Dead */

    /* CPU */
    double cpu_pct = get_cpu_usage(data);
    char cpu_txt[32];
    snprintf(cpu_txt, sizeof(cpu_txt), "%2.0f%%", cpu_pct);
    gtk_label_set_text(GTK_LABEL(data->cpu_label), cpu_txt);

    /* RAM */
    double ram_pct = 0;
    char ram_txt[32];
    get_ram_usage(&ram_pct, ram_txt, sizeof(ram_txt));
    char full_ram_txt[64];
    snprintf(full_ram_txt, sizeof(full_ram_txt), "RAM %s", ram_txt);
    gtk_label_set_text(GTK_LABEL(data->ram_label), full_ram_txt);

    /* Update history */
    data->cpu_hist[data->hist_idx] = cpu_pct;
    data->ram_hist[data->hist_idx] = ram_pct;
    data->hist_idx = (data->hist_idx + 1) % HISTORY_LEN;

    /* Queue redraw for the chart */
    gtk_widget_queue_draw(data->chart_area);

    return G_SOURCE_CONTINUE;
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    SysMonitorData *data = (SysMonitorData *)user_data;
    if (data->timer_id > 0) {
        g_source_remove(data->timer_id);
    }
    g_free(data);
}

/* Construct the UI */
static GtkWidget* create_sys_monitor_widget(void) {
    SysMonitorData *data = g_new0(SysMonitorData, 1);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(main_box, 4);
    gtk_widget_set_margin_end(main_box, 4);

    /* --- Chart Area --- */
    data->chart_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->chart_area, 50, 24);
    gtk_widget_set_valign(data->chart_area, GTK_ALIGN_CENTER);
    g_signal_connect(data->chart_area, "draw", G_CALLBACK(on_draw_chart), data);
    gtk_box_pack_start(GTK_BOX(main_box), data->chart_area, FALSE, FALSE, 0);

    /* --- Labels Box --- */
    GtkWidget *labels_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(labels_box, GTK_ALIGN_CENTER);

    /* CPU Label */
    GtkWidget *cpu_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *cpu_lbl = gtk_label_new("CPU");
    gtk_label_set_markup(GTK_LABEL(cpu_lbl), "<span font_weight='bold' font_size='x-small' foreground='#80e538'>CPU</span>");
    data->cpu_label = gtk_label_new("0%");
    gtk_label_set_markup(GTK_LABEL(data->cpu_label), "<span font_size='x-small'>0%</span>");
    gtk_widget_set_size_request(data->cpu_label, 30, -1);
    gtk_label_set_xalign(GTK_LABEL(data->cpu_label), 1.0);
    
    gtk_box_pack_start(GTK_BOX(cpu_box), cpu_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cpu_box), data->cpu_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_box), cpu_box, FALSE, FALSE, 0);

    /* RAM Label */
    GtkWidget *ram_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    GtkWidget *ram_lbl = gtk_label_new("RAM");
    gtk_label_set_markup(GTK_LABEL(ram_lbl), "<span font_weight='bold' font_size='x-small' foreground='#3399ff'>RAM</span>");
    data->ram_label = gtk_label_new("0.0G");
    gtk_label_set_markup(GTK_LABEL(data->ram_label), "<span font_size='x-small'>0.0G</span>");
    gtk_widget_set_size_request(data->ram_label, 30, -1);
    gtk_label_set_xalign(GTK_LABEL(data->ram_label), 1.0);
    
    gtk_box_pack_start(GTK_BOX(ram_box), ram_lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ram_box), data->ram_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(labels_box), ram_box, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), labels_box, FALSE, FALSE, 0);

    /* Initialize with dummy fast reading */
    get_cpu_usage(data);
    update_sys_stats(data);

    /* Attach timer */
    data->timer_id = g_timeout_add(UPDATE_INTERVAL_MS, update_sys_stats, data);
    g_signal_connect(main_box, "destroy", G_CALLBACK(on_widget_destroy), data);

    gtk_widget_show_all(main_box);
    return main_box;
}

VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "System Monitor";
    api.description   = "Displays minimal CPU & RAM usage and Distro name.";
    api.author        = "Venom";
    api.expand        = FALSE;
    api.padding       = 6;
    api.create_widget = create_sys_monitor_widget;
    return &api;
}

VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size = sizeof(VenomPanelPluginAPIv2),
        .name = "System Monitor",
        .description = "Displays minimal CPU & RAM usage and Distro name.",
        .author = "Venom",
        .zone = VENOM_PLUGIN_ZONE_LEFT,
        .priority = 0,
        .expand = FALSE,
        .padding = 6,
        .create_widget = create_sys_monitor_widget,
        .destroy_widget = NULL,
    };
    return &api;
}

#include "battery_indicator.h"
#include "battery_backend.h"
#include "window_backend.h"
#include <gtk-layer-shell.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

static GtkWidget *battery_button;
static GtkWidget *battery_drawing_area;
static GtkWidget *battery_popup;
static GtkWidget *status_value_label;
static GtkWidget *charge_value_label;
static GtkWidget *size_value_label;
static GtkWidget *last_charge_value_label;
static GtkWidget *health_value_label;
static gboolean battery_popup_visible = FALSE;

static const char *battery_state_to_text(guint32 state) {
    switch (state) {
        case 1: return "Charging";
        case 2: return "Discharging";
        case 3: return "Empty";
        case 4: return "Full";
        case 5: return "Pending Charge";
        case 6: return "Pending Discharge";
        default: return "Unknown";
    }
}

static void format_energy(double energy_wh, char *out, size_t len) {
    if (energy_wh > 0.0) {
        g_snprintf(out, len, "%.1f Wh", energy_wh);
    } else {
        g_snprintf(out, len, "Unavailable");
    }
}

static void format_timestamp(guint64 timestamp, char *out, size_t len) {
    if (timestamp <= 0) {
        g_snprintf(out, len, "Unavailable");
        return;
    }

    time_t raw = (time_t) timestamp;
    struct tm *tm_info = localtime(&raw);
    if (!tm_info) {
        g_snprintf(out, len, "Unavailable");
        return;
    }

    strftime(out, len, "%Y-%m-%d %H:%M", tm_info);
}

static void update_battery_details_ui(const BatteryInfo *info) {
    char charge_text[64];
    char size_text[64];
    char health_text[64];
    char last_charge_text[64];
    double health_value = 0.0;

    if (status_value_label) {
        gtk_label_set_text(GTK_LABEL(status_value_label), battery_state_to_text(info->state));
    }

    g_snprintf(charge_text, sizeof(charge_text), "%.0f%%", info->percentage);
    if (charge_value_label) {
        gtk_label_set_text(GTK_LABEL(charge_value_label), charge_text);
    }

    format_energy(info->energy_full, size_text, sizeof(size_text));
    if (size_value_label) {
        gtk_label_set_text(GTK_LABEL(size_value_label), size_text);
    }

    if (info->capacity > 0.0) {
        health_value = info->capacity;
    } else if (info->energy_full > 0.0 && info->energy_full_design > 0.0) {
        health_value = (info->energy_full / info->energy_full_design) * 100.0;
    }

    if (health_value > 0.0) {
        g_snprintf(health_text, sizeof(health_text), "%.0f%%", health_value);
    } else {
        g_snprintf(health_text, sizeof(health_text), "Unsupported by UPower");
    }
    if (health_value_label) {
        gtk_label_set_text(GTK_LABEL(health_value_label), health_text);
    }

    format_timestamp(info->update_time, last_charge_text, sizeof(last_charge_text));
    if (last_charge_value_label) {
        gtk_label_set_text(GTK_LABEL(last_charge_value_label), last_charge_text);
    }

    if (battery_drawing_area) {
        gtk_widget_queue_draw(battery_drawing_area);
    }
}

static gboolean on_draw_battery(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    const BatteryInfo *info = battery_backend_get_info();
    
    guint width = gtk_widget_get_allocated_width(widget);
    guint height = gtk_widget_get_allocated_height(widget);
    double current_percentage = info->percentage;
    guint32 current_state = info->state;
    double bw = 22.0;
    double bh = 10.0;
    double bx = (width - bw - 2.0) / 2.0;
    double by = (height - bh) / 2.0;
    double radius = 2.0;
    double tw = 2.0;
    double th = 4.0;
    double tx = bx + bw;
    double ty = by + (bh - th) / 2.0;
    double gap = 1.0;
    double inner_x = bx + gap + 0.5;
    double inner_y = by + gap + 0.5;
    double inner_max_w = bw - (gap * 2) - 1.0;
    double inner_h = bh - (gap * 2) - 1.0;
    double fill_w = inner_max_w * (current_percentage / 100.0);
    gboolean charging = (current_state == 1 || current_state == 4 || current_state == 5);

    if (fill_w < 1.0 && current_percentage > 0.0) fill_w = 1.0;

    cairo_set_source_rgba(cr, 0.7, 0.7, 0.7, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, bx + bw - radius, by + radius, radius, -G_PI / 2, 0);
    cairo_arc(cr, bx + bw - radius, by + bh - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, bx + radius, by + bh - radius, radius, G_PI / 2, G_PI);
    cairo_arc(cr, bx + radius, by + radius, radius, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
    cairo_stroke(cr);

    cairo_rectangle(cr, tx, ty, tw, th);
    cairo_fill(cr);

    if (charging) {
        cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 1.0);
    } else if (current_percentage <= 20.0) {
        cairo_set_source_rgba(cr, 0.9, 0.2, 0.2, 1.0);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
    }

    cairo_rectangle(cr, inner_x, inner_y, fill_w, inner_h);
    cairo_fill(cr);

    if (charging) {
        cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.7);
        cairo_move_to(cr, inner_x + inner_max_w / 2 - 1, inner_y + 1);
        cairo_line_to(cr, inner_x + inner_max_w / 2 - 2, inner_y + inner_h / 2);
        cairo_line_to(cr, inner_x + inner_max_w / 2, inner_y + inner_h / 2);
        cairo_line_to(cr, inner_x + inner_max_w / 2 - 1, inner_y + inner_h - 1);
        cairo_stroke(cr);
    }

    return FALSE;
}

static GtkWidget *create_info_row(const char *title, GtkWidget **value_label) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *title_label = gtk_label_new(title);
    GtkWidget *value = gtk_label_new("...");

    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_label_set_xalign(GTK_LABEL(value), 1.0);
    gtk_widget_set_hexpand(value, TRUE);

    gtk_box_pack_start(GTK_BOX(row), title_label, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row), value, FALSE, FALSE, 0);

    if (value_label) *value_label = value;
    return row;
}

static gboolean on_battery_popup_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_hide(battery_popup);
        battery_popup_visible = FALSE;
        return TRUE;
    }

    return FALSE;
}

static void create_battery_popup_window(void) {
    GtkWidget *outer;
    GtkWidget *header;
    GtkWidget *header_icon;
    GtkWidget *title = gtk_label_new("Battery Info");
    GtkWidget *separator;
    GtkWidget *content;
    GdkScreen *screen;
    GdkVisual *visual;

    if (battery_popup) return;

    battery_popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(battery_popup, "battery-indicator-window");
    gtk_window_set_decorated(GTK_WINDOW(battery_popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(battery_popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(battery_popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(battery_popup), TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(battery_popup),
                                    "vaxpwy-battery",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
    panel_window_backend_set_anchor(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_TOP, 32);
    panel_window_backend_set_margin(GTK_WINDOW(battery_popup), GTK_LAYER_SHELL_EDGE_RIGHT, 0);

    screen = gtk_widget_get_screen(battery_popup);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(battery_popup, visual);
        gtk_widget_set_app_paintable(battery_popup, TRUE);
    }

    outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(outer, "battery-popup-outer");
    gtk_container_add(GTK_CONTAINER(battery_popup), outer);

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(header, "battery-popup-header");
    header_icon = gtk_image_new_from_icon_name("battery-symbolic", GTK_ICON_SIZE_MENU);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_box_pack_start(GTK_BOX(header), header_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), header, FALSE, FALSE, 0);

    separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(separator, "battery-popup-sep");
    gtk_box_pack_start(GTK_BOX(outer), separator, FALSE, FALSE, 0);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_name(content, "battery-popup-info");
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Status", &status_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Charge", &charge_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Battery Size", &size_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Last Charge", &last_charge_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), create_info_row("Battery Health", &health_value_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), content, TRUE, TRUE, 0);

    g_signal_connect(battery_popup, "key-press-event", G_CALLBACK(on_battery_popup_key_press), NULL);
    gtk_widget_show_all(outer);
    gtk_widget_hide(battery_popup);
}

static void on_battery_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    (void)user_data;

    const BatteryInfo *info = battery_backend_get_info();
    update_battery_details_ui(info);
    if (!battery_popup) return;

    if (battery_popup_visible) {
        gtk_widget_hide(battery_popup);
        battery_popup_visible = FALSE;
    } else {
        panel_window_backend_align_popup(GTK_WINDOW(battery_popup), battery_button, 300);
        gtk_widget_show_all(battery_popup);
        battery_popup_visible = TRUE;
    }
}

GtkWidget *get_battery_widget(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    battery_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(battery_button), GTK_RELIEF_NONE);
    gtk_widget_set_margin_start(battery_button, 8);

    battery_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(battery_drawing_area, 30, 16);
    gtk_widget_set_valign(battery_drawing_area, GTK_ALIGN_CENTER);
    g_signal_connect(battery_drawing_area, "draw", G_CALLBACK(on_draw_battery), NULL);

    gtk_container_add(GTK_CONTAINER(battery_button), battery_drawing_area);
    gtk_box_pack_start(GTK_BOX(box), battery_button, FALSE, FALSE, 0);

    create_battery_popup_window();
    g_signal_connect(battery_button, "clicked", G_CALLBACK(on_battery_button_clicked), NULL);

    battery_backend_init(update_battery_details_ui);

    return box;
}

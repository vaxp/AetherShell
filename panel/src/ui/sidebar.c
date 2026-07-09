/*
 * sidebar.c — sidebar popup content (Dumb UI)
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include "window_backend.h"
#include "sidebar_backend.h"

/* ═══════════════════════════════════════════════════════════════════
   UI GLOBALS
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget *lbl_time = NULL;
static GtkWidget *lbl_date = NULL;

static GtkWidget *cal_grid  = NULL;
static GtkWidget *lbl_month = NULL;
static GtkWidget *lbl_year  = NULL;

static GtkWidget *weather_icon_lbl = NULL;
static GtkWidget *weather_temp_lbl = NULL;
static GtkWidget *weather_city_lbl = NULL;
static GtkWidget *weather_forecast_grid = NULL;

static GtkCssProvider *bg_css = NULL;
static gboolean sidebar_css_loaded = FALSE;

#define SIDEBAR_POPUP_WIDTH 500
#define SIDEBAR_POPUP_PAD 8

/* ═══════════════════════════════════════════════════════════════════
   GLOBAL CSS
   ═══════════════════════════════════════════════════════════════════ */
static const char *SIDEBAR_CSS =
    "#sidebar { border-radius: 16px; padding: 0; background-color: transparent; }"
    "#clock_panel { padding: 16px; border-right: 1px solid rgba(255,255,255,0.06); }"
    "#lbl_time { color: #ffffff; font-weight: 700; font-size: 42px; letter-spacing: -1px; }"
    "#lbl_date { color: rgba(255,255,255,0.5); font-size: 13px; font-weight: 400; margin-top: 4px; }"
    "#cal_panel { padding: 14px; }"
    "#cal_nav_lbl { color: rgba(255,255,255,0.85); font-size: 12px; font-weight: 700; }"
    "#cal_btn { background: none; border: none; color: rgba(255,255,255,0.4); font-size: 11px; padding: 0 2px; min-width:0; min-height:0; }"
    "#cal_btn:hover { color: #cba6f7; }"
    "#cal_hdr  { color: rgba(255,255,255,0.3); font-size: 10px; font-weight: 700; }"
    "#cal_day  { color: rgba(255,255,255,0.6); font-size: 10px; }"
    "#cal_fade { color: rgba(255,255,255,0.2); font-size: 10px; }"
    "#cal_today { color: #ffffff; background-color: rgba(0, 0, 0, 0.49); border-radius: 10%; font-weight: 700; font-size: 10px; }"
    "#w_box { margin: 10px 14px; }"
    "#w_current_icon { font-size: 32px; }"
    "#w_current_temp { color: #ffffff; font-size: 24px; font-weight: 700; }"
    "#w_current_city { color: rgba(255,255,255,0.6); font-size: 13px; }"
    "#w_day_box { padding: 6px; background-color: rgba(0,0,0,0.2); border-radius: 8px; margin-top: 10px; }"
    "#w_day_name { color: rgba(255,255,255,0.5); font-size: 10px; font-weight: 700; }"
    "#w_day_icon { font-size: 16px; margin: 4px 0; }"
    "#w_day_temps { color: rgba(255,255,255,0.8); font-size: 10px; font-weight: 700; }"
    "";

/* ═══════════════════════════════════════════════════════════════════
   CALLBACKS
   ═══════════════════════════════════════════════════════════════════ */
static void cb_on_time_updated(const char *time_str, const char *date_str) {
    if (lbl_time) gtk_label_set_text(GTK_LABEL(lbl_time), time_str);
    if (lbl_date) gtk_label_set_text(GTK_LABEL(lbl_date), date_str);
}

static void cb_on_weather_current(const char *temp, const char *icon, const char *city) {
    if (weather_temp_lbl) gtk_label_set_text(GTK_LABEL(weather_temp_lbl), temp);
    if (weather_icon_lbl) gtk_label_set_text(GTK_LABEL(weather_icon_lbl), icon);
    if (weather_city_lbl) gtk_label_set_text(GTK_LABEL(weather_city_lbl), city);
}

static void cb_on_weather_forecast_clear(void) {
    if (!weather_forecast_grid) return;
    GList *ch = gtk_container_get_children(GTK_CONTAINER(weather_forecast_grid));
    for (GList *l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);
}

static void cb_on_weather_forecast_day(const char *day_name, const char *icon, const char *temps_str) {
    if (!weather_forecast_grid) return;
    
    GList *ch = gtk_container_get_children(GTK_CONTAINER(weather_forecast_grid));
    guint n_children = g_list_length(ch);
    g_list_free(ch);
    
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_name(vbox, "w_day_box");
    
    GtkWidget *lbl_day = gtk_label_new(day_name);
    gtk_widget_set_name(lbl_day, "w_day_name");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_day, FALSE, FALSE, 0);
    
    GtkWidget *lbl_ic = gtk_label_new(icon);
    gtk_widget_set_name(lbl_ic, "w_day_icon");
    gtk_box_pack_start(GTK_BOX(vbox), lbl_ic, FALSE, FALSE, 0);
    
    GtkWidget *lbl_mm = gtk_label_new(temps_str);
    gtk_widget_set_name(lbl_mm, "w_day_temps");
    gtk_label_set_justify(GTK_LABEL(lbl_mm), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), lbl_mm, FALSE, FALSE, 0);
    
    gtk_grid_attach(GTK_GRID(weather_forecast_grid), vbox, n_children, 0, 1, 1);
    gtk_widget_show_all(weather_forecast_grid);
}

static void cb_on_calendar_rebuild(int view_year, int view_month, int today_mday, int today_mon, int today_year, int dprev, int dcur, int sow) {
    if (!cal_grid) return;
    GList *ch = gtk_container_get_children(GTK_CONTAINER(cal_grid));
    for (GList *l = ch; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(ch);

    char buf[40];
    snprintf(buf, sizeof(buf), " %s ", sidebar_backend_get_month_name(view_month));
    gtk_label_set_text(GTK_LABEL(lbl_month), buf);
    snprintf(buf, sizeof(buf), " %d ", view_year);
    gtk_label_set_text(GTK_LABEL(lbl_year), buf);

    for (int c = 0; c < 7; c++) {
        GtkWidget *h = gtk_label_new(sidebar_backend_get_day_abbr(c));
        gtk_widget_set_name(h, "cal_hdr");
        gtk_widget_set_size_request(h, 28, 18);
        gtk_grid_attach(GTK_GRID(cal_grid), h, c, 0, 1, 1);
    }

    int col = sow, row = 1, day = 1, trail = 1;

    for (int i = sow-1; i >= 0; i--) {
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", dprev-i));
        gtk_widget_set_name(l, "cal_fade");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, sow-1-i, row, 1, 1);
    }
    
    while (day <= dcur) {
        gboolean today_flag = (day == today_mday); // Backend verified month & year already
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", day));
        gtk_widget_set_name(l, today_flag ? "cal_today" : "cal_day");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, col, row, 1, 1);
        col++; if (col==7){col=0; row++;} day++;
    }
    
    while (col > 0 && col < 7) {
        GtkWidget *l = gtk_label_new(g_strdup_printf("%d", trail++));
        gtk_widget_set_name(l, "cal_fade");
        gtk_widget_set_size_request(l, 28, 20);
        gtk_label_set_xalign(GTK_LABEL(l), 0.5);
        gtk_grid_attach(GTK_GRID(cal_grid), l, col++, row, 1, 1);
    }
    gtk_widget_show_all(cal_grid);
}

/* ═══════════════════════════════════════════════════════════════════
   UI ACTIONS
   ═══════════════════════════════════════════════════════════════════ */
static void on_prev_month(GtkWidget *w, gpointer d) { (void)w; (void)d; sidebar_backend_cal_prev_month(); }
static void on_next_month(GtkWidget *w, gpointer d) { (void)w; (void)d; sidebar_backend_cal_next_month(); }
static void on_prev_year (GtkWidget *w, gpointer d) { (void)w; (void)d; sidebar_backend_cal_prev_year(); }
static void on_next_year (GtkWidget *w, gpointer d) { (void)w; (void)d; sidebar_backend_cal_next_year(); }

static void set_theme(const char *hex_color, double opacity) {
    (void)hex_color; (void)opacity;
    if (!bg_css) return;
    gtk_css_provider_load_from_data(bg_css, "#sidebar { background-color: transparent; }", -1, NULL);
}

/* ═══════════════════════════════════════════════════════════════════
   CONTENT FACTORY
   ═══════════════════════════════════════════════════════════════════ */
static GtkWidget* create_sidebar_content(void) {
    if (!sidebar_css_loaded) {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css, SIDEBAR_CSS, -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
        sidebar_css_loaded = TRUE;
    }

    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(card, "sidebar");
    gtk_widget_set_size_request(card, SIDEBAR_POPUP_WIDTH, -1);

    GtkStyleContext *context = gtk_widget_get_style_context(card);
    bg_css = gtk_css_provider_new();
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(bg_css), 801);
    set_theme("#0000003f", 1.0);

    GtkWidget *top_row = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(top_row), TRUE);
    gtk_box_pack_start(GTK_BOX(card), top_row, FALSE, FALSE, 0);

    GtkWidget *clock_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_name(clock_panel, "clock_panel");
    gtk_widget_set_valign(clock_panel, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(clock_panel, GTK_ALIGN_FILL);

    lbl_time = gtk_label_new("00:00");
    gtk_widget_set_name(lbl_time, "lbl_time");
    gtk_label_set_xalign(GTK_LABEL(lbl_time), 0.5);
    gtk_box_pack_start(GTK_BOX(clock_panel), lbl_time, FALSE, FALSE, 0);

    lbl_date = gtk_label_new("");
    gtk_widget_set_name(lbl_date, "lbl_date");
    gtk_label_set_xalign(GTK_LABEL(lbl_date), 0.5);
    gtk_box_pack_start(GTK_BOX(clock_panel), lbl_date, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(top_row), clock_panel, 0, 0, 1, 1);

    GtkWidget *cal_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_name(cal_panel, "cal_panel");

    GtkWidget *nav_m = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(nav_m, GTK_ALIGN_CENTER);
    GtkWidget *bpm = gtk_button_new_with_label("<"); gtk_widget_set_name(bpm, "cal_btn");
    g_signal_connect(bpm, "clicked", G_CALLBACK(on_prev_month), NULL);
    lbl_month = gtk_label_new(""); gtk_widget_set_name(lbl_month, "cal_nav_lbl");
    GtkWidget *bnm = gtk_button_new_with_label(">"); gtk_widget_set_name(bnm, "cal_btn");
    g_signal_connect(bnm, "clicked", G_CALLBACK(on_next_month), NULL);
    gtk_box_pack_start(GTK_BOX(nav_m), bpm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_m), lbl_month, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(nav_m), bnm, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cal_panel), nav_m, FALSE, FALSE, 0);

    GtkWidget *nav_y = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_set_halign(nav_y, GTK_ALIGN_CENTER);
    GtkWidget *bpy = gtk_button_new_with_label("<"); gtk_widget_set_name(bpy, "cal_btn");
    g_signal_connect(bpy, "clicked", G_CALLBACK(on_prev_year), NULL);
    lbl_year = gtk_label_new(""); gtk_widget_set_name(lbl_year, "cal_nav_lbl");
    GtkWidget *bny = gtk_button_new_with_label(">"); gtk_widget_set_name(bny, "cal_btn");
    g_signal_connect(bny, "clicked", G_CALLBACK(on_next_year), NULL);
    gtk_box_pack_start(GTK_BOX(nav_y), bpy, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(nav_y), lbl_year, FALSE, FALSE, 4);
    gtk_box_pack_start(GTK_BOX(nav_y), bny, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cal_panel), nav_y, FALSE, FALSE, 0);

    cal_grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(cal_grid), TRUE);
    gtk_grid_set_row_spacing(GTK_GRID(cal_grid), 1);
    gtk_widget_set_halign(cal_grid, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(cal_panel), cal_grid, FALSE, FALSE, 4);

    gtk_grid_attach(GTK_GRID(top_row), cal_panel, 1, 0, 1, 1);

    GtkWidget *w_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(w_box, "w_box");
    gtk_box_pack_start(GTK_BOX(card), w_box, FALSE, FALSE, 0);

    GtkWidget *w_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(w_box), w_top, FALSE, FALSE, 0);
    
    weather_icon_lbl = gtk_label_new("⏳");
    gtk_widget_set_name(weather_icon_lbl, "w_current_icon");
    gtk_box_pack_start(GTK_BOX(w_top), weather_icon_lbl, FALSE, FALSE, 0);
    
    GtkWidget *w_top_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(w_top_right, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(w_top), w_top_right, TRUE, TRUE, 0);
    
    weather_temp_lbl = gtk_label_new("--°");
    gtk_widget_set_name(weather_temp_lbl, "w_current_temp");
    gtk_label_set_xalign(GTK_LABEL(weather_temp_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(w_top_right), weather_temp_lbl, FALSE, FALSE, 0);
    
    weather_city_lbl = gtk_label_new("Loading...");
    gtk_widget_set_name(weather_city_lbl, "w_current_city");
    gtk_label_set_xalign(GTK_LABEL(weather_city_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(w_top_right), weather_city_lbl, FALSE, FALSE, 0);

    weather_forecast_grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(weather_forecast_grid), TRUE);
    gtk_grid_set_column_spacing(GTK_GRID(weather_forecast_grid), 4);
    gtk_box_pack_start(GTK_BOX(w_box), weather_forecast_grid, FALSE, FALSE, 0);

    SidebarCallbacks cb = {
        .on_time_updated = cb_on_time_updated,
        .on_calendar_rebuild = cb_on_calendar_rebuild,
        .on_weather_current = cb_on_weather_current,
        .on_weather_forecast_clear = cb_on_weather_forecast_clear,
        .on_weather_forecast_day = cb_on_weather_forecast_day
    };
    sidebar_backend_init(&cb);
    sidebar_backend_refresh_all();

    gtk_widget_show_all(card);
    return card;
}

/* ═══════════════════════════════════════════════════════════════════
   POPUP WINDOW
   ═══════════════════════════════════════════════════════════════════ */
static void ensure_rgba_visual(GtkWidget *widget) {
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (!screen) return;
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(widget, visual);
        gtk_widget_set_app_paintable(widget, TRUE);
    }
}

static gboolean draw_sidebar_popup_clear(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)widget; (void)user_data;
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);
    return FALSE;
}

static gboolean draw_sidebar_popup_background(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    GtkAllocation alloc;
    const double radius = 16.0;
    (void)user_data;
    gtk_widget_get_allocation(widget, &alloc);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.392);
    cairo_new_sub_path(cr);
    cairo_arc(cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_arc(cr, alloc.width - radius, radius, radius, 3.0 * G_PI / 2.0, 0.0);
    cairo_arc(cr, alloc.width - radius, alloc.height - radius, radius, 0.0, G_PI / 2.0);
    cairo_arc(cr, radius, alloc.height - radius, radius, G_PI / 2.0, G_PI);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);

    cairo_set_source_rgba(cr, 0.133, 0.133, 0.133, 1.0);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    return FALSE;
}

GtkWidget *init_sidebar_popup(void) {
    GtkWidget *popup = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkWidget *surface = gtk_event_box_new();
    GtkWidget *content;

    gtk_widget_set_name(popup, "sidebar-popup");
    gtk_window_set_decorated(GTK_WINDOW(popup), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(popup), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_POPUP_MENU);
    gtk_widget_set_app_paintable(popup, TRUE);

    panel_window_backend_init_popup(GTK_WINDOW(popup),
                                    "aether-sidebar-popup",
                                    GDK_WINDOW_TYPE_HINT_POPUP_MENU,
                                    GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
    panel_window_backend_set_anchor(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_TOP, 0);
    panel_window_backend_set_margin(GTK_WINDOW(popup), GTK_LAYER_SHELL_EDGE_LEFT, 8);
    
    ensure_rgba_visual(popup);
    g_signal_connect(popup, "draw", G_CALLBACK(draw_sidebar_popup_clear), NULL);
    
    ensure_rgba_visual(surface);
    gtk_widget_set_name(surface, "sidebar-popup-surface");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(surface), TRUE);
    g_signal_connect(surface, "draw", G_CALLBACK(draw_sidebar_popup_background), NULL);

    content = create_sidebar_content();
    gtk_container_add(GTK_CONTAINER(surface), content);
    gtk_container_add(GTK_CONTAINER(popup), surface);

    sidebar_backend_start_timers();

    gtk_widget_show_all(surface);
    gtk_widget_hide(popup);
    return popup;
}

void sidebar_popup_set_relative_to(GtkWidget *popup, GtkWidget *relative_to) {
    if (!popup) return;
    g_object_set_data(G_OBJECT(popup), "sidebar-relative-to", relative_to);
    panel_window_backend_align_popup(GTK_WINDOW(popup), relative_to, SIDEBAR_POPUP_WIDTH);
}

void sidebar_popup_toggle(GtkWidget *popup, GtkWidget *relative_to) {
    GtkWidget *anchor = relative_to;
    if (!popup) return;
    if (!anchor) anchor = g_object_get_data(G_OBJECT(popup), "sidebar-relative-to");
    if (anchor) sidebar_popup_set_relative_to(popup, anchor);

    if (gtk_widget_get_visible(popup)) {
        sidebar_backend_stop_timers();
        gtk_widget_hide(popup);
    } else {
        gtk_widget_show_all(popup);
        sidebar_backend_start_timers();
        sidebar_backend_refresh_all();
    }
}

/*
 * notification-center.c
 *
 * Redesigned to match exim-panel notification panel:
 *   - Header: chevron icon + "Notifications" title + DND GtkSwitch
 *   - Empty state: large icon + label centred
 *   - History cards: app icon (32px) + app-name/summary/body + delete button
 *   - "Clear History" button with icon appears at top of list when there are items
 *   - Cairo-drawn rounded background (rgba 0,0,0,0.30) with subtle border
 */

#include <gtk/gtk.h>
#include <math.h>

#include "notification-center.h"
#include "notification-client.h"
#include "panel-geometry.h"
#include "window-backend.h"

/* ─── State ──────────────────────────────────────────────────────────────── */

typedef struct {
    GtkWidget *window;
    GtkWidget *notifications_box;  /* VBox inside scrolled — rebuilt on update */
    GtkWidget *sw_dnd;
    guint      refresh_idle_id;
} NotificationCenter;

static GtkWidget *g_nc_window = NULL;   /* weak ref */

/* ─── Cairo background ───────────────────────────────────────────────────── */

static gboolean draw_nc_background(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void)user_data;

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    const double R = 14.0;          /* corner radius */
    double w = alloc.width;
    double h = alloc.height;

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.30);

    /* Rounded rectangle */
    cairo_new_sub_path(cr);
    cairo_arc(cr, R,     R,     R, G_PI,       3.0*G_PI/2.0);
    cairo_arc(cr, w-R,   R,     R, 3.0*G_PI/2.0, 0.0);
    cairo_arc(cr, w-R,   h-R,   R, 0.0,        G_PI/2.0);
    cairo_arc(cr, R,     h-R,   R, G_PI/2.0,   G_PI);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);

    /* Thin border */
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.08);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    return FALSE;
}

/* ─── Helpers ────────────────────────────────────────────────────────────── */

static void ensure_rgba(GtkWidget *widget)
{
    GdkScreen *screen = gtk_widget_get_screen(widget);
    if (!screen) return;
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(widget, visual);
        gtk_widget_set_app_paintable(widget, TRUE);
    }
}

/* ─── X11 positioning (unchanged from original) ──────────────────────────── */

static void nc_position_window(GtkWidget *window)
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
    gdk_monitor_get_workarea(monitor, &workarea);

    int win_w = gtk_widget_get_allocated_width(window);
    int win_h = gtk_widget_get_allocated_height(window);
    if (win_w <= 1 || win_h <= 1) gtk_window_get_size(GTK_WINDOW(window), &win_w, &win_h);
    if (win_w <= 1) win_w = 360;
    if (win_h <= 1) win_h = 500;

    const int margin     = 16;
    const int panel_gap  = 2;
    const int panel_h    = 25;
    int x = workarea.x + workarea.width - win_w - margin;
    int y = workarea.y + margin + panel_h + panel_gap;
    if (panel_geometry_get_config_edge() == PANEL_EDGE_BOTTOM)
        y = workarea.y + workarea.height - win_h - margin - panel_h - panel_gap;

    x = CLAMP(x, workarea.x, workarea.x + workarea.width  - win_w);
    y = CLAMP(y, workarea.y, workarea.y + workarea.height - win_h);

    gtk_window_move(GTK_WINDOW(window), x, y);
}

static gboolean nc_deferred_position(gpointer data)
{
    gtk_widget_queue_draw(GTK_WIDGET(data));
    nc_position_window(GTK_WIDGET(data));
    g_object_unref(data);
    return G_SOURCE_REMOVE;
}
static void nc_on_realize(GtkWidget *w, gpointer d) { (void)d; g_idle_add(nc_deferred_position, g_object_ref(w)); }
static void nc_on_show  (GtkWidget *w, gpointer d) { (void)d; g_idle_add(nc_deferred_position, g_object_ref(w)); }

/* ─── Notification list builder ──────────────────────────────────────────── */

static void on_delete_clicked(GtkButton *btn, gpointer user_data)
{
    guint32 id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(btn), "nc-id"));
    if (id) notification_client_remove(id);
    /* The history-update callback will rebuild the list */
}

static void on_clear_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn; (void)user_data;
    notification_client_clear_history();
}

static void rebuild_notifications(NotificationCenter *nc)
{
    if (!nc || !nc->notifications_box) return;

    /* Remove all children */
    GList *children = gtk_container_get_children(GTK_CONTAINER(nc->notifications_box));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    GList *history = notification_client_get_history();

    if (!history) {
        /* Empty state — centred icon + label */
        GtkWidget *empty_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_widget_set_valign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(empty_box, GTK_ALIGN_CENTER);
        gtk_widget_set_vexpand(empty_box, TRUE);
        gtk_widget_set_hexpand(empty_box, TRUE);

        GtkWidget *icon = gtk_image_new_from_icon_name(
            "notifications-disabled-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
        gtk_style_context_add_class(gtk_widget_get_style_context(icon), "notification-icon");

        GtkWidget *lbl = gtk_label_new("No Notifications");
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "notification-label");

        gtk_box_pack_start(GTK_BOX(empty_box), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(empty_box), lbl,  FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(nc->notifications_box), empty_box, TRUE, TRUE, 0);

    } else {
        /* "Clear History" button row */
        GtkWidget *clear_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_widget_set_halign(clear_row, GTK_ALIGN_END);
        gtk_widget_set_margin_bottom(clear_row, 4);
        gtk_widget_set_margin_end(clear_row, 4);

        GtkWidget *clear_btn = gtk_button_new();
        gtk_style_context_add_class(gtk_widget_get_style_context(clear_btn), "clear-btn");
        g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_clicked), NULL);

        GtkWidget *clear_inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        GtkWidget *ci = gtk_image_new_from_icon_name("edit-delete-symbolic", GTK_ICON_SIZE_MENU);
        gtk_style_context_add_class(gtk_widget_get_style_context(ci), "clear-btn-icon");
        GtkWidget *cl = gtk_label_new("Clear History");
        gtk_style_context_add_class(gtk_widget_get_style_context(cl), "clear-btn-label");
        gtk_box_pack_start(GTK_BOX(clear_inner), ci, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(clear_inner), cl, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(clear_btn), clear_inner);
        gtk_box_pack_start(GTK_BOX(clear_row), clear_btn, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(nc->notifications_box), clear_row, FALSE, FALSE, 0);

        /* One card per notification */
        for (GList *l = history; l; l = l->next) {
            NotificationItem *item = (NotificationItem *)l->data;
            if (!item) continue;

            const char *app_name = item->app_name ? item->app_name : "App";
            const char *summary  = item->summary  ? item->summary  : "";
            const char *body     = item->body      ? item->body     : "";

            /* Outer card */
            GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            gtk_style_context_add_class(gtk_widget_get_style_context(card), "card");
            gtk_widget_set_hexpand(card, TRUE);
            gtk_widget_set_margin_top(card, 2);
            gtk_widget_set_margin_bottom(card, 2);

            /* App icon */
            GtkWidget *icon = gtk_image_new_from_icon_name(
                "preferences-system-details", GTK_ICON_SIZE_DND);
            gtk_image_set_pixel_size(GTK_IMAGE(icon), 32);
            gtk_widget_set_valign(icon, GTK_ALIGN_START);

            /* Text column */
            GtkWidget *text_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_set_hexpand(text_col, TRUE);

            GtkWidget *app_lbl = gtk_label_new(app_name);
            gtk_widget_set_halign(app_lbl, GTK_ALIGN_START);
            gtk_label_set_ellipsize(GTK_LABEL(app_lbl), PANGO_ELLIPSIZE_END);
            gtk_style_context_add_class(gtk_widget_get_style_context(app_lbl), "notification-app");

            GtkWidget *sum_lbl = gtk_label_new(NULL);
            gtk_widget_set_halign(sum_lbl, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(sum_lbl), TRUE);
            gtk_label_set_lines(GTK_LABEL(sum_lbl), 2);
            gtk_label_set_ellipsize(GTK_LABEL(sum_lbl), PANGO_ELLIPSIZE_END);
            gtk_style_context_add_class(gtk_widget_get_style_context(sum_lbl), "notification-summary");
            gchar *markup = g_markup_printf_escaped("<b>%s</b>", summary);
            gtk_label_set_markup(GTK_LABEL(sum_lbl), markup);
            g_free(markup);

            GtkWidget *body_lbl = gtk_label_new(body);
            gtk_widget_set_halign(body_lbl, GTK_ALIGN_START);
            gtk_label_set_line_wrap(GTK_LABEL(body_lbl), TRUE);
            gtk_label_set_lines(GTK_LABEL(body_lbl), 2);
            gtk_label_set_ellipsize(GTK_LABEL(body_lbl), PANGO_ELLIPSIZE_END);
            gtk_style_context_add_class(gtk_widget_get_style_context(body_lbl), "notification-body");

            gtk_box_pack_start(GTK_BOX(text_col), app_lbl,  FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(text_col), sum_lbl,  FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(text_col), body_lbl, FALSE, FALSE, 0);

            /* Delete button */
            GtkWidget *del_btn = gtk_button_new_from_icon_name(
                "window-close-symbolic", GTK_ICON_SIZE_MENU);
            gtk_button_set_relief(GTK_BUTTON(del_btn), GTK_RELIEF_NONE);
            gtk_widget_set_can_focus(del_btn, FALSE);
            gtk_widget_set_valign(del_btn, GTK_ALIGN_START);
            gtk_style_context_add_class(gtk_widget_get_style_context(del_btn), "nc-close");
            g_object_set_data(G_OBJECT(del_btn), "nc-id", GUINT_TO_POINTER(item->id));
            g_signal_connect(del_btn, "clicked", G_CALLBACK(on_delete_clicked), NULL);

            gtk_box_pack_start(GTK_BOX(card), icon,    FALSE, FALSE, 0);
            gtk_box_pack_start(GTK_BOX(card), text_col, TRUE, TRUE,  0);
            gtk_box_pack_start(GTK_BOX(card), del_btn, FALSE, FALSE, 0);

            gtk_box_pack_start(GTK_BOX(nc->notifications_box), card, FALSE, FALSE, 0);
        }
        notification_item_list_free(history);
    }

    gtk_widget_show_all(nc->notifications_box);
}

/* ─── Callbacks ──────────────────────────────────────────────────────────── */

static gboolean rebuild_idle(gpointer data)
{
    NotificationCenter *nc = data;
    if (!nc) return G_SOURCE_REMOVE;
    nc->refresh_idle_id = 0;
    rebuild_notifications(nc);
    return G_SOURCE_REMOVE;
}

static void request_rebuild(NotificationCenter *nc)
{
    if (!nc || nc->refresh_idle_id) return;
    nc->refresh_idle_id = g_idle_add(rebuild_idle, nc);
}

static void on_history_update(gpointer user_data)
{
    request_rebuild((NotificationCenter *)user_data);
}

static void on_dnd_changed(gboolean enabled, gpointer user_data)
{
    NotificationCenter *nc = user_data;
    if (!nc || !nc->sw_dnd) return;
    if (gtk_switch_get_active(GTK_SWITCH(nc->sw_dnd)) != enabled)
        gtk_switch_set_active(GTK_SWITCH(nc->sw_dnd), enabled);
}

static gboolean on_dnd_switch_state_set(GtkSwitch *sw, gboolean state, gpointer user_data)
{
    (void)sw; (void)user_data;
    notification_client_set_dnd(state);
    return FALSE;
}

static void nc_on_destroy(GtkWidget *widget, gpointer user_data)
{
    (void)widget;
    NotificationCenter *nc = user_data;
    if (!nc) return;
    if (nc->refresh_idle_id) {
        g_source_remove(nc->refresh_idle_id);
        nc->refresh_idle_id = 0;
    }
    notification_client_on_history_update(NULL, nc);
    notification_client_on_dnd_change(NULL, nc);
    notification_client_cleanup();
    g_nc_window = NULL;
}

/* ─── Public factory ─────────────────────────────────────────────────────── */

GtkWidget *create_notification_center(void)
{
    notification_client_init();

    NotificationCenter *nc = g_new0(NotificationCenter, 1);

    /* ── Window ── */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    nc->window  = window;
    g_nc_window = window;

    ensure_rgba(window);
    gtk_window_set_title(GTK_WINDOW(window), "Notifications");
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 520);

    panel_window_backend_init_popup(GTK_WINDOW(window),
                                   "venom-notification-center",
                                   GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU,
                                   GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    if (panel_window_backend_is_wayland()) {
        panel_window_backend_anchor_popup_to_panel(
            GTK_WINDOW(window),
            GTK_LAYER_SHELL_EDGE_RIGHT, 8);
    } else {
        g_signal_connect(window, "realize", G_CALLBACK(nc_on_realize), NULL);
        g_signal_connect(window, "show",    G_CALLBACK(nc_on_show),    NULL);
    }

    g_signal_connect(window, "destroy", G_CALLBACK(nc_on_destroy), nc);
    g_object_set_data_full(G_OBJECT(window), "notification-center", nc, g_free);

    /* ── Panel surface (EventBox with Cairo background) ── */
    GtkWidget *surface = gtk_event_box_new();
    gtk_widget_set_name(surface, "main-box");
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(surface), TRUE);
    ensure_rgba(surface);
    g_signal_connect(surface, "draw", G_CALLBACK(draw_nc_background), NULL);
    gtk_container_add(GTK_CONTAINER(window), surface);

    /* ── Main content box (360 × 480) ── */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(main_box, 360, 480);
    gtk_widget_set_margin_top   (main_box, 8);
    gtk_widget_set_margin_bottom(main_box, 8);
    gtk_widget_set_margin_start (main_box, 8);
    gtk_widget_set_margin_end   (main_box, 8);
    gtk_container_add(GTK_CONTAINER(surface), main_box);

    /* ── Header ── */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "header-box");

    /* Left: arrow + title */
    GtkWidget *left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU);
    GtkWidget *title = gtk_label_new("Notifications");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "app-title");
    gtk_box_pack_start(GTK_BOX(left), arrow, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);

    /* Right: moon icon + DND switch */
    GtkWidget *right = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(right, GTK_ALIGN_END);

    GtkWidget *moon = gtk_image_new_from_icon_name("weather-clear-night-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_style_context_add_class(gtk_widget_get_style_context(moon), "perf-icon");

    GtkWidget *sw_dnd = gtk_switch_new();
    nc->sw_dnd = sw_dnd;
    gtk_widget_set_valign(sw_dnd, GTK_ALIGN_CENTER);
    gtk_switch_set_active(GTK_SWITCH(sw_dnd), notification_client_get_dnd());
    g_signal_connect(sw_dnd, "state-set", G_CALLBACK(on_dnd_switch_state_set), nc);

    gtk_box_pack_start(GTK_BOX(right), moon,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), sw_dnd, FALSE, FALSE, 0);

    /* Assemble header */
    GtkWidget *spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(header), left,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), spacer, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(header), right,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);

    /* ── Scrolled list ── */
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled), GTK_SHADOW_NONE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled, TRUE, TRUE, 0);

    GtkWidget *notif_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top   (notif_box, 12);
    gtk_widget_set_margin_bottom(notif_box, 12);
    gtk_widget_set_valign(notif_box, GTK_ALIGN_START);
    gtk_widget_set_halign(notif_box, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(scrolled), notif_box);
    nc->notifications_box = notif_box;

    /* ── Wire callbacks ── */
    notification_client_on_history_update(on_history_update, nc);
    notification_client_on_dnd_change(on_dnd_changed, nc);

    rebuild_notifications(nc);
    gtk_widget_show_all(main_box);
    gtk_widget_hide(window);

    return window;
}

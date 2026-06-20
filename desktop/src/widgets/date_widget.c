#include <gtk/gtk.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../../include/vaxp-widget-api.h"

typedef struct {
    GtkWidget *root_eb;
    GtkWidget *day_area;
    
    gboolean  dragging;
    gint      drag_rx, drag_ry;
    gint      drag_wx, drag_wy;
    
    char      day_str[64];
    guint     timer_id;
    vaxpDesktopAPI *api;
} WidgetState;

static WidgetState S;

/* ══════════════════════════════════════════════
   Drag & Drop
   ══════════════════════════════════════════════ */
static gboolean on_card_press(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    if (ev->button != 1) return FALSE;
    S.dragging = TRUE;
    S.drag_rx  = ev->x_root; S.drag_ry = ev->y_root;
    gint wx, wy;
    gtk_widget_translate_coordinates(w, gtk_widget_get_toplevel(w), 0, 0, &wx, &wy);
    S.drag_wx = wx; S.drag_wy = wy;
    return TRUE;
}

static gboolean on_card_motion(GtkWidget *w, GdkEventMotion *ev, gpointer d) {
    if (!S.dragging || !S.api || !S.api->layout_container) return FALSE;
    GtkWidget *target = w;
    while (target && gtk_widget_get_parent(target) != S.api->layout_container) {
        target = gtk_widget_get_parent(target);
    }
    if (target) {
        gtk_layout_move(GTK_LAYOUT(S.api->layout_container), target,
            S.drag_wx + (int)(ev->x_root - S.drag_rx),
            S.drag_wy + (int)(ev->y_root - S.drag_ry));
    }
    return TRUE;
}

static gboolean on_card_release(GtkWidget *w, GdkEventButton *ev, gpointer d) {
    if (ev->button != 1 || !S.dragging) return FALSE;
    S.dragging = FALSE;
    if (S.api && S.api->save_position && S.api->layout_container) {
        gint x, y;
        gtk_widget_translate_coordinates(w, gtk_widget_get_toplevel(w), 0, 0, &x, &y);
        S.api->save_position("date_widget.so", x, y);
    }
    return TRUE;
}

/* ══════════════════════════════════════════════
   Manual Geometric Letter Drawing
   ══════════════════════════════════════════════ */
static void draw_letter(cairo_t *cr, char c) {
    cairo_set_line_width(cr, 6.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    
    switch (c) {
        case 'A':
            cairo_move_to(cr, 0, 50); cairo_line_to(cr, 14, 16); cairo_stroke(cr);
            cairo_move_to(cr, 40, 50); cairo_line_to(cr, 26, 16); cairo_stroke(cr);
            cairo_move_to(cr, 10, 35); cairo_line_to(cr, 30, 35); cairo_stroke(cr);
            break;
        case 'D':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 0); cairo_line_to(cr, 16, 0);
            cairo_arc(cr, 16, 25, 25, -G_PI/2, G_PI/2);
            cairo_line_to(cr, 12, 50); cairo_stroke(cr);
            break;
        case 'E':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 0); cairo_line_to(cr, 40, 0); cairo_stroke(cr);
            cairo_move_to(cr, 12, 25); cairo_line_to(cr, 30, 25); cairo_stroke(cr);
            cairo_move_to(cr, 12, 50); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            break;
        case 'F':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 0); cairo_line_to(cr, 40, 0); cairo_stroke(cr);
            cairo_move_to(cr, 12, 25); cairo_line_to(cr, 30, 25); cairo_stroke(cr);
            break;
        case 'H':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 25); cairo_line_to(cr, 28, 25); cairo_stroke(cr);
            break;
        case 'I':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 40, 0); cairo_stroke(cr);
            cairo_move_to(cr, 0, 50); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            cairo_move_to(cr, 20, 12); cairo_line_to(cr, 20, 38); cairo_stroke(cr);
            break;
        case 'M':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 12); cairo_line_to(cr, 20, 30); cairo_line_to(cr, 28, 12); cairo_stroke(cr);
            break;
        case 'N':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 12); cairo_line_to(cr, 28, 38); cairo_stroke(cr);
            break;
        case 'O':
            cairo_arc(cr, 20, 25, 20, G_PI/2 + 0.3, 3*G_PI/2 - 0.3); cairo_stroke(cr);
            cairo_arc(cr, 20, 25, 20, -G_PI/2 + 0.3, G_PI/2 - 0.3); cairo_stroke(cr);
            break;
        case 'R':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 0); cairo_line_to(cr, 25, 0);
            cairo_arc(cr, 25, 12.5, 12.5, -G_PI/2, G_PI/2);
            cairo_line_to(cr, 12, 25); cairo_stroke(cr);
            cairo_move_to(cr, 14, 33); cairo_line_to(cr, 30, 50); cairo_stroke(cr);
            break;
        case 'S':
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 10, 0); cairo_line_to(cr, 0, 10); cairo_line_to(cr, 0, 20); cairo_line_to(cr, 16, 20); cairo_stroke(cr);
            cairo_move_to(cr, 24, 30); cairo_line_to(cr, 40, 30); cairo_line_to(cr, 40, 40); cairo_line_to(cr, 30, 50); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            break;
        case 'T':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 40, 0); cairo_stroke(cr);
            cairo_move_to(cr, 20, 12); cairo_line_to(cr, 20, 50); cairo_stroke(cr);
            break;
        case 'U':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 30); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 40, 30); cairo_stroke(cr);
            cairo_arc(cr, 20, 30, 20, 0.4, G_PI - 0.4); cairo_stroke(cr);
            break;
        case 'W':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 0, 50); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 40, 50); cairo_stroke(cr);
            cairo_move_to(cr, 12, 38); cairo_line_to(cr, 20, 20); cairo_line_to(cr, 28, 38); cairo_stroke(cr);
            break;
        case 'Y':
            cairo_move_to(cr, 0, 0); cairo_line_to(cr, 14, 16); cairo_stroke(cr);
            cairo_move_to(cr, 40, 0); cairo_line_to(cr, 26, 16); cairo_stroke(cr);
            cairo_move_to(cr, 20, 28); cairo_line_to(cr, 20, 50); cairo_stroke(cr);
            break;
        default:
            break;
    }
}

static gboolean on_draw_day(GtkWidget *w, cairo_t *cr, gpointer d) {
    int width = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);

    char *day = S.day_str;
    int len = strlen(day);
    if (len == 0) return FALSE;

    double intrinsic_w = len * 55.0 - 15.0; 
    double intrinsic_h = 50.0;

    double scale_x = width / (intrinsic_w + 40.0);
    double scale_y = height / (intrinsic_h + 20.0);
    double scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 2.5) scale = 2.5;

    double total_w = intrinsic_w * scale;
    double total_h = intrinsic_h * scale;

    double start_x = (width - total_w) / 2.0;
    double start_y = (height - total_h) / 2.0;

    cairo_save(cr);
    cairo_translate(cr, start_x, start_y);
    cairo_scale(cr, scale, scale);

    // Drop shadow
    cairo_save(cr);
    cairo_translate(cr, 3.0/scale, 3.0/scale);
    cairo_set_source_rgba(cr, 0, 0, 0, 0.4);
    for (int i = 0; i < len; i++) {
        cairo_save(cr);
        cairo_translate(cr, i * 55.0, 0);
        draw_letter(cr, day[i]);
        cairo_restore(cr);
    }
    cairo_restore(cr);

    // Main color
    cairo_set_source_rgba(cr, 0.9, 0.92, 0.96, 1.0);
    for (int i = 0; i < len; i++) {
        cairo_save(cr);
        cairo_translate(cr, i * 55.0, 0);
        draw_letter(cr, day[i]);
        cairo_restore(cr);
    }
    
    cairo_restore(cr);
    return FALSE;
}

/* ══════════════════════════════════════════════
   Date Polling
   ══════════════════════════════════════════════ */
static gboolean poll_date(gpointer data) {
    time_t rawtime;
    struct tm *info;

    time(&rawtime);
    info = localtime(&rawtime);

    // Update Day (for Cairo)
    strftime(S.day_str, sizeof(S.day_str), "%A", info);
    for (int i = 0; S.day_str[i]; i++) S.day_str[i] = g_ascii_toupper(S.day_str[i]);

    if (S.day_area) gtk_widget_queue_draw(S.day_area);

    return TRUE;
}

/* ══════════════════════════════════════════════
   Widget Construction
   ══════════════════════════════════════════════ */
static GtkWidget *create_widget(vaxpDesktopAPI *api) {
    memset(&S, 0, sizeof(S));
    S.api = api;

    /* Custom Drawn Day */
    S.day_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(S.day_area, 450, 120);
    
    /* Attach events directly to the drawing area so the text itself is draggable */
    gtk_widget_add_events(S.day_area, 
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    
    g_signal_connect(S.day_area, "button-press-event",   G_CALLBACK(on_card_press),   NULL);
    g_signal_connect(S.day_area, "motion-notify-event",  G_CALLBACK(on_card_motion),  NULL);
    g_signal_connect(S.day_area, "button-release-event", G_CALLBACK(on_card_release), NULL);
    g_signal_connect(S.day_area, "draw", G_CALLBACK(on_draw_day), NULL);

    gtk_widget_show_all(S.day_area);

    poll_date(NULL);
    S.timer_id = g_timeout_add(60000, poll_date, NULL);

    return S.day_area;
}

static void destroy_date(void) {
    if (S.timer_id) {
        g_source_remove(S.timer_id);
        S.timer_id = 0;
    }
}

/* ══════════════════════════════════════════════
   VAXP Entry Point
   ══════════════════════════════════════════════ */
vaxpWidgetAPI *vaxp_widget_init(void) {
    static vaxpWidgetAPI api;
    api.name           = "VAXP Date Widget";
    api.description    = "A date widget with manually drawn geometric cutout letters";
    api.author         = "VAXP User";
    api.create_widget  = create_widget;
    api.update_theme   = NULL;
    api.destroy_widget = destroy_date;
    return &api;
}

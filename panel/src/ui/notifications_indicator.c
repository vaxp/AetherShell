/*
 * notifications_indicator.c — AetherShell Panel Notifications Indicator
 *
 * Draws a custom interactive indicator (Cairo) on the panel bar, mirroring
 * the battery_indicator approach:
 *
 *   GtkButton
 *     └─ GtkDrawingArea   ← on_draw_notif_indicator() paints:
 *                               • a bell silhouette
 *                               • the notification count (or a dot when 0)
 *
 * The widget subscribes to vaxp_notifications so it redraws automatically
 * every time the notification list changes.
 */

#include "notifications_indicator.h"
#include "window_backend.h"

#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>

/* ── Module-level state ───────────────────────────────────────────────────── */
static GtkWidget  *s_button       = NULL;
static GtkWidget  *s_drawing_area = NULL;
static GtkWidget  *s_notif_window = NULL;
static guint       s_count        = 0;

/* ── Public: called by notifications_ui.c to push the new count ──────────── */
void notifications_indicator_set_count(guint count)
{
    s_count = count;
    if (s_drawing_area)
        gtk_widget_queue_draw(s_drawing_area);
}

/* ── Drawing ──────────────────────────────────────────────────────────────── */

static gboolean on_draw_notif_indicator(GtkWidget *widget, cairo_t *cr,
                                        gpointer user_data)
{
    (void)user_data;

    guint w = (guint)gtk_widget_get_allocated_width(widget);
    guint h = (guint)gtk_widget_get_allocated_height(widget);

    /* ── Colours — read from GTK CSS (designer-controlled named colours) ── */
    GtkStyleContext *sctx = gtk_widget_get_style_context(widget);

    /* Bell fill — @notif_bell_color, default light-grey */
    GdkRGBA bell_rgba = { 0.80, 0.80, 0.80, 1.0 };
    gtk_style_context_lookup_color(sctx, "notif_bell_color", &bell_rgba);

    /* Badge background — @notif_badge_bg, default purple */
    GdkRGBA badge_bg = { 0.807, 0.576, 0.847, 1.0 };
    gtk_style_context_lookup_color(sctx, "notif_badge_bg", &badge_bg);

    /* Badge text — @notif_badge_fg, default near-black */
    GdkRGBA badge_fg = { 0.1, 0.0, 0.15, 1.0 };
    gtk_style_context_lookup_color(sctx, "notif_badge_fg", &badge_fg);

    /* Dim dot (zero notifications) — @notif_dot_color, default dark-grey */
    GdkRGBA dot_rgba = { 0.35, 0.35, 0.35, 0.6 };
    gtk_style_context_lookup_color(sctx, "notif_dot_color", &dot_rgba);

    /* ── Bell body ────────────────────────────────────────────────────── */
    double cx   = w * 0.5;
    double cy   = h * 0.52;
    double bell_w = w * 0.55;
    double bell_h = h * 0.52;

    /* Stem (top arc handle) */
    double stem_r = bell_w * 0.13;
    double stem_cx = cx;
    double stem_cy = cy - bell_h * 0.5 + stem_r * 0.4;

    cairo_set_source_rgba(cr, bell_rgba.red, bell_rgba.green, bell_rgba.blue, bell_rgba.alpha);

    /* Bell dome — rounded trapezoid via arc path */
    cairo_new_sub_path(cr);
    /* top arc */
    cairo_arc(cr, stem_cx, stem_cy, stem_r, G_PI, 0);
    /* right sloped side */
    cairo_line_to(cr, cx + bell_w * 0.5, cy + bell_h * 0.4);
    /* bottom-right corner rounding */
    cairo_arc(cr, cx + bell_w * 0.5 - bell_w * 0.08,
                  cy + bell_h * 0.4,
                  bell_w * 0.08, 0, G_PI / 2.0);
    /* bottom bar */
    cairo_line_to(cr, cx - bell_w * 0.5 + bell_w * 0.08,
                      cy + bell_h * 0.4 + bell_w * 0.08);
    /* bottom-left corner rounding */
    cairo_arc(cr, cx - bell_w * 0.5 + bell_w * 0.08,
                  cy + bell_h * 0.4,
                  bell_w * 0.08, G_PI / 2.0, G_PI);
    /* left sloped side */
    cairo_line_to(cr, cx - bell_w * 0.5, cy - bell_h * 0.5 + stem_r);
    cairo_close_path(cr);
    cairo_fill(cr);

    /* Clapper (small half-circle at the bottom) */
    double clapper_r = bell_w * 0.14;
    double clapper_cx = cx;
    double clapper_cy = cy + bell_h * 0.4 + bell_w * 0.08 + clapper_r * 0.6;
    cairo_arc(cr, clapper_cx, clapper_cy, clapper_r, 0, G_PI);
    cairo_fill(cr);

    /* ── Count / badge ────────────────────────────────────────────────── */
    if (s_count == 0) {
        /* Draw a small dim dot — "no notifications" */
        cairo_set_source_rgba(cr, dot_rgba.red, dot_rgba.green, dot_rgba.blue, dot_rgba.alpha);
        cairo_arc(cr, cx + bell_w * 0.38, cy - bell_h * 0.38, 2.5, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        /* Accent circle badge with count number */
        double badge_cx = cx + bell_w * 0.38;
        double badge_cy = cy - bell_h * 0.44;
        double badge_r  = (s_count >= 10) ? bell_w * 0.40 : bell_w * 0.34;

        /* Badge background */
        cairo_set_source_rgba(cr, badge_bg.red, badge_bg.green, badge_bg.blue, badge_bg.alpha);
        cairo_arc(cr, badge_cx, badge_cy, badge_r, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Count text */
        char txt[8];
        if (s_count > 99)
            g_snprintf(txt, sizeof(txt), "99+");
        else
            g_snprintf(txt, sizeof(txt), "%u", s_count);

        cairo_set_source_rgba(cr, badge_fg.red, badge_fg.green, badge_fg.blue, badge_fg.alpha);

        double font_size = (s_count >= 10) ? badge_r * 1.3 : badge_r * 1.5;
        cairo_set_font_size(cr, font_size);
        cairo_select_font_face(cr, "Sans",
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);

        cairo_text_extents_t te;
        cairo_text_extents(cr, txt, &te);
        cairo_move_to(cr,
                      badge_cx - te.width / 2.0 - te.x_bearing,
                      badge_cy - te.height / 2.0 - te.y_bearing);
        cairo_show_text(cr, txt);
    }

    return FALSE;
}

/* ── Click handler ────────────────────────────────────────────────────────── */

static void on_indicator_clicked(GtkButton *btn, gpointer user_data)
{
    (void)user_data;
    GtkWidget *w = s_notif_window;
    if (!w) return;

    if (gtk_widget_get_visible(w)) {
        gtk_widget_hide(w);
    } else {
        panel_window_backend_align_popup(GTK_WINDOW(w), GTK_WIDGET(btn), 360);
        gtk_widget_show_all(w);
    }
}

/* ── Public factory ───────────────────────────────────────────────────────── */

GtkWidget *create_notifications_indicator_widget(GtkWidget *notif_window)
{
    s_notif_window = notif_window;
    s_count        = 0;

    /* Drawing area — same idea as battery_indicator */
    s_drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_drawing_area, 36, 28);
    gtk_widget_set_valign(s_drawing_area, GTK_ALIGN_CENTER);
    g_signal_connect(s_drawing_area, "draw",
                     G_CALLBACK(on_draw_notif_indicator), NULL);

    /* Button */
    s_button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(s_button), GTK_RELIEF_NONE);
    gtk_widget_set_name(s_button, "notif-indicator-btn");
    gtk_widget_set_tooltip_text(s_button, "Notifications");
    gtk_container_add(GTK_CONTAINER(s_button), s_drawing_area);

    g_signal_connect(s_button, "clicked",
                     G_CALLBACK(on_indicator_clicked), NULL);

    /* Do NOT call vaxp_notifications_init here.
     * notifications_ui.c already owns the single callback slot.
     * Count is pushed externally via notifications_indicator_set_count(). */

    gtk_widget_show_all(s_button);
    return s_button;
}

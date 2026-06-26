/*
 * 🔦 Vaxp Basilisk - basilisk Window (Cairo - Rewrite)
 * النافذة شفافة تماماً، العناصر مرسومة فقط بـ Cairo
 */

#include "basilisk_window.h"
#include "window.h"
#include "ai_chat.h"
#include "commands.h"
#include <cairo/cairo.h>
#include <math.h>
#include <string.h>

extern BasiliskState *state;

/* ── ثوابت ── */
#define SP_WIDTH      620
#define SP_HEIGHT      62
#define SP_RADIUS      36.0   /* pill كامل */
#define SP_ICON_R      22.0
#define SP_ICON_GAP    10.0
#define SP_SEARCH_W   380.0
#define SP_PAD         20.0
#define SP_FONT       "Inter"
#define SP_MODES       SPOT_MODE_COUNT

/* ── حالة داخلية ── */
static struct {
    GtkWidget     *window;
    GtkWidget     *canvas;    /* GtkDrawingArea - يستقبل كل الأحداث */
    GtkWidget     *entry;     /* GtkEntry مخفي لإدارة النص فقط */
    basiliskMode  mode;
    gboolean       visible;
    int            hover;     /* -1 = لا شيء */
    gboolean       cursor_visible;
    guint          blink_timer_id;
    double         icon_cx[SP_MODES];
    double         icon_cy[SP_MODES];
} S;

/* ══════════════════════════════════════════════════════
 * مساعدات Cairo
 * ══════════════════════════════════════════════════════ */

static void pill(cairo_t *cr, double x, double y, double w, double h, double r)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr, x+w-r, y+r,   r, -G_PI/2, 0);
    cairo_arc(cr, x+w-r, y+h-r, r,  0,       G_PI/2);
    cairo_arc(cr, x+r,   y+h-r, r,  G_PI/2,  G_PI);
    cairo_arc(cr, x+r,   y+r,   r,  G_PI,   -G_PI/2);
    cairo_close_path(cr);
}

/* رسم نص في منتصف نقطة */
static void text_at(cairo_t *cr, const char *font, double size,
                    const char *txt, double cx, double cy,
                    double r, double g, double b, double a)
{
    cairo_save(cr);
    cairo_select_font_face(cr, font,
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t e;
    cairo_text_extents(cr, txt, &e);
    cairo_set_source_rgba(cr, r, g, b, a);
    cairo_move_to(cr,
                  cx - e.width/2 - e.x_bearing,
                  cy - e.height/2 - e.y_bearing);
    cairo_show_text(cr, txt);
    cairo_restore(cr);
}

/* رسم عدسة البحث */
static void draw_lens(cairo_t *cr, double cx, double cy)
{
    cairo_save(cr);
    cairo_set_line_width(cr, 2.2);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.6);
    cairo_new_path(cr);
    cairo_arc(cr, cx-2, cy-2, 8.5, 0, 2*G_PI);
    cairo_stroke(cr);
    double a = G_PI/4;
    cairo_move_to(cr, cx-2 + 7.5*cos(a), cy-2 + 7.5*sin(a));
    cairo_line_to(cr, cx-2 + 13*cos(a),  cy-2 + 13*sin(a));
    cairo_stroke(cr);
    cairo_restore(cr);
}

/* رسم أيقونة وضع */
static void draw_icon(cairo_t *cr, int idx, double cx, double cy,
                      gboolean active, gboolean hov)
{
    /* الدائرة */
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, SP_ICON_R, 0, 2*G_PI);
    if (active) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.600);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.700);
        cairo_set_line_width(cr, 1.4);
        cairo_stroke(cr);
    } else if (hov) {
        cairo_set_source_rgba(cr, 0, 0, 0, 0.450);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.500);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    } else {
        /* نفس لون خلفية وحدود شريط البحث */
        cairo_set_source_rgba(cr, 0, 0, 0, 0.300);
        cairo_fill_preserve(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.350);
        cairo_set_line_width(cr, 1.0);
        cairo_stroke(cr);
    }

    double alpha = active ? 0.95 : (hov ? 0.80 : 0.55);
    /* محاولة Nerd Font أولاً */
    const char *nf[] = { "󰍉", "󰀻", "󰭻", "󰘳" };
    const char *fb[] = { "SRC", "APP", "AI", "CMD" };

    cairo_save(cr);
    cairo_select_font_face(cr, "Symbols Nerd Font",
                           CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 18);
    cairo_text_extents_t e;
    cairo_text_extents(cr, nf[idx], &e);
    if (e.width > 3) {
        cairo_set_source_rgba(cr, 1, 1, 1, alpha);
        cairo_move_to(cr,
                      cx - e.width/2 - e.x_bearing,
                      cy - e.height/2 - e.y_bearing);
        cairo_show_text(cr, nf[idx]);
    } else {
        text_at(cr, SP_FONT, 9, fb[idx], cx, cy, 1, 1, 1, alpha);
    }
    cairo_restore(cr);
}

static gboolean on_blink_timer(gpointer data)
{
    (void)data;
    if (S.visible) {
        S.cursor_visible = !S.cursor_visible;
        basilisk_redraw();
    }
    return TRUE; /* استمر في الاستدعاء */
}

static void reset_blink_timer(void)
{
    if (S.blink_timer_id > 0) {
        g_source_remove(S.blink_timer_id);
    }
    S.cursor_visible = TRUE;
    S.blink_timer_id = g_timeout_add(600, on_blink_timer, NULL);
    basilisk_redraw();
}

/* ══════════════════════════════════════════════════════
 * on_draw — كل الرسم هنا
 * ══════════════════════════════════════════════════════ */

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer d)
{
    (void)w; (void)d;
    double H = SP_HEIGHT;
    (void)H;

    /* مسح كامل — الخلفية شفافة 100% */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* ── شريط البحث (pill) ── */
    double bar_w = SP_SEARCH_W;
    double bar_h = H - SP_PAD;
    double bar_y = SP_PAD / 2.0;
    pill(cr, SP_PAD/2, bar_y, bar_w, bar_h, bar_h / 2.0);
    /* تعبئة شفافة خفيفة (glassmorphism) */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.300);
    cairo_fill_preserve(cr);
    /* إطار ناعم */
    cairo_set_source_rgba(cr, 0, 0, 0, 0.350);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);

    /* عدسة البحث */
    draw_lens(cr, SP_PAD/2 + SP_PAD + 4, H/2);

    /* نص البحث أو placeholder */
    const gchar *txt = gtk_entry_get_text(GTK_ENTRY(S.entry));
    double tx = SP_PAD/2 + SP_PAD*2 + 12;
    double ty = H/2;

    if (txt && txt[0]) {
        cairo_save(cr);
        cairo_select_font_face(cr, SP_FONT,
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 18);
        cairo_text_extents_t e;
        cairo_text_extents(cr, txt, &e);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.95);
        cairo_move_to(cr, tx, ty - e.height/2 - e.y_bearing);
        cairo_show_text(cr, txt);
        
        /* cursor at the end of text */
        if (S.cursor_visible) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
            cairo_set_line_width(cr, 1.5);
            double cx2 = tx + e.x_advance + 1; // x_advance includes trailing spaces
            cairo_move_to(cr, cx2, ty-11);
            cairo_line_to(cr, cx2, ty+11);
            cairo_stroke(cr);
        }
        
        cairo_restore(cr);
    } else {
        const char *ph[] = {
            "basilisk Search",
            "Browse Applications...",
            "Ask AI anything...",
            "Type a command..."
        };
        cairo_save(cr);
        cairo_select_font_face(cr, SP_FONT,
                               CAIRO_FONT_SLANT_ITALIC,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 17);
        cairo_text_extents_t e;
        cairo_text_extents(cr, ph[S.mode], &e);
        cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
        cairo_move_to(cr, tx, ty - e.height/2 - e.y_bearing);
        cairo_show_text(cr, ph[S.mode]);
        
        /* cursor at the beginning of placeholder */
        if (S.cursor_visible) {
            cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
            cairo_set_line_width(cr, 1.5);
            cairo_move_to(cr, tx, ty-11);
            cairo_line_to(cr, tx, ty+11);
            cairo_stroke(cr);
        }
        
        cairo_restore(cr);
    }

    /* ── أيقونات الأوضاع ── */
    double ix0 = SP_PAD/2 + bar_w + SP_ICON_GAP + SP_ICON_R;

    for (int i = 0; i < SP_MODES; i++) {
        double cx = ix0 + i * (SP_ICON_R*2 + SP_ICON_GAP);
        double cy = H/2.0;
        S.icon_cx[i] = cx;
        S.icon_cy[i] = cy;
        draw_icon(cr, i, cx, cy,
                  S.mode == (basiliskMode)i,
                  S.hover == i);
    }

    return FALSE;
}

/* ══════════════════════════════════════════════════════
 * أحداث الماوس
 * ══════════════════════════════════════════════════════ */

static int hit_icon(double mx, double my)
{
    for (int i = 0; i < SP_MODES; i++) {
        double dx = mx - S.icon_cx[i];
        double dy = my - S.icon_cy[i];
        if (dx*dx + dy*dy <= SP_ICON_R*SP_ICON_R) return i;
    }
    return -1;
}

static gboolean on_click(GtkWidget *w, GdkEventButton *ev, gpointer d)
{
    (void)w; (void)d;
    if (ev->button != 1) return FALSE;

    int idx = hit_icon(ev->x, ev->y);
    if (idx < 0) {
        /* نقر على البحث → تركيز على entry */
        gtk_widget_grab_focus(S.entry);
        return TRUE;
    }

    basiliskMode m = (basiliskMode)idx;
    if (m == S.mode && m != SPOT_MODE_APPS) {
        S.mode = SPOT_MODE_SEARCH;
        gtk_widget_grab_focus(S.entry);
        basilisk_redraw();
        return TRUE;
    }
    S.mode = m;

    switch (S.mode) {
    case SPOT_MODE_APPS:
        basilisk_hide();
        window_show();
        break;
    case SPOT_MODE_AI:
        basilisk_hide();
        ai_chat_show(gtk_entry_get_text(GTK_ENTRY(S.entry)));
        break;
    default:
        gtk_entry_set_text(GTK_ENTRY(S.entry), "");
        gtk_widget_grab_focus(S.entry);
        basilisk_redraw();
        break;
    }
    return TRUE;
}

static gboolean on_motion(GtkWidget *w, GdkEventMotion *ev, gpointer d)
{
    (void)w; (void)d;
    int h = hit_icon(ev->x, ev->y);
    if (h != S.hover) { S.hover = h; basilisk_redraw(); }
    return FALSE;
}

static gboolean on_leave(GtkWidget *w, GdkEventCrossing *ev, gpointer d)
{
    (void)w; (void)ev; (void)d;
    if (S.hover != -1) { S.hover = -1; basilisk_redraw(); }
    return FALSE;
}

/* ══════════════════════════════════════════════════════
 * أحداث لوحة المفاتيح (على النافذة + الـ entry)
 * ══════════════════════════════════════════════════════ */

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer d)
{
    (void)w; (void)d;
    if (ev->keyval == GDK_KEY_Escape) {
        basilisk_hide();
        return TRUE;
    }
    if (ev->keyval == GDK_KEY_Return || ev->keyval == GDK_KEY_KP_Enter) {
        const gchar *t = gtk_entry_get_text(GTK_ENTRY(S.entry));
        if (t && t[0]) {
            switch (S.mode) {
                case SPOT_MODE_APPS:
                    basilisk_hide();
                    window_show_with_search(t);
                    break;
                case SPOT_MODE_SEARCH:
                    basilisk_hide();
                    if (commands_check_prefix(t)) {
                        commands_execute(t);
                    } else {
                        /* افتراضياً في وضع البحث: نبحث في الويب أو التطبيقات */
                        /* يمكنك تغييره إلى commands_execute_file_search(t) إذا أردت بحث الملفات */
                        commands_execute_web_search(t, "google");
                    }
                    break;
                case SPOT_MODE_COMMANDS:
                    basilisk_hide();
                    if (commands_check_prefix(t)) {
                        commands_execute(t);
                    } else {
                        /* افتراضياً في وضع الأوامر: تشغيل كأمر طرفية */
                        commands_execute_vater(t);
                    }
                    break;
                case SPOT_MODE_AI:
                    basilisk_hide();
                    ai_chat_show(t);
                    break;
                default:
                    break;
            }
        }
        return TRUE;
    }
    /* وجّه كل ضغطة مفتاح إلى الـ entry */
    if (!gtk_widget_has_focus(S.entry))
        return gtk_widget_event(S.entry, (GdkEvent *)ev);
        
    reset_blink_timer();
    return FALSE;
}

static void on_entry_changed(GtkEditable *e, gpointer d)
{
    (void)e; (void)d;
    reset_blink_timer();
}

/* ══════════════════════════════════════════════════════
 * Wayland / X11 — إخفاء decorations
 * ══════════════════════════════════════════════════════ */

static void on_realize(GtkWidget *w, gpointer d)
{
    (void)d;
    GdkWindow *gw = gtk_widget_get_window(w);
    if (!gw) return;
    if (GDK_IS_WAYLAND_WINDOW(gw))
        gdk_wayland_window_announce_csd(gw);
    else {
        gdk_window_set_decorations(gw, 0);
        gdk_window_set_functions(gw, 0);
    }
}

/* ══════════════════════════════════════════════════════
 * basilisk_init
 * ══════════════════════════════════════════════════════ */

void basilisk_init(void)
{
    S.mode    = SPOT_MODE_SEARCH;
    S.hover   = -1;
    S.visible = FALSE;

    /* النافذة */
    S.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(S.window), "Basilisk");
    gtk_window_set_decorated(GTK_WINDOW(S.window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(S.window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(S.window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(S.window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_keep_above(GTK_WINDOW(S.window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(S.window), SP_WIDTH, SP_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(S.window), FALSE);
    gtk_widget_set_app_paintable(S.window, TRUE);

    /* شفافية ARGB */
    GdkScreen *scr = gtk_widget_get_screen(S.window);
    GdkVisual *vis = gdk_screen_get_rgba_visual(scr);
    if (vis) gtk_widget_set_visual(S.window, vis);

    /*
     * التخطيط:
     *   GtkFixed
     *     ├── GtkDrawingArea (كامل الحجم، يستقبل الأحداث)
     *     └── GtkEntry       (حجم 1×1، شفاف، خارج المنطقة المرئية)
     */
    GtkWidget *fixed = gtk_fixed_new();
    gtk_container_add(GTK_CONTAINER(S.window), fixed);

    /* Canvas */
    S.canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(S.canvas, SP_WIDTH, SP_HEIGHT);
    gtk_widget_set_can_focus(S.canvas, TRUE);
    gtk_widget_set_events(S.canvas,
        GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK |
        GDK_LEAVE_NOTIFY_MASK | GDK_KEY_PRESS_MASK);
    gtk_fixed_put(GTK_FIXED(fixed), S.canvas, 0, 0);

    /* Entry — نضعه خارج المنطقة المرئية (ولكن مرئي لـ GTK ليتلقى الفوكس) */
    S.entry = gtk_entry_new();
    gtk_widget_set_size_request(S.entry, 1, 1);
    gtk_fixed_put(GTK_FIXED(fixed), S.entry, -1000, -1000);

    /* إشارات */
    g_signal_connect(S.canvas, "draw",                 G_CALLBACK(on_draw),   NULL);
    g_signal_connect(S.canvas, "button-press-event",   G_CALLBACK(on_click),  NULL);
    g_signal_connect(S.canvas, "motion-notify-event",  G_CALLBACK(on_motion), NULL);
    g_signal_connect(S.canvas, "leave-notify-event",   G_CALLBACK(on_leave),  NULL);
    g_signal_connect(S.entry,  "changed",              G_CALLBACK(on_entry_changed), NULL);
    g_signal_connect(S.window, "key-press-event",      G_CALLBACK(on_key),    NULL);
    g_signal_connect(S.window, "delete-event",         G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    g_signal_connect(S.window, "realize",              G_CALLBACK(on_realize), NULL);
}

/* ══════════════════════════════════════════════════════
 * API العلنية
 * ══════════════════════════════════════════════════════ */

void basilisk_redraw(void)
{
    if (S.canvas) gtk_widget_queue_draw(S.canvas);
}

void basilisk_show(void)
{
    if (S.visible) return;

    gint x = 0, y = 0;

    /* على Wayland لا يوجد "primary monitor" — نستخدم الشاشة الأولى كـ fallback */
    GdkDisplay   *dpy = gdk_display_get_default();
    GdkMonitor   *mon = gdk_display_get_primary_monitor(dpy);
    if (!mon && gdk_display_get_n_monitors(dpy) > 0)
        mon = gdk_display_get_monitor(dpy, 0);

    if (mon) {
        GdkRectangle geo;
        gdk_monitor_get_geometry(mon, &geo);
        x = geo.x + (geo.width  - SP_WIDTH)  / 2;
        y = geo.y + (int)(geo.height * 0.30);
    } else {
        /* آخر حل: نجعل GTK يتولى التمركز */
        gtk_window_set_position(GTK_WINDOW(S.window), GTK_WIN_POS_CENTER);
        x = 0; y = 0; /* سيتجاهلها GTK */
    }

    gtk_entry_set_text(GTK_ENTRY(S.entry), "");
    S.mode  = SPOT_MODE_SEARCH;
    S.hover = -1;

    gtk_window_move(GTK_WINDOW(S.window), x, y);
    gtk_widget_show_all(S.window);   /* يُظهر النافذة + كل الـ children بما فيها الـ entry */
    gtk_window_present(GTK_WINDOW(S.window));
    gtk_widget_grab_focus(S.entry);

    S.visible = TRUE;
    state->basilisk_visible = TRUE;
    reset_blink_timer();
}

void basilisk_hide(void)
{
    if (!S.visible) return;
    if (S.blink_timer_id > 0) {
        g_source_remove(S.blink_timer_id);
        S.blink_timer_id = 0;
    }
    gtk_widget_hide(S.window);
    S.visible = FALSE;
    state->basilisk_visible = FALSE;
}

void basilisk_toggle(void)
{
    if (S.visible) basilisk_hide();
    else           basilisk_show();
}

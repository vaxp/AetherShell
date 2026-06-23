#include "launcher_window.h"
#include "search_bar.h"
#include "app_grid.h"
#include "../core/desktop_reader.h"
#include "../core/icon_loader.h"

#include <gdk/gdk.h>
#include <gtk-layer-shell.h>
#include <string.h>
#include <sys/stat.h>

/* -------------------------------------------------------------------------
 * Private struct
 * ------------------------------------------------------------------------- */

struct _VenomLauncherWindow {
    GtkApplicationWindow  parent_instance;

    GPtrArray    *apps;
    GtkWidget    *search_bar;
    GtkWidget    *app_grid;
    GtkWidget    *root_overlay;

    char         *wallpaper_path;
    GdkPixbuf    *wallpaper_pixbuf;
    cairo_surface_t *wallpaper_surface;
    int           cached_width;
    int           cached_height;
    time_t        wallpaper_mtime;

    GList        *dir_monitors;
    guint         reload_source_id;
};

G_DEFINE_TYPE (VenomLauncherWindow, venom_launcher_window,
               GTK_TYPE_APPLICATION_WINDOW)

/* -------------------------------------------------------------------------
 * CSS loader
 * ------------------------------------------------------------------------- */
static void
load_css (void)
{
    GtkCssProvider *provider = gtk_css_provider_new ();

    /* Try installed path first, then development path */
    const char *paths[] = {
        PKGDATADIR "/style/launcher.css",
        "data/style/launcher.css",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        GError *err = NULL;
        if (gtk_css_provider_load_from_path (provider, paths[i], &err)) {
            gtk_style_context_add_provider_for_screen (
                gdk_screen_get_default (),
                GTK_STYLE_PROVIDER (provider),
                GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
            break;
        }
        g_error_free (err);
    }

    g_object_unref (provider);
}

/* -------------------------------------------------------------------------
 * Transparency / RGBA visual
 * ------------------------------------------------------------------------- */

static void
setup_transparency (GtkWidget *widget)
{
    GdkScreen  *screen = gtk_widget_get_screen (widget);
    GdkVisual  *visual = gdk_screen_get_rgba_visual (screen);

    if (visual && gdk_screen_is_composited (screen)) {
        gtk_widget_set_visual (widget, visual);
        gtk_widget_set_app_paintable (widget, TRUE);
    }
}

/* -------------------------------------------------------------------------
 * Window background
 * ------------------------------------------------------------------------- */

static void
load_wallpaper (VenomLauncherWindow *self)
{
    const char *config_dir = g_get_user_config_dir ();
    char *wallpaper_file_path = g_build_filename (config_dir, "vaxp", "desktop", "background", NULL);

    struct stat st;
    gboolean file_changed = FALSE;

    if (stat (wallpaper_file_path, &st) == 0) {
        if (self->wallpaper_mtime != st.st_mtime) {
            file_changed = TRUE;
            self->wallpaper_mtime = st.st_mtime;
        }
    } else {
        if (self->wallpaper_path != NULL) {
            file_changed = TRUE;
            self->wallpaper_mtime = 0;
        }
    }

    if (file_changed) {
        if (self->wallpaper_surface) {
            cairo_surface_destroy (self->wallpaper_surface);
            self->wallpaper_surface = NULL;
        }
        if (self->wallpaper_pixbuf) {
            g_object_unref (self->wallpaper_pixbuf);
            self->wallpaper_pixbuf = NULL;
        }
        if (self->wallpaper_path) {
            g_free (self->wallpaper_path);
            self->wallpaper_path = NULL;
        }

        GError *pixbuf_error = NULL;
        self->wallpaper_pixbuf = gdk_pixbuf_new_from_file (wallpaper_file_path, &pixbuf_error);
        if (self->wallpaper_pixbuf) {
            self->wallpaper_path = g_strdup (wallpaper_file_path);
        } else {
            if (pixbuf_error) g_error_free (pixbuf_error);
        }
    }

    g_free (wallpaper_file_path);
}

static gboolean
on_window_draw (GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
    (void) user_data;
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (widget);

    GtkAllocation alloc;
    const double radius = 24.0;

    gtk_widget_get_allocation (widget, &alloc);

    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 0.300);
    cairo_paint (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    cairo_new_sub_path (cr);
    cairo_arc (cr, alloc.width - radius, radius, radius, -G_PI / 2.0, 0);
    cairo_arc (cr, alloc.width - radius, alloc.height - radius, radius, 0, G_PI / 2.0);
    cairo_arc (cr, radius, alloc.height - radius, radius, G_PI / 2.0, G_PI);
    cairo_arc (cr, radius, radius, radius, G_PI, 3.0 * G_PI / 2.0);
    cairo_close_path (cr);

    if (self->wallpaper_pixbuf) {
        if (!self->wallpaper_surface || self->cached_width != alloc.width || self->cached_height != alloc.height) {
            if (self->wallpaper_surface) {
                cairo_surface_destroy (self->wallpaper_surface);
                self->wallpaper_surface = NULL;
            }

            self->cached_width = alloc.width;
            self->cached_height = alloc.height;

            int wp = gdk_pixbuf_get_width (self->wallpaper_pixbuf);
            int hp = gdk_pixbuf_get_height (self->wallpaper_pixbuf);

            double scale_x = (double)alloc.width / wp;
            double scale_y = (double)alloc.height / hp;
            double scale = scale_x > scale_y ? scale_x : scale_y;

            int scaled_w = (int)(wp * scale);
            int scaled_h = (int)(hp * scale);

            GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple (self->wallpaper_pixbuf, scaled_w, scaled_h, GDK_INTERP_BILINEAR);
            if (scaled_pixbuf) {
                self->wallpaper_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
                cairo_t *temp_cr = cairo_create (self->wallpaper_surface);

                double offset_x = (alloc.width - scaled_w) / 2.0;
                double offset_y = (alloc.height - scaled_h) / 2.0;

                gdk_cairo_set_source_pixbuf (temp_cr, scaled_pixbuf, offset_x, offset_y);
                cairo_paint (temp_cr);

                cairo_destroy (temp_cr);
                g_object_unref (scaled_pixbuf);
            }
        }

        if (self->wallpaper_surface) {
            cairo_save (cr);
            cairo_clip_preserve (cr);
            cairo_set_source_surface (cr, self->wallpaper_surface, 0, 0);
            cairo_paint (cr);
            cairo_restore (cr);
        }
    } else {
        /* Keep the window itself translucent so the compositor can blur behind it. */
        cairo_set_source_rgba (cr, 0.08, 0.09, 0.12, 0.38);
        cairo_fill_preserve (cr);
    }

    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, 0.12);
    cairo_set_line_width (cr, 1.2);
    cairo_stroke (cr);

    return FALSE;
}


/* -------------------------------------------------------------------------
 * Keyboard handling
 * ------------------------------------------------------------------------- */

static gboolean
on_key_press (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    (void) user_data;
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (widget);

    if (event->keyval == GDK_KEY_Escape) {
        venom_search_bar_clear (VENOM_SEARCH_BAR (self->search_bar));
        gtk_widget_hide (widget);
        return TRUE;
    }

    /* Page Navigation */
    if (event->keyval == GDK_KEY_Page_Up) {
        venom_app_grid_go_prev_page (VENOM_APP_GRID (self->app_grid));
        return TRUE;
    } else if (event->keyval == GDK_KEY_Page_Down) {
        venom_app_grid_go_next_page (VENOM_APP_GRID (self->app_grid));
        return TRUE;
    }

    /* Page Navigation via Arrow Keys when search is empty */
    const char *search_text = venom_search_bar_get_text (VENOM_SEARCH_BAR (self->search_bar));
    if (!search_text || search_text[0] == '\0') {
        if (event->keyval == GDK_KEY_Left) {
            venom_app_grid_go_prev_page (VENOM_APP_GRID (self->app_grid));
            return TRUE;
        } else if (event->keyval == GDK_KEY_Right) {
            venom_app_grid_go_next_page (VENOM_APP_GRID (self->app_grid));
            return TRUE;
        }
    }

    /* Forward typing to search bar */
    if (!gtk_widget_has_focus (self->search_bar)) {
        gtk_widget_grab_focus (self->search_bar);
    }

    return FALSE;
}

/* -------------------------------------------------------------------------
 * Mouse scroll handling
 * ------------------------------------------------------------------------- */

static gboolean
on_scroll (GtkWidget *widget, GdkEventScroll *event, gpointer user_data)
{
    (void) user_data;
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (widget);
    
    /* Accumulate delta for smooth scrolling */
    static double accum_y = 0.0;
    static guint32 last_scroll_time = 0;
    static guint32 last_page_time = 0;

    /* Reset accumulator if a lot of time passed since last scroll */
    if (event->time - last_scroll_time > 200) {
        accum_y = 0.0;
    }
    last_scroll_time = event->time;

    /* Debounce: Ignore events if we just changed a page within 300ms */
    if (event->time - last_page_time < 300) {
        return TRUE;
    }

    if (event->direction == GDK_SCROLL_UP) {
        venom_app_grid_go_prev_page (VENOM_APP_GRID (self->app_grid));
        last_page_time = event->time;
        return TRUE;
    } else if (event->direction == GDK_SCROLL_DOWN) {
        venom_app_grid_go_next_page (VENOM_APP_GRID (self->app_grid));
        last_page_time = event->time;
        return TRUE;
    } else if (event->direction == GDK_SCROLL_SMOOTH) {
        accum_y += event->delta_y;
        
        if (event->delta_y == 0.0 && event->delta_x != 0.0) {
            accum_y += event->delta_x;
        }

        if (accum_y <= -1.0) {
            venom_app_grid_go_prev_page (VENOM_APP_GRID (self->app_grid));
            accum_y = 0.0;
            last_page_time = event->time;
            return TRUE;
        } else if (accum_y >= 1.0) {
            venom_app_grid_go_next_page (VENOM_APP_GRID (self->app_grid));
            accum_y = 0.0;
            last_page_time = event->time;
            return TRUE;
        }
    }

    return FALSE;
}

/* -------------------------------------------------------------------------
 * Search changed handler
 * ------------------------------------------------------------------------- */

static void
on_search_changed (GtkWidget *search, gpointer data)
{
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (data);
    const char *query = venom_search_bar_get_text (VENOM_SEARCH_BAR (search));
    venom_app_grid_set_filter (VENOM_APP_GRID (self->app_grid), query);
}



/* -------------------------------------------------------------------------
 * GObject class init
 * ------------------------------------------------------------------------- */

static gboolean
reload_apps_idle_cb (gpointer data)
{
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (data);
    
    self->reload_source_id = 0;

    GPtrArray *new_apps = desktop_reader_load_apps ();
    
    if (self->apps)
        g_ptr_array_unref (self->apps);
        
    self->apps = new_apps;
    
    venom_app_grid_set_apps (VENOM_APP_GRID (self->app_grid), self->apps);
    
    const char *query = venom_search_bar_get_text (VENOM_SEARCH_BAR (self->search_bar));
    venom_app_grid_set_filter (VENOM_APP_GRID (self->app_grid), query);
    
    return G_SOURCE_REMOVE;
}

static void
on_desktop_dir_changed (GFileMonitor *monitor, GFile *file, GFile *other_file,
                        GFileMonitorEvent event_type, gpointer user_data)
{
    (void) monitor;
    (void) file;
    (void) other_file;
    (void) event_type;
    
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (user_data);
    
    if (self->reload_source_id != 0) {
        g_source_remove (self->reload_source_id);
    }
    
    self->reload_source_id = g_timeout_add (500, reload_apps_idle_cb, self);
}

static void
setup_directory_monitors (VenomLauncherWindow *self)
{
    const char *dirs[] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        NULL
    };
    
    for (int i = 0; dirs[i] != NULL; i++) {
        GFile *file = g_file_new_for_path (dirs[i]);
        GFileMonitor *mon = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, NULL);
        if (mon) {
            g_signal_connect (mon, "changed", G_CALLBACK (on_desktop_dir_changed), self);
            self->dir_monitors = g_list_prepend (self->dir_monitors, mon);
        }
        g_object_unref (file);
    }
    
    /* User directory */
    const char *home = g_get_home_dir ();
    if (home) {
        char *user_dir = g_build_filename (home, ".local", "share", "applications", NULL);
        GFile *file = g_file_new_for_path (user_dir);
        GFileMonitor *mon = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, NULL, NULL);
        if (mon) {
            g_signal_connect (mon, "changed", G_CALLBACK (on_desktop_dir_changed), self);
            self->dir_monitors = g_list_prepend (self->dir_monitors, mon);
        }
        g_object_unref (file);
        g_free (user_dir);
    }
}

static void
venom_launcher_window_finalize (GObject *obj)
{
    VenomLauncherWindow *self = VENOM_LAUNCHER_WINDOW (obj);

    if (self->reload_source_id != 0) {
        g_source_remove (self->reload_source_id);
        self->reload_source_id = 0;
    }

    if (self->dir_monitors) {
        g_list_free_full (self->dir_monitors, g_object_unref);
        self->dir_monitors = NULL;
    }

    if (self->apps)
        g_ptr_array_unref (self->apps);
    if (self->wallpaper_path)
        g_free (self->wallpaper_path);
    if (self->wallpaper_pixbuf)
        g_object_unref (self->wallpaper_pixbuf);
    if (self->wallpaper_surface)
        cairo_surface_destroy (self->wallpaper_surface);
    icon_loader_destroy ();
    G_OBJECT_CLASS (venom_launcher_window_parent_class)->finalize (obj);
}

static void
venom_launcher_window_class_init (VenomLauncherWindowClass *klass)
{
    GObjectClass *obj_class = G_OBJECT_CLASS (klass);
    obj_class->finalize = venom_launcher_window_finalize;
}

static void
set_layer_shell_monitor (GtkWindow *win)
{
    GdkDisplay *display = gdk_display_get_default ();
    if (!display) {
        g_warning ("Launcher: no GDK display available for layer-shell monitor");
        return;
    }

    GdkMonitor *monitor = gdk_display_get_primary_monitor (display);
    if (!monitor && gdk_display_get_n_monitors (display) > 0)
        monitor = gdk_display_get_monitor (display, 0);

    if (!monitor) {
        g_warning ("Launcher: no monitor available for layer-shell");
        return;
    }

    GdkRectangle geometry;
    gdk_monitor_get_geometry (monitor, &geometry);
    gtk_widget_set_size_request (GTK_WIDGET (win), geometry.width, geometry.height);
    gtk_layer_set_monitor (win, monitor);
}

static void
venom_launcher_window_init (VenomLauncherWindow *self)
{
    self->wallpaper_path = NULL;
    self->wallpaper_pixbuf = NULL;
    self->wallpaper_surface = NULL;
    self->cached_width = 0;
    self->cached_height = 0;
    self->dir_monitors = NULL;
    self->reload_source_id = 0;

    setup_directory_monitors (self);
    load_wallpaper (self);

    GtkWindow *win = GTK_WINDOW (self);
    gboolean layer_shell_supported = gtk_layer_is_supported ();

    if (layer_shell_supported) {
        gtk_layer_init_for_window (win);
        gtk_layer_set_namespace (win, "launcher");
        set_layer_shell_monitor (win);
        gtk_layer_set_layer (win, GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_anchor (win, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor (win, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_anchor (win, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor (win, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_margin (win, GTK_LAYER_SHELL_EDGE_LEFT, 0);
        gtk_layer_set_margin (win, GTK_LAYER_SHELL_EDGE_RIGHT, 0);
        gtk_layer_set_margin (win, GTK_LAYER_SHELL_EDGE_TOP, 0);
        gtk_layer_set_margin (win, GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
        gtk_layer_set_exclusive_zone (win, -1);
        gtk_layer_set_keyboard_mode (win, GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
    } else {
        /* X11 fallback */
        gtk_window_set_keep_above (win, TRUE);
        gtk_window_set_position (win, GTK_WIN_POS_CENTER_ALWAYS);
        gtk_window_fullscreen (win);
    }

    gtk_window_set_decorated         (win, FALSE);
    gtk_window_set_skip_taskbar_hint (win, TRUE);
    gtk_window_set_skip_pager_hint   (win, TRUE);
    gtk_window_set_type_hint         (win, GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);

    gtk_widget_set_name (GTK_WIDGET (self), "launcher");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (GTK_WIDGET (self)), "launcher-window");

    /* Setup RGBA transparency */
    setup_transparency (GTK_WIDGET (self));

    /* Add scroll events to the window */
    gtk_widget_add_events (GTK_WIDGET (self), GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);

    /* Load CSS */
    load_css ();

    /* ── Root overlay ───────────────────────────────────────────────── */
    self->root_overlay = gtk_overlay_new ();
    gtk_container_add (GTK_CONTAINER (self), self->root_overlay);

    /* ── Main content box ──────────────────────────────────────────── */
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_top    (vbox, 70);
    gtk_widget_set_margin_bottom (vbox, 32);
    gtk_widget_set_margin_start  (vbox, 150);
    gtk_widget_set_margin_end    (vbox, 150);
    gtk_container_add (GTK_CONTAINER (self->root_overlay), vbox);

    /* ── Search Bar ────────────────────────────────────────────────── */
    self->search_bar = venom_search_bar_new ();
    gtk_widget_set_halign (self->search_bar, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (vbox), self->search_bar, FALSE, FALSE, 0);

    /* ── Load apps ─────────────────────────────────────────────────── */
    self->apps = desktop_reader_load_apps ();

    /* ── App Grid ──────────────────────────────────────────────────── */
    self->app_grid = venom_app_grid_new (self->apps);
    gtk_box_pack_start (GTK_BOX (vbox), self->app_grid, TRUE, TRUE, 0);

    /* Connect signals */
    g_signal_connect (self->search_bar, "search-changed-debounced",
                      G_CALLBACK (on_search_changed), self);

    g_signal_connect (GTK_WIDGET (self), "key-press-event",
                      G_CALLBACK (on_key_press), NULL);

    g_signal_connect (GTK_WIDGET (self), "scroll-event",
                      G_CALLBACK (on_scroll), NULL);

    g_signal_connect (GTK_WIDGET (self), "draw",
                      G_CALLBACK (on_window_draw), NULL);

}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GtkWidget *
venom_launcher_window_new (GtkApplication *app)
{
    VenomLauncherWindow *win = g_object_new (
        VENOM_TYPE_LAUNCHER_WINDOW,
        "application", app,
        NULL);

    return GTK_WIDGET (win);
}

void
venom_launcher_window_show_launcher (VenomLauncherWindow *win)
{
    g_return_if_fail (VENOM_IS_LAUNCHER_WINDOW (win));

    venom_search_bar_clear (VENOM_SEARCH_BAR (win->search_bar));
    venom_app_grid_set_filter (VENOM_APP_GRID (win->app_grid), NULL);

    load_wallpaper (win);
    gtk_widget_queue_draw (GTK_WIDGET (win));

    gtk_widget_show_all (GTK_WIDGET (win));
    gtk_window_present  (GTK_WINDOW (win));

    venom_search_bar_grab_focus (VENOM_SEARCH_BAR (win->search_bar));
}

void
venom_launcher_window_push_overlay (VenomLauncherWindow *win,
                                    GtkWidget           *widget)
{
    g_return_if_fail (VENOM_IS_LAUNCHER_WINDOW (win));
    g_return_if_fail (GTK_IS_WIDGET (widget));

    gtk_widget_set_halign (widget, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (widget, GTK_ALIGN_CENTER);

    gtk_overlay_add_overlay (GTK_OVERLAY (win->root_overlay), widget);
    gtk_widget_show_all (widget);
}

void
venom_launcher_window_pop_overlay (VenomLauncherWindow *win,
                                   GtkWidget           *widget)
{
    g_return_if_fail (VENOM_IS_LAUNCHER_WINDOW (win));
    g_return_if_fail (GTK_IS_WIDGET (widget));

    gtk_widget_destroy (widget);
}

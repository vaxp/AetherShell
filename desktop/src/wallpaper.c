/*
 * wallpaper.c
 * Window bootstrap, wallpaper rendering, and wallpaper picker UI.
*/

#include "wallpaper.h"
#include "desktop_config.h"
#include "video_wallpaper.h"
#include <glib/gstdio.h>
#include <string.h>
#include <gtk-layer-shell.h>
#include <math.h>

#define WALLPAPER_DIR "/usr/share/backgrounds"

GtkWidget *main_window = NULL;
GtkWidget *icon_layout = NULL;
int screen_w = 0;
int screen_h = 0;

static GdkPixbuf *wallpaper_pixbuf = NULL;
static char *current_wallpaper_path = NULL;
static gboolean monitor_signal_handlers_connected = FALSE;
static GFileMonitor *wallpaper_monitor = NULL;


static int current_anim_type = 0;

static GdkPixbuf *prev_wallpaper_pixbuf = NULL;
static double wallpaper_transition_alpha = 1.0;
static guint tick_callback_id = 0;
static gint64 transition_start_time = 0;
static void load_wallpaper(const char *path);
static GdkMonitor *get_target_monitor(GdkDisplay *display);
static void update_desktop_geometry(void);
static void on_monitors_changed(GdkDisplay *display, gpointer user_data);
static void on_main_window_realize(GtkWidget *widget, gpointer user_data);
static gboolean refresh_desktop_after_show(gpointer user_data);

static void on_wallpaper_file_changed(GFileMonitor *monitor, GFile *file, GFile *other_file, GFileMonitorEvent event_type, gpointer user_data) {
    (void)monitor;
    (void)file;
    (void)other_file;
    (void)user_data;
    if (event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event_type == G_FILE_MONITOR_EVENT_CREATED) {
        load_saved_wallpaper();
    }
}

void init_wallpaper_monitor(void) {
    char *path = get_vaxp_config_path("wallpaper");
    GFile *file = g_file_new_for_path(path);
    g_free(path);
    if (!wallpaper_monitor) {
        wallpaper_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, NULL);
        if (wallpaper_monitor) {
            g_signal_connect(wallpaper_monitor, "changed", G_CALLBACK(on_wallpaper_file_changed), NULL);
        }
    }
    g_object_unref(file);
}

static gboolean is_wayland_session(void) {
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        const char *name = G_OBJECT_TYPE_NAME(display);
        if (name && g_str_has_prefix(name, "GdkWayland")) {
            return TRUE;
        }
    }
    return FALSE;
}

static void load_wallpaper(const char *path);

static gboolean on_transition_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data) {
    gint64 current_time = g_get_monotonic_time();
    double elapsed = (current_time - transition_start_time) / 800000.0; // 800ms

    (void)widget;
    (void)frame_clock;
    (void)user_data;

    if (elapsed >= 1.0) {
        wallpaper_transition_alpha = 1.0;
        if (prev_wallpaper_pixbuf) {
            g_object_unref(prev_wallpaper_pixbuf);
            prev_wallpaper_pixbuf = NULL;
        }
        gtk_widget_queue_draw(icon_layout);
        tick_callback_id = 0;
        return G_SOURCE_REMOVE;
    }

    double f = elapsed - 1.0;
    wallpaper_transition_alpha = f * f * f + 1.0;
    
    gtk_widget_queue_draw(icon_layout);
    return G_SOURCE_CONTINUE;
}

static void load_wallpaper(const char *path) {
    GError *err = NULL;
    GdkPixbuf *pb;

    if (!path || strlen(path) == 0) return;
    if (screen_w <= 0 || screen_h <= 0) return;

    if (g_strcmp0(current_wallpaper_path, path) == 0) {
        return;
    }

    pb = gdk_pixbuf_new_from_file_at_scale(path, screen_w, screen_h, FALSE, &err);
    if (!pb) {
        g_warning("[Wallpaper] Failed to load '%s': %s", path, err ? err->message : "?");
        if (err) g_error_free(err);
        return;
    }

    if (wallpaper_pixbuf) {
        if (prev_wallpaper_pixbuf) g_object_unref(prev_wallpaper_pixbuf);
        prev_wallpaper_pixbuf = wallpaper_pixbuf;
    }
    wallpaper_pixbuf = pb;

    if (prev_wallpaper_pixbuf) {
        char *anim_str = NULL;
        current_anim_type = 0;
        char *anim_config = get_vaxp_config_path("wallpaper-anim");
        if (g_file_get_contents(anim_config, &anim_str, NULL, NULL)) {
            current_anim_type = atoi(anim_str);
            g_free(anim_str);
        }
        g_free(anim_config);

        wallpaper_transition_alpha = 0.0;
        transition_start_time = g_get_monotonic_time();
        if (tick_callback_id == 0 && icon_layout) {
            tick_callback_id = gtk_widget_add_tick_callback(icon_layout, on_transition_tick, NULL, NULL);
        }
    } else {
        wallpaper_transition_alpha = 1.0;
    }

    if (current_wallpaper_path != path) {
        g_free(current_wallpaper_path);
        current_wallpaper_path = g_strdup(path);
    }

    if (icon_layout) gtk_widget_queue_draw(icon_layout);
}

void load_saved_wallpaper(void) {
    char *path = NULL;
    gsize len = 0;
    gboolean valid;
    char *wallpaper_config = get_vaxp_config_path("wallpaper");

    if (!g_file_get_contents(wallpaper_config, &path, &len, NULL)) {
        g_free(wallpaper_config);
        return;
    }
    g_free(wallpaper_config);

    g_strstrip(path);

    valid = (path[0] == '/');
    for (gsize i = 1; valid && i < strlen(path); i++) {
        if ((unsigned char)path[i] < 0x20 || (unsigned char)path[i] > 0x7E) {
            valid = FALSE;
        }
    }

    if (!valid || strlen(path) <= 1) {
        g_warning("[Wallpaper] Saved path is invalid, ignoring: '%s'", path);
        g_free(path);
        return;
    }

    if (is_video_file(path)) {
        /* Stop any static wallpaper rendering first */
        if (wallpaper_pixbuf) {
            g_object_unref(wallpaper_pixbuf);
            wallpaper_pixbuf = NULL;
        }
        if (prev_wallpaper_pixbuf) {
            g_object_unref(prev_wallpaper_pixbuf);
            prev_wallpaper_pixbuf = NULL;
        }
        video_wallpaper_load(path);
    } else {
        /* Switching back from video to image */
        if (video_wallpaper_is_active())
            video_wallpaper_stop();
        load_wallpaper(path);
    }

    g_free(path);
}

static GdkMonitor *get_target_monitor(GdkDisplay *display) {
    GdkMonitor *monitor;

    if (!display) return NULL;

    monitor = gdk_display_get_primary_monitor(display);
    if (monitor) return monitor;

    if (gdk_display_get_n_monitors(display) > 0)
        return gdk_display_get_monitor(display, 0);

    return NULL;
}

gboolean desktop_has_available_monitor(void) {
    GdkDisplay *display = gdk_display_get_default();

    if (!display) return FALSE;
    return get_target_monitor(display) != NULL;
}

static void update_desktop_geometry(void) {
    GdkDisplay *display = gdk_display_get_default();
    GdkMonitor *monitor;
    GdkRectangle r;

    if (!display || !main_window || !icon_layout) return;

    monitor = get_target_monitor(display);
    if (!monitor) return;

    gdk_monitor_get_geometry(monitor, &r);
    if (r.width <= 0 || r.height <= 0) return;

    screen_w = r.width;
    screen_h = r.height;

    if (is_wayland_session()) {
        gtk_layer_set_monitor(GTK_WINDOW(main_window), monitor);
    } else {
        gtk_window_move(GTK_WINDOW(main_window), r.x, r.y);
    }
    gtk_window_set_default_size(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_window_resize(GTK_WINDOW(main_window), screen_w, screen_h);
    gtk_widget_set_size_request(icon_layout, screen_w, screen_h);
    gtk_layout_set_size(GTK_LAYOUT(icon_layout), screen_w, screen_h);

    if (current_wallpaper_path) load_wallpaper(current_wallpaper_path);
}

static void on_monitors_changed(GdkDisplay *display, gpointer user_data) {
    (void)display;
    (void)user_data;
    update_desktop_geometry();
}

static void on_main_window_realize(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    update_desktop_geometry();
    g_idle_add(refresh_desktop_after_show, NULL);
}

static gboolean refresh_desktop_after_show(gpointer user_data) {
    (void)user_data;
    update_desktop_geometry();
    if (icon_layout) gtk_widget_queue_draw(icon_layout);
    return G_SOURCE_REMOVE;
}

gboolean on_layout_draw_bg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* If a video wallpaper is active, draw it and skip static image */
    if (video_wallpaper_is_active()) {
        video_wallpaper_draw(cr);
        return FALSE;
    }
    /* Static image wallpaper */
    if (wallpaper_pixbuf) {
        gdk_cairo_set_source_pixbuf(cr, wallpaper_pixbuf, 0, 0);
        cairo_paint(cr);
    }

    if (prev_wallpaper_pixbuf && wallpaper_transition_alpha < 1.0) {
        double progress = wallpaper_transition_alpha;

        if (current_anim_type == 0) {
            // 0: Sliding Doors
            double offset = (screen_w / 2.0) * progress;

            if (progress > 0.0) {
                cairo_pattern_t *sh_left = cairo_pattern_create_linear(screen_w / 2.0 - offset, 0, screen_w / 2.0 - offset + 60, 0);
                cairo_pattern_add_color_stop_rgba(sh_left, 0.0, 0, 0, 0, 0.7 * (1.0 - progress));
                cairo_pattern_add_color_stop_rgba(sh_left, 1.0, 0, 0, 0, 0.0);
                cairo_rectangle(cr, screen_w / 2.0 - offset, 0, 60, screen_h);
                cairo_set_source(cr, sh_left);
                cairo_fill(cr);
                cairo_pattern_destroy(sh_left);

                cairo_pattern_t *sh_right = cairo_pattern_create_linear(screen_w / 2.0 + offset - 60, 0, screen_w / 2.0 + offset, 0);
                cairo_pattern_add_color_stop_rgba(sh_right, 0.0, 0, 0, 0, 0.0);
                cairo_pattern_add_color_stop_rgba(sh_right, 1.0, 0, 0, 0, 0.7 * (1.0 - progress));
                cairo_rectangle(cr, screen_w / 2.0 + offset - 60, 0, 60, screen_h);
                cairo_set_source(cr, sh_right);
                cairo_fill(cr);
                cairo_pattern_destroy(sh_right);
            }

            cairo_save(cr);
            cairo_translate(cr, -offset, 0);
            cairo_rectangle(cr, 0, 0, screen_w / 2.0, screen_h);
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

            cairo_save(cr);
            cairo_translate(cr, offset, 0);
            cairo_rectangle(cr, screen_w / 2.0, 0, screen_w / 2.0, screen_h);
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

        } else if (current_anim_type == 1) {
            // 1: Circle Reveal
            double max_radius = sqrt(screen_w*screen_w/4.0 + screen_h*screen_h/4.0);
            double r = max_radius * progress;
            
            cairo_save(cr);
            cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
            cairo_rectangle(cr, 0, 0, screen_w, screen_h);
            cairo_arc(cr, screen_w/2.0, screen_h/2.0, r, 0, 2 * G_PI);
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);

            if (r > 0) {
                cairo_pattern_t *sh = cairo_pattern_create_radial(
                    screen_w/2.0, screen_h/2.0, MAX(0, r - 60.0),
                    screen_w/2.0, screen_h/2.0, r);
                cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.0);
                cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.7 * (1.0 - progress));
                
                cairo_save(cr);
                cairo_arc(cr, screen_w/2.0, screen_h/2.0, r, 0, 2 * G_PI);
                cairo_clip(cr);
                cairo_set_source(cr, sh);
                cairo_paint(cr);
                cairo_restore(cr);
                cairo_pattern_destroy(sh);
            }
        } else if (current_anim_type == 2) {
            // 2: Smooth Crossfade
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint_with_alpha(cr, 1.0 - progress);

        } else if (current_anim_type == 3) {
            // 3: Wipe Right
            double wipe_x = screen_w * progress;
            
            cairo_save(cr);
            cairo_rectangle(cr, wipe_x, 0, screen_w - wipe_x, screen_h);
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
            
            if (wipe_x > 0 && wipe_x < screen_w) {
                cairo_pattern_t *sh = cairo_pattern_create_linear(wipe_x, 0, wipe_x + 60, 0);
                cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.5 * (1.0 - progress));
                cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.0);
                cairo_rectangle(cr, wipe_x, 0, 60, screen_h);
                cairo_set_source(cr, sh);
                cairo_fill(cr);
                cairo_pattern_destroy(sh);
            }
        } else if (current_anim_type == 4) {
            // 4: Zoom Out & Fade
            cairo_save(cr);
            cairo_translate(cr, screen_w/2.0, screen_h/2.0);
            double scale = 1.0 - (0.5 * progress);
            cairo_scale(cr, scale, scale);
            cairo_translate(cr, -screen_w/2.0, -screen_h/2.0);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint_with_alpha(cr, 1.0 - progress);
            cairo_restore(cr);
        } else if (current_anim_type == 5) {
            // 5: Blinds
            int num_blinds = 12;
            double blind_h = (double)screen_h / num_blinds;
            cairo_save(cr);
            for (int i = 0; i < num_blinds; i++) {
                double h = blind_h * (1.0 - progress);
                double y = i * blind_h + (blind_h - h) / 2.0;
                cairo_rectangle(cr, 0, y, screen_w, h);
            }
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else if (current_anim_type == 6) {
            // 6: Swipe Up
            double y_offset = screen_h * progress;
            cairo_save(cr);
            cairo_translate(cr, 0, -y_offset);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            
            if (y_offset > 0 && progress < 1.0) {
                cairo_pattern_t *sh = cairo_pattern_create_linear(0, screen_h, 0, screen_h + 80);
                cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.8 * (1.0 - progress));
                cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.0);
                cairo_rectangle(cr, 0, screen_h, screen_w, 80);
                cairo_set_source(cr, sh);
                cairo_fill(cr);
                cairo_pattern_destroy(sh);
            }
            cairo_restore(cr);
        } else if (current_anim_type == 7) {
            // 7: Grid/Mosaic
            int cols = 16;
            int rows = 9;
            double cell_w = (double)screen_w / cols;
            double cell_h = (double)screen_h / rows;
            
            cairo_save(cr);
            for (int r = 0; r < rows; r++) {
                for (int c = 0; c < cols; c++) {
                    double delay = ((double)(r + c) / (rows + cols)) * 0.4;
                    double cell_p = (progress - delay) / 0.6;
                    if (cell_p < 0.0) cell_p = 0.0;
                    if (cell_p > 1.0) cell_p = 1.0;
                    
                    double scale = 1.0 - cell_p;
                    if (scale > 0) {
                        double cx = c * cell_w + cell_w / 2.0;
                        double cy = r * cell_h + cell_h / 2.0;
                        double w = cell_w * scale;
                        double h = cell_h * scale;
                        cairo_rectangle(cr, cx - w/2.0, cy - h/2.0, w, h);
                    }
                }
            }
            cairo_clip(cr);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            cairo_restore(cr);
        } else if (current_anim_type == 8) {
            // 8: Diagonal Wipe
            double diag_dist = sqrt(screen_w*screen_w + screen_h*screen_h);
            double dist = diag_dist * progress;
            double angle = atan2(screen_h, screen_w);
            
            cairo_save(cr);
            cairo_rotate(cr, angle);
            cairo_rectangle(cr, dist, -diag_dist, diag_dist, diag_dist * 2);
            cairo_rotate(cr, -angle);
            cairo_clip(cr);
            
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint(cr);
            
            cairo_rotate(cr, angle);
            if (dist > 0 && progress < 1.0) {
                cairo_pattern_t *sh = cairo_pattern_create_linear(dist, 0, dist + 100, 0);
                cairo_pattern_add_color_stop_rgba(sh, 0.0, 0, 0, 0, 0.7 * (1.0 - progress));
                cairo_pattern_add_color_stop_rgba(sh, 1.0, 0, 0, 0, 0.0);
                cairo_rectangle(cr, dist, -diag_dist, 100, diag_dist * 2);
                cairo_set_source(cr, sh);
                cairo_fill(cr);
                cairo_pattern_destroy(sh);
            }
            cairo_restore(cr);
        } else if (current_anim_type == 9) {
            // 9: Spin & Fade
            cairo_save(cr);
            cairo_translate(cr, screen_w/2.0, screen_h/2.0);
            cairo_rotate(cr, progress * G_PI); 
            double scale = 1.0 - progress;
            cairo_scale(cr, scale, scale);
            cairo_translate(cr, -screen_w/2.0, -screen_h/2.0);
            gdk_cairo_set_source_pixbuf(cr, prev_wallpaper_pixbuf, 0, 0);
            cairo_paint_with_alpha(cr, 1.0 - progress);
            cairo_restore(cr);
        }
    }

    return FALSE;
}

void init_main_window(void) {
    GdkVisual *visual;
    GdkScreen *screen;

    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "vaxp Pro Desktop");
    gtk_window_set_decorated(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(main_window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(main_window), FALSE);

    if (is_wayland_session()) {
        gtk_layer_init_for_window(GTK_WINDOW(main_window));
        gtk_layer_set_namespace(GTK_WINDOW(main_window), "desktop");
        gtk_layer_set_layer(GTK_WINDOW(main_window), GTK_LAYER_SHELL_LAYER_BACKGROUND);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
        gtk_layer_set_anchor(GTK_WINDOW(main_window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
        gtk_layer_set_keyboard_mode(GTK_WINDOW(main_window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(GTK_WINDOW(main_window), -1);
    } else {
        gtk_window_set_type_hint(GTK_WINDOW(main_window), GDK_WINDOW_TYPE_HINT_DESKTOP);
        gtk_window_set_keep_below(GTK_WINDOW(main_window), TRUE);
        gtk_window_stick(GTK_WINDOW(main_window));
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(main_window), TRUE);
        gtk_window_set_skip_pager_hint(GTK_WINDOW(main_window), TRUE);
    }

    screen = gtk_widget_get_screen(main_window);
    visual = gdk_screen_get_rgba_visual(screen);
    if (visual && gdk_screen_is_composited(screen)) {
        gtk_widget_set_visual(main_window, visual);
        gtk_widget_set_app_paintable(main_window, TRUE);
    }

    /* Create icon layout (desktop icons / menus layer) */
    icon_layout = gtk_layout_new(NULL, NULL);
    gtk_widget_set_app_paintable(icon_layout, TRUE);
    gtk_widget_add_events(icon_layout,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);

    if (!monitor_signal_handlers_connected) {
        GdkDisplay *display = gdk_display_get_default();

        if (display) {
            g_signal_connect(display, "monitor-added", G_CALLBACK(on_monitors_changed), NULL);
            g_signal_connect(display, "monitor-removed", G_CALLBACK(on_monitors_changed), NULL);
            monitor_signal_handlers_connected = TRUE;
        }
    }

    g_signal_connect(main_window, "realize", G_CALLBACK(on_main_window_realize), NULL);
    g_signal_connect(icon_layout, "draw", G_CALLBACK(on_layout_draw_bg), NULL);

    /*
     * Initialise the video wallpaper subsystem (SW render — no GtkGLArea).
     * If it fails we continue normally with static image wallpapers.
     */
    video_wallpaper_init(icon_layout);

    gtk_container_add(GTK_CONTAINER(main_window), icon_layout);
    update_desktop_geometry();
}



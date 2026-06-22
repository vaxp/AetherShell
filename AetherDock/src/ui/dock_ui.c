#include "ui/dock_ui.h"
#include <math.h>
#include "ui/dock_config.h"
#include "ui/x11_integration.h"
#include "ui/context_menu.h"
#include "ui/app_tracker.h"
#include "utils.h"
#include "launcher.h"
#include "trash.h"
#include "drives.h"
#include "logic/app_manager.h"

typedef struct {
    gchar *desktop_file_path;
} LaunchTimeoutData;

static gboolean clear_stale_launch_state(gpointer data);

static gboolean on_launch_ring_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    gdouble *time_ptr = g_object_get_data(G_OBJECT(widget), "ring-angle");
    if (!time_ptr) return FALSE;
    gdouble time = *time_ptr;

    gint width = gtk_widget_get_allocated_width(widget);
    gint height = gtk_widget_get_allocated_height(widget);
    gdouble radius = MIN(width, height) / 2.0 - 3.0;
    gdouble cx = width / 2.0;
    gdouble cy = height / 2.0;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    switch (current_launch_animation) {
        case 2: { /* Pulse Ripple */
            gdouble progress = fmod(time * 0.4, 1.0); /* 0.0 to 1.0 loop */
            gdouble current_radius = radius * 0.4 + (radius * 0.6 * progress);
            gdouble alpha = 1.0 - progress;
            
            cairo_arc(cr, cx, cy, current_radius, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, alpha);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
            break;
        }
        case 3: { /* Orbiting Dots */
            gdouble speed = time * 0.8;
            for (int i = 0; i < 3; i++) {
                gdouble angle_offset = i * (2 * G_PI / 3.0);
                gdouble dot_x = cx + cos(speed + angle_offset) * radius;
                gdouble dot_y = cy + sin(speed + angle_offset) * radius;
                cairo_arc(cr, dot_x, dot_y, 2.5, 0, 2 * G_PI);
                cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 1.0 - (i * 0.2));
                cairo_fill(cr);
            }
            break;
        }
        case 4: { /* Radar Sweep */
            gdouble speed = time * 0.6;
            /* Background circle */
            cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.1);
            cairo_fill(cr);
            /* Sweep slice */
            cairo_move_to(cr, cx, cy);
            cairo_arc(cr, cx, cy, radius, speed, speed + G_PI / 2.0);
            cairo_close_path(cr);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.4);
            cairo_fill(cr);
            /* Leading edge line */
            cairo_move_to(cr, cx, cy);
            cairo_line_to(cr, cx + cos(speed + G_PI / 2.0) * radius, cy + sin(speed + G_PI / 2.0) * radius);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 1.0);
            cairo_set_line_width(cr, 1.5);
            cairo_stroke(cr);
            break;
        }
        case 5: { /* Dashed Chase */
            gdouble speed = time * 1.5;
            double dashes[] = { 6.0, 6.0 };
            cairo_set_dash(cr, dashes, 2, -speed * 10.0);
            cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.9);
            cairo_set_line_width(cr, 2.0);
            cairo_stroke(cr);
            break;
        }
        case 6: { /* Pendulum Bounce */
            gdouble swing = sin(time * 0.8); /* -1.0 to 1.0 */
            gdouble dot_x = cx + swing * radius;
            gdouble dot_y = cy + radius; /* Bottom edge */
            
            /* Draw a faded track */
            cairo_move_to(cr, cx - radius, cy + radius);
            cairo_line_to(cr, cx + radius, cy + radius);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.2);
            cairo_set_line_width(cr, 1.0);
            cairo_stroke(cr);

            /* Draw the pendulum dot */
            cairo_arc(cr, dot_x, dot_y, 3.0, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 1.0);
            cairo_fill(cr);
            break;
        }
        case 7: { /* Breathing Halo */
            gdouble scale = (sin(time * 0.5) + 1.0) / 2.0; /* 0.0 to 1.0 */
            gdouble current_radius = radius - 1.0 + (scale * 2.0);
            
            cairo_arc(cr, cx, cy, current_radius, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.3 + (scale * 0.7));
            cairo_set_line_width(cr, 1.0 + (scale * 3.0));
            cairo_stroke(cr);
            break;
        }
        case 1:
        default: { /* Spinner (Current) */
            gdouble start = time * 2.2; /* Adjusted to match original 0.22 speed */
            gdouble end = start + (G_PI * 1.15);

            cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.13);
            cairo_set_line_width(cr, 1.4);
            cairo_stroke(cr);

            cairo_arc(cr, cx, cy, radius, start, end);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.95);
            cairo_set_line_width(cr, 2.2);
            cairo_stroke(cr);

            cairo_arc(cr, cx, cy, radius, start, end);
            cairo_set_source_rgba(cr, current_launch_ring_color.red, current_launch_ring_color.green, current_launch_ring_color.blue, 0.28);
            cairo_set_line_width(cr, 5.4);
            cairo_stroke(cr);
            break;
        }
    }

    return FALSE;
}

static gboolean rotate_launch_ring(gpointer data) {
    GtkWidget *ring = GTK_WIDGET(data);
    if (!GTK_IS_WIDGET(ring) || !gtk_widget_get_visible(ring)) {
        return G_SOURCE_REMOVE;
    }

    gdouble *angle = g_object_get_data(G_OBJECT(ring), "ring-angle");
    if (!angle) return G_SOURCE_REMOVE;

    *angle += 0.1; /* Generic time increment */

    gtk_widget_queue_draw(ring);
    return G_SOURCE_CONTINUE;
}

static void on_launch_ring_destroy(GtkWidget *widget, gpointer data) {
    (void)data;
    guint timeout_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "ring-timeout-id"));
    if (timeout_id != 0) {
        g_source_remove(timeout_id);
        g_object_set_data(G_OBJECT(widget), "ring-timeout-id", NULL);
    }
}

static GtkWidget *create_launch_ring(void) {
    GtkWidget *ring = gtk_drawing_area_new();
    gdouble *angle = g_new0(gdouble, 1);

    gtk_widget_set_name(ring, "launch-ring");
    gtk_widget_set_size_request(ring, 40, 40);
    gtk_widget_set_halign(ring, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(ring, GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus(ring, FALSE);

    g_object_set_data_full(G_OBJECT(ring), "ring-angle", angle, g_free);
    g_signal_connect(ring, "draw", G_CALLBACK(on_launch_ring_draw), NULL);
    g_signal_connect(ring, "destroy", G_CALLBACK(on_launch_ring_destroy), NULL);

    guint timeout_id = g_timeout_add(16, rotate_launch_ring, ring);
    g_object_set_data(G_OBJECT(ring), "ring-timeout-id", GUINT_TO_POINTER(timeout_id));

    return ring;
}

static void attach_launch_ring(GtkWidget *button) {
    GtkWidget *overlay = gtk_bin_get_child(GTK_BIN(button));
    GtkWidget *ring = g_object_get_data(G_OBJECT(button), "launch-ring");

    if (!GTK_IS_OVERLAY(overlay) || ring != NULL) {
        return;
    }

    ring = create_launch_ring();
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), ring);
    gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay), ring, TRUE);
    g_object_set_data(G_OBJECT(button), "launch-ring", ring);
    gtk_widget_show(ring);
}

static void mark_app_launching(const gchar *desktop_file_path) {
    LaunchTimeoutData *timeout_data;

    if (!desktop_file_path || !launching_apps) return;
    if (g_hash_table_contains(launching_apps, desktop_file_path)) return;

    g_hash_table_insert(launching_apps, g_strdup(desktop_file_path), GINT_TO_POINTER(1));

    timeout_data = g_new0(LaunchTimeoutData, 1);
    timeout_data->desktop_file_path = g_strdup(desktop_file_path);
    g_timeout_add_seconds(12, clear_stale_launch_state, timeout_data);
}

static gboolean clear_stale_launch_state(gpointer data) {
    LaunchTimeoutData *timeout_data = (LaunchTimeoutData *)data;

    if (launching_apps && g_hash_table_remove(launching_apps, timeout_data->desktop_file_path)) {
        update_window_list();
    }

    g_free(timeout_data->desktop_file_path);
    g_free(timeout_data);
    return G_SOURCE_REMOVE;
}

static gboolean is_app_launching(const gchar *desktop_file_path) {
    return desktop_file_path && launching_apps &&
           g_hash_table_contains(launching_apps, desktop_file_path);
}

static void unmark_app_launching(const gchar *desktop_file_path) {
    if (!desktop_file_path || !launching_apps) return;
    g_hash_table_remove(launching_apps, desktop_file_path);
}

/* Window size allocate callback for centering and shaping */
void on_window_size_allocate(GtkWidget *widget, GtkAllocation *allocation, gpointer data) {
    (void)data;
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;
    
    /* Create a shape mask for true X11 rounding (blur respect) */
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_A1, allocation->width, allocation->height);
    cairo_t *cr = cairo_create(surface);
    
    double radius = 14.0; /* Match CSS border-radius */
    
    /* Draw rounded rectangle mask */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.3);
    cairo_paint(cr);
    
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 1.0);
    cairo_new_sub_path(cr);
    cairo_arc(cr, allocation->width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, allocation->width - radius, allocation->height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, allocation->height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_fill(cr);
    
    /* Create region from surface and apply as shape to window */
    cairo_region_t *region = gdk_cairo_region_create_from_surface(surface);
    gtk_widget_shape_combine_region(widget, region);
    
    cairo_region_destroy(region);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    
    /* Force redraw so Cairo shape updates when window shrinks/expands */
    gtk_widget_queue_draw(widget);
    
    GdkDisplay *display = gdk_window_get_display(gdk_window);
    GdkRectangle geometry;

    if (get_primary_monitor_geometry(display, &geometry)) {
        int x, y;
        if (current_dock_position == DOCK_POSITION_TOP) {
            x = geometry.x + (geometry.width - allocation->width) / 2;
            y = geometry.y + 2;
        } else if (current_dock_position == DOCK_POSITION_LEFT) {
            x = geometry.x + 2;
            y = geometry.y + (geometry.height - allocation->height) / 2;
        } else if (current_dock_position == DOCK_POSITION_RIGHT) {
            x = geometry.x + geometry.width - allocation->width - 2;
            y = geometry.y + (geometry.height - allocation->height) / 2;
        } else {
            x = geometry.x + (geometry.width - allocation->width) / 2;
            y = geometry.y + geometry.height - allocation->height - 2;
        }

        gtk_window_move(GTK_WINDOW(widget), x, y);
    }
}

gboolean on_window_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    /* Create a rounded rectangle shape for the window itself */
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    double radius = 14.0; /* Match CSS border-radius */

    /* Clear the background completely */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);

    /* Draw the rounded mask */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_new_sub_path(cr);
    cairo_arc(cr, width - radius, radius, radius, -G_PI/2, 0);
    cairo_arc(cr, width - radius, height - radius, radius, 0, G_PI/2);
    cairo_arc(cr, radius, height - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, radius, radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    
    /* Paint it with a solid, but since it's RGBA visual it will allow the CSS background through */
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_fill(cr);

    return FALSE; /* Let GTK draw the CSS over our cairo mask */
}

/* Activate window on click - cycles through grouped windows */
void on_button_clicked(GtkWidget *widget, gpointer data) {
    (void)data; 
    
    WindowGroup *group = (WindowGroup *)g_object_get_data(G_OBJECT(widget), "group");
    if (group == NULL) return;
    
    int window_count = g_list_length(group->windows);
    
    /* 1. Launch Logic for Pinned Apps */
    if (window_count == 0) {
        if (group->desktop_file_path != NULL) {
            mark_app_launching(group->desktop_file_path);
            attach_launch_ring(widget);

            GError *error = NULL;
            if (!app_mgr_launch(group->desktop_file_path, &error)) {
                unmark_app_launching(group->desktop_file_path);
                g_warning("Failed to launch app: %s", error ? error->message : "Unknown error");
                if (error) g_error_free(error);
                update_window_list();
            }
        }
        return;
    }

    if (is_wayland_session) {
        gboolean app_is_active = FALSE;
        WaylandToplevel *target_item = NULL;

        for (GList *l = group->windows; l != NULL; l = l->next) {
            WaylandToplevel *item = (WaylandToplevel *)l->data;
            if (item->state_flags & WAYLAND_TOPLEVEL_STATE_ACTIVATED) {
                app_is_active = TRUE;
                break;
            }
        }

        if (window_count == 1) {
            target_item = (WaylandToplevel *)group->windows->data;
            if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_ACTIVATED) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                } else {
                    zwlr_foreign_toplevel_handle_v1_set_minimized(target_item->handle);
                }
            } else if (wl_seat_obj) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                }
                zwlr_foreign_toplevel_handle_v1_activate(target_item->handle, wl_seat_obj);
            }
        } else {
            if (app_is_active) {
                group->active_index = (group->active_index + 1) % window_count;
            }

            GList *window_node = g_list_nth(group->windows, group->active_index);
            if (window_node == NULL) {
                group->active_index = 0;
                window_node = g_list_first(group->windows);
            }

            target_item = window_node ? (WaylandToplevel *)window_node->data : NULL;
            if (target_item != NULL && wl_seat_obj) {
                if (target_item->state_flags & WAYLAND_TOPLEVEL_STATE_MINIMIZED) {
                    zwlr_foreign_toplevel_handle_v1_unset_minimized(target_item->handle);
                }
                zwlr_foreign_toplevel_handle_v1_activate(target_item->handle, wl_seat_obj);
            }
        }

        if (wl_display_conn) {
            wl_display_flush(wl_display_conn);
        }
        return;
    }
}

gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { /* Right click */
        WindowGroup *group = (WindowGroup *)g_object_get_data(G_OBJECT(widget), "group");
        if (group != NULL) {
            show_context_menu(widget, event, group);
            return TRUE;
        }
    }
    return FALSE;
}

void update_window_list() {
    /* Clear existing buttons (except permanent right-side widgets) */
    GList *children, *iter;
    children = gtk_container_get_children(GTK_CONTAINER(box));
    for (iter = children; iter != NULL; iter = g_list_next(iter)) {
        GtkWidget *child = GTK_WIDGET(iter->data);
        /* Preserve: launcher button, trash button, drives area */
        if (child != launcher_button &&
            child != trash_button &&
            child != drives_box &&
            child != system_separator) {
            gtk_widget_destroy(child);
        }
    }
    g_list_free(children);

    /* Clear window lists from groups, but keep pinned apps */
    if (window_groups != NULL) {
        GHashTableIter hash_iter;
        gpointer key, value;
        g_hash_table_iter_init(&hash_iter, window_groups);
        while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
            WindowGroup *group = (WindowGroup *)value;
            
            /* Clear windows list */
            if (group->windows) {
                g_list_free(group->windows);
                group->windows = NULL;
            }
            
            /* Remove unpinned groups */
            if (!group->is_pinned) {
                if (group->icon) {
                    g_object_unref(group->icon);
                }
                if (group->desktop_file_path) {
                    g_free(group->desktop_file_path);
                }
                g_free(group->wm_class);
                g_free(group);
                g_hash_table_iter_remove(&hash_iter);
            }
        }
    }

    if (is_wayland_session) {
        if (wayland_toplevels != NULL) {
            GHashTableIter wayland_iter;
            gpointer wayland_key, wayland_value;

            g_hash_table_iter_init(&wayland_iter, wayland_toplevels);
            while (g_hash_table_iter_next(&wayland_iter, &wayland_key, &wayland_value)) {
                WaylandToplevel *item = (WaylandToplevel *)wayland_value;
                const gchar *app_id = (item->app_id && *item->app_id) ? item->app_id : "unknown";
                WindowGroup *group = g_hash_table_lookup(window_groups, app_id);

                if (group == NULL) {
                    group = g_malloc0(sizeof(WindowGroup));
                    group->wm_class = g_strdup(app_id);
                    group->icon = icon_from_app_id(app_id);
                    group->desktop_file_path = desktop_file_path_from_app_id(app_id);
                    group->active_index = 0;
                    group->is_pinned = pinned_app_contains(app_id);
                    g_hash_table_insert(window_groups, g_strdup(app_id), group);
                }

                group->windows = g_list_append(group->windows, item);
            }
        }

        for (GList *l = pinned_apps; l != NULL; l = l->next) {
            const gchar *pinned_app_id = (const gchar *)l->data;
            gchar *normalized_pinned_app_id = normalize_app_id(pinned_app_id);

            if (g_hash_table_lookup(window_groups, normalized_pinned_app_id) == NULL) {
                WindowGroup *group = g_malloc0(sizeof(WindowGroup));
                group->wm_class = g_strdup(normalized_pinned_app_id);
                group->windows = NULL;
                group->active_index = 0;
                group->is_pinned = TRUE;
                group->desktop_file_path = desktop_file_path_from_app_id(normalized_pinned_app_id);
                group->icon = icon_from_app_id(normalized_pinned_app_id);
                g_hash_table_insert(window_groups, g_strdup(normalized_pinned_app_id), group);
            }

            g_free(normalized_pinned_app_id);
        }

        GHashTableIter hash_iter;
        gpointer key, value;
        g_hash_table_iter_init(&hash_iter, window_groups);

        while (g_hash_table_iter_next(&hash_iter, &key, &value)) {
            WindowGroup *group = (WindowGroup *)value;
            GtkWidget *button = gtk_button_new();
            int window_count = g_list_length(group->windows);

            gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
            group->button = button;

            if (window_count > 0 && group->desktop_file_path) {
                unmark_app_launching(group->desktop_file_path);
            }

            if (window_count > 1) {
                char tooltip[256];
                snprintf(tooltip, sizeof(tooltip), "%s (%d windows)", group->wm_class, window_count);
                gtk_widget_set_tooltip_text(button, tooltip);
            } else if (window_count == 1) {
                WaylandToplevel *item = (WaylandToplevel *)g_list_first(group->windows)->data;
                gtk_widget_set_tooltip_text(button,
                                            (item->title && *item->title) ? item->title : group->wm_class);
            } else {
                gtk_widget_set_tooltip_text(button, group->wm_class);
            }

            GtkWidget *overlay = gtk_overlay_new();

            if (group->icon) {
                GdkPixbuf *rounded = create_rounded_icon_pixbuf(group->icon, 34, 8.0);
                GtkWidget *image = gtk_image_new_from_pixbuf(rounded ? rounded : group->icon);
                gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
                gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
                gtk_container_add(GTK_CONTAINER(overlay), image);
                if (rounded) g_object_unref(rounded);
            } else {
                GtkWidget *label = gtk_label_new("?");
                gtk_widget_set_valign(label, GTK_ALIGN_CENTER);
                gtk_widget_set_halign(label, GTK_ALIGN_CENTER);
                gtk_container_add(GTK_CONTAINER(overlay), label);
            }

            GtkWidget *dot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_name(dot, "indicator-dot");
            gtk_widget_set_size_request(dot, 6, 6);
            gtk_widget_set_halign(dot, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(dot, GTK_ALIGN_END);
            gtk_widget_set_margin_bottom(dot, 2);
            gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dot);
            gtk_container_add(GTK_CONTAINER(button), overlay);

            if (window_count == 0 && is_app_launching(group->desktop_file_path)) {
                attach_launch_ring(button);
            }

            g_object_set_data(G_OBJECT(button), "group", group);
            g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), NULL);
            g_signal_connect(button, "button-press-event", G_CALLBACK(on_button_press), NULL);

            GtkStyleContext *context = gtk_widget_get_style_context(button);
            if (window_count > 0) {
                gtk_style_context_add_class(context, "running-app");
            } else {
                gtk_style_context_add_class(context, "pinned-app");
            }

            gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
        }

        gtk_widget_show_all(box);
        gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
        return;
    }
    gtk_widget_show_all(box);
    
    /* Force main window to recalculate size and shrink if needed */
    gtk_window_resize(GTK_WINDOW(main_window), 1, 1);
}

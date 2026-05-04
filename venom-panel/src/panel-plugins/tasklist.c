#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gio/gdesktopappinfo.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include "venom-panel-plugin-api.h"

/* ============================================================================
 * WINDOW GROUPING TASKLIST PLUGIN
 * Groups multiple windows of the same application into a single button.
 * Click cycles through windows, right-click shows all windows in a menu.
 * ============================================================================ */

typedef struct _AppGroup AppGroup;
typedef struct _TasklistData TasklistData;

/* Represents a single window */
typedef struct {
    Window xid;
    char *title;
    char *wm_class;
    unsigned long desktop;
    gboolean is_active;
    gboolean is_minimized;
    GdkPixbuf *icon;
} WindowInfo;

/* Represents a group of windows belonging to the same application */
struct _AppGroup {
    char *app_id;           /* WM_CLASS (lowercase) used for grouping */
    char *display_name;     /* Human readable app name */
    GdkPixbuf *icon;        /* App icon (shared among windows) */
    GList *windows;         /* List of WindowInfo* */
    int active_index;       /* Index of currently active window, or -1 */
    GtkWidget *button;      /* The toggle button for this group */
    TasklistData *parent;   /* Back-reference to parent */
};

struct _TasklistData {
    GtkWidget *box;
    guint timer_id;
    Display *dpy;
    Window root;
    Window active_win;
    
    /* Grouped application data */
    GHashTable *groups;         /* app_id -> AppGroup* */
    GList *group_list;          /* Ordered list of AppGroup* for display */
    
    /* Cache */
    Window *client_list;
    int num_clients;
    unsigned long current_desktop;
    
    /* Settings */
    gboolean group_windows;     /* Whether to group windows (default: TRUE) */
    gboolean show_window_count; /* Show count badge (default: TRUE) */
};

/* ============================================================================
 * X11 Helper Functions
 * ============================================================================ */

static Window* get_x11_prop_windows(Display *dpy, Window win, const char *prop_name, int *count_out) {
    Atom prop = XInternAtom(dpy, prop_name, True);
    if (prop == None) { *count_out = 0; return NULL; }
    
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop_retval = NULL;
    
    int status = XGetWindowProperty(dpy, win, prop, 0, 1024, False, AnyPropertyType,
                                    &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval);
    
    if (status == Success && prop_retval && actual_format == 32) {
        *count_out = (int)nitems;
        Window *windows = g_memdup2(prop_retval, nitems * sizeof(Window));
        XFree(prop_retval);
        return windows;
    }
    
    if (prop_retval) XFree(prop_retval);
    *count_out = 0;
    return NULL;
}

static unsigned long get_x11_prop_cardinal(Display *dpy, Window win, const char *prop_name, unsigned long fallback_val) {
    Atom prop = XInternAtom(dpy, prop_name, True);
    if (prop == None) return fallback_val;
    
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop_retval = NULL;
    
    int status = XGetWindowProperty(dpy, win, prop, 0, 1, False, XA_CARDINAL,
                                    &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval);
    
    unsigned long result = fallback_val;
    if (status == Success && prop_retval && actual_format == 32 && nitems > 0) {
        result = ((unsigned long*)prop_retval)[0];
    }
    
    if (prop_retval) XFree(prop_retval);
    return result;
}

static char* get_x11_prop_string(Display *dpy, Window win, const char *prop_name) {
    Atom prop = XInternAtom(dpy, prop_name, True);
    if (prop == None) return NULL;
    
    Atom actual_type;
    int actual_format;
    unsigned long nitems;
    unsigned long bytes_after;
    unsigned char *prop_retval = NULL;
    
    int status = XGetWindowProperty(dpy, win, prop, 0, 1024, False, XInternAtom(dpy, "UTF8_STRING", False),
                                    &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval);
                                    
    if (status != Success || prop_retval == NULL) {
        status = XGetWindowProperty(dpy, win, prop, 0, 1024, False, XA_STRING,
                                    &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval);
    }
    
    char *result = NULL;
    if (status == Success && prop_retval) {
        result = g_strdup((char*)prop_retval);
        XFree(prop_retval);
    }
    return result;
}

static gboolean is_normal_window(Display *dpy, Window win) {
    XWindowAttributes attrs;
    if (!XGetWindowAttributes(dpy, win, &attrs)) return FALSE;
    if (attrs.override_redirect) return FALSE;

    /* Check _NET_WM_STATE for SKIP_TASKBAR */
    Atom state_prop = XInternAtom(dpy, "_NET_WM_STATE", True);
    Atom skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", True);
    if (state_prop != None && skip_taskbar != None) {
        Atom actual_type; int actual_format; unsigned long nitems; unsigned long bytes_after;
        unsigned char *prop_retval = NULL;
        if (XGetWindowProperty(dpy, win, state_prop, 0, 1024, False, XA_ATOM,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval) == Success && prop_retval) {
            Atom *atoms = (Atom*)prop_retval;
            gboolean skip = FALSE;
            for (unsigned long i = 0; i < nitems; i++) {
                if (atoms[i] == skip_taskbar) { skip = TRUE; break; }
            }
            XFree(prop_retval);
            if (skip) return FALSE;
        } else if (prop_retval) {
            XFree(prop_retval);
        }
    }

    /* Check _NET_WM_WINDOW_TYPE */
    Atom type_prop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
    if (type_prop == None) return TRUE; 
    
    Atom normal = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NORMAL", True);
    Atom dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", True);
    Atom dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", True);
    Atom desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", True);
    Atom utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", True);
    
    Atom actual_type; int actual_format; unsigned long nitems; unsigned long bytes_after;
    unsigned char *prop_retval = NULL;
    
    if (XGetWindowProperty(dpy, win, type_prop, 0, 1024, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval) == Success && prop_retval && actual_format == 32) {
        Atom *atoms = (Atom*)prop_retval;
        gboolean is_normal = FALSE;
        gboolean is_dock = FALSE;
        
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == normal || atoms[i] == dialog) is_normal = TRUE;
            if (atoms[i] == dock || atoms[i] == desktop || atoms[i] == utility) is_dock = TRUE;
        }
        XFree(prop_retval);
        
        if (is_dock) return FALSE;
        if (is_normal) return TRUE;
    } else if (prop_retval) {
        XFree(prop_retval);
    }
    
    return TRUE;
}

static gboolean is_window_minimized(Display *dpy, Window win) {
    Atom state_prop = XInternAtom(dpy, "_NET_WM_STATE", True);
    Atom hidden = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", True);
    
    if (state_prop == None || hidden == None) return FALSE;
    
    Atom actual_type; int actual_format; unsigned long nitems; unsigned long bytes_after;
    unsigned char *prop_retval = NULL;
    
    if (XGetWindowProperty(dpy, win, state_prop, 0, 1024, False, XA_ATOM,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval) == Success && prop_retval) {
        Atom *atoms = (Atom*)prop_retval;
        gboolean is_hidden = FALSE;
        for (unsigned long i = 0; i < nitems; i++) {
            if (atoms[i] == hidden) { is_hidden = TRUE; break; }
        }
        XFree(prop_retval);
        return is_hidden;
    }
    
    if (prop_retval) XFree(prop_retval);
    return FALSE;
}

/* ============================================================================
 * X11 Window Actions
 * ============================================================================ */

static void activate_window(Display *dpy, Window root, Window win) {
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 2;
    xev.xclient.data.l[1] = CurrentTime;
    xev.xclient.data.l[2] = 0;

    XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XMapRaised(dpy, win);
    XFlush(dpy);
}

static void minimize_window(Display *dpy, Window root, Window win) {
    (void)root;
    XIconifyWindow(dpy, win, DefaultScreen(dpy));
    XFlush(dpy);
}

static void close_window(Display *dpy, Window root, Window win) {
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = XInternAtom(dpy, "_NET_CLOSE_WINDOW", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = CurrentTime;
    xev.xclient.data.l[1] = 2;

    XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(dpy);
}

static void send_window_to_desktop(Display *dpy, Window root, Window win, int desktop) {
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = win;
    xev.xclient.message_type = XInternAtom(dpy, "_NET_WM_DESKTOP", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = desktop;
    xev.xclient.data.l[1] = 2;

    XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(dpy);
}

/* ============================================================================
 * Window Info Helpers
 * ============================================================================ */

static char* get_wm_class(Display *dpy, Window win) {
    XClassHint hint;
    memset(&hint, 0, sizeof(hint));
    if (XGetClassHint(dpy, win, &hint)) {
        char *res = NULL;
        /* Use res_class (application name) for grouping, fallback to res_name */
        if (hint.res_class) res = g_strdup(hint.res_class);
        else if (hint.res_name) res = g_strdup(hint.res_name);
        
        if (hint.res_name) XFree(hint.res_name);
        if (hint.res_class) XFree(hint.res_class);
        
        if (res) {
            for (int i = 0; res[i]; i++) res[i] = g_ascii_tolower(res[i]);
        }
        return res;
    }
    return NULL;
}

static GdkPixbuf* get_window_icon(Display *dpy, Window xwindow, const char *class_name) {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    GdkPixbuf *pixbuf = NULL;

    /* Method 1: Try _NET_WM_ICON */
    Atom net_wm_icon = XInternAtom(dpy, "_NET_WM_ICON", False);
    if (XGetWindowProperty(dpy, xwindow, net_wm_icon, 0, 65536, False,
                           XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
        if (prop && actual_format == 32 && nitems > 2) {
            unsigned long *data = (unsigned long *)prop;
            int width = data[0];
            int height = data[1];
            int size = width * height;

            if (nitems >= (unsigned long)(size + 2) && width > 0 && height > 0 && width < 512 && height < 512) {
                pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, width, height);
                guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);

                for (int i = 0; i < size; i++) {
                    unsigned long argb = data[i + 2];
                    pixels[i * 4 + 0] = (argb >> 16) & 0xFF;
                    pixels[i * 4 + 1] = (argb >> 8) & 0xFF;
                    pixels[i * 4 + 2] = argb & 0xFF;
                    pixels[i * 4 + 3] = (argb >> 24) & 0xFF;
                }
            }
            XFree(prop);
            if (pixbuf != NULL) {
                GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, 24, 24, GDK_INTERP_BILINEAR);
                g_object_unref(pixbuf);
                return scaled;
            }
        } else if (prop) {
            XFree(prop);
        }
    }

    /* Method 2: Try GDesktopAppInfo */
    GtkIconTheme *icon_theme = gtk_icon_theme_get_default();
    GError *error = NULL;
    
    if (class_name) {
        GDesktopAppInfo *app_info = NULL;
        
        gchar *desktop_id = g_strdup_printf("%s.desktop", class_name);
        app_info = g_desktop_app_info_new(desktop_id);
        g_free(desktop_id);
        
        if (!app_info) {
            gchar ***desktop_ids = g_desktop_app_info_search(class_name);
            if (desktop_ids != NULL && desktop_ids[0] != NULL) {
                app_info = g_desktop_app_info_new(desktop_ids[0][0]);
            }
            if (desktop_ids != NULL) {
                for (gchar ***p = desktop_ids; *p != NULL; p++) g_strfreev(*p);
                g_free(desktop_ids);
            }
        }
        
        if (app_info) {
            gchar *icon_name = g_desktop_app_info_get_string(app_info, "Icon");
            if (icon_name) {
                if (g_path_is_absolute(icon_name)) {
                    pixbuf = gdk_pixbuf_new_from_file_at_scale(icon_name, 24, 24, TRUE, &error);
                } else {
                    pixbuf = gtk_icon_theme_load_icon(icon_theme, icon_name, 24, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
                }
                g_free(icon_name);
                if (error) { g_error_free(error); error = NULL; }
            }
            g_object_unref(app_info);
            if (pixbuf) return pixbuf;
        }
        
        /* Direct class name lookup fallback */
        pixbuf = gtk_icon_theme_load_icon(icon_theme, class_name, 24, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
        if (error) { g_error_free(error); error = NULL; }
        if (pixbuf) return pixbuf;
    }
    
    /* Method 3: Generic fallback */
    pixbuf = gtk_icon_theme_load_icon(icon_theme, "application-x-executable", 24, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
    if (error) { g_error_free(error); error = NULL; }
    return pixbuf;
}

/* ============================================================================
 * Window Info Management
 * ============================================================================ */

static WindowInfo* window_info_new(Display *dpy, Window win, unsigned long current_desktop) {
    WindowInfo *info = g_new0(WindowInfo, 1);
    info->xid = win;
    info->title = get_x11_prop_string(dpy, win, "_NET_WM_NAME");
    if (!info->title) info->title = get_x11_prop_string(dpy, win, "WM_NAME");
    info->wm_class = get_wm_class(dpy, win);
    info->desktop = get_x11_prop_cardinal(dpy, win, "_NET_WM_DESKTOP", current_desktop);
    info->is_minimized = is_window_minimized(dpy, win);
    info->icon = get_window_icon(dpy, win, info->wm_class);
    return info;
}

static void window_info_free(WindowInfo *info) {
    if (info->title) g_free(info->title);
    if (info->wm_class) g_free(info->wm_class);
    if (info->icon) g_object_unref(info->icon);
    g_free(info);
}

/* ============================================================================
 * App Group Management
 * ============================================================================ */

static AppGroup* app_group_new(const char *app_id, TasklistData *parent) {
    AppGroup *group = g_new0(AppGroup, 1);
    group->app_id = g_strdup(app_id);
    group->parent = parent;
    group->active_index = -1;
    
    /* Try to get a nice display name from desktop file */
    GDesktopAppInfo *app_info = NULL;
    gchar *desktop_id = g_strdup_printf("%s.desktop", app_id);
    app_info = g_desktop_app_info_new(desktop_id);
    g_free(desktop_id);
    
    if (!app_info) {
        gchar ***desktop_ids = g_desktop_app_info_search(app_id);
        if (desktop_ids && desktop_ids[0]) {
            app_info = g_desktop_app_info_new(desktop_ids[0][0]);
        }
        if (desktop_ids) {
            for (gchar ***p = desktop_ids; *p; p++) g_strfreev(*p);
            g_free(desktop_ids);
        }
    }
    
    if (app_info) {
        group->display_name = g_strdup(g_app_info_get_name(G_APP_INFO(app_info)));
        g_object_unref(app_info);
    }
    
    if (!group->display_name) {
        /* Capitalize first letter of app_id */
        group->display_name = g_strdup(app_id);
        if (group->display_name[0]) {
            group->display_name[0] = g_ascii_toupper(group->display_name[0]);
        }
    }
    
    return group;
}

static void app_group_free(AppGroup *group) {
    if (group->app_id) g_free(group->app_id);
    if (group->display_name) g_free(group->display_name);
    if (group->icon) g_object_unref(group->icon);
    if (group->button) gtk_widget_destroy(group->button);
    
    g_list_free_full(group->windows, (GDestroyNotify)window_info_free);
    g_free(group);
}

static void app_group_add_window(AppGroup *group, WindowInfo *win) {
    group->windows = g_list_append(group->windows, win);
    
    /* Use first window's icon if group doesn't have one */
    if (!group->icon && win->icon) {
        group->icon = g_object_ref(win->icon);
    }
    
    /* Update active index */
    if (win->is_active) {
        group->active_index = g_list_length(group->windows) - 1;
    }
}

static int app_group_get_window_count(AppGroup *group) {
    return g_list_length(group->windows);
}

/* Get the next window to activate (for cycling) */
static WindowInfo* app_group_get_next_window(AppGroup *group) {
    int count = app_group_get_window_count(group);
    if (count == 0) return NULL;
    
    /* If no active window or count is 1, return first window */
    if (group->active_index < 0 || count == 1) {
        return (WindowInfo*)group->windows->data;
    }
    
    /* Cycle to next window */
    int next_index = (group->active_index + 1) % count;
    GList *item = g_list_nth(group->windows, next_index);
    return item ? (WindowInfo*)item->data : NULL;
}

/* Get the active window in this group */
static WindowInfo* app_group_get_active_window(AppGroup *group) {
    if (group->active_index < 0) return NULL;
    GList *item = g_list_nth(group->windows, group->active_index);
    return item ? (WindowInfo*)item->data : NULL;
}

/* ============================================================================
 * Button Drawing with Badge
 * ============================================================================ */

/* Draw a badge showing window count if > 1 */
static gboolean on_button_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AppGroup *group = (AppGroup*)user_data;
    int count = app_group_get_window_count(group);
    
    if (count <= 1) return FALSE;
    
    TasklistData *data = group->parent;
    if (!data->show_window_count) return FALSE;
    
    /* Draw badge in top-right corner */
    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    
    int badge_size = 16;
    int x = allocation.width - badge_size - 2;
    int y = 2;
    
    /* Draw circle background */
    cairo_set_source_rgb(cr, 0.2, 0.6, 0.9);
    cairo_arc(cr, x + badge_size/2, y + badge_size/2, badge_size/2, 0, 2 * G_PI);
    cairo_fill(cr);
    
    /* Draw count text */
    gchar *count_str = g_strdup_printf("%d", count);
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 9);
    
    cairo_text_extents_t extents;
    cairo_text_extents(cr, count_str, &extents);
    cairo_move_to(cr, x + (badge_size - extents.width) / 2, y + badge_size - (extents.height + extents.y_bearing) / 2 - 1);
    cairo_show_text(cr, count_str);
    g_free(count_str);
    
    return FALSE;
}

/* ============================================================================
 * Button Click Handlers
 * ============================================================================ */

static void on_group_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AppGroup *group = (AppGroup*)user_data;
    TasklistData *data = group->parent;
    int count = app_group_get_window_count(group);
    
    if (count == 0) return;
    
    if (count == 1) {
        /* Single window: toggle minimize/activate */
        WindowInfo *win = (WindowInfo*)group->windows->data;
        if (win->is_active && !win->is_minimized) {
            minimize_window(data->dpy, data->root, win->xid);
        } else {
            activate_window(data->dpy, data->root, win->xid);
        }
    } else {
        /* Multiple windows: cycle through them */
        WindowInfo *current = app_group_get_active_window(group);
        
        /* If current window is active and not minimized, minimize it and go to next */
        if (current && current->is_active && !current->is_minimized) {
            minimize_window(data->dpy, data->root, current->xid);
        }
        
        /* Activate next window */
        WindowInfo *next = app_group_get_next_window(group);
        if (next) {
            activate_window(data->dpy, data->root, next->xid);
        }
    }
}

static void on_window_menu_activate(GtkMenuItem *item, gpointer user_data) {
    (void)user_data;
    Window xid = (Window)GPOINTER_TO_SIZE(g_object_get_data(G_OBJECT(item), "window_xid"));
    TasklistData *td = (TasklistData*)g_object_get_data(G_OBJECT(item), "tasklist_data");

    if (xid && td) {
        activate_window(td->dpy, td->root, xid);
    }
}

static void on_close_all_activate(GtkMenuItem *item, gpointer user_data) {
    (void)item;
    AppGroup *group = (AppGroup*)user_data;
    TasklistData *td = group->parent;

    for (GList *l = group->windows; l; l = l->next) {
        WindowInfo *win = (WindowInfo*)l->data;
        close_window(td->dpy, td->root, win->xid);
    }
}

static void on_move_group_to_workspace_activate(GtkMenuItem *item, gpointer user_data) {
    (void)user_data;
    int target_ws = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "target_ws"));
    AppGroup *group = (AppGroup*)g_object_get_data(G_OBJECT(item), "app_group");
    TasklistData *td;

    if (!group) {
        return;
    }

    td = group->parent;
    for (GList *l = group->windows; l; l = l->next) {
        WindowInfo *win = (WindowInfo*)l->data;
        send_window_to_desktop(td->dpy, td->root, win->xid, target_ws);
    }
}

/* Context menu for window group */
static gboolean on_group_button_press(GtkWidget *btn, GdkEventButton *event, gpointer user_data) {
    (void)btn;
    AppGroup *group = (AppGroup*)user_data;
    TasklistData *data = group->parent;
    
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) { /* Right click */
        GtkWidget *menu = gtk_menu_new();
        
        /* Add header with app name */
        GtkWidget *header = gtk_menu_item_new_with_label(group->display_name);
        gtk_widget_set_sensitive(header, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), header);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        
        /* Add each window as a menu item */
        for (GList *l = group->windows; l; l = l->next) {
            WindowInfo *win = (WindowInfo*)l->data;
            
            GtkWidget *win_item = gtk_menu_item_new_with_label(win->title ? win->title : "Untitled");
            
            /* Highlight active window */
            if (win->is_active) {
                GtkWidget *label = gtk_bin_get_child(GTK_BIN(win_item));
                if (label) {
                    gchar *markup = g_markup_printf_escaped("<b>%s</b>", win->title ? win->title : "Untitled");
                    gtk_label_set_markup(GTK_LABEL(label), markup);
                    g_free(markup);
                }
            }
            
            /* Store window XID for callback */
            g_object_set_data(G_OBJECT(win_item), "window_xid", GSIZE_TO_POINTER((gsize)win->xid));
            g_object_set_data(G_OBJECT(win_item), "tasklist_data", data);
            
            g_signal_connect(win_item, "activate", G_CALLBACK(on_window_menu_activate), NULL);
            
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), win_item);
        }
        
        /* Get number of desktops */
        int num_desktops = 4;
        Atom prop = XInternAtom(data->dpy, "_NET_NUMBER_OF_DESKTOPS", True);
        Atom actual_type; int actual_format; unsigned long nitems; unsigned long bytes_after;
        unsigned char *prop_retval = NULL;
        
        if (XGetWindowProperty(data->dpy, data->root, prop, 0, 1, False, AnyPropertyType,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop_retval) == Success && prop_retval) {
            num_desktops = (int)(*(long*)prop_retval);
            XFree(prop_retval);
        }
        
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        
        /* Close All Windows */
        GtkWidget *close_all = gtk_menu_item_new_with_label("Close All Windows");
        g_signal_connect(close_all, "activate", G_CALLBACK(on_close_all_activate), group);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), close_all);
        
        /* Move All to Workspace submenu */
        if (num_desktops > 1) {
            GtkWidget *move_item = gtk_menu_item_new_with_label("Move All to Workspace...");
            GtkWidget *submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(move_item), submenu);
            
            for (int i = 0; i < num_desktops; i++) {
                char label[32];
                snprintf(label, sizeof(label), "Workspace %d", i + 1);
                GtkWidget *ws_item = gtk_menu_item_new_with_label(label);
                g_object_set_data(G_OBJECT(ws_item), "target_ws", GINT_TO_POINTER(i));
                g_object_set_data(G_OBJECT(ws_item), "app_group", group);
                
                g_signal_connect(ws_item, "activate", G_CALLBACK(on_move_group_to_workspace_activate), NULL);
                
                gtk_menu_shell_append(GTK_MENU_SHELL(submenu), ws_item);
            }
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), move_item);
        }
        
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    
    return FALSE;
}

/* ============================================================================
 * Tasklist Building
 * ============================================================================ */

static GtkWidget* create_group_button(AppGroup *group) {
    GtkWidget *btn = gtk_toggle_button_new();
    gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
    gtk_widget_set_size_request(btn, 36, 36);
    
    /* Create icon */
    GdkPixbuf *icon = group->icon ? group->icon : NULL;
    GtkWidget *image;
    if (icon) {
        image = gtk_image_new_from_pixbuf(icon);
    } else {
        image = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_LARGE_TOOLBAR);
    }
    gtk_container_add(GTK_CONTAINER(btn), image);
    
    /* Set tooltip */
    int count = app_group_get_window_count(group);
    if (count == 1) {
        WindowInfo *win = (WindowInfo*)group->windows->data;
        gtk_widget_set_tooltip_text(btn, win->title ? win->title : group->display_name);
    } else {
        gchar *tooltip = g_strdup_printf("%s (%d windows)", group->display_name, count);
        gtk_widget_set_tooltip_text(btn, tooltip);
        g_free(tooltip);
    }
    
    /* Set active state */
    WindowInfo *active_win = app_group_get_active_window(group);
    if (active_win && active_win->is_active) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
    }
    
    /* Connect signals */
    g_signal_connect(btn, "clicked", G_CALLBACK(on_group_clicked), group);
    g_signal_connect(btn, "button-press-event", G_CALLBACK(on_group_button_press), group);
    
    /* Draw badge for multiple windows */
    if (count > 1 && group->parent->show_window_count) {
        g_signal_connect(btn, "draw", G_CALLBACK(on_button_draw), group);
    }
    
    return btn;
}

static void rebuild_tasklist(TasklistData *data) {
    /* Clear existing groups */
    if (data->group_list) {
        g_list_free_full(data->group_list, (GDestroyNotify)app_group_free);
        data->group_list = NULL;
    }
    if (data->groups) {
        g_hash_table_destroy(data->groups);
    }
    data->groups = g_hash_table_new(g_str_hash, g_str_equal);
    
    /* Clear container */
    GList *children = gtk_container_get_children(GTK_CONTAINER(data->box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    if (!data->client_list) return;
    
    /* Group windows by WM_CLASS */
    for (int i = 0; i < data->num_clients; i++) {
        Window win = data->client_list[i];
        if (!is_normal_window(data->dpy, win)) continue;
        
        /* Check if window is on current workspace (or all workspaces: 0xFFFFFFFF) */
        unsigned long win_desktop = get_x11_prop_cardinal(data->dpy, win, "_NET_WM_DESKTOP", data->current_desktop);
        if (win_desktop != 0xFFFFFFFF && win_desktop != data->current_desktop) {
            continue;
        }
        
        /* Create window info */
        WindowInfo *win_info = window_info_new(data->dpy, win, data->current_desktop);
        win_info->is_active = (win == data->active_win);
        
        /* Get or create app group */
        const char *app_id = win_info->wm_class ? win_info->wm_class : "unknown";
        
        AppGroup *group = g_hash_table_lookup(data->groups, app_id);
        if (!group) {
            group = app_group_new(app_id, data);
            g_hash_table_insert(data->groups, group->app_id, group);
            data->group_list = g_list_append(data->group_list, group);
        }
        
        app_group_add_window(group, win_info);
    }
    
    /* Create buttons for each group */
    for (GList *l = data->group_list; l; l = l->next) {
        AppGroup *group = (AppGroup*)l->data;
        group->button = create_group_button(group);
        gtk_box_pack_start(GTK_BOX(data->box), group->button, FALSE, FALSE, 0);
    }
    
    gtk_widget_show_all(data->box);
}

/* ============================================================================
 * Poll and Update
 * ============================================================================ */

static gboolean poll_tasklist(gpointer user_data) {
    TasklistData *data = (TasklistData*)user_data;
    if (!GTK_IS_WIDGET(data->box)) return G_SOURCE_REMOVE;
    if (!data->dpy) return G_SOURCE_CONTINUE;
    
    int new_count = 0;
    Window *new_list = get_x11_prop_windows(data->dpy, data->root, "_NET_CLIENT_LIST", &new_count);
    
    int active_count = 0;
    Window *active_wins = get_x11_prop_windows(data->dpy, data->root, "_NET_ACTIVE_WINDOW", &active_count);
    Window new_active = (active_count > 0 && active_wins) ? active_wins[0] : None;
    if (active_wins) g_free(active_wins);
    
    unsigned long new_desktop = get_x11_prop_cardinal(data->dpy, data->root, "_NET_CURRENT_DESKTOP", 0);
    
    gboolean needs_rebuild = FALSE;
    
    /* Check for changes */
    if (new_count != data->num_clients || new_active != data->active_win || new_desktop != data->current_desktop) {
        needs_rebuild = TRUE;
    } else if (new_list && data->client_list) {
        if (memcmp(new_list, data->client_list, new_count * sizeof(Window)) != 0) {
            needs_rebuild = TRUE;
        }
    }
    
    if (needs_rebuild) {
        if (data->client_list) g_free(data->client_list);
        data->client_list = new_list;
        data->num_clients = new_count;
        data->active_win = new_active;
        data->current_desktop = new_desktop;
        rebuild_tasklist(data);
    } else {
        if (new_list) g_free(new_list);
    }
    
    return G_SOURCE_CONTINUE;
}

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    TasklistData *data = (TasklistData*)user_data;
    if (data->timer_id > 0) g_source_remove(data->timer_id);
    if (data->client_list) g_free(data->client_list);
    if (data->group_list) g_list_free_full(data->group_list, (GDestroyNotify)app_group_free);
    if (data->groups) g_hash_table_destroy(data->groups);
    g_free(data);
}

static GtkWidget* create_tasklist_widget(void) {
    TasklistData *data = g_new0(TasklistData, 1);
    
    /* Default settings */
    data->group_windows = TRUE;
    data->show_window_count = TRUE;
    
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(gdk_dpy)) {
        data->dpy = gdk_x11_display_get_xdisplay(gdk_dpy);
        data->root = DefaultRootWindow(data->dpy);
    }
    
    data->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    
    /* Initial fetch */
    if (data->dpy) {
        data->client_list = get_x11_prop_windows(data->dpy, data->root, "_NET_CLIENT_LIST", &data->num_clients);
        data->current_desktop = get_x11_prop_cardinal(data->dpy, data->root, "_NET_CURRENT_DESKTOP", 0);
        int active_count = 0;
        Window *active = get_x11_prop_windows(data->dpy, data->root, "_NET_ACTIVE_WINDOW", &active_count);
        if (active) {
            data->active_win = active[0];
            g_free(active);
        }
    }
    rebuild_tasklist(data);
    
    /* Poll every 200ms */
    data->timer_id = g_timeout_add(200, poll_tasklist, data);
    g_signal_connect(data->box, "destroy", G_CALLBACK(on_widget_destroy), data);

    return data->box;
}

/* ============================================================================
 * Plugin API
 * ============================================================================ */

VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "Window List (Grouped)";
    api.description   = "Displays open windows grouped by application.";
    api.author        = "Venom";
    api.expand        = TRUE;
    api.padding       = 4;
    api.create_widget = create_tasklist_widget;
    return &api;
}

VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size = sizeof(VenomPanelPluginAPIv2),
        .name = "Window List (Grouped)",
        .description = "Displays open windows grouped by application. Click to cycle through windows.",
        .author = "Venom",
        .zone = VENOM_PLUGIN_ZONE_CENTER,
        .priority = 0,
        .expand = TRUE,
        .padding = 4,
        .create_widget = create_tasklist_widget,
        .destroy_widget = NULL,
    };
    return &api;
}

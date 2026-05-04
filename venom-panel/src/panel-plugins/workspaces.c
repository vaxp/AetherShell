#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include "venom-panel-plugin-api.h"

typedef struct {
    GtkWidget *box;
    int current_desktop;
    int num_desktops;
    guint timer_id;
    Display *dpy;
    Window root;
} WorkspaceData;

/* Helper to get an integer property from X11 */
static int get_x11_prop_int(Display *dpy, Window win, const char *prop_name) {
    Atom prop = XInternAtom(dpy, prop_name, True);
    if (prop == None) return -1;
    
    Atom actual_type_return;
    int actual_format_return;
    unsigned long nitems_return;
    unsigned long bytes_after_return;
    unsigned char *prop_return = NULL;
    
    int status = XGetWindowProperty(dpy, win, prop, 0, 1, False, AnyPropertyType,
                                    &actual_type_return, &actual_format_return,
                                    &nitems_return, &bytes_after_return, &prop_return);
    
    int value = -1;
    if (status == Success && prop_return) {
        if (actual_format_return == 32 && nitems_return > 0) {
            value = (int)(*(long*)prop_return);
        }
        XFree(prop_return);
    }
    return value;
}

/* Helper to change workspace */
static void set_current_desktop(Display *dpy, Window root, int desktop_idx) {
    XEvent xev;
    memset(&xev, 0, sizeof(xev));
    xev.type = ClientMessage;
    xev.xclient.window = root;
    xev.xclient.message_type = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = desktop_idx;
    xev.xclient.data.l[1] = CurrentTime;

    XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    XFlush(dpy);
}

static void on_workspace_clicked(GtkButton *btn, gpointer user_data) {
    WorkspaceData *data = (WorkspaceData*)user_data;
    int target_idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "ws_idx"));
    
    if (target_idx != data->current_desktop) {
        set_current_desktop(data->dpy, data->root, target_idx);
    }
}

/* Rebuilds the buttons inside the box */
static void rebuild_workspace_buttons(WorkspaceData *data) {
    /* Clear existing */
    GList *children = gtk_container_get_children(GTK_CONTAINER(data->box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    static const char* roman_numerals[] = {
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
        "XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX"
    };

    for (int i = 0; i < data->num_desktops; i++) {
        char label[16];
        if (i < 20) {
            snprintf(label, sizeof(label), "%s", roman_numerals[i]);
        } else {
            snprintf(label, sizeof(label), "%d", i + 1);
        }
        
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_set_size_request(btn, 28, 28);
        
        /* Remove ugly button bevel to make it panel-friendly */
        gtk_button_set_relief(GTK_BUTTON(btn), GTK_RELIEF_NONE);
        
        /* Highlight current desktop */
        if (i == data->current_desktop) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
            gtk_style_context_add_class(ctx, "suggested-action");
        }
        
        g_object_set_data(G_OBJECT(btn), "ws_idx", GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_workspace_clicked), data);
        
        gtk_box_pack_start(GTK_BOX(data->box), btn, FALSE, FALSE, 0);
    }
    
    gtk_widget_show_all(data->box);
}

/* Polling timer to detect workspace changes from the WM */
static gboolean poll_workspaces(gpointer user_data) {
    WorkspaceData *data = (WorkspaceData*)user_data;
    if (!GTK_IS_WIDGET(data->box)) return G_SOURCE_REMOVE;
    
    int num = get_x11_prop_int(data->dpy, data->root, "_NET_NUMBER_OF_DESKTOPS");
    int cur = get_x11_prop_int(data->dpy, data->root, "_NET_CURRENT_DESKTOP");
    
    /* Fallbacks if WM doesn't support EWMH temporarily */
    if (num <= 0) num = 1;
    if (cur < 0) cur = 0;
    
    if (num != data->num_desktops || cur != data->current_desktop) {
        data->num_desktops = num;
        data->current_desktop = cur;
        rebuild_workspace_buttons(data);
    }
    
    return G_SOURCE_CONTINUE;
}

static void on_widget_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    WorkspaceData *data = (WorkspaceData*)user_data;
    if (data->timer_id > 0) {
        g_source_remove(data->timer_id);
    }
    g_free(data);
}

static GtkWidget* create_workspaces_widget(void) {
    WorkspaceData *data = g_new0(WorkspaceData, 1);
    
    /* GTK/GDK context setup for X11 */
    GdkDisplay *gdk_dpy = gdk_display_get_default();
    if (GDK_IS_X11_DISPLAY(gdk_dpy)) {
        data->dpy = gdk_x11_display_get_xdisplay(gdk_dpy);
        data->root = DefaultRootWindow(data->dpy);
    }
    
    data->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    
    /* Get initial state */
    if (data->dpy) {
        data->num_desktops = get_x11_prop_int(data->dpy, data->root, "_NET_NUMBER_OF_DESKTOPS");
        data->current_desktop = get_x11_prop_int(data->dpy, data->root, "_NET_CURRENT_DESKTOP");
    }
    if (data->num_desktops <= 0) data->num_desktops = 4; /* Default if fails */
    if (data->current_desktop < 0) data->current_desktop = 0;
    
    rebuild_workspace_buttons(data);
    
    /* Start polling every 100ms (X11 fast poll is practically free) */
    data->timer_id = g_timeout_add(150, poll_workspaces, data);
    g_signal_connect(data->box, "destroy", G_CALLBACK(on_widget_destroy), data);

    return data->box;
}

VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "Workspace Switcher";
    api.description   = "Displays virtual desktops and allows switching via EWMH.";
    api.author        = "Venom";
    api.expand        = FALSE;
    api.padding       = 2;
    api.create_widget = create_workspaces_widget;
    return &api;
}

VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version = VENOM_PANEL_PLUGIN_API_VERSION,
        .struct_size = sizeof(VenomPanelPluginAPIv2),
        .name = "Workspace Switcher",
        .description = "Displays virtual desktops and allows switching via EWMH.",
        .author = "Venom",
        .zone = VENOM_PLUGIN_ZONE_LEFT,
        .priority = 0,
        .expand = FALSE,
        .padding = 2,
        .create_widget = create_workspaces_widget,
        .destroy_widget = NULL,
    };
    return &api;
}

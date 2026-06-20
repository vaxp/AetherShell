/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🖼️ Venom Basilisk - Window Module (macOS-style App Launcher)
 * ═══════════════════════════════════════════════════════════════════════════
 */

#include "window.h"
#include "search.h"
#include "commands.h"

extern BasiliskState *state;

// ═══════════════════════════════════════════════════════════════════════════
// CSS - Light glassmorphism, macOS-inspired
// ═══════════════════════════════════════════════════════════════════════════

static const gchar *css_data =
    "* { padding: 0; margin: 0; }"
    "window { background-color: transparent; }"
    "box { background-color: transparent; }"
    "grid { background-color: transparent; }"
    "viewport { background-color: transparent; }"
    "scrolledwindow { background-color: transparent; }"

    /* ── Outer window shell ── */
    "#app-shell {"
    "  background-color: rgba(0, 0, 0, 0.32);"
    "  border-radius: 20px;"
    "  border: 1px solid rgba(0, 0, 0, 0.38);"
    "  padding: 0px;"
    "}"

    /* ── Title bar ── */
    "#title-bar {"
    "  background-color: transparent;"
    "  padding: 16px 18px 8px 18px;"
    "}"
    "#app-title {"
    "  color: rgba(255, 255, 255, 0.9);"
    "  font-family: 'SF Pro Display', 'Inter', sans-serif;"
    "  font-size: 20px;"
    "  font-weight: 700;"
    "  letter-spacing: -0.3px;"
    "}"
    "#title-icon {"
    "  color: rgba(255, 255, 255, 0.8);"
    "  font-size: 18px;"
    "  font-weight: 700;"
    "  margin-right: 6px;"
    "}"
    "#more-button {"
    "  background-color: transparent;"
    "  border: none;"
    "  border-radius: 50%;"
    "  color: rgba(0, 0, 0, 0.6);"
    "  font-size: 16px;"
    "  padding: 4px 8px;"
    "  min-width: 28px;"
    "  min-height: 28px;"
    "}"
    "#more-button:hover {"
    "  background-color: rgba(0, 0, 0, 0.12);"
    "}"

    /* ── Category pills bar ── */
    "#category-bar {"
    "  background-color: transparent;"
    "  padding: 0px 18px 10px 18px;"
    "}"
    ".category-pill {"
    "  background-color: rgba(190,200,225,0.55);"
    "  border: 1px solid rgba(255,255,255,0.60);"
    "  border-radius: 20px;"
    "  color: rgba(255, 255, 255, 0.75);"
    "  font-family: 'SF Pro Text', 'Inter', sans-serif;"
    "  font-size: 12px;"
    "  font-weight: 500;"
    "  padding: 4px 12px;"
    "  margin-right: 6px;"
    "  min-height: 0px;"
    "}"
    ".category-pill:hover {"
    "  background-color: rgba(170,185,215,0.75);"
    "  color: rgba(255, 255, 255, 0.9);"
    "}"
    ".category-pill.active {"
    "  background-color: rgba(255,255,255,0.80);"
    "  border-color: rgba(255,255,255,0.90);"
    "  color: rgba(255, 255, 255, 0.95);"
    "  font-weight: 600;"
    "}"

    /* ── Separator ── */
    "#cat-separator {"
    "  background-color: rgba(150,165,200,0.30);"
    "  min-height: 1px;"
    "  margin: 0px 0px 0px 0px;"
    "}"

    /* ── Scroll area ── */
    "#scroll-area {"
    "  background-color: transparent;"
    "}"

    /* ── App grid ── */
    "#app-grid-inner {"
    "  background-color: transparent;"
    "  padding: 16px 18px 16px 18px;"
    "}"

    /* ── App button ── */
    ".app-button {"
    "  background-color: transparent;"
    "  border: none;"
    "  border-radius: 16px;"
    "  padding: 10px 8px 8px 8px;"
    "  min-width: 100px;"
    "  min-height: 100px;"
    "}"
    ".app-button:hover {"
    "  background-color: rgba(150,165,210,0.22);"
    "}"
    ".app-button:active {"
    "  background-color: rgba(120,140,190,0.35);"
    "}"

    /* ── App icon ── */
    ".app-icon {"
    "  margin-bottom: 6px;"
    "}"

    /* ── App label ── */
    ".app-name {"
    "  color: rgba(255, 255, 255, 1);"
    "  font-family: 'SF Pro Text', 'Inter', sans-serif;"
    "  font-size: 11px;"
    "  font-weight: 400;"
    "}"

    /* ── Search bar (hidden, not shown in this view) ── */
    "#search-entry {"
    "  background-color: rgba(200,210,235,0.60);"
    "  border: 1px solid rgba(255,255,255,0.55);"
    "  border-radius: 10px;"
    "  color: rgba(255, 255, 255, 0.9);"
    "  font-size: 14px;"
    "  padding: 8px 12px;"
    "  min-height: 0px;"
    "  margin: 0px 18px 10px 18px;"
    "}"
    "#search-entry:focus {"
    "  border-color: rgba(0, 0, 0, 0.6);"
    "  background-color: rgba(0, 0, 0, 0.54);"
    "  outline: none;"
    "  box-shadow: none;"
    "}"
    "#search-entry, #search-entry text {"
    "  color: rgba(255, 255, 255, 0.9);"
    "  caret-color: rgba(255, 255, 255, 0.9);"
    "}";

// ═══════════════════════════════════════════════════════════════════════════
// Globals
// ═══════════════════════════════════════════════════════════════════════════

static GtkWidget *category_buttons[CAT_COUNT];
static const gchar *search_hint_text = "BasiliskSearch ";

static void on_window_realize_disable_decorations(GtkWidget *widget, gpointer data) {
    GdkWindow *gdk_window;
    (void)data;
    gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;
    if (GDK_IS_WAYLAND_WINDOW(gdk_window)) {
        gdk_wayland_window_announce_csd(gdk_window);
    } else {
        gdk_window_set_decorations(gdk_window, 0);
        gdk_window_set_functions(gdk_window, 0);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Forward declarations
// ═══════════════════════════════════════════════════════════════════════════

void window_refresh_grid(void);

// ═══════════════════════════════════════════════════════════════════════════
// CSS Application
// ═══════════════════════════════════════════════════════════════════════════

void window_apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css_data, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

// ═══════════════════════════════════════════════════════════════════════════
// App Button Creation
// ═══════════════════════════════════════════════════════════════════════════

static void on_app_clicked(GtkButton *button, gpointer data) {
    (void)button;
    const gchar *desktop_file = (const gchar *)data;

    GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(desktop_file);
    if (app_info) {
        g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, NULL);
        g_object_unref(app_info);
    }

    window_hide();
}

static GtkWidget* create_app_button(AppEntry *app, int index) {
    (void)index;

    GtkWidget *button = gtk_button_new();
    GtkStyleContext *ctx = gtk_widget_get_style_context(button);
    gtk_style_context_add_class(ctx, "app-button");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(button), vbox);

    /* ── Icon ── */
    GtkWidget *icon = NULL;
    if (app->icon) {
        GtkIconTheme *theme = gtk_icon_theme_get_default();
        if (g_path_is_absolute(app->icon)) {
            GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_scale(app->icon, ICON_SIZE, ICON_SIZE, TRUE, NULL);
            if (pb) { icon = gtk_image_new_from_pixbuf(pb); g_object_unref(pb); }
        } else {
            GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, app->icon, ICON_SIZE, GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
            if (pb) { icon = gtk_image_new_from_pixbuf(pb); g_object_unref(pb); }
        }
    }
    if (!icon) {
        icon = gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DIALOG);
        gtk_image_set_pixel_size(GTK_IMAGE(icon), ICON_SIZE);
    }
    gtk_style_context_add_class(gtk_widget_get_style_context(icon), "app-icon");
    gtk_box_pack_start(GTK_BOX(vbox), icon, FALSE, FALSE, 0);

    /* ── Label ── */
    GtkWidget *label = gtk_label_new(app->name);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "app-name");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 11);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(label, 6);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);

    g_signal_connect(button, "clicked", G_CALLBACK(on_app_clicked), app->desktop_file);

    return button;
}

// ═══════════════════════════════════════════════════════════════════════════
// Category Handling
// ═══════════════════════════════════════════════════════════════════════════

static void on_category_clicked(GtkButton *button, gpointer data) {
    AppCategory cat = GPOINTER_TO_INT(data);
    state->current_category = cat;

    for (int i = 0; i < CAT_COUNT; i++) {
        GtkStyleContext *ctx = gtk_widget_get_style_context(category_buttons[i]);
        if (i == (int)cat) {
            gtk_style_context_add_class(ctx, "active");
        } else {
            gtk_style_context_remove_class(ctx, "active");
        }
    }

    window_refresh_grid();
    (void)button;
}

static GtkWidget* create_category_bar(void) {
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_name(hbox, "category-bar");
    gtk_widget_set_halign(hbox, GTK_ALIGN_FILL);

    const gchar *names[] = {"All", "Development", "System", "Internet", "Utility", "Other"};

    for (int i = 0; i < CAT_COUNT; i++) {
        GtkWidget *btn = gtk_button_new_with_label(names[i]);
        GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
        gtk_style_context_add_class(ctx, "category-pill");

        if (i == 0)
            gtk_style_context_add_class(ctx, "active");

        g_signal_connect(btn, "clicked", G_CALLBACK(on_category_clicked), GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);

        category_buttons[i] = btn;
    }

    return hbox;
}

// ═══════════════════════════════════════════════════════════════════════════
// Grid Refresh
// ═══════════════════════════════════════════════════════════════════════════

void window_refresh_grid(void) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(state->app_grid));
    for (GList *l = children; l != NULL; l = l->next)
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    const gchar *search_text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    gchar *lower_search = (!state->search_hint_visible && search_text && strlen(search_text) > 0)
        ? g_utf8_strdown(search_text, -1) : NULL;

    int count = 0;
    int max_apps = GRID_COLS * GRID_ROWS;

    for (GList *l = state->app_cache; l != NULL && count < max_apps; l = l->next) {
        AppEntry *app = (AppEntry *)l->data;

        if (state->current_category != CAT_ALL && app->category != state->current_category)
            continue;

        if (lower_search) {
            gchar *lower_name = g_utf8_strdown(app->name, -1);
            gboolean match = strstr(lower_name, lower_search) != NULL;
            g_free(lower_name);
            if (!match) continue;
        }

        GtkWidget *btn = create_app_button(app, count);
        gtk_grid_attach(GTK_GRID(state->app_grid), btn, count % GRID_COLS, count / GRID_COLS, 1, 1);
        count++;
    }

    g_free(lower_search);
    gtk_widget_show_all(state->app_grid);
}

// ═══════════════════════════════════════════════════════════════════════════
// Event Handlers
// ═══════════════════════════════════════════════════════════════════════════

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;

    if (event->keyval == GDK_KEY_Escape) {
        window_hide();
        return TRUE;
    }

    if (event->keyval == GDK_KEY_Return) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
        if (!state->search_hint_visible && commands_check_prefix(text)) {
            commands_execute(text);
            return TRUE;
        }
    }

    return FALSE;
}

static void on_search_changed(GtkEditable *editable, gpointer data) {
    (void)editable; (void)data;
    window_refresh_grid();
}

static void set_search_hint_visible(gboolean visible) {
    if (!state || !state->search_entry) return;
    GtkStyleContext *ctx = gtk_widget_get_style_context(state->search_entry);
    state->search_hint_visible = visible;
    if (visible) {
        gtk_style_context_add_class(ctx, "search-hint");
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), search_hint_text);
        gtk_editable_set_position(GTK_EDITABLE(state->search_entry), 0);
    } else {
        gtk_style_context_remove_class(ctx, "search-hint");
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), "");
    }
}

static gboolean on_search_focus_out(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
    (void)widget; (void)event; (void)data;
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(state->search_entry));
    if (!text || text[0] == '\0')
        set_search_hint_visible(TRUE);
    return FALSE;
}

static gboolean on_search_entry_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget; (void)data;
    if (!state->search_hint_visible) return FALSE;
    gboolean printable = g_unichar_isprint(gdk_keyval_to_unicode(event->keyval));
    if (printable || event->keyval == GDK_KEY_BackSpace || event->keyval == GDK_KEY_Delete)
        set_search_hint_visible(FALSE);
    return FALSE;
}

// ═══════════════════════════════════════════════════════════════════════════
// Window Creation
// ═══════════════════════════════════════════════════════════════════════════

void window_init(void) {
    window_apply_css();

    /* ── Main Window ── */
    state->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state->window), "Applications");
    gtk_window_set_decorated(GTK_WINDOW(state->window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(state->window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(state->window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(state->window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_keep_above(GTK_WINDOW(state->window), TRUE);
    gtk_widget_set_app_paintable(state->window, TRUE);
    gtk_window_set_default_size(GTK_WINDOW(state->window), WINDOW_WIDTH, WINDOW_HEIGHT);
    gtk_window_set_resizable(GTK_WINDOW(state->window), FALSE);
    g_signal_connect(state->window, "realize",
                     G_CALLBACK(on_window_realize_disable_decorations), NULL);

    /* ── RGBA visual ── */
    GdkScreen *screen = gtk_widget_get_screen(state->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(state->window, visual);

    /* ══════════════════════════════════════════
     * Layout:
     *   window
     *     └── app-shell (outer rounded card)
     *           ├── title-bar (icon + title + ··· button)
     *           ├── category-bar (pills)
     *           ├── separator
     *           ├── search-entry (optional, shown when searching)
     *           └── scroll-area
     *                 └── app-grid-inner
     *                       └── GtkGrid
     * ══════════════════════════════════════════ */

    /* Outer shell */
    GtkWidget *shell = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(shell, "app-shell");
    gtk_container_add(GTK_CONTAINER(state->window), shell);

    /* ── Title bar ── */
    GtkWidget *title_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(title_bar, "title-bar");
    gtk_box_pack_start(GTK_BOX(shell), title_bar, FALSE, FALSE, 0);

    /* "A" icon label */
    GtkWidget *icon_lbl = gtk_label_new("✦");
    gtk_widget_set_name(icon_lbl, "title-icon");
    gtk_box_pack_start(GTK_BOX(title_bar), icon_lbl, FALSE, FALSE, 0);

    /* Title */
    GtkWidget *title_lbl = gtk_label_new("Applications");
    gtk_widget_set_name(title_lbl, "app-title");
    gtk_box_pack_start(GTK_BOX(title_bar), title_lbl, FALSE, FALSE, 0);

    /* Spacer */
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(title_bar), spacer, TRUE, TRUE, 0);

    /* ··· button */
    GtkWidget *more_btn = gtk_button_new_with_label("···");
    gtk_widget_set_name(more_btn, "more-button");
    gtk_box_pack_end(GTK_BOX(title_bar), more_btn, FALSE, FALSE, 0);

    /* ── Category pills ── */
    state->category_bar = create_category_bar();
    gtk_box_pack_start(GTK_BOX(shell), state->category_bar, FALSE, FALSE, 0);

    /* ── Thin separator ── */
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(sep, "cat-separator");
    gtk_box_pack_start(GTK_BOX(shell), sep, FALSE, FALSE, 0);

    /* ── Search entry (hidden placeholder, active when user types) ── */
    state->search_entry = gtk_entry_new();
    gtk_widget_set_name(state->search_entry, "search-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(state->search_entry), "Search Applications...");
    gtk_widget_set_no_show_all(state->search_entry, FALSE);
    gtk_box_pack_start(GTK_BOX(shell), state->search_entry, FALSE, FALSE, 0);

    /* ── Scrolled window ── */
    state->scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(state->scroll_window, "scroll-area");
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(state->scroll_window),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(state->scroll_window, TRUE);
    gtk_box_pack_start(GTK_BOX(shell), state->scroll_window, TRUE, TRUE, 0);

    /* ── Inner grid container ── */
    GtkWidget *grid_wrap = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(grid_wrap, "app-grid-inner");
    gtk_container_add(GTK_CONTAINER(state->scroll_window), grid_wrap);

    /* ── App grid ── */
    state->app_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(state->app_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(state->app_grid), 4);
    gtk_widget_set_halign(state->app_grid, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state->app_grid, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(grid_wrap), state->app_grid, TRUE, TRUE, 0);

    /* ── Signals ── */
    g_signal_connect(state->window, "key-press-event", G_CALLBACK(on_key_press), NULL);
    g_signal_connect(state->search_entry, "changed", G_CALLBACK(on_search_changed), NULL);
    g_signal_connect(state->search_entry, "focus-out-event", G_CALLBACK(on_search_focus_out), NULL);
    g_signal_connect(state->search_entry, "key-press-event", G_CALLBACK(on_search_entry_key_press), NULL);
    g_signal_connect(state->window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    state->visible = FALSE;
    state->search_hint_visible = FALSE;
    state->current_category = CAT_ALL;
    set_search_hint_visible(TRUE);
}

// ═══════════════════════════════════════════════════════════════════════════
// Show / Hide
// ═══════════════════════════════════════════════════════════════════════════

void window_show(void) {
    if (!state->visible) {
        set_search_hint_visible(TRUE);
        state->current_category = CAT_ALL;

        for (int i = 0; i < CAT_COUNT; i++) {
            GtkStyleContext *ctx = gtk_widget_get_style_context(category_buttons[i]);
            if (i == 0)
                gtk_style_context_add_class(ctx, "active");
            else
                gtk_style_context_remove_class(ctx, "active");
        }

        window_refresh_grid();

        GdkDisplay *display = gdk_display_get_default();
        GdkMonitor *monitor = gdk_display_get_primary_monitor(display);
        if (!monitor && gdk_display_get_n_monitors(display) > 0)
            monitor = gdk_display_get_monitor(display, 0);

        gint x, y;
        if (monitor) {
            GdkRectangle geometry;
            gdk_monitor_get_geometry(monitor, &geometry);
            x = geometry.x + (geometry.width  - WINDOW_WIDTH)  / 2;
            y = geometry.y + (geometry.height - WINDOW_HEIGHT) / 2;
        } else {
            gtk_window_set_position(GTK_WINDOW(state->window), GTK_WIN_POS_CENTER);
            x = 0; y = 0;
        }

        gtk_window_move(GTK_WINDOW(state->window), x, y);
        gtk_widget_show_all(state->window);
        gtk_window_present(GTK_WINDOW(state->window));

        g_timeout_add(50, (GSourceFunc)gtk_widget_grab_focus, state->search_entry);

        state->visible = TRUE;
    }
}

void window_hide(void) {
    if (state->visible) {
        gtk_widget_hide(state->window);
        state->visible = FALSE;
    }
}

void window_show_with_search(const gchar *query) {
    window_show();
    if (query && query[0] != '\0') {
        set_search_hint_visible(FALSE);
        gtk_entry_set_text(GTK_ENTRY(state->search_entry), query);
        window_refresh_grid();
    }
}

void window_toggle(void) {
    if (state->visible)
        window_hide();
    else
        window_show();
}

void dropdown_show(void) {}
void dropdown_hide(void) {}

/*
 * selection.c
 * Selection state, rubber-band drawing, and desktop background menu.
 */

#include "selection.h"
#include "desktop_config.h"
#include "icons.h"
#include "menu.h"
#include "wallpaper.h"
#include "widgets_manager.h"
#include <math.h>

double start_x = 0;
double start_y = 0;
double current_x = 0;
double current_y = 0;
gboolean is_selecting = FALSE;
GList *selected_items = NULL;
gboolean color_picker_active = FALSE;

gboolean is_selected(GtkWidget *item) {
    return g_list_find(selected_items, item) != NULL;
}

void select_item(GtkWidget *item) {
    if (!is_selected(item)) {
        GtkStyleContext *context;
        selected_items = g_list_append(selected_items, item);
        context = gtk_widget_get_style_context(item);
        gtk_style_context_add_class(context, "selected");
        gtk_widget_queue_draw(item);
    }
}

void deselect_item(GtkWidget *item) {
    GList *l = g_list_find(selected_items, item);
    if (l) {
        GtkStyleContext *context = gtk_widget_get_style_context(item);
        selected_items = g_list_delete_link(selected_items, l);
        gtk_style_context_remove_class(context, "selected");
        gtk_widget_queue_draw(item);
    }
}

void deselect_all(void) {
    for (GList *l = selected_items; l != NULL; l = l->next) {
        GtkWidget *item = GTK_WIDGET(l->data);
        if (GTK_IS_WIDGET(item)) {
            GtkStyleContext *context = gtk_widget_get_style_context(item);
            gtk_style_context_remove_class(context, "selected");
            gtk_widget_queue_draw(item);
        }
    }
    g_list_free(selected_items);
    selected_items = NULL;
}

gboolean on_layout_draw_fg(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)widget;
    (void)data;

    if (is_selecting) {
        double x = MIN(start_x, current_x);
        double y = MIN(start_y, current_y);
        double w = fabs(current_x - start_x);
        double h = fabs(current_y - start_y);

        cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 0.3);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 0.2, 0.6, 1.0, 0.8);
        cairo_set_line_width(cr, 1.0);
        cairo_rectangle(cr, x, y, w, h);
        cairo_stroke(cr);
    }

    return FALSE;
}

static void on_color_picker_activated(GtkWidget *item, gpointer data) {
    (void)item;
    (void)data;
    
    color_picker_active = TRUE;
    
    GdkWindow *gdk_win = gtk_widget_get_window(icon_layout);
    if (gdk_win) {
        GdkDisplay *display = gdk_window_get_display(gdk_win);
        GdkCursor *cursor = gdk_cursor_new_from_name(display, "crosshair");
        if (cursor) {
            gdk_window_set_cursor(gdk_win, cursor);
            g_object_unref(cursor);
        }
    }
}

gboolean on_bg_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)data;

    if (color_picker_active) {
        if (event->button == 1) {
            pick_color_at(event->x, event->y);
            return TRUE;
        } else if (event->button == 3) {
            deactivate_color_picker();
            return TRUE;
        }
    }

    if (event->button == 1) {
        deselect_all();
        is_selecting = TRUE;
        start_x = event->x;
        start_y = event->y;
        current_x = event->x;
        current_y = event->y;
        gtk_widget_queue_draw(icon_layout);
        return TRUE;
    }

    if (event->button == 3) {
        GtkWidget *menu = gtk_menu_new();
        GtkWidget *new_folder = gtk_menu_item_new_with_label("Create Folder");
        GtkWidget *create_doc = gtk_menu_item_new_with_label("📄 Create Document");
        GtkWidget *term = gtk_menu_item_new_with_label("Open Terminal Here");
        GtkWidget *paste = gtk_menu_item_new_with_label("Paste");
        GtkWidget *sort_item = gtk_menu_item_new_with_label("Sort By");
        GtkWidget *sort_menu = gtk_menu_new();
        GtkWidget *refresh = gtk_menu_item_new_with_label("Refresh");
        GtkWidget *templates_sub = build_templates_submenu();
        GtkWidget *wallpaper = gtk_menu_item_new_with_label("🖼 Change Wallpaper");
        GtkWidget *color_picker = gtk_menu_item_new_with_label("🎨 Color Picker");
        GtkWidget *mode_item = gtk_menu_item_new_with_label("💻 Desktop Mode");
        GtkWidget *mode_menu = gtk_menu_new();
        GtkWidget *m_normal = gtk_menu_item_new_with_label("Normal (Desktop)");
        GtkWidget *m_work = gtk_menu_item_new_with_label("Work (Work)");
        GtkWidget *m_widgets = gtk_menu_item_new_with_label("Widgets Only");
        GtkWidget *s_manual = gtk_menu_item_new_with_label("Manual");
        GtkWidget *s_name = gtk_menu_item_new_with_label("Name");
        GtkWidget *s_type = gtk_menu_item_new_with_label("Type");
        GtkWidget *s_date = gtk_menu_item_new_with_label("Date Modified");
        GtkWidget *s_size = gtk_menu_item_new_with_label("Size");
        GtkWidget *edit_widgets = gtk_menu_item_new_with_label("🧩 Edit Widgets");
        DesktopMode current_mode = get_current_desktop_mode();

        style_context_menu(menu);
        style_context_menu(sort_menu);
        style_context_menu(mode_menu);

        gtk_menu_item_set_submenu(GTK_MENU_ITEM(create_doc), templates_sub);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mode_item), mode_menu);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(sort_item), sort_menu);

        g_signal_connect(new_folder, "activate", G_CALLBACK(on_create_folder), widget);
        g_signal_connect(term, "activate", G_CALLBACK(on_open_terminal), NULL);
        g_signal_connect(paste, "activate", G_CALLBACK(on_bg_paste), NULL);
        g_signal_connect(refresh, "activate", G_CALLBACK(on_refresh_clicked), NULL);
        g_signal_connect(wallpaper, "activate", G_CALLBACK(on_change_wallpaper), widget);
        g_signal_connect(color_picker, "activate", G_CALLBACK(on_color_picker_activated), NULL);
        g_signal_connect(m_normal, "activate", G_CALLBACK(on_mode_normal), NULL);
        g_signal_connect(m_work, "activate", G_CALLBACK(on_mode_work), NULL);
        g_signal_connect(m_widgets, "activate", G_CALLBACK(on_mode_widgets), NULL);
        g_signal_connect(s_manual, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_MANUAL));
        g_signal_connect(s_name, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_NAME));
        g_signal_connect(s_type, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_TYPE));
        g_signal_connect(s_date, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_DATE_MODIFIED));
        g_signal_connect(s_size, "activate", G_CALLBACK(on_sort_mode_selected), GINT_TO_POINTER(SORT_SIZE));
        g_signal_connect(edit_widgets, "activate", G_CALLBACK(on_edit_widgets), widget);

        if (current_mode == MODE_NORMAL) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_normal))), "<b>Normal (Desktop)</b>");
        if (current_mode == MODE_WORK) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_work))), "<b>Work (Work)</b>");
        if (current_mode == MODE_WIDGETS) gtk_label_set_markup(GTK_LABEL(gtk_bin_get_child(GTK_BIN(m_widgets))), "<b>Widgets Only</b>");
        sort_mode_to_markup(SORT_MANUAL, s_manual);
        sort_mode_to_markup(SORT_NAME, s_name);
        sort_mode_to_markup(SORT_TYPE, s_type);
        sort_mode_to_markup(SORT_DATE_MODIFIED, s_date);
        sort_mode_to_markup(SORT_SIZE, s_size);

        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_normal);
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_work);
        gtk_menu_shell_append(GTK_MENU_SHELL(mode_menu), m_widgets);

        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_manual);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_name);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_type);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_date);
        gtk_menu_shell_append(GTK_MENU_SHELL(sort_menu), s_size);

        gtk_menu_shell_append(GTK_MENU_SHELL(menu), new_folder);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), create_doc);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), term);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), paste);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mode_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), sort_item);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), edit_widgets);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), refresh);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), wallpaper);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), color_picker);

        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
        return TRUE;
    }

    return FALSE;
}

gboolean on_bg_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    (void)widget;
    (void)data;

    if (is_selecting) {
        GList *children;
        current_x = event->x;
        current_y = event->y;
        gtk_widget_queue_draw(icon_layout);

        {
            double x = MIN(start_x, current_x);
            double y = MIN(start_y, current_y);
            double w = fabs(current_x - start_x);
            double h = fabs(current_y - start_y);

            children = gtk_container_get_children(GTK_CONTAINER(icon_layout));
            for (GList *l = children; l != NULL; l = l->next) {
                GtkWidget *item = GTK_WIDGET(l->data);
                GtkAllocation alloc;
                gtk_widget_get_allocation(item, &alloc);

                if (alloc.x < x + w && alloc.x + alloc.width > x &&
                    alloc.y < y + h && alloc.y + alloc.height > y) {
                    select_item(item);
                } else {
                    deselect_item(item);
                }
            }
            g_list_free(children);
        }
    }

    return FALSE;
}

gboolean on_bg_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)data;

    if (event->button == 1 && is_selecting) {
        is_selecting = FALSE;
        gtk_widget_queue_draw(icon_layout);
    }

    return FALSE;
}

void deactivate_color_picker(void) {
    color_picker_active = FALSE;
    GdkWindow *gdk_win = gtk_widget_get_window(icon_layout);
    if (gdk_win) {
        gdk_window_set_cursor(gdk_win, NULL);
    }
}

static void rgb_to_hsl(guchar r_in, guchar g_in, guchar b_in, int *h, int *s, int *l) {
    double r = r_in / 255.0;
    double g = g_in / 255.0;
    double b = b_in / 255.0;

    double max = MAX(r, MAX(g, b));
    double min = MIN(r, MIN(g, b));
    double delta = max - min;

    double h_val = 0.0;
    double s_val = 0.0;
    double l_val = (max + min) / 2.0;

    if (delta > 0.00001) {
        if (l_val < 0.5) {
            s_val = delta / (max + min);
        } else {
            s_val = delta / (2.0 - max - min);
        }

        if (r == max) {
            h_val = (g - b) / delta + (g < b ? 6.0 : 0.0);
        } else if (g == max) {
            h_val = (b - r) / delta + 2.0;
        } else if (b == max) {
            h_val = (r - g) / delta + 4.0;
        }
        h_val /= 6.0;
    }

    *h = (int)round(h_val * 360.0);
    *s = (int)round(s_val * 100.0);
    *l = (int)round(l_val * 100.0);
}

static gboolean restore_button_label(gpointer user_data) {
    GtkWidget **button_ptr = (GtkWidget **)user_data;
    if (*button_ptr) {
        gtk_button_set_label(GTK_BUTTON(*button_ptr), "Copy");
        g_object_remove_weak_pointer(G_OBJECT(*button_ptr), (gpointer *)button_ptr);
    }
    g_free(button_ptr);
    return G_SOURCE_REMOVE;
}

static void on_copy_clicked(GtkWidget *widget, gpointer data) {
    const char *text = (const char *)data;
    if (text) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, text, -1);
        
        gtk_button_set_label(GTK_BUTTON(widget), "Copied! ✓");
        
        GtkWidget **button_ptr = g_new(GtkWidget *, 1);
        *button_ptr = widget;
        g_object_add_weak_pointer(G_OBJECT(widget), (gpointer *)button_ptr);
        g_timeout_add(1500, restore_button_label, button_ptr);
    }
}

static gboolean on_swatch_draw_rounded(GtkWidget *widget, cairo_t *cr, gpointer data) {
    GdkRGBA *color = (GdkRGBA *)data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    double r = 8.0; // corner radius
    
    cairo_new_sub_path(cr);
    cairo_arc(cr, w - r, r,     r, -G_PI/2, 0);
    cairo_arc(cr, w - r, h - r, r, 0,       G_PI/2);
    cairo_arc(cr, r,     h - r, r, G_PI/2,  G_PI);
    cairo_arc(cr, r,     r,     r, G_PI,    3*G_PI/2);
    cairo_close_path(cr);
    
    cairo_set_source_rgba(cr, color->red, color->green, color->blue, 1.0);
    cairo_fill(cr);
    return TRUE;
}

void pick_color_at(double x, double y) {
    int ix = (int)x;
    int iy = (int)y;
    GdkWindow *gdk_win = gtk_widget_get_window(icon_layout);
    
    if (!gdk_win) {
        deactivate_color_picker();
        return;
    }
    
    int win_w = gdk_window_get_width(gdk_win);
    int win_h = gdk_window_get_height(gdk_win);
    if (ix < 0) ix = 0;
    if (iy < 0) iy = 0;
    if (ix >= win_w) ix = win_w - 1;
    if (iy >= win_h) iy = win_h - 1;
    
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_window(gdk_win, ix, iy, 1, 1);
    if (!pixbuf) {
        deactivate_color_picker();
        return;
    }
    
    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    guchar r = pixels[0];
    guchar g = pixels[1];
    guchar b = pixels[2];
    g_object_unref(pixbuf);
    
    deactivate_color_picker();
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Color Picker Result", GTK_WINDOW(main_window),
                                         GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                         "_Close", GTK_RESPONSE_CLOSE,
                                         NULL);
    
    GdkScreen *screen = gtk_widget_get_screen(dialog);
    if (screen) {
        GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
        if (visual && gdk_screen_is_composited(screen)) {
            gtk_widget_set_visual(dialog, visual);
        }
    }
    
    gtk_widget_set_name(dialog, "desktop-blur-dialog");
    gtk_widget_set_app_paintable(dialog, TRUE);
    
    GtkStyleContext *context = gtk_widget_get_style_context(dialog);
    gtk_style_context_add_class(context, "desktop-blur-dialog");
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    if (content_area) {
        gtk_widget_set_name(content_area, "desktop-blur-dialog-content");
        gtk_widget_set_app_paintable(content_area, TRUE);
        gtk_container_set_border_width(GTK_CONTAINER(content_area), 12);
    }
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 4);
    gtk_container_add(GTK_CONTAINER(content_area), main_box);
    
    GtkWidget *preview_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(main_box), preview_box, FALSE, FALSE, 0);
    
    GdkRGBA color;
    color.red = r / 255.0;
    color.green = g / 255.0;
    color.blue = b / 255.0;
    color.alpha = 1.0;
    
    GtkWidget *swatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(swatch, 70, 70);
    g_signal_connect(swatch, "draw", G_CALLBACK(on_swatch_draw_rounded), &color);
    gtk_box_pack_start(GTK_BOX(preview_box), swatch, FALSE, FALSE, 0);
    
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(preview_box), info_box, TRUE, TRUE, 0);
    
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label), "<span size='large' weight='bold'>Color Captured!</span>");
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0);
    gtk_box_pack_start(GTK_BOX(info_box), title_label, FALSE, FALSE, 0);
    
    GtkWidget *subtitle_label = gtk_label_new("Click Copy to save format to clipboard.");
    gtk_label_set_xalign(GTK_LABEL(subtitle_label), 0.0);
    gtk_box_pack_start(GTK_BOX(info_box), subtitle_label, FALSE, FALSE, 0);
    
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(main_box), grid, TRUE, TRUE, 4);
    
    char hex_str[32];
    char rgb_str[64];
    char hsl_str[64];
    char ox_str[32];
    
    snprintf(hex_str, sizeof(hex_str), "#%02X%02X%02X", r, g, b);
    snprintf(rgb_str, sizeof(rgb_str), "rgb(%d, %d, %d)", r, g, b);
    
    int h_val, s_val, l_val;
    rgb_to_hsl(r, g, b, &h_val, &s_val, &l_val);
    snprintf(hsl_str, sizeof(hsl_str), "hsl(%d, %d%%, %d%%)", h_val, s_val, l_val);
    snprintf(ox_str, sizeof(ox_str), "0x%02x%02x%02xff", r, g, b);
    
    const char *labels[] = { "HEX:", "RGB:", "HSL:", "0xRGBA:" };
    const char *vals[] = { hex_str, rgb_str, hsl_str, ox_str };
    
    for (int i = 0; i < 4; i++) {
        GtkWidget *lbl = gtk_label_new(labels[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_END);
        
        GtkWidget *entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), vals[i]);
        gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
        gtk_widget_set_hexpand(entry, TRUE);
        
        GtkWidget *btn = gtk_button_new_with_label("Copy");
        g_object_set_data_full(G_OBJECT(btn), "copy-text", g_strdup(vals[i]), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_copy_clicked), g_object_get_data(G_OBJECT(btn), "copy-text"));
        
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entry, 1, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), btn, 2, i, 1, 1);
    }
    
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

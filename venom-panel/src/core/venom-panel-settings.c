#include <gtk/gtk.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "venom-panel-plugin-api.h"

static char* panel_config_dir(void) {
    return g_build_filename(g_get_user_config_dir(), "venom", NULL);
}

static char* panel_config_file(void) {
    return g_build_filename(g_get_user_config_dir(), "venom", "panel.conf", NULL);
}

static char* panel_plugins_dir(void) {
    return g_build_filename(g_get_user_config_dir(), "venom", "panel-plugins", NULL);
}

static gboolean path_is_regular_nosymlink(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return FALSE;
    if (S_ISLNK(st.st_mode)) return FALSE;
    return S_ISREG(st.st_mode) ? TRUE : FALSE;
}

typedef struct {
    char type[32];
    char name_or_file[128];
    gboolean expand;
    int padding;
} ConfigItem;

static char g_panel_position[16] = "top"; /* top|bottom|left|right */
static GList *config_items = NULL;
static GtkWidget *listbox;

/* --- Parser & Writer --- */
static const char *normalize_panel_position(const char *pos)
{
    if (!pos) return "top";
    if (g_strcmp0(pos, "top") == 0) return "top";
    if (g_strcmp0(pos, "bottom") == 0) return "bottom";
    if (g_strcmp0(pos, "left") == 0) return "left";
    if (g_strcmp0(pos, "right") == 0) return "right";
    return "top";
}

static void load_config(void) {
    g_list_free_full(config_items, g_free);
    config_items = NULL;
    g_strlcpy(g_panel_position, "top", sizeof(g_panel_position));

    char *contents = NULL;
    char *cfg_file = panel_config_file();
    if (!g_file_get_contents(cfg_file, &contents, NULL, NULL)) {
        g_free(cfg_file);
        return;
    }
    g_free(cfg_file);

    ConfigItem *cur = NULL;
    typedef enum { SEC_NONE, SEC_PANEL, SEC_ITEM } Section;
    Section sec = SEC_NONE;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (!line[0] || line[0] == '#') continue;
        if (!g_strcmp0(line, "[panel]")) {
            if (cur) config_items = g_list_append(config_items, cur);
            cur = NULL;
            sec = SEC_PANEL;
            continue;
        }
        if (!g_strcmp0(line, "[item]")) {
            if (cur) config_items = g_list_append(config_items, cur);
            cur = g_new0(ConfigItem, 1);
            cur->expand = FALSE; cur->padding = 2;
            sec = SEC_ITEM;
            continue;
        }
        char **kv = g_strsplit(line, "=", 2);
        if (kv[0] && kv[1]) {
            char *k = g_strstrip(kv[0]); char *v = g_strstrip(kv[1]);
            if (sec == SEC_PANEL) {
                if (!g_strcmp0(k, "position")) {
                    g_strlcpy(g_panel_position, normalize_panel_position(v), sizeof(g_panel_position));
                }
            } else if (sec == SEC_ITEM && cur) {
                if (!g_strcmp0(k, "type")) strncpy(cur->type, v, 31);
                else if (!g_strcmp0(k, "file") || !g_strcmp0(k, "name")) strncpy(cur->name_or_file, v, 127);
                else if (!g_strcmp0(k, "expand")) cur->expand = !g_strcmp0(v, "true");
                else if (!g_strcmp0(k, "padding")) cur->padding = atoi(v);
            }
        }
        g_strfreev(kv);
    }
    if (cur) config_items = g_list_append(config_items, cur);
    g_strfreev(lines);
    g_free(contents);
}

static void save_config(void) {
    GString *out = g_string_new("# venom-panel layout config\n\n");
    g_string_append(out, "[panel]\n");
    g_string_append_printf(out, "position=%s\n\n", normalize_panel_position(g_panel_position));

    for (GList *l = config_items; l; l = l->next) {
        ConfigItem *item = l->data;
        g_string_append(out, "[item]\n");
        g_string_append_printf(out, "type=%s\n", item->type);
        if (!g_strcmp0(item->type, "plugin"))
            g_string_append_printf(out, "file=%s\n", item->name_or_file);
        else if (!g_strcmp0(item->type, "builtin"))
            g_string_append_printf(out, "name=%s\n", item->name_or_file);
        
        if (g_strcmp0(item->type, "separator") != 0 && g_strcmp0(item->type, "spacer") != 0) {
            g_string_append_printf(out, "expand=%s\n", item->expand ? "true" : "false");
            g_string_append_printf(out, "padding=%d\n", item->padding);
        } else if (!g_strcmp0(item->type, "spacer")) {
            g_string_append_printf(out, "expand=%s\n", item->expand ? "true" : "false");
        }
        g_string_append(out, "\n");
    }
    char *cfg_dir = panel_config_dir();
    char *cfg_file = panel_config_file();
    g_mkdir_with_parents(cfg_dir, 0755);
    g_file_set_contents(cfg_file, out->str, -1, NULL);
    g_free(cfg_dir);
    g_free(cfg_file);
    g_string_free(out, TRUE);
    
    /* Reload panel */
    g_spawn_command_line_async("killall -SIGUSR1 venom-panel", NULL);
}

static void on_position_changed(GtkComboBox *box, gpointer user_data)
{
    (void)user_data;
    const char *id = gtk_combo_box_get_active_id(box);
    g_strlcpy(g_panel_position, normalize_panel_position(id), sizeof(g_panel_position));
    save_config();
}

/* --- UI Helpers --- */
static void on_remove_clicked(GtkButton *btn, gpointer data) {
    GtkWidget *row = GTK_WIDGET(data);
    int index = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));
    ConfigItem *item = g_list_nth_data(config_items, index);
    config_items = g_list_remove(config_items, item);
    g_free(item);
    gtk_widget_destroy(row);
    save_config();
}

static void rebuild_listbox(void);

static void on_move_up(GtkButton *btn, gpointer data) {
    int idx = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(data));
    if (idx <= 0) return;
    GList *l = g_list_nth(config_items, idx);
    ConfigItem *cur = l->data;
    l->data = l->prev->data;
    l->prev->data = cur;
    save_config();
    rebuild_listbox();
}

static void on_move_down(GtkButton *btn, gpointer data) {
    int idx = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(data));
    GList *l = g_list_nth(config_items, idx);
    if (!l || !l->next) return;
    ConfigItem *cur = l->data;
    l->data = l->next->data;
    l->next->data = cur;
    save_config();
    rebuild_listbox();
}

static GtkTargetEntry dnd_targets[] = {
    { (gchar*)"VENOM_PANEL_ROW", GTK_TARGET_SAME_APP, 0 }
};

static gboolean defer_rebuild(gpointer data) {
    save_config();
    rebuild_listbox();
    return FALSE;
}

static void on_drag_data_get(GtkWidget *widget, GdkDragContext *context, GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    (void)widget; (void)context; (void)info; (void)time;
    GtkWidget *row = GTK_WIDGET(user_data);
    int index = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));
    gtk_selection_data_set(data, gdk_atom_intern("VENOM_PANEL_ROW", FALSE), 8, (const guchar *)&index, sizeof(int));
}

static void on_drag_data_received(GtkWidget *widget, GdkDragContext *context, int x, int y, GtkSelectionData *data, guint info, guint time, gpointer user_data) {
    (void)widget; (void)x; (void)y; (void)info;
    GtkWidget *row = GTK_WIDGET(user_data);
    if (gtk_selection_data_get_length(data) == sizeof(int)) {
        int source_index = *(int *)gtk_selection_data_get_data(data);
        int dest_index = gtk_list_box_row_get_index(GTK_LIST_BOX_ROW(row));
        
        if (source_index >= 0 && dest_index >= 0 && source_index != dest_index) {
            ConfigItem *item = g_list_nth_data(config_items, source_index);
            if (item) {
                config_items = g_list_remove(config_items, item);
                config_items = g_list_insert(config_items, item, dest_index);
                g_idle_add(defer_rebuild, NULL);
            }
        }
        gtk_drag_finish(context, TRUE, FALSE, time);
    } else {
        gtk_drag_finish(context, FALSE, FALSE, time);
    }
}

static GtkWidget* create_row(ConfigItem *item) {
    GtkWidget *row = gtk_list_box_row_new();
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *drag_handle = gtk_event_box_new();
    GtkWidget *drag_icon = gtk_image_new_from_icon_name("view-grid-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_container_add(GTK_CONTAINER(drag_handle), drag_icon);
    gtk_widget_set_tooltip_text(drag_handle, "Drag to reorder");
    
    gtk_drag_source_set(drag_handle, GDK_BUTTON1_MASK, dnd_targets, 1, GDK_ACTION_MOVE);
    gtk_drag_dest_set(row, GTK_DEST_DEFAULT_ALL, dnd_targets, 1, GDK_ACTION_MOVE);
    g_signal_connect(drag_handle, "drag-data-get", G_CALLBACK(on_drag_data_get), row);
    g_signal_connect(row, "drag-data-received", G_CALLBACK(on_drag_data_received), row);

    gchar *label_text = NULL;
    const char *icon_name = "application-x-addon";
    if (!g_strcmp0(item->type, "separator")) {
        label_text = g_strdup("<b>Separator</b>");
        icon_name = "list-remove-symbolic";
    } else if (!g_strcmp0(item->type, "spacer")) {
        label_text = g_markup_printf_escaped("<b>Spacer</b> (expands: %s)", item->expand ? "yes" : "no");
        icon_name = "go-next-symbolic"; 
    } else {
        label_text = g_markup_printf_escaped("<b>%s</b> <small>(%s)</small>", item->name_or_file, item->type);
    }

    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_BUTTON);
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), label_text);
    g_free(label_text);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    
    gtk_box_pack_start(GTK_BOX(box), drag_handle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lbl, TRUE, TRUE, 0);

    GtkWidget *btn_up = gtk_button_new_from_icon_name("go-up-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_dn = gtk_button_new_from_icon_name("go-down-symbolic", GTK_ICON_SIZE_BUTTON);
    GtkWidget *btn_rm = gtk_button_new_from_icon_name("user-trash-symbolic", GTK_ICON_SIZE_BUTTON);
    
    g_signal_connect(btn_up, "clicked", G_CALLBACK(on_move_up), row);
    g_signal_connect(btn_dn, "clicked", G_CALLBACK(on_move_down), row);
    g_signal_connect(btn_rm, "clicked", G_CALLBACK(on_remove_clicked), row);
    
    gtk_box_pack_end(GTK_BOX(box), btn_rm, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), btn_dn, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), btn_up, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_widget_show_all(row);
    return row;
}

static void rebuild_listbox(void) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(listbox));
    for (GList *l = children; l; l = l->next) gtk_widget_destroy(l->data);
    g_list_free(children);

    for (GList *l = config_items; l; l = l->next) {
        gtk_list_box_insert(GTK_LIST_BOX(listbox), create_row((ConfigItem*)l->data), -1);
    }
}

static void on_add_item(GtkButton *btn, gpointer data) {
    (void)btn;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Item", GTK_WINDOW(data),
                                          GTK_DIALOG_MODAL,
                                          "Cancel", GTK_RESPONSE_CANCEL,
                                          "Add", GTK_RESPONSE_ACCEPT,
                                          NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *combo = gtk_combo_box_text_new();
    
    /* Dynamically load plugins from directory */
    char *plugins_dir = panel_plugins_dir();
    g_mkdir_with_parents(plugins_dir, 0755);
    DIR *dir = opendir(plugins_dir);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (g_str_has_suffix(ent->d_name, ".so")) {
                char *full_path = g_build_filename(plugins_dir, ent->d_name, NULL);
                if (!path_is_regular_nosymlink(full_path)) {
                    g_free(full_path);
                    continue;
                }

                void *handle = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
                if (handle) {
                    VenomPanelPluginInitFnV2 init_func_v2 = (VenomPanelPluginInitFnV2)dlsym(handle, "venom_panel_plugin_init_v2");
                    VenomPanelPluginInitFn init_func_v1 = (VenomPanelPluginInitFn)dlsym(handle, "venom_panel_plugin_init");

                    const char *plugin_name = ent->d_name;
                    if (init_func_v2) {
                        VenomPanelPluginAPIv2 *api2 = init_func_v2();
                        const size_t min_sz = offsetof(VenomPanelPluginAPIv2, create_widget) + sizeof(((VenomPanelPluginAPIv2 *)0)->create_widget);
                        if (api2 && api2->api_version == VENOM_PANEL_PLUGIN_API_VERSION && api2->struct_size >= min_sz && api2->name && g_utf8_validate(api2->name, -1, NULL)) {
                            plugin_name = api2->name;
                        }
                    } else if (init_func_v1) {
                        VenomPanelPluginAPI *api1 = init_func_v1();
                        if (api1 && api1->name && g_utf8_validate(api1->name, -1, NULL)) plugin_name = api1->name;
                    }

                    if (init_func_v2 || init_func_v1) {
                        char id_str[256];
                        char label_str[256];
                        snprintf(id_str, sizeof(id_str), "plugin:%s", ent->d_name);
                        snprintf(label_str, sizeof(label_str), "Plugin: %s", plugin_name);
                        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), id_str, label_str);
                    }
                    dlclose(handle);
                }
                g_free(full_path);
            }
        }
        closedir(dir);
    }
    g_free(plugins_dir);
    
    /* Append static builtin items */
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:app-menu", "Builtin: App Menu");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:tray", "Builtin: System Tray");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:power", "Builtin: Power Menu");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:clock", "Builtin: Clock");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:system-icons", "Builtin: Wi-Fi / Sound");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:kb-indicator", "Builtin: Keyboard Layout");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:control-center", "Builtin: Control Center Button");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "builtin:notification-center", "Builtin: Notification Center Button");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "spacer", "Spacer (Expands)");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "separator", "Separator Line");
    
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    
    gtk_box_pack_start(GTK_BOX(content), combo, TRUE, TRUE, 8);
    gtk_widget_show_all(dialog);
    
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
        if (id) {
            ConfigItem *it = g_new0(ConfigItem, 1);
            it->expand = FALSE; it->padding = 2;
            if (!g_strcmp0(id, "spacer")) { strcpy(it->type, "spacer"); it->expand = TRUE; }
            else if (!g_strcmp0(id, "separator")) { strcpy(it->type, "separator"); }
            else {
                char **parts = g_strsplit(id, ":", 2);
                if (parts && parts[0] && parts[1]) {
                    g_strlcpy(it->type, parts[0], sizeof(it->type));
                    g_strlcpy(it->name_or_file, parts[1], sizeof(it->name_or_file));
                }
                g_strfreev(parts);
            }
            config_items = g_list_append(config_items, it);
            save_config();
            rebuild_listbox();
        }
    }
    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    load_config();

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Panel Preferences");
    gtk_window_set_default_size(GTK_WINDOW(window), 450, 500);

    /* Enable transparency and set background color */
    GdkScreen *screen = gtk_widget_get_screen(window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual != NULL) {
        gtk_widget_set_visual(window, visual);
    }
    gtk_widget_set_app_paintable(window, TRUE);

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "window { background: rgba(0, 0, 0, 0.541); }", -1, NULL);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Panel position */
    GtkWidget *pos_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(pos_box, 8);
    gtk_widget_set_margin_end(pos_box, 8);
    gtk_widget_set_margin_top(pos_box, 12);
    gtk_widget_set_margin_bottom(pos_box, 6);

    GtkWidget *pos_label = gtk_label_new("Panel Position");
    gtk_widget_set_halign(pos_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(pos_box), pos_label, TRUE, TRUE, 0);

    GtkWidget *pos_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), "top", "Top");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), "bottom", "Bottom");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), "left", "Left");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(pos_combo), "right", "Right");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(pos_combo), normalize_panel_position(g_panel_position));
    g_signal_connect(pos_combo, "changed", G_CALLBACK(on_position_changed), NULL);
    gtk_box_pack_end(GTK_BOX(pos_box), pos_combo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), pos_box, FALSE, FALSE, 0);

    /* Header */
    GtkWidget *header = gtk_label_new("<b>Panel Items</b>");
    gtk_label_set_use_markup(GTK_LABEL(header), TRUE);
    gtk_widget_set_margin_top(header, 6);
    gtk_widget_set_margin_bottom(header, 12);
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* ListBox inside ScrolledWindow */
    listbox = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_NONE);
    rebuild_listbox();

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), listbox);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(toolbar, 8);
    gtk_widget_set_margin_end(toolbar, 8);
    gtk_widget_set_margin_top(toolbar, 8);
    gtk_widget_set_margin_bottom(toolbar, 8);
    
    GtkWidget *btn_add = gtk_button_new_with_label("+ Add Item");
    g_signal_connect(btn_add, "clicked", G_CALLBACK(on_add_item), window);
    gtk_box_pack_start(GTK_BOX(toolbar), btn_add, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}

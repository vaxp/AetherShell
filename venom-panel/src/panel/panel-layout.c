#include <gtk/gtk.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dlfcn.h>
#include <glib-unix.h>
#include <sys/stat.h>

#include "panel-layout.h"
#include "panel-builtins.h"
#include "panel-geometry.h"
#include "venom-panel-plugin-api.h"

typedef enum {
    ITEM_TYPE_PLUGIN,
    ITEM_TYPE_BUILTIN,
    ITEM_TYPE_SPACER,
    ITEM_TYPE_SEPARATOR,
} PanelItemType;

typedef struct {
    PanelItemType type;
    char file[256];
    char name[64];
    gboolean expand;
    int padding;
} PanelItem;

typedef struct {
    const VenomPanelPluginAPIv2 *api_v2;
    gboolean destroyed;
} PanelPluginInstance;

static GtkWidget *g_panel_hbox = NULL;
static GHashTable *g_plugin_handles = NULL;

static char *panel_config_dir(void)
{
    return g_build_filename(g_get_user_config_dir(), "venom", NULL);
}

static char *panel_config_file(void)
{
    return g_build_filename(g_get_user_config_dir(), "venom", "panel.conf", NULL);
}

static char *panel_plugins_dir(void)
{
    return g_build_filename(g_get_user_config_dir(), "venom", "panel-plugins", NULL);
}

static void write_default_config(void)
{
    char *cfg_dir = panel_config_dir();
    char *cfg_file = panel_config_file();
    g_mkdir_with_parents(cfg_dir, 0755);
    const char *cfg =
        "# venom-panel layout — edit and restart to apply changes\n"
        "# Types: plugin, builtin, spacer, separator\n"
        "# Builtins: app-menu, tray, power, system-icons, kb-indicator, clock, control-center, notification-center, workspaces\n"
        "\n"
        "[panel]\nposition=top\n"
        "\n"
        "[item]\ntype=builtin\nname=app-menu\nexpand=false\npadding=4\n"
        "\n"
        "[item]\ntype=builtin\nname=workspaces\nexpand=false\npadding=4\n"
        "\n"
        "[item]\ntype=spacer\nexpand=true\npadding=0\n"
        "\n"
        "[item]\ntype=builtin\nname=tray\nexpand=false\npadding=4\n"
        "\n"
        "[item]\ntype=separator\n"
        "\n"
        "[item]\ntype=builtin\nname=power\nexpand=false\npadding=2\n"
        "\n"
        "[item]\ntype=separator\n"
        "\n"
        "[item]\ntype=builtin\nname=system-icons\nexpand=false\npadding=2\n"
        "\n"
        "[item]\ntype=builtin\nname=kb-indicator\nexpand=false\npadding=2\n"
        "\n"
        "[item]\ntype=builtin\nname=clock\nexpand=false\npadding=4\n"
        "\n"
        "[item]\ntype=separator\n"
        "\n"
        "[item]\ntype=builtin\nname=control-center\nexpand=false\npadding=2\n";
    g_file_set_contents(cfg_file, cfg, -1, NULL);
    g_print("[Panel] Created default config at %s\n", cfg_file);
    g_free(cfg_dir);
    g_free(cfg_file);
}

static GArray *parse_panel_config(void)
{
    char *cfg_file = panel_config_file();
    if (!g_file_test(cfg_file, G_FILE_TEST_EXISTS)) write_default_config();

    GArray *items = g_array_new(FALSE, TRUE, sizeof(PanelItem));
    char *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(cfg_file, &contents, &length, NULL)) {
        g_warning("[Panel] Could not read %s", cfg_file);
        g_free(cfg_file);
        return items;
    }
    g_free(cfg_file);

    PanelItem cur;
    memset(&cur, 0, sizeof(cur));
    cur.expand = FALSE;
    cur.padding = 2;
    gboolean in_item = FALSE;

    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (line[0] == '#' || line[0] == '\0') continue;
        if (g_strcmp0(line, "[item]") == 0) {
            if (in_item) g_array_append_val(items, cur);
            memset(&cur, 0, sizeof(cur));
            cur.expand = FALSE;
            cur.padding = 2;
            in_item = TRUE;
            continue;
        }
        if (!in_item) continue;
        char **kv = g_strsplit(line, "=", 2);
        if (!kv[0] || !kv[1]) {
            g_strfreev(kv);
            continue;
        }
        char *k = g_strstrip(kv[0]), *v = g_strstrip(kv[1]);
        if (!g_strcmp0(k, "type")) {
            if (!g_strcmp0(v, "plugin")) cur.type = ITEM_TYPE_PLUGIN;
            else if (!g_strcmp0(v, "builtin")) cur.type = ITEM_TYPE_BUILTIN;
            else if (!g_strcmp0(v, "spacer")) cur.type = ITEM_TYPE_SPACER;
            else if (!g_strcmp0(v, "separator")) cur.type = ITEM_TYPE_SEPARATOR;
        } else if (!g_strcmp0(k, "file"))
            strncpy(cur.file, v, sizeof(cur.file) - 1);
        else if (!g_strcmp0(k, "name"))
            strncpy(cur.name, v, sizeof(cur.name) - 1);
        else if (!g_strcmp0(k, "expand"))
            cur.expand = !g_strcmp0(v, "true");
        else if (!g_strcmp0(k, "padding")) {
            cur.padding = atoi(v);
            if (cur.padding < 0) cur.padding = 0;
            if (cur.padding > 128) cur.padding = 128;
        }
        g_strfreev(kv);
    }
    if (in_item) g_array_append_val(items, cur);
    g_strfreev(lines);
    g_free(contents);
    return items;
}

static gboolean plugin_is_safe_basename(const char *name)
{
    if (!name || !name[0]) return FALSE;
    if (g_path_is_absolute(name)) return FALSE;
    if (strchr(name, G_DIR_SEPARATOR)) return FALSE;
    if (g_strrstr(name, "..")) return FALSE;
    if (!g_str_has_suffix(name, ".so")) return FALSE;
    return TRUE;
}

static gboolean path_is_regular_nosymlink(const char *path)
{
    struct stat st;
    if (lstat(path, &st) != 0) return FALSE;
    if (S_ISLNK(st.st_mode)) return FALSE;
    return S_ISREG(st.st_mode) ? TRUE : FALSE;
}

static void plugin_instance_destroy(PanelPluginInstance *inst, GtkWidget *widget)
{
    if (!inst || inst->destroyed) return;
    inst->destroyed = TRUE;
    if (inst->api_v2 && inst->api_v2->destroy_widget) inst->api_v2->destroy_widget(widget);
}

static void on_plugin_widget_destroy(GtkWidget *widget, gpointer user_data)
{
    plugin_instance_destroy((PanelPluginInstance *)user_data, widget);
}

static void plugin_instance_free(gpointer data)
{
    PanelPluginInstance *inst = (PanelPluginInstance *)data;
    if (!inst) return;
    g_free(inst);
}

static void load_items_from_config(GtkWidget *hbox)
{
    GArray *items = parse_panel_config();
    char *plugins_dir = panel_plugins_dir();
    g_mkdir_with_parents(plugins_dir, 0755);

    for (guint i = 0; i < items->len; i++) {
        PanelItem *item = &g_array_index(items, PanelItem, i);
        GtkWidget *widget = NULL;
        gboolean expand = item->expand;
        int padding = item->padding;

        switch (item->type) {
            case ITEM_TYPE_SPACER:
                widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
                expand = TRUE;
                break;
            case ITEM_TYPE_SEPARATOR:
                widget = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
                gtk_widget_set_margin_top(widget, 6);
                gtk_widget_set_margin_bottom(widget, 6);
                expand = FALSE;
                padding = 4;
                break;
            case ITEM_TYPE_BUILTIN:
                widget = panel_builtin_create(item->name);
                if (!widget) g_warning("[Panel] Unknown builtin: '%s'", item->name);
                break;
            case ITEM_TYPE_PLUGIN: {
                if (!plugin_is_safe_basename(item->file)) {
                    g_warning("[Panel] Refusing plugin path '%s' (must be a basename ending with .so)", item->file);
                    GtkWidget *warn = gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
                    gtk_widget_set_tooltip_text(warn, "Invalid plugin filename in config");
                    widget = warn;
                    break;
                }

                char *fp = g_build_filename(plugins_dir, item->file, NULL);
                if (!path_is_regular_nosymlink(fp)) {
                    g_warning("[Panel] Refusing plugin '%s' (not a regular file or is a symlink)", item->file);
                    GtkWidget *warn = gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
                    char *tip = g_strdup_printf("Plugin is not a regular file: %s", item->file);
                    gtk_widget_set_tooltip_text(warn, tip);
                    g_free(tip);
                    widget = warn;
                    g_free(fp);
                    break;
                }

                gboolean opened_new = FALSE;
                void *h = NULL;
                if (!g_plugin_handles) g_plugin_handles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
                h = g_hash_table_lookup(g_plugin_handles, fp);
                if (!h) {
                    opened_new = TRUE;
                    h = dlopen(fp, RTLD_NOW | RTLD_LOCAL);
                }
                if (!h) {
                    g_warning("[Panel] dlopen %s: %s", item->file, dlerror());
                    GtkWidget *warn = gtk_image_new_from_icon_name("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
                    char *tip = g_strdup_printf("Plugin missing or crashed: %s", item->file);
                    gtk_widget_set_tooltip_text(warn, tip);
                    g_free(tip);
                    widget = warn;
                } else {
                    VenomPanelPluginInitFnV2 fn2 = (VenomPanelPluginInitFnV2)dlsym(h, "venom_panel_plugin_init_v2");
                    VenomPanelPluginInitFn fn1 = (VenomPanelPluginInitFn)dlsym(h, "venom_panel_plugin_init");

                    const char *plugin_display_name = item->file;
                    const VenomPanelPluginAPIv2 *api2 = NULL;
                    const VenomPanelPluginAPI *api1 = NULL;

                    if (fn2) {
                        api2 = fn2();
                        const size_t min_sz =
                            offsetof(VenomPanelPluginAPIv2, create_widget) +
                            sizeof(((VenomPanelPluginAPIv2 *)0)->create_widget);
                        if (!api2 || api2->api_version != VENOM_PANEL_PLUGIN_API_VERSION || api2->struct_size < min_sz) {
                            g_warning("[Panel] Invalid v2 API in %s", item->file);
                            api2 = NULL;
                        } else if (api2->name) {
                            plugin_display_name = api2->name;
                        }
                    } else if (fn1) {
                        api1 = fn1();
                        if (api1 && api1->name) plugin_display_name = api1->name;
                    }

                    if ((!api2 || !api2->create_widget) && (!api1 || !api1->create_widget)) {
                        g_warning("[Panel] No valid init symbol in %s", item->file);
                        if (opened_new) dlclose(h);
                        GtkWidget *warn = gtk_image_new_from_icon_name("dialog-error-symbolic", GTK_ICON_SIZE_MENU);
                        char *tip = g_strdup_printf("Invalid plugin: %s", item->file);
                        gtk_widget_set_tooltip_text(warn, tip);
                        g_free(tip);
                        widget = warn;
                    } else {
                        if (opened_new) g_hash_table_insert(g_plugin_handles, g_strdup(fp), h);
                        widget = api2 && api2->create_widget ? api2->create_widget() : api1->create_widget();
                        if (!widget) {
                            GtkWidget *warn = gtk_image_new_from_icon_name("dialog-error-symbolic", GTK_ICON_SIZE_MENU);
                            char *tip = g_strdup_printf("Plugin %s failed to create UI", plugin_display_name);
                            gtk_widget_set_tooltip_text(warn, tip);
                            g_free(tip);
                            widget = warn;
                        } else {
                            PanelPluginInstance *inst = g_new0(PanelPluginInstance, 1);
                            inst->api_v2 = api2;
                            inst->destroyed = FALSE;
                            g_signal_connect(widget, "destroy", G_CALLBACK(on_plugin_widget_destroy), inst);
                            g_object_set_data_full(G_OBJECT(widget),
                                                   "venom-panel-plugin-instance",
                                                   inst,
                                                   plugin_instance_free);
                            g_print("[Panel] Plugin '%s' loaded (expand=%d pad=%d)\n",
                                    plugin_display_name,
                                    expand,
                                    padding);
                        }
                    }
                }
                g_free(fp);
                break;
            }
        }

        if (widget) {
            gtk_box_pack_start(GTK_BOX(hbox), widget, expand, expand, padding);
            gtk_widget_show_all(widget);
        }
    }
    g_array_free(items, TRUE);
    g_free(plugins_dir);
}

static gboolean on_panel_sigusr1(gpointer user_data)
{
    (void)user_data;
    g_print("[Panel] Received reload signal! Reloading config...\n");
    if (!g_panel_hbox) return G_SOURCE_CONTINUE;

    GtkWidget *toplevel = gtk_widget_get_toplevel(g_panel_hbox);
    if (GTK_IS_WINDOW(toplevel)) {
        panel_geometry_apply(toplevel);
    }

    panel_builtins_prepare_reload();

    GList *children = gtk_container_get_children(GTK_CONTAINER(g_panel_hbox));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);

    PanelEdge edge = panel_geometry_get_config_edge();
    GtkOrientation want = panel_geometry_edge_orientation(edge);
    GtkOrientation have = GTK_ORIENTATION_HORIZONTAL;
    if (GTK_IS_ORIENTABLE(g_panel_hbox)) have = gtk_orientable_get_orientation(GTK_ORIENTABLE(g_panel_hbox));

    if (want != have && GTK_IS_WINDOW(toplevel)) {
        GtkWidget *new_box = gtk_box_new(want, 0);
        gtk_widget_set_margin_start(new_box, 4);
        gtk_widget_set_margin_end(new_box, 4);
        gtk_widget_set_margin_top(new_box, 4);
        gtk_widget_set_margin_bottom(new_box, 4);

        gtk_container_remove(GTK_CONTAINER(toplevel), g_panel_hbox);
        gtk_container_add(GTK_CONTAINER(toplevel), new_box);
        gtk_widget_show_all(new_box);

        g_panel_hbox = new_box;
    }

    load_items_from_config(g_panel_hbox);
    return G_SOURCE_CONTINUE;
}

void panel_layout_load(GtkWidget *hbox)
{
    g_panel_hbox = hbox;
    load_items_from_config(hbox);
}

void panel_layout_enable_live_reload(void)
{
    g_unix_signal_add(SIGUSR1, on_panel_sigusr1, NULL);
}

void panel_layout_cleanup(void)
{
    g_panel_hbox = NULL;
    /* Keep plugin handles cached for performance; no dlclose here. */
}

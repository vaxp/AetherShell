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
#include "vpanel-plugin-api.h"
#include "plugin-sandbox.h"

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum {
    ITEM_TYPE_PLUGIN,
    ITEM_TYPE_BUILTIN,
    ITEM_TYPE_SPACER,
    ITEM_TYPE_SEPARATOR,
} PanelItemType;

/* Per-plugin visual config parsed from panel.conf */
typedef struct {
    gboolean valid;              /* TRUE if any visual key was set          */
    /* Background */
    gboolean has_bg;
    double   bg_r, bg_g, bg_b, bg_a;
    gboolean gradient;
    double   bg_r2, bg_g2, bg_b2;
    /* Border */
    gboolean has_border;
    double   border_r, border_g, border_b, border_a;
    int      border_width;
    int      border_radius;
} ItemVisuals;

/* One item from panel.conf */
typedef struct {
    PanelItemType  type;
    char           file[256];   /* plugin .so basename */
    char           name[64];    /* builtin name        */
    gboolean       expand;
    int            padding;
    /* Extra key=value pairs forwarded to the plugin via on_config_changed */
    GHashTable    *extra_keys;  /* char* -> char*       */
    /* Visual overrides parsed from panel.conf */
    ItemVisuals    visuals;
} PanelItem;

/* =========================================================================
 * Module state
 * ========================================================================= */

static GtkWidget *g_panel_hbox = NULL;
/* List of PluginSandbox* for live plugins (used during reload cleanup) */
static GSList    *g_sandboxes  = NULL;

/* =========================================================================
 * Config path helpers
 * ========================================================================= */

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

/* =========================================================================
 * Default config writer
 * ========================================================================= */

static void write_default_config(void)
{
    char *cfg_dir  = panel_config_dir();
    char *cfg_file = panel_config_file();
    g_mkdir_with_parents(cfg_dir, 0755);
    const char *cfg =
        "# vpanel layout — edit and restart to apply changes\n"
        "# Types: plugin, builtin, spacer, separator\n"
        "# Builtins: app-menu, tray, power, system-icons, kb-indicator,\n"
        "#           clock, control-center, notification-center, workspaces\n"
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

/* =========================================================================
 * Colour parser:  #rrggbb or #rrggbbaa → RGBA doubles in [0,1]
 * ========================================================================= */

static gboolean parse_hex_colour(const char *hex,
                                  double *r, double *g, double *b, double *a)
{
    if (!hex || hex[0] != '#') return FALSE;
    const char *h = hex + 1;
    gsize len = strlen(h);
    unsigned int ri=0, gi=0, bi=0, ai=255;

    if (len == 6) {
        if (sscanf(h, "%02x%02x%02x", &ri, &gi, &bi) != 3) return FALSE;
    } else if (len == 8) {
        if (sscanf(h, "%02x%02x%02x%02x", &ri, &gi, &bi, &ai) != 4) return FALSE;
    } else {
        return FALSE;
    }
    *r = ri / 255.0;
    *g = gi / 255.0;
    *b = bi / 255.0;
    *a = ai / 255.0;
    return TRUE;
}

/* =========================================================================
 * Config parser
 * ========================================================================= */

static void panel_item_free_extra(PanelItem *item)
{
    if (item->extra_keys) {
        g_hash_table_destroy(item->extra_keys);
        item->extra_keys = NULL;
    }
}

static GArray *parse_panel_config(void)
{
    char *cfg_file = panel_config_file();
    if (!g_file_test(cfg_file, G_FILE_TEST_EXISTS)) write_default_config();

    GArray *items = g_array_new(FALSE, TRUE, sizeof(PanelItem));
    char *contents = NULL;
    if (!g_file_get_contents(cfg_file, &contents, NULL, NULL)) {
        g_warning("[Panel] Could not read %s", cfg_file);
        g_free(cfg_file);
        return items;
    }
    g_free(cfg_file);

    PanelItem cur;
    memset(&cur, 0, sizeof(cur));
    cur.expand  = FALSE;
    cur.padding = 2;
    gboolean in_item = FALSE;

    /* Known visual keys (handled here, not forwarded to plugin) */
    static const char * const visual_keys[] = {
        "bg_color", "bg_alpha", "bg_gradient_end",
        "border_color", "border_width", "border_radius",
        NULL
    };

    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (line[0] == '#' || line[0] == '\0') continue;

        if (g_strcmp0(line, "[item]") == 0) {
            if (in_item) g_array_append_val(items, cur);
            memset(&cur, 0, sizeof(cur));
            cur.expand  = FALSE;
            cur.padding = 2;
            in_item = TRUE;
            continue;
        }
        if (!in_item) continue;

        char **kv = g_strsplit(line, "=", 2);
        if (!kv[0] || !kv[1]) { g_strfreev(kv); continue; }

        char *k = g_strstrip(kv[0]);
        char *v = g_strstrip(kv[1]);

        /* ── Standard keys ── */
        if (!g_strcmp0(k, "type")) {
            if (!g_strcmp0(v, "plugin"))    cur.type = ITEM_TYPE_PLUGIN;
            else if (!g_strcmp0(v, "builtin"))  cur.type = ITEM_TYPE_BUILTIN;
            else if (!g_strcmp0(v, "spacer"))   cur.type = ITEM_TYPE_SPACER;
            else if (!g_strcmp0(v, "separator"))cur.type = ITEM_TYPE_SEPARATOR;
        } else if (!g_strcmp0(k, "file")) {
            strncpy(cur.file, v, sizeof(cur.file) - 1);
        } else if (!g_strcmp0(k, "name")) {
            strncpy(cur.name, v, sizeof(cur.name) - 1);
        } else if (!g_strcmp0(k, "expand")) {
            cur.expand = !g_strcmp0(v, "true");
        } else if (!g_strcmp0(k, "padding")) {
            cur.padding = CLAMP(atoi(v), 0, 128);
        }

        /* ── Visual override keys ── */
        else if (!g_strcmp0(k, "bg_color")) {
            double r, g2, b2, a;
            if (parse_hex_colour(v, &r, &g2, &b2, &a)) {
                cur.visuals.has_bg = TRUE;
                cur.visuals.valid  = TRUE;
                cur.visuals.bg_r   = r;
                cur.visuals.bg_g   = g2;
                cur.visuals.bg_b   = b2;
                cur.visuals.bg_a   = a;
            }
        } else if (!g_strcmp0(k, "bg_alpha")) {
            cur.visuals.bg_a  = CLAMP(g_ascii_strtod(v, NULL), 0.0, 1.0);
            cur.visuals.valid = TRUE;
        } else if (!g_strcmp0(k, "bg_gradient_end")) {
            double r, g2, b2, a;
            if (parse_hex_colour(v, &r, &g2, &b2, &a)) {
                cur.visuals.gradient = TRUE;
                cur.visuals.valid    = TRUE;
                cur.visuals.bg_r2    = r;
                cur.visuals.bg_g2    = g2;
                cur.visuals.bg_b2    = b2;
            }
        } else if (!g_strcmp0(k, "border_color")) {
            double r, g2, b2, a;
            if (parse_hex_colour(v, &r, &g2, &b2, &a)) {
                cur.visuals.has_border  = TRUE;
                cur.visuals.valid       = TRUE;
                cur.visuals.border_r    = r;
                cur.visuals.border_g    = g2;
                cur.visuals.border_b    = b2;
                cur.visuals.border_a    = a;
            }
        } else if (!g_strcmp0(k, "border_width")) {
            cur.visuals.border_width = CLAMP(atoi(v), 0, 32);
            cur.visuals.has_border   = TRUE;
            cur.visuals.valid        = TRUE;
        } else if (!g_strcmp0(k, "border_radius")) {
            cur.visuals.border_radius = CLAMP(atoi(v), 0, 64);
            cur.visuals.valid         = TRUE;
        }

        /* ── Everything else forwarded to plugin as extra config ── */
        else {
            /* Only forward for plugin items */
            gboolean is_visual = FALSE;
            for (int vi = 0; visual_keys[vi]; vi++) {
                if (!g_strcmp0(k, visual_keys[vi])) { is_visual = TRUE; break; }
            }
            if (!is_visual) {
                if (!cur.extra_keys)
                    cur.extra_keys = g_hash_table_new_full(g_str_hash,
                                                           g_str_equal,
                                                           g_free, g_free);
                g_hash_table_insert(cur.extra_keys, g_strdup(k), g_strdup(v));
            }
        }

        g_strfreev(kv);
    }
    if (in_item) g_array_append_val(items, cur);
    g_strfreev(lines);
    g_free(contents);
    return items;
}

/* =========================================================================
 * CSS Visual application
 * Builds a CSS string from ItemVisuals and applies it to the slot widget.
 * ========================================================================= */

static void apply_item_visuals(GtkWidget *slot, const ItemVisuals *v)
{
    if (!v || !v->valid) return;

    GString *css = g_string_new(".vpanel-plugin-slot {");

    /* Background */
    if (v->has_bg) {
        if (v->gradient) {
            g_string_append_printf(css,
                "background: linear-gradient(to right,"
                " rgba(%d,%d,%d,%.3f), rgba(%d,%d,%d,%.3f));",
                (int)(v->bg_r*255),  (int)(v->bg_g*255),
                (int)(v->bg_b*255),  v->bg_a,
                (int)(v->bg_r2*255), (int)(v->bg_g2*255),
                (int)(v->bg_b2*255), v->bg_a);
        } else {
            g_string_append_printf(css,
                "background-color: rgba(%d,%d,%d,%.3f);",
                (int)(v->bg_r*255), (int)(v->bg_g*255),
                (int)(v->bg_b*255), v->bg_a);
        }
    }

    /* Border */
    if (v->has_border && v->border_width > 0) {
        g_string_append_printf(css,
            "border: %dpx solid rgba(%d,%d,%d,%.3f);",
            v->border_width,
            (int)(v->border_r*255), (int)(v->border_g*255),
            (int)(v->border_b*255), v->border_a);
    }
    if (v->border_radius > 0) {
        g_string_append_printf(css, "border-radius: %dpx;", v->border_radius);
    }

    g_string_append(css, "}");

    GtkCssProvider *prov = gtk_css_provider_new();
    GError *err = NULL;
    gtk_css_provider_load_from_data(prov, css->str, -1, &err);
    if (err) {
        g_warning("[Panel] CSS error for plugin slot: %s", err->message);
        g_error_free(err);
    } else {
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(slot),
            GTK_STYLE_PROVIDER(prov),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(prov);
    g_string_free(css, TRUE);
}

/* =========================================================================
 * Merge VenomPluginVisuals from the API into ItemVisuals
 * (panel.conf values always take precedence over plugin defaults)
 * ========================================================================= */

static void merge_api_visuals(ItemVisuals *iv, const VenomPluginVisuals *av)
{
    if (!av || av->bg_type == VENOM_PLUGIN_BG_INHERIT) return;
    if (iv->has_bg) return;  /* panel.conf already set a background */

    iv->valid  = TRUE;
    iv->has_bg = TRUE;
    iv->bg_r   = av->bg_r;
    iv->bg_g   = av->bg_g;
    iv->bg_b   = av->bg_b;
    iv->bg_a   = (av->bg_a > 0.0) ? av->bg_a : 1.0;

    if (av->bg_type == VENOM_PLUGIN_BG_GRADIENT) {
        iv->gradient = TRUE;
        iv->bg_r2    = av->bg_r2;
        iv->bg_g2    = av->bg_g2;
        iv->bg_b2    = av->bg_b2;
    } else if (av->bg_type == VENOM_PLUGIN_BG_TRANSPARENT) {
        iv->bg_a = 0.0;
    }

    if (!iv->has_border && av->border_enabled && av->border_width > 0) {
        iv->has_border    = TRUE;
        iv->border_r      = av->border_r;
        iv->border_g      = av->border_g;
        iv->border_b      = av->border_b;
        iv->border_a      = av->border_a;
        iv->border_width  = av->border_width;
        iv->border_radius = av->border_radius;
    }
}

/* =========================================================================
 * Security checks
 * ========================================================================= */

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
    return S_ISREG(st.st_mode);
}

/* =========================================================================
 * Sandbox crash callback — called by PluginSandbox on unexpected exit
 * ========================================================================= */

static void on_plugin_crashed(PluginSandbox *sb, gpointer user_data)
{
    (void)user_data;
    g_warning("[Panel] Plugin crashed: %s (restarts: %d)",
              plugin_sandbox_get_so_path(sb),
              plugin_sandbox_get_restart_count(sb));
}

/* =========================================================================
 * Plugin item loading
 * ========================================================================= */

static GtkWidget *load_plugin_item(PanelItem *item,
                                   const char *plugins_dir,
                                   ItemVisuals *visuals_out)
{
    if (!plugin_is_safe_basename(item->file)) {
        g_warning("[Panel] Refusing plugin '%s' (invalid basename)", item->file);
        GtkWidget *w = gtk_image_new_from_icon_name("dialog-warning-symbolic",
                                                     GTK_ICON_SIZE_MENU);
        gtk_widget_set_tooltip_text(w, "Invalid plugin filename in config");
        return w;
    }

    char *fp = g_build_filename(plugins_dir, item->file, NULL);
    if (!path_is_regular_nosymlink(fp)) {
        g_warning("[Panel] Plugin '%s' is not a regular file", item->file);
        GtkWidget *w = gtk_image_new_from_icon_name("dialog-warning-symbolic",
                                                     GTK_ICON_SIZE_MENU);
        char *tip = g_strdup_printf("Plugin not found: %s", item->file);
        gtk_widget_set_tooltip_text(w, tip);
        g_free(tip);
        g_free(fp);
        return w;
    }

    /* Peek at the plugin's API to read its built-in visuals before launching */
    void *peek = dlopen(fp, RTLD_LAZY | RTLD_LOCAL);
    if (peek) {
        VenomPanelPluginInitFnV3 fn3 =
            (VenomPanelPluginInitFnV3)dlsym(peek, "venom_panel_plugin_init_v3");
        if (fn3) {
            const VenomPanelPluginAPIv3 *api3 = fn3();
            if (api3 && api3->api_version == VENOM_PANEL_PLUGIN_API_VERSION)
                merge_api_visuals(visuals_out, &api3->visuals);
        }
        dlclose(peek);
    }

    /* Create the sandbox and launch */
    PluginSandbox *sb = plugin_sandbox_new(fp);
    g_free(fp);

    plugin_sandbox_set_crash_callback(sb, on_plugin_crashed, NULL);

    /* Send extra config keys after a short idle (widget may not exist yet) */
    if (item->extra_keys && g_hash_table_size(item->extra_keys) > 0) {
        GHashTableIter it;
        gpointer k, v;
        g_hash_table_iter_init(&it, item->extra_keys);
        while (g_hash_table_iter_next(&it, &k, &v))
            plugin_sandbox_send_config(sb, (const char*)k, (const char*)v);
    }

    gboolean ok = plugin_sandbox_launch(sb);
    GtkWidget *slot = plugin_sandbox_get_slot_widget(sb);

    /* Track sandbox for cleanup on reload */
    g_sandboxes = g_slist_prepend(g_sandboxes, sb);

    if (ok) {
        g_print("[Panel] Plugin '%s' launched (expand=%d pad=%d)\n",
                item->file, item->expand, item->padding);
    }

    return slot;  /* always returns the slot container (even on failure) */
}

/* =========================================================================
 * Main item loader
 * ========================================================================= */

static void load_items_from_config(GtkWidget *hbox)
{
    GArray *items       = parse_panel_config();
    char   *plugins_dir = panel_plugins_dir();
    g_mkdir_with_parents(plugins_dir, 0755);

    for (guint i = 0; i < items->len; i++) {
        PanelItem *item   = &g_array_index(items, PanelItem, i);
        GtkWidget *widget = NULL;
        gboolean   expand = item->expand;
        int        pad    = item->padding;

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
                pad    = 4;
                break;

            case ITEM_TYPE_BUILTIN:
                widget = panel_builtin_create(item->name);
                if (!widget)
                    g_warning("[Panel] Unknown builtin: '%s'", item->name);
                break;

            case ITEM_TYPE_PLUGIN: {
                ItemVisuals vis = item->visuals;  /* start with conf values */
                widget = load_plugin_item(item, plugins_dir, &vis);

                /* Apply visual CSS to the slot widget */
                if (widget && vis.valid)
                    apply_item_visuals(widget, &vis);
                break;
            }
        }

        if (widget) {
            gtk_box_pack_start(GTK_BOX(hbox), widget, expand, expand, pad);
            gtk_widget_show_all(widget);
        }
    }

    /* Free extra_keys in each parsed item */
    for (guint i = 0; i < items->len; i++)
        panel_item_free_extra(&g_array_index(items, PanelItem, i));

    g_array_free(items, TRUE);
    g_free(plugins_dir);
}

/* =========================================================================
 * SIGUSR1 — live reload
 * ========================================================================= */

static gboolean on_panel_sigusr1(gpointer user_data)
{
    (void)user_data;
    g_print("[Panel] Received reload signal — reloading config…\n");
    if (!g_panel_hbox) return G_SOURCE_CONTINUE;

    GtkWidget *toplevel = gtk_widget_get_toplevel(g_panel_hbox);
    if (GTK_IS_WINDOW(toplevel))
        panel_geometry_apply(toplevel);

    panel_builtins_prepare_reload();

    /* Terminate all sandboxes */
    for (GSList *l = g_sandboxes; l; l = l->next)
        plugin_sandbox_free((PluginSandbox*)l->data);
    g_slist_free(g_sandboxes);
    g_sandboxes = NULL;

    /* Destroy all slot widgets */
    GList *children = gtk_container_get_children(GTK_CONTAINER(g_panel_hbox));
    for (GList *l = children; l; l = g_list_next(l))
        gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(children);

    /* Handle orientation change */
    PanelEdge      edge = panel_geometry_get_config_edge();
    GtkOrientation want = panel_geometry_edge_orientation(edge);
    GtkOrientation have = GTK_ORIENTATION_HORIZONTAL;
    if (GTK_IS_ORIENTABLE(g_panel_hbox))
        have = gtk_orientable_get_orientation(GTK_ORIENTABLE(g_panel_hbox));

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

/* =========================================================================
 * Public API
 * ========================================================================= */

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
    for (GSList *l = g_sandboxes; l; l = l->next)
        plugin_sandbox_free((PluginSandbox*)l->data);
    g_slist_free(g_sandboxes);
    g_sandboxes   = NULL;
    g_panel_hbox  = NULL;
}

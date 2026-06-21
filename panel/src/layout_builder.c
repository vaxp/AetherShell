/*
 * layout_builder.c — AetherShell Panel Layout Builder
 *
 * Reads panel.json and constructs the GTK widget tree for the panel bar.
 * Pill containers (GtkBoxes) receive CSS classes so users can style them
 * freely from ~/.config/vaxp/panel/panel-user.css.
 */

#include <json-glib/json-glib.h>
#include <string.h>
#include "layout_builder.h"
#include "plugin_engine.h"

/* ── Defaults ──────────────────────────────────────────────────────────────── */

#define DEFAULT_HEIGHT       36
#define DEFAULT_MARGIN_TOP    4
#define DEFAULT_MARGIN_LEFT   8
#define DEFAULT_MARGIN_RIGHT  8
#define DEFAULT_SPACING       8
#define DEFAULT_PILL_SPACING  4

/* ── Helpers ───────────────────────────────────────────────────────────────── */

/* Safe helper: get an integer field from a JsonObject with a fallback. */
static int obj_get_int(JsonObject *obj, const char *key, int fallback)
{
    if (!obj || !json_object_has_member(obj, key)) return fallback;
    return (int)json_object_get_int_member(obj, key);
}

/* ── Config parsing ────────────────────────────────────────────────────────── */

gboolean layout_builder_parse_config(const char       *json_path,
                                     PanelLayoutConfig *out)
{
    if (!json_path || !out) return FALSE;

    /* Set defaults first */
    out->height      = DEFAULT_HEIGHT;
    out->margin_top  = DEFAULT_MARGIN_TOP;
    out->margin_left = DEFAULT_MARGIN_LEFT;
    out->margin_right= DEFAULT_MARGIN_RIGHT;
    out->spacing     = DEFAULT_SPACING;

    GError  *err    = NULL;
    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_file(parser, json_path, &err)) {
        g_warning("[LayoutBuilder] Cannot parse '%s': %s",
                  json_path, err ? err->message : "?");
        if (err) g_error_free(err);
        g_object_unref(parser);
        return FALSE;
    }

    JsonNode   *root     = json_parser_get_root(parser);
    JsonObject *root_obj = json_node_get_object(root);

    if (json_object_has_member(root_obj, "panel")) {
        JsonObject *panel = json_object_get_object_member(root_obj, "panel");
        out->height  = obj_get_int(panel, "height",  out->height);
        out->spacing = obj_get_int(panel, "spacing",  out->spacing);

        if (json_object_has_member(panel, "margin")) {
            JsonObject *m = json_object_get_object_member(panel, "margin");
            out->margin_top   = obj_get_int(m, "top",   out->margin_top);
            out->margin_left  = obj_get_int(m, "left",  out->margin_left);
            out->margin_right = obj_get_int(m, "right", out->margin_right);
        }
    }

    g_object_unref(parser);
    return TRUE;
}

/* ── Pill builder ──────────────────────────────────────────────────────────── */

/*
 * build_pill:
 * Creates a GtkBox from a JSON pill object and populates it with the
 * plugin widgets listed in its "plugins" array.
 *
 * The box receives:
 *   CSS class "aether-pill"   — can be styled generically
 *   CSS class <pill-id>       — can be styled specifically
 */
static GtkWidget *build_pill(JsonObject *pill_obj)
{
    if (!pill_obj) return NULL;

    int spacing = obj_get_int(pill_obj, "spacing", DEFAULT_PILL_SPACING);
    GtkWidget *pill = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, spacing);

    GtkStyleContext *sc = gtk_widget_get_style_context(pill);
    gtk_style_context_add_class(sc, "aether-pill");
    const char *id = NULL;
    if (json_object_has_member(pill_obj, "id")) {
        const char *id = json_object_get_string_member(pill_obj, "id");
        if (id && id[0] != '\0') {
            gtk_widget_set_name(pill, id);
            gtk_style_context_add_class(sc, id);
        }
    }
    if (json_object_has_member(pill_obj, "border_color")) {
        const char *border_color = json_object_get_string_member(pill_obj, "border_color");
        if (border_color && border_color[0] != '\0') {
            int border_width = obj_get_int(pill_obj, "border_width", 1);
            int border_radius = obj_get_int(pill_obj, "border_radius", 14);
            GtkCssProvider *provider = gtk_css_provider_new();
            char *css = NULL;
            if (id && id[0] != '\0') {
                css = g_strdup_printf("#%s { border: %dpx solid %s; border-radius: %dpx; }", id, border_width, border_color, border_radius);
            } else {
                css = g_strdup_printf(".aether-pill { border: %dpx solid %s; border-radius: %dpx; }", border_width, border_color, border_radius);
            }
            gtk_css_provider_load_from_data(provider, css, -1, NULL);
            gtk_style_context_add_provider(sc, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 20);
            g_free(css);
            g_object_unref(provider);
        }
    }

    if (!json_object_has_member(pill_obj, "plugins")) return pill;

    JsonArray *plugins = json_object_get_array_member(pill_obj, "plugins");
    if (!plugins) return pill;

    guint n = json_array_get_length(plugins);
    for (guint i = 0; i < n; i++) {
        const char *plugin_id = json_array_get_string_element(plugins, i);
        if (!plugin_id) continue;

        GtkWidget *w = plugin_engine_get_widget(plugin_id);
        if (w) {
            gtk_box_pack_start(GTK_BOX(pill), w, FALSE, FALSE, 0);
            g_debug("[LayoutBuilder] Added plugin '%s' to pill", plugin_id);
        } else {
            g_warning("[LayoutBuilder] Plugin '%s' not found in engine — skipping",
                      plugin_id);
        }
    }

    return pill;
}

/* Build all pills from a JSON array and pack them into @target_box. */
static void build_zone(JsonArray  *arr,
                        GtkWidget  *target_box,
                        gboolean    pack_end)
{
    if (!arr || !target_box) return;

    guint n = json_array_get_length(arr);
    for (guint i = 0; i < n; i++) {
        JsonObject *pill_obj = json_array_get_object_element(arr, i);
        if (!pill_obj) continue;

        GtkWidget *pill = build_pill(pill_obj);
        if (!pill) continue;

        if (pack_end)
            gtk_box_pack_end(GTK_BOX(target_box), pill, FALSE, FALSE, 0);
        else
            gtk_box_pack_start(GTK_BOX(target_box), pill, FALSE, FALSE, 0);
    }
}

/* ── Public API ────────────────────────────────────────────────────────────── */

GtkWidget *layout_builder_build(const char *json_path)
{
    if (!json_path) {
        g_warning("[LayoutBuilder] No JSON path provided");
        return NULL;
    }

    PanelLayoutConfig cfg;
    layout_builder_parse_config(json_path, &cfg);

    GError     *err    = NULL;
    JsonParser *parser = json_parser_new();

    if (!json_parser_load_from_file(parser, json_path, &err)) {
        g_warning("[LayoutBuilder] Cannot parse '%s': %s",
                  json_path, err ? err->message : "?");
        if (err) g_error_free(err);
        g_object_unref(parser);
        return NULL;
    }

    JsonNode   *root     = json_parser_get_root(parser);
    JsonObject *root_obj = json_node_get_object(root);

    /* ── Outer content box ─────────────────────────────────────────────── */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    gtk_widget_set_name(content, "panel-content");
    gtk_widget_set_margin_start(content, cfg.margin_left);
    gtk_widget_set_margin_end  (content, cfg.margin_right);
    gtk_widget_set_margin_top  (content, cfg.margin_top);

    /* ── Three zones: left, center, right ──────────────────────────────── */
    GtkWidget *left_box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *right_box  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);

    gtk_widget_set_name(left_box,   "zone-left");
    gtk_widget_set_name(center_box, "zone-center");
    gtk_widget_set_name(right_box,  "zone-right");

    if (!json_object_has_member(root_obj, "layout")) {
        g_warning("[LayoutBuilder] JSON has no 'layout' section");
    } else {
        JsonObject *layout = json_object_get_object_member(root_obj, "layout");

        if (json_object_has_member(layout, "left"))
            build_zone(json_object_get_array_member(layout, "left"),
                       left_box, FALSE);

        if (json_object_has_member(layout, "center"))
            build_zone(json_object_get_array_member(layout, "center"),
                       center_box, FALSE);

        if (json_object_has_member(layout, "right"))
            build_zone(json_object_get_array_member(layout, "right"),
                       right_box, TRUE);   /* pack_end for right zone */
    }

    /* Pack zones: left sticks to start, right to end, center floats */
    gtk_box_pack_start(GTK_BOX(content), left_box,  FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(content), center_box);
    gtk_box_pack_end  (GTK_BOX(content), right_box, FALSE, FALSE, 0);

    g_object_unref(parser);

    g_debug("[LayoutBuilder] Panel layout built from '%s'", json_path);
    return content;
}

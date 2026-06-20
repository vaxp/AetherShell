/*
 * layout_builder.c — AetherShell AetherCore Layout Builder
 *
 * Reads AetherCore.json and constructs the GTK widget tree for the AetherCore bar.
 * Pill containers (GtkBoxes) receive CSS classes so users can style them
 * freely from ~/.config/aether/AetherCore-user.css.
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
                                     AetherCoreLayoutConfig *out)
{
    if (!json_path || !out) return FALSE;

    /* Set defaults first */
    out->height      = DEFAULT_HEIGHT;
    out->margin_top  = DEFAULT_MARGIN_TOP;
    out->margin_bottom= DEFAULT_MARGIN_TOP; // default to same as top
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

    if (json_object_has_member(root_obj, "AetherCore")) {
        JsonObject *AetherCore = json_object_get_object_member(root_obj, "AetherCore");
        out->height  = obj_get_int(AetherCore, "height",  out->height);
        out->spacing = obj_get_int(AetherCore, "spacing",  out->spacing);

        if (json_object_has_member(AetherCore, "margin")) {
            JsonObject *m = json_object_get_object_member(AetherCore, "margin");
            out->margin_top   = obj_get_int(m, "top",   out->margin_top);
            out->margin_bottom= obj_get_int(m, "bottom",out->margin_bottom);
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
 * @orientation  GTK_ORIENTATION_HORIZONTAL for top/bottom zones,
 *               GTK_ORIENTATION_VERTICAL   for left/right side zones.
 */
static GtkWidget *build_pill(JsonObject *pill_obj, GtkOrientation orientation, GtkAlign valign, GtkAlign halign)
{
    if (!pill_obj) return NULL;

    int spacing = obj_get_int(pill_obj, "spacing", DEFAULT_PILL_SPACING);
    GtkWidget *pill = gtk_box_new(orientation, spacing);

    /* Prevent pills from stretching or drifting when the row/col grows. */
    gtk_widget_set_valign(pill, valign);
    gtk_widget_set_halign(pill, halign);

    GtkStyleContext *sc = gtk_widget_get_style_context(pill);
    gtk_style_context_add_class(sc, "aether-pill");

    /* Add orientation class so CSS can distinguish side pills */
    gtk_style_context_add_class(sc,
        orientation == GTK_ORIENTATION_VERTICAL
            ? "aether-pill-vertical"
            : "aether-pill-horizontal");

    if (json_object_has_member(pill_obj, "id")) {
        const char *id = json_object_get_string_member(pill_obj, "id");
        if (id && id[0] != '\0') {
            gtk_widget_set_name(pill, id);
            gtk_style_context_add_class(sc, id);
        }
    }

    /* Explicit size — 0 means "let GTK decide".
     * set_size_request enforces a MINIMUM; disabling expand stops the widget
     * from growing past that, so CSS max-width/max-height can also shrink it. */
    int req_w = obj_get_int(pill_obj, "width",  0);
    int req_h = obj_get_int(pill_obj, "height", 0);
    if (req_w > 0 || req_h > 0) {
        gtk_widget_set_size_request(pill,
                                    req_w > 0 ? req_w : -1,
                                    req_h > 0 ? req_h : -1);
        /* Prevent the box from expanding beyond the requested size so that
         * CSS max-width / max-height can reduce it below the natural size. */
        if (req_w > 0) gtk_widget_set_hexpand(pill, FALSE);
        if (req_h > 0) gtk_widget_set_vexpand(pill, FALSE);
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
            plugin_engine_notify_orientation(plugin_id, orientation);
            g_debug("[LayoutBuilder] Added plugin '%s' to pill (orientation=%d)", plugin_id, orientation);
        } else {
            g_warning("[LayoutBuilder] Plugin '%s' not found in engine — skipping",
                      plugin_id);
        }
    }

    return pill;
}

/*
 * build_zone:
 * Build all pills from a JSON array and pack into @target_box.
 */
static void build_zone(JsonArray      *arr,
                        GtkWidget      *target_box,
                        gboolean        pack_end,
                        GtkOrientation  pill_orientation,
                        GtkAlign        valign,
                        GtkAlign        halign)
{
    if (!arr || !target_box) return;

    guint n = json_array_get_length(arr);
    for (guint i = 0; i < n; i++) {
        JsonObject *pill_obj = json_array_get_object_element(arr, i);
        if (!pill_obj) continue;

        GtkWidget *pill = build_pill(pill_obj, pill_orientation, valign, halign);
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

    AetherCoreLayoutConfig cfg;
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

    /* ── Outer content box (fullscreen v-box) ────────────────────────── */
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(content, "AetherCore-content");
    gtk_widget_set_margin_start(content, cfg.margin_left);
    gtk_widget_set_margin_end  (content, cfg.margin_right);
    gtk_widget_set_margin_top  (content, cfg.margin_top);
    gtk_widget_set_margin_bottom(content, cfg.margin_bottom);

    /* ── Three rows: top, middle, bottom ──────────────────────────────── */
    GtkWidget *row_top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *row_mid = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *row_bot = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);

    /* ── Nine zones ───────────────────────────────────────────────────── */
    GtkWidget *top_left   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *top_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *top_right  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);

    /* Middle side zones are VERTICAL columns — pills stack top-to-bottom */
    GtkWidget *mid_left   = gtk_box_new(GTK_ORIENTATION_VERTICAL,   cfg.spacing);
    GtkWidget *mid_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *mid_right  = gtk_box_new(GTK_ORIENTATION_VERTICAL,   cfg.spacing);

    GtkWidget *bot_left   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *bot_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);
    GtkWidget *bot_right  = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, cfg.spacing);

    gtk_widget_set_name(top_left,   "zone-top-left");
    gtk_widget_set_name(top_center, "zone-top-center");
    gtk_widget_set_name(top_right,  "zone-top-right");

    gtk_widget_set_name(mid_left,   "zone-left");
    gtk_widget_set_name(mid_center, "zone-center");
    gtk_widget_set_name(mid_right,  "zone-right");

    gtk_widget_set_name(bot_left,   "zone-bottom-left");
    gtk_widget_set_name(bot_center, "zone-bottom-center");
    gtk_widget_set_name(bot_right,  "zone-bottom-right");

    if (!json_object_has_member(root_obj, "layout")) {
        g_warning("[LayoutBuilder] JSON has no 'layout' section");
    } else {
        JsonObject *layout = json_object_get_object_member(root_obj, "layout");

        /* ── Detect schema version ─────────────────────────────────────────
         * If the JSON uses the new "top-left / top-center / top-right" keys
         * we load them into the top row directly.
         *
         * If NONE of those new keys exist but the old "left / center / right"
         * keys are present (legacy config), we silently promote them to the
         * top row so existing user configs continue to work unchanged.
         * ──────────────────────────────────────────────────────────────────*/
        gboolean has_new_top = json_object_has_member(layout, "top-left")   ||
                               json_object_has_member(layout, "top-center") ||
                               json_object_has_member(layout, "top-right");

        if (has_new_top) {
            /* New-style: populate top row from explicit keys */
            if (json_object_has_member(layout, "top-left"))
                build_zone(json_object_get_array_member(layout, "top-left"),   top_left,   FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);
            if (json_object_has_member(layout, "top-center"))
                build_zone(json_object_get_array_member(layout, "top-center"), top_center, FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);
            if (json_object_has_member(layout, "top-right"))
                build_zone(json_object_get_array_member(layout, "top-right"),  top_right,  TRUE,  GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);

            /* Middle row: left/right are vertical, center is horizontal */
            if (json_object_has_member(layout, "left"))
                build_zone(json_object_get_array_member(layout, "left"),   mid_left,   FALSE, GTK_ORIENTATION_VERTICAL,   GTK_ALIGN_START,  GTK_ALIGN_START);
            if (json_object_has_member(layout, "center"))
                build_zone(json_object_get_array_member(layout, "center"), mid_center, FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_CENTER, GTK_ALIGN_FILL);
            if (json_object_has_member(layout, "right"))
                build_zone(json_object_get_array_member(layout, "right"),  mid_right,  FALSE, GTK_ORIENTATION_VERTICAL,   GTK_ALIGN_START,  GTK_ALIGN_END);
        } else {
            /* Legacy-style: promote left/center/right → top row */
            g_debug("[LayoutBuilder] Legacy config detected: mapping left/center/right → top row");
            if (json_object_has_member(layout, "left"))
                build_zone(json_object_get_array_member(layout, "left"),   top_left,   FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);
            if (json_object_has_member(layout, "center"))
                build_zone(json_object_get_array_member(layout, "center"), top_center, FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);
            if (json_object_has_member(layout, "right"))
                build_zone(json_object_get_array_member(layout, "right"),  top_right,  TRUE,  GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_START, GTK_ALIGN_FILL);
        }

        /* Bottom row — always horizontal */
        if (json_object_has_member(layout, "bottom-left"))
            build_zone(json_object_get_array_member(layout, "bottom-left"),   bot_left,   FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_END, GTK_ALIGN_FILL);
        if (json_object_has_member(layout, "bottom-center"))
            build_zone(json_object_get_array_member(layout, "bottom-center"), bot_center, FALSE, GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_END, GTK_ALIGN_FILL);
        if (json_object_has_member(layout, "bottom-right"))
            build_zone(json_object_get_array_member(layout, "bottom-right"),  bot_right,  TRUE,  GTK_ORIENTATION_HORIZONTAL, GTK_ALIGN_END, GTK_ALIGN_FILL);
    }

    /* Pack rows */
    gtk_box_pack_start(GTK_BOX(row_top), top_left, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(row_top), top_center);
    gtk_box_pack_end(GTK_BOX(row_top), top_right, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(row_mid), mid_left, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(row_mid), mid_center);
    gtk_box_pack_end(GTK_BOX(row_mid), mid_right, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(row_bot), bot_left, FALSE, FALSE, 0);
    gtk_box_set_center_widget(GTK_BOX(row_bot), bot_center);
    gtk_box_pack_end(GTK_BOX(row_bot), bot_right, FALSE, FALSE, 0);

    /* Mid row needs to expand to push bottom row down */
    gtk_box_pack_start(GTK_BOX(content), row_top, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), row_mid, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(content), row_bot, FALSE, FALSE, 0);

    g_object_unref(parser);

    g_debug("[LayoutBuilder] AetherCore layout built from '%s'", json_path);
    return content;
}

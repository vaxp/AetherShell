#pragma once

#include <gtk/gtk.h>

/* =========================================================================
 * AetherShell AetherCore — Layout Builder
 *
 * Reads a AetherCore.json configuration file and constructs the full GTK widget
 * tree for the AetherCore bar.  Each "pill" in the JSON becomes a styled GtkBox;
 * each plugin ID inside a pill is resolved via plugin_engine_get_widget().
 *
 * JSON schema (simplified):
 *
 *   {
 *     "AetherCore": {
 *       "height"  : 36,
 *       "margin"  : { "top": 4, "left": 8, "right": 8 }
 *     },
 *     "layout": {
 *       "left"   : [ <pill>, … ],
 *       "center" : [ <pill>, … ],
 *       "right"  : [ <pill>, … ]
 *     }
 *   }
 *
 *   pill = {
 *     "type"    : "pill",          // "pill" is the only type for now
 *     "id"      : "my-pill",       // CSS class applied to the GtkBox
 *     "spacing" : 4,               // px between plugin widgets (optional)
 *     "plugins" : [ "aether-wifi", "aether-volume", … ]
 *   }
 * ========================================================================= */

/**
 * AetherCoreLayoutConfig — settings parsed from the "AetherCore" object.
 */
typedef struct {
    int height;        /* AetherCore bar height in pixels  (default 36)          */
    int margin_top;    /* Wayland exclusive-zone top margin  (default 4)    */
    int margin_bottom; /* AetherCore content bottom margin        (default 4)    */
    int margin_left;   /* AetherCore content left margin          (default 8)    */
    int margin_right;  /* AetherCore content right margin         (default 8)    */
    int spacing;       /* default spacing between pills      (default 8)    */
} AetherCoreLayoutConfig;

/**
 * layout_builder_parse_config:
 * Parse only the "AetherCore" section of @json_path and fill @out_config.
 * Falls back to defaults for any missing key.
 *
 * Returns TRUE on success, FALSE if the file could not be opened/parsed.
 */
gboolean layout_builder_parse_config(const char       *json_path,
                                     AetherCoreLayoutConfig *out_config);

/**
 * layout_builder_build:
 * Read @json_path and construct the full AetherCore content widget.
 *
 * The returned widget is a GtkBox (id "AetherCore-content") ready to be packed
 * into the AetherCore window.  Each pill is a GtkBox with:
 *   - CSS class "aether-pill"
 *   - CSS class equal to the pill's "id" field
 *   - Plugin widgets obtained from plugin_engine_get_widget()
 *
 * Plugins referenced in the JSON but not found in the engine are silently
 * skipped with a g_warning().
 *
 * Returns a new GtkWidget* on success, NULL on failure.
 */
GtkWidget *layout_builder_build(const char *json_path);

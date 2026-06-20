#pragma once

#include <gtk/gtk.h>

/* =========================================================================
 * AetherShell Panel — Built-in Plugin Registration
 *
 * Wraps every hard-coded panel component (battery, wifi, clock, etc.) in a
 * thin AetherPanelPluginAPIv3 struct and registers each one with the plugin
 * engine under a stable string ID.
 *
 * ID reference:
 *   "aether-appmenu"    — App / start-menu button
 *   "aether-clipboard"  — Clipboard toggle button
 *   "aether-workspaces" — Workspace dot indicators
 *   "aether-clock"      — Clock / date button (opens sidebar)
 *   "aether-sni-tray"   — StatusNotifierItem system tray
 *   "aether-keyboard"   — Keyboard layout indicator
 *   "aether-wifi"       — Wi-Fi indicator
 *   "aether-bt"         — Bluetooth indicator
 *   "aether-mic"        — Microphone indicator
 *   "aether-volume"     — Volume indicator
 *   "aether-battery"    — Battery indicator
 *   "aether-search"     — Search (Basilisk) button
 *   "aether-notifs"     — Notifications bell button
 *   "aether-cc"         — Control-center toggle button
 * ========================================================================= */

/**
 * builtin_plugins_register_all:
 * Registers every built-in plugin with the engine.
 * Must be called after plugin_engine_init() and before layout_builder_build().
 */
void builtin_plugins_register_all(void);

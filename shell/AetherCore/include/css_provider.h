#pragma once

#include <gtk/gtk.h>

/* =========================================================================
 * AetherShell AetherCore — CSS Provider
 *
 * Loads the base AetherCore stylesheet plus an optional user-override stylesheet.
 * Watches the user stylesheet with GFileMonitor and hot-reloads it whenever
 * the file changes on disk (Live Reload — no AetherCore restart needed).
 * ========================================================================= */

/**
 * AetherCore_css_provider_init:
 *
 * Must be called once after gtk_init() and before any widgets are shown.
 *
 * Load order (highest priority last, so user CSS wins):
 *   1. System base: @base_css_path   (e.g. /usr/share/AetherCore/style.css)
 *   2. User override: ~/.config/aether/AetherCore-user.css  (created if absent)
 *
 * @base_css_path : absolute path to the shipped style.css.
 *                  Pass NULL to skip the base stylesheet.
 */
void AetherCore_css_provider_init(const char *base_css_path);

/**
 * AetherCore_css_provider_reload_user:
 * Force-reloads the user stylesheet immediately.
 * Called automatically by the GFileMonitor; can also be triggered manually.
 */
void AetherCore_css_provider_reload_user(void);

/**
 * AetherCore_css_provider_shutdown:
 * Stops the file monitor and unref's the GtkCssProviders.
 * Call before gtk_main_quit().
 */
void AetherCore_css_provider_shutdown(void);

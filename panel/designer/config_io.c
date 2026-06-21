#include "config_io.h"
#include <glib.h>
#include <stdlib.h>

/* ── Path helpers ─────────────────────────────────────────────────────── */

static char *layout_path(void)
{
    /* 1. User config */
    char *p = g_build_filename(g_get_user_config_dir(), "vaxp", "panel", "panel.json", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);

    /* 2. Dev path — next to the designer binary */
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
    g_free(exe);
    p = g_build_filename(dir, "..", "config", "vaxp", "panel", "panel.json", NULL);
    g_free(dir);
    if (g_file_test(p, G_FILE_TEST_EXISTS)) return p;
    g_free(p);

    /* 3. Generate path (will be created on first save) */
    return g_build_filename(g_get_user_config_dir(), "vaxp", "panel", "panel.json", NULL);
}

static char *user_css_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "vaxp", "panel", "panel-user.css", NULL);
}

static char *state_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "vaxp", "panel",
                            "panel-designer-state.json", NULL);
}

static void ensure_dir(const char *file_path)
{
    char *d = g_path_get_dirname(file_path);
    g_mkdir_with_parents(d, 0755);
    g_free(d);
}

/* ── Public API ───────────────────────────────────────────────────────── */

char *config_io_read_layout(void)
{
    char *p = layout_path();
    char *c = NULL;
    g_file_get_contents(p, &c, NULL, NULL);
    g_free(p);
    return c;
}

char *config_io_read_designer_state(void)
{
    char *p = state_path();
    char *c = NULL;
    g_file_get_contents(p, &c, NULL, NULL);
    g_free(p);
    return c;
}

void config_io_write_layout(const char *json)
{
    if (!json) return;
    char *p = g_build_filename(g_get_user_config_dir(), "vaxp", "panel", "panel.json", NULL);
    ensure_dir(p);
    GError *e = NULL;
    if (!g_file_set_contents(p, json, -1, &e))
        g_warning("[Designer] write layout: %s", e ? e->message : "?");
    if (e) g_error_free(e);
    g_free(p);
}

void config_io_write_user_css(const char *css)
{
    if (!css) return;
    char *p = user_css_path();
    ensure_dir(p);
    GError *e = NULL;
    if (!g_file_set_contents(p, css, -1, &e))
        g_warning("[Designer] write CSS: %s", e ? e->message : "?");
    if (e) g_error_free(e);
    g_free(p);
}

void config_io_write_designer_state(const char *json)
{
    if (!json) return;
    char *p = state_path();
    ensure_dir(p);
    GError *e = NULL;
    if (!g_file_set_contents(p, json, -1, &e))
        g_warning("[Designer] write state: %s", e ? e->message : "?");
    if (e) g_error_free(e);
    g_free(p);
}

void config_io_restart_panel(void)
{
    /* Find the panel binary next to the designer */
    char *exe = g_file_read_link("/proc/self/exe", NULL);
    char *dir = exe ? g_path_get_dirname(exe) : g_strdup(".");
    g_free(exe);
    char *panel = g_build_filename(dir, "..", "panel", NULL);
    g_free(dir);

    char *cmd = g_strdup_printf(
        "pkill -x panel 2>/dev/null; sleep 0.4; '%s' &", panel);
    g_free(panel);
    system(cmd);
    g_free(cmd);
}

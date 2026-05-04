/*
 * venom_kb_indicator.c — Keyboard layout indicator builtin for venom-panel.
 *
 * On Wayland (Aether / Wayfire) the active layout is received via
 * compositor_backend callbacks.
 *
 * On X11 the compositor_backend drives kb-indicator-xorg which polls
 * XKB state, so we still get updates through the same callback path.
 *
 * This replaces the old X11-only implementation that used
 * gdk_window_add_filter() and would only show "--" on Wayland.
 */

#include "venom_kb_indicator.h"
#include "compositor-backend.h"
#include <gtk/gtk.h>
#include <string.h>
#include <ctype.h>

/* ─── Singleton references ───────────────────────────────────────── */
static GtkWidget *kb_label  = NULL;
static GtkWidget *kb_widget = NULL;
static gboolean   kb_callback_set = FALSE;

/* ─── Helper: pick the short label from a layout string ──────────
 *
 * compositor_backend delivers layouts as a newline-separated list such as:
 *   "English (US)\nArabic (Iraq)"
 *
 * On Aether/Wayfire the active name is at index layout_index.
 * On X11 the string is already the short label (e.g. "EN" or "AR").
 *
 * We trim the text, take the first word, and uppercase it so the
 * label always reads "EN", "AR", "RU", etc.
 * ──────────────────────────────────────────────────────────────── */

static void extract_short_label(const char *layouts, int index, char *out, size_t out_size) {
    const char *line;
    char *newline;
    char tmp[64];
    char *open, *close;
    const char *src;
    size_t len;
    int i;

    if (!layouts || !layouts[0] || out_size == 0) {
        g_strlcpy(out, "--", out_size);
        return;
    }

    /* Walk to the line at `index` */
    line = layouts;
    for (i = 0; i < index; i++) {
        line = strchr(line, '\n');
        if (!line) { line = layouts; break; }
        line++; /* skip the '\n' */
    }

    /* Copy this line into a temporary buffer */
    newline = strchr(line, '\n');
    if (newline)
        len = (size_t)(newline - line);
    else
        len = strlen(line);

    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, line, len);
    tmp[len] = '\0';
    g_strstrip(tmp);

    if (tmp[0] == '\0') {
        g_strlcpy(out, "--", out_size);
        return;
    }

    /* Prefer text inside parentheses: "English (US)" → "US" */
    open  = strrchr(tmp, '(');
    close = strrchr(tmp, ')');

    if (open && close && close > open + 1) {
        src = open + 1;
        len = (size_t)(close - open - 1);
    } else {
        /* No parentheses: use up to the first space */
        char *sp = strchr(tmp, ' ');
        src = tmp;
        len = sp ? (size_t)(sp - tmp) : strlen(tmp);
    }

    if (len >= out_size) len = out_size - 1;
    memcpy(out, src, len);
    out[len] = '\0';

    /* Uppercase the result */
    for (size_t j = 0; out[j]; j++)
        out[j] = (char)toupper((unsigned char)out[j]);
}

/* ─── compositor_backend callback ────────────────────────────────── */

static void on_keyboard_state_changed(const PanelKeyboardState *state, gpointer user_data) {
    char label_text[16] = {0};
    char markup[128];

    (void)user_data;

    if (!kb_label || !GTK_IS_LABEL(kb_label)) return;

    extract_short_label(state->layouts, state->layout_index,
                        label_text, sizeof(label_text));

    if (label_text[0] == '\0')
        g_strlcpy(label_text, "--", sizeof(label_text));

    snprintf(markup, sizeof(markup),
             "<span font='10' weight='bold'>%s</span>", label_text);
    gtk_label_set_markup(GTK_LABEL(kb_label), markup);
}

/* ─── Destroy handler ────────────────────────────────────────────── */

static void on_kb_indicator_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;

    if (kb_callback_set) {
        panel_compositor_backend_set_keyboard_callback(NULL, NULL);
        kb_callback_set = FALSE;
    }
    kb_label  = NULL;
    kb_widget = NULL;
}

/* ─── Public factory ─────────────────────────────────────────────── */

GtkWidget *create_kb_indicator_widget(void) {
    if (kb_widget && GTK_IS_WIDGET(kb_widget))
        return kb_widget;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    kb_widget = box;
    g_signal_connect(box, "destroy", G_CALLBACK(on_kb_indicator_destroy), NULL);

    kb_label = gtk_label_new("...");
    gtk_box_pack_start(GTK_BOX(box), kb_label, FALSE, FALSE, 0);

    /* Subscribe to keyboard layout changes.
     * compositor_backend selects the right source automatically:
     *   - Aether compositor  (Wayland)  → keyboard_state event
     *   - Wayfire            (Wayland)  → keyboard-modifier-state-changed IPC
     *   - X11                           → XKB poll via kb-indicator-xorg      */
    kb_callback_set = TRUE;
    panel_compositor_backend_set_keyboard_callback(on_keyboard_state_changed, NULL);

    return box;
}

/*
 * video_wallpaper.c
 * Video wallpaper engine using libmpv's SOFTWARE render API.
 *
 * Architecture (SW render — no OpenGL/EGL required):
 *
 *   mpv thread
 *     └─ on_mpv_update() → g_idle_add(on_render_idle)
 *
 *   GTK main thread
 *     ├─ on_render_idle()
 *     │    mpv renders frame → g_framebuf (BGRX, 32-bit/pixel)
 *     │    gtk_widget_queue_draw(icon_layout)
 *     │
 *     └─ on_layout_draw_bg()  [in wallpaper.c, calls video_wallpaper_draw()]
 *          cairo_image_surface from g_framebuf → cairo_paint()
 *
 * Why SW over OpenGL:
 *   GtkGLArea inside a gtk-layer-shell BACKGROUND window has known
 *   EGL/Wayland compositing issues. SW render is simpler, portable,
 *   and plenty fast enough for a looping wallpaper video.
 */

#include "video_wallpaper.h"
#include "desktop_config.h"

#ifdef HAVE_MPV
#include <mpv/client.h>
#include <mpv/render.h>
#include <cairo/cairo.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>



/* ── Module-private state ───────────────────────────────────────── */
static mpv_handle         *g_mpv      = NULL;
static mpv_render_context *g_mpv_ctx  = NULL;
static GtkWidget          *g_layout   = NULL;   /* icon_layout ref    */
static gboolean            g_active   = FALSE;
static char               *g_cur_path = NULL;   /* dedup reloads      */

/* SW frame buffer — BGRX 32-bit/pixel, matches CAIRO_FORMAT_RGB24  */
static uint8_t *g_framebuf   = NULL;
static int      g_framebuf_w = 0;
static int      g_framebuf_h = 0;
static gboolean g_frame_ready = FALSE;  /* TRUE once first frame rendered */

/* Atomic: mpv thread sets 1, idle callback clears it               */
static volatile gint g_pending = 0;

/* ── Idle: render one mpv frame to g_framebuf (main thread) ─────── */
static gboolean on_render_idle(gpointer user_data)
{
    (void)user_data;

    g_atomic_int_set(&g_pending, 0);

    if (!g_mpv_ctx || !g_active || !g_layout) return G_SOURCE_REMOVE;

    int w = gtk_widget_get_allocated_width(g_layout);
    int h = gtk_widget_get_allocated_height(g_layout);
    if (w <= 0 || h <= 0) return G_SOURCE_REMOVE;

    /* Resize frame buffer if dimensions changed */
    if (w != g_framebuf_w || h != g_framebuf_h || !g_framebuf) {
        g_free(g_framebuf);
        g_framebuf   = (uint8_t *)g_malloc(w * h * 4);
        g_framebuf_w = w;
        g_framebuf_h = h;
        g_frame_ready = FALSE;
        memset(g_framebuf, 0, (size_t)w * h * 4);
    }

    int    size[2] = { w, h };
    size_t stride  = (size_t)w * 4;

    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_SW_SIZE,    size         },
        { MPV_RENDER_PARAM_SW_FORMAT,  "bgr0"       }, /* BGRX = CAIRO_FORMAT_RGB24 on LE */
        { MPV_RENDER_PARAM_SW_STRIDE,  &stride      },
        { MPV_RENDER_PARAM_SW_POINTER, g_framebuf   },
        { MPV_RENDER_PARAM_INVALID,    NULL          }
    };

    int ret = mpv_render_context_render(g_mpv_ctx, params);
    if (ret < 0) {
        g_warning("[VideoWallpaper] SW render error: %d", ret);
        return G_SOURCE_REMOVE;
    }

    g_frame_ready = TRUE;
    gtk_widget_queue_draw(g_layout);
    return G_SOURCE_REMOVE;
}

/* ── mpv update callback (mpv thread → schedule idle) ────────────── */
static void on_mpv_update(void *ctx)
{
    (void)ctx;
    /* Only check if a new video frame is available */
    uint64_t flags = mpv_render_context_update(g_mpv_ctx);
    if (!(flags & MPV_RENDER_UPDATE_FRAME)) return;

    if (g_atomic_int_compare_and_exchange(&g_pending, 0, 1))
        g_idle_add(on_render_idle, NULL);
}

/* ── Read saved volume ────────────────────────────────────────────── */
static int read_saved_volume(void)
{
    char *buf = NULL;
    int vol = 0;
    char *vol_cfg = get_vaxp_config_path("video-wallpaper-volume");
    if (g_file_get_contents(vol_cfg, &buf, NULL, NULL)) {
        vol = CLAMP(atoi(buf), 0, 100);
        g_free(buf);
    }
    g_free(vol_cfg);
    return vol;
}

/* ── Public API ───────────────────────────────────────────────────── */

gboolean is_video_file(const char *path)
{
    if (!path || !*path) return FALSE;
    static const char * const exts[] = {
        ".mp4", ".mkv", ".webm", ".avi", ".mov",
        ".flv", ".wmv", ".m4v",  ".ogv", ".ts",
        ".m2ts",".mpg", ".mpeg", ".3gp", ".hevc", NULL
    };
    char *lo = g_ascii_strdown(path, -1);
    gboolean ok = FALSE;
    for (int i = 0; exts[i]; i++)
        if (g_str_has_suffix(lo, exts[i])) { ok = TRUE; break; }
    g_free(lo);
    return ok;
}

/*
 * video_wallpaper_init:
 *   Initialises the mpv handle and SW render context.
 *   @icon_layout: the GtkLayout used as the desktop canvas; we store
 *   a reference so we can call gtk_widget_queue_draw() from the idle.
 *
 *   Returns TRUE on success, FALSE on failure.
 *   No widget hierarchy changes are made — SW render hooks into the
 *   existing Cairo draw pipeline via video_wallpaper_draw().
 */
gboolean video_wallpaper_init(GtkWidget *icon_layout)
{
    /* libmpv requires LC_NUMERIC="C" */
    setlocale(LC_NUMERIC, "C");

    g_mpv = mpv_create();
    if (!g_mpv) {
        g_warning("[VideoWallpaper] mpv_create() failed");
        return FALSE;
    }

    /* Core options */
    mpv_set_option_string(g_mpv, "vo",            "libmpv");
    mpv_set_option_string(g_mpv, "loop",          "inf");
    mpv_set_option_string(g_mpv, "really-quiet",  "yes");
    mpv_set_option_string(g_mpv, "no-audio-display", "yes");

    char vol_str[16];
    snprintf(vol_str, sizeof(vol_str), "%d", read_saved_volume());
    mpv_set_option_string(g_mpv, "volume", vol_str);

    if (mpv_initialize(g_mpv) < 0) {
        g_warning("[VideoWallpaper] mpv_initialize() failed");
        mpv_destroy(g_mpv);
        g_mpv = NULL;
        return FALSE;
    }

    /* SW render context — no OpenGL/EGL needed */
    mpv_render_param params[] = {
        { MPV_RENDER_PARAM_API_TYPE, MPV_RENDER_API_TYPE_SW },
        { MPV_RENDER_PARAM_INVALID,  NULL                   }
    };

    if (mpv_render_context_create(&g_mpv_ctx, g_mpv, params) < 0) {
        g_warning("[VideoWallpaper] mpv_render_context_create() failed");
        mpv_destroy(g_mpv);
        g_mpv = NULL;
        return FALSE;
    }

    mpv_render_context_set_update_callback(g_mpv_ctx, on_mpv_update, NULL);

    g_layout = icon_layout;

    g_print("[VideoWallpaper] SW render context ready\n");
    return TRUE;
}

void video_wallpaper_load(const char *path)
{
    if (!g_mpv || !path || !*path) return;

    /* Skip if same file already playing */
    if (g_active && g_cur_path && g_strcmp0(g_cur_path, path) == 0)
        return;

    g_active = TRUE;
    g_frame_ready = FALSE;
    g_free(g_cur_path);
    g_cur_path = g_strdup(path);

    const char *cmd[] = { "loadfile", path, "replace", NULL };
    mpv_command(g_mpv, cmd);

    g_print("[VideoWallpaper] Loaded: %s\n", path);
}

void video_wallpaper_stop(void)
{
    if (!g_mpv) return;

    g_active = FALSE;
    g_frame_ready = FALSE;
    g_free(g_cur_path);
    g_cur_path = NULL;

    const char *cmd[] = { "stop", NULL };
    mpv_command(g_mpv, cmd);

    if (g_layout) gtk_widget_queue_draw(g_layout);
    g_print("[VideoWallpaper] Stopped\n");
}

gboolean video_wallpaper_is_active(void)
{
    return g_active;
}

void video_wallpaper_set_volume(int volume)
{
    if (!g_mpv) return;
    volume = CLAMP(volume, 0, 100);

    char vs[16];
    snprintf(vs, sizeof(vs), "%d", volume);

    /* Use mpv_set_property_string post-init */
    mpv_set_property_string(g_mpv, "volume", vs);

    ensure_config_dir();
    char *vol_cfg = get_vaxp_config_path("video-wallpaper-volume");
    g_file_set_contents(vol_cfg, vs, -1, NULL);
    g_free(vol_cfg);
}

/*
 * video_wallpaper_draw:
 *   Called from on_layout_draw_bg() in wallpaper.c.
 *   Paints the current video frame onto the Cairo context.
 *   Returns TRUE if a frame was drawn, FALSE if no frame available yet.
 */
gboolean video_wallpaper_draw(cairo_t *cr)
{
    if (!g_active || !g_frame_ready || !g_framebuf ||
        g_framebuf_w <= 0 || g_framebuf_h <= 0)
        return FALSE;

    /*
     * g_framebuf is in BGRX (32-bit, little-endian) format.
     * CAIRO_FORMAT_RGB24 on x86 is stored as 0x00RRGGBB in a 32-bit word,
     * which in memory (LE) is B, G, R, 0x00 — exactly matching "bgr0".
     * So we can wrap the buffer directly with zero copy.
     */
    cairo_surface_t *surf = cairo_image_surface_create_for_data(
        g_framebuf,
        CAIRO_FORMAT_RGB24,
        g_framebuf_w,
        g_framebuf_h,
        g_framebuf_w * 4   /* stride in bytes */
    );

    if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surf);
        return FALSE;
    }

    cairo_set_source_surface(cr, surf, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surf);
    return TRUE;
}

#else /* HAVE_MPV */

#include <glib.h>

gboolean is_video_file(const char *path) {
    if (!path || !*path) return FALSE;
    static const char * const exts[] = {
        ".mp4", ".mkv", ".webm", ".avi", ".mov",
        ".flv", ".wmv", ".m4v",  ".ogv", ".ts",
        ".m2ts",".mpg", ".mpeg", ".3gp", ".hevc", NULL
    };
    char *lo = g_ascii_strdown(path, -1);
    gboolean ok = FALSE;
    for (int i = 0; exts[i]; i++)
        if (g_str_has_suffix(lo, exts[i])) { ok = TRUE; break; }
    g_free(lo);
    return ok;
}

gboolean video_wallpaper_init(GtkWidget *icon_layout) {
    (void)icon_layout;
    g_warning("[VideoWallpaper] Video wallpaper support is disabled (compiled without libmpv-dev)");
    return FALSE;
}

void video_wallpaper_load(const char *path) {
    g_warning("[VideoWallpaper] Cannot play '%s': Video wallpaper support is disabled (compiled without libmpv-dev)", path);
}

void video_wallpaper_stop(void) {
}

gboolean video_wallpaper_is_active(void) {
    return FALSE;
}

void video_wallpaper_set_volume(int volume) {
    (void)volume;
}

gboolean video_wallpaper_draw(cairo_t *cr) {
    (void)cr;
    return FALSE;
}

#endif /* HAVE_MPV */

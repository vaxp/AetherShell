/*
 * video_wallpaper.h
 * Video wallpaper subsystem via libmpv Software render API.
 */

#ifndef VIDEO_WALLPAPER_H
#define VIDEO_WALLPAPER_H

#include <gtk/gtk.h>
#include <cairo/cairo.h>

/*
 * video_wallpaper_init:
 *   Initialise the mpv handle and SW render context.
 *   @icon_layout: the GtkLayout used as the desktop canvas.
 *   Returns TRUE on success.
 */
gboolean video_wallpaper_init(GtkWidget *icon_layout);

/* Load and loop a video file as wallpaper. */
void video_wallpaper_load(const char *path);

/* Stop playback (mpv stays warm in memory for quick restart). */
void video_wallpaper_stop(void);

/* Full teardown: frees mpv handle, render context, and frame buffer. */
void video_wallpaper_destroy(void);


/* TRUE if a video is currently playing. */
gboolean video_wallpaper_is_active(void);

/* Set playback volume (0–100). */
void video_wallpaper_set_volume(int volume);

/*
 * video_wallpaper_draw:
 *   Paint the current video frame onto @cr.
 *   Call this from the layout's "draw" signal handler BEFORE drawing icons.
 *   Returns TRUE if a frame was painted, FALSE if no frame available.
 */
gboolean video_wallpaper_draw(cairo_t *cr);

/*
 * is_video_file:
 *   Returns TRUE if @path has a recognised video extension.
 */
gboolean is_video_file(const char *path);

#endif /* VIDEO_WALLPAPER_H */

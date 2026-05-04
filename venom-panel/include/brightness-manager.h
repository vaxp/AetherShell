/*
 * brightness-manager.h
 *
 * Controls screen brightness via systemd-logind D-Bus:
 *   org.freedesktop.login1.Session.SetBrightness("backlight", device, value)
 *
 * Reads current value from /sys/class/backlight/<device>/{brightness,max_brightness}.
 * Watches for external changes via a poll timer.
 */

#ifndef BRIGHTNESS_MANAGER_H
#define BRIGHTNESS_MANAGER_H

#include <glib.h>

typedef void (*BrightnessChangedCallback)(int percent, gpointer user_data);

/* Initialise brightness control and read the current value.
 * cb will be called once immediately with the current brightness,
 * and again whenever it changes externally (keyboard shortcuts, etc.). */
void brightness_init(BrightnessChangedCallback cb, gpointer user_data);

/* Set brightness (1–100 percent). */
void brightness_set(int percent);

/* Returns the current brightness percent (0–100), or -1 on error. */
int  brightness_get(void);

#endif /* BRIGHTNESS_MANAGER_H */

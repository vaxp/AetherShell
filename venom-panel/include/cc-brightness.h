/*
 * cc-brightness.h
 *
 * Brightness logic for Control Center slider.
 */

#ifndef CC_BRIGHTNESS_H
#define CC_BRIGHTNESS_H

typedef struct _GtkWidget GtkWidget;

void cc_brightness_init(void);
void cc_brightness_cleanup(void);

GtkWidget *cc_brightness_create_scale(void);

#endif /* CC_BRIGHTNESS_H */


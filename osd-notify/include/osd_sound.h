#ifndef VAXP_GUI_OSD_SOUND_H
#define VAXP_GUI_OSD_SOUND_H

#include <glib.h>

typedef enum {
    OSD_SOUND_NOTIFICATION,
    OSD_SOUND_CHARGER_CONNECT,
    OSD_SOUND_CHARGER_DISCONNECT,
    OSD_SOUND_USB_CONNECT,
    OSD_SOUND_USB_DISCONNECT,
    OSD_SOUND_LIMIT_HIGH,
    OSD_SOUND_LIMIT_LOW,
    OSD_SOUND_ERROR,
    OSD_SOUND_EVENT_COUNT
} OsdSoundEvent;

void osd_sound_init(void);
void osd_sound_reload(void);
void osd_sound_play(OsdSoundEvent event);
gboolean osd_sound_should_ignore_events(void);

#endif

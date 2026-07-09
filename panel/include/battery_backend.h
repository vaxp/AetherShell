#ifndef BATTERY_BACKEND_H
#define BATTERY_BACKEND_H

#include <glib.h>
#include <gio/gio.h>

typedef struct {
    double percentage;
    guint32 state;
    double energy;
    double energy_full;
    double energy_full_design;
    double capacity;
    guint64 update_time;
} BatteryInfo;

/* Initialize the battery backend. Pass a callback to be notified when the state changes. */
void battery_backend_init(void (*on_update)(const BatteryInfo *info));

/* Get the current battery state directly */
const BatteryInfo* battery_backend_get_info(void);

#endif // BATTERY_BACKEND_H

#include "airplane_mode.h"
#include "network_actions.h"
#include "bluetooth_manager.h"

static gboolean is_airplane_mode = FALSE;
static gboolean saved_wifi_state = FALSE;
static gboolean saved_bt_state = FALSE;

static AirplaneModeStateCallback state_cb = NULL;
static gpointer state_cb_data = NULL;

void airplane_mode_init(AirplaneModeStateCallback cb, gpointer user_data)
{
    state_cb = cb;
    state_cb_data = user_data;
    is_airplane_mode = FALSE;
    
    if (state_cb) {
        state_cb(is_airplane_mode, state_cb_data);
    }
}

void airplane_mode_toggle(void)
{
    is_airplane_mode = !is_airplane_mode;
    
    if (is_airplane_mode) {
        /* Save current states */
        saved_wifi_state = network_is_wifi_enabled();
        saved_bt_state = bluetooth_is_powered();
        
        /* Turn them off immediately */
        if (saved_wifi_state) {
            network_set_wifi(FALSE);
        }
        if (saved_bt_state) {
            bluetooth_set_powered(FALSE);
        }
    } else {
        /* Restore saved states */
        if (saved_wifi_state) {
            network_set_wifi(TRUE);
        }
        if (saved_bt_state) {
            bluetooth_set_powered(TRUE);
        }
    }
    
    if (state_cb) {
        state_cb(is_airplane_mode, state_cb_data);
    }
}

gboolean airplane_mode_is_active(void)
{
    return is_airplane_mode;
}

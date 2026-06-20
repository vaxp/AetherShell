#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <wayland-cursor.h>
#include <linux/input-event-codes.h>
#include "log.h"
#include "aetherlock.h"
#include "seat.h"
#include "loop.h"
#include "power_actions.h"

static void keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t format, int32_t fd, uint32_t size) {
	struct aetherlock_seat *seat = data;
	struct aetherlock_state *state = seat->state;

	struct xkb_keymap *keymap = NULL;
	struct xkb_state *xkb_state = NULL;

	switch (format) {
	case WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP:
		break;
	case WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1:;
		char *map_shm = mmap(NULL, size - 1, PROT_READ, MAP_PRIVATE, fd, 0);
		if (map_shm == MAP_FAILED) {
			close(fd);
			aetherlock_log(LOG_ERROR, "Unable to initialize keymap shm, aborting");
			exit(1);
		}
		keymap = xkb_keymap_new_from_buffer(
			state->xkb.context, map_shm, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1,
			XKB_KEYMAP_COMPILE_NO_FLAGS);
		assert(keymap);
		munmap(map_shm, size - 1);

		xkb_state = xkb_state_new(keymap);
		assert(xkb_state);
		break;
	}

	close(fd);

	xkb_keymap_unref(state->xkb.keymap);
	xkb_state_unref(state->xkb.state);
	state->xkb.keymap = keymap;
	state->xkb.state = xkb_state;
}

static void keyboard_enter(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	// Who cares
}

static void keyboard_leave(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static void keyboard_repeat(void *data) {
	struct aetherlock_seat *seat = data;
	struct aetherlock_state *state = seat->state;
	seat->repeat_timer = loop_add_timer(
		state->eventloop, seat->repeat_period_ms, keyboard_repeat, seat);
	aetherlock_handle_key(state, seat->repeat_sym, seat->repeat_codepoint);
}

static void keyboard_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t _key_state) {
	struct aetherlock_seat *seat = data;
	struct aetherlock_state *state = seat->state;
	if (state->xkb.state == NULL) {
		return;
	}

	enum wl_keyboard_key_state key_state = _key_state;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(state->xkb.state, key + 8);
	uint32_t keycode = key_state == WL_KEYBOARD_KEY_STATE_PRESSED ?
		key + 8 : 0;
	uint32_t codepoint = xkb_state_key_get_utf32(state->xkb.state, keycode);
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		aetherlock_handle_key(state, sym, codepoint);
	}

	if (seat->repeat_timer) {
		loop_remove_timer(seat->state->eventloop, seat->repeat_timer);
		seat->repeat_timer = NULL;
	}

	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED && seat->repeat_period_ms > 0) {
		seat->repeat_sym = sym;
		seat->repeat_codepoint = codepoint;
		seat->repeat_timer = loop_add_timer(
			seat->state->eventloop, seat->repeat_delay_ms, keyboard_repeat, seat);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group) {
	struct aetherlock_seat *seat = data;
	struct aetherlock_state *state = seat->state;
	if (state->xkb.state == NULL) {
		return;
	}

	int layout_same = xkb_state_layout_index_is_active(state->xkb.state,
		group, XKB_STATE_LAYOUT_EFFECTIVE);
	xkb_state_update_mask(state->xkb.state,
		mods_depressed, mods_latched, mods_locked, 0, 0, group);
	int caps_lock = xkb_state_mod_name_is_active(state->xkb.state,
		XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED);
	if (caps_lock != state->xkb.caps_lock || !layout_same) {
		state->xkb.caps_lock = caps_lock;
		damage_state(state);
	}
	state->xkb.control = xkb_state_mod_name_is_active(state->xkb.state,
		XKB_MOD_NAME_CTRL,
		XKB_STATE_MODS_DEPRESSED | XKB_STATE_MODS_LATCHED);
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *wl_keyboard,
		int32_t rate, int32_t delay) {
	struct aetherlock_seat *seat = data;
	if (rate <= 0) {
		seat->repeat_period_ms = -1;
	} else {
		// Keys per second -> milliseconds between keys
		seat->repeat_period_ms = 1000 / rate;
	}
	seat->repeat_delay_ms = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

static void wl_pointer_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct aetherlock_seat *seat = data;
	if (!seat) return;
	struct aetherlock_state *state = seat->state;
	state->hovered_surface = surface;
	state->pointer_x = wl_fixed_to_double(surface_x);
	state->pointer_y = wl_fixed_to_double(surface_y);
	if (state->cursor_surface) {
		wl_pointer_set_cursor(wl_pointer, serial,
			state->cursor_surface,
			state->cursor_hotspot_x,
			state->cursor_hotspot_y);
	}
}

static void wl_pointer_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	// Who cares
}

static void wl_pointer_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct aetherlock_seat *seat = data;
	if (!seat) return;
	seat->state->pointer_x = wl_fixed_to_double(surface_x);
	seat->state->pointer_y = wl_fixed_to_double(surface_y);
}

static void wl_pointer_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t button_state) {
	struct aetherlock_seat *seat = data;
	if (!seat) return;
	struct aetherlock_state *state = seat->state;

	if (button_state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		struct aetherlock_surface *s_surface;
		bool is_child = false;
		wl_list_for_each(s_surface, &state->surfaces, link) {
			if (s_surface->child == state->hovered_surface) {
				is_child = true;
				break;
			}
		}

		if (is_child) {
			double px = state->pointer_x;
			double py = state->pointer_y;
			
			const double PW = 370.0;
			double cx = PW / 2.0;
			const double BUTTON_Y = 430.0;
			const double BUTTON_R = 20.0;
			const double BUTTON_SP = 75.0;

			double btns[3] = { cx - BUTTON_SP, cx, cx + BUTTON_SP };
			for (int i = 0; i < 3; i++) {
				double dx = px - btns[i];
				double dy = py - BUTTON_Y;
				if (dx*dx + dy*dy <= BUTTON_R*BUTTON_R) {
					if (i == 0) venom_power_off();
					else if (i == 1) venom_reboot();
					else if (i == 2) venom_sleep();
				}
			}
		}
	}
}

static void wl_pointer_axis(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value) {
	// Who cares
}

static void wl_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	// Who cares
}

static void wl_pointer_axis_source(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis_source) {
	// Who cares
}

static void wl_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis) {
	// Who cares
}

static void wl_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer,
		uint32_t axis, int32_t discrete) {
	// Who cares
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = wl_pointer_enter,
	.leave = wl_pointer_leave,
	.motion = wl_pointer_motion,
	.button = wl_pointer_button,
	.axis = wl_pointer_axis,
	.frame = wl_pointer_frame,
	.axis_source = wl_pointer_axis_source,
	.axis_stop = wl_pointer_axis_stop,
	.axis_discrete = wl_pointer_axis_discrete,
};

static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		enum wl_seat_capability caps) {
	struct aetherlock_seat *seat = data;
	if (seat->pointer) {
		wl_pointer_release(seat->pointer);
		seat->pointer = NULL;
	}
	if (seat->keyboard) {
		wl_keyboard_release(seat->keyboard);
		seat->keyboard = NULL;
	}
	if ((caps & WL_SEAT_CAPABILITY_POINTER)) {
		seat->pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->pointer, &pointer_listener, seat);
	}
	if ((caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		seat->keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(seat->keyboard, &keyboard_listener, seat);
	}
}

static void seat_handle_name(void *data, struct wl_seat *wl_seat,
		const char *name) {
	// Who cares
}

const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

#ifndef _AETHERLOCK_H
#define _AETHERLOCK_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "background-image.h"
#include "cairo.h"
#include "loop.h"
#include "pool-buffer.h"
#include "seat.h"
#include "sysstats.h"
#include "weather.h"
#include "venom_notifications.h"

// Indicator state: status of authentication attempt
enum auth_state {
	AUTH_STATE_IDLE, // nothing happening
	AUTH_STATE_VALIDATING, // currently validating password
	AUTH_STATE_INVALID, // displaying message: password was wrong
};

// Indicator state: status of password buffer / typing letters
enum input_state {
	INPUT_STATE_IDLE, // nothing happening; other states decay to this after time
	INPUT_STATE_CLEAR, // displaying message: password buffer was cleared
	INPUT_STATE_LETTER, // pressed a key that input a letter
	INPUT_STATE_BACKSPACE, // pressed backspace and removed a letter
	INPUT_STATE_NEUTRAL, // pressed a key (like Ctrl) that did nothing
};

struct aetherlock_colorset {
	uint32_t input;
	uint32_t cleared;
	uint32_t caps_lock;
	uint32_t verifying;
	uint32_t wrong;
};

struct aetherlock_colors {
	uint32_t background;
	uint32_t bs_highlight;
	uint32_t key_highlight;
	uint32_t caps_lock_bs_highlight;
	uint32_t caps_lock_key_highlight;
	uint32_t separator;
	uint32_t layout_background;
	uint32_t layout_border;
	uint32_t layout_text;
	struct aetherlock_colorset inside;
	struct aetherlock_colorset line;
	struct aetherlock_colorset ring;
	struct aetherlock_colorset text;
};

struct aetherlock_args {
	struct aetherlock_colors colors;
	enum background_mode mode;
	char *font;
	uint32_t font_size;
	uint32_t radius;
	uint32_t thickness;
	uint32_t indicator_x_position;
	uint32_t indicator_y_position;
	bool override_indicator_x_position;
	bool override_indicator_y_position;
	bool ignore_empty;
	bool show_indicator;
	bool show_caps_lock_text;
	bool show_caps_lock_indicator;
	bool show_keyboard_layout;
	bool hide_keyboard_layout;
	bool show_failed_attempts;
	bool daemonize;
	int ready_fd;
	bool indicator_idle_visible;
	// New UI fields
	char *user_name;    // displayed username
	char *avatar_path;  // path to avatar image (~/.face)
	bool show_clock;    // show date/time above login area
	uint32_t blur_radius; // gaussian blur radius for background (0 = no blur)
};

struct aetherlock_password {
	size_t len;
	size_t buffer_len;
	char *buffer;
};

struct aetherlock_state {
	struct loop *eventloop;
	struct loop_timer *input_idle_timer; // timer to reset input state to IDLE
	struct loop_timer *auth_idle_timer; // timer to stop displaying AUTH_STATE_INVALID
	struct loop_timer *clear_password_timer;  // clears the password buffer
	struct loop_timer *clock_timer;           // timer to redraw clock every second
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_subcompositor *subcompositor;
	struct wl_shm *shm;
	struct wl_list surfaces;
	struct wl_list images;
	struct aetherlock_args args;
	struct aetherlock_password password;
	struct aetherlock_xkb xkb;
	cairo_surface_t *test_surface;
	cairo_t *test_cairo; // used to estimate font/text sizes
	cairo_surface_t *avatar_image; // loaded avatar image
	enum auth_state auth_state; // state of the authentication attempt
	enum input_state input_state; // state of the password buffer and key inputs
	uint32_t highlight_start; // position of highlight; 2048 = 1 full turn
	int failed_attempts;
	bool run_display, locked;
	struct ext_session_lock_manager_v1 *ext_session_lock_manager_v1;
	struct ext_session_lock_v1 *ext_session_lock_v1;
	// Cursor support
	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	uint32_t cursor_hotspot_x, cursor_hotspot_y;
	struct wl_surface *hovered_surface;
	double pointer_x, pointer_y;
	
	// MPRIS State
	bool mpris_playing;
	char *mpris_title;
	char *mpris_artist;
	char *mpris_art_url;
	cairo_surface_t *mpris_art_surface;
	bool mpris_title_scroll;
	double marquee_offset;
	
	// System stats
	struct sysstats_data sysstats;
	
	// Weather State
	struct weather_data weather;
	bool weather_fetched;

	// Notifications
	char *latest_notif_app;
	char *latest_notif_summary;
	char *latest_notif_body;
	cairo_surface_t *latest_notif_icon;
	gboolean notifications_dnd;
};

struct aetherlock_surface {
	cairo_surface_t *image;
	struct aetherlock_state *state;
	struct wl_output *output;
	uint32_t output_global_name;
	struct wl_surface *surface; // surface for background
	struct wl_surface *child; // indicator surface made into subsurface
	struct wl_subsurface *subsurface;
	struct ext_session_lock_surface_v1 *ext_session_lock_surface_v1;
	struct pool_buffer indicator_buffers[2];
	bool created;
	bool dirty;
	uint32_t width, height;
	int32_t scale;
	enum wl_output_subpixel subpixel;
	char *output_name;
	struct wl_list link;
	struct wl_callback *frame;
	// Dimensions of last wl_buffer committed to background surface
	int last_buffer_width, last_buffer_height;
};

// There is exactly one aetherlock_image for each -i argument
struct aetherlock_image {
	char *path;
	char *output_name;
	cairo_surface_t *cairo_surface;
	struct wl_list link;
};

void aetherlock_handle_key(struct aetherlock_state *state,
		xkb_keysym_t keysym, uint32_t codepoint);

void render(struct aetherlock_surface *surface);
void damage_state(struct aetherlock_state *state);
void clear_password_buffer(struct aetherlock_password *pw);
void schedule_auth_idle(struct aetherlock_state *state);

void initialize_pw_backend(int argc, char **argv);
void run_pw_backend_child(void);
void clear_buffer(char *buf, size_t size);

#endif

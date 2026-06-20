#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <wayland-client.h>
#include "cairo.h"
#include "background-image.h"
#include "aetherlock.h"
#include "log.h"

#define M_PI 3.14159265358979323846

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/* ── Frame callback (unchanged) ────────────────────────────────────────── */

static void surface_frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	struct aetherlock_surface *surface = data;
	wl_callback_destroy(callback);
	surface->frame = NULL;
	render(surface);
}

static const struct wl_callback_listener surface_frame_listener = {
	.done = surface_frame_handle_done,
};

static bool render_frame(struct aetherlock_surface *surface);

/* ── Background render (unchanged logic) ───────────────────────────────── */

void render(struct aetherlock_surface *surface) {
	struct aetherlock_state *state = surface->state;

	int buffer_width = surface->width * surface->scale;
	int buffer_height = surface->height * surface->scale;
	if (buffer_width == 0 || buffer_height == 0) {
		return;
	}
	if (!surface->dirty || surface->frame) {
		return;
	}

	bool need_destroy = false;
	struct pool_buffer buffer;

	if (buffer_width != surface->last_buffer_width ||
			buffer_height != surface->last_buffer_height) {
		need_destroy = true;
		if (!create_buffer(state->shm, &buffer, buffer_width, buffer_height,
				WL_SHM_FORMAT_ARGB8888)) {
			aetherlock_log(LOG_ERROR,
				"Failed to create new buffer for frame background.");
			return;
		}

		cairo_t *cairo = buffer.cairo;
		cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

		cairo_save(cairo);
		cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_u32(cairo, state->args.colors.background);
		cairo_paint(cairo);
		if (surface->image && state->args.mode != BACKGROUND_MODE_SOLID_COLOR) {
			cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
			render_background_image(cairo, surface->image,
				state->args.mode, buffer_width, buffer_height);
		}
		/* Dark overlay on the full screen — rgba(0,0,0,0.20) */
		cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgba(cairo, 0.0, 0.0, 0.0, 0.20);
		cairo_paint(cairo);
		cairo_restore(cairo);
		cairo_identity_matrix(cairo);

		wl_surface_attach(surface->surface, buffer.buffer, 0, 0);
		wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);

		surface->last_buffer_width = buffer_width;
		surface->last_buffer_height = buffer_height;
	}

	wl_surface_set_buffer_scale(surface->surface, surface->scale);
	render_frame(surface);
	surface->dirty = false;
	surface->frame = wl_surface_frame(surface->surface);
	wl_callback_add_listener(surface->frame, &surface_frame_listener, surface);
	wl_surface_commit(surface->surface);

	if (need_destroy) {
		destroy_buffer(&buffer);
	}
}

/* ── Drawing helpers ────────────────────────────────────────────────────── */

/* Rounded rectangle path */
static void rounded_rect(cairo_t *cr, double x, double y,
		double w, double h, double r) {
	cairo_move_to(cr, x + r, y);
	cairo_line_to(cr, x + w - r, y);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2.0, 0.0);
	cairo_line_to(cr, x + w, y + h - r);
	cairo_arc(cr, x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
	cairo_line_to(cr, x + r, y + h);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2.0, M_PI);
	cairo_line_to(cr, x, y + r);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
	cairo_close_path(cr);
}

/* Draw a centred text string, return its width */
static double draw_text_centered(cairo_t *cr, const char *text,
		double cx, double y) {
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);
	cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, y);
	cairo_show_text(cr, text);
	return ext.width;
}

/* Power icon ⏻ : circle-arc + vertical line */
static void draw_power_icon(cairo_t *cr, double cx, double cy, double r) {
	cairo_set_line_width(cr, 2.2);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.80);
	double gap = M_PI * 0.30;
	cairo_arc(cr, cx, cy, r * 0.60,
		-M_PI / 2.0 + gap, -M_PI / 2.0 + 2.0 * M_PI - gap);
	cairo_stroke(cr);
	cairo_move_to(cr, cx, cy - r * 0.95);
	cairo_line_to(cr, cx, cy - r * 0.28);
	cairo_stroke(cr);
}

/* Restart icon ↺ : arc with arrowhead */
static void draw_restart_icon(cairo_t *cr, double cx, double cy, double r) {
	cairo_set_line_width(cr, 2.2);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.80);
	double arc_r = r * 0.60;
	cairo_arc(cr, cx, cy, arc_r, M_PI * 0.60, M_PI * 2.60);
	cairo_stroke(cr);
	/* arrowhead at end of arc */
	double end_a = M_PI * 2.60;
	double ax = cx + arc_r * cos(end_a);
	double ay = cy + arc_r * sin(end_a);
	double ta = end_a + M_PI / 2.0;
	double hs = r * 0.22;
	cairo_move_to(cr, ax + hs * cos(ta - 0.5), ay + hs * sin(ta - 0.5));
	cairo_line_to(cr, ax, ay);
	cairo_line_to(cr, ax + hs * cos(ta + 0.5), ay + hs * sin(ta + 0.5));
	cairo_stroke(cr);
}

/* Sleep / moon icon : filled crescent */
static void draw_moon_icon(cairo_t *cr, double cx, double cy, double r) {
	cairo_set_source_rgba(cr, 1, 1, 1, 0.80);
	double mr = r * 0.62;
	/* outer circle */
	cairo_arc(cr, cx, cy, mr, 0, 2.0 * M_PI);
	/* punch inner circle (offset to create crescent) */
	cairo_arc_negative(cr, cx + mr * 0.38, cy - mr * 0.08,
		mr * 0.75, 2.0 * M_PI, 0);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
	cairo_fill(cr);
	cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
}

/* Draw pill background with glassmorphism effect */
static void draw_pill(cairo_t *cr, double x, double y, double w, double h,
		double fill_r, double fill_g, double fill_b, double fill_a,
		double border_a) {
	double radius = h / 2.0;
	rounded_rect(cr, x, y, w, h, radius);
	cairo_set_source_rgba(cr, fill_r, fill_g, fill_b, fill_a);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, border_a);
	cairo_set_line_width(cr, 1.2);
	cairo_stroke(cr);
}

/* Draw a simple person icon */
static void draw_person_icon(cairo_t *cr, double cx, double cy, double r) {
	cairo_set_source_rgba(cr, 1, 1, 1, 0.70);
	/* head */
	cairo_arc(cr, cx, cy - r * 0.30, r * 0.32, 0, 2.0 * M_PI);
	cairo_fill(cr);
	/* body (semicircle) */
	cairo_arc(cr, cx, cy + r * 0.55, r * 0.50, M_PI, 2.0 * M_PI);
	cairo_fill(cr);
}

/* ── Main frame renderer ─────────────────────────────────────────────── */

static bool render_frame(struct aetherlock_surface *surface) {
	struct aetherlock_state *state = surface->state;
	double scale = (double)surface->scale;

	/* ── Layout constants (logical pixels) ── */
	const double PW = 370.0;  /* panel width  */
	const double PH = 490.0;  /* panel height */

	int buffer_width  = (int)(PW * scale);
	int buffer_height = (int)(PH * scale);
	/* Must be a multiple of scale */
	int sc = (int)scale;
	if (sc > 1) {
		if (buffer_width  % sc) buffer_width  += sc - (buffer_width  % sc);
		if (buffer_height % sc) buffer_height += sc - (buffer_height % sc);
	}

	/* Centre the panel on screen */
	int subsurf_xpos = surface->width  / 2 - buffer_width  / (2 * sc);
	int subsurf_ypos = surface->height / 2 - buffer_height / (2 * sc);

	struct pool_buffer *buf = get_next_buffer(state->shm,
		surface->indicator_buffers, buffer_width, buffer_height);
	if (!buf) {
		aetherlock_log(LOG_ERROR, "No buffer for render_frame");
		return false;
	}

	cairo_t *cr = buf->cairo;
	cairo_set_antialias(cr, CAIRO_ANTIALIAS_BEST);
	cairo_identity_matrix(cr);

	/* Work in logical pixels */
	cairo_scale(cr, scale, scale);

	/* Clear to transparent */
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);

	double cx = PW / 2.0;   /* horizontal centre */

	/* ── 0. Panel background — black 30% opacity ─────────────────── */
	const double PAD = 20.0;   /* padding from panel edge */
	rounded_rect(cr, PAD, PAD, PW - PAD * 2.0, PH - PAD * 2.0, 22.0);
	cairo_set_source_rgba(cr, 0, 0, 0, 0.30);
	cairo_fill_preserve(cr);
	/* Subtle border */
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);

	/* ── 1. Date / Time ──────────────────────────────────────────── */
	if (state->args.show_clock) {
		time_t t = time(NULL);
		struct tm *tm_info = localtime(&t);

		char day_str[32], date_str[32], time_str[32];
		strftime(day_str,  sizeof(day_str),  "%A",    tm_info); /* Tuesday   */
		strftime(date_str, sizeof(date_str), "%d %B", tm_info); /* 30 July   */
		strftime(time_str, sizeof(time_str), "- %H:%M -", tm_info);

		cairo_select_font_face(cr, state->args.font,
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

		/* Day */
		cairo_set_font_size(cr, 54.0);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
		draw_text_centered(cr, day_str, cx, 68.0);

		/* Date */
		cairo_set_font_size(cr, 26.0);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.88);
		draw_text_centered(cr, date_str, cx, 102.0);

		/* Time */
		cairo_set_font_size(cr, 16.0);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.70);
		draw_text_centered(cr, time_str, cx, 126.0);
	}

	/* ── 2. Avatar circle ────────────────────────────────────────── */
	const double AV_CY = 205.0;
	const double AV_R  = 42.0;

	/* Outer glow ring */
	cairo_arc(cr, cx, AV_CY, AV_R + 3.0, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
	cairo_set_line_width(cr, 2.0);
	cairo_stroke(cr);

	if (state->avatar_image) {
		/* Clip to circle */
		cairo_save(cr);
		cairo_arc(cr, cx, AV_CY, AV_R, 0, 2.0 * M_PI);
		cairo_clip(cr);

		int iw = cairo_image_surface_get_width(state->avatar_image);
		int ih = cairo_image_surface_get_height(state->avatar_image);
		double img_side = (double)MIN(iw, ih);
		double img_scale = (AV_R * 2.0) / img_side;

		cairo_translate(cr,
			cx - (double)iw * img_scale / 2.0,
			AV_CY - (double)ih * img_scale / 2.0);
		cairo_scale(cr, img_scale, img_scale);
		cairo_set_source_surface(cr, state->avatar_image, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		/* Fallback: gradient circle + person icon */
		cairo_arc(cr, cx, AV_CY, AV_R, 0, 2.0 * M_PI);
		cairo_set_source_rgba(cr, 0.3, 0.35, 0.5, 0.60);
		cairo_fill(cr);
		draw_person_icon(cr, cx, AV_CY, AV_R);
	}

	/* Avatar border */
	cairo_arc(cr, cx, AV_CY, AV_R, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.40);
	cairo_set_line_width(cr, 1.8);
	cairo_stroke(cr);

	/* ── 3. Username pill ────────────────────────────────────────── */
	const double PILL_X = 35.0;
	const double PILL_W = PW - 70.0;
	const double PILL_H = 44.0;
	double pill_y = 267.0;

	draw_pill(cr, PILL_X, pill_y, PILL_W, PILL_H,
		1, 1, 1, 0.13, 0.28);

	/* Person icon inside pill */
	draw_person_icon(cr, PILL_X + 26.0, pill_y + PILL_H / 2.0, 10.0);

	/* Username text */
	cairo_select_font_face(cr, state->args.font,
		CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 15.0);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.90);
	cairo_text_extents_t ext;
	const char *uname = state->args.user_name ? state->args.user_name : "user";
	cairo_text_extents(cr, uname, &ext);
	cairo_move_to(cr,
		PILL_X + 50.0,
		pill_y + (PILL_H + ext.height) / 2.0 - ext.height + ext.height);
	cairo_show_text(cr, uname);

	/* ── 4. Password pill ────────────────────────────────────────── */
	pill_y = 322.0;

	/* State-dependent tint */
	double pr = 1, pg = 1, pb = 1, pa = 0.13, ba = 0.28;
	if (state->auth_state == AUTH_STATE_VALIDATING) {
		pr = 0.20; pg = 0.55; pb = 1.0; pa = 0.22; ba = 0.45;
	} else if (state->auth_state == AUTH_STATE_INVALID) {
		pr = 1.0; pg = 0.20; pb = 0.25; pa = 0.22; ba = 0.45;
	} else if (state->input_state == INPUT_STATE_CLEAR) {
		pr = 1.0; pg = 0.65; pb = 0.10; pa = 0.18; ba = 0.38;
	}

	draw_pill(cr, PILL_X, pill_y, PILL_W, PILL_H, pr, pg, pb, pa, ba);

	/* Password dots or status text */
	size_t pw_len = state->password.len;
	if (state->auth_state == AUTH_STATE_VALIDATING) {
		cairo_set_font_size(cr, 13.0);
		cairo_set_source_rgba(cr, 0.7, 0.9, 1.0, 0.95);
		draw_text_centered(cr, "Verifying...", cx, pill_y + PILL_H * 0.67);
	} else if (state->auth_state == AUTH_STATE_INVALID) {
		cairo_set_font_size(cr, 13.0);
		cairo_set_source_rgba(cr, 1.0, 0.6, 0.6, 0.95);
		draw_text_centered(cr, "Incorrect password", cx, pill_y + PILL_H * 0.67);
	} else if (state->input_state == INPUT_STATE_CLEAR) {
		cairo_set_font_size(cr, 13.0);
		cairo_set_source_rgba(cr, 1.0, 0.85, 0.4, 0.95);
		draw_text_centered(cr, "Cleared", cx, pill_y + PILL_H * 0.67);
	} else if (pw_len > 0) {
		/* Draw dots */
		int ndots = (int)MIN(pw_len, 20);
		double dot_r = 4.0;
		double spacing = 10.0;
		double total_w = ndots * dot_r * 2.0 + (ndots - 1) * spacing;
		double dot_start_x = cx - total_w / 2.0 + dot_r;
		double dot_cy = pill_y + PILL_H / 2.0;

		cairo_set_source_rgba(cr, 1, 1, 1, 0.92);
		for (int i = 0; i < ndots; i++) {
			cairo_arc(cr,
				dot_start_x + i * (dot_r * 2.0 + spacing),
				dot_cy, dot_r, 0, 2.0 * M_PI);
			cairo_fill(cr);
		}
	} else {
		/* Placeholder text */
		cairo_set_font_size(cr, 13.5);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
		draw_text_centered(cr, "Enter password", cx, pill_y + PILL_H * 0.67);
	}

	/* ── 5. Caps Lock warning ────────────────────────────────────── */
	if (state->xkb.caps_lock && state->args.show_caps_lock_text) {
		cairo_set_font_size(cr, 11.5);
		cairo_set_source_rgba(cr, 1.0, 0.85, 0.3, 0.88);
		draw_text_centered(cr, "⇪ Caps Lock", cx, pill_y + PILL_H + 18.0);
	}

	/* ── 6. System buttons ───────────────────────────────────────── */
	const double BTN_Y  = 430.0;
	const double BTN_R  = 20.0;
	const double BTN_SP = 75.0;

	double btn_positions[3] = {
		cx - BTN_SP,
		cx,
		cx + BTN_SP,
	};

	/* Button circle backgrounds */
	for (int i = 0; i < 3; i++) {
		cairo_arc(cr, btn_positions[i], BTN_Y, BTN_R, 0, 2.0 * M_PI);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.10);
		cairo_fill_preserve(cr);
		cairo_set_source_rgba(cr, 1, 1, 1, 0.20);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	draw_power_icon  (cr, btn_positions[0], BTN_Y, BTN_R);
	draw_restart_icon(cr, btn_positions[1], BTN_Y, BTN_R);
	draw_moon_icon   (cr, btn_positions[2], BTN_Y, BTN_R);

	/* ── Send to Wayland ─────────────────────────────────────────── */
	wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);
	wl_surface_set_buffer_scale(surface->child, sc);
	wl_surface_attach(surface->child, buf->buffer, 0, 0);
	wl_surface_damage_buffer(surface->child, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface->child);

	return true;
}

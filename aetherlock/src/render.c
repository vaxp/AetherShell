#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <wayland-client.h>
#include <pango/pangocairo.h>
#include "cairo.h"
#include "background-image.h"
#include "aetherlock.h"

static inline void set_color(cairo_t *cr, struct color_rgba c) {
	cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}
#include "sysstats.h"
#include "vaxp_logo.h"
#include "log.h"
#include "sysinfo.h"
#include <sys/sysinfo.h>

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

static double draw_text_centered(cairo_t *cr, const char *text,
		double cx, double y) {
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);
	cairo_move_to(cr, cx - ext.width / 2.0 - ext.x_bearing, y);
	cairo_show_text(cr, text);
	return ext.width;
}

static void draw_text_pango(cairo_t *cr, const char *text, const char *font_family, double font_size, bool bold, double cx, double cy, double max_width) {
	PangoLayout *layout = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout, text, -1);
	
	PangoFontDescription *desc = pango_font_description_from_string(font_family);
	pango_font_description_set_absolute_size(desc, font_size * PANGO_SCALE);
	if (bold) {
		pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);
	}
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);
	
	if (max_width > 0) {
		pango_layout_set_width(layout, max_width * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
	}

	int w, h;
	pango_layout_get_pixel_size(layout, &w, &h);

	// Pango uses top-left origin, cy is usually bottom/baseline for cairo.
	// We'll treat cy as the baseline if we were using cairo, so offset by an estimated baseline.
	// But actually, just center it vertically around cy-h/2 for visual consistency with other elements
	cairo_move_to(cr, cx - w / 2.0, cy - h / 1.5); // Adjusting cy to approximate baseline
	pango_cairo_update_layout(cr, layout);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
}

static void draw_person_icon(cairo_t *cr, double cx, double cy, double r) {
	cairo_set_source_rgba(cr, 1, 1, 1, 0.70);
	cairo_arc(cr, cx, cy - r * 0.30, r * 0.32, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_arc(cr, cx, cy + r * 0.55, r * 0.50, M_PI, 2.0 * M_PI);
	cairo_fill(cr);
}

static void draw_play_icon(cairo_t *cr, double x, double y) {
	cairo_new_path(cr);
	cairo_move_to(cr, x - 4, y - 8);
	cairo_line_to(cr, x + 8, y);
	cairo_line_to(cr, x - 4, y + 8);
	cairo_close_path(cr);
	cairo_fill(cr);
}

static void draw_pause_icon(cairo_t *cr, double x, double y) {
	cairo_new_path(cr);
	cairo_rectangle(cr, x - 6, y - 8, 4, 16);
	cairo_fill(cr);
	cairo_new_path(cr);
	cairo_rectangle(cr, x + 2, y - 8, 4, 16);
	cairo_fill(cr);
}

static void draw_next_icon(cairo_t *cr, double x, double y) {
	cairo_new_path(cr);
	cairo_move_to(cr, x - 5, y - 6);
	cairo_line_to(cr, x + 3, y);
	cairo_line_to(cr, x - 5, y + 6);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_new_path(cr);
	cairo_rectangle(cr, x + 3, y - 6, 3, 12);
	cairo_fill(cr);
}

static void draw_prev_icon(cairo_t *cr, double x, double y) {
	cairo_new_path(cr);
	cairo_move_to(cr, x + 5, y - 6);
	cairo_line_to(cr, x - 3, y);
	cairo_line_to(cr, x + 5, y + 6);
	cairo_close_path(cr);
	cairo_fill(cr);
	cairo_new_path(cr);
	cairo_rectangle(cr, x - 6, y - 6, 3, 12);
	cairo_fill(cr);
}

static void draw_progress_ring(cairo_t *cr, double cx, double cy, double r, double percentage, double r_col, double g_col, double b_col) {
    cairo_set_line_width(cr, 5.0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    // Background ring
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
    cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
    cairo_stroke(cr);

    // Foreground ring
    if (percentage > 0.0) {
		percentage = MIN(percentage, 100.0);
        cairo_set_source_rgba(cr, r_col, g_col, b_col, 1.0);
        double end_angle = -M_PI / 2.0 + (percentage / 100.0) * 2.0 * M_PI;
        cairo_arc(cr, cx, cy, r, -M_PI / 2.0, end_angle);
        cairo_stroke(cr);
    }
}

/* ── New Layout Helpers ─────────────────────────────────────────────────── */

static void draw_card_bg(cairo_t *cr, struct aetherlock_state *state, double x, double y, double w, double h) {
	rounded_rect(cr, x, y, w, h, 16.0);
	set_color(cr, state->vaxp_colors.panel_bg); // panel-bg
	cairo_fill_preserve(cr);
	set_color(cr, state->vaxp_colors.panel_border); // panel-border
	cairo_set_line_width(cr, state->vaxp_colors.panel_border_width);
	cairo_stroke(cr);
}

static void hex_to_rgb(const char* hex, double *r, double *g, double *b) {
	if (hex[0] == '#') hex++;
	int num = (int)strtol(hex, NULL, 16);
	*r = ((num >> 16) & 0xFF) / 255.0;
	*g = ((num >> 8) & 0xFF) / 255.0;
	*b = (num & 0xFF) / 255.0;
}



/* ── Main frame renderer ─────────────────────────────────────────────── */

static bool render_frame(struct aetherlock_surface *surface) {
	struct aetherlock_state *state = surface->state;
	double scale = (double)surface->scale;

	/* ── Layout constants (logical pixels) ── */
	const double PW = 1344.0;
	const double PH = 580.0;
	
	const double COL_W = 400.0;
	const double MID_W = 440.0;
	const double GAP = 24.0;
	const double PAD = 28.0;

	int buffer_width  = (int)(PW * scale);
	int buffer_height = (int)(PH * scale);
	int sc = (int)scale;
	if (sc > 1) {
		if (buffer_width  % sc) buffer_width  += sc - (buffer_width  % sc);
		if (buffer_height % sc) buffer_height += sc - (buffer_height % sc);
	}

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
	cairo_scale(cr, scale, scale);

	/* Clear */
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cr);
	cairo_restore(cr);

	/* Panel Base */
	rounded_rect(cr, 0, 0, PW, PH, 22.0);
	cairo_set_source_rgba(cr, 8.0/255.0, 14.0/255.0, 14.0/255.0, 0.45);
	cairo_fill_preserve(cr);
	set_color(cr, state->vaxp_colors.outer_border);
	cairo_set_line_width(cr, state->vaxp_colors.outer_border_width);
	cairo_stroke(cr);

	/* ── Column 1: Left ───────────────────────────────────────────── */
	double cx1 = PAD;
	double cy = PAD;
	
	// Weather Card
	double w_card_h = 100.0;
	draw_card_bg(cr, state, cx1, cy, COL_W, w_card_h);
	
	cairo_select_font_face(cr, state->args.font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 20.0);
	set_color(cr, state->vaxp_colors.accent); // teal
	cairo_move_to(cr, cx1 + 20, cy + 35);
	cairo_show_text(cr, state->weather_fetched ? state->weather.location : "Weather");
	
	// Weather icon placeholder (Cloud)
	cairo_new_path(cr);
	cairo_set_line_width(cr, 2.0);
	set_color(cr, state->vaxp_colors.text_dim); // text-dim
	
	cairo_arc(cr, cx1 + 35, cy + 70, 8, M_PI/2, M_PI*1.5);
	cairo_arc(cr, cx1 + 45, cy + 62, 12, M_PI, 2*M_PI);
	cairo_arc(cr, cx1 + 55, cy + 68, 10, -M_PI/2, M_PI/2);
	cairo_move_to(cr, cx1 + 35, cy + 78);
	cairo_line_to(cr, cx1 + 55, cy + 78);
	cairo_stroke(cr);
	
	cairo_set_font_size(cr, 18.0);
	set_color(cr, state->vaxp_colors.text_bright); // text-bright
	cairo_move_to(cr, cx1 + 80, cy + 60);
	cairo_show_text(cr, state->weather.condition);
	
	cairo_select_font_face(cr, state->args.font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.0);
	set_color(cr, state->vaxp_colors.text_dim);
	cairo_move_to(cr, cx1 + 80, cy + 80);
	cairo_show_text(cr, state->weather.temperature);
	
	cy += w_card_h + 16.0;

	// Fetch Card
	double f_card_h = 200.0;
	draw_card_bg(cr, state, cx1, cy, COL_W, f_card_h);
	// Prompt >
	cairo_arc(cr, cx1 + 33, cy + 31, 13, 0, 2*M_PI);
	set_color(cr, state->vaxp_colors.accent_dim);
	cairo_fill(cr);
	set_color(cr, state->vaxp_colors.accent);
	cairo_set_font_size(cr, 13.0);
	draw_text_centered(cr, ">", cx1 + 33, cy + 36);
	
	cairo_set_font_size(cr, 14.0);
	set_color(cr, state->vaxp_colors.text_dim);
	cairo_move_to(cr, cx1 + 56, cy + 36);
	cairo_show_text(cr, "VAXP.org");
	
	// VAXP Logo
	set_color(cr, state->vaxp_colors.text_bright); // White-ish color
	draw_vaxp_logo(cr, cx1 + 55, cy + 92, 0.086);
	
	// Specs
	static struct sysinfo_data sinfo = {0};
	static bool sinfo_fetched = false;
	if (!sinfo_fetched) {
		fetch_sysinfo(&sinfo);
		sinfo_fetched = true;
	} else {
		struct sysinfo si;
		if (sysinfo(&si) == 0) {
			long days = si.uptime / 86400;
			long hours = (si.uptime % 86400) / 3600;
			long mins = (si.uptime % 3600) / 60;
			if (days > 0) snprintf(sinfo.uptime, sizeof(sinfo.uptime), "%ld days, %ld hours", days, hours);
			else if (hours > 0) snprintf(sinfo.uptime, sizeof(sinfo.uptime), "%ld hours, %ld mins", hours, mins);
			else snprintf(sinfo.uptime, sizeof(sinfo.uptime), "%ld mins", mins);
		}
	}

	set_color(cr, state->vaxp_colors.text_dim);
	char spec_buf[128];
	
	snprintf(spec_buf, sizeof(spec_buf), "OS  : %s", sinfo.os_name);
	cairo_move_to(cr, cx1 + 120, cy + 75);
	cairo_show_text(cr, spec_buf);
	
	snprintf(spec_buf, sizeof(spec_buf), "DE  : %s", sinfo.de_name);
	cairo_move_to(cr, cx1 + 120, cy + 95);
	cairo_show_text(cr, spec_buf);
	
	snprintf(spec_buf, sizeof(spec_buf), "USER: %s", sinfo.user_name);
	cairo_move_to(cr, cx1 + 120, cy + 115);
	cairo_show_text(cr, spec_buf);
	
	snprintf(spec_buf, sizeof(spec_buf), "UP  : %s", sinfo.uptime);
	cairo_move_to(cr, cx1 + 120, cy + 135);
	cairo_show_text(cr, spec_buf);
	
	// Swatches
	const char* swatches[] = {"#3a3a3a", "#a8c93a", "#5fd9a8", "#7ee0c9", "#6f9b95", "#5b7fd6", "#7ee0c9"};
	double sw_x = cx1 + 20;
	for (int i=0; i<7; i++) {
		rounded_rect(cr, sw_x, cy + 160, 22, 22, 6.0);
		double sr, sg, sb;
		hex_to_rgb(swatches[i], &sr, &sg, &sb);
		cairo_set_source_rgba(cr, sr, sg, sb, 1.0);
		cairo_fill(cr);
		sw_x += 30;
	}
	
	cy += f_card_h + 16.0;

	// Now Playing Card
	double np_card_h = 160.0;
	draw_card_bg(cr, state, cx1, cy, COL_W, np_card_h);
	// Content
	cairo_set_font_size(cr, 13.0);
	set_color(cr, state->vaxp_colors.text_dim);
	cairo_move_to(cr, cx1 + 20, cy + 30);
	cairo_show_text(cr, "Now playing");
	
	double text_cx = cx1 + COL_W/2;
	double max_text_w = 360.0;

	if (state->mpris_art_surface) {
		double iw = cairo_image_surface_get_width(state->mpris_art_surface);
		double ih = cairo_image_surface_get_height(state->mpris_art_surface);
		double size = 64.0;
		double scale_x = size / iw;
		double scale_y = size / ih;
		double scale = MAX(scale_x, scale_y);

		cairo_save(cr);
		rounded_rect(cr, cx1 + 20, cy + 45, size, size, 8.0);
		cairo_clip(cr);
		// Center the image if aspect ratio isn't exactly 1:1
		double tx = cx1 + 20 + (size - iw * scale) / 2.0;
		double ty = cy + 45 + (size - ih * scale) / 2.0;
		cairo_translate(cr, tx, ty);
		cairo_scale(cr, scale, scale);
		cairo_set_source_surface(cr, state->mpris_art_surface, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);

		max_text_w = COL_W - 40 - size - 16;
		text_cx = cx1 + 20 + size + 16 + max_text_w / 2.0;
	}

	// Text rendering with Pango for font fallback
	set_color(cr, state->vaxp_colors.text_bright);
		const char *title = state->mpris_title ? state->mpris_title : "No Media";
		set_color(cr, state->vaxp_colors.text_bright);
		
		PangoLayout *layout_title = pango_cairo_create_layout(cr);
		pango_layout_set_text(layout_title, title, -1);
		PangoFontDescription *desc_title = pango_font_description_from_string(state->args.font);
		pango_font_description_set_absolute_size(desc_title, 18.0 * PANGO_SCALE);
		pango_font_description_set_weight(desc_title, PANGO_WEIGHT_BOLD);
		pango_layout_set_font_description(layout_title, desc_title);
		pango_font_description_free(desc_title);
		
		int tw, th;
		pango_layout_get_pixel_size(layout_title, &tw, &th);
		
		if (tw > max_text_w) {
			state->mpris_title_scroll = true;
			cairo_save(cr);
			cairo_rectangle(cr, text_cx - max_text_w/2, cy + 40, max_text_w, 35);
			cairo_clip(cr);
			
			if (state->marquee_offset > tw + 50) {
				state->marquee_offset = -max_text_w;
			}
			
			cairo_move_to(cr, (text_cx - max_text_w/2) - state->marquee_offset, cy + 60 - th/1.5);
			pango_cairo_update_layout(cr, layout_title);
			pango_cairo_show_layout(cr, layout_title);
			cairo_restore(cr);
		} else {
			state->mpris_title_scroll = false;
			state->marquee_offset = 0;
			cairo_move_to(cr, text_cx - tw/2.0, cy + 60 - th/1.5);
			pango_cairo_update_layout(cr, layout_title);
			pango_cairo_show_layout(cr, layout_title);
		}
		g_object_unref(layout_title);
	
	set_color(cr, state->vaxp_colors.text_dim);
	draw_text_pango(cr, state->mpris_artist ? state->mpris_artist : "", state->args.font, 13.0, false, text_cx, cy + 85, max_text_w);

	// Now Playing Card Controls
	set_color(cr, state->vaxp_colors.panel_border);
	cairo_arc(cr, cx1 + COL_W/2 - 50, cy + 120, 19, 0, 2*M_PI);
	cairo_fill(cr);
	cairo_arc(cr, cx1 + COL_W/2 + 50, cy + 120, 19, 0, 2*M_PI);
	cairo_fill(cr);
	
	set_color(cr, state->vaxp_colors.accent);
	cairo_arc(cr, cx1 + COL_W/2, cy + 120, 24, 0, 2*M_PI);
	cairo_fill(cr);

	// Draw Next and Prev Icons
	set_color(cr, state->vaxp_colors.text_dim);
	draw_prev_icon(cr, cx1 + COL_W/2 - 50, cy + 120);
	draw_next_icon(cr, cx1 + COL_W/2 + 50, cy + 120);

	// Play pause icon
	cairo_set_source_rgba(cr, 3.0/255.0, 38.0/255.0, 29.0/255.0, 1.0);
	if (state->mpris_playing) {
		draw_pause_icon(cr, cx1 + COL_W/2, cy + 120);
	} else {
		draw_play_icon(cr, cx1 + COL_W/2, cy + 120);
	}

	// ── Column 2: Center ───────────────────────────────────────────
	double cx2 = cx1 + COL_W + GAP;
	double mid_x = cx2 + MID_W / 2.0;

	if (state->args.show_clock) {
		time_t t = time(NULL);
		struct tm *tm_info = localtime(&t);

		char date_str[64], time_str[32], ampm_str[8];
		strftime(date_str, sizeof(date_str), "%A, %e %B %Y", tm_info); 
		strftime(time_str, sizeof(time_str), "%I:%M", tm_info);
		strftime(ampm_str, sizeof(ampm_str), "%p", tm_info);

		cairo_select_font_face(cr, state->args.font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, 64.0);
		set_color(cr, state->vaxp_colors.text_bright);
		
		cairo_text_extents_t ext_time, ext_ampm;
		cairo_text_extents(cr, time_str, &ext_time);
		
		cairo_set_font_size(cr, 24.0);
		cairo_text_extents(cr, ampm_str, &ext_ampm);
		
		double total_w = ext_time.width + 8.0 + ext_ampm.width;
		double start_x = mid_x - total_w / 2.0;
		
		cairo_set_font_size(cr, 64.0);
		cairo_move_to(cr, start_x - ext_time.x_bearing, PAD + 80);
		cairo_show_text(cr, time_str);
		
		cairo_set_font_size(cr, 24.0);
		set_color(cr, state->vaxp_colors.accent); // teal
		cairo_move_to(cr, start_x + ext_time.width + 8.0 - ext_ampm.x_bearing, PAD + 80);
		cairo_show_text(cr, ampm_str);
		
		cairo_select_font_face(cr, state->args.font, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(cr, 18.0);
		set_color(cr, state->vaxp_colors.text_dim);
		draw_text_centered(cr, date_str, mid_x, PAD + 115);
	}

	// Avatar
	const double AV_CY = PAD + 265.0;
	const double AV_R  = 100.0;
	
	cairo_new_path(cr);
	cairo_arc(cr, mid_x, AV_CY, AV_R, 0, 2.0 * M_PI);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	
	if (state->avatar_image) {
		cairo_save(cr);
		cairo_arc(cr, mid_x, AV_CY, AV_R, 0, 2.0 * M_PI);
		cairo_clip(cr);
		int iw = cairo_image_surface_get_width(state->avatar_image);
		int ih = cairo_image_surface_get_height(state->avatar_image);
		double img_side = (double)MIN(iw, ih);
		double img_scale = (AV_R * 2.0) / img_side;
		cairo_translate(cr, mid_x - (double)iw * img_scale / 2.0, AV_CY - (double)ih * img_scale / 2.0);
		cairo_scale(cr, img_scale, img_scale);
		cairo_set_source_surface(cr, state->avatar_image, 0, 0);
		cairo_paint(cr);
		cairo_restore(cr);
	} else {
		cairo_arc(cr, mid_x, AV_CY, AV_R, 0, 2.0 * M_PI);
		set_color(cr, state->vaxp_colors.background);
		cairo_fill(cr);
		draw_person_icon(cr, mid_x, AV_CY, AV_R * 0.5);
	}

	// PW Box
	const double PW_W = 340.0;
	const double PW_H = 48.0;
	double pw_y = PAD + 395.0;
	double pw_x = mid_x - PW_W / 2.0;
	
	rounded_rect(cr, pw_x, pw_y, PW_W, PW_H, 24.0);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.05);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.08);
	cairo_set_line_width(cr, 1.0);
	cairo_stroke(cr);
	
	// lock icon
	cairo_set_line_width(cr, 1.5);
	set_color(cr, state->vaxp_colors.text_dim);
	rounded_rect(cr, pw_x + 21, pw_y + PW_H/2 - 2, 14, 10, 2.0);
	cairo_stroke(cr);
	cairo_arc(cr, pw_x + 28, pw_y + PW_H/2 - 2, 4, M_PI, 2*M_PI);
	cairo_stroke(cr);

	size_t pw_len = state->password.len;
	cairo_set_font_size(cr, 15.0);
	if (state->auth_state == AUTH_STATE_VALIDATING) {
		set_color(cr, state->vaxp_colors.accent);
		cairo_move_to(cr, pw_x + 50, pw_y + PW_H/2 + 5);
		cairo_show_text(cr, "Verifying...");
	} else if (state->auth_state == AUTH_STATE_INVALID) {
		cairo_set_source_rgba(cr, 1.0, 0.3, 0.3, 1.0);
		cairo_move_to(cr, pw_x + 50, pw_y + PW_H/2 + 5);
		cairo_show_text(cr, "Incorrect password");
	} else if (pw_len > 0) {
		int ndots = (int)MIN(pw_len, 20);
		double dot_r = 4.0;
		double spacing = 8.0;
		set_color(cr, state->vaxp_colors.text_bright);
		for (int i = 0; i < ndots; i++) {
			cairo_arc(cr, pw_x + 50 + i * (dot_r * 2.0 + spacing), pw_y + PW_H / 2.0, dot_r, 0, 2.0 * M_PI);
			cairo_fill(cr);
		}
	} else {
		set_color(cr, state->vaxp_colors.text_dim);
		cairo_move_to(cr, pw_x + 50, pw_y + PW_H/2 + 5);
		cairo_show_text(cr, "Enter your password");
	}
	
	// arrow button
	cairo_arc(cr, pw_x + PW_W - 24, pw_y + PW_H/2, 15, 0, 2*M_PI);
	set_color(cr, state->vaxp_colors.panel_border);
	cairo_fill(cr);
	cairo_move_to(cr, pw_x + PW_W - 27, pw_y + PW_H/2 - 4);
	cairo_line_to(cr, pw_x + PW_W - 21, pw_y + PW_H/2);
	cairo_line_to(cr, pw_x + PW_W - 27, pw_y + PW_H/2 + 4);
	set_color(cr, state->vaxp_colors.text_dim);
	cairo_stroke(cr);

	// ── Column 3: Right ────────────────────────────────────────────
	double cx3 = cx2 + MID_W + GAP;
	cy = PAD;
	
	// Stats Grid
	double stat_size = (COL_W - 16.0) / 2.0;
	
	const char *stat_names[] = {"CPU", "RAM", "Disk", "Temp"};
	double stat_vals[] = {state->sysstats.cpu_usage, state->sysstats.ram_usage, state->sysstats.disk_usage, state->sysstats.temperature};
	double stat_max[] = {100.0, 100.0, 100.0, 100.0}; // percentage except temp which we cap at 100C
	const char *stat_fmt[] = {"%.0f%%", "%.0f%%", "%.0f%%", "%.0f°C"};
	// Colors
	double r_c[] = {126.0/255.0, 168.0/255.0, 95.0/255.0, 255.0/255.0};
	double g_c[] = {224.0/255.0, 201.0/255.0, 217.0/255.0, 100.0/255.0};
	double b_c[] = {201.0/255.0, 58.0/255.0, 168.0/255.0, 100.0/255.0};

	for(int i=0; i<4; i++) {
		double sx = cx3 + (i%2) * (stat_size + 16.0);
		double sy = cy + (i/2) * (stat_size + 16.0);
		draw_card_bg(cr, state, sx, sy, stat_size, stat_size);
		
		// Progress Ring (centered differently for the right side)
		draw_progress_ring(cr, sx + stat_size/2, sy + stat_size/2, stat_size/2 - 25, (stat_vals[i] / stat_max[i]) * 100.0, r_c[i], g_c[i], b_c[i]);
		
		// We'll put text in the center
		char buf[32];
		snprintf(buf, sizeof(buf), stat_fmt[i], stat_vals[i]);
		cairo_set_font_size(cr, 24.0);
		set_color(cr, state->vaxp_colors.text_bright);
		draw_text_centered(cr, buf, sx + stat_size/2, sy + stat_size/2 - 10);
		
		cairo_set_font_size(cr, 12.0);
		set_color(cr, state->vaxp_colors.text_dim);
		draw_text_centered(cr, stat_names[i], sx + stat_size/2, sy + stat_size/2 + 15);
	}
	
	cy += stat_size * 2 + 16.0 + 16.0;
	
	// Notifications Card
	double notif_h = PH - cy - PAD;
	draw_card_bg(cr, state, cx3, cy, COL_W, notif_h);
	cairo_set_font_size(cr, 13.0);
	set_color(cr, state->vaxp_colors.text_dim);
	cairo_move_to(cr, cx3 + 20, cy + 30);
	const char *app_title = (state->latest_notif_app && !state->notifications_dnd) ? state->latest_notif_app : "Notifications";
	PangoLayout *layout_app = pango_cairo_create_layout(cr);
	pango_layout_set_text(layout_app, app_title, -1);
	PangoFontDescription *desc_app = pango_font_description_from_string(state->args.font);
	pango_font_description_set_absolute_size(desc_app, 13.0 * PANGO_SCALE);
	pango_layout_set_font_description(layout_app, desc_app);
	pango_font_description_free(desc_app);
	cairo_move_to(cr, cx3 + 20, cy + 17);
	pango_cairo_update_layout(cr, layout_app);
	pango_cairo_show_layout(cr, layout_app);
	g_object_unref(layout_app);
	
	if (state->notifications_dnd) {
		cairo_set_font_size(cr, 14.0);
		draw_text_centered(cr, "Do Not Disturb is On", cx3 + COL_W/2, cy + notif_h - 40);
	} else if (state->latest_notif_summary) {
		double text_cx = cx3 + COL_W/2;
		double max_text_w = COL_W - 40.0;
		
		if (state->latest_notif_icon) {
			cairo_set_source_surface(cr, state->latest_notif_icon, cx3 + 20, cy + 50);
			cairo_paint(cr);
			max_text_w = COL_W - 88.0;
			text_cx = cx3 + 78 + max_text_w/2;
		}

		set_color(cr, state->vaxp_colors.text_bright);
		if (state->vaxp_colors.hide_notification_content) {
			draw_text_pango(cr, "****************", state->args.font, 16.0, true, text_cx, cy + 60, max_text_w);
			set_color(cr, state->vaxp_colors.text_dim);
			draw_text_pango(cr, "****************", state->args.font, 13.0, false, text_cx, cy + 85, max_text_w);
		} else {
			draw_text_pango(cr, state->latest_notif_summary, state->args.font, 16.0, true, text_cx, cy + 60, max_text_w);
			set_color(cr, state->vaxp_colors.text_dim);
			draw_text_pango(cr, state->latest_notif_body ? state->latest_notif_body : "", state->args.font, 13.0, false, text_cx, cy + 85, max_text_w);
		}
	} else {
		cairo_set_font_size(cr, 14.0);
		draw_text_centered(cr, "No Notifications", cx3 + COL_W/2, cy + notif_h - 40);
	}

	/* ── Send to Wayland ─────────────────────────────────────────── */
	wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);
	wl_surface_set_buffer_scale(surface->child, sc);
	wl_surface_attach(surface->child, buf->buffer, 0, 0);
	wl_surface_damage_buffer(surface->child, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(surface->child);

	return true;
}

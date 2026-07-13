#include "ui/view_ai.h"
#include "core/ai_ctrl.h"
#include <gtk/gtk.h>
#include <string.h>
#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm-core-extensions.h>
#include <math.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *message_list_box;
    GtkWidget *current_ai_textview;
    GtkWidget *scroll;
    GtkWidget *entry;
    GtkWidget *thinking_row;
    gchar *response;
    gint char_index;
    guint type_timer;
    gboolean in_code_block;
    gboolean in_inline_code;
    gboolean in_bold;
    gboolean skip_until_newline;
    gboolean is_done;
    gboolean in_execute_block;
    gchar *extracted_command;
    gboolean in_bg_execute_block;
    gchar *extracted_bg_command;
    gchar *last_user_query;
    gboolean ignore_rest;
    GtkWidget *action_box;
    GtkWidget *header_canvas;
    guint anim_timer;
    gdouble anim_t;
} AiChatData;

/* ================= VAXP angular-cut drawing helpers =================
 * GTK3 CSS has no clip-path, so the "cut corner" look from the web
 * prototype is produced by connecting to a container's "draw" signal
 * (which fires BEFORE the container's default handler paints its
 * children, since GtkWidget::draw is RUN_LAST) and painting a cairo
 * polygon behind the real child widget. The child itself gets a
 * transparent CSS background so the shape shows through.
 * ===================================================================*/

static void vaxp_cut_path(cairo_t *cr, double x, double y, double w, double h,
                           double cut, gboolean tl, gboolean tr, gboolean br, gboolean bl) {
    cairo_new_path(cr);
    cairo_move_to(cr, x + (tl ? cut : 0), y);
    if (tr) { cairo_line_to(cr, x + w - cut, y); cairo_line_to(cr, x + w, y + cut); }
    else      cairo_line_to(cr, x + w, y);
    if (br) { cairo_line_to(cr, x + w, y + h - cut); cairo_line_to(cr, x + w - cut, y + h); }
    else      cairo_line_to(cr, x + w, y + h);
    if (bl) { cairo_line_to(cr, x + cut, y + h); cairo_line_to(cr, x, y + h - cut); }
    else      cairo_line_to(cr, x, y + h);
    cairo_close_path(cr);
}

static void vaxp_glow_stroke(cairo_t *cr, double r, double g, double b, double alpha) {
    cairo_push_group(cr);
    cairo_set_line_width(cr, 4.0);
    cairo_set_source_rgba(cr, r, g, b, alpha * 0.12);
    cairo_stroke_preserve(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_stroke(cr);
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);
}

/* AI bubble: dark glass fill, cyan hairline, top-start corner cut. */
static gboolean vaxp_ai_bubble_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    gboolean rtl = (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL);

    vaxp_cut_path(cr, 0, 0, w, h, 14, !rtl, rtl, FALSE, FALSE);
    cairo_set_source_rgba(cr, 0.09, 0.10, 0.14, 0.72);
    cairo_fill_preserve(cr);
    vaxp_glow_stroke(cr, 0.31, 0.89, 0.81, 0.28);
    return FALSE; /* let the class handler paint the child (label/textview) on top */
}

/* User bubble: cyan/violet tinted glass, opposite corner cut. */
static gboolean vaxp_user_bubble_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    gboolean rtl = (gtk_widget_get_direction(widget) == GTK_TEXT_DIR_RTL);

    vaxp_cut_path(cr, 0, 0, w, h, 14, rtl, !rtl, FALSE, FALSE);
    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, w, h);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 0.31, 0.89, 0.81, 0.20);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, 0.61, 0.53, 0.96, 0.18);
    cairo_set_source(cr, grad);
    cairo_fill_preserve(cr);
    cairo_pattern_destroy(grad);
    vaxp_glow_stroke(cr, 0.31, 0.89, 0.81, 0.32);
    return FALSE;
}

/* Entry input shell: angular cut on both trailing corners. */
static gboolean vaxp_entry_wrap_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    (void)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    vaxp_cut_path(cr, 0, 0, w, h, 16, TRUE, FALSE, TRUE, FALSE);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.035);
    cairo_fill_preserve(cr);
    vaxp_glow_stroke(cr, 1.0, 1.0, 1.0, 0.10);
    return FALSE;
}

static void vaxp_hex_ring(cairo_t *cr, double cx, double cy, double radius, double rot,
                           double r, double g, double b, double alpha, double lw) {
    cairo_new_path(cr);
    for (int i = 0; i < 6; i++) {
        double ang = rot + i * (G_PI / 3.0);
        double x = cx + radius * cos(ang);
        double y = cy + radius * sin(ang);
        if (i == 0) cairo_move_to(cr, x, y); else cairo_line_to(cr, x, y);
    }
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, r, g, b, alpha);
    cairo_set_line_width(cr, lw);
    cairo_stroke(cr);
}

/* Header "core" logo: two counter-rotating hex rings + a pulsing dot,
 * matching the animated SVG mark from the web prototype. */
static gboolean vaxp_header_canvas_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    double cx = w / 2.0, cy = h / 2.0;

    vaxp_hex_ring(cr, cx, cy, w * 0.42, data->anim_t, 0.31, 0.89, 0.81, 0.75, 1.4);
    vaxp_hex_ring(cr, cx, cy, w * 0.29, -data->anim_t * 1.4, 0.61, 0.53, 0.96, 0.6, 1.1);

    double pulse = (w * 0.09) + (w * 0.03) * sin(data->anim_t * 2.4);
    cairo_arc(cr, cx, cy, pulse * 1.8, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 0.31, 0.89, 0.81, 0.12);
    cairo_fill(cr);
    cairo_arc(cr, cx, cy, pulse, 0, 2 * G_PI);
    cairo_set_source_rgba(cr, 0.31, 0.89, 0.81, 0.95);
    cairo_fill(cr);
    return FALSE;
}

static gboolean vaxp_anim_tick(gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    data->anim_t += 0.045;
    if (data->header_canvas) gtk_widget_queue_draw(data->header_canvas);
    if (data->thinking_row) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(data->thinking_row));
        if (g_list_length(children) >= 2) {
            gtk_widget_queue_draw(GTK_WIDGET(g_list_nth_data(children, 1)));
        }
        g_list_free(children);
    }
    return TRUE;
}

static void on_ai_window_realize_disable_decorations(GtkWidget *widget, gpointer user_data) {
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (gdk_window) {
        gdk_window_set_decorations(gdk_window, 0);
    }
}

static void on_perm_dialog_realize(GtkWidget *widget, gpointer data) {
    GdkWindow *gdk_window;
    (void)data;
    gdk_window = gtk_widget_get_window(widget);
    if (!gdk_window) return;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_WINDOW(gdk_window)) {
        gdk_wayland_window_announce_csd(gdk_window);
    } else
#endif
    {
        gdk_window_set_decorations(gdk_window, 0);
    }
}

static gboolean show_permission_dialog(GtkWidget *parent_window, const gchar *cmd) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "",
        GTK_WINDOW(parent_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL);
        
    gtk_window_set_decorated(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 480, -1);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_widget_set_name(dialog, "ai-perm-dialog");
    
    g_signal_connect(dialog, "realize", G_CALLBACK(on_perm_dialog_realize), NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_box_set_spacing(GTK_BOX(content_area), 12);
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 20);
    
    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = gtk_image_new_from_icon_name("dialog-warning", GTK_ICON_SIZE_DIALOG);
    GtkWidget *title = gtk_label_new("⚠️ VAI Requires Permission");
    gtk_widget_set_name(title, "ai-perm-title");
    gtk_box_pack_start(GTK_BOX(title_box), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title_box), title, FALSE, FALSE, 0);
    
    GtkWidget *desc = gtk_label_new("The AI is attempting to execute a system command. Do you allow this action?");
    gtk_widget_set_name(desc, "ai-perm-desc");
    gtk_label_set_line_wrap(GTK_LABEL(desc), TRUE);
    gtk_widget_set_halign(desc, GTK_ALIGN_START);
    
    GtkWidget *cmd_lbl = gtk_label_new(cmd);
    gtk_widget_set_name(cmd_lbl, "ai-perm-cmd");
    gtk_label_set_line_wrap(GTK_LABEL(cmd_lbl), TRUE);
    gtk_widget_set_halign(cmd_lbl, GTK_ALIGN_FILL);
    
    gtk_box_pack_start(GTK_BOX(content_area), title_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_area), desc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content_area), cmd_lbl, TRUE, TRUE, 0);
    
    GtkWidget *action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(action_area), 12);
    
    GtkWidget *btn_cancel = gtk_button_new_with_label("إلغاء (Cancel)");
    gtk_widget_set_name(btn_cancel, "ai-perm-btn-cancel");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_cancel, GTK_RESPONSE_CANCEL);
    
    GtkWidget *btn_accept = gtk_button_new_with_label("موافقة (Accept)");
    gtk_widget_set_name(btn_accept, "ai-perm-btn-accept");
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), btn_accept, GTK_RESPONSE_ACCEPT);
    
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    return (response == GTK_RESPONSE_ACCEPT);
}

static void start_typewriter(AiChatData *data);

static void prompt_execute_command(AiChatData *data, const gchar *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    
    gchar *cmd_copy = g_strdup(cmd);
    if (data->extracted_command) {
        g_free(data->extracted_command);
        data->extracted_command = NULL;
    }
    
    if (data->type_timer > 0) {
        g_source_remove(data->type_timer);
        data->type_timer = 0;
    }
    
    gboolean accepted = show_permission_dialog(data->window, cmd_copy);
    
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_widget_set_halign(lbl, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_widget_set_margin_top(lbl, 8);
    gtk_widget_set_margin_bottom(lbl, 8);
    
    if (accepted) {
        gchar *term_cmd = g_strdup_printf("x-terminal-emulator -e bash -c \"%s; echo ''; read -p 'Press Enter to close...'\"", cmd_copy);
        g_spawn_command_line_async(term_cmd, NULL);
        g_free(term_cmd);
        
        gchar *msg = g_strdup_printf("✅ Command Executed: %s", cmd_copy);
        gtk_label_set_text(GTK_LABEL(lbl), msg);
        g_free(msg);
        gtk_widget_override_color(lbl, GTK_STATE_FLAG_NORMAL, &(GdkRGBA){0.3, 0.9, 0.5, 1.0});
    } else {
        gchar *msg = g_strdup_printf("❌ Command Cancelled: %s", cmd_copy);
        gtk_label_set_text(GTK_LABEL(lbl), msg);
        g_free(msg);
        gtk_widget_override_color(lbl, GTK_STATE_FLAG_NORMAL, &(GdkRGBA){0.9, 0.4, 0.4, 1.0});
    }
    
    gtk_box_pack_start(GTK_BOX(data->action_box), lbl, FALSE, FALSE, 0);
    gtk_widget_show_all(data->action_box);
    
    g_free(cmd_copy);
    start_typewriter(data);
};

static gchar *run_bg_command(const gchar *cmd) {
    gchar *std_out = NULL;
    gchar *std_err = NULL;
    gint exit_status = 0;
    
    gchar *full_cmd = g_strdup_printf("bash -c \"%s\"", cmd);
    g_spawn_command_line_sync(full_cmd, &std_out, &std_err, &exit_status, NULL);
    g_free(full_cmd);
    
    gchar *result = NULL;
    if (std_out && strlen(std_out) > 0) {
        result = g_strdup(std_out);
    } else if (std_err && strlen(std_err) > 0) {
        result = g_strdup(std_err);
    } else {
        result = g_strdup("Command executed with no output.");
    }
    
    if (std_out) g_free(std_out);
    if (std_err) g_free(std_err);
    return result;
}

static void auto_scroll(AiChatData *data) {
    if (!data->scroll) return;
    GtkAdjustment *adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(data->scroll));
    gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj));
}

static void add_chat_bubble(AiChatData *data, const gchar *text, gboolean is_user) {
    GtkWidget *align = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    
    if (is_user) {
        GtkWidget *bubble_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_halign(bubble_box, GTK_ALIGN_END);
        gtk_widget_set_margin_start(bubble_box, 60);
        gtk_style_context_add_class(gtk_widget_get_style_context(bubble_box), "user-bubble");
        gtk_widget_set_app_paintable(bubble_box, TRUE);
        g_signal_connect(bubble_box, "draw", G_CALLBACK(vaxp_user_bubble_draw), NULL);

        GtkWidget *label = gtk_label_new(text);
        gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(label), 50);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_widget_set_margin_top(label, 12);
        gtk_widget_set_margin_bottom(label, 12);
        gtk_widget_set_margin_start(label, 16);
        gtk_widget_set_margin_end(label, 16);
        
        gtk_box_pack_start(GTK_BOX(bubble_box), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(align), bubble_box, FALSE, FALSE, 0);
    } else {
        GtkWidget *bubble = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(bubble), FALSE);
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(bubble), GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(bubble), 16);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(bubble), 16);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(bubble), 12);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(bubble), 12);
        gtk_widget_set_name(bubble, "ai-textview");
        
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(bubble));
        gtk_text_buffer_create_tag(buf, "code_block", "family", "Monospace", "background", "#171a20", "foreground", "#7fe8d6", "paragraph-background", "#0f1116", NULL);
        gtk_text_buffer_create_tag(buf, "inline_code", "family", "Monospace", "background", "#1c2028", "foreground", "#f2c14e", NULL);
        gtk_text_buffer_create_tag(buf, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
        gtk_text_buffer_create_tag(buf, "italic", "style", PANGO_STYLE_ITALIC, NULL);
        gtk_text_buffer_create_tag(buf, "h1", "family", "Chakra Petch", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_XX_LARGE, "foreground", "#4fe3cf", NULL);
        gtk_text_buffer_create_tag(buf, "h2", "family", "Chakra Petch", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_X_LARGE, "foreground", "#4fe3cf", NULL);
        gtk_text_buffer_create_tag(buf, "h3", "family", "Chakra Petch", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_LARGE, "foreground", "#4fe3cf", NULL);
        gtk_text_buffer_create_tag(buf, "h4", "family", "Chakra Petch", "weight", PANGO_WEIGHT_BOLD, "scale", PANGO_SCALE_MEDIUM, "foreground", "#4fe3cf", NULL);
        gtk_text_buffer_create_tag(buf, "link", "foreground", "#9c86f5", "underline", PANGO_UNDERLINE_SINGLE, NULL);
        
        gtk_widget_set_halign(bubble, GTK_ALIGN_FILL);
        gtk_widget_set_hexpand(bubble, TRUE);
        gtk_widget_set_margin_end(bubble, 60);
        gtk_style_context_add_class(gtk_widget_get_style_context(bubble), "ai-bubble");
        gtk_widget_set_app_paintable(bubble, TRUE);
        g_signal_connect(bubble, "draw", G_CALLBACK(vaxp_ai_bubble_draw), NULL);
        gtk_box_pack_start(GTK_BOX(align), bubble, FALSE, FALSE, 0);
        
        data->action_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_halign(data->action_box, GTK_ALIGN_START);
        gtk_widget_set_margin_start(data->action_box, 12);
        gtk_box_pack_start(GTK_BOX(align), data->action_box, FALSE, FALSE, 0);
        data->current_ai_textview = bubble;
    }
    
    gtk_box_pack_start(GTK_BOX(data->message_list_box), align, FALSE, FALSE, 8);
    gtk_widget_show_all(data->message_list_box);
    auto_scroll(data);
}

static gboolean vaxp_thinking_dots_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    int h = gtk_widget_get_allocated_height(widget);
    double x = 0, y = h / 2.0;
    for (int i = 0; i < 3; i++) {
        double delay = i * 0.15;
        double sec = fmod(data->anim_t - delay + 1000.0, 1.1);
        double opacity = 0.25, ty = 0.0;
        if (sec < 0.66) {
            double p = sec / 0.66;
            double val = sin(p * G_PI);
            opacity = 0.25 + 0.75 * val;
            ty = -3.0 * val;
        }
        cairo_set_source_rgba(cr, 0.61, 0.53, 0.96, opacity);
        cairo_arc(cr, x + i * 9.0 + 2.5, y + ty, 2.5, 0, 2 * G_PI);
        cairo_fill(cr);
    }
    return FALSE;
}

static void remove_thinking_row(AiChatData *data) {
    if (data->thinking_row) {
        gtk_widget_destroy(data->thinking_row);
        data->thinking_row = NULL;
    }
}

static void add_thinking_row(AiChatData *data, const gchar *label_text) {
    if (data->thinking_row) return;
    
    data->thinking_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(data->thinking_row, 4);
    gtk_widget_set_margin_bottom(data->thinking_row, 4);
    gtk_widget_set_margin_start(data->thinking_row, 4);
    gtk_widget_set_margin_end(data->thinking_row, 4);
    
    GtkWidget *label = gtk_label_new(label_text);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "thinking-label");
    
    GtkWidget *dots = gtk_drawing_area_new();
    gtk_widget_set_size_request(dots, 23, 10);
    gtk_widget_set_valign(dots, GTK_ALIGN_CENTER);
    g_signal_connect(dots, "draw", G_CALLBACK(vaxp_thinking_dots_draw), data);
    
    gtk_box_pack_start(GTK_BOX(data->thinking_row), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(data->thinking_row), dots, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(data->message_list_box), data->thinking_row, FALSE, FALSE, 8);
    gtk_widget_show_all(data->thinking_row);
    auto_scroll(data);
}

static void fetch_response_hidden(AiChatData *data, const gchar *query);

static void auto_scroll(AiChatData *data);

static void on_copy_code_clicked(GtkButton *btn, gpointer user_data) {
    const gchar *code = (const gchar *)user_data;
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, code, -1);
    gtk_button_set_label(btn, "✅ تم النسخ");
}

static void apply_cmark_ast_to_buffer(cmark_node *root, GtkWidget *textview) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev_type;
    
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    gtk_text_buffer_delete(buf, &start, &end);
    
    int list_depth = 0;
    
    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        cmark_node *cur = cmark_iter_get_node(iter);
        cmark_node_type type = cmark_node_get_type(cur);
        
        GtkTextIter text_iter;
        gtk_text_buffer_get_end_iter(buf, &text_iter);
        
        if (ev_type == CMARK_EVENT_ENTER) {
            if (type == CMARK_NODE_EMPH || type == CMARK_NODE_STRONG || type == CMARK_NODE_HEADING || type == CMARK_NODE_LINK) {
                GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &text_iter, TRUE);
                cmark_node_set_user_data(cur, mark);
            }
            
            if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE || type == CMARK_NODE_CODE_BLOCK || type == CMARK_NODE_HTML_INLINE || type == CMARK_NODE_HTML_BLOCK) {
                const char *lit = cmark_node_get_literal(cur);
                if (lit) {
                    GtkTextIter text_iter;
                    gtk_text_buffer_get_end_iter(buf, &text_iter);
                    GtkTextMark *start_mark = gtk_text_buffer_create_mark(buf, NULL, &text_iter, TRUE);
                    
                    gtk_text_buffer_insert(buf, &text_iter, lit, -1);
                    
                    if (type == CMARK_NODE_CODE_BLOCK) {
                        gtk_text_buffer_insert(buf, &text_iter, "\n", -1);
                        GtkTextChildAnchor *anchor = gtk_text_buffer_create_child_anchor(buf, &text_iter);
                        GtkWidget *btn = gtk_button_new_with_label("📋 نسخ الكود");
                        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "copy-code-btn");
                        g_object_set_data_full(G_OBJECT(btn), "code_text", g_strdup(lit), g_free);
                        g_signal_connect(btn, "clicked", G_CALLBACK(on_copy_code_clicked), g_object_get_data(G_OBJECT(btn), "code_text"));
                        gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(textview), btn, anchor);
                        gtk_widget_show_all(btn);
                        gtk_text_buffer_insert(buf, &text_iter, "\n", -1);
                    }
                    
                    GtkTextIter start_tag;
                    gtk_text_buffer_get_iter_at_mark(buf, &start_tag, start_mark);
                    gtk_text_buffer_get_end_iter(buf, &text_iter);
                    
                    if (type == CMARK_NODE_CODE) {
                        gtk_text_buffer_apply_tag_by_name(buf, "inline_code", &start_tag, &text_iter);
                    } else if (type == CMARK_NODE_CODE_BLOCK) {
                        gtk_text_buffer_apply_tag_by_name(buf, "code_block", &start_tag, &text_iter);
                    }
                    
                    gtk_text_buffer_delete_mark(buf, start_mark);
                }
            } else if (type == CMARK_NODE_SOFTBREAK) {
                gtk_text_buffer_insert(buf, &text_iter, " ", -1);
            } else if (type == CMARK_NODE_LINEBREAK) {
                gtk_text_buffer_insert(buf, &text_iter, "\n", -1);
            } else if (type == CMARK_NODE_ITEM) {
                gtk_text_buffer_insert(buf, &text_iter, "• ", -1);
            } else if (type == CMARK_NODE_LIST) {
                list_depth++;
            }
        } else if (ev_type == CMARK_EVENT_EXIT) {
            GtkTextMark *start_mark = (GtkTextMark *)cmark_node_get_user_data(cur);
            if (start_mark) {
                GtkTextIter start_tag;
                gtk_text_buffer_get_iter_at_mark(buf, &start_tag, start_mark);
                gtk_text_buffer_get_end_iter(buf, &text_iter);
                
                if (type == CMARK_NODE_EMPH) {
                    gtk_text_buffer_apply_tag_by_name(buf, "italic", &start_tag, &text_iter);
                } else if (type == CMARK_NODE_STRONG) {
                    gtk_text_buffer_apply_tag_by_name(buf, "bold", &start_tag, &text_iter);
                } else if (type == CMARK_NODE_HEADING) {
                    int level = cmark_node_get_heading_level(cur);
                    if (level > 4) level = 4;
                    gchar *tag_name = g_strdup_printf("h%d", level);
                    gtk_text_buffer_apply_tag_by_name(buf, tag_name, &start_tag, &text_iter);
                    g_free(tag_name);
                } else if (type == CMARK_NODE_LINK) {
                    gtk_text_buffer_apply_tag_by_name(buf, "link", &start_tag, &text_iter);
                }
                
                gtk_text_buffer_delete_mark(buf, start_mark);
            }
            
            if (type == CMARK_NODE_PARAGRAPH || type == CMARK_NODE_HEADING) {
                gtk_text_buffer_insert(buf, &text_iter, "\n\n", -1);
            } else if (type == CMARK_NODE_LIST) {
                list_depth--;
                if (list_depth == 0) gtk_text_buffer_insert(buf, &text_iter, "\n", -1);
            } else if (type == CMARK_NODE_ITEM) {
                gtk_text_buffer_insert(buf, &text_iter, "\n", -1);
            }
        }
    }
    
    gtk_text_buffer_get_end_iter(buf, &end);
    start = end;
    while (gtk_text_iter_backward_char(&start)) {
        if (!g_unichar_isspace(gtk_text_iter_get_char(&start))) {
            gtk_text_iter_forward_char(&start);
            break;
        }
    }
    if (gtk_text_iter_compare(&start, &end) < 0) {
        gtk_text_buffer_delete(buf, &start, &end);
    }
    
    cmark_iter_free(iter);
}

static void render_markdown_final(AiChatData *data) {
    if (!data->response || strlen(data->response) == 0 || !data->current_ai_textview) return;
    
    cmark_gfm_core_extensions_ensure_registered();
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE);
    cmark_syntax_extension *ext = cmark_find_syntax_extension("table");
    if (ext) cmark_parser_attach_syntax_extension(parser, ext);
    ext = cmark_find_syntax_extension("strikethrough");
    if (ext) cmark_parser_attach_syntax_extension(parser, ext);
    ext = cmark_find_syntax_extension("autolink");
    if (ext) cmark_parser_attach_syntax_extension(parser, ext);
    
    cmark_parser_feed(parser, data->response, strlen(data->response));
    cmark_node *doc = cmark_parser_finish(parser);
    
    apply_cmark_ast_to_buffer(doc, data->current_ai_textview);
    
    cmark_node_free(doc);
    cmark_parser_free(parser);
    auto_scroll(data);
}

static gboolean typewriter_tick(gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    
    if (!data->response) return TRUE;
    
    if (data->thinking_row && strlen(data->response) > 0) {
        remove_thinking_row(data);
        add_chat_bubble(data, "", FALSE);
    }
    
    gchar *p = data->response + data->char_index;
    
    if (!p || *p == '\0') {
        if (data->is_done) {
            data->type_timer = 0;
            render_markdown_final(data);
            return FALSE;
        }
        return TRUE;
    }

    if (data->in_execute_block) {
        if (!data->is_done && strlen(p) < 10) return TRUE;
        if (g_str_has_prefix(p, "</execute>")) {
            data->in_execute_block = FALSE;
            data->char_index += 10;
            data->ignore_rest = TRUE;
            p[10] = '\0';
            prompt_execute_command(data, data->extracted_command);
            return FALSE;
        }
        
        gchar *next = g_utf8_next_char(p);
        gint len = next - p;
        gchar *chunk = g_strndup(p, len);
        if (data->extracted_command) {
            gchar *tmp = g_strconcat(data->extracted_command, chunk, NULL);
            g_free(data->extracted_command);
            data->extracted_command = tmp;
        } else {
            data->extracted_command = g_strdup(chunk);
        }
        g_free(chunk);
        data->char_index += len;
        return TRUE;
    }

    if (data->in_bg_execute_block) {
        if (!data->is_done && strlen(p) < 13) return TRUE;
        if (g_str_has_prefix(p, "</bg_execute>")) {
            data->in_bg_execute_block = FALSE;
            data->char_index += 13;
            data->ignore_rest = TRUE;
            p[13] = '\0';
            
            GtkWidget *align = gtk_widget_get_parent(data->current_ai_textview);
            gtk_widget_destroy(align);
            data->current_ai_textview = NULL;
            data->action_box = NULL;
            
            gchar *stdout_txt = run_bg_command(data->extracted_bg_command);
            gchar *hidden_query = g_strdup_printf(
                "[SYSTEM INJECTION] The user asked: '%s'. "
                "To answer, we ran a background command which returned:\n%s\n"
                "Now, provide the final answer to the user based ONLY on this output. "
                "CRITICAL: You MUST answer in the EXACT SAME LANGUAGE the user used in their original question.", 
                data->last_user_query ? data->last_user_query : "", stdout_txt);
            
            fetch_response_hidden(data, hidden_query);
            
            g_free(hidden_query);
            g_free(stdout_txt);
            return FALSE;
        }
        
        gchar *next = g_utf8_next_char(p);
        gint len = next - p;
        gchar *chunk = g_strndup(p, len);
        if (data->extracted_bg_command) {
            gchar *tmp = g_strconcat(data->extracted_bg_command, chunk, NULL);
            g_free(data->extracted_bg_command);
            data->extracted_bg_command = tmp;
        } else {
            data->extracted_bg_command = g_strdup(chunk);
        }
        g_free(chunk);
        data->char_index += len;
        return TRUE;
    }

    if (*p == '<') {
        if (!data->is_done && strlen(p) < 13) return TRUE;
        if (g_str_has_prefix(p, "<execute>")) {
            data->in_execute_block = TRUE;
            data->char_index += 9;
            return TRUE;
        }
        if (g_str_has_prefix(p, "<bg_execute>")) {
            data->in_bg_execute_block = TRUE;
            data->char_index += 12;
            return TRUE;
        }
    }

    if (*p == '`') {
        if (p[1] == '`' && p[2] == '`') {
            data->in_code_block = !data->in_code_block;
            data->char_index += 3;
            if (data->in_code_block) {
                data->skip_until_newline = TRUE;
            }
            return TRUE;
        } else if ((p[1] == '\0' || (p[1] == '`' && p[2] == '\0')) && !data->is_done) {
            return TRUE;
        } else {
            data->in_inline_code = !data->in_inline_code;
            data->char_index += 1;
            return TRUE;
        }
    }
    
    if (*p == '*' && p[1] == '*') {
        data->in_bold = !data->in_bold;
        data->char_index += 2;
        return TRUE;
    } else if (*p == '*' && p[1] == '\0' && !data->is_done) {
        return TRUE;
    }
    
    if (data->skip_until_newline) {
        if (*p == '\n') {
            data->skip_until_newline = FALSE;
            data->char_index += 1;
        } else {
            data->char_index += 1;
        }
        return TRUE;
    }

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->current_ai_textview));
    gchar *next = g_utf8_next_char(p);
    gint len = next - p;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    gtk_text_buffer_insert(buf, &end, p, len);
    
    GtkTextIter start_insert = end;
    gtk_text_iter_backward_chars(&start_insert, 1);
    
    if (data->in_code_block) {
        gtk_text_buffer_apply_tag_by_name(buf, "code_block", &start_insert, &end);
    }
    if (data->in_inline_code) {
        gtk_text_buffer_apply_tag_by_name(buf, "inline_code", &start_insert, &end);
    }
    if (data->in_bold) {
        gtk_text_buffer_apply_tag_by_name(buf, "bold", &start_insert, &end);
    }
    
    data->char_index += len;
    auto_scroll(data);
    
    return TRUE;
}

static void start_typewriter(AiChatData *data) {
    if (data->type_timer == 0) {
        data->type_timer = g_timeout_add(15, typewriter_tick, data);
    }
}

static void on_ai_response_chunk(const gchar *chunk, gboolean is_done, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    
    if (data->ignore_rest) {
        if (is_done) {
            data->is_done = TRUE;
        }
        return;
    }
    
    if (chunk && strlen(chunk) > 0) {
        if (!data->response) {
            data->response = g_strdup(chunk);
        } else {
            gchar *old = data->response;
            data->response = g_strconcat(old, chunk, NULL);
            g_free(old);
        }
    }
    
    gchar *ad_marker = g_strstr_len(data->response, -1, "Support Pollinations.AI:");
    if (ad_marker) {
        data->ignore_rest = TRUE;
        
        gchar *truncate_point = g_strrstr_len(data->response, ad_marker - data->response, "---");
        if (truncate_point) {
            while (truncate_point > data->response && (*(truncate_point - 1) == '\n' || *(truncate_point - 1) == '\r' || *(truncate_point - 1) == ' ')) {
                truncate_point--;
            }
            *truncate_point = '\0';
        } else {
            *ad_marker = '\0';
        }
        
        if (data->current_ai_textview) {
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->current_ai_textview));
            GtkTextIter end_iter, match_start, match_end;
            gtk_text_buffer_get_end_iter(buf, &end_iter);
            if (gtk_text_iter_backward_search(&end_iter, "---", 0, &match_start, &match_end, NULL)) {
                while (gtk_text_iter_backward_char(&match_start)) {
                    gunichar c = gtk_text_iter_get_char(&match_start);
                    if (c != '\n' && c != '\r' && c != ' ') {
                        gtk_text_iter_forward_char(&match_start);
                        break;
                    }
                }
                gtk_text_buffer_delete(buf, &match_start, &end_iter);
            }
        }
        
        if (data->response && data->char_index > (gint)strlen(data->response)) {
            data->char_index = strlen(data->response);
        }
    }
    
    if (is_done) {
        data->is_done = TRUE;
    } else if (data->type_timer == 0 && data->response) {
        start_typewriter(data);
    }
}

static void fetch_response_hidden(AiChatData *data, const gchar *query) {
    if (!query || strlen(query) == 0) return;
    
    if (data->response) { g_free(data->response); data->response = NULL; }
    if (data->type_timer > 0) { g_source_remove(data->type_timer); data->type_timer = 0; }
    
    if (data->extracted_command) { g_free(data->extracted_command); data->extracted_command = NULL; }
    data->in_execute_block = FALSE;
    if (data->extracted_bg_command) { g_free(data->extracted_bg_command); data->extracted_bg_command = NULL; }
    data->in_bg_execute_block = FALSE;
    data->ignore_rest = FALSE;
    data->char_index = 0;
    data->in_code_block = FALSE;
    data->in_inline_code = FALSE;
    data->in_bold = FALSE;
    data->skip_until_newline = FALSE;
    data->is_done = FALSE;
    
    add_thinking_row(data, "VAI يحلل النتائج...");
    
    ai_ctrl_fetch_response(query, on_ai_response_chunk, data);
}

static void fetch_response(AiChatData *data, const gchar *query) {
    if (!query || strlen(query) == 0) return;
    
    if (data->last_user_query) { g_free(data->last_user_query); data->last_user_query = NULL; }
    data->last_user_query = g_strdup(query);
    
    if (data->response) { g_free(data->response); data->response = NULL; }
    if (data->type_timer > 0) { g_source_remove(data->type_timer); data->type_timer = 0; }
    
    if (data->extracted_command) { g_free(data->extracted_command); data->extracted_command = NULL; }
    data->in_execute_block = FALSE;
    if (data->extracted_bg_command) { g_free(data->extracted_bg_command); data->extracted_bg_command = NULL; }
    data->in_bg_execute_block = FALSE;
    data->ignore_rest = FALSE;
    data->char_index = 0;
    data->in_code_block = FALSE;
    data->in_inline_code = FALSE;
    data->in_bold = FALSE;
    data->skip_until_newline = FALSE;
    data->is_done = FALSE;
    
    add_chat_bubble(data, query, TRUE);
    add_thinking_row(data, "VAI يفكّر");
    
    ai_ctrl_fetch_response(query, on_ai_response_chunk, data);
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    const gchar *text = gtk_entry_get_text(entry);
    if (!text || strlen(text) == 0) return;
    
    if (g_str_has_prefix(text, "ai:")) fetch_response(data, text + 3);
    else fetch_response(data, text);
    
    gtk_entry_set_text(entry, "");
}

static void on_send_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AiChatData *data = (AiChatData *)user_data;
    on_entry_activate(GTK_ENTRY(data->entry), data);
}

static gboolean on_ai_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
    (void)user_data;
    if (event->keyval == GDK_KEY_Escape) {
        gtk_widget_destroy(widget);
        return TRUE;
    }
    return FALSE;
}

static void on_window_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    AiChatData *data = (AiChatData *)user_data;
    if (data->type_timer > 0) g_source_remove(data->type_timer);
    if (data->anim_timer > 0) g_source_remove(data->anim_timer);
    ai_ctrl_cleanup(); // Clean up core backend request if any
    if (data->response) g_free(data->response);
    if (data->extracted_command) g_free(data->extracted_command);
    if (data->extracted_bg_command) g_free(data->extracted_bg_command);
    if (data->last_user_query) g_free(data->last_user_query);
    g_free(data);
}

void view_ai_show(const gchar *initial_query) {
    AiChatData *data = g_new0(AiChatData, 1);
    
    static gboolean css_applied = FALSE;
    if (!css_applied) {
        GtkCssProvider *css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css,
            "#ai-window { background: rgba(9, 10, 14, 0.72); border-radius: 20px; border: 1px solid rgba(255,255,255,0.08); box-shadow: 0 24px 60px rgba(0,0,0,0.6); }"
            "#ai-main-box { background: rgba(9, 10, 14, 0.72); }"
            "#ai-header { background: linear-gradient(rgba(255,255,255,0.03), transparent); border-bottom: 1px solid rgba(255,255,255,0.07); border-radius: 20px 20px 0 0; padding: 14px 18px; }"
            ".ai-title { font-family: 'Chakra Petch', 'Tajawal', sans-serif; font-size: 17px; font-weight: 700; letter-spacing: 0.5px; color: #eef1f6; }"
            ".ai-title-accent { color: #4fe3cf; }"
            ".ai-subtitle { font-family: 'Tajawal', sans-serif; font-size: 11.5px; color: #5c6478; }"
            ".ai-beta { background: rgba(79,227,207,0.08); color: #4fe3cf; border: 1px solid rgba(79,227,207,0.35); padding: 4px 12px; border-radius: 3px; font-family: 'Chakra Petch', sans-serif; font-size: 10.5px; font-weight: 600; letter-spacing: 1.5px; }"
            "#ai-entry { background: transparent; border: none; box-shadow: none; padding: 12px 14px; color: #eef1f6; font-family: 'Tajawal', sans-serif; font-size: 14.5px; caret-color: #4fe3cf; }"
            "#ai-entry-wrap { background: transparent; }"
            "#ai-send-btn { background: linear-gradient(135deg, #4fe3cf, #9c86f5); border: none; border-radius: 4px; min-width: 34px; min-height: 34px; padding: 0; }"
            "#ai-send-btn:hover { filter: brightness(1.12); }"
            "#ai-response-scroll { background: rgba(255,255,255,0.015); border-radius: 12px; border: 1px solid rgba(255,255,255,0.06); }"
            "textview.ai-bubble text { background: transparent; }"
            "textview.ai-bubble { background: transparent; font-family: 'Tajawal', sans-serif; font-size: 14.5px; color: #eef1f6; }"
            "box.user-bubble label { font-family: 'Tajawal', sans-serif; color: #eef1f6; font-size: 14.5px; }"
            ".thinking-label { font-family: 'Chakra Petch', sans-serif; font-size: 12px; color: #5c6478; letter-spacing: 0.3px; }"
            "#ai-hint { text-align: center; font-size: 10.5px; color: #5c6478; margin-top: 10px; margin-bottom: 5px; font-family: 'Chakra Petch', sans-serif; letter-spacing: 0.4px; }"
            "#ai-perm-dialog { background: rgba(19, 21, 29, 0.95); border: 1px solid rgba(79, 227, 207, 0.35); border-radius: 16px; box-shadow: 0 10px 30px rgba(0,0,0,0.8); }"
            "#ai-perm-title { color: #4fe3cf; font-family: 'Chakra Petch', sans-serif; font-weight: bold; font-size: 16px; margin-top: 6px; }"
            "#ai-perm-desc { color: #eef1f6; font-family: 'Tajawal', sans-serif; font-size: 14.5px; }"
            "#ai-perm-cmd { color: #eef1f6; font-family: monospace; font-size: 13.5px; background: rgba(0,0,0,0.4); padding: 12px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.05); margin-top: 8px; }"
            "#ai-perm-btn-accept { background: linear-gradient(135deg, #4fe3cf, #2b8f83); color: #07080c; border: none; border-radius: 8px; padding: 8px 24px; font-family: 'Tajawal', sans-serif; font-weight: bold; margin: 10px; }"
            "#ai-perm-btn-accept:hover { background: linear-gradient(135deg, #6aece0, #4fe3cf); }"
            "#ai-perm-btn-cancel { background: rgba(255,255,255,0.05); color: #eef1f6; border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; padding: 8px 24px; font-family: 'Tajawal', sans-serif; margin: 10px; }"
            "#ai-perm-btn-cancel:hover { background: rgba(255,255,255,0.1); border-color: rgba(255,255,255,0.2); }"
            ".copy-code-btn { background: rgba(79, 227, 207, 0.15); color: #4fe3cf; border: 1px solid rgba(79, 227, 207, 0.4); border-radius: 4px; padding: 2px 8px; font-family: 'Tajawal', sans-serif; font-size: 11px; margin-bottom: 4px; }"
            ".copy-code-btn:hover { background: rgba(79, 227, 207, 0.3); }", -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER + 100);
        g_object_unref(css);
        css_applied = TRUE;
    }
    
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_direction(GTK_WIDGET(data->window), GTK_TEXT_DIR_RTL);
    gtk_window_set_title(GTK_WINDOW(data->window), "VAXP AI");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 650, 980);
    gtk_window_set_position(GTK_WINDOW(data->window), GTK_WIN_POS_CENTER);
    gtk_window_set_decorated(GTK_WINDOW(data->window), FALSE);
    g_signal_connect(data->window, "realize", G_CALLBACK(on_ai_window_realize_disable_decorations), NULL);
    gtk_widget_set_app_paintable(data->window, TRUE);
    gtk_widget_set_name(data->window, "ai-window");
    
    GdkScreen *screen = gtk_widget_get_screen(data->window);
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(data->window, visual);
    
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(main_box, "ai-main-box");
    gtk_container_add(GTK_CONTAINER(data->window), main_box);
    
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 13);
    gtk_widget_set_name(header, "ai-header");

    data->header_canvas = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->header_canvas, 34, 34);
    gtk_widget_set_valign(data->header_canvas, GTK_ALIGN_CENTER);
    gtk_widget_set_app_paintable(data->header_canvas, TRUE);
    g_signal_connect(data->header_canvas, "draw", G_CALLBACK(vaxp_header_canvas_draw), data);

    GtkWidget *brand_text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_valign(brand_text, GTK_ALIGN_CENTER);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), "VAXP <span foreground=\"#4fe3cf\">AI</span>");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ai-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);

    GtkWidget *subtitle = gtk_label_new("متصل بنظام VAXP-OS");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "ai-subtitle");
    gtk_widget_set_halign(subtitle, GTK_ALIGN_START);

    gtk_box_pack_start(GTK_BOX(brand_text), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brand_text), subtitle, FALSE, FALSE, 0);

    GtkWidget *beta = gtk_label_new("BETA");
    gtk_style_context_add_class(gtk_widget_get_style_context(beta), "ai-beta");
    gtk_widget_set_valign(beta, GTK_ALIGN_CENTER);

    gtk_box_pack_start(GTK_BOX(header), data->header_canvas, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), brand_text, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), beta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);

    data->anim_timer = g_timeout_add(45, vaxp_anim_tick, data);
    
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_pack_start(GTK_BOX(main_box), content, TRUE, TRUE, 0);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    data->scroll = scroll;
    gtk_widget_set_name(scroll, "ai-response-scroll");
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    data->message_list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(data->message_list_box), 12);
    
    GtkWidget *viewport = gtk_viewport_new(NULL, NULL);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
    gtk_container_add(GTK_CONTAINER(viewport), data->message_list_box);
    gtk_container_add(GTK_CONTAINER(scroll), viewport);
    
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    
    GtkWidget *entry_wrap = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(entry_wrap, "ai-entry-wrap");
    gtk_widget_set_app_paintable(entry_wrap, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(entry_wrap), 5);
    g_signal_connect(entry_wrap, "draw", G_CALLBACK(vaxp_entry_wrap_draw), NULL);

    data->entry = gtk_entry_new();
    gtk_widget_set_name(data->entry, "ai-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->entry), "اسأل VAI عن أي شيء…");
    gtk_widget_set_hexpand(data->entry, TRUE);
    if (initial_query) gtk_entry_set_text(GTK_ENTRY(data->entry), initial_query);
    g_signal_connect(data->entry, "activate", G_CALLBACK(on_entry_activate), data);

    GtkWidget *send_btn = gtk_button_new_from_icon_name("go-next-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_name(send_btn, "ai-send-btn");
    gtk_widget_set_valign(send_btn, GTK_ALIGN_CENTER);
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_send_clicked), data);

    gtk_box_pack_start(GTK_BOX(entry_wrap), data->entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(entry_wrap), send_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), entry_wrap, FALSE, FALSE, 0);
    
    GtkWidget *hint = gtk_label_new("VAI · نموذج تجريبي محلي — قد يخطئ أحياناً");
    gtk_widget_set_name(hint, "ai-hint");
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 0);
    
    g_signal_connect(data->window, "key-press-event", G_CALLBACK(on_ai_key_press), data);
    g_signal_connect(data->window, "destroy", G_CALLBACK(on_window_destroy), data);
    
    gtk_widget_show_all(data->window);
    gtk_widget_grab_focus(data->entry);
    
    if (initial_query && strlen(initial_query) > 0) {
        fetch_response(data, initial_query);
    }
}
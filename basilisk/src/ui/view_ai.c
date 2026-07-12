#include "ui/view_ai.h"
#include "core/ai_ctrl.h"
#include <gtk/gtk.h>
#include <string.h>
#include <gdk/gdkx.h>
#include <gdk/gdkwayland.h>

typedef struct {
    GtkWidget *window;
    GtkWidget *text_view;
    GtkWidget *entry;
    GtkWidget *spinner;
    GtkWidget *status_label;
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
    GtkWidget *action_box;
    GtkTextMark *ai_response_start_mark;
} AiChatData;

static void on_ai_window_realize_disable_decorations(GtkWidget *widget, gpointer user_data) {
    GdkWindow *gdk_window = gtk_widget_get_window(widget);
    if (gdk_window) {
        gdk_window_set_decorations(gdk_window, 0);
    }
}

static void on_execute_clicked(GtkButton *btn, gpointer user_data) {
    const gchar *cmd = (const gchar *)user_data;
    if (cmd) {
        gchar *term_cmd = g_strdup_printf("x-terminal-emulator -e bash -c \"%s; echo ''; read -p 'Press Enter to close...'\"", cmd);
        g_spawn_command_line_async(term_cmd, NULL);
        g_free(term_cmd);
    }
    gtk_widget_destroy(GTK_WIDGET(btn));
}

static void show_execute_button(AiChatData *data, const gchar *cmd) {
    if (!cmd || strlen(cmd) == 0) return;
    gchar *label = g_strdup_printf("🚀 Execute Command: %s", cmd);
    GtkWidget *btn = gtk_button_new_with_label(label);
    g_free(label);
    
    g_object_set_data_full(G_OBJECT(btn), "cmd", g_strdup(cmd), g_free);
    g_signal_connect(btn, "clicked", G_CALLBACK(on_execute_clicked), g_object_get_data(G_OBJECT(btn), "cmd"));
    
    gtk_box_pack_start(GTK_BOX(data->action_box), btn, FALSE, FALSE, 0);
    gtk_widget_show_all(data->action_box);
}

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

static void fetch_response_hidden(AiChatData *data, const gchar *query);

static gboolean typewriter_tick(gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    gchar *p = data->response ? data->response + data->char_index : NULL;
    
    if (!p || *p == '\0') {
        if (data->is_done) {
            data->type_timer = 0;
            gtk_label_set_text(GTK_LABEL(data->status_label), "Done.");
            return FALSE;
        }
        return TRUE;
    }

    if (data->in_execute_block) {
        if (!data->is_done && strlen(p) < 10) return TRUE;
        if (g_str_has_prefix(p, "</execute>")) {
            data->in_execute_block = FALSE;
            data->char_index += 10;
            show_execute_button(data, data->extracted_command);
            return TRUE;
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
            
            GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
            GtkTextIter start_iter, end_iter;
            if (data->ai_response_start_mark) {
                gtk_text_buffer_get_iter_at_mark(buf, &start_iter, data->ai_response_start_mark);
                gtk_text_buffer_get_end_iter(buf, &end_iter);
                gtk_text_buffer_delete(buf, &start_iter, &end_iter);
            }
            
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

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
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
    
    GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(data->text_view), mark, 0, TRUE, 0, 1);
    gtk_text_buffer_delete_mark(buf, mark);
    
    return TRUE;
}

static void start_typewriter(AiChatData *data) {
    if (data->type_timer > 0) g_source_remove(data->type_timer);
    data->char_index = 0;
    data->in_code_block = FALSE;
    data->in_inline_code = FALSE;
    data->in_bold = FALSE;
    data->skip_until_newline = FALSE;
    data->is_done = FALSE;
    gtk_label_set_text(GTK_LABEL(data->status_label), "Typing...");
    data->type_timer = g_timeout_add(10, typewriter_tick, data);
}

static void on_ai_response_chunk(const gchar *chunk, gboolean is_done, gpointer user_data) {
    AiChatData *data = (AiChatData *)user_data;
    if (chunk) {
        if (data->response) {
            gchar *tmp = g_strconcat(data->response, chunk, NULL);
            g_free(data->response);
            data->response = tmp;
        } else {
            data->response = g_strdup(chunk);
        }
    }
    if (is_done) {
        data->is_done = TRUE;
        gtk_spinner_stop(GTK_SPINNER(data->spinner));
        if (!data->response) {
            gtk_label_set_text(GTK_LABEL(data->status_label), "No response.");
        }
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
    
    gtk_label_set_text(GTK_LABEL(data->status_label), "VAI is analyzing results...");
    gtk_spinner_start(GTK_SPINNER(data->spinner));
    
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
    
    GList *children = gtk_container_get_children(GTK_CONTAINER(data->action_box));
    for (GList *iter = children; iter != NULL; iter = g_list_next(iter)) {
        gtk_widget_destroy(GTK_WIDGET(iter->data));
    }
    g_list_free(children);
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);
    
    if (gtk_text_iter_get_offset(&end) > 0) {
        gtk_text_buffer_insert(buf, &end, "\n\n", -1);
    }
    
    gtk_text_buffer_insert_with_tags_by_name(buf, &end, "VAXP Client:\n", -1, "user_name", NULL);
    gtk_text_buffer_insert_with_tags_by_name(buf, &end, query, -1, "user_msg", NULL);
    
    gtk_text_buffer_insert(buf, &end, "\n\n", -1);
    gtk_text_buffer_insert_with_tags_by_name(buf, &end, "VAXP AI:\n", -1, "ai_name", NULL);
    
    if (data->ai_response_start_mark) {
        gtk_text_buffer_delete_mark(buf, data->ai_response_start_mark);
    }
    data->ai_response_start_mark = gtk_text_buffer_create_mark(buf, NULL, &end, TRUE);
    
    GtkTextMark *mark = gtk_text_buffer_create_mark(buf, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(data->text_view), mark, 0, TRUE, 0, 1);
    gtk_text_buffer_delete_mark(buf, mark);
    
    gtk_label_set_text(GTK_LABEL(data->status_label), "VAI is thinking...");
    gtk_spinner_start(GTK_SPINNER(data->spinner));
    
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
            "#ai-window { background: rgba(0, 0, 0, 0.300); border-radius: 16px; border: 2px solid rgba(0, 0, 0, 1.0); box-shadow: 0 10px 40px rgba(0, 0, 0, 0.5); }"
            "#ai-main-box { background: rgba(0, 0, 0, 0.300); }"
            "#ai-header { background: rgba(0, 0, 0, 0); border-radius: 12px 12px 0 0; padding: 12px 16px; }"
            ".ai-title { font-size: 22px; font-weight: bold; color: #ffffff; }"
            ".ai-beta { background: rgba(0, 0, 0, 0.44); color: #ffffff; padding: 4px 12px; border-radius: 12px; font-size: 11px; font-weight: bold; }"
            "#ai-entry { background: rgba(255, 255, 255, 0.08); border: 1px solid rgba(0, 0, 0, 1); border-radius: 10px; padding: 14px 16px; color: #ffffff; font-size: 15px; caret-color: rgba(0, 0, 0, 0); }"
            "#ai-entry:focus { border-color: rgba(0, 0, 0, 0); background: rgba(255, 255, 255, 0.12); }"
            "#ai-response-scroll { background: rgba(0, 0, 0, 0.3); border-radius: 10px; border: 1px solid rgba(255, 255, 255, 0.1); }"
            "#ai-textview { background: transparent; font-size: 15px; color: #e0e0e0; }"
            "#ai-textview text { background: transparent; }"
            "#ai-footer { padding: 8px 0; }"
            "#ai-status { color: #888888; font-size: 12px; }", -1, NULL);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_USER + 100);
        g_object_unref(css);
        css_applied = TRUE;
    }
    
    data->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(data->window), "VAXP AI");
    gtk_window_set_default_size(GTK_WINDOW(data->window), 650, 480);
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
    
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(header, "ai-header");
    GtkWidget *title = gtk_label_new("VAXP AI");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "ai-title");
    GtkWidget *beta = gtk_label_new("BETA");
    gtk_style_context_add_class(gtk_widget_get_style_context(beta), "ai-beta");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), beta, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);
    
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_pack_start(GTK_BOX(main_box), content, TRUE, TRUE, 0);
    
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_name(scroll, "ai-response-scroll");
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    data->text_view = gtk_text_view_new();
    gtk_widget_set_name(data->text_view, "ai-textview");
    
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(data->text_view));
    gtk_text_buffer_create_tag(buf, "code_block", 
                               "family", "Monospace", 
                               "background", "#2b2b2b", 
                               "foreground", "#a9b7c6", 
                               "paragraph-background", "#1e1e1e", 
                               NULL);
    gtk_text_buffer_create_tag(buf, "inline_code", 
                               "family", "Monospace", 
                               "background", "#3c3c3c", 
                               "foreground", "#f8c555", 
                               NULL);
    gtk_text_buffer_create_tag(buf, "bold", 
                               "weight", PANGO_WEIGHT_BOLD, 
                               NULL);
    gtk_text_buffer_create_tag(buf, "user_name", 
                               "weight", PANGO_WEIGHT_BOLD, 
                               "foreground", "#4da6ff", 
                               NULL);
    gtk_text_buffer_create_tag(buf, "ai_name", 
                               "weight", PANGO_WEIGHT_BOLD, 
                               "foreground", "#ff4da6", 
                               NULL);
    gtk_text_buffer_create_tag(buf, "user_msg", 
                               "foreground", "#ffffff", 
                               NULL);
                               
    gtk_text_view_set_editable(GTK_TEXT_VIEW(data->text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(data->text_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(data->text_view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(data->text_view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(data->text_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(data->text_view), 12);
    gtk_container_add(GTK_CONTAINER(scroll), data->text_view);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    
    data->action_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_box_pack_start(GTK_BOX(content), data->action_box, FALSE, FALSE, 0);
    
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(footer, "ai-footer");
    data->spinner = gtk_spinner_new();
    data->status_label = gtk_label_new("Press Enter to ask Vaxp AI...");
    gtk_widget_set_name(data->status_label, "ai-status");
    gtk_box_pack_start(GTK_BOX(footer), data->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(footer), data->status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), footer, FALSE, FALSE, 0);
    
    data->entry = gtk_entry_new();
    gtk_widget_set_name(data->entry, "ai-entry");
    gtk_entry_set_placeholder_text(GTK_ENTRY(data->entry), "Ask Vaxp AI anything...");
    if (initial_query) gtk_entry_set_text(GTK_ENTRY(data->entry), initial_query);
    g_signal_connect(data->entry, "activate", G_CALLBACK(on_entry_activate), data);
    gtk_box_pack_start(GTK_BOX(content), data->entry, FALSE, FALSE, 0);
    
    g_signal_connect(data->window, "key-press-event", G_CALLBACK(on_ai_key_press), data);
    g_signal_connect(data->window, "destroy", G_CALLBACK(on_window_destroy), data);
    
    gtk_widget_show_all(data->window);
    gtk_widget_grab_focus(data->entry);
    
    if (initial_query && strlen(initial_query) > 0) {
        fetch_response(data, initial_query);
    }
}

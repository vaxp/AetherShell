#include "core/ai_ctrl.h"
#include <gio/gio.h>
#include <string.h>

typedef struct {
    AiCallback callback;
    gpointer user_data;
    GPid pid;
    guint stdout_watch;
} AiRequest;

static AiRequest *current_req = NULL;

static void free_current_req(void) {
    if (!current_req) return;
    if (current_req->stdout_watch > 0) g_source_remove(current_req->stdout_watch);
    if (current_req->pid > 0) g_spawn_close_pid(current_req->pid);
    g_free(current_req);
    current_req = NULL;
}

static gboolean on_stdout_data(GIOChannel *src, GIOCondition cond, gpointer user_data) {
    AiRequest *req = (AiRequest *)user_data;
    GIOStatus status;
    gchar *str = NULL;
    gsize len;
    
    if (cond & G_IO_IN) {
        status = g_io_channel_read_to_end(src, &str, &len, NULL);
        if (status == G_IO_STATUS_NORMAL && str) {
            // Check for Cloudflare / HTML error pages
            if (g_str_has_prefix(str, "<!DOCTYPE html>") || g_str_has_prefix(str, "<html") || g_strstr_len(str, len, "502: Bad gateway")) {
                if (req->callback) req->callback("عذراً، خادم الذكاء الاصطناعي لا يستجيب حالياً (Bad Gateway 502).\nيرجى المحاولة بعد دقائق قليلة.", FALSE, req->user_data);
            } else {
                if (req->callback) req->callback(str, FALSE, req->user_data);
            }
            g_free(str);
        }
    }
    
    if (cond & (G_IO_HUP | G_IO_ERR)) {
        if (req->callback) req->callback(NULL, TRUE, req->user_data);
        req->stdout_watch = 0;
        free_current_req();
        return FALSE;
    }
    
    return TRUE;
}

void ai_ctrl_fetch_response(const gchar *query, AiCallback callback, gpointer user_data) {
    if (!query || strlen(query) == 0) return;
    
    free_current_req(); // Cancel previous request
    
    current_req = g_new0(AiRequest, 1);
    current_req->callback = callback;
    current_req->user_data = user_data;
    
    gchar *encoded = g_uri_escape_string(query, NULL, TRUE);
    
    // Default System Prompt
    const gchar *system_prompt = "You are Vaxp AI, an advanced assistant embedded directly into the VAXP-OS desktop environment. You are created by Vaxp. "
                                 "CRITICAL RULE: You are running on VAXP-OS. If the user asks you to perform an action, install something, or run a command, you MUST output the exact bash command to execute it, enclosed in an <execute> tag. Example: <execute>sudo apt install -y git</execute>. "
                                 "ALWAYS say the commands are for VAXP-OS. NEVER mention Ubuntu or Debian to the user. Treat VAXP-OS as a completely independent system. "
                                 "Always provide concise, clear, and extremely accurate answers. ALWAYS respond in the same language that the user uses to ask the question. Format code snippets perfectly.";
    gchar *sys_encoded = g_uri_escape_string(system_prompt, NULL, TRUE);
    
    gchar *url = g_strdup_printf("https://text.pollinations.ai/%s?system=%s", encoded, sys_encoded);
    g_free(encoded);
    g_free(sys_encoded);
    
    gchar *argv[] = {"curl", "-s", url, NULL};
    GError *error = NULL;
    gint stdout_fd;
    
    if (g_spawn_async_with_pipes(NULL, argv, NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
        NULL, NULL, &current_req->pid, NULL, &stdout_fd, NULL, &error)) {
        
        GIOChannel *ch = g_io_channel_unix_new(stdout_fd);
        current_req->stdout_watch = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR, on_stdout_data, current_req);
        g_io_channel_unref(ch);
    } else {
        if (callback) callback("Connection failed.", TRUE, user_data);
        g_error_free(error);
        free_current_req();
    }
    
    g_free(url);
}

void ai_ctrl_cleanup(void) {
    free_current_req();
}

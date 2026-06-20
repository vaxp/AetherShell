#include <gtk/gtk.h>
#include <glib-unix.h>

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

#include "config.h"
#include "settings.hpp"

struct AppWidgets
{
    GtkApplication *app = nullptr;
    GtkWindow *window = nullptr;
    GtkButton *start_button = nullptr;
    GtkButton *stop_button = nullptr;
    GtkButton *browse_button = nullptr;
    GtkButton *refresh_outputs_button = nullptr;
    GtkButton *slurp_button = nullptr;
    GtkButton *save_config_button = nullptr;

    GtkEntry *file_entry = nullptr;
    GtkCheckButton *auto_filename_check = nullptr;
    GtkEntry *output_dir_entry = nullptr;
    GtkEntry *filename_template_entry = nullptr;
    GtkComboBoxText *output_combo = nullptr;
    GtkEntry *geometry_entry = nullptr;
    GtkCheckButton *overwrite_check = nullptr;
    GtkCheckButton *log_check = nullptr;

    GtkEntry *codec_entry = nullptr;
    GtkEntry *muxer_entry = nullptr;
    GtkEntry *pixel_format_entry = nullptr;
    GtkEntry *filter_entry = nullptr;
    GtkSpinButton *framerate_spin = nullptr;
    GtkSpinButton *bframes_spin = nullptr;
    GtkSpinButton *buffrate_spin = nullptr;
    GtkEntry *device_entry = nullptr;
    GtkCheckButton *no_dmabuf_check = nullptr;
    GtkCheckButton *no_damage_check = nullptr;
    GtkTextView *video_params_view = nullptr;

    GtkCheckButton *audio_check = nullptr;
    GtkEntry *audio_device_entry = nullptr;
    GtkComboBoxText *audio_backend_combo = nullptr;
    GtkEntry *audio_codec_entry = nullptr;
    GtkSpinButton *sample_rate_spin = nullptr;
    GtkEntry *sample_format_entry = nullptr;
    GtkTextView *audio_params_view = nullptr;

    GtkTextView *command_view = nullptr;
    GtkTextView *log_view = nullptr;
    GtkLabel *status_label = nullptr;

    GPid child_pid = 0;
    guint stdout_watch_id = 0;
    guint stderr_watch_id = 0;
    gint stdout_fd = -1;
    gint stderr_fd = -1;
    bool close_after_stop = false;
    std::string recorder_path;
};

static std::string get_entry_text(GtkEntry *entry)
{
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    return text ? text : "";
}

static std::string get_buffer_text(GtkTextView *view)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    std::string value = text ? text : "";
    g_free(text);
    return value;
}

static void set_buffer_text(GtkTextView *view, const std::string& text)
{
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(view), text.c_str(), -1);
}

static std::string join_lines(const std::vector<std::string>& lines)
{
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i)
        {
            result += '\n';
        }

        result += lines[i];
    }

    return result;
}

static void append_log(AppWidgets *widgets, const std::string& text)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(widgets->log_view);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text.c_str(), -1);

    GtkTextMark *mark = gtk_text_buffer_get_insert(buffer);
    gtk_text_view_scroll_mark_onscreen(widgets->log_view, mark);
}

static void set_status(AppWidgets *widgets, const std::string& text)
{
    gtk_label_set_text(widgets->status_label, text.c_str());
}

static std::string shell_quote(const std::string& value)
{
    if (value.empty())
    {
        return "''";
    }

    bool needs_quote = false;
    for (char ch : value)
    {
        if (g_ascii_isspace(ch) || ch == '\'' || ch == '"' || ch == '$' || ch == '\\')
        {
            needs_quote = true;
            break;
        }
    }

    if (!needs_quote)
    {
        return value;
    }

    std::string quoted = "'";
    for (char ch : value)
    {
        if (ch == '\'')
        {
            quoted += "'\\''";
        } else
        {
            quoted.push_back(ch);
        }
    }

    quoted += "'";
    return quoted;
}

static std::vector<std::string> split_params(const std::string& raw)
{
    std::vector<std::string> params;
    std::stringstream stream(raw);
    std::string line;
    while (std::getline(stream, line))
    {
        line.erase(std::remove_if(line.begin(), line.end(), [](unsigned char c)
        {
            return c == '\r';
        }), line.end());

        if (line.empty())
        {
            continue;
        }

        size_t start = 0;
        while (start < line.size())
        {
            size_t comma = line.find(',', start);
            std::string part = line.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            size_t left = part.find_first_not_of(" \t");
            size_t right = part.find_last_not_of(" \t");
            if (left != std::string::npos)
            {
                params.push_back(part.substr(left, right - left + 1));
            }

            if (comma == std::string::npos)
            {
                break;
            }

            start = comma + 1;
        }
    }

    return params;
}

static std::string executable_dir()
{
    gchar *path = g_file_read_link("/proc/self/exe", nullptr);
    if (!path)
    {
        return "";
    }

    gchar *dir = g_path_get_dirname(path);
    std::string result = dir ? dir : "";
    g_free(dir);
    g_free(path);
    return result;
}

static std::string resolve_recorder_binary()
{
    std::string dir = executable_dir();
    if (!dir.empty())
    {
        std::string sibling = dir + "/aether-recorder";
        if (g_file_test(sibling.c_str(), G_FILE_TEST_IS_EXECUTABLE))
        {
            return sibling;
        }
    }

    return "aether-recorder";
}

static std::vector<std::string> build_command(AppWidgets *widgets)
{
    std::vector<std::string> cmd;
    cmd.push_back(widgets->recorder_path);

    if (!gtk_check_button_get_active(widgets->auto_filename_check))
    {
        const std::string file = get_entry_text(widgets->file_entry);
        if (!file.empty())
        {
            cmd.push_back("-f");
            cmd.push_back(file);
        }
    }

    const gchar *output = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->output_combo));
    if (output && *output)
    {
        cmd.push_back("-o");
        cmd.push_back(output);
    }

    const std::string geometry = get_entry_text(widgets->geometry_entry);
    if (!geometry.empty())
    {
        cmd.push_back("-g");
        cmd.push_back(geometry);
    }

    const std::string codec = get_entry_text(widgets->codec_entry);
    if (!codec.empty())
    {
        cmd.push_back("-c");
        cmd.push_back(codec);
    }

    const std::string muxer = get_entry_text(widgets->muxer_entry);
    if (!muxer.empty())
    {
        cmd.push_back("-m");
        cmd.push_back(muxer);
    }

    const std::string pix_fmt = get_entry_text(widgets->pixel_format_entry);
    if (!pix_fmt.empty())
    {
        cmd.push_back("-x");
        cmd.push_back(pix_fmt);
    }

    const std::string filter = get_entry_text(widgets->filter_entry);
    if (!filter.empty())
    {
        cmd.push_back("-F");
        cmd.push_back(filter);
    }

    const int framerate = gtk_spin_button_get_value_as_int(widgets->framerate_spin);
    if (framerate > 0)
    {
        cmd.push_back("-r");
        cmd.push_back(std::to_string(framerate));
    }

    const int bframes = gtk_spin_button_get_value_as_int(widgets->bframes_spin);
    if (bframes >= 0)
    {
        cmd.push_back("-b");
        cmd.push_back(std::to_string(bframes));
    }

    const int buffrate = gtk_spin_button_get_value_as_int(widgets->buffrate_spin);
    if (buffrate > 0)
    {
        cmd.push_back("-B");
        cmd.push_back(std::to_string(buffrate));
    }

    const std::string device = get_entry_text(widgets->device_entry);
    if (!device.empty())
    {
        cmd.push_back("-d");
        cmd.push_back(device);
    }

    if (gtk_check_button_get_active(widgets->no_dmabuf_check))
    {
        cmd.push_back("--no-dmabuf");
    }

    if (gtk_check_button_get_active(widgets->no_damage_check))
    {
        cmd.push_back("-D");
    }

    if (gtk_check_button_get_active(widgets->overwrite_check))
    {
        cmd.push_back("-y");
    }

    if (gtk_check_button_get_active(widgets->log_check))
    {
        cmd.push_back("-l");
    }

    for (const auto& param : split_params(get_buffer_text(widgets->video_params_view)))
    {
        cmd.push_back("-p");
        cmd.push_back(param);
    }

    if (gtk_check_button_get_active(widgets->audio_check))
    {
        const std::string audio_device = get_entry_text(widgets->audio_device_entry);
        if (audio_device.empty())
        {
            cmd.push_back("-a");
        } else
        {
            cmd.push_back("--audio=" + audio_device);
        }

        const gchar *backend = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->audio_backend_combo));
        if (backend && *backend && g_strcmp0(backend, "auto") != 0)
        {
            cmd.push_back("--audio-backend");
            cmd.push_back(backend);
        }

        const std::string audio_codec = get_entry_text(widgets->audio_codec_entry);
        if (!audio_codec.empty())
        {
            cmd.push_back("-C");
            cmd.push_back(audio_codec);
        }

        const int sample_rate = gtk_spin_button_get_value_as_int(widgets->sample_rate_spin);
        if (sample_rate > 0)
        {
            cmd.push_back("-R");
            cmd.push_back(std::to_string(sample_rate));
        }

        const std::string sample_fmt = get_entry_text(widgets->sample_format_entry);
        if (!sample_fmt.empty())
        {
            cmd.push_back("-X");
            cmd.push_back(sample_fmt);
        }

        for (const auto& param : split_params(get_buffer_text(widgets->audio_params_view)))
        {
            cmd.push_back("-P");
            cmd.push_back(param);
        }
    }

    return cmd;
}

static void update_command_preview(AppWidgets *widgets)
{
    std::vector<std::string> cmd = build_command(widgets);
    std::string preview;
    for (size_t i = 0; i < cmd.size(); ++i)
    {
        if (i)
        {
            preview += ' ';
        }

        preview += shell_quote(cmd[i]);
    }

    set_buffer_text(widgets->command_view, preview);
}

static void on_any_value_changed(GtkWidget*, gpointer data)
{
    update_command_preview(static_cast<AppWidgets*>(data));
}

static RecorderConfig build_config_from_widgets(AppWidgets *widgets)
{
    RecorderConfig config;
    config.file = get_entry_text(widgets->file_entry);
    config.auto_filename = gtk_check_button_get_active(widgets->auto_filename_check);
    config.output_dir = get_entry_text(widgets->output_dir_entry);
    config.filename_template = get_entry_text(widgets->filename_template_entry);

    const gchar *output = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->output_combo));
    config.output = output ? output : "";

    config.geometry = get_entry_text(widgets->geometry_entry);
    config.overwrite = gtk_check_button_get_active(widgets->overwrite_check);
    config.log = gtk_check_button_get_active(widgets->log_check);

    config.codec = get_entry_text(widgets->codec_entry);
    config.muxer = get_entry_text(widgets->muxer_entry);
    config.pixel_format = get_entry_text(widgets->pixel_format_entry);
    config.filter = get_entry_text(widgets->filter_entry);
    config.framerate = gtk_spin_button_get_value_as_int(widgets->framerate_spin);
    config.bframes = gtk_spin_button_get_value_as_int(widgets->bframes_spin);
    config.buffrate = gtk_spin_button_get_value_as_int(widgets->buffrate_spin);
    config.device = get_entry_text(widgets->device_entry);
    config.no_dmabuf = gtk_check_button_get_active(widgets->no_dmabuf_check);
    config.no_damage = gtk_check_button_get_active(widgets->no_damage_check);
    config.video_params = split_params(get_buffer_text(widgets->video_params_view));

    config.audio_enabled = gtk_check_button_get_active(widgets->audio_check);
    config.audio_device = get_entry_text(widgets->audio_device_entry);

    const gchar *audio_backend = gtk_combo_box_get_active_id(GTK_COMBO_BOX(widgets->audio_backend_combo));
    config.audio_backend = audio_backend ? audio_backend : "auto";

    config.audio_codec = get_entry_text(widgets->audio_codec_entry);
    config.sample_rate = gtk_spin_button_get_value_as_int(widgets->sample_rate_spin);
    config.sample_format = get_entry_text(widgets->sample_format_entry);
    config.audio_params = split_params(get_buffer_text(widgets->audio_params_view));
    return config;
}

static void apply_config_to_widgets(AppWidgets *widgets, const RecorderConfig& config)
{
    if (config.file)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->file_entry), config.file->c_str());
    }

    if (config.auto_filename)
    {
        gtk_check_button_set_active(widgets->auto_filename_check, *config.auto_filename);
    }

    if (config.output_dir)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->output_dir_entry), config.output_dir->c_str());
    }

    if (config.filename_template)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->filename_template_entry), config.filename_template->c_str());
    }

    if (config.output)
    {
        if (config.output->empty())
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->output_combo), 0);
        } else
        {
            gtk_combo_box_set_active_id(GTK_COMBO_BOX(widgets->output_combo), config.output->c_str());
        }
    }

    if (config.geometry)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->geometry_entry), config.geometry->c_str());
    }

    if (config.overwrite)
    {
        gtk_check_button_set_active(widgets->overwrite_check, *config.overwrite);
    }

    if (config.log)
    {
        gtk_check_button_set_active(widgets->log_check, *config.log);
    }

    if (config.codec)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->codec_entry), config.codec->c_str());
    }

    if (config.muxer)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->muxer_entry), config.muxer->c_str());
    }

    if (config.pixel_format)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->pixel_format_entry), config.pixel_format->c_str());
    }

    if (config.filter)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->filter_entry), config.filter->c_str());
    }

    if (config.framerate)
    {
        gtk_spin_button_set_value(widgets->framerate_spin, *config.framerate);
    }

    if (config.bframes)
    {
        gtk_spin_button_set_value(widgets->bframes_spin, *config.bframes);
    }

    if (config.buffrate)
    {
        gtk_spin_button_set_value(widgets->buffrate_spin, *config.buffrate);
    }

    if (config.device)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->device_entry), config.device->c_str());
    }

    if (config.no_dmabuf)
    {
        gtk_check_button_set_active(widgets->no_dmabuf_check, *config.no_dmabuf);
    }

    if (config.no_damage)
    {
        gtk_check_button_set_active(widgets->no_damage_check, *config.no_damage);
    }

    set_buffer_text(widgets->video_params_view, join_lines(config.video_params));

    if (config.audio_enabled)
    {
        gtk_check_button_set_active(widgets->audio_check, *config.audio_enabled);
    }

    if (config.audio_device)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->audio_device_entry), config.audio_device->c_str());
    }

    if (config.audio_backend && !config.audio_backend->empty())
    {
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(widgets->audio_backend_combo), config.audio_backend->c_str());
    }

    if (config.audio_codec)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->audio_codec_entry), config.audio_codec->c_str());
    }

    if (config.sample_rate)
    {
        gtk_spin_button_set_value(widgets->sample_rate_spin, *config.sample_rate);
    }

    if (config.sample_format)
    {
        gtk_editable_set_text(GTK_EDITABLE(widgets->sample_format_entry), config.sample_format->c_str());
    }

    set_buffer_text(widgets->audio_params_view, join_lines(config.audio_params));
}

static void save_config(AppWidgets *widgets)
{
    RecorderConfig config = build_config_from_widgets(widgets);
    std::string error;
    if (!save_recorder_config(config, &error))
    {
        set_status(widgets, "Could not save config: " + error);
        return;
    }

    set_status(widgets, "Config saved to " + recorder_config_path());
}

static void on_save_config_clicked(GtkButton*, gpointer data)
{
    save_config(static_cast<AppWidgets*>(data));
}

static void set_recording_state(AppWidgets *widgets, bool running)
{
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->start_button), !running);
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->stop_button), running);
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->browse_button), !running);
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->refresh_outputs_button), !running);
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->slurp_button), !running);
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->save_config_button), !running);
}

static gboolean on_process_fd_ready(gint fd, GIOCondition condition, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    (void)widgets;
    (void)condition;

    char buffer[4096];
    const ssize_t bytes = read(fd, buffer, sizeof(buffer));
    if (bytes > 0)
    {
        append_log(widgets, std::string(buffer, bytes));
        return G_SOURCE_CONTINUE;
    }

    return G_SOURCE_REMOVE;
}

static void clear_watch(guint& watch_id)
{
    if (watch_id != 0)
    {
        g_source_remove(watch_id);
        watch_id = 0;
    }
}

static void close_fd(gint& fd)
{
    if (fd >= 0)
    {
        close(fd);
        fd = -1;
    }
}

static void on_child_exit(GPid pid, gint status, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);

    clear_watch(widgets->stdout_watch_id);
    clear_watch(widgets->stderr_watch_id);
    close_fd(widgets->stdout_fd);
    close_fd(widgets->stderr_fd);
    g_spawn_close_pid(pid);
    widgets->child_pid = 0;
    set_recording_state(widgets, false);

    if (g_spawn_check_wait_status(status, nullptr))
    {
        set_status(widgets, "Recording stopped.");
    } else
    {
        set_status(widgets, "aether-recorder exited with an error. Check the session log.");
    }

    if (widgets->close_after_stop)
    {
        gtk_window_destroy(widgets->window);
    }
}

static void start_recording(AppWidgets *widgets)
{
    if (widgets->child_pid != 0)
    {
        return;
    }

    std::string config_error;
    if (!save_recorder_config(build_config_from_widgets(widgets), &config_error))
    {
        set_status(widgets, "Could not save config: " + config_error);
        return;
    }

    std::vector<std::string> command = build_command(widgets);
    if (command.empty())
    {
        set_status(widgets, "Could not build the aether-recorder command.");
        return;
    }

    GtkTextBuffer *log_buffer = gtk_text_view_get_buffer(widgets->log_view);
    gtk_text_buffer_set_text(log_buffer, "", -1);

    std::vector<gchar*> argv;
    argv.reserve(command.size() + 1);
    for (const auto& part : command)
    {
        argv.push_back(g_strdup(part.c_str()));
    }
    argv.push_back(nullptr);

    gint stdout_fd = -1;
    gint stderr_fd = -1;
    GError *error = nullptr;

    gboolean spawned = g_spawn_async_with_pipes(
        nullptr,
        argv.data(),
        nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
        nullptr,
        nullptr,
        &widgets->child_pid,
        nullptr,
        &stdout_fd,
        &stderr_fd,
        &error);

    for (gchar *part : argv)
    {
        g_free(part);
    }

    if (!spawned)
    {
        const std::string message = error ? error->message : "Unable to launch aether-recorder.";
        set_status(widgets, message);
        if (error)
        {
            g_error_free(error);
        }
        return;
    }

    widgets->stdout_watch_id = g_unix_fd_add(stdout_fd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), on_process_fd_ready, widgets);
    widgets->stderr_watch_id = g_unix_fd_add(stderr_fd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR), on_process_fd_ready, widgets);
    widgets->stdout_fd = stdout_fd;
    widgets->stderr_fd = stderr_fd;
    g_child_watch_add(widgets->child_pid, on_child_exit, widgets);

    set_recording_state(widgets, true);
    set_status(widgets, "Recording in progress...");
    append_log(widgets, "Launching: ");
    append_log(widgets, get_buffer_text(widgets->command_view));
    append_log(widgets, "\n\n");
}

static void stop_recording(AppWidgets *widgets)
{
    if (widgets->child_pid == 0)
    {
        return;
    }

    kill(widgets->child_pid, SIGINT);
    set_status(widgets, "Stopping recording...");
}

static std::vector<std::pair<std::string, std::string>> parse_output_list(const std::string& stdout_text)
{
    std::vector<std::pair<std::string, std::string>> outputs;
    std::stringstream stream(stdout_text);
    std::string line;
    while (std::getline(stream, line))
    {
        const std::string name_marker = "Name: ";
        const std::string desc_marker = " Description: ";
        size_t name_pos = line.find(name_marker);
        size_t desc_pos = line.find(desc_marker);
        if (name_pos == std::string::npos || desc_pos == std::string::npos || desc_pos <= name_pos + name_marker.size())
        {
            continue;
        }

        std::string name = line.substr(name_pos + name_marker.size(), desc_pos - (name_pos + name_marker.size()));
        std::string description = line.substr(desc_pos + desc_marker.size());
        outputs.emplace_back(name, description);
    }

    return outputs;
}

static void refresh_outputs(AppWidgets *widgets)
{
    gchar *argv[] = {
        g_strdup(widgets->recorder_path.c_str()),
        g_strdup("-L"),
        nullptr,
    };

    gchar *stdout_data = nullptr;
    gchar *stderr_data = nullptr;
    gint status = 0;
    GError *error = nullptr;

    gboolean ok = g_spawn_sync(
        nullptr,
        argv,
        nullptr,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        &stdout_data,
        &stderr_data,
        &status,
        &error);

    g_free(argv[0]);
    g_free(argv[1]);

    gtk_combo_box_text_remove_all(widgets->output_combo);
    gtk_combo_box_text_append(widgets->output_combo, "", "Auto / interactive");
    gtk_combo_box_set_active(GTK_COMBO_BOX(widgets->output_combo), 0);

    if (!ok)
    {
        set_status(widgets, error ? error->message : "Could not query outputs.");
        if (error)
        {
            g_error_free(error);
        }
        return;
    }

    if (!g_spawn_check_wait_status(status, nullptr))
    {
        set_status(widgets, stderr_data && *stderr_data ? stderr_data : "aether-recorder could not list outputs.");
        g_free(stdout_data);
        g_free(stderr_data);
        return;
    }

    for (const auto& output : parse_output_list(stdout_data ? stdout_data : ""))
    {
        const std::string label = output.first + "  |  " + output.second;
        gtk_combo_box_text_append(widgets->output_combo, output.first.c_str(), label.c_str());
    }

    set_status(widgets, "Outputs refreshed.");
    g_free(stdout_data);
    g_free(stderr_data);
    update_command_preview(widgets);
}

static void on_refresh_outputs_clicked(GtkButton*, gpointer data)
{
    refresh_outputs(static_cast<AppWidgets*>(data));
}

static void on_start_clicked(GtkButton*, gpointer data)
{
    start_recording(static_cast<AppWidgets*>(data));
}

static void on_stop_clicked(GtkButton*, gpointer data)
{
    stop_recording(static_cast<AppWidgets*>(data));
}

static void on_file_dialog_response(GtkNativeDialog *dialog, gint response, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    if (response == GTK_RESPONSE_ACCEPT)
    {
        GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
        GFile *file = gtk_file_chooser_get_file(chooser);
        if (file)
        {
            gchar *path = g_file_get_path(file);
            if (path)
            {
                gtk_editable_set_text(GTK_EDITABLE(widgets->file_entry), path);
                g_free(path);
            }
            g_object_unref(file);
        }
    }

    g_object_unref(dialog);
    update_command_preview(widgets);
}

static void on_browse_clicked(GtkButton*, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    GtkFileChooserNative *dialog = gtk_file_chooser_native_new(
        "Select output file",
        widgets->window,
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "Use this file",
        "Cancel");

    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    g_signal_connect(dialog, "response", G_CALLBACK(on_file_dialog_response), widgets);
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(dialog));
}

static void on_slurp_ready(GObject *source_object, GAsyncResult *result, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    GSubprocess *process = G_SUBPROCESS(source_object);
    gchar *stdout_data = nullptr;
    gchar *stderr_data = nullptr;
    GError *error = nullptr;

    gboolean ok = g_subprocess_communicate_utf8_finish(process, result, &stdout_data, &stderr_data, &error);
    if (!ok)
    {
        set_status(widgets, error ? error->message : "slurp failed.");
        if (error)
        {
            g_error_free(error);
        }
        return;
    }

    if (g_subprocess_get_successful(process))
    {
        std::string geometry = stdout_data ? stdout_data : "";
        geometry.erase(std::remove(geometry.begin(), geometry.end(), '\n'), geometry.end());
        gtk_editable_set_text(GTK_EDITABLE(widgets->geometry_entry), geometry.c_str());
        set_status(widgets, geometry.empty() ? "slurp returned an empty region." : "Geometry captured from slurp.");
        update_command_preview(widgets);
    } else
    {
        set_status(widgets, stderr_data && *stderr_data ? stderr_data : "slurp did not return a region.");
    }

    g_free(stdout_data);
    g_free(stderr_data);
}

static void on_slurp_clicked(GtkButton*, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    GError *error = nullptr;
    GSubprocess *process = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error,
        "slurp",
        nullptr);

    if (!process)
    {
        set_status(widgets, error ? error->message : "Could not launch slurp.");
        if (error)
        {
            g_error_free(error);
        }
        return;
    }

    set_status(widgets, "Select a region with slurp...");
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr, on_slurp_ready, widgets);
    g_object_unref(process);
}

static gboolean on_close_request(GtkWindow*, gpointer data)
{
    AppWidgets *widgets = static_cast<AppWidgets*>(data);
    if (widgets->child_pid == 0)
    {
        return FALSE;
    }

    widgets->close_after_stop = true;
    stop_recording(widgets);
    return TRUE;
}

static GtkWidget* make_labeled_row(const char *title, GtkWidget *field, GtkWidget *extra = nullptr)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(row, TRUE);
    gtk_widget_set_halign(row, GTK_ALIGN_FILL);
    GtkWidget *label = gtk_label_new(title);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_size_request(label, 165, -1);
    gtk_box_append(GTK_BOX(row), label);
    gtk_widget_set_hexpand(field, TRUE);
    gtk_widget_set_halign(field, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(row), field);
    if (extra)
    {
        gtk_box_append(GTK_BOX(row), extra);
    }
    return row;
}

static GtkWidget* make_text_area(GtkTextView **out_view, int min_height)
{
    GtkWidget *scroller = gtk_scrolled_window_new();
    gtk_widget_set_size_request(scroller, -1, min_height);
    gtk_widget_set_hexpand(scroller, TRUE);
    gtk_widget_set_halign(scroller, GTK_ALIGN_FILL);
    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), view);
    *out_view = GTK_TEXT_VIEW(view);
    return scroller;
}

static GtkWidget* make_section(const char *title, const char *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_halign(box, GTK_ALIGN_FILL);
    GtkWidget *heading = gtk_label_new(nullptr);
    std::string markup = std::string("<span weight=\"700\" size=\"large\">") + title + "</span>";
    gtk_label_set_markup(GTK_LABEL(heading), markup.c_str());
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0f);
    GtkWidget *sub = gtk_label_new(subtitle);
    gtk_label_set_xalign(GTK_LABEL(sub), 0.0f);
    gtk_widget_add_css_class(sub, "dim-label");
    gtk_box_append(GTK_BOX(box), heading);
    gtk_box_append(GTK_BOX(box), sub);
    gtk_widget_add_css_class(box, "card");
    return box;
}

static void connect_updates(AppWidgets *widgets, GtkWidget *widget)
{
    if (GTK_IS_EDITABLE(widget))
    {
        g_signal_connect(widget, "changed", G_CALLBACK(on_any_value_changed), widgets);
    } else if (GTK_IS_CHECK_BUTTON(widget))
    {
        g_signal_connect(widget, "toggled", G_CALLBACK(on_any_value_changed), widgets);
    } else if (GTK_IS_SPIN_BUTTON(widget))
    {
        g_signal_connect(widget, "value-changed", G_CALLBACK(on_any_value_changed), widgets);
    } else if (GTK_IS_COMBO_BOX(widget))
    {
        g_signal_connect(widget, "changed", G_CALLBACK(on_any_value_changed), widgets);
    } else if (GTK_IS_TEXT_VIEW(widget))
    {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
        g_signal_connect(buffer, "changed", G_CALLBACK(on_any_value_changed), widgets);
    }
}

static void load_css()
{
    static const char *css = R"(
window {
    background: rgba(0, 0, 0, 0.392);
    color: #ffffff;
}

.card {
    background: rgba(0, 0, 0, 0.392);
    border-radius: 18px;
    padding: 18px;
    color: #ffffff;
}

label, textview, textview text, entry, spinbutton, combobox, combobox box, button, checkbutton {
    font-size: 14px;
    color: #ffffff;
}

.dim-label {
    color: #ffffff;
}

entry, spinbutton, textview, textview text, combobox box, button {
    background: rgba(0, 0, 0, 0.392);
    color: #ffffff;
    border-color: rgba(255, 255, 255, 0.35);
}

button.suggested-action {
    background: rgba(255, 255, 255, 0.14);
    color: #ffffff;
}
)";

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, css);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

static void activate(GtkApplication *app, gpointer)
{
    load_css();

    auto *widgets = new AppWidgets();
    widgets->app = app;
    widgets->recorder_path = resolve_recorder_binary();

    widgets->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(widgets->window, "aether-recorder Control Center");
    gtk_window_set_default_size(widgets->window, 1120, 880);
    g_signal_connect(widgets->window, "close-request", G_CALLBACK(on_close_request), widgets);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_hexpand(root, TRUE);
    gtk_widget_set_vexpand(root, TRUE);
    gtk_widget_set_halign(root, GTK_ALIGN_FILL);
    gtk_widget_set_valign(root, GTK_ALIGN_FILL);
    gtk_widget_set_margin_top(root, 20);
    gtk_widget_set_margin_bottom(root, 20);
    gtk_widget_set_margin_start(root, 20);
    gtk_widget_set_margin_end(root, 20);

    GtkWidget *hero = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_hexpand(hero, TRUE);
    gtk_widget_set_halign(hero, GTK_ALIGN_FILL);
    GtkWidget *title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title), "<span weight=\"800\" size=\"xx-large\">aether-recorder Control Center</span>");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    GtkWidget *subtitle = gtk_label_new("Build precise capture commands, launch recording, and monitor the live session from one place.");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_box_append(GTK_BOX(hero), title);
    gtk_box_append(GTK_BOX(hero), subtitle);
    gtk_box_append(GTK_BOX(root), hero);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_widget_set_halign(scroll, GTK_ALIGN_FILL);
    gtk_widget_set_valign(scroll, GTK_ALIGN_FILL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(root), scroll);

    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_hexpand(content, TRUE);
    gtk_widget_set_halign(content, GTK_ALIGN_FILL);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content);

    GtkWidget *general = make_section("General Capture", "File target, output selection, region picking, and recording behavior.");
    GtkWidget *general_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(general_box, TRUE);
    gtk_widget_set_halign(general_box, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(general), general_box);

    widgets->file_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->file_entry), "recording." DEFAULT_CONTAINER_FORMAT);
    widgets->browse_button = GTK_BUTTON(gtk_button_new_with_label("Browse"));
    gtk_box_append(GTK_BOX(general_box), make_labeled_row("Output file", GTK_WIDGET(widgets->file_entry), GTK_WIDGET(widgets->browse_button)));

    widgets->auto_filename_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Use date and time filename"));
    gtk_check_button_set_active(widgets->auto_filename_check, TRUE);
    gtk_box_append(GTK_BOX(general_box), GTK_WIDGET(widgets->auto_filename_check));

    widgets->output_dir_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->output_dir_entry), default_recording_directory().c_str());
    gtk_box_append(GTK_BOX(general_box), make_labeled_row("Output directory", GTK_WIDGET(widgets->output_dir_entry)));

    widgets->filename_template_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->filename_template_entry), "Recording_%Y-%m-%d_%H-%M-%S." DEFAULT_CONTAINER_FORMAT);
    gtk_box_append(GTK_BOX(general_box), make_labeled_row("Filename template", GTK_WIDGET(widgets->filename_template_entry)));

    widgets->output_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    widgets->refresh_outputs_button = GTK_BUTTON(gtk_button_new_with_label("Refresh"));
    gtk_box_append(GTK_BOX(general_box), make_labeled_row("Target output", GTK_WIDGET(widgets->output_combo), GTK_WIDGET(widgets->refresh_outputs_button)));

    widgets->geometry_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(widgets->geometry_entry, "x,y WxH");
    widgets->slurp_button = GTK_BUTTON(gtk_button_new_with_label("Pick with slurp"));
    gtk_box_append(GTK_BOX(general_box), make_labeled_row("Geometry", GTK_WIDGET(widgets->geometry_entry), GTK_WIDGET(widgets->slurp_button)));

    GtkWidget *general_toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(general_toggles, TRUE);
    gtk_widget_set_halign(general_toggles, GTK_ALIGN_FILL);
    widgets->overwrite_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Overwrite existing file"));
    widgets->log_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Enable ffmpeg log output"));
    gtk_box_append(GTK_BOX(general_toggles), GTK_WIDGET(widgets->overwrite_check));
    gtk_box_append(GTK_BOX(general_toggles), GTK_WIDGET(widgets->log_check));
    gtk_box_append(GTK_BOX(general_box), general_toggles);
    gtk_box_append(GTK_BOX(content), general);

    GtkWidget *video = make_section("Video Encoding", "Codec, muxer, buffer strategy, and advanced ffmpeg options.");
    GtkWidget *video_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(video_box, TRUE);
    gtk_widget_set_halign(video_box, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(video), video_box);

    widgets->codec_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->codec_entry), DEFAULT_CODEC);
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Video codec", GTK_WIDGET(widgets->codec_entry)));

    widgets->muxer_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Muxer override", GTK_WIDGET(widgets->muxer_entry)));

    widgets->pixel_format_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->pixel_format_entry), DEFAULT_PIX_FMT);
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Pixel format", GTK_WIDGET(widgets->pixel_format_entry)));

    widgets->filter_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("FFmpeg filter", GTK_WIDGET(widgets->filter_entry)));

    widgets->framerate_spin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 240, 1));
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Force framerate", GTK_WIDGET(widgets->framerate_spin)));

    widgets->bframes_spin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(-1, 32, 1));
    gtk_spin_button_set_value(widgets->bframes_spin, -1);
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Max B-frames", GTK_WIDGET(widgets->bframes_spin)));

    widgets->buffrate_spin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(0, 240, 1));
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Expected buffer FPS", GTK_WIDGET(widgets->buffrate_spin)));

    widgets->device_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(widgets->device_entry, "/dev/dri/renderD128");
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Hardware device", GTK_WIDGET(widgets->device_entry)));

    GtkWidget *video_toggles = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_hexpand(video_toggles, TRUE);
    gtk_widget_set_halign(video_toggles, GTK_ALIGN_FILL);
    widgets->no_dmabuf_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Disable DMA-BUF"));
    widgets->no_damage_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Disable damage tracking"));
    gtk_box_append(GTK_BOX(video_toggles), GTK_WIDGET(widgets->no_dmabuf_check));
    gtk_box_append(GTK_BOX(video_toggles), GTK_WIDGET(widgets->no_damage_check));
    gtk_box_append(GTK_BOX(video_box), video_toggles);

    GtkWidget *video_params = make_text_area(&widgets->video_params_view, 110);
    gtk_box_append(GTK_BOX(video_box), make_labeled_row("Codec params", video_params));
    gtk_box_append(GTK_BOX(content), video);

    GtkWidget *audio = make_section("Audio", "Optional source capture, backend choice, sample tuning, and audio encoder params.");
    GtkWidget *audio_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(audio_box, TRUE);
    gtk_widget_set_halign(audio_box, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(audio), audio_box);

    widgets->audio_check = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Enable audio capture"));
    gtk_box_append(GTK_BOX(audio_box), GTK_WIDGET(widgets->audio_check));

    widgets->audio_device_entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(widgets->audio_device_entry, "Optional source name, for example alsa_output.pci-0000_00_1f.3.monitor");
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Audio source", GTK_WIDGET(widgets->audio_device_entry)));

    widgets->audio_backend_combo = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(widgets->audio_backend_combo, "auto", "Auto");
    gtk_combo_box_text_append(widgets->audio_backend_combo, "pulse", "PulseAudio");
    gtk_combo_box_text_append(widgets->audio_backend_combo, "pipewire", "PipeWire");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(widgets->audio_backend_combo), DEFAULT_AUDIO_BACKEND);
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Audio backend", GTK_WIDGET(widgets->audio_backend_combo)));

    widgets->audio_codec_entry = GTK_ENTRY(gtk_entry_new());
    gtk_editable_set_text(GTK_EDITABLE(widgets->audio_codec_entry), DEFAULT_AUDIO_CODEC);
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Audio codec", GTK_WIDGET(widgets->audio_codec_entry)));

    widgets->sample_rate_spin = GTK_SPIN_BUTTON(gtk_spin_button_new_with_range(8000, 384000, 1000));
    gtk_spin_button_set_value(widgets->sample_rate_spin, DEFAULT_AUDIO_SAMPLE_RATE);
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Sample rate", GTK_WIDGET(widgets->sample_rate_spin)));

    widgets->sample_format_entry = GTK_ENTRY(gtk_entry_new());
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Sample format", GTK_WIDGET(widgets->sample_format_entry)));

    GtkWidget *audio_params = make_text_area(&widgets->audio_params_view, 110);
    gtk_box_append(GTK_BOX(audio_box), make_labeled_row("Audio codec params", audio_params));
    gtk_box_append(GTK_BOX(content), audio);

    GtkWidget *session = make_section("Session", "Review the generated command, then watch the live recorder output.");
    GtkWidget *session_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_hexpand(session_box, TRUE);
    gtk_widget_set_halign(session_box, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(session), session_box);

    GtkWidget *command_view = make_text_area(&widgets->command_view, 90);
    gtk_text_view_set_editable(widgets->command_view, FALSE);
    gtk_box_append(GTK_BOX(session_box), make_labeled_row("Generated command", command_view));

    GtkWidget *log_view = make_text_area(&widgets->log_view, 260);
    gtk_text_view_set_editable(widgets->log_view, FALSE);
    gtk_box_append(GTK_BOX(session_box), make_labeled_row("Session log", log_view));

    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(footer, TRUE);
    gtk_widget_set_halign(footer, GTK_ALIGN_FILL);
    widgets->status_label = GTK_LABEL(gtk_label_new("Ready."));
    gtk_label_set_xalign(widgets->status_label, 0.0f);
    gtk_widget_set_hexpand(GTK_WIDGET(widgets->status_label), TRUE);
    widgets->save_config_button = GTK_BUTTON(gtk_button_new_with_label("Save Config"));
    widgets->start_button = GTK_BUTTON(gtk_button_new_with_label("Start Recording"));
    widgets->stop_button = GTK_BUTTON(gtk_button_new_with_label("Stop"));
    gtk_widget_add_css_class(GTK_WIDGET(widgets->start_button), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(widgets->stop_button), FALSE);
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(widgets->status_label));
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(widgets->save_config_button));
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(widgets->stop_button));
    gtk_box_append(GTK_BOX(footer), GTK_WIDGET(widgets->start_button));
    gtk_box_append(GTK_BOX(session_box), footer);
    gtk_box_append(GTK_BOX(content), session);

    for (GtkWidget *widget : {
        GTK_WIDGET(widgets->file_entry),
        GTK_WIDGET(widgets->auto_filename_check),
        GTK_WIDGET(widgets->output_dir_entry),
        GTK_WIDGET(widgets->filename_template_entry),
        GTK_WIDGET(widgets->output_combo),
        GTK_WIDGET(widgets->geometry_entry),
        GTK_WIDGET(widgets->overwrite_check),
        GTK_WIDGET(widgets->log_check),
        GTK_WIDGET(widgets->codec_entry),
        GTK_WIDGET(widgets->muxer_entry),
        GTK_WIDGET(widgets->pixel_format_entry),
        GTK_WIDGET(widgets->filter_entry),
        GTK_WIDGET(widgets->framerate_spin),
        GTK_WIDGET(widgets->bframes_spin),
        GTK_WIDGET(widgets->buffrate_spin),
        GTK_WIDGET(widgets->device_entry),
        GTK_WIDGET(widgets->no_dmabuf_check),
        GTK_WIDGET(widgets->no_damage_check),
        GTK_WIDGET(widgets->video_params_view),
        GTK_WIDGET(widgets->audio_check),
        GTK_WIDGET(widgets->audio_device_entry),
        GTK_WIDGET(widgets->audio_backend_combo),
        GTK_WIDGET(widgets->audio_codec_entry),
        GTK_WIDGET(widgets->sample_rate_spin),
        GTK_WIDGET(widgets->sample_format_entry),
        GTK_WIDGET(widgets->audio_params_view),
    })
    {
        connect_updates(widgets, widget);
    }

    g_signal_connect(widgets->browse_button, "clicked", G_CALLBACK(on_browse_clicked), widgets);
    g_signal_connect(widgets->refresh_outputs_button, "clicked", G_CALLBACK(on_refresh_outputs_clicked), widgets);
    g_signal_connect(widgets->slurp_button, "clicked", G_CALLBACK(on_slurp_clicked), widgets);
    g_signal_connect(widgets->save_config_button, "clicked", G_CALLBACK(on_save_config_clicked), widgets);
    g_signal_connect(widgets->start_button, "clicked", G_CALLBACK(on_start_clicked), widgets);
    g_signal_connect(widgets->stop_button, "clicked", G_CALLBACK(on_stop_clicked), widgets);

    gtk_window_set_child(widgets->window, root);
    refresh_outputs(widgets);

    RecorderConfig config;
    std::string config_error;
    if (load_recorder_config(config, &config_error))
    {
        apply_config_to_widgets(widgets, config);
    } else
    {
        set_status(widgets, "Could not load config: " + config_error);
    }

    update_command_preview(widgets);
    gtk_window_present(widgets->window);
}

int main(int argc, char **argv)
{
    GtkApplication *app = gtk_application_new("org.wfrecorder.controlcenter", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

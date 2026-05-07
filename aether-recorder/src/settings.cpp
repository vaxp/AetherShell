#include "settings.hpp"

#include "config.h"

#include <gio/gio.h>
#include <glib.h>

#include <string>

static constexpr const char *CONFIG_DIR_NAME = "aether-recorder";
static constexpr const char *CONFIG_FILE_NAME = "config.ini";

std::string recorder_config_path()
{
    gchar *dir = g_build_filename(g_get_user_config_dir(), CONFIG_DIR_NAME, nullptr);
    gchar *path = g_build_filename(dir, CONFIG_FILE_NAME, nullptr);
    std::string result = path ? path : "";
    g_free(path);
    g_free(dir);
    return result;
}

std::string default_recording_directory()
{
    const gchar *videos_dir = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    if (videos_dir && *videos_dir)
    {
        return videos_dir;
    }

    return g_get_home_dir();
}

static std::string extension_from_file(const std::string& file)
{
    const size_t slash = file.find_last_of('/');
    const size_t dot = file.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash) &&
        dot + 1 < file.size())
    {
        return file.substr(dot + 1);
    }

    return DEFAULT_CONTAINER_FORMAT;
}

std::string generate_recording_filename(const RecorderConfig& config, const std::string& fallback_file)
{
    std::string filename_template = config.filename_template.value_or("");
    if (filename_template.empty())
    {
        filename_template = "Recording_%Y-%m-%d_%H-%M-%S." + extension_from_file(fallback_file);
    }

    GDateTime *now = g_date_time_new_now_local();
    gchar *formatted = g_date_time_format(now, filename_template.c_str());
    std::string filename = formatted ? formatted : filename_template;
    g_free(formatted);
    g_date_time_unref(now);

    if (g_path_is_absolute(filename.c_str()))
    {
        return filename;
    }

    const std::string output_dir = config.output_dir && !config.output_dir->empty() ?
        *config.output_dir : default_recording_directory();
    g_mkdir_with_parents(output_dir.c_str(), 0755);

    gchar *path = g_build_filename(output_dir.c_str(), filename.c_str(), nullptr);
    std::string result = path ? path : filename;
    g_free(path);

    if (!g_file_test(result.c_str(), G_FILE_TEST_EXISTS))
    {
        return result;
    }

    const size_t slash = result.find_last_of('/');
    const size_t dot = result.find_last_of('.');
    const bool has_extension = dot != std::string::npos &&
        (slash == std::string::npos || dot > slash);
    const std::string base = has_extension ? result.substr(0, dot) : result;
    const std::string extension = has_extension ? result.substr(dot) : "";

    for (int i = 1; i < 1000; ++i)
    {
        const std::string candidate = base + "-" + std::to_string(i) + extension;
        if (!g_file_test(candidate.c_str(), G_FILE_TEST_EXISTS))
        {
            return candidate;
        }
    }

    return result;
}

static bool has_key(GKeyFile *key_file, const char *group, const char *key)
{
    return g_key_file_has_key(key_file, group, key, nullptr);
}

static std::optional<std::string> get_string(GKeyFile *key_file, const char *group, const char *key)
{
    if (!has_key(key_file, group, key))
    {
        return std::nullopt;
    }

    GError *error = nullptr;
    gchar *value = g_key_file_get_string(key_file, group, key, &error);
    if (error)
    {
        g_error_free(error);
        return std::nullopt;
    }

    std::string result = value ? value : "";
    g_free(value);
    return result;
}

static std::optional<int> get_integer(GKeyFile *key_file, const char *group, const char *key)
{
    if (!has_key(key_file, group, key))
    {
        return std::nullopt;
    }

    GError *error = nullptr;
    int value = g_key_file_get_integer(key_file, group, key, &error);
    if (error)
    {
        g_error_free(error);
        return std::nullopt;
    }

    return value;
}

static std::optional<bool> get_boolean(GKeyFile *key_file, const char *group, const char *key)
{
    if (!has_key(key_file, group, key))
    {
        return std::nullopt;
    }

    GError *error = nullptr;
    gboolean value = g_key_file_get_boolean(key_file, group, key, &error);
    if (error)
    {
        g_error_free(error);
        return std::nullopt;
    }

    return value;
}

static std::vector<std::string> get_string_list(GKeyFile *key_file, const char *group, const char *key)
{
    std::vector<std::string> result;
    if (!has_key(key_file, group, key))
    {
        return result;
    }

    GError *error = nullptr;
    gsize length = 0;
    gchar **items = g_key_file_get_string_list(key_file, group, key, &length, &error);
    if (error)
    {
        g_error_free(error);
        return result;
    }

    for (gsize i = 0; i < length; ++i)
    {
        result.emplace_back(items[i] ? items[i] : "");
    }

    g_strfreev(items);
    return result;
}

static void set_optional_string(GKeyFile *key_file, const char *group, const char *key,
    const std::optional<std::string>& value)
{
    if (value)
    {
        g_key_file_set_string(key_file, group, key, value->c_str());
    }
}

static void set_optional_integer(GKeyFile *key_file, const char *group, const char *key,
    const std::optional<int>& value)
{
    if (value)
    {
        g_key_file_set_integer(key_file, group, key, *value);
    }
}

static void set_optional_boolean(GKeyFile *key_file, const char *group, const char *key,
    const std::optional<bool>& value)
{
    if (value)
    {
        g_key_file_set_boolean(key_file, group, key, *value);
    }
}

static void set_string_list(GKeyFile *key_file, const char *group, const char *key,
    const std::vector<std::string>& values)
{
    std::vector<const gchar*> raw;
    raw.reserve(values.size());
    for (const auto& value : values)
    {
        raw.push_back(value.c_str());
    }

    g_key_file_set_string_list(key_file, group, key, raw.data(), raw.size());
}

bool load_recorder_config(RecorderConfig& config, std::string *error_message)
{
    const std::string path = recorder_config_path();
    if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
    {
        return true;
    }

    GKeyFile *key_file = g_key_file_new();
    GError *error = nullptr;
    if (!g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE, &error))
    {
        if (error_message)
        {
            *error_message = error ? error->message : "Failed to load recorder config.";
        }

        if (error)
        {
            g_error_free(error);
        }

        g_key_file_unref(key_file);
        return false;
    }

    config.file = get_string(key_file, "general", "file");
    config.output_dir = get_string(key_file, "general", "output_dir");
    config.filename_template = get_string(key_file, "general", "filename_template");
    config.output = get_string(key_file, "general", "output");
    config.geometry = get_string(key_file, "general", "geometry");
    config.overwrite = get_boolean(key_file, "general", "overwrite");
    config.log = get_boolean(key_file, "general", "log");
    config.auto_filename = get_boolean(key_file, "general", "auto_filename");

    config.codec = get_string(key_file, "video", "codec");
    config.muxer = get_string(key_file, "video", "muxer");
    config.pixel_format = get_string(key_file, "video", "pixel_format");
    config.filter = get_string(key_file, "video", "filter");
    config.framerate = get_integer(key_file, "video", "framerate");
    config.bframes = get_integer(key_file, "video", "bframes");
    config.buffrate = get_integer(key_file, "video", "buffrate");
    config.device = get_string(key_file, "video", "device");
    config.no_dmabuf = get_boolean(key_file, "video", "no_dmabuf");
    config.no_damage = get_boolean(key_file, "video", "no_damage");
    config.video_params = get_string_list(key_file, "video", "params");

    config.audio_enabled = get_boolean(key_file, "audio", "enabled");
    config.audio_device = get_string(key_file, "audio", "device");
    config.audio_backend = get_string(key_file, "audio", "backend");
    config.audio_codec = get_string(key_file, "audio", "codec");
    config.sample_rate = get_integer(key_file, "audio", "sample_rate");
    config.sample_format = get_string(key_file, "audio", "sample_format");
    config.audio_params = get_string_list(key_file, "audio", "params");

    g_key_file_unref(key_file);
    return true;
}

bool save_recorder_config(const RecorderConfig& config, std::string *error_message)
{
    GKeyFile *key_file = g_key_file_new();

    set_optional_string(key_file, "general", "file", config.file);
    set_optional_string(key_file, "general", "output_dir", config.output_dir);
    set_optional_string(key_file, "general", "filename_template", config.filename_template);
    set_optional_string(key_file, "general", "output", config.output);
    set_optional_string(key_file, "general", "geometry", config.geometry);
    set_optional_boolean(key_file, "general", "overwrite", config.overwrite);
    set_optional_boolean(key_file, "general", "log", config.log);
    set_optional_boolean(key_file, "general", "auto_filename", config.auto_filename);

    set_optional_string(key_file, "video", "codec", config.codec);
    set_optional_string(key_file, "video", "muxer", config.muxer);
    set_optional_string(key_file, "video", "pixel_format", config.pixel_format);
    set_optional_string(key_file, "video", "filter", config.filter);
    set_optional_integer(key_file, "video", "framerate", config.framerate);
    set_optional_integer(key_file, "video", "bframes", config.bframes);
    set_optional_integer(key_file, "video", "buffrate", config.buffrate);
    set_optional_string(key_file, "video", "device", config.device);
    set_optional_boolean(key_file, "video", "no_dmabuf", config.no_dmabuf);
    set_optional_boolean(key_file, "video", "no_damage", config.no_damage);
    set_string_list(key_file, "video", "params", config.video_params);

    set_optional_boolean(key_file, "audio", "enabled", config.audio_enabled);
    set_optional_string(key_file, "audio", "device", config.audio_device);
    set_optional_string(key_file, "audio", "backend", config.audio_backend);
    set_optional_string(key_file, "audio", "codec", config.audio_codec);
    set_optional_integer(key_file, "audio", "sample_rate", config.sample_rate);
    set_optional_string(key_file, "audio", "sample_format", config.sample_format);
    set_string_list(key_file, "audio", "params", config.audio_params);

    const std::string path = recorder_config_path();
    gchar *dir = g_path_get_dirname(path.c_str());
    GError *error = nullptr;
    if (g_mkdir_with_parents(dir, 0755) != 0)
    {
        if (error_message)
        {
            *error_message = "Failed to create config directory.";
        }

        g_free(dir);
        g_key_file_unref(key_file);
        return false;
    }
    g_free(dir);

    gboolean ok = g_key_file_save_to_file(key_file, path.c_str(), &error);
    if (!ok && error_message)
    {
        *error_message = error ? error->message : "Failed to save recorder config.";
    }

    if (error)
    {
        g_error_free(error);
    }

    g_key_file_unref(key_file);
    return ok;
}

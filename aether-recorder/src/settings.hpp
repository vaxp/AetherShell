#pragma once

#include <optional>
#include <string>
#include <vector>

struct RecorderConfig
{
    std::optional<std::string> file;
    std::optional<std::string> output_dir;
    std::optional<std::string> filename_template;
    std::optional<std::string> output;
    std::optional<std::string> geometry;
    std::optional<std::string> muxer;
    std::optional<std::string> codec;
    std::optional<std::string> pixel_format;
    std::optional<std::string> filter;
    std::optional<std::string> device;
    std::optional<std::string> audio_backend;
    std::optional<std::string> audio_device;
    std::optional<std::string> audio_codec;
    std::optional<std::string> sample_format;

    std::optional<int> framerate;
    std::optional<int> bframes;
    std::optional<int> buffrate;
    std::optional<int> sample_rate;

    std::optional<bool> overwrite;
    std::optional<bool> log;
    std::optional<bool> auto_filename;
    std::optional<bool> audio_enabled;
    std::optional<bool> no_dmabuf;
    std::optional<bool> no_damage;

    std::vector<std::string> video_params;
    std::vector<std::string> audio_params;
};

std::string recorder_config_path();
std::string default_recording_directory();
std::string generate_recording_filename(const RecorderConfig& config, const std::string& fallback_file);
bool load_recorder_config(RecorderConfig& config, std::string *error_message = nullptr);
bool save_recorder_config(const RecorderConfig& config, std::string *error_message = nullptr);

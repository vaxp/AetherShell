#pragma once

#include <functional>
#include <memory>

class RecordingTray
{
  public:
    explicit RecordingTray(std::function<void()> stop_callback);
    ~RecordingTray();

    RecordingTray(const RecordingTray&) = delete;
    RecordingTray& operator=(const RecordingTray&) = delete;

    void set_recording(bool recording);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

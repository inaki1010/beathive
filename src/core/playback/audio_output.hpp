#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/model/error.hpp"
#include "core/playback/audio_format.hpp"

namespace bh::playback {

struct AudioDeviceInfo {
  std::string id;  // opaque; empty string = system default
  std::string name;
  bool is_default = false;
};

// An audio device that pulls interleaved float32 samples from a callback.
// The callback runs on the device's real-time thread: it must not block or allocate.
class IAudioOutput {
 public:
  using RenderFn = std::function<void(float* out, std::size_t frames)>;
  virtual ~IAudioOutput() = default;

  // Opens (but does not start) the device. The returned format is what render() is asked for.
  virtual Result<AudioFormat> open(const std::string& device_id, RenderFn render) = 0;
  virtual Result<void> start() = 0;
  virtual void stop() = 0;
  virtual void close() = 0;
  virtual std::vector<AudioDeviceInfo> devices() = 0;
};

// miniaudio-backed output (WASAPI / PulseAudio / PipeWire / ALSA ...).
// `null_backend` plays into a real-time-paced void: no sound, useful for headless runs.
std::unique_ptr<IAudioOutput> make_miniaudio_output(bool null_backend = false);

}  // namespace bh::playback

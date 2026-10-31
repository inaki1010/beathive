#pragma once
#include <array>
#include <chrono>
#include <string>

#include "core/model/queue.hpp"

namespace bh {

enum class ReplayGainMode { Off, Track, Album };

struct EqualizerSettings {
  static constexpr std::size_t kBands = 10;  // 31 Hz ... 16 kHz
  bool enabled = false;
  std::array<float, kBands> gains_db{};  // each clamped to [-12, +12] by the engine
  float preamp_db = 0.0f;
  std::string preset = "Flat";
};

struct PlaybackSettings {
  std::string output_device;  // empty = system default
  float volume = 1.0f;        // 0..1 linear
  bool muted = false;
  bool gapless = true;
  std::chrono::milliseconds crossfade{0};  // 0 = off, max 10 s
  ReplayGainMode replay_gain = ReplayGainMode::Track;
  float replay_gain_preamp_db = 0.0f;
  bool prevent_clipping = true;
  EqualizerSettings eq;
};

struct PlayerState {
  RepeatMode repeat = RepeatMode::Off;
  bool shuffle = false;
  std::chrono::milliseconds resume_position{0};
};

struct AppSettings {
  PlaybackSettings playback;
  PlayerState player;
  bool resume_on_startup = true;
  bool portable_mode = false;
  std::string theme = "system";  // "system" | "light" | "dark"
};

}  // namespace bh

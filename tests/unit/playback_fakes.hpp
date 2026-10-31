#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "core/playback/engine.hpp"

namespace bh::testutil {

using namespace std::chrono_literals;

// An output device that only produces audio when the test asks for it.
class ManualOutput final : public playback::IAudioOutput {
 public:
  explicit ManualOutput(playback::AudioFormat f = {48000, 2}) : format_(f) {}

  Result<playback::AudioFormat> open(const std::string& id, RenderFn render) override {
    if (fail_open) return std::unexpected(Error{ErrorCode::Device, "no device"});
    opened_id = id;
    render_ = std::move(render);
    ++open_count;
    return format_;
  }
  // Like a real device, the callback only becomes live once started.
  Result<void> start() override {
    running = true;
    ready_.store(true, std::memory_order_release);
    return {};
  }
  void stop() override {
    running = false;
    ready_.store(false, std::memory_order_release);
  }
  void close() override {
    running = false;
    ready_.store(false, std::memory_order_release);
    render_ = nullptr;
  }
  std::vector<playback::AudioDeviceInfo> devices() override { return {{"", "Default", true}}; }

  // Pull `frames` frames, as the device's callback would.
  std::vector<float> pull(std::size_t frames) {
    std::vector<float> out(frames * static_cast<std::size_t>(format_.channels));
    if (ready_.load(std::memory_order_acquire)) render_(out.data(), frames);  // else: device not open yet -> silence
    return out;
  }

  bool fail_open = false;
  std::atomic<bool> running{false};
  std::atomic<int> open_count{0};
  std::string opened_id;

 private:
  playback::AudioFormat format_;
  RenderFn render_;
  std::atomic<bool> ready_{false};
};

// Emits frame k as the value base + k in every channel, so tests can check exact continuity.
class SyntheticDecoder final : public playback::IDecoder {
 public:
  SyntheticDecoder(std::size_t frames, float base, playback::AudioFormat f)
      : total_(frames), base_(base), ch_(static_cast<std::size_t>(f.channels)), rate_(f.sample_rate) {}

  std::size_t read(float* dst, std::size_t frames) override {
    auto n = std::min(frames, total_ - pos_);
    for (std::size_t i = 0; i < n; ++i)
      for (std::size_t c = 0; c < ch_; ++c) dst[i * ch_ + c] = base_ + static_cast<float>(pos_ + i);
    pos_ += n;
    return n;
  }
  Result<void> seek(std::chrono::milliseconds p) override {
    pos_ = std::min(total_, static_cast<std::size_t>(p.count()) * static_cast<std::size_t>(rate_) / 1000);
    return {};
  }
  std::chrono::milliseconds duration() const override {
    return std::chrono::milliseconds{static_cast<std::int64_t>(total_ * 1000 / static_cast<std::size_t>(rate_))};
  }

 private:
  std::size_t total_, pos_ = 0;
  float base_;
  std::size_t ch_;
  int rate_;
};

// Path "len=<frames>,base=<n>" describes a synthetic track; "missing" fails to open.
inline playback::DecoderFactory synthetic_factory() {
  return [](const std::filesystem::path& p, playback::AudioFormat f) -> Result<std::unique_ptr<playback::IDecoder>> {
    auto s = p.string();
    if (s.starts_with("missing")) return std::unexpected(Error{ErrorCode::NotFound, "no such file"});
    std::size_t len = 0;
    float base = 0;
    std::sscanf(s.c_str(), "len=%zu,base=%f", &len, &base);
    return std::unique_ptr<playback::IDecoder>(std::make_unique<SyntheticDecoder>(len, base, f));
  };
}

inline playback::PlayItem synth(int id, std::size_t len, float base) {
  return {TrackId{id}, "len=" + std::to_string(len) + ",base=" + std::to_string(static_cast<int>(base))};
}

// Thread-safe event log.
struct EventLog {
  std::mutex mu;
  std::vector<playback::EngineEvent> events;

  void attach(playback::Engine& e) {
    e.set_event_handler([this](const playback::EngineEvent& ev) {
      std::lock_guard lk(mu);
      events.push_back(ev);
    });
  }
  std::vector<playback::EngineEvent> snapshot() {
    std::lock_guard lk(mu);
    return events;
  }
  int count(playback::EngineEvent::Type t) {
    int n = 0;
    for (auto& e : snapshot()) n += e.type == t;
    return n;
  }
  std::vector<std::pair<playback::EngineEvent::Type, std::int64_t>> track_events() {
    std::vector<std::pair<playback::EngineEvent::Type, std::int64_t>> out;
    for (auto& e : snapshot())
      if (e.type == playback::EngineEvent::Type::TrackStarted || e.type == playback::EngineEvent::Type::TrackFinished ||
          e.type == playback::EngineEvent::Type::PlaybackFinished)
        out.emplace_back(e.type, e.item.id.value);
    return out;
  }
};

template <typename Pred>
bool wait_for(Pred p, std::chrono::milliseconds timeout = 3000ms) {
  auto end = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < end) {
    if (p()) return true;
    std::this_thread::sleep_for(1ms);
  }
  return p();
}

// Pulls one small buffer (if any audio is ready), as a device would; never underruns.
inline std::vector<float> pull_some(ManualOutput& out, playback::Engine& e, std::size_t max_frames = 480) {
  auto n = e.buffered_frames();
  if (n == 0) return {};
  return out.pull(std::min(n, max_frames));
}

// Drains whatever the engine has decoded so far, in order, without ever underrunning.
inline std::vector<float> drain(ManualOutput& out, playback::Engine& e, std::size_t channels = 2) {
  std::vector<float> all;
  while (auto n = e.buffered_frames()) {
    auto chunk = out.pull(std::min<std::size_t>(n, 480));
    all.insert(all.end(), chunk.begin(), chunk.end());
  }
  (void)channels;
  return all;
}

}  // namespace bh::testutil

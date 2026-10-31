#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "core/model/ids.hpp"
#include "core/playback/audio_output.hpp"
#include "core/playback/decoder.hpp"
#include "core/playback/ring_buffer.hpp"

namespace bh::playback {

struct PlayItem {
  TrackId id;
  std::filesystem::path path;
  std::uint64_t tag = 0;  // opaque to the engine, echoed in events (the controller uses the queue entry id)
};

enum class PlayState { Stopped, Playing, Paused };

struct EngineEvent {
  enum class Type {
    TrackStarted,      // `item` began sounding (not sent after a seek)
    TrackFinished,     // `item` played to its end
    StateChanged,      // `state` is the new state
    NeedNext,          // decoding reached the end of the current item and nothing is queued:
                       // answer with set_next() (or leave it, and playback ends after the buffer drains)
    PlaybackFinished,  // everything queued has played; state is now Stopped
    Error,             // `error` (and `item`, if a specific file failed to open)
  };
  Type type;
  PlayItem item;
  PlayState state = PlayState::Stopped;
  Error error{ErrorCode::Io, {}};
};

struct PositionInfo {
  PlayItem item;
  std::chrono::milliseconds position{0};
  std::chrono::milliseconds duration{0};
};

// Gapless audio player core.
//
// Threads: public methods may be called from any thread and return immediately.
// A decoder thread decodes ahead into a lock-free ring buffer; the device's real-time
// callback only reads that ring (never locks). Events are delivered on the decoder
// thread without any engine lock held, so a handler may call back into the engine
// (e.g. set_next) but should hand slow work to another thread.
//
// Gapless: tell the engine what comes next with set_next(), either ahead of time or in
// answer to a NeedNext event (sent roughly one buffer-length before the audio runs out).
// When the current file ends the engine continues straight into it, sample-accurately.
class Engine {
 public:
  Engine(std::unique_ptr<IAudioOutput> output, DecoderFactory decoders = open_ffmpeg_decoder,
         std::chrono::milliseconds buffer = std::chrono::milliseconds{500});
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  // Replaces the handler. Blocks until a handler call already in progress has returned, so
  // after set_event_handler(nullptr) the old handler (and what it captured) is safe to destroy.
  // Must not be called from inside a handler.
  void set_event_handler(std::function<void(const EngineEvent&)> handler);

  void play(PlayItem item, std::chrono::milliseconds start = std::chrono::milliseconds{0});
  void set_next(std::optional<PlayItem> item);
  void pause();
  void resume();
  void stop();
  void seek(std::chrono::milliseconds position);

  void set_volume(float linear);  // 0..1
  void set_muted(bool muted);
  void set_output_device(std::string device_id);  // "" = default; playback continues on the new device

  PlayState state() const { return state_.load(); }
  std::optional<PositionInfo> position() const;  // nullopt when stopped
  std::size_t buffered_frames() const;           // decoded but not yet played (diagnostics / tests)
  std::vector<AudioDeviceInfo> devices() { return output_->devices(); }

 private:
  struct Command {
    enum class Type { Play, Seek, Stop, Device } type;
    PlayItem item;
    std::chrono::milliseconds position{0};
    std::string device;
  };
  struct Segment {
    PlayItem item;
    std::uint64_t start;                // stream frame where this item's audio begins
    std::optional<std::uint64_t> end;   // known once the decoder reaches EOF
    std::chrono::milliseconds duration;
    std::chrono::milliseconds offset;   // item position corresponding to `start` (after a seek)
    bool announce;                      // send TrackStarted/Finished
    bool started = false;
  };

  void run();
  void handle(Command& c);
  bool produce();
  bool open_next();
  bool ensure_output();
  void begin_item(const PlayItem& item, std::chrono::milliseconds start, bool announce);
  void flush_output();
  void fail(const Error& e, const PlayItem& item);
  void set_state_locked(PlayState s);  // mu_ held
  void check_boundaries();
  void dispatch_events();
  void render(float* out, std::size_t frames);  // real-time thread

  std::unique_ptr<IAudioOutput> output_;
  DecoderFactory factory_;
  const std::chrono::milliseconds buffer_ms_;

  // Shared, guarded by mu_.
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  std::vector<EngineEvent> events_;
  std::optional<PlayItem> next_;
  std::deque<Segment> segments_;
  std::function<void(const EngineEvent&)> handler_;
  std::mutex dispatch_mu_;  // held while handlers run; always taken before mu_
  std::string device_id_;
  bool quit_ = false;

  // Lock-free state shared with the real-time callback.
  std::atomic<PlayState> state_{PlayState::Stopped};
  std::atomic<bool> paused_{false};
  std::atomic<bool> muted_{false};
  std::atomic<float> volume_{1.0f};
  std::atomic<std::uint64_t> played_{0};   // frames consumed by the device (incl. discarded)
  std::atomic_flag busy_ = ATOMIC_FLAG_INIT;  // held by the callback, or by flush_output()
  std::unique_ptr<SpscRing> ring_;
  AudioFormat format_{};
  float gain_ = 1.0f;  // callback-owned ramp state
  float fade_ = 1.0f;

  // Decoder-thread only.
  std::unique_ptr<IDecoder> decoder_;
  std::uint64_t written_ = 0;  // frames pushed into the ring
  std::vector<float> chunk_;
  bool output_open_ = false;
  bool need_next_sent_ = false;  // NeedNext already raised for the current gap

  std::thread thread_;  // last: starts in the constructor body
};

}  // namespace bh::playback

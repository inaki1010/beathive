#pragma once
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "core/model/queue.hpp"
#include "core/playback/engine.hpp"

namespace bh::playback {

struct QueueSnapshot {
  std::vector<QueueEntry> entries;
  std::optional<std::uint64_t> now_playing;  // entry id of the audible track
  RepeatMode repeat = RepeatMode::Off;
  bool shuffle = false;
};

// Connects the play queue to the engine: starts tracks, keeps the engine's "up next"
// slot filled for gapless playback, and handles skipping / errors.
//
// Thread-safe. Callbacks run on whichever thread triggered them (the UI thread for user
// actions, the engine thread for playback events) with no controller lock held, so a UI
// layer should marshal them to its own thread.
class PlaybackController {
 public:
  using Resolver = std::function<std::optional<std::filesystem::path>(TrackId)>;

  PlaybackController(Engine& engine, Resolver resolver);
  ~PlaybackController();
  PlaybackController(const PlaybackController&) = delete;
  PlaybackController& operator=(const PlaybackController&) = delete;

  // Queue or now-playing changed.
  void set_on_changed(std::function<void()> cb);
  // A track could not be played and was skipped.
  void set_on_skipped(std::function<void(TrackId, const Error&)> cb);

  // Replace the queue and start playing at tracks[start].
  void play_tracks(const std::vector<TrackId>& tracks, std::size_t start = 0);
  void play_entry(std::uint64_t entry_id);
  void enqueue(const std::vector<TrackId>& tracks);
  void play_next(const std::vector<TrackId>& tracks);
  void remove(std::uint64_t entry_id);
  void move(std::size_t from, std::size_t to);
  void clear();

  void next();
  void previous();  // restarts the track if it has been playing for more than 3 s
  void toggle_pause();
  void stop();
  void set_shuffle(bool on);
  void set_repeat(RepeatMode mode);

  QueueSnapshot snapshot() const;
  std::optional<QueueEntry> now_playing() const;

 private:
  struct Scope;  // locks, and on destruction runs deferred callbacks outside the lock
  void on_event(const EngineEvent& e);
  bool play_cursor_locked();            // play the queue's current entry, skipping unplayable ones
  void refresh_next_locked();           // tell the engine what follows the cursor
  void adopt_locked(std::uint64_t tag); // a track became audible
  void report_skip_locked(TrackId id, const Error& e);
  std::optional<PlayItem> make_item_locked(const QueueEntry& e, Error* why);

  Engine& engine_;
  Resolver resolver_;
  mutable std::mutex mu_;
  PlayQueue queue_;
  std::uint64_t now_tag_ = 0;
  std::function<void()> on_changed_;
  std::function<void(TrackId, const Error&)> on_skipped_;
  std::vector<std::function<void()>> deferred_;
};

}  // namespace bh::playback

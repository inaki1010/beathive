#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "core/model/ids.hpp"

namespace bh {

enum class RepeatMode { Off, All, One };

// The "Now Playing" list. Holds track ids only; metadata comes from the library.
// Entries get a unique entry_id so the same track can appear twice and the
// UI can address one occurrence.
struct QueueEntry {
  std::uint64_t entry_id;
  TrackId track;
};

class PlayQueue {
 public:
  explicit PlayQueue(std::uint32_t seed = std::random_device{}()) : rng_(seed) {}

  const std::vector<QueueEntry>& entries() const { return entries_; }
  std::optional<std::size_t> current_index() const { return current_; }
  std::optional<QueueEntry> current() const;

  RepeatMode repeat() const { return repeat_; }
  bool shuffle() const { return shuffle_; }
  void set_repeat(RepeatMode m) { repeat_ = m; peeked_.reset(); }
  void set_shuffle(bool on);

  void clear();
  void append(const std::vector<TrackId>& tracks);
  void play_next(const std::vector<TrackId>& tracks);  // right after current
  void remove(std::uint64_t entry_id);
  void move(std::size_t from, std::size_t to);
  void set_current(std::uint64_t entry_id);

  // Replaces the queue and starts at `start` (an index into `tracks`).
  void replace(const std::vector<TrackId>& tracks, std::size_t start = 0);

  // Advance after a track ends (`auto_advance`, honours RepeatMode::One) or on
  // a user "next" press (never repeats the same track). Returns the new
  // current entry, or nullopt when the queue is finished.
  std::optional<QueueEntry> next(bool auto_advance);
  std::optional<QueueEntry> previous();

  // The entry next(true) would return, without moving. Stable until the queue changes
  // (so a shuffled "up next" stays what it was announced as).
  std::optional<QueueEntry> peek_next();

 private:
  void rebuild_shuffle_bag();
  std::optional<std::uint64_t> pick_shuffled(bool auto_advance);
  QueueEntry make(TrackId t) { return {next_entry_id_++, t}; }

  std::vector<QueueEntry> entries_;
  std::optional<std::size_t> current_;
  RepeatMode repeat_ = RepeatMode::Off;
  bool shuffle_ = false;
  std::uint64_t next_entry_id_ = 1;
  std::vector<std::uint64_t> shuffle_bag_;  // entry ids not yet played this round
  std::vector<std::uint64_t> history_;      // entry ids played, for previous()
  std::optional<std::uint64_t> peeked_;     // shuffled choice promised by peek_next()
  std::mt19937 rng_;
};

}  // namespace bh

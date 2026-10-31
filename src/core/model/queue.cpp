#include "core/model/queue.hpp"

#include <algorithm>

namespace bh {

namespace {
template <typename V, typename T>
void erase_value(V& v, const T& x) { v.erase(std::remove(v.begin(), v.end(), x), v.end()); }
}  // namespace

std::optional<QueueEntry> PlayQueue::current() const {
  if (!current_) return std::nullopt;
  return entries_[*current_];
}

void PlayQueue::set_shuffle(bool on) {
  shuffle_ = on;
  peeked_.reset();
  if (on) rebuild_shuffle_bag();
}

void PlayQueue::rebuild_shuffle_bag() {
  shuffle_bag_.clear();
  for (std::size_t i = 0; i < entries_.size(); ++i)
    if (i != current_) shuffle_bag_.push_back(entries_[i].entry_id);
}

void PlayQueue::clear() {
  peeked_.reset();
  entries_.clear();
  shuffle_bag_.clear();
  history_.clear();
  current_.reset();
}

void PlayQueue::append(const std::vector<TrackId>& tracks) {
  peeked_.reset();
  for (auto t : tracks) {
    entries_.push_back(make(t));
    if (shuffle_) shuffle_bag_.push_back(entries_.back().entry_id);
  }
}

void PlayQueue::play_next(const std::vector<TrackId>& tracks) {
  peeked_.reset();
  auto pos = current_ ? entries_.begin() + static_cast<std::ptrdiff_t>(*current_) + 1 : entries_.begin();
  std::vector<QueueEntry> added;
  for (auto t : tracks) added.push_back(make(t));
  if (shuffle_)
    for (auto& e : added) shuffle_bag_.push_back(e.entry_id);
  entries_.insert(pos, added.begin(), added.end());
}

void PlayQueue::remove(std::uint64_t entry_id) {
  auto it = std::ranges::find(entries_, entry_id, &QueueEntry::entry_id);
  if (it == entries_.end()) return;
  auto idx = static_cast<std::size_t>(it - entries_.begin());
  entries_.erase(it);
  peeked_.reset();
  erase_value(shuffle_bag_, entry_id);
  erase_value(history_, entry_id);

  if (entries_.empty()) {
    current_.reset();
  } else if (current_) {
    // Removing the current entry moves "current" to the one that followed it.
    if (idx < *current_) --*current_;
    if (*current_ >= entries_.size()) current_ = entries_.size() - 1;
  }
}

void PlayQueue::move(std::size_t from, std::size_t to) {
  if (from >= entries_.size() || to >= entries_.size() || from == to) return;
  peeked_.reset();
  auto cur_id = current() ? std::optional(current()->entry_id) : std::nullopt;
  auto e = entries_[from];
  entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(from));
  entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(to), e);
  if (cur_id) {
    auto it = std::ranges::find(entries_, *cur_id, &QueueEntry::entry_id);
    current_ = static_cast<std::size_t>(it - entries_.begin());
  }
}

void PlayQueue::set_current(std::uint64_t entry_id) {
  auto it = std::ranges::find(entries_, entry_id, &QueueEntry::entry_id);
  if (it == entries_.end()) return;
  peeked_.reset();
  if (auto c = current()) history_.push_back(c->entry_id);
  current_ = static_cast<std::size_t>(it - entries_.begin());
  erase_value(shuffle_bag_, entry_id);
}

void PlayQueue::replace(const std::vector<TrackId>& tracks, std::size_t start) {
  clear();  // also drops any peeked choice
  for (auto t : tracks) entries_.push_back(make(t));
  if (!entries_.empty()) current_ = std::min(start, entries_.size() - 1);
  if (shuffle_) rebuild_shuffle_bag();
}

std::optional<std::uint64_t> PlayQueue::pick_shuffled(bool auto_advance) {
  if (shuffle_bag_.empty()) {
    if (auto_advance && repeat_ != RepeatMode::All) return std::nullopt;  // round over
    rebuild_shuffle_bag();
    if (shuffle_bag_.empty()) return entries_[*current_].entry_id;  // single-track queue
  }
  std::uniform_int_distribution<std::size_t> pick(0, shuffle_bag_.size() - 1);
  return shuffle_bag_[pick(rng_)];
}

std::optional<QueueEntry> PlayQueue::peek_next() {
  if (entries_.empty()) return std::nullopt;
  if (!current_) return entries_[0];
  if (repeat_ == RepeatMode::One) return entries_[*current_];

  if (shuffle_) {
    if (!peeked_) peeked_ = pick_shuffled(/*auto_advance=*/true);
    if (!peeked_) return std::nullopt;
    auto it = std::ranges::find(entries_, *peeked_, &QueueEntry::entry_id);
    return it == entries_.end() ? std::nullopt : std::optional(*it);
  }
  if (*current_ + 1 < entries_.size()) return entries_[*current_ + 1];
  if (repeat_ == RepeatMode::All) return entries_[0];
  return std::nullopt;
}

std::optional<QueueEntry> PlayQueue::next(bool auto_advance) {
  if (entries_.empty()) return std::nullopt;
  if (!current_) {  // nothing playing: start from the top
    current_ = 0;
    if (shuffle_) rebuild_shuffle_bag();
    return entries_[0];
  }
  if (auto_advance && repeat_ == RepeatMode::One) return entries_[*current_];

  history_.push_back(entries_[*current_].entry_id);

  if (shuffle_) {
    // Honour a promised peek_next(); otherwise choose now.
    auto chosen = peeked_ ? peeked_ : pick_shuffled(auto_advance);
    peeked_.reset();
    if (!chosen) { current_.reset(); return std::nullopt; }
    erase_value(shuffle_bag_, *chosen);
    auto it = std::ranges::find(entries_, *chosen, &QueueEntry::entry_id);
    current_ = static_cast<std::size_t>(it - entries_.begin());
    return entries_[*current_];
  }

  if (*current_ + 1 < entries_.size()) {
    ++*current_;
  } else if (repeat_ == RepeatMode::All || !auto_advance) {
    current_ = 0;  // user pressed "next" on the last track: wrap
  } else {
    current_.reset();
    return std::nullopt;
  }
  return entries_[*current_];
}

std::optional<QueueEntry> PlayQueue::previous() {
  if (entries_.empty()) return std::nullopt;
  while (!history_.empty()) {
    auto id = history_.back();
    history_.pop_back();
    auto it = std::ranges::find(entries_, id, &QueueEntry::entry_id);
    if (it == entries_.end()) continue;
    current_ = static_cast<std::size_t>(it - entries_.begin());
    peeked_.reset();
    if (shuffle_) erase_value(shuffle_bag_, id);  // going back must not re-queue it this round
    return *it;
  }
  return current();  // no history: restart the current track
}

}  // namespace bh

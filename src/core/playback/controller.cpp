#include "core/playback/controller.hpp"

#include <algorithm>

namespace bh::playback {

using namespace std::chrono_literals;

struct PlaybackController::Scope {
  explicit Scope(PlaybackController& c) : c_(c), lk_(c.mu_) {}
  ~Scope() {
    auto run = std::move(c_.deferred_);
    c_.deferred_.clear();
    lk_.unlock();
    for (auto& f : run) f();
  }
  PlaybackController& c_;
  std::unique_lock<std::mutex> lk_;
};

PlaybackController::PlaybackController(Engine& engine, Resolver resolver)
    : engine_(engine), resolver_(std::move(resolver)) {
  engine_.set_event_handler([this](const EngineEvent& e) { on_event(e); });
}

PlaybackController::~PlaybackController() { engine_.set_event_handler(nullptr); }

void PlaybackController::set_on_changed(std::function<void()> cb) {
  std::lock_guard lk(mu_);
  on_changed_ = std::move(cb);
}
void PlaybackController::set_on_skipped(std::function<void(TrackId, const Error&)> cb) {
  std::lock_guard lk(mu_);
  on_skipped_ = std::move(cb);
}

// ------------------------------------------------------------------ helpers

std::optional<PlayItem> PlaybackController::make_item_locked(const QueueEntry& e, Error* why) {
  auto path = resolver_(e.track);
  if (!path) {
    if (why) *why = {ErrorCode::NotFound, "track is no longer in the library"};
    return std::nullopt;
  }
  return PlayItem{e.track, std::move(*path), e.entry_id};
}

void PlaybackController::report_skip_locked(TrackId id, const Error& e) {
  if (on_skipped_) deferred_.push_back([cb = on_skipped_, id, e] { cb(id, e); });
}

bool PlaybackController::play_cursor_locked() {
  const auto limit = queue_.entries().size() + 1;
  for (std::size_t tries = 0; tries < limit; ++tries) {
    auto cur = queue_.current();
    if (!cur) break;
    Error why{ErrorCode::NotFound, {}};
    if (auto item = make_item_locked(*cur, &why)) {
      now_tag_ = cur->entry_id;
      engine_.play(std::move(*item));
      refresh_next_locked();
      if (on_changed_) deferred_.push_back(on_changed_);
      return true;
    }
    report_skip_locked(cur->track, why);
    if (!queue_.next(true)) break;
  }
  engine_.stop();
  now_tag_ = 0;
  if (on_changed_) deferred_.push_back(on_changed_);
  return false;
}

void PlaybackController::refresh_next_locked() {
  // Entries that can no longer be resolved are dropped from the queue so playback never stalls on them.
  while (auto n = queue_.peek_next()) {
    Error why{ErrorCode::NotFound, {}};
    if (auto item = make_item_locked(*n, &why)) {
      engine_.set_next(std::move(item));
      return;
    }
    report_skip_locked(n->track, why);
    queue_.remove(n->entry_id);
  }
  engine_.set_next(std::nullopt);
}

void PlaybackController::adopt_locked(std::uint64_t tag) {
  auto cur = queue_.current();
  if (!(cur && cur->entry_id == tag)) {
    auto nxt = queue_.peek_next();
    if (nxt && nxt->entry_id == tag) queue_.next(true);  // the gapless handoff we arranged
    else queue_.set_current(tag);                         // queue was edited underneath us
  }
  now_tag_ = tag;
  if (on_changed_) deferred_.push_back(on_changed_);
}

// ------------------------------------------------------------ engine events

void PlaybackController::on_event(const EngineEvent& e) {
  Scope s(*this);
  switch (e.type) {
    case EngineEvent::Type::NeedNext:
      refresh_next_locked();
      break;

    case EngineEvent::Type::TrackStarted:
      adopt_locked(e.item.tag);
      break;

    case EngineEvent::Type::TrackFinished:
      break;

    case EngineEvent::Type::StateChanged:
      if (on_changed_) deferred_.push_back(on_changed_);
      break;

    case EngineEvent::Type::PlaybackFinished:
      // Normally the queue is exhausted. If a next entry exists (we were too late), carry on.
      if (queue_.peek_next()) {
        queue_.next(true);
        play_cursor_locked();
      } else {
        queue_.next(true);  // resets the cursor
        now_tag_ = 0;
        if (on_changed_) deferred_.push_back(on_changed_);
      }
      break;

    case EngineEvent::Type::Error: {
      if (e.item.tag == 0) {  // not about a particular track (e.g. the audio device)
        report_skip_locked(e.item.id, e.error);
        break;
      }
      report_skip_locked(e.item.id, e.error);
      auto cur = queue_.current();
      auto nxt = queue_.peek_next();
      if (cur && cur->entry_id == e.item.tag) {          // the track we just started
        if (queue_.next(true)) play_cursor_locked();
        else { engine_.stop(); now_tag_ = 0; if (on_changed_) deferred_.push_back(on_changed_); }
      } else if (nxt && nxt->entry_id == e.item.tag) {   // the one we prefetched
        queue_.next(true);                               // skip over it
        refresh_next_locked();
      }
      break;
    }
  }
}

// ---------------------------------------------------------------- user actions

void PlaybackController::play_tracks(const std::vector<TrackId>& tracks, std::size_t start) {
  Scope s(*this);
  queue_.replace(tracks, start);
  play_cursor_locked();
}

void PlaybackController::play_entry(std::uint64_t entry_id) {
  Scope s(*this);
  queue_.set_current(entry_id);
  play_cursor_locked();
}

void PlaybackController::enqueue(const std::vector<TrackId>& tracks) {
  Scope s(*this);
  queue_.append(tracks);
  refresh_next_locked();
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::play_next(const std::vector<TrackId>& tracks) {
  Scope s(*this);
  queue_.play_next(tracks);
  refresh_next_locked();
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::remove(std::uint64_t entry_id) {
  Scope s(*this);
  const auto& entries = queue_.entries();
  const auto it = std::ranges::find(entries, entry_id, &QueueEntry::entry_id);
  if (it == entries.end()) return;
  const auto idx = static_cast<std::size_t>(it - entries.begin());
  const bool was_cursor = queue_.current() && queue_.current()->entry_id == entry_id;

  queue_.remove(entry_id);

  if (was_cursor && engine_.state() != PlayState::Stopped) {
    // The playing track left the list but keeps sounding. What follows it is whatever now
    // sits at its old position (the queue's own cursor clamps backwards at the end of the list).
    const auto& now = queue_.entries();
    std::optional<QueueEntry> following;
    if (idx < now.size()) following = now[idx];
    else if (queue_.repeat() == RepeatMode::All && !now.empty()) following = now[0];
    if (following) {
      if (auto item = make_item_locked(*following, nullptr)) engine_.set_next(std::move(item));
      else engine_.set_next(std::nullopt);
    } else {
      engine_.set_next(std::nullopt);
    }
  } else {
    refresh_next_locked();
  }
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::move(std::size_t from, std::size_t to) {
  Scope s(*this);
  queue_.move(from, to);
  refresh_next_locked();
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::clear() {
  Scope s(*this);
  queue_.clear();
  engine_.stop();
  now_tag_ = 0;
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::next() {
  Scope s(*this);
  if (queue_.next(false)) play_cursor_locked();
  else { engine_.stop(); now_tag_ = 0; if (on_changed_) deferred_.push_back(on_changed_); }
}

void PlaybackController::previous() {
  Scope s(*this);
  if (auto pos = engine_.position(); pos && pos->position > 3000ms) {
    engine_.seek(0ms);
    return;
  }
  if (queue_.previous()) play_cursor_locked();
}

void PlaybackController::toggle_pause() {
  switch (engine_.state()) {
    case PlayState::Playing: engine_.pause(); break;
    case PlayState::Paused: engine_.resume(); break;
    case PlayState::Stopped: {
      Scope s(*this);
      if (queue_.current() || queue_.next(false)) play_cursor_locked();
      break;
    }
  }
}

void PlaybackController::stop() {
  Scope s(*this);
  engine_.stop();
  now_tag_ = 0;
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::set_shuffle(bool on) {
  Scope s(*this);
  queue_.set_shuffle(on);
  refresh_next_locked();
  if (on_changed_) deferred_.push_back(on_changed_);
}

void PlaybackController::set_repeat(RepeatMode mode) {
  Scope s(*this);
  queue_.set_repeat(mode);
  refresh_next_locked();
  if (on_changed_) deferred_.push_back(on_changed_);
}

QueueSnapshot PlaybackController::snapshot() const {
  std::lock_guard lk(mu_);
  QueueSnapshot q;
  q.entries = queue_.entries();
  if (now_tag_) q.now_playing = now_tag_;
  q.repeat = queue_.repeat();
  q.shuffle = queue_.shuffle();
  return q;
}

std::optional<QueueEntry> PlaybackController::now_playing() const {
  std::lock_guard lk(mu_);
  for (auto& e : queue_.entries())
    if (e.entry_id == now_tag_) return e;
  return std::nullopt;
}

}  // namespace bh::playback

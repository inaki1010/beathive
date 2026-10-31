#include "core/playback/engine.hpp"

#include <algorithm>
#include <cstring>

namespace bh::playback {

using namespace std::chrono_literals;

namespace {
constexpr std::size_t kChunkFrames = 2048;

std::chrono::milliseconds frames_to_ms(std::uint64_t frames, int rate) {
  return std::chrono::milliseconds{static_cast<std::int64_t>(frames * 1000 / static_cast<std::uint64_t>(rate))};
}
}  // namespace

Engine::Engine(std::unique_ptr<IAudioOutput> output, DecoderFactory decoders, std::chrono::milliseconds buffer)
    : output_(std::move(output)), factory_(std::move(decoders)), buffer_ms_(buffer) {
  thread_ = std::thread([this] { run(); });
}

Engine::~Engine() {
  {
    std::lock_guard lk(mu_);
    quit_ = true;
  }
  cv_.notify_all();
  thread_.join();
  output_->close();  // waits for the real-time callback to finish
}

// ---------------------------------------------------------------- public API

void Engine::set_event_handler(std::function<void(const EngineEvent&)> handler) {
  std::lock_guard dispatch(dispatch_mu_);  // wait out any in-flight dispatch
  std::lock_guard lk(mu_);
  handler_ = std::move(handler);
}

void Engine::set_state_locked(PlayState s) {
  if (state_.exchange(s) != s) events_.push_back({EngineEvent::Type::StateChanged, {}, s});
}

void Engine::play(PlayItem item, std::chrono::milliseconds start) {
  {
    std::lock_guard lk(mu_);
    commands_.push_back({Command::Type::Play, std::move(item), start, {}});
    next_.reset();
    paused_ = false;
    set_state_locked(PlayState::Playing);
  }
  cv_.notify_all();
}

void Engine::set_next(std::optional<PlayItem> item) {
  {
    std::lock_guard lk(mu_);
    next_ = std::move(item);
  }
  cv_.notify_all();
}

void Engine::pause() {
  std::lock_guard lk(mu_);
  if (state_ != PlayState::Playing) return;
  paused_ = true;
  set_state_locked(PlayState::Paused);
}

void Engine::resume() {
  {
    std::lock_guard lk(mu_);
    if (state_ != PlayState::Paused) return;
    paused_ = false;
    set_state_locked(PlayState::Playing);
  }
  cv_.notify_all();
}

void Engine::stop() {
  {
    std::lock_guard lk(mu_);
    commands_.push_back({Command::Type::Stop, {}, {}, {}});
    next_.reset();
    paused_ = false;
    set_state_locked(PlayState::Stopped);
  }
  cv_.notify_all();
}

void Engine::seek(std::chrono::milliseconds position) {
  {
    std::lock_guard lk(mu_);
    if (state_ == PlayState::Stopped) return;
    commands_.push_back({Command::Type::Seek, {}, position, {}});
  }
  cv_.notify_all();
}

void Engine::set_volume(float v) { volume_ = std::clamp(v, 0.0f, 1.0f); }
void Engine::set_muted(bool m) { muted_ = m; }

void Engine::set_output_device(std::string id) {
  {
    std::lock_guard lk(mu_);
    commands_.push_back({Command::Type::Device, {}, {}, std::move(id)});
  }
  cv_.notify_all();
}

std::optional<PositionInfo> Engine::position() const {
  std::lock_guard lk(mu_);
  if (segments_.empty() || state_ == PlayState::Stopped || format_.sample_rate <= 0) return std::nullopt;
  const auto& s = segments_.front();
  auto played = played_.load();
  auto into = played > s.start ? played - s.start : 0;
  return PositionInfo{s.item, s.offset + frames_to_ms(into, format_.sample_rate), s.duration};
}

std::size_t Engine::buffered_frames() const {
  std::lock_guard lk(mu_);
  return ring_ && format_.channels > 0 ? ring_->read_available() / static_cast<std::size_t>(format_.channels) : 0;
}

// ------------------------------------------------------------ real-time side

void Engine::render(float* out, std::size_t frames) {
  const auto ch = static_cast<std::size_t>(format_.channels);
  // flush_output() briefly owns the consumer side; never wait for it, just emit silence.
  if (busy_.test_and_set(std::memory_order_acquire)) {
    std::memset(out, 0, frames * ch * sizeof(float));
    return;
  }

  const bool paused = paused_.load(std::memory_order_relaxed);
  const float gain_target = muted_.load(std::memory_order_relaxed) ? 0.0f : volume_.load(std::memory_order_relaxed);
  const float fade_target = paused ? 0.0f : 1.0f;

  if (paused && fade_ == 0.0f) {  // fully faded out: don't consume anything
    std::memset(out, 0, frames * ch * sizeof(float));
    busy_.clear(std::memory_order_release);
    return;
  }

  const std::size_t got = ring_->read(out, frames * ch) / ch;
  std::memset(out + got * ch, 0, (frames - got) * ch * sizeof(float));  // underrun -> silence

  // Ramp volume and pause-fade across the buffer so changes never click.
  // (Interpolated per sample rather than accumulated, so the end point is exact.)
  const float g0 = gain_, f0 = fade_;
  const float inv = 1.0f / static_cast<float>(frames);
  for (std::size_t i = 0; i < frames; ++i) {
    const float t = static_cast<float>(i + 1) * inv;
    const float k = (g0 + (gain_target - g0) * t) * (f0 + (fade_target - f0) * t);
    for (std::size_t c = 0; c < ch; ++c) out[i * ch + c] *= k;
  }
  gain_ = gain_target;
  fade_ = fade_target;

  played_.fetch_add(got, std::memory_order_relaxed);
  busy_.clear(std::memory_order_release);
}

// ------------------------------------------------------------ decoder thread

void Engine::run() {
  for (;;) {
    std::deque<Command> cmds;
    {
      std::lock_guard lk(mu_);
      if (quit_) return;
      cmds.swap(commands_);
    }
    for (auto& c : cmds) handle(c);
    check_boundaries();
    dispatch_events();

    if (!produce()) {
      std::unique_lock lk(mu_);
      if (quit_) return;
      if (commands_.empty() && events_.empty()) cv_.wait_for(lk, 5ms);
    }
  }
}

void Engine::handle(Command& c) {
  switch (c.type) {
    case Command::Type::Play:
      begin_item(c.item, c.position, /*announce=*/true);
      break;

    case Command::Type::Stop:
      flush_output();
      decoder_.reset();
      break;

    case Command::Type::Seek: {
      std::optional<PositionInfo> at;
      {
        std::lock_guard lk(mu_);
        if (segments_.empty()) return;
        // If the next item was already decoding into the ring, hand it back to be reopened later.
        if (segments_.size() > 1 && !next_) next_ = segments_[1].item;
      }
      at = position();
      if (at) begin_item(at->item, c.position, /*announce=*/false);
      break;
    }

    case Command::Type::Device: {
      std::optional<PositionInfo> at = position();
      {
        std::lock_guard lk(mu_);
        if (segments_.size() > 1 && !next_) next_ = segments_[1].item;
        device_id_ = c.device;
      }
      if (output_open_) {
        output_->stop();
        output_->close();
        output_open_ = false;
        std::lock_guard lk(mu_);
        ring_.reset();
      }
      decoder_.reset();
      {
        std::lock_guard lk(mu_);
        segments_.clear();
        written_ = played_.load();
      }
      if (at && state_ != PlayState::Stopped) begin_item(at->item, at->position, /*announce=*/false);
      break;
    }
  }
}

bool Engine::ensure_output() {
  if (output_open_) return true;
  std::string dev;
  {
    std::lock_guard lk(mu_);
    dev = device_id_;
  }
  auto fmt = output_->open(dev, [this](float* out, std::size_t n) { render(out, n); });
  if (!fmt) {
    fail(fmt.error(), {});
    return false;
  }
  {
    std::lock_guard lk(mu_);
    format_ = *fmt;
    ring_ = std::make_unique<SpscRing>(static_cast<std::size_t>(fmt->sample_rate) *
                                       static_cast<std::size_t>(fmt->channels) *
                                       static_cast<std::size_t>(buffer_ms_.count()) / 1000);
  }
  chunk_.assign(kChunkFrames * static_cast<std::size_t>(fmt->channels), 0.0f);
  gain_ = muted_ ? 0.0f : volume_.load();
  fade_ = paused_ ? 0.0f : 1.0f;
  if (auto s = output_->start(); !s) {
    output_->close();
    fail(s.error(), {});
    return false;
  }
  output_open_ = true;
  return true;
}

// Empties the ring and forgets queued segments. The producer (this thread) briefly acts as
// the consumer; the callback sees busy_ set and outputs silence for that instant.
void Engine::flush_output() {
  if (ring_) {
    while (busy_.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    const auto dropped = ring_->discard_all();
    played_.fetch_add(dropped / static_cast<std::size_t>(format_.channels));
    busy_.clear(std::memory_order_release);
  }
  std::lock_guard lk(mu_);
  segments_.clear();
  written_ = played_.load();
}

void Engine::begin_item(const PlayItem& item, std::chrono::milliseconds start, bool announce) {
  decoder_.reset();
  if (!ensure_output()) {
    std::lock_guard lk(mu_);
    set_state_locked(PlayState::Stopped);
    return;
  }
  flush_output();

  auto dec = factory_(item.path, format_);
  if (!dec) {
    fail(dec.error(), item);
    std::lock_guard lk(mu_);
    set_state_locked(PlayState::Stopped);
    return;
  }
  decoder_ = std::move(*dec);
  need_next_sent_ = false;
  if (start.count() > 0) (void)decoder_->seek(start);  // on failure we simply start from 0

  std::lock_guard lk(mu_);
  segments_.push_back({item, written_, std::nullopt, decoder_->duration(), start, announce});
}

bool Engine::open_next() {
  PlayItem item;
  {
    std::lock_guard lk(mu_);
    if (!next_ || state_ == PlayState::Stopped) return false;
    item = std::move(*next_);
    next_.reset();
  }
  auto dec = factory_(item.path, format_);
  if (!dec) {
    fail(dec.error(), item);  // playback carries on until the buffered audio drains
    return false;
  }
  decoder_ = std::move(*dec);
  std::lock_guard lk(mu_);
  segments_.push_back({item, written_, std::nullopt, decoder_->duration(), 0ms, true});
  return true;
}

// Decodes one chunk into the ring if there is room. Returns true if it did any work.
bool Engine::produce() {
  if (state_ == PlayState::Stopped || !output_open_) return false;

  if (!decoder_) {
    {
      // Decode at most one item ahead of the one being heard. This keeps events in order
      // even for items shorter than the buffer: NeedNext for item N+2 can't fire before
      // TrackStarted for item N+1 has been delivered.
      std::lock_guard lk(mu_);
      if (segments_.size() >= 2) return false;
    }
    if (open_next()) {
      need_next_sent_ = false;
      return true;
    }
    if (!need_next_sent_) {  // nothing queued (or it failed to open): ask while audio is still buffered
      need_next_sent_ = true;
      std::lock_guard lk(mu_);
      events_.push_back({EngineEvent::Type::NeedNext, {}});
    }
    return false;
  }

  const auto ch = static_cast<std::size_t>(format_.channels);
  if (ring_->write_available() < kChunkFrames * ch) return false;

  const std::size_t n = decoder_->read(chunk_.data(), kChunkFrames);
  ring_->write(chunk_.data(), n * ch);
  written_ += n;

  if (n < kChunkFrames) {  // end of this item
    {
      std::lock_guard lk(mu_);
      if (!segments_.empty()) segments_.back().end = written_;
    }
    decoder_.reset();
    need_next_sent_ = false;
  }
  return true;
}

// Turns "the device has played past X" into events, and detects the end of playback.
void Engine::check_boundaries() {
  std::lock_guard lk(mu_);
  const auto played = played_.load();
  while (!segments_.empty()) {
    auto& s = segments_.front();
    if (!s.started && played >= s.start) {
      s.started = true;
      if (s.announce) events_.push_back({EngineEvent::Type::TrackStarted, s.item});
    }
    if (s.end && played >= *s.end) {
      if (s.announce) events_.push_back({EngineEvent::Type::TrackFinished, s.item});
      segments_.pop_front();
      continue;
    }
    break;
  }
  // Finished only once the controller has had its chance to answer NeedNext (asked, and delivered).
  if (segments_.empty() && !decoder_ && commands_.empty() && !next_ && events_.empty() && need_next_sent_ &&
      state_ != PlayState::Stopped) {
    events_.push_back({EngineEvent::Type::PlaybackFinished, {}});
    set_state_locked(PlayState::Stopped);
    paused_ = false;
  }
}

void Engine::fail(const Error& e, const PlayItem& item) {
  std::lock_guard lk(mu_);
  EngineEvent ev{EngineEvent::Type::Error, item};
  ev.error = e;
  events_.push_back(std::move(ev));
}

void Engine::dispatch_events() {
  std::lock_guard dispatch(dispatch_mu_);
  std::vector<EngineEvent> events;
  std::function<void(const EngineEvent&)> handler;
  {
    std::lock_guard lk(mu_);
    if (events_.empty()) return;
    events.swap(events_);
    handler = handler_;
  }
  if (handler)
    for (auto& e : events) handler(e);
}

}  // namespace bh::playback

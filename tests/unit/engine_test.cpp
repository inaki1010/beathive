#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "playback_fakes.hpp"

using namespace bh;
using namespace bh::playback;
using namespace bh::testutil;
using T = EngineEvent::Type;

namespace {

struct Rig {
  ManualOutput* out;  // owned by the engine
  EventLog log;
  std::unique_ptr<Engine> engine;

  explicit Rig(std::chrono::milliseconds buffer = 500ms) {
    auto o = std::make_unique<ManualOutput>();
    out = o.get();
    engine = std::make_unique<Engine>(std::move(o), synthetic_factory(), buffer);
    log.attach(*engine);
  }

  // Pull everything until the engine reports the end, collecting the audio.
  std::vector<float> play_to_end() {
    std::vector<float> all;
    auto ok = wait_for([&] {
      auto chunk = pull_some(*out, *engine);
      all.insert(all.end(), chunk.begin(), chunk.end());
      return log.count(T::PlaybackFinished) > 0;
    });
    REQUIRE(ok);
    return all;
  }
};

// Left channel of an interleaved stereo buffer.
std::vector<float> left(const std::vector<float>& v) {
  std::vector<float> l;
  for (std::size_t i = 0; i < v.size(); i += 2) l.push_back(v[i]);
  return l;
}

}  // namespace

TEST_CASE("a single track plays out completely and reports its lifecycle") {
  Rig r;
  r.engine->play(synth(1, 10000, 0));
  auto audio = left(r.play_to_end());

  REQUIRE(audio.size() >= 10000);
  for (std::size_t i = 0; i < 10000; ++i) REQUIRE(audio[i] == static_cast<float>(i));

  using P = std::pair<T, std::int64_t>;
  CHECK(r.log.track_events() == std::vector<P>{{T::TrackStarted, 1}, {T::TrackFinished, 1}, {T::PlaybackFinished, 0}});
  CHECK(r.engine->state() == PlayState::Stopped);
  CHECK_FALSE(r.engine->position());
}

TEST_CASE("gapless: the next track follows with no missing or extra samples") {
  Rig r;
  r.engine->play(synth(1, 10000, 0));
  r.engine->set_next(synth(2, 7000, 100000));
  auto audio = left(r.play_to_end());

  // Exactly the first track, then exactly the second, back to back.
  for (std::size_t i = 0; i < 10000; ++i) REQUIRE(audio[i] == static_cast<float>(i));
  for (std::size_t i = 0; i < 7000; ++i) REQUIRE(audio[10000 + i] == 100000.f + static_cast<float>(i));

  using P = std::pair<T, std::int64_t>;
  CHECK(r.log.track_events() == std::vector<P>{{T::TrackStarted, 1},
                                               {T::TrackFinished, 1},
                                               {T::TrackStarted, 2},
                                               {T::TrackFinished, 2},
                                               {T::PlaybackFinished, 0}});
}

TEST_CASE("the controller supplies the next track when the engine asks (NeedNext)") {
  Rig r;
  std::vector<PlayItem> playlist = {synth(1, 6000, 0), synth(2, 6000, 100000), synth(3, 6000, 200000)};
  std::size_t cursor = 0;  // touched only by the engine's event thread
  r.engine->set_event_handler([&](const EngineEvent& e) {
    {
      std::lock_guard lk(r.log.mu);
      r.log.events.push_back(e);
    }
    if (e.type == T::NeedNext && cursor + 1 < playlist.size()) r.engine->set_next(playlist[++cursor]);
  });
  r.engine->play(playlist[0]);
  auto audio = left(r.play_to_end());

  REQUIRE(audio.size() >= 18000);
  CHECK(audio[0] == 0.f);
  CHECK(audio[6000] == 100000.f);
  CHECK(audio[12000] == 200000.f);
  CHECK(audio[17999] == 200000.f + 5999.f);
  CHECK(r.log.count(T::TrackStarted) == 3);
  CHECK(r.log.count(T::NeedNext) == 3);  // the last answer is "nothing more"
}

TEST_CASE("position tracks the device, not the decoder") {
  Rig r;
  r.engine->play(synth(1, 48000, 0));  // 1 second at 48 kHz
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 24000; }));

  r.out->pull(12000);  // device consumes a quarter second
  r.out->pull(1);
  REQUIRE(wait_for([&] { return r.engine->position().has_value(); }));
  auto p = *r.engine->position();
  CHECK(p.item.id == TrackId{1});
  CHECK(p.duration == 1000ms);
  CHECK(p.position >= 250ms);
  CHECK(p.position < 260ms);
}

TEST_CASE("seek jumps within the track and drops stale audio") {
  Rig r;
  r.engine->play(synth(1, 96000, 0));  // 2 seconds
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 20000; }));
  r.out->pull(1000);

  r.engine->seek(1000ms);
  std::this_thread::sleep_for(150ms);  // let the decoder thread flush and reopen
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 1000; }));

  auto next = left(r.out->pull(2000));
  CHECK(next[0] == 48000.f);                      // exactly one second in
  CHECK(next[1999] == 48000.f + 1999.f);          // and contiguous from there
  CHECK(r.engine->position()->position >= 1000ms);
  CHECK(r.log.count(T::TrackStarted) == 1);       // a seek is not a new track
}

TEST_CASE("seeking back after the next track began buffering does not lose that track") {
  Rig r(2000ms);  // big buffer so track 2 is decoded well ahead of playback
  r.engine->play(synth(1, 4000, 0));
  r.engine->set_next(synth(2, 4000, 100000));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 8000; }));  // both fully decoded

  r.engine->seek(0ms);
  std::this_thread::sleep_for(150ms);
  auto audio = left(r.play_to_end());

  // Track 1 again from its start, then track 2 in full.
  REQUIRE(audio.size() >= 8000);
  CHECK(audio[0] == 0.f);
  CHECK(audio[3999] == 3999.f);
  CHECK(audio[4000] == 100000.f);
  CHECK(audio[7999] == 103999.f);
  CHECK(r.log.count(T::TrackStarted) == 2);
}

TEST_CASE("pause fades out without losing place; resume continues seamlessly") {
  Rig r;
  r.engine->play(synth(1, 48000, 1000));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 20000; }));
  r.out->pull(480);

  r.engine->pause();
  CHECK(r.engine->state() == PlayState::Paused);
  auto fade = left(r.out->pull(480));
  auto silent1 = left(r.out->pull(480));
  auto silent2 = left(r.out->pull(480));

  // Fade-out buffer: continues the signal, ramping to zero. Afterwards: pure silence.
  CHECK(fade[0] > fade[479] / 1000.f);                    // some audio at the start
  CHECK(std::fabs(fade[479]) < std::fabs(fade[0]) * 0.5f);  // decaying by the end (signal itself grows slowly)
  for (float s : silent1) REQUIRE(s == 0.f);
  for (float s : silent2) REQUIRE(s == 0.f);

  r.engine->resume();
  auto back = left(r.out->pull(480));
  // The device consumed exactly 480 (before) + 480 (fade) frames before pausing, so the
  // signal resumes at frame 960 — nothing was skipped or repeated while paused.
  CHECK(back[479] == (1000.f + 960.f + 479.f));  // fade-in reaches full gain on the last frame
}

TEST_CASE("volume and mute scale the output") {
  Rig r;
  r.engine->play(synth(1, 48000, 1000));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 10000; }));

  r.engine->set_volume(0.5f);
  r.out->pull(480);  // ramp buffer
  auto half = left(r.out->pull(10));
  CHECK(half[0] == 0.5f * (1000.f + 480.f));

  r.engine->set_muted(true);
  r.out->pull(480);
  auto muted = left(r.out->pull(10));
  for (float s : muted) CHECK(s == 0.f);
  // Muted playback still advances.
  r.engine->set_muted(false);
  r.engine->set_volume(1.0f);
  r.out->pull(480);
  auto loud = left(r.out->pull(1));
  CHECK(loud[0] == 1000.f + 480 + 10 + 480 + 10 + 480);
}

TEST_CASE("stop empties everything and clears the queue") {
  Rig r;
  r.engine->play(synth(1, 48000, 0));
  r.engine->set_next(synth(2, 100, 0));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() > 0; }));

  r.engine->stop();
  CHECK(r.engine->state() == PlayState::Stopped);
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() == 0; }));
  std::this_thread::sleep_for(50ms);
  CHECK(r.engine->buffered_frames() == 0);  // not refilled by the cleared next item
  for (float s : r.out->pull(100)) REQUIRE(s == 0.f);
  CHECK(r.log.count(T::PlaybackFinished) == 0);
}

TEST_CASE("play replaces what was playing without leaking old audio") {
  Rig r;
  r.engine->play(synth(1, 48000, 0));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 10000; }));
  r.engine->play(synth(2, 5000, 500000));
  std::this_thread::sleep_for(150ms);
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 5000; }));
  auto audio = left(r.out->pull(5000));
  CHECK(audio[0] == 500000.f);
  CHECK(audio[4999] == 500000.f + 4999.f);
}

TEST_CASE("a file that cannot be opened is reported and playback stops") {
  Rig r;
  r.engine->play({TrackId{9}, "missing.flac"});
  REQUIRE(wait_for([&] { return r.log.count(T::Error) > 0; }));
  auto errors = r.log.snapshot();
  auto it = std::find_if(errors.begin(), errors.end(), [](auto& e) { return e.type == T::Error; });
  CHECK(it->item.id == TrackId{9});
  CHECK(it->error.code == ErrorCode::NotFound);
  REQUIRE(wait_for([&] { return r.engine->state() == PlayState::Stopped; }));
}

TEST_CASE("a bad next track is reported; the current one still finishes") {
  Rig r;
  r.engine->play(synth(1, 5000, 0));
  r.engine->set_next(PlayItem{TrackId{2}, "missing.flac"});
  auto audio = left(r.play_to_end());
  CHECK(audio.size() >= 5000);
  CHECK(r.log.count(T::Error) == 1);
  CHECK(r.log.count(T::TrackFinished) == 1);
}

TEST_CASE("an unavailable output device surfaces an error") {
  auto o = std::make_unique<ManualOutput>();
  o->fail_open = true;
  Engine e(std::move(o), synthetic_factory());
  EventLog log;
  log.attach(e);
  e.play(synth(1, 100, 0));
  REQUIRE(wait_for([&] { return log.count(T::Error) > 0; }));
  CHECK(wait_for([&] { return e.state() == PlayState::Stopped; }));
}

TEST_CASE("switching output device keeps the position and the queued track") {
  Rig r;
  r.engine->play(synth(1, 48000, 0));
  r.engine->set_next(synth(2, 1000, 900000));
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 20000; }));
  r.out->pull(24000);  // half a second played
  r.out->pull(1);
  REQUIRE(wait_for([&] { return r.engine->position().has_value(); }));

  r.engine->set_output_device("other");
  REQUIRE(wait_for([&] { return r.out->open_count == 2; }));
  CHECK(r.out->opened_id == "other");
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 1000; }));
  auto resumed = left(r.out->pull(10));
  CHECK(resumed[0] >= 24000.f);  // continues near where it was, not from the start
  CHECK(resumed[0] < 26000.f);
  CHECK(r.engine->state() == PlayState::Playing);

  auto rest = left(r.play_to_end());  // the previously queued track still follows
  CHECK(r.log.count(T::TrackStarted) == 2);
}

TEST_CASE("destroying the engine mid-playback is clean") {
  for (int i = 0; i < 20; ++i) {
    Rig r;
    r.engine->play(synth(1, 480000, 0));
    r.engine->set_next(synth(2, 480000, 0));
    if (i % 2) std::this_thread::sleep_for(3ms);
    r.engine->seek(100ms);
  }
  SUCCEED();
}

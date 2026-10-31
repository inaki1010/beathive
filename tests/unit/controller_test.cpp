#include <catch2/catch_test_macros.hpp>
#include <map>

#include "core/playback/controller.hpp"
#include "playback_fakes.hpp"

using namespace bh;
using namespace bh::playback;
using namespace bh::testutil;

namespace {

constexpr std::size_t kLen = 6000;
constexpr float kSpacing = 100000.f;

// Track id N plays N*kSpacing + frame index. Ids >= 900 fail to open; ids >= 1000 are unknown to the library.
struct Rig {
  // Declared before the engine/controller so they outlive the callbacks that use them.
  std::mutex mu;
  std::vector<std::int64_t> skipped;
  std::atomic<int> changes{0};
  ManualOutput* out;
  std::unique_ptr<Engine> engine;
  std::unique_ptr<PlaybackController> ctl;

  Rig() {
    auto o = std::make_unique<ManualOutput>();
    out = o.get();
    engine = std::make_unique<Engine>(std::move(o), synthetic_factory());
    ctl = std::make_unique<PlaybackController>(*engine, [](TrackId id) -> std::optional<std::filesystem::path> {
      if (id.value >= 1000) return std::nullopt;
      if (id.value >= 900) return "missing.flac";
      return "len=" + std::to_string(kLen) + ",base=" + std::to_string(id.value * static_cast<int>(kSpacing));
    });
    ctl->set_on_changed([this] { ++changes; });
    ctl->set_on_skipped([this](TrackId id, const Error&) {
      std::lock_guard lk(mu);
      skipped.push_back(id.value);
    });
  }

  std::vector<std::int64_t> skipped_ids() {
    std::lock_guard lk(mu);
    return skipped;
  }

  // Pull audio until `done()` holds; returns the left channel.
  template <typename Done>
  std::vector<float> run_until(Done done) {
    std::vector<float> left;
    bool ok = wait_for([&] {
      auto chunk = pull_some(*out, *engine);
      for (std::size_t i = 0; i < chunk.size(); i += 2) left.push_back(chunk[i]);
      return done(left);
    });
    REQUIRE(ok);
    return left;
  }
  std::vector<float> run_to_end() {
    return run_until([&](const std::vector<float>&) { return engine->state() == PlayState::Stopped; });
  }
};

// Run-length decode "which track is this sample from" -> [1,1,1,2,2,3...] collapsed to [1,2,3].
std::vector<int> tracks_in(const std::vector<float>& audio) {
  std::vector<int> seq;
  for (float s : audio) {
    int id = static_cast<int>(s / kSpacing + 0.5f);
    if (s < 0.5f * kSpacing) id = 0;
    if (s == 0.f) continue;  // silence from underruns / the 1-frame ticks
    if (seq.empty() || seq.back() != id) seq.push_back(id);
  }
  return seq;
}

}  // namespace

TEST_CASE("controller plays a queue gapless, end to end") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}, TrackId{3}});
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 2, 3});
  REQUIRE(audio.size() >= 3 * kLen);
  CHECK(audio[kLen - 1] == 1 * kSpacing + (kLen - 1));  // no gap, no overlap at the joins
  CHECK(audio[kLen] == 2 * kSpacing);
  CHECK(audio[2 * kLen] == 3 * kSpacing);

  auto snap = r.ctl->snapshot();
  CHECK(snap.entries.size() == 3);
  CHECK_FALSE(snap.now_playing);  // finished
  CHECK(r.changes > 0);
}

TEST_CASE("now_playing follows the audible track") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}});
  REQUIRE(wait_for([&] { return r.ctl->now_playing().has_value(); }));
  CHECK(r.ctl->now_playing()->track == TrackId{1});

  // Consume track 1 completely; track 2 becomes audible.
  r.run_until([&](const std::vector<float>&) { return r.ctl->now_playing() && r.ctl->now_playing()->track == TrackId{2}; });
  CHECK(r.ctl->now_playing()->track == TrackId{2});
}

TEST_CASE("next and previous") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}, TrackId{3}});
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() > 1000; }));

  r.ctl->next();
  std::this_thread::sleep_for(150ms);
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 100; }));
  CHECK(r.out->pull(1)[0] == 2 * kSpacing);
  CHECK(r.ctl->now_playing()->track == TrackId{2});

  r.ctl->previous();  // under 3 s in: goes to the previous track
  std::this_thread::sleep_for(150ms);
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() >= 100; }));
  CHECK(r.out->pull(1)[0] == 1 * kSpacing);
  CHECK(r.ctl->now_playing()->track == TrackId{1});

  // The rest of the queue still follows.
  auto rest = r.run_to_end();
  CHECK(tracks_in(rest) == std::vector<int>{1, 2, 3});
}

TEST_CASE("unplayable tracks are skipped and reported") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{900}, TrackId{2}, TrackId{1000}, TrackId{3}});
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 2, 3});
  auto skipped = r.skipped_ids();
  CHECK(std::count(skipped.begin(), skipped.end(), 900) >= 1);
  CHECK(std::count(skipped.begin(), skipped.end(), 1000) >= 1);
}

TEST_CASE("starting on an unplayable track moves on to the next") {
  Rig r;
  r.ctl->play_tracks({TrackId{900}, TrackId{2}});
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{2});
}

TEST_CASE("a queue of nothing but unplayable tracks stops cleanly") {
  Rig r;
  r.ctl->play_tracks({TrackId{900}, TrackId{901}});
  REQUIRE(wait_for([&] { return r.skipped_ids().size() >= 2 && r.engine->state() == PlayState::Stopped; }));
  CHECK_FALSE(r.ctl->now_playing());
}

TEST_CASE("repeat all loops around the queue") {
  Rig r;
  r.ctl->set_repeat(RepeatMode::All);
  r.ctl->play_tracks({TrackId{1}, TrackId{2}});
  auto audio = r.run_until([](const std::vector<float>& a) { return a.size() >= 5 * kLen; });
  auto seq = tracks_in(audio);
  REQUIRE(seq.size() >= 5);
  CHECK(std::vector<int>(seq.begin(), seq.begin() + 5) == std::vector<int>{1, 2, 1, 2, 1});
  CHECK(audio[2 * kLen] == 1 * kSpacing);  // second lap starts cleanly at the top of track 1
  r.ctl->stop();
}

TEST_CASE("repeat one keeps replaying the same track") {
  Rig r;
  r.ctl->set_repeat(RepeatMode::One);
  r.ctl->play_tracks({TrackId{1}, TrackId{2}});
  auto audio = r.run_until([](const std::vector<float>& a) { return a.size() >= 3 * kLen; });
  CHECK(tracks_in(audio) == std::vector<int>{1});
  CHECK(audio[kLen] == 1 * kSpacing);
  r.ctl->stop();
}

TEST_CASE("play_next and enqueue take effect while playing") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}});
  REQUIRE(wait_for([&] { return r.ctl->now_playing().has_value(); }));
  r.ctl->play_next({TrackId{7}});
  r.ctl->enqueue({TrackId{8}});
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 7, 2, 8});
}

TEST_CASE("removing the playing track lets it finish and continues with what followed") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}, TrackId{3}});
  REQUIRE(wait_for([&] { return r.ctl->now_playing().has_value(); }));
  r.ctl->remove(r.ctl->now_playing()->entry_id);
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 2, 3});
}

TEST_CASE("removing the upcoming track skips it") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}, TrackId{3}});
  REQUIRE(wait_for([&] { return r.ctl->now_playing().has_value(); }));
  r.ctl->remove(r.ctl->snapshot().entries[1].entry_id);
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 3});
}

TEST_CASE("stop, then play resumes from the queue cursor") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}, TrackId{2}});
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() > 0; }));
  r.ctl->stop();
  CHECK(r.engine->state() == PlayState::Stopped);
  CHECK_FALSE(r.ctl->now_playing());
  r.ctl->toggle_pause();  // stopped -> start again
  CHECK(r.engine->state() == PlayState::Playing);
  auto audio = r.run_to_end();
  CHECK(tracks_in(audio) == std::vector<int>{1, 2});
}

TEST_CASE("toggle_pause pauses and resumes") {
  Rig r;
  r.ctl->play_tracks({TrackId{1}});
  REQUIRE(wait_for([&] { return r.engine->buffered_frames() > 0; }));
  r.ctl->toggle_pause();
  CHECK(r.engine->state() == PlayState::Paused);
  r.ctl->toggle_pause();
  CHECK(r.engine->state() == PlayState::Playing);
}

TEST_CASE("shuffle plays every track exactly once") {
  Rig r;
  r.ctl->set_shuffle(true);
  r.ctl->play_tracks({TrackId{1}, TrackId{2}, TrackId{3}, TrackId{4}, TrackId{5}});
  auto audio = r.run_to_end();
  auto seq = tracks_in(audio);
  std::sort(seq.begin(), seq.end());
  CHECK(seq == std::vector<int>{1, 2, 3, 4, 5});
}

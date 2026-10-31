#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <cmath>

#include "core/playback/decoder.hpp"
#include "fs_test_util.hpp"

using namespace bh;
using namespace bh::playback;
using namespace bh::testutil;
using namespace std::chrono_literals;

namespace {

struct Decoded {
  std::vector<float> samples;
  std::size_t frames;
};

Decoded decode_all(IDecoder& d, int channels) {
  Decoded out;
  std::vector<float> buf(1000 * static_cast<std::size_t>(channels));
  for (;;) {
    auto n = d.read(buf.data(), 1000);
    out.samples.insert(out.samples.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(n) * channels);
    if (n < 1000) break;
  }
  out.frames = out.samples.size() / static_cast<std::size_t>(channels);
  return out;
}

float peak(const std::vector<float>& v) {
  float p = 0;
  for (float x : v) p = std::max(p, std::fabs(x));
  return p;
}

}  // namespace

TEST_CASE("decodes every format at the device rate") {
  for (const char* name : {"sine.mp3", "sine.flac", "sine.opus", "sine.ogg", "sine.m4a"}) {
    DYNAMIC_SECTION(name) {
      auto d = open_ffmpeg_decoder(kDataDir / name, {48000, 2});
      REQUIRE(d);
      auto out = decode_all(**d, 2);
      // 1 second of sine, resampled to 48 kHz. Lossy codecs may add/trim a few ms.
      CHECK(out.frames > 46000);
      CHECK(out.frames < 50500);
      CHECK(peak(out.samples) > 0.05f);
      CHECK(peak(out.samples) <= 1.01f);
      CHECK((**d).duration() > 900ms);
    }
  }
}

TEST_CASE("lossless decode is sample-exact in length and keeps channel conversion sane") {
  auto d = open_ffmpeg_decoder(kDataDir / "sine.flac", {44100, 2});
  REQUIRE(d);
  auto out = decode_all(**d, 2);
  CHECK(out.frames == 44100);  // no resampling needed: exactly one second

  auto mono = open_ffmpeg_decoder(kDataDir / "sine.flac", {44100, 1});
  REQUIRE(mono);
  CHECK(decode_all(**mono, 1).frames == 44100);
}

TEST_CASE("mp3 gapless: encoder delay and padding are trimmed") {
  auto d = open_ffmpeg_decoder(kDataDir / "sine.mp3", {44100, 2});
  REQUIRE(d);
  auto out = decode_all(**d, 2);
  // 1 s of audio. Without trimming an mp3 decodes ~1105 extra frames (delay + padding).
  CHECK(out.frames >= 44100 - 50);
  CHECK(out.frames <= 44100 + 50);
}

TEST_CASE("seeking lands on the requested frame") {
  auto full = open_ffmpeg_decoder(kDataDir / "sine.flac", {44100, 1});
  REQUIRE(full);
  auto all = decode_all(**full, 1);

  auto d = open_ffmpeg_decoder(kDataDir / "sine.flac", {44100, 1});
  REQUIRE(d);
  REQUIRE((**d).seek(500ms));
  auto tail = decode_all(**d, 1);
  CHECK(tail.frames == 44100 - 22050);
  // Content after the seek equals the same region of a straight decode.
  bool same = true;
  for (std::size_t i = 0; i < tail.frames; ++i) same &= std::fabs(tail.samples[i] - all.samples[22050 + i]) < 1e-4f;
  CHECK(same);

  // Seek back to the start and decode again.
  REQUIRE((**d).seek(0ms));
  CHECK(decode_all(**d, 1).frames == 44100);
}

TEST_CASE("bad input yields errors, not crashes") {
  CHECK_FALSE(open_ffmpeg_decoder(kDataDir / "nope.flac", {48000, 2}));
  CHECK_FALSE(open_ffmpeg_decoder(kDataDir / "sine.flac", {0, 2}));
  TempDir t("dec_bad");
  { std::ofstream(t.path / "x.mp3") << "garbage garbage garbage"; }
  CHECK_FALSE(open_ffmpeg_decoder(t.path / "x.mp3", {48000, 2}));
}

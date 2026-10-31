#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/tags/tag_io.hpp"
#include "core/tags/tag_parse.hpp"
#include "fs_test_util.hpp"

using namespace bh;
using namespace bh::tags;
using namespace bh::testutil;
using Catch::Matchers::WithinAbs;
using namespace std::chrono_literals;

TEST_CASE("tag value parsing") {
  CHECK(parse_number_pair("3") == std::pair{std::optional(3), std::optional<int>{}});
  CHECK(parse_number_pair("3/12") == std::pair{std::optional(3), std::optional(12)});
  CHECK(parse_number_pair(" 07 / 0 ") == std::pair{std::optional(7), std::optional<int>{}});
  CHECK(parse_number_pair("x").first == std::nullopt);
  CHECK(parse_number_pair("").first == std::nullopt);

  CHECK(parse_year("2001-05-06") == 2001);
  CHECK(parse_year("1999") == 1999);
  CHECK(parse_year("99") == std::nullopt);
  CHECK(parse_year("0000") == std::nullopt);

  CHECK(*parse_gain_db("-6.50 dB") == -6.5);
  CHECK(*parse_gain_db("+3.10 dB") == 3.1);
  CHECK(*parse_gain_db("2") == 2.0);
  CHECK_FALSE(parse_gain_db("loud"));
  CHECK(*parse_peak("0.98") == 0.98);
  CHECK_FALSE(parse_peak("-1"));
}

TEST_CASE("supported extensions") {
  CHECK(is_supported_extension("a/b/Song.MP3"));
  CHECK(is_supported_extension("x.flac"));
  CHECK_FALSE(is_supported_extension("cover.jpg"));
  CHECK_FALSE(is_supported_extension("noext"));
}

TEST_CASE("reads tags and properties from real files in every format") {
  for (const char* name : {"sine.mp3", "sine.flac", "sine.opus", "sine.ogg", "sine.m4a"}) {
    DYNAMIC_SECTION(name) {
      auto r = read_tags(kDataDir / name);
      REQUIRE(r);
      CHECK(r->tags.title == "Test Song");
      CHECK(r->tags.artists == std::vector<std::string>{"Artist One"});
      CHECK(r->tags.album == "Test Album");
      CHECK(r->tags.album_artists == std::vector<std::string>{"Album Artist"});
      CHECK(r->tags.genres == std::vector<std::string>{"Rock"});
      CHECK(r->tags.composers == std::vector<std::string>{"Composer X"});
      CHECK(r->tags.track_no == 3);
      CHECK(r->tags.track_total == 12);
      CHECK(r->tags.disc_no == 1);
      CHECK(r->tags.year == 2001);
      CHECK(r->audio.duration > 900ms);
      CHECK(r->audio.duration < 1200ms);
      CHECK(r->audio.channels >= 1);
      CHECK(r->audio.sample_rate > 0);
      CHECK_FALSE(r->audio.codec.empty());
    }
  }
}

TEST_CASE("codec, bit depth, replaygain and embedded art") {
  auto mp3 = *read_tags(kDataDir / "sine.mp3", {.want_cover = true});
  CHECK(mp3.audio.codec == "mp3");
  CHECK(mp3.tags.has_embedded_art);
  REQUIRE(mp3.cover);
  CHECK(mp3.cover->data.size() > 100);
  CHECK(mp3.cover->mime_type.find("jpeg") != std::string::npos);
  REQUIRE(mp3.tags.replay_gain.track_gain_db);
  CHECK_THAT(*mp3.tags.replay_gain.track_gain_db, WithinAbs(-6.5, 0.001));
  CHECK_THAT(*mp3.tags.replay_gain.track_peak, WithinAbs(0.987654, 1e-6));

  auto flac = *read_tags(kDataDir / "sine.flac");
  CHECK(flac.audio.codec == "flac");
  CHECK(flac.audio.bit_depth == 16);
  CHECK(flac.tags.has_embedded_art);
  CHECK_FALSE(flac.cover);  // not requested
  REQUIRE(flac.tags.replay_gain.album_gain_db);
  CHECK_THAT(*flac.tags.replay_gain.album_gain_db, WithinAbs(3.1, 0.001));
  CHECK_FALSE(flac.tags.replay_gain.track_gain_db);

  CHECK(read_tags(kDataDir / "sine.opus")->audio.codec == "opus");
  CHECK(read_tags(kDataDir / "sine.ogg")->audio.codec == "vorbis");
  CHECK(read_tags(kDataDir / "sine.m4a")->audio.codec == "aac");
  CHECK_FALSE(read_tags(kDataDir / "sine.ogg")->tags.has_embedded_art);
}

TEST_CASE("unreadable files report an error instead of throwing") {
  TempDir d("tags_bad");
  auto bad = d.path / "broken.mp3";
  { std::ofstream(bad) << "this is not audio"; }
  CHECK_FALSE(read_tags(bad));
  CHECK_FALSE(read_tags(d.path / "nope.flac"));
}

TEST_CASE("write_tags round-trips, including multi-value fields, and keeps the cover") {
  for (const char* name : {"sine.mp3", "sine.flac", "sine.ogg", "sine.m4a"}) {
    DYNAMIC_SECTION(name) {
      TempDir d(std::string("tags_w_") + name);
      auto f = copy_fixture(name, d.path / name);

      auto before = *read_tags(f);
      TagData t = before.tags;
      t.title = "Neue Straße";
      t.artists = {"Björk", "Sigur Rós"};
      t.genres = {"Electronic", "Art Pop"};
      t.year = 1997;
      t.track_no = 5;
      t.track_total = std::nullopt;
      t.comment.clear();
      t.replay_gain.track_gain_db = -4.25;
      REQUIRE(write_tags(f, t));

      auto after = read_tags(f);
      REQUIRE(after);
      CHECK(after->tags.title == "Neue Straße");
      CHECK(after->tags.artists == t.artists);
      CHECK(after->tags.genres == t.genres);
      CHECK(after->tags.year == 1997);
      CHECK(after->tags.track_no == 5);
      CHECK_FALSE(after->tags.track_total);
      CHECK(after->tags.comment.empty());
      CHECK(after->tags.album == "Test Album");  // untouched
      CHECK(after->tags.has_embedded_art == before.tags.has_embedded_art);
      REQUIRE(after->tags.replay_gain.track_gain_db);
      CHECK_THAT(*after->tags.replay_gain.track_gain_db, WithinAbs(-4.25, 0.001));
      CHECK(after->audio.duration == before.audio.duration);
    }
  }
}

TEST_CASE("semicolon-separated values in one frame are split") {
  TempDir d("tags_semi");
  auto f = copy_fixture("sine.flac", d.path / "a.flac");
  TagData t = (*read_tags(f)).tags;
  t.genres = {"Rock; Pop"};  // as another tagger might write it
  REQUIRE(write_tags(f, t));
  CHECK((*read_tags(f)).tags.genres == std::vector<std::string>{"Rock", "Pop"});
}

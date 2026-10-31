#include <catch2/catch_test_macros.hpp>

#include "core/model/track.hpp"
#include "core/util/text.hpp"

using namespace bh;
using namespace std::chrono_literals;

TEST_CASE("split_multi trims and drops empties") {
  CHECK(text::split_multi(" Rock ;Pop;; ") == std::vector<std::string>{"Rock", "Pop"});
  CHECK(text::split_multi("").empty());
  CHECK(text::join({"A", "B"}) == "A; B");
}

TEST_CASE("format_duration") {
  CHECK(text::format_duration(187'000ms) == "3:07");
  CHECK(text::format_duration(3'723'000ms) == "1:02:03");
  CHECK(text::format_duration(-5ms) == "0:00");
}

TEST_CASE("Rating clamps") {
  CHECK(Rating::clamped(9).stars == 5);
  CHECK(Rating::clamped(-1).stars == 0);
}

TEST_CASE("album artist falls back to artist") {
  Track t;
  t.tags.artists = {"Daft Punk"};
  t.tags.album = "Discovery";
  CHECK(album_key_of(t) == AlbumKey{"Daft Punk", "Discovery"});
  t.tags.album_artists = {"Various Artists"};
  CHECK(album_key_of(t).album_artist == "Various Artists");
}

TEST_CASE("TagPatch only touches set fields") {
  TagData d;
  d.title = "Old";
  d.album = "Keep";
  d.year = 1999;

  TagPatch p;
  CHECK(p.empty());
  p.title = "New";
  p.genres = std::vector<std::string>{"Rock"};
  CHECK_FALSE(p.empty());
  p.apply_to(d);

  CHECK(d.title == "New");
  CHECK(d.album == "Keep");
  CHECK(d.year == 1999);
  CHECK(d.genre_display() == "Rock");
}

#pragma once
#include <catch2/catch_test_macros.hpp>

#include "core/db/query.hpp"
#include "core/db/track_repo.hpp"

namespace bh::testutil {

inline db::Database open_db() {
  auto r = db::Database::open_memory();
  REQUIRE(r.has_value());
  return std::move(*r);
}

inline Track make_track(const std::string& path, const std::string& title, std::vector<std::string> artists,
                        const std::string& album, std::vector<std::string> genres = {}, int year = 0) {
  Track t;
  t.file.path = path;
  t.file.size_bytes = 1000;
  t.file.mtime_unix = 1700000000;
  t.tags.title = title;
  t.tags.artists = std::move(artists);
  t.tags.album = album;
  t.tags.genres = std::move(genres);
  if (year) t.tags.year = year;
  t.audio.duration = std::chrono::milliseconds{180'000};
  t.audio.codec = "flac";
  t.audio.bitrate_kbps = 900;
  t.lib.date_added = Timestamp{std::chrono::seconds{1'700'000'000}};
  return t;
}

}  // namespace bh::testutil

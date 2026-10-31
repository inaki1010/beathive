#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/model/ids.hpp"

namespace bh {

using Timestamp = std::chrono::sys_seconds;  // UTC, second resolution

// Star rating, 0 = unrated, 1..5 stars.
struct Rating {
  std::uint8_t stars = 0;
  static constexpr Rating clamped(int v) { return Rating{static_cast<std::uint8_t>(v < 0 ? 0 : v > 5 ? 5 : v)}; }
  friend constexpr bool operator==(Rating, Rating) = default;
};

struct ReplayGain {
  std::optional<double> track_gain_db;
  std::optional<double> track_peak;  // linear, 1.0 = full scale
  std::optional<double> album_gain_db;
  std::optional<double> album_peak;
};

// Immutable facts about the encoded stream. Never edited by the user.
struct AudioProperties {
  std::chrono::milliseconds duration{0};
  int bitrate_kbps = 0;
  int sample_rate = 0;
  int channels = 0;
  int bit_depth = 0;  // 0 = unknown / lossy
  std::string codec;  // "mp3", "flac", "aac", "opus", ...
};

// Fields stored in the file's tags. This is what the tag editor changes.
struct TagData {
  std::string title;
  std::vector<std::string> artists;
  std::string album;
  std::vector<std::string> album_artists;
  std::vector<std::string> genres;
  std::vector<std::string> composers;
  std::string comment;
  std::optional<int> track_no, track_total;
  std::optional<int> disc_no, disc_total;
  std::optional<int> year;
  ReplayGain replay_gain;
  bool has_embedded_art = false;

  // "A; B" display strings.
  std::string artist_display() const;
  std::string album_artist_display() const;  // falls back to artist when empty
  std::string genre_display() const;
};

// Fields only the library knows about (not written to files).
struct LibraryData {
  Rating rating;
  int play_count = 0;
  std::optional<Timestamp> last_played;
  Timestamp date_added{};
};

struct FileInfo {
  std::filesystem::path path;
  FolderId folder;
  std::int64_t size_bytes = 0;
  std::int64_t mtime_unix = 0;  // for change detection during rescans
  bool missing = false;         // file not found at last scan; row kept
};

struct Track {
  TrackId id;
  FileInfo file;
  TagData tags;
  AudioProperties audio;
  LibraryData lib;
  ArtId art;  // 0 = no art
};

// Partial update for batch tag editing: nullopt = leave unchanged.
struct TagPatch {
  std::optional<std::string> title;
  std::optional<std::vector<std::string>> artists;
  std::optional<std::string> album;
  std::optional<std::vector<std::string>> album_artists;
  std::optional<std::vector<std::string>> genres;
  std::optional<std::vector<std::string>> composers;
  std::optional<std::string> comment;
  std::optional<int> track_no, track_total, disc_no, disc_total, year;

  bool empty() const;
  void apply_to(TagData& t) const;
};

// Identity of an album: same album title by the same album artist.
struct AlbumKey {
  std::string album_artist;
  std::string album;
  friend bool operator==(const AlbumKey&, const AlbumKey&) = default;
  friend auto operator<=>(const AlbumKey&, const AlbumKey&) = default;
};
AlbumKey album_key_of(const Track& t);

// Aggregates produced by the library query layer for browse views.
struct AlbumSummary {
  AlbumKey key;
  int track_count = 0;
  std::optional<int> year;
  std::chrono::milliseconds total_duration{0};
  ArtId art;
};

struct ArtistSummary {
  std::string name;
  int album_count = 0;
  int track_count = 0;
};

}  // namespace bh

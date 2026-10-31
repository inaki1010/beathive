#pragma once
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/db/sqlite.hpp"
#include "core/model/playlist.hpp"

namespace bh::db {

using SqlParam = std::variant<std::int64_t, std::string>;

// A WHERE-clause fragment with positional (?) parameters, in order of appearance.
struct SqlFragment {
  std::string sql;
  std::vector<SqlParam> params;
};

// Translates a rule tree to SQL over `track t`. Must agree with bh::matches().
SqlFragment compile_rules(const RuleGroup& g, Timestamp now);

// Turns user input into an FTS5 query: every word becomes a prefix term, AND-ed.
// Returns "" when the input has no searchable characters.
std::string fts_query(std::string_view user_input);

// Cascading filter used by the browse columns, search box and auto-playlists.
// Within a field, values are OR-ed; between fields they are AND-ed.
struct TrackFilter {
  std::vector<std::string> genres;
  std::vector<std::string> artists;
  std::optional<std::string> album_artist;  // together with `album`, identifies one album
  std::optional<std::string> album;
  std::optional<int> year;
  std::string search;
  std::optional<RuleGroup> rules;
  bool include_missing = false;
};

struct TrackSort {
  SortField field = SortField::Title;
  bool descending = false;
};

struct GenreCount {
  std::string name;
  int track_count;
};

struct TrackSummary {
  std::int64_t track_count = 0;
  std::chrono::milliseconds total_duration{0};
};

class Query {
 public:
  explicit Query(Database& db) : db_(db) {}

  // Ordered track ids matching the filter. `now` anchors date rules.
  Result<std::vector<TrackId>> tracks(const TrackFilter& f, TrackSort sort = {},
                                      std::optional<int> limit = std::nullopt,
                                      Timestamp now = std::chrono::floor<std::chrono::seconds>(
                                          std::chrono::system_clock::now()));

  // Browse lists: each applies the filter, so selecting a genre narrows artists, etc.
  Result<std::vector<GenreCount>> genres(const TrackFilter& f);
  Result<std::vector<ArtistSummary>> artists(const TrackFilter& f);
  Result<std::vector<AlbumSummary>> albums(const TrackFilter& f);
  Result<std::vector<int>> years(const TrackFilter& f);  // descending

  Result<TrackSummary> summary(const TrackFilter& f, Timestamp now = std::chrono::floor<std::chrono::seconds>(
                                                                  std::chrono::system_clock::now()));

  // Resolves an auto-playlist to its current tracks.
  Result<std::vector<TrackId>> resolve(const AutoPlaylistSpec& spec, Timestamp now);

 private:
  Database& db_;
};

}  // namespace bh::db

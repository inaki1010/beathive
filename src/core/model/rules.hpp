#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/model/track.hpp"

namespace bh {

enum class RuleField {
  Title, Artist, AlbumArtist, Album, Genre, Composer, Comment, Codec,  // text
  Year, Rating, PlayCount, BitrateKbps, DurationSeconds,               // numeric
  DateAdded, LastPlayed,                                               // date
};

enum class RuleOp {
  Contains, NotContains, Is, IsNot, StartsWith,  // text
  Greater, Less, Equal,                          // numeric
  InLastDays, NotInLastDays,                     // date; value = days
};

struct Condition {
  RuleField field;
  RuleOp op;
  std::variant<std::string, std::int64_t> value;
};

enum class Match { All, Any };

// A tree of conditions: `match` combines conditions and child groups.
// An empty group matches everything.
struct RuleGroup {
  Match match = Match::All;
  std::vector<Condition> conditions;
  std::vector<RuleGroup> children;
};

enum class SortField { Title, Artist, Album, Year, Rating, PlayCount, DateAdded, LastPlayed, Random };

struct AutoPlaylistSpec {
  RuleGroup root;
  SortField sort = SortField::Title;
  bool descending = false;
  std::optional<int> limit;
};

// In-memory evaluation. Used for tests, for re-checking edited tracks, and as
// the reference for the SQL compiler added with the library layer.
bool matches(const Condition& c, const Track& t, Timestamp now);
bool matches(const RuleGroup& g, const Track& t, Timestamp now);

}  // namespace bh

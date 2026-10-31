#include "core/db/rules_json.hpp"

#include <array>
#include <nlohmann/json.hpp>

namespace bh::db {

using nlohmann::json;

namespace {

template <typename E, std::size_t N>
struct EnumNames {
  std::array<std::pair<E, const char*>, N> items;
  const char* name(E e) const {
    for (auto& [v, n] : items) if (v == e) return n;
    return "";
  }
  std::optional<E> parse(const std::string& s) const {
    for (auto& [v, n] : items) if (s == n) return v;
    return std::nullopt;
  }
};

constexpr EnumNames<RuleField, 15> kFields{{{
    {RuleField::Title, "title"}, {RuleField::Artist, "artist"}, {RuleField::AlbumArtist, "album_artist"},
    {RuleField::Album, "album"}, {RuleField::Genre, "genre"}, {RuleField::Composer, "composer"},
    {RuleField::Comment, "comment"}, {RuleField::Codec, "codec"}, {RuleField::Year, "year"},
    {RuleField::Rating, "rating"}, {RuleField::PlayCount, "play_count"}, {RuleField::BitrateKbps, "bitrate_kbps"},
    {RuleField::DurationSeconds, "duration_seconds"}, {RuleField::DateAdded, "date_added"},
    {RuleField::LastPlayed, "last_played"}}}};

constexpr EnumNames<RuleOp, 10> kOps{{{
    {RuleOp::Contains, "contains"}, {RuleOp::NotContains, "not_contains"}, {RuleOp::Is, "is"},
    {RuleOp::IsNot, "is_not"}, {RuleOp::StartsWith, "starts_with"}, {RuleOp::Greater, "greater"},
    {RuleOp::Less, "less"}, {RuleOp::Equal, "equal"}, {RuleOp::InLastDays, "in_last_days"},
    {RuleOp::NotInLastDays, "not_in_last_days"}}}};

constexpr EnumNames<SortField, 9> kSorts{{{
    {SortField::Title, "title"}, {SortField::Artist, "artist"}, {SortField::Album, "album"},
    {SortField::Year, "year"}, {SortField::Rating, "rating"}, {SortField::PlayCount, "play_count"},
    {SortField::DateAdded, "date_added"}, {SortField::LastPlayed, "last_played"}, {SortField::Random, "random"}}}};

json group_to_json(const RuleGroup& g) {
  json conds = json::array();
  for (auto& c : g.conditions) {
    json value = std::visit([](const auto& v) { return json(v); }, c.value);
    conds.push_back({{"field", kFields.name(c.field)}, {"op", kOps.name(c.op)}, {"value", value}});
  }
  json kids = json::array();
  for (auto& k : g.children) kids.push_back(group_to_json(k));
  return {{"match", g.match == Match::All ? "all" : "any"}, {"conditions", conds}, {"groups", kids}};
}

Result<RuleGroup> group_from_json(const json& j) {
  auto bad = [](const char* why) { return std::unexpected(Error{ErrorCode::Corrupt, why}); };
  if (!j.is_object()) return bad("rule group must be an object");
  RuleGroup g;
  g.match = j.value("match", "all") == "any" ? Match::Any : Match::All;
  for (auto& c : j.value("conditions", json::array())) {
    auto f = kFields.parse(c.value("field", ""));
    auto o = kOps.parse(c.value("op", ""));
    if (!f || !o || !c.contains("value")) return bad("invalid rule condition");
    const auto& v = c["value"];
    if (v.is_string()) g.conditions.push_back({*f, *o, v.get<std::string>()});
    else if (v.is_number_integer()) g.conditions.push_back({*f, *o, v.get<std::int64_t>()});
    else return bad("rule value must be a string or integer");
  }
  for (auto& k : j.value("groups", json::array())) {
    auto child = group_from_json(k);
    if (!child) return std::unexpected(child.error());
    g.children.push_back(std::move(*child));
  }
  return g;
}

}  // namespace

std::string rules_to_json(const AutoPlaylistSpec& s) {
  json j = {{"version", 1},
            {"root", group_to_json(s.root)},
            {"sort", kSorts.name(s.sort)},
            {"descending", s.descending},
            {"limit", s.limit ? json(*s.limit) : json(nullptr)}};
  return j.dump();
}

Result<AutoPlaylistSpec> rules_from_json(const std::string& text) {
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object())
    return std::unexpected(Error{ErrorCode::Corrupt, "rules JSON is not valid"});
  if (j.value("version", 0) != 1) return std::unexpected(Error{ErrorCode::Unsupported, "unknown rules version"});

  AutoPlaylistSpec spec;
  auto root = group_from_json(j.value("root", json::object()));
  if (!root) return std::unexpected(root.error());
  spec.root = std::move(*root);
  spec.sort = kSorts.parse(j.value("sort", "title")).value_or(SortField::Title);
  spec.descending = j.value("descending", false);
  if (j.contains("limit") && j["limit"].is_number_integer()) spec.limit = j["limit"].get<int>();
  return spec;
}

}  // namespace bh::db

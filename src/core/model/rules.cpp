#include "core/model/rules.hpp"

#include <algorithm>

#include "core/util/text.hpp"

namespace bh {

namespace {

using namespace std::chrono_literals;

std::vector<std::string> text_values(RuleField f, const Track& t) {
  switch (f) {
    case RuleField::Title: return {t.tags.title};
    case RuleField::Artist: return t.tags.artists;
    case RuleField::AlbumArtist: return {t.tags.album_artist_display()};
    case RuleField::Album: return {t.tags.album};
    case RuleField::Genre: return t.tags.genres;
    case RuleField::Composer: return t.tags.composers;
    case RuleField::Comment: return {t.tags.comment};
    case RuleField::Codec: return {t.audio.codec};
    default: return {};
  }
}

std::optional<std::int64_t> numeric_value(RuleField f, const Track& t) {
  switch (f) {
    case RuleField::Year: return t.tags.year;
    case RuleField::Rating: return t.lib.rating.stars;
    case RuleField::PlayCount: return t.lib.play_count;
    case RuleField::BitrateKbps: return t.audio.bitrate_kbps;
    case RuleField::DurationSeconds: return t.audio.duration.count() / 1000;
    default: return std::nullopt;
  }
}

std::optional<Timestamp> date_value(RuleField f, const Track& t) {
  if (f == RuleField::DateAdded) return t.lib.date_added;
  if (f == RuleField::LastPlayed) return t.lib.last_played;
  return std::nullopt;
}

bool is_text(RuleField f) { return f <= RuleField::Codec; }
bool is_date(RuleField f) { return f == RuleField::DateAdded || f == RuleField::LastPlayed; }

bool match_text(const Condition& c, const Track& t) {
  const auto* needle = std::get_if<std::string>(&c.value);
  if (!needle) return false;
  auto values = text_values(c.field, t);
  if (values.empty()) values.emplace_back();  // so "Is ''" matches an empty tag

  auto any = [&](auto pred) { return std::ranges::any_of(values, pred); };
  switch (c.op) {
    case RuleOp::Contains: return any([&](auto& v) { return text::contains_ci(v, *needle); });
    case RuleOp::NotContains: return !any([&](auto& v) { return text::contains_ci(v, *needle); });
    case RuleOp::Is: return any([&](auto& v) { return text::equals_ci(v, *needle); });
    case RuleOp::IsNot: return !any([&](auto& v) { return text::equals_ci(v, *needle); });
    case RuleOp::StartsWith: return any([&](auto& v) { return text::starts_with_ci(v, *needle); });
    default: return false;
  }
}

bool match_numeric(const Condition& c, const Track& t) {
  const auto* rhs = std::get_if<std::int64_t>(&c.value);
  auto lhs = numeric_value(c.field, t);
  if (!rhs || !lhs) return false;  // missing values never match numeric tests
  switch (c.op) {
    case RuleOp::Greater: return *lhs > *rhs;
    case RuleOp::Less: return *lhs < *rhs;
    case RuleOp::Equal: return *lhs == *rhs;
    default: return false;
  }
}

bool match_date(const Condition& c, const Track& t, Timestamp now) {
  const auto* days = std::get_if<std::int64_t>(&c.value);
  if (!days) return false;
  auto when = date_value(c.field, t);
  bool in_window = when && *when >= now - std::chrono::days{*days} && *when <= now;
  switch (c.op) {
    case RuleOp::InLastDays: return in_window;
    case RuleOp::NotInLastDays: return !in_window;  // includes "never played"
    default: return false;
  }
}

}  // namespace

bool matches(const Condition& c, const Track& t, Timestamp now) {
  if (is_text(c.field)) return match_text(c, t);
  if (is_date(c.field)) return match_date(c, t, now);
  return match_numeric(c, t);
}

bool matches(const RuleGroup& g, const Track& t, Timestamp now) {
  auto cond = [&](const Condition& c) { return matches(c, t, now); };
  auto child = [&](const RuleGroup& r) { return matches(r, t, now); };
  if (g.conditions.empty() && g.children.empty()) return true;
  if (g.match == Match::All)
    return std::ranges::all_of(g.conditions, cond) && std::ranges::all_of(g.children, child);
  return std::ranges::any_of(g.conditions, cond) || std::ranges::any_of(g.children, child);
}

}  // namespace bh

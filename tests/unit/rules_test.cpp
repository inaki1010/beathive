#include <catch2/catch_test_macros.hpp>

#include "core/model/playlist.hpp"

using namespace bh;
using namespace std::chrono;

namespace {
const Timestamp kNow = sys_days{2026y / October / 29};

Track make() {
  Track t;
  t.tags.title = "Around the World";
  t.tags.artists = {"Daft Punk"};
  t.tags.genres = {"Electronic", "House"};
  t.tags.year = 1997;
  t.lib.rating = Rating{4};
  t.lib.play_count = 12;
  t.lib.date_added = kNow - days{10};
  t.audio.codec = "flac";
  return t;
}
Condition text(RuleField f, RuleOp o, std::string v) { return {f, o, std::move(v)}; }
Condition num(RuleField f, RuleOp o, std::int64_t v) { return {f, o, v}; }
}  // namespace

TEST_CASE("text conditions are case-insensitive and multi-value aware") {
  auto t = make();
  CHECK(matches(text(RuleField::Title, RuleOp::Contains, "WORLD"), t, kNow));
  CHECK(matches(text(RuleField::Genre, RuleOp::Is, "house"), t, kNow));
  CHECK_FALSE(matches(text(RuleField::Genre, RuleOp::IsNot, "House"), t, kNow));
  CHECK(matches(text(RuleField::Artist, RuleOp::StartsWith, "daft"), t, kNow));
  CHECK(matches(text(RuleField::Composer, RuleOp::Is, ""), t, kNow));  // empty tag
}

TEST_CASE("numeric conditions; missing values never match") {
  auto t = make();
  CHECK(matches(num(RuleField::Year, RuleOp::Less, 2000), t, kNow));
  CHECK(matches(num(RuleField::Rating, RuleOp::Equal, 4), t, kNow));
  t.tags.year.reset();
  CHECK_FALSE(matches(num(RuleField::Year, RuleOp::Less, 2000), t, kNow));
}

TEST_CASE("date conditions") {
  auto t = make();
  CHECK(matches(num(RuleField::DateAdded, RuleOp::InLastDays, 30), t, kNow));
  CHECK_FALSE(matches(num(RuleField::DateAdded, RuleOp::InLastDays, 5), t, kNow));
  // Never played counts as "not in last N days".
  CHECK(matches(num(RuleField::LastPlayed, RuleOp::NotInLastDays, 30), t, kNow));
  CHECK_FALSE(matches(num(RuleField::LastPlayed, RuleOp::InLastDays, 30), t, kNow));
}

TEST_CASE("groups combine with All/Any and nest") {
  auto t = make();
  RuleGroup g;
  CHECK(matches(g, t, kNow));  // empty matches all

  g.conditions = {text(RuleField::Genre, RuleOp::Is, "Jazz"), num(RuleField::Rating, RuleOp::Greater, 3)};
  g.match = Match::All;
  CHECK_FALSE(matches(g, t, kNow));
  g.match = Match::Any;
  CHECK(matches(g, t, kNow));

  RuleGroup outer;
  outer.conditions = {text(RuleField::Codec, RuleOp::Is, "flac")};
  outer.children = {g};
  CHECK(matches(outer, t, kNow));
  outer.conditions = {text(RuleField::Codec, RuleOp::Is, "mp3")};
  CHECK_FALSE(matches(outer, t, kNow));
}

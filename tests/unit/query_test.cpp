#include <catch2/catch_test_macros.hpp>
#include <random>
#include <set>

#include "db_test_util.hpp"

using namespace bh;
using namespace bh::testutil;
using namespace std::chrono_literals;

namespace {

void seed(db::Database& d) {
  db::TrackRepo repo(d);
  auto add = [&](Track t) { REQUIRE(repo.upsert(t)); };

  auto t1 = make_track("/m/dp/1.flac", "One More Time", {"Daft Punk"}, "Discovery", {"Electronic", "House"}, 2001);
  t1.tags.track_no = 1;
  auto t2 = make_track("/m/dp/2.flac", "Aerodynamic", {"Daft Punk"}, "Discovery", {"Electronic"}, 2001);
  t2.tags.track_no = 2;
  auto t3 = make_track("/m/mj/1.flac", "Thriller", {"Michael Jackson"}, "Thriller", {"Pop"}, 1982);
  auto t4 = make_track("/m/va/1.flac", "Duet", {"Daft Punk", "Michael Jackson"}, "Mashups", {"Pop", "Electronic"}, 2010);
  t4.tags.album_artists = {"Various Artists"};
  add(t1); add(t2); add(t3); add(t4);
}

std::vector<std::string> titles(db::Database& d, const std::vector<TrackId>& ids) {
  std::vector<std::string> out;
  for (auto& t : *db::TrackRepo(d).get_many(ids)) out.push_back(t.tags.title);
  return out;
}

}  // namespace

TEST_CASE("browse lists respect cascading filters") {
  auto d = open_db();
  seed(d);
  db::Query q(d);

  auto genres = q.genres({});
  REQUIRE(genres);
  REQUIRE(genres->size() == 3);  // Electronic, House, Pop
  CHECK((*genres)[0].name == "Electronic");
  CHECK((*genres)[0].track_count == 3);

  db::TrackFilter f;
  f.genres = {"pop"};  // case-insensitive
  auto artists = q.artists(f);
  REQUIRE(artists);
  REQUIRE(artists->size() == 2);  // Daft Punk (via the duet), Michael Jackson
  CHECK((*artists)[0].name == "Daft Punk");
  CHECK((*artists)[0].track_count == 1);

  f = {};
  f.artists = {"Daft Punk"};
  auto albums = q.albums(f);
  REQUIRE(albums);
  REQUIRE(albums->size() == 2);
  CHECK((*albums)[0].key == AlbumKey{"Daft Punk", "Discovery"});
  CHECK((*albums)[0].track_count == 2);
  CHECK((*albums)[0].total_duration == 360'000ms);
  CHECK((*albums)[0].year == 2001);
  CHECK((*albums)[1].key == AlbumKey{"Various Artists", "Mashups"});

  auto years = q.years({});
  REQUIRE(years);
  CHECK(*years == std::vector<int>{2010, 2001, 1982});
}

TEST_CASE("album order is by disc and track number") {
  auto d = open_db();
  seed(d);
  db::Query q(d);
  db::TrackFilter f;
  f.album_artist = "Daft Punk";
  f.album = "Discovery";
  auto ids = q.tracks(f, {SortField::Album, false});
  REQUIRE(ids);
  CHECK(titles(d, *ids) == std::vector<std::string>{"One More Time", "Aerodynamic"});
}

TEST_CASE("sorting and limits") {
  auto d = open_db();
  seed(d);
  db::Query q(d);
  auto asc = q.tracks({}, {SortField::Title, false});
  CHECK(titles(d, *asc) == std::vector<std::string>{"Aerodynamic", "Duet", "One More Time", "Thriller"});
  auto top2 = q.tracks({}, {SortField::Year, true}, 2);
  CHECK(titles(d, *top2) == std::vector<std::string>{"Duet", "One More Time"});  // ties fall back to album/track order
}

TEST_CASE("full-text search matches prefixes across fields and survives odd input") {
  auto d = open_db();
  seed(d);
  db::Query q(d);
  auto search = [&](const char* s) {
    db::TrackFilter f;
    f.search = s;
    return titles(d, *q.tracks(f, {SortField::Title, false}));
  };
  CHECK(search("aero") == std::vector<std::string>{"Aerodynamic"});
  CHECK(search("daft thr").empty());
  CHECK(search("michael jack") == std::vector<std::string>{"Duet", "Thriller"});
  CHECK(search("DISCOV") == std::vector<std::string>{"Aerodynamic", "One More Time"});
  CHECK(search("\"AND OR (*\"").empty());  // syntax characters are neutralised, no error
  CHECK(search("   ").size() == 4);       // blank search = no filter

  // Edits are reflected via triggers.
  db::TrackRepo repo(d);
  auto t = *repo.find_by_path("/m/mj/1.flac");
  auto tags = t->tags;
  tags.title = "Billie Jean";
  REQUIRE(repo.update_tags(t->id, tags));
  CHECK(search("thriller") == std::vector<std::string>{"Billie Jean"});  // album still matches
  CHECK(search("billie") == std::vector<std::string>{"Billie Jean"});
}

TEST_CASE("missing tracks are hidden unless requested") {
  auto d = open_db();
  seed(d);
  db::TrackRepo repo(d);
  REQUIRE(repo.set_missing((*repo.find_by_path("/m/mj/1.flac"))->id, true));
  db::Query q(d);
  CHECK(q.tracks({})->size() == 3);
  db::TrackFilter f;
  f.include_missing = true;
  CHECK(q.tracks(f)->size() == 4);
}

TEST_CASE("fts_query building") {
  CHECK(db::fts_query("hello world") == "\"hello\"* AND \"world\"*");
  CHECK(db::fts_query("  \"x\" -y ") == "\"x\"* AND \"y\"*");
  CHECK(db::fts_query("***").empty());
  CHECK(db::fts_query("björk") == "\"björk\"*");
}

// The SQL compiler and the in-memory evaluator must agree on every rule.
TEST_CASE("compiled SQL rules agree with in-memory evaluation") {
  auto d = open_db();
  db::TrackRepo repo(d);
  const Timestamp now{1'800'000'000s};

  std::mt19937 rng(7);
  auto rn = [&](unsigned n) { return static_cast<int>(rng() % n); };
  auto pick = [&](auto& v) -> auto& { return v[rng() % v.size()]; };
  const std::vector<std::string> words = {"Rock", "Pop", "House", "Jazz", "Daft Punk", "Björk", "x", ""};
  // List-valued tags never contain empty elements (split_multi drops them), so the generator doesn't either.
  const std::vector<std::string> list_words(words.begin(), words.end() - 1);
  std::vector<Track> tracks;
  for (int i = 0; i < 80; ++i) {
    auto t = make_track("/m/" + std::to_string(i) + ".flac", pick(words) + " song", {}, pick(words));
    for (int n = rn(3); n > 0; --n) t.tags.artists.push_back(pick(list_words));
    for (int n = rn(3); n > 0; --n) t.tags.genres.push_back(pick(list_words));
    for (int n = rn(2); n > 0; --n) t.tags.composers.push_back(pick(list_words));
    if (rn(3)) t.tags.album_artists = {pick(list_words)};
    if (rn(4)) t.tags.year = 1980 + static_cast<int>(rn(40));
    t.lib.rating = Rating{static_cast<std::uint8_t>(rn(6))};
    t.lib.play_count = static_cast<int>(rn(20));
    t.audio.duration = std::chrono::milliseconds{static_cast<int>(rn(400'000))};
    t.audio.bitrate_kbps = static_cast<int>(rn(1000));
    t.lib.date_added = now - std::chrono::days{static_cast<int>(rn(100))};
    if (rn(2)) t.lib.last_played = now - std::chrono::days{static_cast<int>(rn(100))};
    REQUIRE(repo.upsert(t));
    tracks.push_back(t);
  }

  using F = RuleField;
  using O = RuleOp;
  std::vector<F> text_fields = {F::Title, F::Artist, F::AlbumArtist, F::Album, F::Genre, F::Composer, F::Codec, F::Comment};
  std::vector<O> text_ops = {O::Contains, O::NotContains, O::Is, O::IsNot, O::StartsWith};
  std::vector<F> num_fields = {F::Year, F::Rating, F::PlayCount, F::BitrateKbps, F::DurationSeconds};
  std::vector<O> num_ops = {O::Greater, O::Less, O::Equal};
  std::vector<F> date_fields = {F::DateAdded, F::LastPlayed};
  std::vector<O> date_ops = {O::InLastDays, O::NotInLastDays};

  auto random_condition = [&]() -> Condition {
    switch (rn(3)) {
      case 0: return {pick(text_fields), pick(text_ops), pick(words)};
      case 1: return {pick(num_fields), pick(num_ops), static_cast<std::int64_t>(rn(30) == 0 ? 3 : (rn(3)) * 1000 / 7 % 2100)};
      default: return {pick(date_fields), pick(date_ops), static_cast<std::int64_t>(rn(100))};
    }
  };

  db::Query q(d);
  for (int round = 0; round < 300; ++round) {
    RuleGroup g;
    g.match = rn(2) ? Match::All : Match::Any;
    for (int n = rn(3) + (rn(8) == 0 ? 0 : 1); n > 0; --n) g.conditions.push_back(random_condition());
    if (rn(3) == 0) {
      RuleGroup child;
      child.match = rn(2) ? Match::All : Match::Any;
      child.conditions = {random_condition(), random_condition()};
      g.children.push_back(child);
    }

    std::set<std::int64_t> expected;
    for (auto& t : tracks)
      if (matches(g, t, now)) expected.insert(t.id.value);

    db::TrackFilter f;
    f.rules = g;
    auto ids = q.tracks(f, {SortField::Title, false}, std::nullopt, now);
    REQUIRE(ids);
    std::set<std::int64_t> actual;
    for (auto id : *ids) actual.insert(id.value);
    INFO("round " << round << " sql: " << db::compile_rules(g, now).sql);
    for (auto id : expected) if (!actual.contains(id)) INFO("missing in SQL: " << id << " " << tracks[static_cast<size_t>(id-1)].file.path.string());
    CHECK(actual == expected);
  }
}

TEST_CASE("summary counts tracks and sums their duration under a filter") {
  auto d = open_db();
  seed(d);
  db::Query q(d);
  auto all = q.summary({});
  REQUIRE(all);
  CHECK(all->track_count == 4);
  CHECK(all->total_duration == 720'000ms);

  db::TrackFilter f;
  f.genres = {"Pop"};
  CHECK(q.summary(f)->track_count == 2);
  f.search = "nonexistentword";
  CHECK(q.summary(f)->track_count == 0);
  CHECK(q.summary(f)->total_duration == 0ms);
}

TEST_CASE("an album tagged with inconsistent capitalisation is one album, and filters find all of it") {
  auto d = open_db();
  db::TrackRepo repo(d);
  const char* spellings[] = {"Hail to the Thief", "Hail To The Thief", "Hail To The Thief", "Hail to the Thief"};
  for (int i = 0; i < 4; ++i) {
    auto t = make_track("/m/htt/" + std::to_string(i) + ".flac", "Song " + std::to_string(i), {"Radiohead"}, spellings[i],
                        {"Alternative"}, 2003);
    t.tags.track_no = i + 1;
    REQUIRE(repo.upsert(t));
  }
  db::Query q(d);

  auto albums = q.albums({});
  REQUIRE(albums);
  REQUIRE(albums->size() == 1);  // grouped case-insensitively...
  CHECK((*albums)[0].track_count == 4);

  // ...so selecting it with whichever spelling the list shows must give every track.
  for (const char* spelling : {"Hail to the Thief", "Hail To The Thief", "HAIL TO THE THIEF"}) {
    db::TrackFilter f;
    f.album_artist = "radiohead";
    f.album = spelling;
    auto ids = q.tracks(f);
    REQUIRE(ids);
    CHECK(ids->size() == 4);
  }

  db::TrackFilter by_artist;
  by_artist.artists = {"RADIOHEAD"};
  by_artist.genres = {"alternative"};
  CHECK(q.tracks(by_artist)->size() == 4);
}

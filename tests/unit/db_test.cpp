#include <catch2/catch_test_macros.hpp>

#include "core/db/folder_repo.hpp"
#include "core/db/playlist_repo.hpp"
#include "core/db/rules_json.hpp"
#include "db_test_util.hpp"

using namespace bh;
using namespace bh::testutil;
using namespace std::chrono_literals;

TEST_CASE("database migrates to the latest version and reopens cleanly") {
  auto dir = std::filesystem::temp_directory_path() / "bh_db_test";
  std::filesystem::create_directories(dir);
  auto file = dir / "lib.db";
  std::filesystem::remove(file);
  {
    auto d = db::Database::open(file);
    REQUIRE(d);
    auto q = d->prepare("PRAGMA user_version");
    REQUIRE(q);
    REQUIRE(q->step());
    CHECK(q->i32(0) == db::latest_schema_version());
  }
  CHECK(db::Database::open(file));  // second open: nothing to migrate
  std::filesystem::remove_all(dir);
}

TEST_CASE("track round-trips through the database") {
  auto d = open_db();
  db::TrackRepo repo(d);

  auto t = make_track("/music/a/Song ü.flac", "Song", {"A", "B"}, "Album", {"Rock", "Pop"}, 2001);
  t.tags.album_artists = {"Various"};
  t.tags.track_no = 3;
  t.tags.replay_gain.track_gain_db = -6.5;
  t.tags.comment = "hi";
  t.lib.rating = Rating{4};
  auto id = repo.upsert(t);
  REQUIRE(id);
  CHECK(t.id == *id);

  auto got = repo.get(*id);
  REQUIRE(got);
  REQUIRE(got->has_value());
  auto& r = **got;
  CHECK(r.file.path == t.file.path);
  CHECK(r.tags.artists == t.tags.artists);
  CHECK(r.tags.genres == t.tags.genres);
  CHECK(r.tags.album_artists == std::vector<std::string>{"Various"});
  CHECK(r.tags.track_no == 3);
  CHECK_FALSE(r.tags.disc_no.has_value());
  CHECK(r.tags.replay_gain.track_gain_db == -6.5);
  CHECK_FALSE(r.tags.replay_gain.album_gain_db.has_value());
  CHECK(r.audio.duration == 180'000ms);
  CHECK(r.lib.rating == Rating{4});
  CHECK(r.lib.date_added == t.lib.date_added);
  CHECK_FALSE(r.lib.last_played.has_value());
}

TEST_CASE("re-upserting a path keeps library data but refreshes tags") {
  auto d = open_db();
  db::TrackRepo repo(d);
  auto t = make_track("/m/x.mp3", "Old", {"A"}, "Al");
  REQUIRE(repo.upsert(t));
  REQUIRE(repo.set_rating(t.id, Rating{5}));
  REQUIRE(repo.record_play(t.id, Timestamp{1'800'000'000s}));

  auto again = make_track("/m/x.mp3", "New", {"A"}, "Al");
  auto id2 = repo.upsert(again);
  REQUIRE(id2);
  CHECK(*id2 == t.id);
  CHECK(*repo.count() == 1);

  auto got = **repo.get(t.id);
  CHECK(got.tags.title == "New");
  CHECK(got.lib.rating == Rating{5});
  CHECK(got.lib.play_count == 1);
  CHECK(got.lib.last_played == Timestamp{1'800'000'000s});
}

TEST_CASE("update_tags, missing flags, purge and stamps") {
  auto d = open_db();
  db::TrackRepo repo(d);
  db::FolderRepo folders(d);
  auto fid = folders.add({{}, "/m", true, true});
  REQUIRE(fid);

  auto t = make_track("/m/a.flac", "A", {"X"}, "Al");
  t.file.folder = *fid;
  REQUIRE(repo.upsert(t));

  TagData edited = t.tags;
  edited.title = "Edited";
  REQUIRE(repo.update_tags(t.id, edited));
  CHECK((**repo.get(t.id)).tags.title == "Edited");
  CHECK_FALSE(repo.update_tags(TrackId{999}, edited));

  auto stamps = repo.stamps(*fid);
  REQUIRE(stamps);
  REQUIRE(stamps->size() == 1);
  CHECK((*stamps)[0].mtime_unix == 1700000000);

  REQUIRE(repo.set_missing(t.id, true));
  CHECK(*repo.purge_missing() == 1);
  CHECK(*repo.count() == 0);
}

TEST_CASE("removing a folder removes its tracks") {
  auto d = open_db();
  db::TrackRepo repo(d);
  db::FolderRepo folders(d);
  auto fid = *folders.add({{}, "/m", true, true});
  CHECK(*folders.add({{}, "/m", false, true}) == fid);  // same path -> same id
  CHECK_FALSE((*folders.list())[0].recursive);

  auto t = make_track("/m/a.flac", "A", {"X"}, "Al");
  t.file.folder = fid;
  REQUIRE(repo.upsert(t));
  REQUIRE(folders.remove(fid));
  CHECK(*repo.count() == 0);
}

TEST_CASE("manual playlists keep order and duplicates; deleting a track drops its entries") {
  auto d = open_db();
  db::TrackRepo repo(d);
  db::PlaylistRepo pl(d);
  auto a = make_track("/m/a.flac", "A", {"X"}, "Al");
  auto b = make_track("/m/b.flac", "B", {"X"}, "Al");
  REQUIRE(repo.upsert(a));
  REQUIRE(repo.upsert(b));

  auto id = pl.create_manual("Mix");
  REQUIRE(id);
  REQUIRE(pl.set_items(*id, {a.id, b.id, a.id}));
  REQUIRE(pl.append_items(*id, {b.id}));
  auto got = pl.get(*id);
  REQUIRE(got);
  CHECK((*got)->items == std::vector<TrackId>{a.id, b.id, a.id, b.id});

  REQUIRE(repo.remove(a.id));
  CHECK((**pl.get(*id)).items == std::vector<TrackId>{b.id, b.id});

  REQUIRE(pl.rename(*id, "Renamed"));
  CHECK((*pl.list())[0].name == "Renamed");
  REQUIRE(pl.remove(*id));
  CHECK_FALSE((*pl.get(*id)).has_value());
}

TEST_CASE("rules JSON round-trips and rejects garbage") {
  AutoPlaylistSpec s;
  s.root.match = Match::Any;
  s.root.conditions = {{RuleField::Genre, RuleOp::Is, std::string("Rock")},
                       {RuleField::Rating, RuleOp::Greater, std::int64_t{3}}};
  s.root.children.push_back({Match::All, {{RuleField::DateAdded, RuleOp::InLastDays, std::int64_t{30}}}, {}});
  s.sort = SortField::PlayCount;
  s.descending = true;
  s.limit = 50;

  auto back = db::rules_from_json(db::rules_to_json(s));
  REQUIRE(back);
  CHECK(back->root.match == Match::Any);
  REQUIRE(back->root.conditions.size() == 2);
  CHECK(std::get<std::string>(back->root.conditions[0].value) == "Rock");
  CHECK(std::get<std::int64_t>(back->root.conditions[1].value) == 3);
  REQUIRE(back->root.children.size() == 1);
  CHECK(back->root.children[0].conditions[0].op == RuleOp::InLastDays);
  CHECK(back->sort == SortField::PlayCount);
  CHECK(back->descending);
  CHECK(back->limit == 50);

  CHECK_FALSE(db::rules_from_json("not json"));
  CHECK_FALSE(db::rules_from_json(R"({"version":1,"root":{"conditions":[{"field":"nope","op":"is","value":"x"}]}})"));
}

TEST_CASE("auto playlist persists and resolves") {
  auto d = open_db();
  db::TrackRepo repo(d);
  db::PlaylistRepo pl(d);
  auto a = make_track("/m/a.flac", "A", {"X"}, "Al", {"Rock"});
  auto b = make_track("/m/b.flac", "B", {"X"}, "Al", {"Jazz"});
  REQUIRE(repo.upsert(a));
  REQUIRE(repo.upsert(b));

  AutoPlaylistSpec s;
  s.root.conditions = {{RuleField::Genre, RuleOp::Is, std::string("jazz")}};
  auto id = pl.create_auto("Jazz", s);
  REQUIRE(id);
  auto p = pl.get(*id);
  REQUIRE(p);
  REQUIRE((*p)->rules.has_value());

  db::Query q(d);
  auto ids = q.resolve(*(*p)->rules, Timestamp{1'800'000'000s});
  REQUIRE(ids);
  CHECK(*ids == std::vector<TrackId>{b.id});
}

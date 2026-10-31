#include <catch2/catch_test_macros.hpp>
#include <fstream>

#include "core/db/art_repo.hpp"
#include "core/db/folder_repo.hpp"
#include "core/db/track_repo.hpp"
#include "core/library/scanner.hpp"
#include "core/tags/tag_io.hpp"
#include "db_test_util.hpp"
#include "fs_test_util.hpp"

using namespace bh;
using namespace bh::testutil;
using namespace std::chrono_literals;

namespace {

struct Fixture {
  TempDir dir;
  db::Database db = open_db();
  WatchedFolder folder;

  explicit Fixture(const std::string& name) : dir(name) {
    folder.path = dir.path;
    folder.id = *db::FolderRepo(db).add(folder);
  }
  library::Scanner scanner() { return library::Scanner(db); }
  library::ScanReport scan(library::ScanOptions o = {}) {
    auto r = library::Scanner(db).scan(folder, o);
    REQUIRE(r);
    return std::move(*r);
  }
  std::int64_t count() { return *db::TrackRepo(db).count(); }
};

// Bump mtime so change detection sees the edit even within the same second.
void touch_later(const std::filesystem::path& p) {
  std::filesystem::last_write_time(p, std::filesystem::last_write_time(p) + 10s);
}

}  // namespace

TEST_CASE("first scan imports supported files recursively and ignores the rest") {
  Fixture f("scan_basic");
  copy_fixture("sine.mp3", f.dir.path / "a/one.mp3");
  copy_fixture("sine.flac", f.dir.path / "a/b/two.flac");
  copy_fixture("sine.opus", f.dir.path / "three.opus");
  std::ofstream(f.dir.path / "notes.txt") << "hi";
  copy_fixture("sine.mp3", f.dir.path / ".hidden/skip.mp3");

  std::vector<library::ScanProgress::Phase> phases;
  auto r = library::Scanner(f.db).scan(f.folder, {}, [&](auto& p) { phases.push_back(p.phase); });
  REQUIRE(r);
  CHECK(r->added == 3);
  CHECK(r->updated == 0);
  CHECK(r->errors.empty());
  CHECK(f.count() == 3);
  CHECK(phases.front() == library::ScanProgress::Phase::Reading);
  CHECK(phases.back() == library::ScanProgress::Phase::Finishing);

  auto t = *db::TrackRepo(f.db).find_by_path(f.dir.path / "a/one.mp3");
  REQUIRE(t);
  CHECK(t->tags.title == "Test Song");
  CHECK(t->audio.codec == "mp3");
  CHECK(t->file.folder == f.folder.id);
  CHECK(t->file.size_bytes > 0);
  CHECK(t->lib.date_added.time_since_epoch().count() > 1'700'000'000);
  CHECK(t->art);
}

TEST_CASE("rescans are incremental") {
  Fixture f("scan_incr");
  auto a = copy_fixture("sine.mp3", f.dir.path / "a.mp3");
  copy_fixture("sine.flac", f.dir.path / "b.flac");
  f.scan();

  db::TrackRepo repo(f.db);
  auto id = (*repo.find_by_path(a))->id;
  REQUIRE(repo.set_rating(id, Rating{5}));

  auto again = f.scan();
  CHECK(again.added == 0);
  CHECK(again.updated == 0);
  CHECK(again.unchanged == 2);

  // Edit a file's tags: only that file is re-read, and the rating survives.
  auto tags = (*tags::read_tags(a)).tags;
  tags.title = "Edited";
  REQUIRE(tags::write_tags(a, tags));
  touch_later(a);
  auto third = f.scan();
  CHECK(third.updated == 1);
  CHECK(third.unchanged == 1);
  auto t = **repo.find_by_path(a);
  CHECK(t.tags.title == "Edited");
  CHECK(t.lib.rating == Rating{5});
  CHECK(t.id == id);

  copy_fixture("sine.ogg", f.dir.path / "c.ogg");
  CHECK(f.scan().added == 1);
  CHECK(f.count() == 3);
}

TEST_CASE("deleted files are flagged missing, and restored when they return") {
  Fixture f("scan_missing");
  auto a = copy_fixture("sine.mp3", f.dir.path / "a.mp3");
  copy_fixture("sine.flac", f.dir.path / "b.flac");
  f.scan();
  db::TrackRepo repo(f.db);
  auto id = (*repo.find_by_path(a))->id;
  REQUIRE(repo.set_rating(id, Rating{4}));

  auto backup = f.dir.path.parent_path() / "bh_scan_missing_backup.mp3";
  std::filesystem::rename(a, backup);
  auto r = f.scan();
  CHECK(r.newly_missing == 1);
  CHECK(f.count() == 2);  // row kept
  CHECK((**repo.get(id)).file.missing);
  CHECK(db::Query(f.db).tracks({})->size() == 1);  // hidden from browsing

  CHECK(f.scan().newly_missing == 0);  // not reported twice

  auto stamp = std::filesystem::last_write_time(backup);
  std::filesystem::rename(backup, a);
  std::filesystem::last_write_time(a, stamp);
  auto back = f.scan();
  CHECK(back.restored == 1);
  CHECK(back.added == 0);
  auto t = **repo.get(id);
  CHECK_FALSE(t.file.missing);
  CHECK(t.lib.rating == Rating{4});
}

TEST_CASE("a missing root changes nothing") {
  Fixture f("scan_root_gone");
  copy_fixture("sine.mp3", f.dir.path / "a.mp3");
  f.scan();

  WatchedFolder gone = f.folder;
  gone.path = f.dir.path / "unmounted";
  auto r = library::Scanner(f.db).scan(gone);
  REQUIRE_FALSE(r);
  CHECK(r.error().code == ErrorCode::NotFound);
  CHECK(db::Query(f.db).tracks({})->size() == 1);
}

TEST_CASE("broken files are reported and skipped") {
  Fixture f("scan_errors");
  copy_fixture("sine.mp3", f.dir.path / "good.mp3");
  std::ofstream(f.dir.path / "bad.mp3") << "not audio at all";
  auto r = f.scan();
  CHECK(r.added == 1);
  REQUIRE(r.errors.size() == 1);
  CHECK(r.errors[0].path.filename() == "bad.mp3");
}

TEST_CASE("non-recursive folders ignore subdirectories") {
  Fixture f("scan_flat");
  f.folder.recursive = false;
  copy_fixture("sine.mp3", f.dir.path / "top.mp3");
  copy_fixture("sine.mp3", f.dir.path / "sub/deep.mp3");
  CHECK(f.scan().added == 1);
}

TEST_CASE("cover art: embedded images are shared, folder images are the fallback") {
  Fixture f("scan_art");
  copy_fixture("sine.mp3", f.dir.path / "x/one.mp3");
  copy_fixture("sine.flac", f.dir.path / "x/two.flac");   // same embedded image -> same art row
  copy_fixture("sine.ogg", f.dir.path / "y/three.ogg");   // no embedded art
  copy_fixture("sine.opus", f.dir.path / "z/four.opus");  // no embedded art, no folder image
  write_image(f.dir.path / "y/Cover.JPG", 'a');
  f.scan();

  db::TrackRepo repo(f.db);
  auto one = **repo.find_by_path(f.dir.path / "x/one.mp3");
  auto two = **repo.find_by_path(f.dir.path / "x/two.flac");
  auto three = **repo.find_by_path(f.dir.path / "y/three.ogg");
  auto four = **repo.find_by_path(f.dir.path / "z/four.opus");
  CHECK(one.art);
  CHECK(one.art == two.art);
  CHECK(three.art);
  CHECK(three.art != one.art);
  CHECK_FALSE(four.art);

  db::ArtRepo art(f.db);
  CHECK((*art.source(one.art)).value().starts_with("embedded:"));
  CHECK((*art.source(three.art)).value().starts_with("file:"));
}

TEST_CASE("cancellation stops early and does not flag anything missing") {
  Fixture f("scan_cancel");
  copy_fixture("sine.mp3", f.dir.path / "a.mp3");
  f.scan();

  std::filesystem::remove(f.dir.path / "a.mp3");
  std::stop_source src;
  src.request_stop();
  auto r = library::Scanner(f.db).scan(f.folder, {}, {}, src.get_token());
  REQUIRE(r);
  CHECK(r->cancelled);
  CHECK(r->newly_missing == 0);
  CHECK_FALSE((*db::TrackRepo(f.db).find_by_path(f.dir.path / "a.mp3"))->file.missing);
}

TEST_CASE("many files across several chunks and threads") {
  Fixture f("scan_many");
  for (int i = 0; i < 60; ++i) copy_fixture("sine.ogg", f.dir.path / ("d" + std::to_string(i % 5)) / (std::to_string(i) + ".ogg"));
  auto r = f.scan({.threads = 4, .chunk_size = 16});
  CHECK(r.added == 60);
  CHECK(r.errors.empty());
  CHECK(f.count() == 60);
}

TEST_CASE("a cover added after the first scan reaches tracks that were not re-read") {
  Fixture f("scan_late_cover");
  copy_fixture("sine.ogg", f.dir.path / "album/one.ogg");   // no embedded art
  copy_fixture("sine.opus", f.dir.path / "album/two.opus");
  f.scan();

  db::TrackRepo repo(f.db);
  auto one = [&] { return **repo.find_by_path(f.dir.path / "album/one.ogg"); };
  auto two = [&] { return **repo.find_by_path(f.dir.path / "album/two.opus"); };
  CHECK_FALSE(one().art);

  write_image(f.dir.path / "album/Folder.jpg", 'x');
  auto r = f.scan();
  CHECK(r.unchanged == 2);        // the audio files really were skipped...
  CHECK(r.covers_updated == 2);   // ...but they picked up the new cover
  CHECK(one().art);
  CHECK(one().art == two().art);
  auto first = one().art;

  CHECK(f.scan().covers_updated == 0);  // nothing changed: nothing to do

  // Replacing the image (new content, new mtime) switches the tracks to the new art row.
  write_image(f.dir.path / "album/Folder.jpg", 'y');
  touch_later(f.dir.path / "album/Folder.jpg");
  CHECK(f.scan().covers_updated == 2);
  CHECK(one().art);
  CHECK(one().art != first);

  // Removing it clears the art again.
  std::filesystem::remove(f.dir.path / "album/Folder.jpg");
  CHECK(f.scan().covers_updated == 2);
  CHECK_FALSE(one().art);
}

TEST_CASE("multi-disc albums share the cover that sits next to the disc folders") {
  Fixture f("scan_discs");
  copy_fixture("sine.ogg", f.dir.path / "Album/CD1/a.ogg");
  copy_fixture("sine.ogg", f.dir.path / "Album/CD2/b.ogg");
  write_image(f.dir.path / "Album/cover.png", 'c');
  f.scan();

  db::TrackRepo repo(f.db);
  auto a = **repo.find_by_path(f.dir.path / "Album/CD1/a.ogg");
  auto b = **repo.find_by_path(f.dir.path / "Album/CD2/b.ogg");
  CHECK(a.art);
  CHECK(a.art == b.art);
}

TEST_CASE("embedded art wins over a folder image, and is not replaced by one") {
  Fixture f("scan_embedded_wins");
  copy_fixture("sine.flac", f.dir.path / "x/a.flac");  // has an embedded picture
  f.scan();
  db::TrackRepo repo(f.db);
  auto embedded = (**repo.find_by_path(f.dir.path / "x/a.flac")).art;
  REQUIRE(embedded);

  write_image(f.dir.path / "x/cover.jpg", 'z');
  CHECK(f.scan().covers_updated == 0);
  CHECK((**repo.find_by_path(f.dir.path / "x/a.flac")).art == embedded);
}

TEST_CASE("identical cover images in different folders share one art row without re-reading") {
  Fixture f("scan_dupe_covers");
  copy_fixture("sine.ogg", f.dir.path / "A/one.ogg");
  copy_fixture("sine.ogg", f.dir.path / "B/two.ogg");
  write_image(f.dir.path / "A/cover.jpg", 'q');
  write_image(f.dir.path / "B/cover.jpg", 'q');
  f.scan();
  db::TrackRepo repo(f.db);
  CHECK((**repo.find_by_path(f.dir.path / "A/one.ogg")).art == (**repo.find_by_path(f.dir.path / "B/two.ogg")).art);
  CHECK(f.scan().covers_updated == 0);
}

#include <catch2/catch_test_macros.hpp>

#include "core/library/cover_finder.hpp"
#include "fs_test_util.hpp"

using namespace bh;
using namespace bh::library;
using namespace bh::testutil;

namespace {

// Creates the named image files (plus an audio file) in a fresh album folder and returns the pick.
std::string pick(std::initializer_list<const char*> files, const char* subdir = "album") {
  static int n = 0;
  TempDir d("cover_" + std::to_string(++n));
  auto dir = d.path / subdir;
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "01 - song.mp3") << "audio";
  char seed = 'a';
  for (auto* f : files) write_image(dir / f, seed++);
  auto found = find_cover_image(dir);
  return found ? found->filename().string() : std::string{};
}

}  // namespace

TEST_CASE("common cover names are recognised case-insensitively, with any spacing") {
  CHECK(pick({"cover.jpg"}) == "cover.jpg");
  CHECK(pick({"Folder.JPG"}) == "Folder.JPG");
  CHECK(pick({"FRONT.png"}) == "FRONT.png");
  CHECK(pick({"Cover Art.jpg"}) == "Cover Art.jpg");
  CHECK(pick({"cover_art.jpeg"}) == "cover_art.jpeg");
  CHECK(pick({"album.webp"}) == "album.webp");
  CHECK(pick({"Front Cover.png"}) == "Front Cover.png");
  CHECK(pick({"Artist - Album (front).jpg"}) == "Artist - Album (front).jpg");
}

TEST_CASE("an image named after the album is used when nothing else is labelled") {
  CHECK(pick({"Discovery.jpg"}) == "Discovery.jpg");  // "disc"/"cover" inside a word must not disqualify it
  CHECK(pick({"OK Computer.png"}) == "OK Computer.png");
}

TEST_CASE("the front cover beats other artwork in the same folder") {
  CHECK(pick({"back.jpg", "cover.jpg", "cd.jpg"}) == "cover.jpg");
  CHECK(pick({"Back Cover.jpg", "Front Cover.jpg"}) == "Front Cover.jpg");
  CHECK(pick({"inlay.jpg", "booklet.jpg", "folder.jpg"}) == "folder.jpg");
  CHECK(pick({"cover.jpg", "folder.jpg", "front.jpg"}) == "cover.jpg");
  CHECK(pick({"BackCover.jpg", "FrontCover.jpg"}) == "FrontCover.jpg");
}

TEST_CASE("Windows Media Player names: the large image wins and the small thumbnail is a last resort") {
  CHECK(pick({"AlbumArtSmall.jpg", "Folder.jpg"}) == "Folder.jpg");
  CHECK(pick({"AlbumArt_{3A4B-1C2D}_Small.jpg", "AlbumArt_{3A4B-1C2D}_Large.jpg"}) == "AlbumArt_{3A4B-1C2D}_Large.jpg");
  CHECK(pick({"AlbumArtSmall.jpg"}) == "AlbumArtSmall.jpg");  // better than nothing
}

TEST_CASE("only artwork that is clearly not the front is ignored") {
  CHECK(pick({"back.jpg"}).empty());
  CHECK(pick({"cd.png", "inlay.jpg"}).empty());
  CHECK(pick({"scan01.jpg"}).empty());
}

TEST_CASE("unlabelled images are a fallback; several of them resolve to the first by name") {
  CHECK(pick({"IMG_0042.jpg"}) == "IMG_0042.jpg");
  CHECK(pick({"02.jpg", "01.jpg", "03.jpg"}) == "01.jpg");
}

TEST_CASE("non-images and tiny placeholders are not covers") {
  TempDir d("cover_nonimage");
  std::ofstream(d.path / "cover.txt") << "text";
  std::ofstream(d.path / "cover.jpg.bak") << "x";
  write_image(d.path / "folder.jpg", 'a', 100);  // 100 bytes: a placeholder
  CHECK_FALSE(find_cover_image(d.path));
  write_image(d.path / "cover.jpg", 'a');
  CHECK(find_cover_image(d.path)->filename() == "cover.jpg");
}

TEST_CASE("disc folders fall back to the album folder; ordinary subfolders do not") {
  for (const char* disc : {"CD1", "CD 2", "Disc 1", "disc2 - Bonus", "Disk3", "DVD1"}) {
    DYNAMIC_SECTION(disc) {
      TempDir d(std::string("cover_disc_") + disc);
      write_image(d.path / "Album/cover.jpg", 'a');
      std::filesystem::create_directories(d.path / "Album" / disc);
      auto found = find_cover_image(d.path / "Album" / disc);
      REQUIRE(found);
      CHECK(found->parent_path() == d.path / "Album");
    }
  }
  TempDir d("cover_subfolder");
  write_image(d.path / "Artist/cover.jpg", 'a');
  std::filesystem::create_directories(d.path / "Artist/Album Name");
  CHECK_FALSE(find_cover_image(d.path / "Artist/Album Name"));  // the artist folder's image isn't this album's

  // The disc folder's own image takes precedence over the parent's.
  TempDir e("cover_disc_own");
  write_image(e.path / "Album/cover.jpg", 'a');
  write_image(e.path / "Album/CD1/folder.jpg", 'b');
  CHECK(find_cover_image(e.path / "Album/CD1")->parent_path() == e.path / "Album/CD1");
}

TEST_CASE("a missing directory yields nothing") {
  CHECK_FALSE(find_cover_image("/nonexistent/dir/for/sure"));
}

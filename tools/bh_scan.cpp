// Developer tool: scan a music folder into a library database and print a summary.
//   bh_scan <library.db> <music-folder> [--search text]
#include <chrono>
#include <cstdio>
#include <string>

#include "core/db/folder_repo.hpp"
#include "core/db/query.hpp"
#include "core/db/track_repo.hpp"
#include "core/library/scanner.hpp"

using namespace bh;

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <library.db> <music-folder> [--search text]\n", argv[0]);
    return 2;
  }
  auto db = db::Database::open(argv[1]);
  if (!db) { std::fprintf(stderr, "db: %s\n", db.error().message.c_str()); return 1; }

  WatchedFolder folder{{}, std::filesystem::absolute(argv[2]), true, true};
  auto id = db::FolderRepo(*db).add(folder);
  if (!id) { std::fprintf(stderr, "folder: %s\n", id.error().message.c_str()); return 1; }
  folder.id = *id;

  auto t0 = std::chrono::steady_clock::now();
  auto report = library::Scanner(*db).scan(folder, {}, [](const library::ScanProgress& p) {
    if (p.phase == library::ScanProgress::Phase::Reading && p.files_to_read)
      std::fprintf(stderr, "\rreading %zu/%zu", p.files_done, p.files_to_read);
  });
  std::fprintf(stderr, "\n");
  if (!report) { std::fprintf(stderr, "scan: %s\n", report.error().message.c_str()); return 1; }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

  std::printf("added %zu, updated %zu, unchanged %zu, missing %zu, restored %zu, covers %zu, errors %zu  (%.2fs, %.0f files/s)\n",
              report->added, report->updated, report->unchanged, report->newly_missing, report->restored,
              report->covers_updated, report->errors.size(), secs, double(report->added + report->updated) / (secs > 0 ? secs : 1));
  for (std::size_t i = 0; i < report->errors.size() && i < 10; ++i)
    std::printf("  ! %s: %s\n", report->errors[i].path.string().c_str(), report->errors[i].message.c_str());

  db::Query q(*db);
  std::printf("tracks in library: %lld\n", static_cast<long long>(*db::TrackRepo(*db).count()));
  if (argc >= 5 && std::string(argv[3]) == "--search") {
    db::TrackFilter f;
    f.search = argv[4];
    auto ids = q.tracks(f, {SortField::Artist, false}, 20);
    if (ids)
      for (auto& t : *db::TrackRepo(*db).get_many(*ids))
        std::printf("  %s - %s [%s]\n", t.tags.artist_display().c_str(), t.tags.title.c_str(), t.tags.album.c_str());
  }
}

#include "core/db/folder_repo.hpp"

#include "core/util/paths.hpp"

namespace bh::db {

Result<FolderId> FolderRepo::add(const WatchedFolder& f) {
  auto st = db_.prepare(
      "INSERT INTO folder (path, recursive, watch_live) VALUES (?, ?, ?) "
      "ON CONFLICT(path) DO UPDATE SET recursive = excluded.recursive, watch_live = excluded.watch_live "
      "RETURNING id");
  BH_TRY(st);
  auto p = to_utf8(f.path);
  st->bind(1, std::string_view(p)).bind(2, f.recursive ? 1 : 0).bind(3, f.watch_live ? 1 : 0);
  auto row = st->step();
  BH_TRY(row);
  auto id = FolderId{st->i64(0)};
  BH_TRY(st->run());  // drain so the statement completes
  return id;
}

Result<std::vector<WatchedFolder>> FolderRepo::list() {
  auto st = db_.prepare("SELECT id, path, recursive, watch_live FROM folder ORDER BY path");
  BH_TRY(st);
  std::vector<WatchedFolder> out;
  for (;;) {
    auto row = st->step();
    BH_TRY(row);
    if (!*row) break;
    out.push_back({FolderId{st->i64(0)}, path_from_utf8(st->text(1)), st->i64(2) != 0, st->i64(3) != 0});
  }
  return out;
}

Result<void> FolderRepo::remove(FolderId id) {
  auto st = db_.prepare("DELETE FROM folder WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  return st->run();
}

}  // namespace bh::db

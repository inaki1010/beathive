#pragma once
#include <vector>

#include "core/db/sqlite.hpp"
#include "core/model/library.hpp"

namespace bh::db {

class FolderRepo {
 public:
  explicit FolderRepo(Database& db) : db_(db) {}
  // Adding an existing path returns its id and updates the options.
  Result<FolderId> add(const WatchedFolder& f);
  Result<std::vector<WatchedFolder>> list();
  // Also deletes the folder's tracks (ON DELETE CASCADE).
  Result<void> remove(FolderId id);

 private:
  Database& db_;
};

}  // namespace bh::db

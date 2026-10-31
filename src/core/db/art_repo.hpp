#pragma once
#include <optional>
#include <string>

#include "core/db/sqlite.hpp"
#include "core/model/ids.hpp"

namespace bh::db {

class ArtRepo {
 public:
  explicit ArtRepo(Database& db) : db_(db) {}
  // Identical images (same hash) share one row; the first source seen is kept.
  Result<ArtId> get_or_create(const std::string& hash, const std::string& source);
  // Image-file bookkeeping: an unchanged file (same size and mtime) maps straight to its art row.
  Result<std::optional<ArtId>> find_file(const std::string& utf8_path, std::int64_t size, std::int64_t mtime);
  Result<void> remember_file(const std::string& utf8_path, std::int64_t size, std::int64_t mtime, ArtId art);

  Result<std::optional<std::string>> source(ArtId id);
  Result<std::optional<std::string>> hash(ArtId id);

 private:
  Database& db_;
};

}  // namespace bh::db

#include "core/db/art_repo.hpp"

namespace bh::db {

Result<ArtId> ArtRepo::get_or_create(const std::string& hash, const std::string& source) {
  auto st = db_.prepare(
      "INSERT INTO art (hash, source) VALUES (?, ?) "
      "ON CONFLICT(hash) DO UPDATE SET source = COALESCE(art.source, excluded.source) RETURNING id");
  BH_TRY(st);
  st->bind(1, std::string_view(hash)).bind(2, std::string_view(source));
  BH_TRY(st->step());
  auto id = ArtId{st->i64(0)};
  BH_TRY(st->run());
  return id;
}

Result<std::optional<ArtId>> ArtRepo::find_file(const std::string& path, std::int64_t size, std::int64_t mtime) {
  auto st = db_.prepare("SELECT art_id FROM art_file WHERE path = ? AND size = ? AND mtime = ?");
  BH_TRY(st);
  st->bind(1, std::string_view(path)).bind(2, size).bind(3, mtime);
  auto row = st->step();
  BH_TRY(row);
  if (!*row) return std::optional<ArtId>{};
  return std::optional<ArtId>{ArtId{st->i64(0)}};
}

Result<void> ArtRepo::remember_file(const std::string& path, std::int64_t size, std::int64_t mtime, ArtId art) {
  auto st = db_.prepare(
      "INSERT INTO art_file (path, size, mtime, art_id) VALUES (?, ?, ?, ?) "
      "ON CONFLICT(path) DO UPDATE SET size = excluded.size, mtime = excluded.mtime, art_id = excluded.art_id");
  BH_TRY(st);
  st->bind(1, std::string_view(path)).bind(2, size).bind(3, mtime).bind(4, art.value);
  return st->run();
}

namespace {
Result<std::optional<std::string>> text_column(Database& db, const char* col, ArtId id) {
  auto st = db.prepare(std::string("SELECT ") + col + " FROM art WHERE id = ?");
  BH_TRY(st);
  st->bind(1, id.value);
  auto row = st->step();
  BH_TRY(row);
  if (!*row || st->is_null(0)) return std::optional<std::string>{};
  return std::optional<std::string>{st->text(0)};
}
}  // namespace

Result<std::optional<std::string>> ArtRepo::source(ArtId id) { return text_column(db_, "source", id); }
Result<std::optional<std::string>> ArtRepo::hash(ArtId id) { return text_column(db_, "hash", id); }

}  // namespace bh::db

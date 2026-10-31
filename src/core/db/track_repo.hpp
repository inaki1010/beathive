#pragma once
#include <optional>
#include <vector>

#include "core/db/sqlite.hpp"
#include "core/model/track.hpp"

namespace bh::db {

// What the scanner needs to decide whether a file changed.
struct FileStamp {
  TrackId id;
  std::filesystem::path path;
  std::int64_t size_bytes;
  std::int64_t mtime_unix;
  bool missing;
  ArtId art;               // 0 = none
  bool has_embedded_art;
};

// Stateless over a Database; cheap to construct.
class TrackRepo {
 public:
  explicit TrackRepo(Database& db) : db_(db) {}

  // Insert or update by path. File/tag/audio fields are overwritten; library data
  // (rating, play count, last played, date added) is kept for existing rows.
  // Sets track.id on success.
  Result<TrackId> upsert(Track& track);

  Result<std::optional<Track>> get(TrackId id);
  Result<std::optional<Track>> find_by_path(const std::filesystem::path& p);
  // Preserves order of `ids`; ids that no longer exist are skipped.
  Result<std::vector<Track>> get_many(const std::vector<TrackId>& ids);

  Result<void> update_tags(TrackId id, const TagData& tags);
  Result<void> set_rating(TrackId id, Rating r);
  Result<void> record_play(TrackId id, Timestamp when);
  Result<void> set_missing(TrackId id, bool missing);
  Result<void> set_art(TrackId id, ArtId art);  // 0 clears it
  Result<void> remove(TrackId id);
  // Deletes every track flagged missing; returns how many.
  Result<int> purge_missing();

  Result<std::vector<FileStamp>> stamps(FolderId folder);
  Result<std::int64_t> count();

 private:
  Result<void> refresh_index(TrackId id, const TagData& tags);
  Database& db_;
};

}  // namespace bh::db

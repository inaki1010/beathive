#pragma once
#include <optional>
#include <vector>

#include "core/db/sqlite.hpp"
#include "core/model/playlist.hpp"

namespace bh::db {

class PlaylistRepo {
 public:
  explicit PlaylistRepo(Database& db) : db_(db) {}

  Result<PlaylistId> create_manual(const std::string& name);
  Result<PlaylistId> create_auto(const std::string& name, const AutoPlaylistSpec& spec);

  // Manual playlists include their items; auto playlists carry rules and no items.
  Result<std::optional<Playlist>> get(PlaylistId id);
  Result<std::vector<Playlist>> list();  // names/kinds only, no items

  Result<void> rename(PlaylistId id, const std::string& name);
  Result<void> set_rules(PlaylistId id, const AutoPlaylistSpec& spec);
  Result<void> set_items(PlaylistId id, const std::vector<TrackId>& items);  // replaces all
  Result<void> append_items(PlaylistId id, const std::vector<TrackId>& items);
  Result<void> remove(PlaylistId id);

 private:
  Database& db_;
};

}  // namespace bh::db

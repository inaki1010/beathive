#pragma once
#include <optional>
#include <string>
#include <vector>

#include "core/model/rules.hpp"

namespace bh {

enum class PlaylistKind { Manual, Auto };

struct Playlist {
  PlaylistId id;
  std::string name;
  PlaylistKind kind = PlaylistKind::Manual;
  std::vector<TrackId> items;                // Manual only; duplicates allowed
  std::optional<AutoPlaylistSpec> rules;     // Auto only; items resolved on demand
};

}  // namespace bh

#pragma once
#include <string>

#include "core/model/error.hpp"
#include "core/model/rules.hpp"

namespace bh::db {

// Stable, versioned JSON for auto-playlist rules (stored in playlist.rules_json,
// and reusable for export). Enum values are written as names, not numbers.
std::string rules_to_json(const AutoPlaylistSpec& spec);
Result<AutoPlaylistSpec> rules_from_json(const std::string& json);

}  // namespace bh::db

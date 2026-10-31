#pragma once
#include <unordered_map>
#include <unordered_set>

#include "core/db/track_repo.hpp"

namespace bh::ui {

// Read-through cache of full Track rows for the models. UI thread only.
class TrackCache {
 public:
  explicit TrackCache(db::Database& db) : repo_(db) {}

  // Already-cached row or nullptr; never touches the database.
  const Track* peek(TrackId id) const {
    auto it = tracks_.find(id.value);
    return it == tracks_.end() ? nullptr : &it->second;
  }
  // Loads on a miss. nullptr if the track no longer exists.
  const Track* get(TrackId id);
  // Loads any of `ids` not yet cached in one pass (call with a page of rows).
  void prefetch(const TrackId* first, std::size_t count);
  void invalidate();
  void invalidate(TrackId id);

 private:
  static constexpr std::size_t kMax = 4096;
  db::TrackRepo repo_;
  std::unordered_map<std::int64_t, Track> tracks_;
  std::unordered_set<std::int64_t> missing_;
};

}  // namespace bh::ui

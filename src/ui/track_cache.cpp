#include "ui/track_cache.hpp"

namespace bh::ui {

const Track* TrackCache::get(TrackId id) {
  if (auto it = tracks_.find(id.value); it != tracks_.end()) return &it->second;
  if (missing_.contains(id.value)) return nullptr;
  prefetch(&id, 1);
  auto it = tracks_.find(id.value);
  return it == tracks_.end() ? nullptr : &it->second;
}

void TrackCache::prefetch(const TrackId* first, std::size_t count) {
  if (tracks_.size() + count > kMax) {  // simple and cheap: start over rather than track recency
    tracks_.clear();
    missing_.clear();
  }
  std::vector<TrackId> want;
  for (std::size_t i = 0; i < count; ++i)
    if (!tracks_.contains(first[i].value) && !missing_.contains(first[i].value)) want.push_back(first[i]);
  if (want.empty()) return;

  auto rows = repo_.get_many(want);
  if (rows)
    for (auto& t : *rows) tracks_.emplace(t.id.value, std::move(t));
  for (auto id : want)
    if (!tracks_.contains(id.value)) missing_.insert(id.value);
}

void TrackCache::invalidate() {
  tracks_.clear();
  missing_.clear();
}

void TrackCache::invalidate(TrackId id) {
  tracks_.erase(id.value);
  missing_.erase(id.value);
}

}  // namespace bh::ui

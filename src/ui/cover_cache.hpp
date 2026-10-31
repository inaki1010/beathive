#pragma once
#include <QPixmap>
#include <map>

#include "core/db/art_repo.hpp"

namespace bh::ui {

// Loads cover images from where the scanner found them (an embedded picture or an image
// file) and keeps recently used scaled versions. UI thread only.
class CoverCache {
 public:
  explicit CoverCache(db::Database& db) : art_(db) {}
  // Null pixmap if there is no art or it can't be read.
  QPixmap get(ArtId id, int size);

 private:
  db::ArtRepo art_;
  std::map<std::pair<std::int64_t, int>, QPixmap> cache_;
};

}  // namespace bh::ui

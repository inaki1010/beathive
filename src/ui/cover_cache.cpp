#include "ui/cover_cache.hpp"

#include <QImage>

#include "core/tags/tag_io.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

QPixmap CoverCache::get(ArtId id, int size) {
  if (!id) return {};
  auto key = std::make_pair(id.value, size);
  if (auto it = cache_.find(key); it != cache_.end()) return it->second;

  QImage img;
  if (auto src = art_.source(id); src && *src) {
    const std::string& s = **src;
    if (s.starts_with("embedded:")) {
      if (auto cover = tags::read_cover(path_from_utf8(s.substr(9))); cover && *cover)
        img.loadFromData(QByteArrayView(reinterpret_cast<const char*>((*cover)->data.data()),
                                        static_cast<qsizetype>((*cover)->data.size())));
    } else if (s.starts_with("file:")) {
      img.load(qs(s.substr(5)));
    }
  }
  QPixmap pm;
  if (!img.isNull()) pm = QPixmap::fromImage(img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation));

  if (cache_.size() > 600) cache_.clear();
  cache_[key] = pm;  // negative results are cached too
  return pm;
}

}  // namespace bh::ui

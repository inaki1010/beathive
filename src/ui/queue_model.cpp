#include "ui/queue_model.hpp"

#include <QFont>

#include "core/util/text.hpp"
#include "ui/item_roles.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

void QueueModel::set_snapshot(playback::QueueSnapshot snap) {
  beginResetModel();
  snap_ = std::move(snap);
  endResetModel();
}

std::uint64_t QueueModel::entry_id_at(int row) const {
  return row >= 0 && row < rowCount() ? snap_.entries[static_cast<std::size_t>(row)].entry_id : 0;
}

int QueueModel::row_of_entry(std::uint64_t id) const {
  for (std::size_t i = 0; i < snap_.entries.size(); ++i)
    if (snap_.entries[i].entry_id == id) return static_cast<int>(i);
  return -1;
}

QVariant QueueModel::headerData(int s, Qt::Orientation o, int role) const {
  if (o != Qt::Horizontal) return {};
  if (role == Qt::TextAlignmentRole)
    return QVariant::fromValue(static_cast<int>((s == Time ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter));
  if (role != Qt::DisplayRole) return {};
  switch (s) {
    case Title: return tr("Title");
    case Artist: return tr("Artist");
    case Time: return tr("Time");
  }
  return {};
}

QVariant QueueModel::data(const QModelIndex& idx, int role) const {
  if (!idx.isValid() || idx.row() >= rowCount()) return {};
  const auto& e = snap_.entries[static_cast<std::size_t>(idx.row())];
  const bool playing = snap_.now_playing && *snap_.now_playing == e.entry_id;

  if (role == SubtitleRole && idx.column() == Title) {
    const Track* t = cache_.get(e.track);
    return t ? qs(t->tags.artist_display()) : QString();
  }
  if (role == Qt::FontRole && playing) {
    QFont f;
    f.setBold(true);
    return f;
  }
  if (role == PlayingRole) return playing;
  if (role == KindRole)
    return static_cast<int>(idx.column() == Marker ? CellKind::Marker
                            : idx.column() == Title ? CellKind::Primary
                            : idx.column() == Time  ? CellKind::Numeric
                                                    : CellKind::Secondary);
  if (role == Qt::TextAlignmentRole && idx.column() == Time) return QVariant::fromValue(static_cast<int>(Qt::AlignRight | Qt::AlignVCenter));
  if (role != Qt::DisplayRole) return {};

  if (idx.column() == Marker) return playing ? QString::fromUtf8("▶") : QString::number(idx.row() + 1);
  const Track* t = cache_.get(e.track);
  if (!t) return idx.column() == Title ? tr("(removed from library)") : QString();
  switch (idx.column()) {
    case Title: return t->tags.title.empty() ? from_path(t->file.path.stem()) : qs(t->tags.title);
    case Artist: return qs(t->tags.artist_display());
    case Time: return qs(text::format_duration(t->audio.duration));
  }
  return {};
}

}  // namespace bh::ui

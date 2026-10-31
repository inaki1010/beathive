#include "ui/track_table_model.hpp"

#include <QDateTime>
#include <QFont>

#include "core/util/text.hpp"
#include "ui/item_roles.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

TrackTableModel::TrackTableModel(TrackCache& cache, QObject* parent) : QAbstractTableModel(parent), cache_(cache) {}

void TrackTableModel::set_ids(std::vector<TrackId> ids) {
  beginResetModel();
  ids_ = std::move(ids);
  endResetModel();
}

TrackId TrackTableModel::id_at(int row) const {
  return row >= 0 && row < static_cast<int>(ids_.size()) ? ids_[static_cast<std::size_t>(row)] : TrackId{};
}

const Track* TrackTableModel::track_at(int row) const {
  if (row < 0 || row >= static_cast<int>(ids_.size())) return nullptr;
  auto id = ids_[static_cast<std::size_t>(row)];
  if (auto* t = cache_.get(id)) return t;
  return nullptr;
}

void TrackTableModel::set_now_playing(TrackId id) {
  if (now_playing_ == id) return;
  now_playing_ = id;
  if (!ids_.empty()) emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
}

void TrackTableModel::refresh_track(TrackId id) {
  cache_.invalidate(id);
  for (std::size_t i = 0; i < ids_.size(); ++i)
    if (ids_[i] == id) emit dataChanged(index(static_cast<int>(i), 0), index(static_cast<int>(i), ColumnCount - 1));
}

std::optional<SortField> TrackTableModel::sort_field(int column) {
  switch (column) {
    case Title: return SortField::Title;
    case Artist: return SortField::Artist;
    case Album: return SortField::Album;
    case Year: return SortField::Year;
    case Rating: return SortField::Rating;
    case PlayCount: return SortField::PlayCount;
    case DateAdded: return SortField::DateAdded;
    default: return std::nullopt;
  }
}

QString TrackTableModel::column_title(int c) {
  switch (c) {
    case Number: return tr("#");
    case Title: return tr("Title");
    case Artist: return tr("Artist");
    case Album: return tr("Album");
    case AlbumArtist: return tr("Album Artist");
    case Genre: return tr("Genre");
    case Year: return tr("Year");
    case Duration: return tr("Time");
    case Rating: return tr("Rating");
    case PlayCount: return tr("Plays");
    case DateAdded: return tr("Date Added");
    case Bitrate: return tr("Bitrate");
    case Codec: return tr("Format");
  }
  return {};
}

int TrackTableModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(ids_.size());
}
int TrackTableModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

namespace {
bool is_numeric(int col) {
  using C = TrackTableModel;
  return col == C::Number || col == C::Year || col == C::Duration || col == C::PlayCount || col == C::Bitrate;
}

CellKind kind_of(int col) {
  using C = TrackTableModel;
  switch (col) {
    case C::Number: return CellKind::Marker;
    case C::Title: return CellKind::Primary;
    case C::Rating: return CellKind::Rating;
    case C::Year: case C::Duration: case C::PlayCount: case C::Bitrate: case C::DateAdded: return CellKind::Numeric;
    default: return CellKind::Secondary;
  }
}
}  // namespace

QVariant TrackTableModel::headerData(int section, Qt::Orientation o, int role) const {
  if (o != Qt::Horizontal) return {};
  if (role == Qt::DisplayRole) return column_title(section);
  if (role == Qt::TextAlignmentRole)
    return QVariant::fromValue(static_cast<int>((is_numeric(section) ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter));
  return {};
}

// Cached row data; on a miss load the whole page around the row in one query.
const Track* TrackTableModel::loaded(int row, TrackId id) const {
  if (const Track* t = cache_.peek(id)) return t;
  const auto start = static_cast<std::size_t>(row / kPage * kPage);
  const auto count = std::min<std::size_t>(kPage, ids_.size() - start);
  cache_.prefetch(ids_.data() + start, count);
  return cache_.get(id);
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};
  const int row = index.row(), col = index.column();
  const bool numeric = is_numeric(col);

  if (role == Qt::TextAlignmentRole)
    return QVariant::fromValue(static_cast<int>((numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter));

  const auto id = id_at(row);
  const bool playing = id && id == now_playing_;
  if (role == Qt::FontRole && playing) {
    QFont f;
    f.setBold(true);
    return f;
  }
  if (role == PlayingRole) return playing;
  if (role == KindRole) return static_cast<int>(kind_of(col));
  if (role == RatingRole) {
    const Track* r = loaded(row, id);
    return r ? r->lib.rating.stars : 0;
  }
  if (role == Qt::UserRole) return QVariant::fromValue<qlonglong>(id.value);
  if (role != Qt::DisplayRole) return {};

  const Track* t = loaded(row, id);
  if (!t) return {};  // deleted since the list was built

  switch (col) {
    case Number: return playing ? QString::fromUtf8("▶") : (t->tags.track_no ? QString::number(*t->tags.track_no) : QString());
    case Title: return t->tags.title.empty() ? from_path(t->file.path.stem()) : qs(t->tags.title);
    case Artist: return qs(t->tags.artist_display());
    case Album: return qs(t->tags.album);
    case AlbumArtist: return qs(t->tags.album_artist_display());
    case Genre: return qs(t->tags.genre_display());
    case Year: return t->tags.year ? QString::number(*t->tags.year) : QString();
    case Duration: return qs(text::format_duration(t->audio.duration));
    case Rating: return QString(t->lib.rating.stars, QChar(0x2605));  // ★ per star
    case PlayCount: return t->lib.play_count ? QString::number(t->lib.play_count) : QString();
    case DateAdded:
      return QDateTime::fromSecsSinceEpoch(t->lib.date_added.time_since_epoch().count()).toString("yyyy-MM-dd");
    case Bitrate: return t->audio.bitrate_kbps ? tr("%1 kbps").arg(t->audio.bitrate_kbps) : QString();
    case Codec: return qs(text::to_lower(t->audio.codec)).toUpper();
  }
  return {};
}

}  // namespace bh::ui

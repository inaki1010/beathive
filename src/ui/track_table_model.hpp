#pragma once
#include <QAbstractTableModel>
#include <optional>
#include <vector>

#include "core/model/rules.hpp"
#include "ui/track_cache.hpp"

namespace bh::ui {

// The main track list. Holds only ids (cheap for 100k+ rows); row data is loaded a page at a
// time, on demand, as the view asks for it.
class TrackTableModel : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column {
    Number, Title, Artist, Album, AlbumArtist, Genre, Year, Duration, Rating, PlayCount, DateAdded, Bitrate, Codec,
    ColumnCount
  };

  TrackTableModel(TrackCache& cache, QObject* parent = nullptr);

  void set_ids(std::vector<TrackId> ids);
  const std::vector<TrackId>& ids() const { return ids_; }
  TrackId id_at(int row) const;
  const Track* track_at(int row) const;
  void set_now_playing(TrackId id);
  void refresh_track(TrackId id);  // after its row changed in the database

  // Columns the library query can order by (nullopt = not sortable).
  static std::optional<SortField> sort_field(int column);
  static QString column_title(int column);

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

 private:
  const Track* loaded(int row, TrackId id) const;
  static constexpr int kPage = 200;
  TrackCache& cache_;
  std::vector<TrackId> ids_;
  TrackId now_playing_;
};

}  // namespace bh::ui

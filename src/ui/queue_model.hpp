#pragma once
#include <QAbstractTableModel>

#include "core/playback/controller.hpp"
#include "ui/track_cache.hpp"

namespace bh::ui {

class QueueModel : public QAbstractTableModel {
  Q_OBJECT
 public:
  enum Column { Marker, Title, Artist, Time, ColumnCount };
  explicit QueueModel(TrackCache& cache, QObject* parent = nullptr) : QAbstractTableModel(parent), cache_(cache) {}

  void set_snapshot(playback::QueueSnapshot snap);
  std::uint64_t entry_id_at(int row) const;
  int row_of_entry(std::uint64_t entry_id) const;

  int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(snap_.entries.size()); }
  int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : ColumnCount; }
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

 private:
  TrackCache& cache_;
  playback::QueueSnapshot snap_;
};

}  // namespace bh::ui

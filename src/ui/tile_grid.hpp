#pragma once
#include <QAbstractScrollArea>
#include <functional>
#include <vector>

#include "core/model/track.hpp"
#include "ui/cover_cache.hpp"

class QLabel;
class QPushButton;
class QTableWidget;

namespace bh::ui {

// One tile in a TileGrid.
struct TileItem {
  QString title;
  QString subtitle;
  QString key;  // identifies the item across reloads, so an open panel stays open
  ArtId art;    // none: a placeholder is drawn
};

TileItem album_item(const AlbumSummary& a);  // "Title" / "Artist · Year"

// What the panel under an opened tile shows.
struct DetailData {
  QString title;
  QString meta;
  std::vector<const Track*> tracks;
};
// "lead · 5 tracks · 38 min"
QString track_meta(const QString& lead, const std::vector<const Track*>& tracks);

// The track list that opens under a clicked tile.
class TileDetail : public QWidget {
  Q_OBJECT
 public:
  explicit TileDetail(QWidget* parent = nullptr);
  void show_detail(const DetailData& data);
  static int height_for(std::size_t tracks);

 signals:
  void play_requested(int start_row);

 private:
  QLabel *title_, *meta_;
  QTableWidget* table_;
};

// A wrapping grid of album covers. Clicking a tile opens its track list in a
// full-width panel under that row of tiles; clicking it again (or Esc) closes it.
class TileGrid : public QAbstractScrollArea {
  Q_OBJECT
 public:
  using DetailOf = std::function<DetailData(int index)>;
  TileGrid(CoverCache& covers, DetailOf detail_of, QWidget* parent = nullptr);

  void set_items(std::vector<TileItem> items);
  int count() const { return static_cast<int>(items_.size()); }
  int expanded() const { return expanded_; }
  void set_expanded(int index);  // -1 closes

 signals:
  void play_requested(int index, int start_row);
  void context_requested(int index, const QPoint& global_pos);

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  void scrollContentsBy(int, int) override;
  bool viewportEvent(QEvent*) override;

 private:
  int columns() const;
  int row_count() const;
  int row_top(int row) const;   // content coordinates
  int panel_top() const;        // top of the open panel (just under its row of covers)
  int panel_height() const;
  int content_height() const;
  int index_at(const QPoint& viewport_pos) const;
  QRect tile_rect(int index) const;  // content coordinates
  void relayout();
  void paint_tile(QPainter& p, int index);
  void reveal_panel();

  CoverCache& covers_;
  DetailOf detail_of_;
  TileDetail* detail_;
  std::vector<TileItem> items_;
  int expanded_ = -1;
  int current_ = -1;
  int hover_ = -1;
};

}  // namespace bh::ui

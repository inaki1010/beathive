#pragma once
#include <QAbstractScrollArea>
#include <functional>
#include <vector>

#include "core/model/ids.hpp"
#include "ui/cover_cache.hpp"
#include "ui/track_cache.hpp"

namespace bh::ui {

struct AlbumBlock {
  QString title;
  QString subtitle;  // "Artist · Year"
  ArtId art;
  int track_count = 0;
};

// Each album as a block: its cover on the left, its full track list on the right. Tracks
// are fetched only for blocks that scroll into view.
class AlbumTracksView : public QAbstractScrollArea {
  Q_OBJECT
 public:
  using TracksOf = std::function<std::vector<TrackId>(int block)>;
  AlbumTracksView(CoverCache& covers, TrackCache& tracks, TracksOf tracks_of, QWidget* parent = nullptr);

  void set_albums(std::vector<AlbumBlock> albums);
  int count() const { return static_cast<int>(blocks_.size()); }
  void set_now_playing(TrackId id);

  // The selected position: `row` is -1 when the whole album (its cover) is selected.
  struct Pos {
    int block = -1;
    int row = -1;
    friend bool operator==(Pos, Pos) = default;
  };
  Pos selection() const { return sel_; }

 signals:
  void play_requested(int block, int row);  // row = the track to start from
  void context_requested(int block, int row, const QPoint& global_pos);

 protected:
  void paintEvent(QPaintEvent*) override;
  void resizeEvent(QResizeEvent*) override;
  void mousePressEvent(QMouseEvent*) override;
  void mouseDoubleClickEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void contextMenuEvent(QContextMenuEvent*) override;
  void keyPressEvent(QKeyEvent*) override;
  bool viewportEvent(QEvent*) override;

 private:
  struct Block {
    AlbumBlock info;
    std::vector<TrackId> ids;
    bool loaded = false;
    int top = 0;
    int height = 0;
  };
  enum class Hit { None, Cover, Play, Track };
  struct HitResult {
    Hit kind = Hit::None;
    int block = -1;
    int row = -1;
  };

  HitResult hit_test(const QPoint& viewport_pos);
  Block& load(int block);
  int block_at_content_y(int y) const;
  QRect cover_rect(const Block&) const;
  QRect play_rect(const Block&) const;
  int rows_top(const Block& b) const;
  void relayout();
  void paint_block(QPainter& p, int index);
  void select(Pos p);
  void reveal(Pos p);

  CoverCache& covers_;
  TrackCache& tracks_;
  TracksOf tracks_of_;
  std::vector<Block> blocks_;
  int content_height_ = 0;
  Pos sel_, hover_;
  bool hover_play_ = false;
  TrackId playing_;
};

}  // namespace bh::ui

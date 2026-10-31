#pragma once
#include <QAbstractButton>
#include <QColor>
#include <QIcon>
#include <QPainter>

namespace bh::ui {

// Vector icons drawn in code on a 24x24 grid: crisp at any scale, recoloured with the theme,
// and no image files to ship.
enum class Glyph {
  Play, Pause, Stop, Prev, Next, Shuffle, Repeat, RepeatOne,
  Volume, VolumeMute, Search, Close, Note, Clock, PlusCircle, Queue, Columns,
  Equalizer, Star, StarOutline, Hex, SortUp, SortDown,
};

// `level` only matters for Volume: 0 = no waves .. 2 = two waves.
void paint_glyph(QPainter& p, Glyph g, const QRectF& rect, const QColor& color, int level = 0);

// A QIcon that paints the glyph on demand (any size, any DPI); `selected` is used in the
// Selected mode (e.g. the highlighted sidebar row), the theme's faint colour when disabled.
QIcon glyph_icon(Glyph g, const QColor& normal, const QColor& selected = {}, int level = 0);

// A flat icon button. `Round` draws the filled accent circle used for play/pause.
class IconButton : public QAbstractButton {
  Q_OBJECT
 public:
  enum class Look { Plain, Round };
  explicit IconButton(Glyph g, int box = 30, int glyph_px = 18, QWidget* parent = nullptr);
  void set_glyph(Glyph g, int level = 0);
  void set_look(Look l);
  QSize sizeHint() const override { return {box_, box_}; }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  Glyph glyph_;
  int level_ = 0;
  int box_, glyph_px_;
  Look look_ = Look::Plain;
};

}  // namespace bh::ui

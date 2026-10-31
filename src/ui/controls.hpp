#pragma once
#include <QLabel>
#include <QPixmap>
#include <QSlider>
#include <QWidget>

namespace bh::ui {

// A horizontal slider where a click jumps straight to that point and dragging continues from
// there (the stock QSlider only pages toward the click). Shows its handle while hovered.
class SeekSlider : public QSlider {
  Q_OBJECT
 public:
  explicit SeekSlider(QWidget* parent = nullptr);

 protected:
  void mousePressEvent(QMouseEvent*) override;
  void mouseMoveEvent(QMouseEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;
  void enterEvent(QEnterEvent*) override { set_hot(true); }
  void leaveEvent(QEvent*) override { set_hot(isSliderDown()); }

 private:
  int value_at(int x) const;
  void set_hot(bool hot);
};

// A label that elides instead of forcing its parent wider.
class ElidedLabel : public QLabel {
  Q_OBJECT
 public:
  using QLabel::QLabel;
  QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }

 protected:
  void paintEvent(QPaintEvent*) override;
};

// Square cover art with rounded corners; a note placeholder when there is none.
class CoverView : public QWidget {
  Q_OBJECT
 public:
  explicit CoverView(int radius = 8, QWidget* parent = nullptr);
  void set_pixmap(const QPixmap& pm);
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override { return w; }
  QSize sizeHint() const override { return {200, 200}; }

 protected:
  void paintEvent(QPaintEvent*) override;

 private:
  QPixmap pm_;
  int radius_;
};

// The hexagon-and-bars app mark.
class BrandMark : public QWidget {
  Q_OBJECT
 public:
  explicit BrandMark(int size = 30, QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent*) override;
};

}  // namespace bh::ui

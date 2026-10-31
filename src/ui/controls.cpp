#include "ui/controls.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>

#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

namespace bh::ui {

// ------------------------------------------------------------------ SeekSlider

SeekSlider::SeekSlider(QWidget* parent) : QSlider(Qt::Horizontal, parent) {
  setFixedHeight(18);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
}

int SeekSlider::value_at(int x) const {
  constexpr int kInset = 6;  // half the handle, so the ends are reachable
  const int span = std::max(1, width() - 2 * kInset);
  return QStyle::sliderValueFromPosition(minimum(), maximum(), std::clamp(x - kInset, 0, span), span);
}

void SeekSlider::mousePressEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !isEnabled()) return QSlider::mousePressEvent(e);
  setSliderDown(true);
  setSliderPosition(value_at(static_cast<int>(e->position().x())));
  e->accept();
}

void SeekSlider::mouseMoveEvent(QMouseEvent* e) {
  if (!isSliderDown()) return QSlider::mouseMoveEvent(e);
  setSliderPosition(value_at(static_cast<int>(e->position().x())));
  e->accept();
}

void SeekSlider::mouseReleaseEvent(QMouseEvent* e) {
  if (!isSliderDown()) return QSlider::mouseReleaseEvent(e);
  setSliderDown(false);
  set_hot(underMouse());
  e->accept();
}

void SeekSlider::set_hot(bool hot) {
  if (property("hot").toBool() == hot) return;
  setProperty("hot", hot);
  style()->unpolish(this);  // re-evaluate the [hot="true"] style rules
  style()->polish(this);
  update();
}

// ----------------------------------------------------------------- ElidedLabel

void ElidedLabel::paintEvent(QPaintEvent*) {
  QPainter p(this);
  p.setPen(palette().color(foregroundRole()));
  p.setFont(font());
  p.drawText(rect(), static_cast<int>(alignment()), fontMetrics().elidedText(text(), Qt::ElideRight, width()));
}

// ------------------------------------------------------------------- CoverView

CoverView::CoverView(int radius, QWidget* parent) : QWidget(parent), radius_(radius) {
  QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
  sp.setHeightForWidth(true);
  setSizePolicy(sp);
}

void CoverView::set_pixmap(const QPixmap& pm) {
  pm_ = pm;
  update();
}

void CoverView::paintEvent(QPaintEvent*) {
  const Theme& t = theme();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
  QPainterPath clip;
  clip.addRoundedRect(r, radius_, radius_);

  if (pm_.isNull()) {
    p.fillPath(clip, t.raised);
    const qreal side = std::min(r.width(), r.height()) * 0.38;
    paint_glyph(p, Glyph::Note, QRectF(r.center().x() - side / 2, r.center().y() - side / 2, side, side), t.faint);
  } else {
    p.save();
    p.setClipPath(clip);
    // Centre-crop to a square, in the pixmap's own pixels.
    const qreal side = std::min(pm_.width(), pm_.height());
    const QRectF src((pm_.width() - side) / 2, (pm_.height() - side) / 2, side, side);
    p.drawPixmap(r, pm_, src);
    p.restore();
  }
  p.setPen(QPen(QColor(0, 0, 0, 40), 1));
  p.setBrush(Qt::NoBrush);
  p.drawRoundedRect(r, radius_, radius_);
}

// ------------------------------------------------------------------- BrandMark

BrandMark::BrandMark(int size, QWidget* parent) : QWidget(parent) { setFixedSize(size, size); }

void BrandMark::paintEvent(QPaintEvent*) {
  const Theme& t = theme();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const QRectF r(rect());
  paint_glyph(p, Glyph::Hex, r, t.accent);
  paint_glyph(p, Glyph::Equalizer, r.adjusted(r.width() * 0.2, r.height() * 0.2, -r.width() * 0.2, -r.height() * 0.2),
              t.accent_text);
}

}  // namespace bh::ui

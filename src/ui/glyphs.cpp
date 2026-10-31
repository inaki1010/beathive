#include "ui/glyphs.hpp"

#include <QIconEngine>
#include <QPainterPath>
#include <QPixmap>
#include <algorithm>
#include <cmath>
#include <numbers>

#include "ui/theme.hpp"

namespace bh::ui {

namespace {

QPolygonF poly(std::initializer_list<QPointF> pts) { return QPolygonF(QList<QPointF>(pts)); }

QPolygonF regular(int n, qreal r, qreal start_deg, QPointF c = {12, 12}) {
  QPolygonF out;
  for (int i = 0; i < n; ++i) {
    const qreal a = (start_deg + 360.0 * i / n) * std::numbers::pi / 180.0;
    out << QPointF(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
  }
  return out;
}

QPolygonF star() {
  QPolygonF out;
  for (int i = 0; i < 10; ++i) {
    const qreal r = i % 2 == 0 ? 10.4 : 4.5;
    const qreal a = (-90.0 + 36.0 * i) * std::numbers::pi / 180.0;
    out << QPointF(12 + r * std::cos(a), 12.8 + r * std::sin(a));
  }
  return out;
}

class GlyphEngine : public QIconEngine {
 public:
  GlyphEngine(Glyph g, QColor normal, QColor selected, int level)
      : g_(g), normal_(normal), selected_(selected.isValid() ? selected : normal), level_(level) {}

  void paint(QPainter* p, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
    QColor c = mode == QIcon::Selected ? selected_ : normal_;
    if (mode == QIcon::Disabled) c = theme().faint;
    paint_glyph(*p, g_, rect, c, level_);
  }
  QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(&p, QRect(QPoint(), size), mode, state);
    return pm;
  }
  QIconEngine* clone() const override { return new GlyphEngine(*this); }

 private:
  Glyph g_;
  QColor normal_, selected_;
  int level_;
};

}  // namespace

void paint_glyph(QPainter& p, Glyph g, const QRectF& r, const QColor& c, int level) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  const qreal s = std::min(r.width(), r.height()) / 24.0;
  p.translate(r.center() - QPointF(12 * s, 12 * s));
  p.scale(s, s);

  const QPen line(c, 1.9, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
  auto stroke = [&] { p.setPen(line); p.setBrush(Qt::NoBrush); };
  auto fill = [&] { p.setPen(QPen(c, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin)); p.setBrush(c); };
  auto solid = [&] { p.setPen(Qt::NoPen); p.setBrush(c); };

  switch (g) {
    case Glyph::Play:
      fill();
      p.drawPolygon(poly({{8.2, 5.8}, {8.2, 18.2}, {18.4, 12}}));
      break;
    case Glyph::Pause:
      solid();
      p.drawRoundedRect(QRectF(6.6, 5, 3.8, 14), 1.2, 1.2);
      p.drawRoundedRect(QRectF(13.6, 5, 3.8, 14), 1.2, 1.2);
      break;
    case Glyph::Stop:
      solid();
      p.drawRoundedRect(QRectF(6.2, 6.2, 11.6, 11.6), 2, 2);
      break;
    case Glyph::Prev:
      solid();
      p.drawRoundedRect(QRectF(5.2, 5.6, 2.5, 12.8), 1, 1);
      fill();
      p.drawPolygon(poly({{18.6, 6.4}, {18.6, 17.6}, {9.6, 12}}));
      break;
    case Glyph::Next:
      solid();
      p.drawRoundedRect(QRectF(16.3, 5.6, 2.5, 12.8), 1, 1);
      fill();
      p.drawPolygon(poly({{5.4, 6.4}, {5.4, 17.6}, {14.4, 12}}));
      break;
    case Glyph::Shuffle: {
      stroke();
      QPainterPath a, b;
      a.moveTo(3.5, 7.5); a.lineTo(7, 7.5); a.cubicTo(10.5, 7.5, 12, 16.5, 15.5, 16.5); a.lineTo(19, 16.5);
      b.moveTo(3.5, 16.5); b.lineTo(7, 16.5); b.cubicTo(10.5, 16.5, 12, 7.5, 15.5, 7.5); b.lineTo(19, 7.5);
      p.drawPath(a);
      p.drawPath(b);
      p.drawPolyline(poly({{17, 14.2}, {19.6, 16.5}, {17, 18.8}}));
      p.drawPolyline(poly({{17, 5.2}, {19.6, 7.5}, {17, 9.8}}));
      break;
    }
    case Glyph::Repeat:
    case Glyph::RepeatOne: {
      stroke();
      QPainterPath top, bottom;
      top.moveTo(5, 12); top.lineTo(5, 10); top.quadTo(5, 7, 8, 7); top.lineTo(18.4, 7);
      bottom.moveTo(19, 12); bottom.lineTo(19, 14); bottom.quadTo(19, 17, 16, 17); bottom.lineTo(5.6, 17);
      p.drawPath(top);
      p.drawPath(bottom);
      p.drawPolyline(poly({{16, 4.4}, {18.8, 7}, {16, 9.6}}));
      p.drawPolyline(poly({{8, 14.4}, {5.2, 17}, {8, 19.6}}));
      if (g == Glyph::RepeatOne) {
        QFont f = p.font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        p.setPen(c);
        p.drawText(QRectF(8.5, 8.4, 7, 7), Qt::AlignCenter, "1");
      }
      break;
    }
    case Glyph::Volume:
    case Glyph::VolumeMute:
      fill();
      p.drawPolygon(poly({{4, 9.6}, {7.8, 9.6}, {12.4, 5.6}, {12.4, 18.4}, {7.8, 14.4}, {4, 14.4}}));
      stroke();
      if (g == Glyph::VolumeMute) {
        p.drawLine(QPointF(15.6, 9.6), QPointF(20.6, 14.6));
        p.drawLine(QPointF(20.6, 9.6), QPointF(15.6, 14.6));
      } else {
        if (level >= 1) p.drawArc(QRectF(8.9, 8.5, 7, 7), -50 * 16, 100 * 16);
        if (level >= 2) p.drawArc(QRectF(5.9, 5.5, 13, 13), -50 * 16, 100 * 16);
      }
      break;
    case Glyph::Search:
      stroke();
      p.drawEllipse(QPointF(10.5, 10.5), 6, 6);
      p.drawLine(QPointF(15, 15), QPointF(20, 20));
      break;
    case Glyph::Close:
      stroke();
      p.drawLine(QPointF(7, 7), QPointF(17, 17));
      p.drawLine(QPointF(17, 7), QPointF(7, 17));
      break;
    case Glyph::Note: {
      solid();
      p.drawEllipse(QPointF(8, 17.6), 3.4, 2.8);
      stroke();
      p.drawLine(QPointF(11.2, 17.2), QPointF(11.2, 5.6));
      QPainterPath flag;
      flag.moveTo(11.2, 5.6);
      flag.cubicTo(14.2, 6.4, 17.4, 7.2, 17.2, 10.8);
      p.drawPath(flag);
      break;
    }
    case Glyph::Clock:
      stroke();
      p.drawEllipse(QPointF(12, 12), 8.2, 8.2);
      p.drawPolyline(poly({{12, 7.4}, {12, 12}, {15.6, 14.2}}));
      break;
    case Glyph::PlusCircle:
      stroke();
      p.drawEllipse(QPointF(12, 12), 8.2, 8.2);
      p.drawLine(QPointF(12, 8), QPointF(12, 16));
      p.drawLine(QPointF(8, 12), QPointF(16, 12));
      break;
    case Glyph::Queue:
      stroke();
      p.drawLine(QPointF(4, 7), QPointF(15.5, 7));
      p.drawLine(QPointF(4, 12), QPointF(15.5, 12));
      p.drawLine(QPointF(4, 17), QPointF(10, 17));
      fill();
      p.drawPolygon(poly({{14.8, 14.4}, {14.8, 19.8}, {19.6, 17.1}}));
      break;
    case Glyph::Columns:
      stroke();
      p.drawRoundedRect(QRectF(4, 5, 16, 14), 2.6, 2.6);
      p.drawLine(QPointF(9.4, 5), QPointF(9.4, 19));
      p.drawLine(QPointF(14.6, 5), QPointF(14.6, 19));
      break;
    case Glyph::Equalizer:
      solid();
      p.drawRoundedRect(QRectF(5, 10, 3.6, 9), 1.2, 1.2);
      p.drawRoundedRect(QRectF(10.2, 5, 3.6, 14), 1.2, 1.2);
      p.drawRoundedRect(QRectF(15.4, 8, 3.6, 11), 1.2, 1.2);
      break;
    case Glyph::Star:
      fill();
      p.drawPolygon(star());
      break;
    case Glyph::StarOutline:
      p.setPen(QPen(c, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      p.setBrush(Qt::NoBrush);
      p.drawPolygon(star());
      break;
    case Glyph::Hex:
      fill();
      p.drawPolygon(regular(6, 10.2, -90));
      break;
    case Glyph::SortUp:
      solid();
      p.drawPolygon(poly({{6.5, 14.5}, {17.5, 14.5}, {12, 8}}));
      break;
    case Glyph::SortDown:
      solid();
      p.drawPolygon(poly({{6.5, 9.5}, {17.5, 9.5}, {12, 16}}));
      break;
  }
  p.restore();
}

QIcon glyph_icon(Glyph g, const QColor& normal, const QColor& selected, int level) {
  return QIcon(new GlyphEngine(g, normal, selected, level));
}

// ------------------------------------------------------------------ IconButton

IconButton::IconButton(Glyph g, int box, int glyph_px, QWidget* parent)
    : QAbstractButton(parent), glyph_(g), box_(box), glyph_px_(glyph_px) {
  setFixedSize(box, box);
  setFocusPolicy(Qt::NoFocus);
  setCursor(Qt::PointingHandCursor);
  setAttribute(Qt::WA_Hover);
}

void IconButton::set_glyph(Glyph g, int level) {
  if (g == glyph_ && level == level_) return;
  glyph_ = g;
  level_ = level;
  update();
}

void IconButton::set_look(Look l) {
  look_ = l;
  update();
}

void IconButton::paintEvent(QPaintEvent*) {
  const Theme& t = theme();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  const bool hot = underMouse() && isEnabled();
  const QRectF box(rect());

  QColor ink;
  if (look_ == Look::Round) {
    QColor bg = t.accent;
    if (!isEnabled()) bg = t.hover;
    else if (isDown()) bg = bg.darker(115);
    else if (hot) bg = bg.lighter(110);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawEllipse(box.adjusted(1, 1, -1, -1));
    ink = isEnabled() ? t.accent_text : t.faint;
  } else {
    if (hot || isDown()) {
      p.setPen(Qt::NoPen);
      p.setBrush(t.hover);
      p.drawRoundedRect(box.adjusted(1, 1, -1, -1), 7, 7);
    }
    ink = !isEnabled() ? t.faint : isChecked() ? t.accent : hot ? t.text : t.dim;
  }
  const QRectF g(box.center().x() - glyph_px_ / 2.0, box.center().y() - glyph_px_ / 2.0, glyph_px_, glyph_px_);
  paint_glyph(p, glyph_, g, ink, level_);
}

}  // namespace bh::ui

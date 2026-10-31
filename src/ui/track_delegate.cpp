#include "ui/track_delegate.hpp"

#include <QMouseEvent>
#include <QPainter>

#include "ui/glyphs.hpp"
#include "ui/item_roles.hpp"
#include "ui/theme.hpp"

namespace bh::ui {

namespace {
constexpr int kPad = 8;
constexpr qreal kStar = 13;
constexpr qreal kStarGap = 2;
}  // namespace

QRectF TrackDelegate::star_rect(const QRect& cell, int i) {
  const qreal y = cell.center().y() - kStar / 2.0 + 0.5;
  return QRectF(cell.left() + kPad + i * (kStar + kStarGap), y, kStar, kStar);
}

QSize TrackDelegate::sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
  return {QStyledItemDelegate::sizeHint(opt, idx).width(),
          idx.data(SubtitleRole).toString().isEmpty() ? kRowHeight : kTwoLineRowHeight};
}

void TrackDelegate::paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const {
  const Theme& t = theme();
  const bool selected = opt.state & QStyle::State_Selected;
  const bool hot = opt.state & QStyle::State_MouseOver;
  const bool playing = idx.data(PlayingRole).toBool();
  const auto kind = static_cast<CellKind>(idx.data(KindRole).toInt());

  p->save();
  p->fillRect(opt.rect, (idx.row() & 1) ? t.alt : t.base);
  if (selected) p->fillRect(opt.rect, t.accent_tint);
  else if (hot) p->fillRect(opt.rect, t.hover);
  p->setRenderHint(QPainter::Antialiasing);

  const QRect inner = opt.rect.adjusted(kPad, 0, -kPad, 0);

  if (kind == CellKind::Rating) {
    const int stars = idx.data(RatingRole).toInt();
    for (int i = 0; i < 5; ++i) {
      if (i < stars) paint_glyph(*p, Glyph::Star, star_rect(opt.rect, i), t.accent);
      else if (selected || hot) paint_glyph(*p, Glyph::StarOutline, star_rect(opt.rect, i), t.faint);
    }
    p->restore();
    return;
  }

  if (kind == CellKind::Marker && playing) {
    const qreal s = 15;
    paint_glyph(*p, Glyph::Equalizer, QRectF(inner.right() - s + 1, opt.rect.center().y() - s / 2.0 + 0.5, s, s), t.accent);
    p->restore();
    return;
  }

  QColor ink = t.text;
  QFont font = opt.font;
  switch (kind) {
    case CellKind::Primary:
      if (playing) {
        ink = t.accent;
        font.setBold(true);
      }
      break;
    case CellKind::Secondary: ink = selected ? t.text : t.dim; break;
    case CellKind::Numeric:
    case CellKind::Marker: ink = selected ? t.text : t.faint; break;
    case CellKind::Rating: break;
  }
  if (kind == CellKind::Primary) {
    if (const QString sub = idx.data(SubtitleRole).toString(); !sub.isEmpty()) {
      const QRect top(inner.left(), opt.rect.top() + 6, inner.width(), 19);
      const QRect bottom(inner.left(), opt.rect.top() + 25, inner.width(), 16);
      p->setFont(font);
      p->setPen(ink);
      p->drawText(top, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, top.width()));
      QFont small = opt.font;
      small.setPointSizeF(small.pointSizeF() * 0.9);
      p->setFont(small);
      p->setPen(selected ? t.text : t.dim);
      p->drawText(bottom, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(small).elidedText(sub, Qt::ElideRight, bottom.width()));
      p->restore();
      return;
    }
  }
  p->setFont(font);
  p->setPen(ink);
  auto align = static_cast<Qt::Alignment>(idx.data(Qt::TextAlignmentRole).toInt());
  if (!(align & Qt::AlignVertical_Mask)) align |= Qt::AlignVCenter;
  p->drawText(inner, static_cast<int>(align),
              QFontMetrics(font).elidedText(idx.data(Qt::DisplayRole).toString(), Qt::ElideRight, inner.width()));
  p->restore();
}

bool TrackDelegate::editorEvent(QEvent* e, QAbstractItemModel* m, const QStyleOptionViewItem& opt, const QModelIndex& idx) {
  if (e->type() == QEvent::MouseButtonRelease && idx.data(KindRole).toInt() == static_cast<int>(CellKind::Rating)) {
    auto* me = static_cast<QMouseEvent*>(e);
    if (me->button() == Qt::LeftButton)
      for (int i = 0; i < 5; ++i)
        if (star_rect(opt.rect, i).adjusted(-1, -4, 1, 4).contains(me->position())) {
          const int cur = idx.data(RatingRole).toInt();
          emit rating_clicked(idx, cur == i + 1 ? 0 : i + 1);  // clicking the current rating clears it
          return true;
        }
  }
  return QStyledItemDelegate::editorEvent(e, m, opt, idx);
}

// ------------------------------------------------------------------ TrackHeader

TrackHeader::TrackHeader(QWidget* parent) : QHeaderView(Qt::Horizontal, parent) {
  setSectionsClickable(true);
  setHighlightSections(false);
  setMouseTracking(true);
}

QSize TrackHeader::sectionSizeFromContents(int logical) const {
  return {QHeaderView::sectionSizeFromContents(logical).width(), 32};
}

void TrackHeader::paintSection(QPainter* p, const QRect& rect, int logical) const {
  const Theme& t = theme();
  p->save();
  p->fillRect(rect, t.panel);
  if (isSortIndicatorShown() && sectionsClickable() && rect.contains(viewport()->mapFromGlobal(QCursor::pos())))
    p->fillRect(rect, t.hover);
  p->setPen(t.border);
  p->drawLine(rect.bottomLeft(), rect.bottomRight());

  const bool sorted = isSortIndicatorShown() && sortIndicatorSection() == logical;
  const auto align = static_cast<Qt::Alignment>(
      model()->headerData(logical, Qt::Horizontal, Qt::TextAlignmentRole).toInt());
  const bool right = align & Qt::AlignRight;

  QFont f = font();
  f.setPointSizeF(f.pointSizeF() * 0.82);
  f.setBold(true);
  f.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
  p->setFont(f);
  p->setPen(sorted ? t.text : t.faint);

  QRect text = rect.adjusted(kPad, 0, -kPad, 0);
  const int chevron = sorted ? 12 : 0;
  if (right) text.setLeft(text.left() + chevron);
  else text.setRight(text.right() - chevron);
  const QString label = model()->headerData(logical, Qt::Horizontal, Qt::DisplayRole).toString().toUpper();
  p->drawText(text, static_cast<int>((right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter),
              QFontMetrics(f).elidedText(label, Qt::ElideRight, text.width()));

  if (sorted) {
    const int w = QFontMetrics(f).horizontalAdvance(label);
    const qreal x = right ? text.right() - w - 13 : std::min(text.left() + w + 1, rect.right() - 22);
    p->setRenderHint(QPainter::Antialiasing);
    paint_glyph(*p, sortIndicatorOrder() == Qt::AscendingOrder ? Glyph::SortUp : Glyph::SortDown,
                QRectF(x, rect.center().y() - 7, 14, 14), t.accent);
  }
  p->restore();
}

}  // namespace bh::ui

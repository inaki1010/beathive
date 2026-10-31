#pragma once
#include <QHeaderView>
#include <QStyledItemDelegate>

namespace bh::ui {

// Paints the rows of the track and queue lists: selection and hover, an accent for the playing
// row, dimmed secondary columns, and clickable star ratings.
class TrackDelegate : public QStyledItemDelegate {
  Q_OBJECT
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
  QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override;
  bool editorEvent(QEvent* e, QAbstractItemModel* m, const QStyleOptionViewItem& opt, const QModelIndex& idx) override;

  // The rectangle of star `i` (0-based) inside a cell.
  static QRectF star_rect(const QRect& cell, int i);

 signals:
  void rating_clicked(const QModelIndex& index, int stars);
};

// A flat, themed header with small-caps labels and a sort chevron.
class TrackHeader : public QHeaderView {
  Q_OBJECT
 public:
  explicit TrackHeader(QWidget* parent = nullptr);

 protected:
  void paintSection(QPainter* p, const QRect& rect, int logical) const override;
  QSize sectionSizeFromContents(int logical) const override;
};

}  // namespace bh::ui

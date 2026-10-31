#include "ui/tile_grid.hpp"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

#include "core/util/text.hpp"
#include "ui/glyphs.hpp"
#include "ui/item_roles.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"
#include "ui/track_delegate.hpp"

namespace bh::ui {

namespace {
constexpr int kCover = 168;
constexpr int kPad = 10;
constexpr int kTextHeight = 40;
constexpr int kCellW = kCover + 2 * kPad;
constexpr int kCellH = kCover + kTextHeight + 2 * kPad;
constexpr int kMargin = 12;
constexpr int kPanelGap = 10;         // space above and below the open panel (the caret lives in the top one)
constexpr int kDetailHeader = 70;
constexpr std::size_t kMaxVisibleRows = 10;
}  // namespace

TileItem album_item(const AlbumSummary& a) {
  QString artist = a.key.album_artist.empty() ? QObject::tr("Unknown Artist") : qs(a.key.album_artist);
  if (a.year) artist += QString(" · %1").arg(*a.year);
  return {a.key.album.empty() ? QObject::tr("[Unknown Album]") : qs(a.key.album), artist,
          qs(a.key.album_artist) + '\x1f' + qs(a.key.album), a.art};
}

QString track_meta(const QString& lead, const std::vector<const Track*>& tracks) {
  std::chrono::milliseconds total{0};
  for (const Track* t : tracks) total += t->audio.duration;
  const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(total).count();
  return QString("%1 · %2 · %3")
      .arg(lead, tracks.size() == 1 ? QObject::tr("1 track") : QObject::tr("%1 tracks").arg(tracks.size()),
           minutes >= 60 ? QObject::tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60) : QObject::tr("%1 min").arg(minutes));
}

// ------------------------------------------------------------------ TileDetail

TileDetail::TileDetail(QWidget* parent) : QWidget(parent) {
  setObjectName("album_detail");
  setAttribute(Qt::WA_StyledBackground);

  title_ = new QLabel(this);
  title_->setObjectName("np_title");
  meta_ = new QLabel(this);
  meta_->setObjectName("np_album");
  auto* play = new QPushButton(tr("Play"), this);
  play->setProperty("accent", true);
  play->setCursor(Qt::PointingHandCursor);
  play->setMinimumWidth(90);

  table_ = new QTableWidget(0, 3, this);
  table_->setObjectName("album_tracks");
  table_->setItemDelegate(new TrackDelegate(table_));
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setShowGrid(false);
  table_->setFrameShape(QFrame::NoFrame);
  table_->setMouseTracking(true);
  table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table_->horizontalHeader()->hide();
  table_->verticalHeader()->hide();
  table_->verticalHeader()->setDefaultSectionSize(kRowHeight);
  table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
  table_->setColumnWidth(0, 52);
  table_->setColumnWidth(2, 76);

  auto* names = new QVBoxLayout;
  names->setSpacing(2);
  names->addStretch();
  names->addWidget(title_);
  names->addWidget(meta_);
  names->addStretch();
  auto* head = new QHBoxLayout;
  head->setContentsMargins(20, 0, 20, 0);
  head->addLayout(names, 1);
  head->addWidget(play, 0, Qt::AlignVCenter);

  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(0);
  auto* header = new QWidget(this);
  header->setFixedHeight(kDetailHeader);
  header->setLayout(head);
  lay->addWidget(header);
  lay->addWidget(table_, 1);

  connect(play, &QPushButton::clicked, this, [this] { emit play_requested(0); });
  connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { emit play_requested(row); });
}

int TileDetail::height_for(std::size_t tracks) {
  return kDetailHeader + static_cast<int>(std::clamp<std::size_t>(tracks, 1, kMaxVisibleRows)) * kRowHeight + 8;
}

void TileDetail::show_detail(const DetailData& info) {
  const auto& tracks = info.tracks;
  title_->setText(info.title);
  meta_->setText(info.meta);

  table_->setRowCount(static_cast<int>(tracks.size()));
  auto put = [&](int row, int col, const QString& text, CellKind kind, Qt::Alignment align) {
    auto* it = new QTableWidgetItem(text);
    it->setData(KindRole, static_cast<int>(kind));
    it->setData(Qt::TextAlignmentRole, static_cast<int>(align));
    table_->setItem(row, col, it);
  };
  for (int r = 0; r < static_cast<int>(tracks.size()); ++r) {
    const Track& t = *tracks[static_cast<std::size_t>(r)];
    const QString number = t.tags.track_no ? QString::number(*t.tags.track_no) : QString::number(r + 1);
    put(r, 0, number, CellKind::Numeric, Qt::AlignRight | Qt::AlignVCenter);
    put(r, 1, t.tags.title.empty() ? from_path(t.file.path.stem()) : qs(t.tags.title), CellKind::Primary,
        Qt::AlignLeft | Qt::AlignVCenter);
    put(r, 2, qs(text::format_duration(t.audio.duration)), CellKind::Numeric, Qt::AlignRight | Qt::AlignVCenter);
  }
  table_->scrollToTop();
}

// ---------------------------------------------------------------------- TileGrid

TileGrid::TileGrid(CoverCache& covers, DetailOf detail_of, QWidget* parent)
    : QAbstractScrollArea(parent), covers_(covers), detail_of_(std::move(detail_of)) {
  setObjectName("album_grid");
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setMouseTracking(true);
  viewport()->setAutoFillBackground(false);
  verticalScrollBar()->setSingleStep(48);

  detail_ = new TileDetail(viewport());
  detail_->hide();
  connect(detail_, &TileDetail::play_requested, this, [this](int row) {
    if (expanded_ >= 0) emit play_requested(expanded_, row);
  });
}

int TileGrid::columns() const { return std::max(1, (viewport()->width() - 2 * kMargin) / kCellW); }
int TileGrid::row_count() const { return (count() + columns() - 1) / columns(); }
int TileGrid::panel_height() const { return expanded_ >= 0 ? detail_->height() + 2 * kPanelGap : 0; }

int TileGrid::row_top(int row) const {
  int y = kMargin + row * kCellH;
  if (expanded_ >= 0 && row > expanded_ / columns()) y += panel_height();
  return y;
}

int TileGrid::panel_top() const { return kMargin + (expanded_ / columns() + 1) * kCellH; }
int TileGrid::content_height() const { return 2 * kMargin + row_count() * kCellH + panel_height(); }

QRect TileGrid::tile_rect(int index) const {
  const int cols = columns();
  return {kMargin + (index % cols) * kCellW, row_top(index / cols), kCellW, kCellH};
}

int TileGrid::index_at(const QPoint& pos) const {
  const int y = pos.y() + verticalScrollBar()->value();
  for (int row = 0, rows = row_count(); row < rows; ++row) {
    const int top = row_top(row);
    if (y < top || y >= top + kCellH) continue;
    const int col = (pos.x() - kMargin) / kCellW;
    const int i = row * columns() + col;
    return pos.x() >= kMargin && col < columns() && i < count() ? i : -1;
  }
  return -1;
}

void TileGrid::relayout() {
  if (expanded_ >= 0) {
    detail_->setFixedSize(std::max(200, viewport()->width() - 2 * kMargin), detail_->height());
    detail_->move(kMargin, panel_top() + kPanelGap - verticalScrollBar()->value());
  }
  auto* bar = verticalScrollBar();
  bar->setPageStep(viewport()->height());
  bar->setRange(0, std::max(0, content_height() - viewport()->height()));
  if (expanded_ >= 0) detail_->move(kMargin, panel_top() + kPanelGap - bar->value());
  viewport()->update();
}

void TileGrid::set_items(std::vector<TileItem> items) {
  QString open;
  if (expanded_ >= 0) open = items_[static_cast<std::size_t>(expanded_)].key;
  const bool was_open = expanded_ >= 0;
  items_ = std::move(items);
  hover_ = current_ = -1;
  expanded_ = -1;
  detail_->hide();
  if (was_open)
    for (int i = 0; i < count(); ++i)
      if (items_[static_cast<std::size_t>(i)].key == open) {
        set_expanded(i);
        current_ = i;
        break;
      }
  relayout();
}

void TileGrid::set_expanded(int index) {
  if (index < 0 || index >= count()) {
    expanded_ = -1;
    detail_->hide();
    relayout();
    return;
  }
  const DetailData info = detail_of_(index);
  detail_->show_detail(info);
  detail_->setFixedHeight(TileDetail::height_for(info.tracks.size()));
  expanded_ = index;
  detail_->show();
  relayout();
  reveal_panel();
}

// Scrolls just enough that the open panel (and its row of covers, if both fit) is on screen.
// When they don't, the tracks win, with a strip of the album's title left in view above them.
void TileGrid::reveal_panel() {
  auto* bar = verticalScrollBar();
  const int top = row_top(expanded_ / columns());
  const int bottom = panel_top() + panel_height();
  int v = bar->value();
  if (bottom > v + viewport()->height()) v = bottom - viewport()->height();
  if (bottom - top > viewport()->height()) v = std::min(bottom - viewport()->height(), panel_top() - 48);
  else if (top < v) v = top - 6;
  bar->setValue(std::clamp(v, bar->minimum(), bar->maximum()));
}

void TileGrid::paint_tile(QPainter& p, int index) {
  const Theme& t = theme();
  const QRect cell = tile_rect(index);
  const TileItem& item = items_[static_cast<std::size_t>(index)];
  const bool open = index == expanded_;

  if (open || index == hover_) {
    p.setPen(Qt::NoPen);
    p.setBrush(open ? t.accent_tint : t.hover);
    p.drawRoundedRect(cell.adjusted(2, 2, -2, -2), 8, 8);
  }
  if (hasFocus() && index == current_ && !open) {
    p.setPen(QPen(t.faint, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(cell.adjusted(2, 2, -2, -2), 8, 8);
  }

  const QRect cover(cell.left() + kPad, cell.top() + kPad, kCover, kCover);
  p.save();
  QPainterPath clip;
  clip.addRoundedRect(cover, 6, 6);
  p.setClipPath(clip);
  p.fillRect(cover, t.raised);
  const QPixmap pm = covers_.get(item.art, static_cast<int>(kCover * devicePixelRatioF()));
  if (pm.isNull()) {
    const int inset = kCover * 3 / 10;
    paint_glyph(p, Glyph::Note, QRectF(cover).adjusted(inset, inset, -inset, -inset), t.faint);
  } else {
    const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();  // scaled to fit; centre non-square art
    p.drawPixmap(QRectF(cover.left() + (kCover - logical.width()) / 2, cover.top() + (kCover - logical.height()) / 2,
                        logical.width(), logical.height()),
                 pm, QRectF(pm.rect()));
  }
  p.restore();

  QFont title = font();
  title.setWeight(QFont::DemiBold);
  const QRect title_rect(cover.left(), cover.bottom() + 8, kCover, 17);
  const QRect sub_rect(cover.left(), title_rect.bottom() + 3, kCover, 16);
  p.setFont(title);
  p.setPen(t.text);
  p.drawText(title_rect, Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(title).elidedText(item.title, Qt::ElideRight, kCover));
  QFont sub = font();
  sub.setPointSizeF(sub.pointSizeF() * 0.9);
  p.setFont(sub);
  p.setPen(t.dim);
  p.drawText(sub_rect, Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(sub).elidedText(item.subtitle, Qt::ElideRight, kCover));
}

void TileGrid::paintEvent(QPaintEvent*) {
  const Theme& t = theme();
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), t.base);
  p.setRenderHint(QPainter::Antialiasing);
  const int scroll = verticalScrollBar()->value();
  p.translate(0, -scroll);
  const int view_top = scroll, view_bottom = scroll + viewport()->height();

  const int cols = columns();
  for (int row = 0, rows = row_count(); row < rows; ++row) {
    const int top = row_top(row);
    if (top + kCellH < view_top) continue;
    if (top > view_bottom) break;
    for (int i = row * cols; i < std::min(count(), (row + 1) * cols); ++i) paint_tile(p, i);
  }

  if (expanded_ >= 0) {  // the caret joining the open panel to its album
    const int cx = tile_rect(expanded_).center().x();
    const int y = panel_top() + kPanelGap;
    QPolygon caret;
    caret << QPoint(cx - 9, y) << QPoint(cx, y - kPanelGap + 1) << QPoint(cx + 9, y);
    p.setPen(Qt::NoPen);
    p.setBrush(t.panel);
    p.drawPolygon(caret);
  }
}

void TileGrid::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  relayout();
}

void TileGrid::scrollContentsBy(int, int) {
  if (expanded_ >= 0) detail_->move(kMargin, panel_top() + kPanelGap - verticalScrollBar()->value());
  viewport()->update();
}

void TileGrid::mousePressEvent(QMouseEvent* e) {
  setFocus();
  if (e->button() != Qt::LeftButton) return;
  const int i = index_at(e->position().toPoint());
  if (i < 0) return;
  current_ = i;
  set_expanded(i == expanded_ ? -1 : i);
}

void TileGrid::mouseDoubleClickEvent(QMouseEvent* e) {
  const int i = index_at(e->position().toPoint());
  if (i >= 0 && e->button() == Qt::LeftButton) emit play_requested(i, 0);
}

void TileGrid::mouseMoveEvent(QMouseEvent* e) {
  const int i = index_at(e->position().toPoint());
  if (i != hover_) {
    hover_ = i;
    viewport()->update();
  }
  viewport()->setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool TileGrid::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::Leave && hover_ != -1) {
    hover_ = -1;
    viewport()->update();
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void TileGrid::contextMenuEvent(QContextMenuEvent* e) {
  const int i = index_at(e->pos());
  if (i >= 0) emit context_requested(i, e->globalPos());
}

void TileGrid::keyPressEvent(QKeyEvent* e) {
  if (count() == 0) return QAbstractScrollArea::keyPressEvent(e);
  const int cols = columns();
  int next = current_ < 0 ? 0 : current_;
  switch (e->key()) {
    case Qt::Key_Left: next = std::max(0, next - 1); break;
    case Qt::Key_Right: next = std::min(count() - 1, next + 1); break;
    case Qt::Key_Up: next = std::max(0, next - cols); break;
    case Qt::Key_Down: next = std::min(count() - 1, next + cols); break;
    case Qt::Key_Home: next = 0; break;
    case Qt::Key_End: next = count() - 1; break;
    case Qt::Key_Escape: set_expanded(-1); return;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (current_ >= 0) {
        if (e->key() == Qt::Key_Space) set_expanded(current_ == expanded_ ? -1 : current_);
        else emit play_requested(current_, 0);
      }
      return;
    default: return QAbstractScrollArea::keyPressEvent(e);
  }
  current_ = next;
  if (expanded_ >= 0) {
    set_expanded(next);  // keep the panel following the cursor while one is open
  } else {
    auto* bar = verticalScrollBar();
    const QRect r = tile_rect(next);
    if (r.top() < bar->value()) bar->setValue(r.top() - kMargin);
    else if (r.bottom() > bar->value() + viewport()->height()) bar->setValue(r.bottom() - viewport()->height() + kMargin);
  }
  viewport()->update();
}

}  // namespace bh::ui

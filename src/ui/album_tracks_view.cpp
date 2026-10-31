#include "ui/album_tracks_view.hpp"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <algorithm>

#include "core/util/text.hpp"
#include "ui/glyphs.hpp"
#include "ui/item_roles.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"

namespace bh::ui {

namespace {
constexpr int kPad = 16;
constexpr int kCover = 148;
constexpr int kHeader = 70;   // title, subtitle and totals above the track rows
constexpr int kRow = 28;
constexpr int kTextGap = 22;  // between the cover and the text
constexpr int kNumberW = 40;
constexpr int kPlayW = 74, kPlayH = 28;

int block_height(int tracks) {
  return std::max(2 * kPad + kCover, 2 * kPad + kHeader + std::max(1, tracks) * kRow);
}
}  // namespace

AlbumTracksView::AlbumTracksView(CoverCache& covers, TrackCache& tracks, TracksOf tracks_of, QWidget* parent)
    : QAbstractScrollArea(parent), covers_(covers), tracks_(tracks), tracks_of_(std::move(tracks_of)) {
  setObjectName("album_tracks_view");
  setFrameShape(QFrame::NoFrame);
  setFocusPolicy(Qt::StrongFocus);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  viewport()->setMouseTracking(true);
  verticalScrollBar()->setSingleStep(kRow * 2);
}

void AlbumTracksView::set_albums(std::vector<AlbumBlock> albums) {
  blocks_.clear();
  blocks_.reserve(albums.size());
  for (auto& a : albums) {
    Block b;
    b.info = std::move(a);
    blocks_.push_back(std::move(b));
  }
  sel_ = hover_ = {};
  relayout();
}

void AlbumTracksView::set_now_playing(TrackId id) {
  playing_ = id;
  viewport()->update();
}

void AlbumTracksView::relayout() {
  int y = 0;
  for (auto& b : blocks_) {
    b.top = y;
    b.height = block_height(b.info.track_count);
    y += b.height;
  }
  content_height_ = y;
  auto* bar = verticalScrollBar();
  bar->setPageStep(viewport()->height());
  bar->setRange(0, std::max(0, content_height_ - viewport()->height()));
  viewport()->update();
}

void AlbumTracksView::resizeEvent(QResizeEvent* e) {
  QAbstractScrollArea::resizeEvent(e);
  relayout();
}

AlbumTracksView::Block& AlbumTracksView::load(int index) {
  Block& b = blocks_[static_cast<std::size_t>(index)];
  if (!b.loaded) {
    b.ids = tracks_of_(index);
    b.loaded = true;
  }
  return b;
}

int AlbumTracksView::block_at_content_y(int y) const {
  auto it = std::upper_bound(blocks_.begin(), blocks_.end(), y, [](int v, const Block& b) { return v < b.top; });
  return static_cast<int>(it - blocks_.begin()) - 1;
}

QRect AlbumTracksView::cover_rect(const Block& b) const { return {kPad, b.top + kPad, kCover, kCover}; }
QRect AlbumTracksView::play_rect(const Block& b) const {
  return {viewport()->width() - kPad - kPlayW, b.top + kPad + 4, kPlayW, kPlayH};
}
int AlbumTracksView::rows_top(const Block& b) const { return b.top + kPad + kHeader; }

AlbumTracksView::HitResult AlbumTracksView::hit_test(const QPoint& pos) {
  const QPoint p(pos.x(), pos.y() + verticalScrollBar()->value());
  const int i = block_at_content_y(p.y());
  if (i < 0 || i >= count()) return {};
  const Block& b = blocks_[static_cast<std::size_t>(i)];
  if (cover_rect(b).contains(p)) return {Hit::Cover, i, -1};
  if (play_rect(b).contains(p)) return {Hit::Play, i, -1};
  const int text_x = kPad + kCover + kTextGap - 8;
  if (p.x() >= text_x && p.y() >= rows_top(b)) {
    const int row = (p.y() - rows_top(b)) / kRow;
    if (row < b.info.track_count) return {Hit::Track, i, row};
  }
  return {Hit::None, i, -1};
}

void AlbumTracksView::paint_block(QPainter& p, int index) {
  const Theme& t = theme();
  Block& b = load(index);
  const int w = viewport()->width();

  // Cover.
  const QRect cover = cover_rect(b);
  p.save();
  QPainterPath clip;
  clip.addRoundedRect(cover, 6, 6);
  p.setClipPath(clip);
  p.fillRect(cover, t.raised);
  const QPixmap pm = covers_.get(b.info.art, static_cast<int>(kCover * devicePixelRatioF()));
  if (pm.isNull()) {
    const int inset = kCover * 3 / 10;
    paint_glyph(p, Glyph::Note, QRectF(cover).adjusted(inset, inset, -inset, -inset), t.faint);
  } else {
    const QSizeF logical = QSizeF(pm.size()) / pm.devicePixelRatio();
    p.drawPixmap(QRectF(cover.left() + (kCover - logical.width()) / 2, cover.top() + (kCover - logical.height()) / 2,
                        logical.width(), logical.height()),
                 pm, QRectF(pm.rect()));
  }
  p.restore();
  if (sel_ == Pos{index, -1}) {
    p.setPen(QPen(t.accent, 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(cover).adjusted(-1, -1, 1, 1), 7, 7);
  }

  // Heading.
  const int text_x = kPad + kCover + kTextGap;
  QFont title = font();
  title.setPixelSize(17);
  title.setWeight(QFont::Bold);
  const QRect play = play_rect(b);
  const int text_w = play.left() - text_x - 12;
  p.setFont(title);
  p.setPen(t.text);
  p.drawText(QRect(text_x, b.top + kPad, text_w, 24), Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(title).elidedText(b.info.title, Qt::ElideRight, text_w));
  QFont sub = font();
  p.setFont(sub);
  p.setPen(t.dim);
  p.drawText(QRect(text_x, b.top + kPad + 26, text_w, 18), Qt::AlignLeft | Qt::AlignVCenter,
             QFontMetrics(sub).elidedText(b.info.subtitle, Qt::ElideRight, text_w));

  std::chrono::milliseconds total{0};
  std::vector<const Track*> rows;
  rows.reserve(b.ids.size());
  for (TrackId id : b.ids) {
    const Track* trk = tracks_.get(id);
    rows.push_back(trk);
    if (trk) total += trk->audio.duration;
  }
  const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(total).count();
  QFont small = font();
  small.setPointSizeF(small.pointSizeF() * 0.9);
  p.setFont(small);
  p.setPen(t.faint);
  p.drawText(QRect(text_x, b.top + kPad + 46, text_w, 16), Qt::AlignLeft | Qt::AlignVCenter,
             QString("%1 · %2").arg(b.ids.size() == 1 ? tr("1 track") : tr("%1 tracks").arg(b.ids.size()),
                                    minutes >= 60 ? tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60) : tr("%1 min").arg(minutes)));

  // Play button.
  p.setPen(Qt::NoPen);
  p.setBrush(t.accent);
  p.drawRoundedRect(play, kPlayH / 2.0, kPlayH / 2.0);
  QFont bold = font();
  bold.setWeight(QFont::DemiBold);
  p.setFont(bold);
  p.setPen(t.accent_text);
  p.drawText(play, Qt::AlignCenter, tr("Play"));
  if (hover_play_ && hover_.block == index) {
    p.setPen(QPen(t.text, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(play).adjusted(0.5, 0.5, -0.5, -0.5), kPlayH / 2.0, kPlayH / 2.0);
  }

  // Track rows.
  const int rows_x = text_x - 8;
  for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
    const QRect row(rows_x, rows_top(b) + r * kRow, w - rows_x - kPad + 4, kRow);
    const bool selected = sel_ == Pos{index, r} || sel_ == Pos{index, -1};
    const bool hot = hover_ == Pos{index, r} && !hover_play_;
    const Track* trk = rows[static_cast<std::size_t>(r)];
    const bool playing = trk && playing_ && trk->id == playing_;
    if (selected) p.fillRect(row, t.accent_tint);
    else if (hot) p.fillRect(row, t.hover);
    else if (r & 1) p.fillRect(row, t.alt);
    if (!trk) continue;

    const QString number = trk->tags.track_no ? QString::number(*trk->tags.track_no) : QString::number(r + 1);
    const QRect num_rect(row.left() + 8, row.top(), kNumberW - 8, kRow);
    if (playing) {
      paint_glyph(p, Glyph::Equalizer, QRectF(num_rect.right() - 14, row.center().y() - 7, 14, 14), t.accent);
    } else {
      p.setFont(font());
      p.setPen(selected ? t.text : t.faint);
      p.drawText(num_rect, Qt::AlignRight | Qt::AlignVCenter, number);
    }
    const QString dur = qs(text::format_duration(trk->audio.duration));
    const int dur_w = 64;
    QFont f = font();
    if (playing) f.setBold(true);
    p.setFont(f);
    p.setPen(playing ? t.accent : t.text);
    const QString name = trk->tags.title.empty() ? from_path(trk->file.path.stem()) : qs(trk->tags.title);
    const QRect name_rect(row.left() + kNumberW + 14, row.top(), row.width() - kNumberW - 14 - dur_w - 14, kRow);
    p.drawText(name_rect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(name, Qt::ElideRight, name_rect.width()));
    p.setFont(font());
    p.setPen(selected ? t.text : t.dim);
    p.drawText(QRect(row.right() - dur_w - 8, row.top(), dur_w, kRow), Qt::AlignRight | Qt::AlignVCenter, dur);
  }

  // Divider under the block.
  p.setPen(t.border);
  p.drawLine(kPad, b.top + b.height - 1, w - kPad, b.top + b.height - 1);
}

void AlbumTracksView::paintEvent(QPaintEvent*) {
  QPainter p(viewport());
  p.fillRect(viewport()->rect(), theme().base);
  p.setRenderHint(QPainter::Antialiasing);
  const int scroll = verticalScrollBar()->value();
  p.translate(0, -scroll);
  const int first = std::max(0, block_at_content_y(scroll));
  for (int i = first; i < count() && blocks_[static_cast<std::size_t>(i)].top < scroll + viewport()->height(); ++i)
    paint_block(p, i);
}

void AlbumTracksView::select(Pos pos) {
  sel_ = pos;
  viewport()->update();
}

void AlbumTracksView::reveal(Pos pos) {
  if (pos.block < 0) return;
  const Block& b = blocks_[static_cast<std::size_t>(pos.block)];
  auto* bar = verticalScrollBar();
  const int top = pos.row < 0 ? b.top : rows_top(b) + pos.row * kRow;
  const int bottom = pos.row < 0 ? b.top + b.height : top + kRow;
  if (top < bar->value()) bar->setValue(top - kPad);
  else if (bottom > bar->value() + viewport()->height()) bar->setValue(bottom - viewport()->height() + kPad);
}

void AlbumTracksView::mousePressEvent(QMouseEvent* e) {
  setFocus();
  if (e->button() != Qt::LeftButton && e->button() != Qt::RightButton) return;
  const HitResult h = hit_test(e->position().toPoint());
  if (h.kind == Hit::Play && e->button() == Qt::LeftButton) return emit play_requested(h.block, 0);
  if (h.kind == Hit::Cover) select({h.block, -1});
  else if (h.kind == Hit::Track) select({h.block, h.row});
}

void AlbumTracksView::mouseDoubleClickEvent(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton) return;
  const HitResult h = hit_test(e->position().toPoint());
  if (h.kind == Hit::Track) emit play_requested(h.block, h.row);
  else if (h.kind == Hit::Cover) emit play_requested(h.block, 0);
}

void AlbumTracksView::mouseMoveEvent(QMouseEvent* e) {
  const HitResult h = hit_test(e->position().toPoint());
  const Pos next = h.kind == Hit::Track ? Pos{h.block, h.row} : Pos{h.block, -1};
  const bool play = h.kind == Hit::Play;
  if (next != hover_ || play != hover_play_) {
    hover_ = next;
    hover_play_ = play;
    viewport()->update();
  }
  viewport()->setCursor(h.kind == Hit::Play || h.kind == Hit::Cover ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

bool AlbumTracksView::viewportEvent(QEvent* e) {
  if (e->type() == QEvent::Leave && (hover_.block != -1 || hover_play_)) {
    hover_ = {};
    hover_play_ = false;
    viewport()->update();
  }
  return QAbstractScrollArea::viewportEvent(e);
}

void AlbumTracksView::contextMenuEvent(QContextMenuEvent* e) {
  const HitResult h = hit_test(e->pos());
  if (h.block < 0 || h.kind == Hit::None) return;
  const Pos target = h.kind == Hit::Track ? Pos{h.block, h.row} : Pos{h.block, -1};
  select(target);
  emit context_requested(target.block, target.row, e->globalPos());
}

void AlbumTracksView::keyPressEvent(QKeyEvent* e) {
  if (count() == 0) return QAbstractScrollArea::keyPressEvent(e);
  Pos cur = sel_.block < 0 ? Pos{0, -1} : sel_;
  auto rows_in = [&](int block) { return std::max(1, blocks_[static_cast<std::size_t>(block)].info.track_count); };
  switch (e->key()) {
    case Qt::Key_Down:
      if (sel_.block < 0) cur = {0, 0};
      else if (cur.row + 1 < rows_in(cur.block)) ++cur.row;
      else if (cur.block + 1 < count()) cur = {cur.block + 1, 0};
      break;
    case Qt::Key_Up:
      if (sel_.block < 0) cur = {0, 0};
      else if (cur.row > 0) --cur.row;
      else if (cur.block > 0) cur = {cur.block - 1, rows_in(cur.block - 1) - 1};
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (sel_.block >= 0) emit play_requested(sel_.block, std::max(0, sel_.row));
      return;
    default: return QAbstractScrollArea::keyPressEvent(e);
  }
  select(cur);
  reveal(cur);
}

}  // namespace bh::ui

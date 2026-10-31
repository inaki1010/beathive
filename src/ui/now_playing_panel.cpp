#include "ui/now_playing_panel.hpp"

#include <QVBoxLayout>

#include "core/util/text.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

NowPlayingPanel::NowPlayingPanel(CoverCache& covers, QWidget* parent) : QWidget(parent), covers_(covers) {
  setObjectName("now_playing");
  setAttribute(Qt::WA_StyledBackground);

  cover_ = new CoverView(10, this);
  cover_->setMaximumWidth(420);
  title_ = new QLabel(this);
  title_->setObjectName("np_title");
  artist_ = new QLabel(this);
  artist_->setObjectName("np_artist");
  album_ = new QLabel(this);
  album_->setObjectName("np_album");
  for (auto* l : {title_, artist_, album_}) {
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  }
  badges_ = new QHBoxLayout;
  badges_->setSpacing(6);
  badges_->setContentsMargins(0, 0, 0, 0);

  auto* lay = new QVBoxLayout(this);
  lay->setContentsMargins(18, 18, 18, 14);
  lay->setSpacing(3);
  lay->addWidget(cover_);
  lay->addSpacing(10);
  lay->addWidget(title_);
  lay->addWidget(artist_);
  lay->addWidget(album_);
  lay->addSpacing(8);
  lay->addLayout(badges_);
  lay->addStretch(1);
  show_track(nullptr);
}

void NowPlayingPanel::show_track(const Track* t) {
  while (auto* item = badges_->takeAt(0)) {
    delete item->widget();
    delete item;
  }

  QStringList badges;
  if (!t) {
    art_ = {};
    title_->setText(tr("Nothing playing"));
    artist_->setText(tr("Double-click a track to start."));
    album_->clear();
  } else {
    art_ = t->art;
    title_->setText(t->tags.title.empty() ? from_path(t->file.path.stem()) : qs(t->tags.title));
    artist_->setText(qs(t->tags.artist_display()));
    album_->setText(qs(t->tags.album) + (t->tags.year ? QString(" · %1").arg(*t->tags.year) : QString()));
    if (!t->audio.codec.empty()) badges << qs(text::to_lower(t->audio.codec)).toUpper();
    QStringList q;
    if (t->audio.sample_rate) q << tr("%1 kHz").arg(t->audio.sample_rate / 1000.0, 0, 'g', 4);
    if (t->audio.bit_depth) q << tr("%1-bit").arg(t->audio.bit_depth);
    if (!q.isEmpty()) badges << q.join(" · ");
    if (t->audio.bitrate_kbps) badges << tr("%1 kbps").arg(t->audio.bitrate_kbps);
  }
  album_->setVisible(!album_->text().isEmpty());
  for (const auto& b : badges) {
    auto* l = new QLabel(b, this);
    l->setObjectName("badge");
    badges_->addWidget(l);
  }
  badges_->addStretch(1);
  update_cover();
}

void NowPlayingPanel::update_cover() {
  const int px = static_cast<int>(std::max(cover_->width(), 160) * devicePixelRatioF());
  cover_->set_pixmap(art_ ? covers_.get(art_, std::min(px, 800)) : QPixmap());
}

void NowPlayingPanel::resizeEvent(QResizeEvent* e) {
  QWidget::resizeEvent(e);
  update_cover();
}

}  // namespace bh::ui

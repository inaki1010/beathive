#include "ui/transport_bar.hpp"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include "core/util/text.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

namespace {
QString time_text(std::chrono::milliseconds ms) { return qs(text::format_duration(ms)); }
constexpr int kSideWidth = 300;
}  // namespace

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
  setObjectName("transport");
  setAttribute(Qt::WA_StyledBackground);
  setFixedHeight(92);

  // --- left: what's playing
  cover_ = new CoverView(6, this);
  cover_->setFixedSize(66, 66);
  line1_ = new ElidedLabel(this);
  line1_->setObjectName("tp_title");
  line2_ = new ElidedLabel(this);
  line2_->setObjectName("tp_artist");
  auto* titles = new QVBoxLayout;
  titles->setSpacing(2);
  titles->addStretch();
  titles->addWidget(line1_);
  titles->addWidget(line2_);
  titles->addStretch();
  auto* left = new QWidget(this);
  left->setFixedWidth(kSideWidth);
  auto* llay = new QHBoxLayout(left);
  llay->setContentsMargins(0, 0, 0, 0);
  llay->setSpacing(12);
  llay->addWidget(cover_);
  llay->addLayout(titles, 1);

  // --- middle: transport and seek
  shuffle_ = new IconButton(Glyph::Shuffle, 32, 19, this);
  shuffle_->setCheckable(true);
  shuffle_->setToolTip(tr("Shuffle"));
  prev_ = new IconButton(Glyph::Prev, 34, 20, this);
  prev_->setToolTip(tr("Previous"));
  play_ = new IconButton(Glyph::Play, 46, 24, this);
  play_->set_look(IconButton::Look::Round);
  play_->setToolTip(tr("Play"));
  next_ = new IconButton(Glyph::Next, 34, 20, this);
  next_->setToolTip(tr("Next"));
  repeat_ = new IconButton(Glyph::Repeat, 32, 19, this);
  repeat_->setCheckable(true);
  repeat_->setToolTip(tr("Repeat: off"));
  stop_ = new IconButton(Glyph::Stop, 30, 16, this);
  stop_->setToolTip(tr("Stop"));
  stop_->setEnabled(false);

  auto* buttons = new QHBoxLayout;
  buttons->setSpacing(6);
  buttons->addStretch();
  buttons->addWidget(stop_);
  buttons->addSpacing(8);
  buttons->addWidget(shuffle_);
  buttons->addWidget(prev_);
  buttons->addWidget(play_);
  buttons->addWidget(next_);
  buttons->addWidget(repeat_);
  buttons->addSpacing(8 + 30 + 6);  // balances the stop button so play stays centred
  buttons->addStretch();

  elapsed_ = new QLabel("0:00", this);
  remaining_ = new QLabel("0:00", this);
  for (auto* l : {elapsed_, remaining_}) {
    l->setObjectName("tp_time");
    l->setFixedWidth(44);
  }
  elapsed_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  remaining_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  seek_ = new SeekSlider(this);
  seek_->setRange(0, 0);
  seek_->setEnabled(false);
  auto* seekrow = new QHBoxLayout;
  seekrow->setSpacing(8);
  seekrow->addWidget(elapsed_);
  seekrow->addWidget(seek_, 1);
  seekrow->addWidget(remaining_);

  auto* mid = new QVBoxLayout;
  mid->setSpacing(2);
  mid->addStretch();
  mid->addLayout(buttons);
  mid->addLayout(seekrow);
  mid->addStretch();

  // --- right: volume
  mute_ = new IconButton(Glyph::Volume, 32, 19, this);
  mute_->setToolTip(tr("Mute"));
  volume_ = new SeekSlider(this);
  volume_->setRange(0, 100);
  volume_->setValue(80);
  volume_->setFixedWidth(104);
  volume_->setToolTip(tr("Volume"));
  volume_->setSingleStep(5);
  queue_ = new IconButton(Glyph::Queue, 32, 19, this);
  queue_->setCheckable(true);
  queue_->setChecked(true);
  queue_->setToolTip(tr("Show or hide the queue panel"));
  auto* right = new QWidget(this);
  right->setFixedWidth(kSideWidth);
  auto* rlay = new QHBoxLayout(right);
  rlay->setContentsMargins(0, 0, 0, 0);
  rlay->setSpacing(4);
  rlay->addStretch();
  rlay->addWidget(mute_);
  rlay->addWidget(volume_);
  rlay->addSpacing(10);
  rlay->addWidget(queue_);

  auto* row = new QHBoxLayout(this);
  row->setContentsMargins(18, 0, 18, 0);
  row->setSpacing(16);
  row->addWidget(left);
  row->addLayout(mid, 1);
  row->addWidget(right);

  connect(prev_, &QAbstractButton::clicked, this, &TransportBar::previousClicked);
  connect(play_, &QAbstractButton::clicked, this, &TransportBar::playPauseClicked);
  connect(stop_, &QAbstractButton::clicked, this, &TransportBar::stopClicked);
  connect(next_, &QAbstractButton::clicked, this, &TransportBar::nextClicked);
  connect(volume_, &QSlider::valueChanged, this, [this](int v) {
    if (v > 0) premute_ = v;
    update_volume_glyph();
    emit volumeChanged(v);
  });
  connect(mute_, &QAbstractButton::clicked, this, [this] { volume_->setValue(volume_->value() > 0 ? 0 : premute_); });
  connect(shuffle_, &QAbstractButton::toggled, this, &TransportBar::shuffleToggled);
  connect(queue_, &QAbstractButton::toggled, this, &TransportBar::queueToggled);
  connect(repeat_, &QAbstractButton::clicked, this, [this] {
    // Off -> All -> One -> Off
    auto m = repeat_mode_ == RepeatMode::Off ? RepeatMode::All
             : repeat_mode_ == RepeatMode::All ? RepeatMode::One
                                               : RepeatMode::Off;
    set_repeat(m);
    emit repeatChanged(m);
  });
  connect(seek_, &QSlider::sliderPressed, this, [this] { dragging_ = true; });
  connect(seek_, &QSlider::sliderMoved, this, [this](int v) { elapsed_->setText(time_text(std::chrono::milliseconds(v))); });
  connect(seek_, &QSlider::sliderReleased, this, [this] {
    dragging_ = false;
    emit seekRequested(seek_->sliderPosition());  // the value itself is only updated after this signal
  });
  update_volume_glyph();
}

void TransportBar::update_volume_glyph() {
  const int v = volume_->value();
  if (v == 0) mute_->set_glyph(Glyph::VolumeMute);
  else mute_->set_glyph(Glyph::Volume, v < 34 ? 1 : 2);
  mute_->setToolTip(v == 0 ? tr("Unmute") : tr("Mute"));
}

void TransportBar::set_state(playback::PlayState s) {
  using playback::PlayState;
  play_->set_glyph(s == PlayState::Playing ? Glyph::Pause : Glyph::Play);
  play_->setToolTip(s == PlayState::Playing ? tr("Pause") : tr("Play"));
  stop_->setEnabled(s != PlayState::Stopped);
}

void TransportBar::set_position(std::chrono::milliseconds pos, std::chrono::milliseconds dur) {
  seek_->setEnabled(dur.count() > 0);
  seek_->setRange(0, static_cast<int>(dur.count()));
  remaining_->setText(time_text(dur));
  if (dragging_) return;
  seek_->setValue(static_cast<int>(pos.count()));
  elapsed_->setText(time_text(pos));
}

void TransportBar::clear_position() {
  seek_->setEnabled(false);
  seek_->setRange(0, 0);
  elapsed_->setText("0:00");
  remaining_->setText("0:00");
}

void TransportBar::set_shuffle(bool on) {
  QSignalBlocker b(shuffle_);
  shuffle_->setChecked(on);
}

void TransportBar::set_repeat(RepeatMode m) {
  repeat_mode_ = m;
  QSignalBlocker b(repeat_);
  repeat_->setChecked(m != RepeatMode::Off);
  repeat_->set_glyph(m == RepeatMode::One ? Glyph::RepeatOne : Glyph::Repeat);
  repeat_->setToolTip(m == RepeatMode::Off ? tr("Repeat: off") : m == RepeatMode::All ? tr("Repeat: all") : tr("Repeat: one"));
}

void TransportBar::set_volume_percent(int v) {
  QSignalBlocker b(volume_);
  volume_->setValue(v);
  if (v > 0) premute_ = v;
  update_volume_glyph();
}

void TransportBar::set_queue_checked(bool on) {
  QSignalBlocker b(queue_);
  queue_->setChecked(on);
}

void TransportBar::set_title(const QString& a, const QString& b) {
  line1_->setText(a);
  line2_->setText(b);
}

}  // namespace bh::ui

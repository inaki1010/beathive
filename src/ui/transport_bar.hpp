#pragma once
#include <QLabel>
#include <QPixmap>
#include <QWidget>

#include "core/model/queue.hpp"
#include "core/playback/engine.hpp"
#include "ui/controls.hpp"
#include "ui/glyphs.hpp"

namespace bh::ui {

// Cover and title on the left; shuffle, previous, play, next, repeat and the seek bar in the
// middle; volume and the queue toggle on the right.
class TransportBar : public QWidget {
  Q_OBJECT
 public:
  explicit TransportBar(QWidget* parent = nullptr);

  void set_state(playback::PlayState s);
  void set_position(std::chrono::milliseconds pos, std::chrono::milliseconds dur);
  void clear_position();
  void set_shuffle(bool on);
  void set_repeat(RepeatMode m);
  void set_volume_percent(int v);
  int volume_percent() const { return volume_->value(); }
  void set_title(const QString& line1, const QString& line2);
  void set_cover(const QPixmap& pm) { cover_->set_pixmap(pm); }
  int cover_side() const { return cover_->width(); }
  void set_queue_checked(bool on);

 signals:
  void previousClicked();
  void playPauseClicked();
  void stopClicked();
  void nextClicked();
  void seekRequested(qint64 ms);
  void volumeChanged(int percent);
  void shuffleToggled(bool on);
  void repeatChanged(bh::RepeatMode mode);
  void queueToggled(bool on);

 private:
  void update_volume_glyph();

  IconButton *prev_, *play_, *stop_, *next_, *shuffle_, *repeat_, *mute_, *queue_;
  SeekSlider *seek_, *volume_;
  QLabel *elapsed_, *remaining_;
  ElidedLabel *line1_, *line2_;
  CoverView* cover_;
  RepeatMode repeat_mode_ = RepeatMode::Off;
  bool dragging_ = false;
  int premute_ = 60;
};

}  // namespace bh::ui

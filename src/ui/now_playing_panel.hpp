#pragma once
#include <QHBoxLayout>
#include <QLabel>
#include <QWidget>

#include "core/model/track.hpp"
#include "ui/controls.hpp"
#include "ui/cover_cache.hpp"

namespace bh::ui {

// The big cover with what's playing under it: title, artist, album and format badges.
class NowPlayingPanel : public QWidget {
  Q_OBJECT
 public:
  NowPlayingPanel(CoverCache& covers, QWidget* parent = nullptr);
  void show_track(const Track* t);  // nullptr = nothing playing

 protected:
  void resizeEvent(QResizeEvent*) override;

 private:
  void update_cover();
  CoverCache& covers_;
  ArtId art_;
  CoverView* cover_;
  QLabel *title_, *artist_, *album_;
  QHBoxLayout* badges_;
};

}  // namespace bh::ui

#pragma once
#include <QButtonGroup>
#include <QWidget>
#include <vector>

namespace bh::ui {

// The ways the centre pane can show the library. Add an entry here and in view_selector.cpp
// to get another button.
enum class ViewMode { Tracks, AlbumTracks, Covers };

// A segmented control for choosing the library view.
class ViewSelector : public QWidget {
  Q_OBJECT
 public:
  explicit ViewSelector(QWidget* parent = nullptr);
  ViewMode mode() const { return mode_; }
  void set_mode(ViewMode m);

 signals:
  void modeChanged(ViewMode mode);

 private:
  QButtonGroup group_;
  ViewMode mode_ = ViewMode::Tracks;
};

}  // namespace bh::ui

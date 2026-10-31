#pragma once
#include <memory>
#include <mutex>

#include <QObject>

#include "core/playback/controller.hpp"

namespace bh::ui {

// Re-emits the controller's callbacks (which arrive on the engine or UI thread) as Qt signals
// delivered on the UI thread.
class PlaybackBridge : public QObject {
  Q_OBJECT
 public:
  PlaybackBridge(playback::PlaybackController& ctl, QObject* parent = nullptr);
  ~PlaybackBridge() override;
 signals:
  void changed();                                  // queue, now-playing or play state changed
  void skipped(qlonglong track, QString message);  // a track couldn't be played

 private:
  // The controller calls back from the engine thread, which can race with our destruction.
  // Callbacks only touch the bridge while holding `mu`; the destructor clears `bridge` under it.
  struct Link {
    std::mutex mu;
    PlaybackBridge* bridge = nullptr;
  };
  playback::PlaybackController& ctl_;
  std::shared_ptr<Link> link_ = std::make_shared<Link>();
};

}  // namespace bh::ui

#include "ui/playback_bridge.hpp"

#include "ui/qt_util.hpp"

namespace bh::ui {

PlaybackBridge::PlaybackBridge(playback::PlaybackController& ctl, QObject* parent) : QObject(parent), ctl_(ctl) {
  link_->bridge = this;
  ctl.set_on_changed([link = link_] {
    std::lock_guard lk(link->mu);
    if (link->bridge) QMetaObject::invokeMethod(link->bridge, &PlaybackBridge::changed, Qt::QueuedConnection);
  });
  ctl.set_on_skipped([link = link_](TrackId id, const Error& e) {
    std::lock_guard lk(link->mu);
    if (!link->bridge) return;
    QMetaObject::invokeMethod(
        link->bridge, [b = link->bridge, id, msg = qs(e.message)] { emit b->skipped(id.value, msg); },
        Qt::QueuedConnection);
  });
}

PlaybackBridge::~PlaybackBridge() {
  {
    std::lock_guard lk(link_->mu);  // waits out a callback that is mid-flight on the engine thread
    link_->bridge = nullptr;
  }
  ctl_.set_on_changed(nullptr);
  ctl_.set_on_skipped(nullptr);
}

}  // namespace bh::ui

#include "ui/view_selector.hpp"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QPushButton>

namespace bh::ui {

ViewSelector::ViewSelector(QWidget* parent) : QWidget(parent) {
  setObjectName("view_selector");
  setMinimumWidth(440);
  setAttribute(Qt::WA_StyledBackground);
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(3, 3, 3, 3);
  lay->setSpacing(2);
  group_.setExclusive(true);

  const std::pair<ViewMode, QString> views[] = {
      {ViewMode::Tracks, tr("Tracks")},
      {ViewMode::AlbumTracks, tr("Album && Tracks")},
      {ViewMode::Covers, tr("Album Covers")},
  };
  for (auto& [mode, label] : views) {
    auto* b = new QPushButton(label, this);
    b->setProperty("segment", true);
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    group_.addButton(b, static_cast<int>(mode));
    lay->addWidget(b, 1);
  }
  group_.button(static_cast<int>(mode_))->setChecked(true);
  connect(&group_, &QButtonGroup::idToggled, this, [this](int id, bool on) {
    if (!on) return;
    mode_ = static_cast<ViewMode>(id);
    emit modeChanged(mode_);
  });
}

void ViewSelector::set_mode(ViewMode m) {
  if (auto* b = group_.button(static_cast<int>(m))) b->setChecked(true);
}

}  // namespace bh::ui

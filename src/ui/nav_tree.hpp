#pragma once
#include <QTreeWidget>
#include <optional>
#include <string>
#include <vector>

#include "core/model/track.hpp"

namespace bh::ui {

enum class NavSource { AllMusic, RecentlyAdded, RecentlyPlayed };

class NavTree : public QTreeWidget {
  Q_OBJECT
 public:
  explicit NavTree(QWidget* parent = nullptr);
  // An artist picked in the list; the source is then AllMusic.
  std::optional<std::string> current_artist() const;
  NavSource current_source() const;
  void select(NavSource s);
  // Replaces the artist list, keeping the selected artist if it is still there.
  void set_artists(const std::vector<ArtistSummary>& artists);
 signals:
  void selectionChanged();

 private:
  void refresh_icons();
  QTreeWidgetItem* artists_header_ = nullptr;
};

}  // namespace bh::ui

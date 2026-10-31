#pragma once
#include <QTreeWidget>

namespace bh::ui {

enum class NavSource { AllMusic, RecentlyAdded, RecentlyPlayed };

class NavTree : public QTreeWidget {
  Q_OBJECT
 public:
  explicit NavTree(QWidget* parent = nullptr);
  NavSource current_source() const;
  void select(NavSource s);
 signals:
  void sourceSelected(NavSource source);

 private:
  void refresh_icons();
};

}  // namespace bh::ui

#include "ui/nav_tree.hpp"

#include <QHeaderView>
#include <QStyle>

#include "ui/glyphs.hpp"
#include "ui/theme.hpp"

namespace bh::ui {

namespace {
constexpr int kSourceRole = Qt::UserRole + 1;
}

NavTree::NavTree(QWidget* parent) : QTreeWidget(parent) {
  setObjectName("nav");
  setHeaderHidden(true);
  setIndentation(0);
  setRootIsDecorated(false);
  setEditTriggers(NoEditTriggers);
  setFrameShape(QFrame::NoFrame);
  setIconSize({18, 18});
  setMouseTracking(true);
  setFocusPolicy(Qt::NoFocus);

  auto* library = new QTreeWidgetItem(this, QStringList(tr("LIBRARY")));
  library->setFlags(Qt::ItemIsEnabled);
  QFont f = font();
  f.setBold(true);
  f.setPointSizeF(f.pointSizeF() * 0.82);
  library->setFont(0, f);
  library->setData(0, Qt::ForegroundRole, QBrush(theme().faint));
  library->setSizeHint(0, QSize(0, 34));

  auto add = [&](const QString& text, NavSource s, Glyph glyph) {
    auto* it = new QTreeWidgetItem(library, QStringList(text));
    it->setData(0, kSourceRole, static_cast<int>(s));
    it->setData(0, Qt::UserRole + 2, static_cast<int>(glyph));
  };
  add(tr("All Music"), NavSource::AllMusic, Glyph::Note);
  add(tr("Recently Added"), NavSource::RecentlyAdded, Glyph::PlusCircle);
  add(tr("Recently Played"), NavSource::RecentlyPlayed, Glyph::Clock);
  library->setExpanded(true);
  refresh_icons();
  connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, &NavTree::refresh_icons);

  connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur) {
    if (cur && cur->data(0, kSourceRole).isValid())
      emit sourceSelected(static_cast<NavSource>(cur->data(0, kSourceRole).toInt()));
  });
  select(NavSource::AllMusic);
}

NavSource NavTree::current_source() const {
  auto* it = currentItem();
  return it && it->data(0, kSourceRole).isValid() ? static_cast<NavSource>(it->data(0, kSourceRole).toInt())
                                                  : NavSource::AllMusic;
}

void NavTree::select(NavSource s) {
  for (QTreeWidgetItemIterator it(this); *it; ++it)
    if ((*it)->data(0, kSourceRole).isValid() && (*it)->data(0, kSourceRole).toInt() == static_cast<int>(s))
      setCurrentItem(*it);
}

}  // namespace bh::ui

namespace bh::ui {

// Icons are painted in theme colours, so they are rebuilt when the theme changes.
void NavTree::refresh_icons() {
  const Theme& t = theme();
  for (QTreeWidgetItemIterator it(this); *it; ++it) {
    auto* item = *it;
    if (item->data(0, Qt::UserRole + 2).isValid())
      item->setIcon(0, glyph_icon(static_cast<Glyph>(item->data(0, Qt::UserRole + 2).toInt()), t.dim, t.accent));
    else
      item->setData(0, Qt::ForegroundRole, QBrush(t.faint));
  }
}

}  // namespace bh::ui

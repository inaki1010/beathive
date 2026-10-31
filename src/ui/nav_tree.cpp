#include "ui/nav_tree.hpp"

#include <QHeaderView>
#include <QStyle>

#include "ui/glyphs.hpp"
#include "ui/qt_util.hpp"
#include "ui/theme.hpp"

namespace bh::ui {

namespace {
constexpr int kSourceRole = Qt::UserRole + 1;
constexpr int kGlyphRole = Qt::UserRole + 2;
constexpr int kArtistRole = Qt::UserRole + 3;  // the artist name (may be empty: untagged)
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

  QFont f = font();
  f.setBold(true);
  f.setPointSizeF(f.pointSizeF() * 0.82);
  auto section = [&](const QString& title) {
    auto* it = new QTreeWidgetItem(this, QStringList(title));
    it->setFlags(Qt::ItemIsEnabled);
    it->setFont(0, f);
    it->setData(0, Qt::ForegroundRole, QBrush(theme().faint));
    it->setSizeHint(0, QSize(0, 34));
    return it;
  };
  auto* library = section(tr("LIBRARY"));

  auto add = [&](const QString& text, NavSource s, Glyph glyph) {
    auto* it = new QTreeWidgetItem(library, QStringList(text));
    it->setData(0, kSourceRole, static_cast<int>(s));
    it->setData(0, kGlyphRole, static_cast<int>(glyph));
  };
  add(tr("All Music"), NavSource::AllMusic, Glyph::Note);
  add(tr("Recently Added"), NavSource::RecentlyAdded, Glyph::PlusCircle);
  add(tr("Recently Played"), NavSource::RecentlyPlayed, Glyph::Clock);
  library->setExpanded(true);
  artists_header_ = section(tr("ARTISTS"));
  artists_header_->setExpanded(true);
  refresh_icons();
  connect(&ThemeNotifier::instance(), &ThemeNotifier::changed, this, &NavTree::refresh_icons);

  connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* cur) {
    if (cur && cur->parent()) emit selectionChanged();  // section headers aren't selectable
  });
  select(NavSource::AllMusic);
}

NavSource NavTree::current_source() const {
  auto* it = currentItem();
  return it && it->data(0, kSourceRole).isValid() ? static_cast<NavSource>(it->data(0, kSourceRole).toInt())
                                                  : NavSource::AllMusic;
}

std::optional<std::string> NavTree::current_artist() const {
  auto* it = currentItem();
  if (it && it->data(0, kArtistRole).isValid()) return stdstr(it->data(0, kArtistRole).toString());
  return std::nullopt;
}

void NavTree::set_artists(const std::vector<ArtistSummary>& artists) {
  const auto keep = current_artist();
  QSignalBlocker block(this);
  setUpdatesEnabled(false);
  qDeleteAll(artists_header_->takeChildren());
  QTreeWidgetItem* reselect = nullptr;
  for (const auto& a : artists) {
    auto* it = new QTreeWidgetItem(artists_header_, QStringList(a.name.empty() ? tr("[Unknown Artist]") : qs(a.name)));
    it->setData(0, kArtistRole, qs(a.name));
    it->setToolTip(0, tr("%1 albums · %2 tracks").arg(a.album_count).arg(a.track_count));
    if (keep && *keep == a.name) reselect = it;
  }
  const bool lost = keep && !reselect;
  if (reselect) setCurrentItem(reselect);
  else if (lost) select(NavSource::AllMusic);
  setUpdatesEnabled(true);
  block.unblock();
  if (lost) emit selectionChanged();  // the artist vanished (e.g. a search): fall back to everything
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
    if (item->data(0, kGlyphRole).isValid())
      item->setIcon(0, glyph_icon(static_cast<Glyph>(item->data(0, kGlyphRole).toInt()), t.dim, t.accent));
    else if (!item->parent())
      item->setData(0, Qt::ForegroundRole, QBrush(t.faint));
  }
}

}  // namespace bh::ui

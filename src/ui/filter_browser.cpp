#include "ui/filter_browser.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "ui/qt_util.hpp"

namespace bh::ui {

namespace {
enum { Genre = 0, Artist = 1, Album = 2 };
constexpr int kValueRole = Qt::UserRole + 1;  // the filter value (QStringList for albums: [artist, album])
}  // namespace

FilterBrowser::FilterBrowser(db::Database& db, QWidget* parent) : QWidget(parent), db_(db) {
  auto* lay = new QHBoxLayout(this);
  lay->setContentsMargins(0, 0, 0, 0);
  lay->setSpacing(1);
  const QString titles[3] = {tr("GENRE"), tr("ARTIST"), tr("ALBUM")};
  for (int c = 0; c < 3; ++c) {
    auto* list = lists_[c] = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setFrameShape(QFrame::NoFrame);
    list->setUniformItemSizes(true);
    list->setAlternatingRowColors(false);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setTextElideMode(Qt::ElideRight);
    auto* title = new QLabel(titles[c], this);
    title->setObjectName("column_title");
    auto* col = new QVBoxLayout;
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(title);
    col->addWidget(list, 1);
    lay->addLayout(col);
  }
  for (int c = 0; c < 3; ++c)
    connect(lists_[c], &QListWidget::itemSelectionChanged, this, [this, c] {
      if (rebuilding_) return;
      rebuild(c + 1);
      emit filterChanged();
    });
  setMaximumHeight(190);
  setMinimumHeight(110);
}

void FilterBrowser::fill(QListWidget* list, const QString& all_label, const QList<QPair<QString, QVariant>>& items) {
  // Remember what was selected so a refresh keeps the user's place.
  auto key = [](const QVariant& v) { return v.metaType().id() == QMetaType::QStringList ? v.toStringList().join('\x1f') : v.toString(); };
  QSet<QString> keep;
  for (auto* it : list->selectedItems())
    if (it->data(kValueRole).isValid()) keep.insert(key(it->data(kValueRole)));

  list->clear();
  auto* all = new QListWidgetItem(all_label, list);
  QFont f = all->font();
  f.setItalic(true);
  all->setFont(f);
  for (auto& [text, value] : items) {
    auto* it = new QListWidgetItem(text, list);
    it->setData(kValueRole, value);
    if (keep.contains(key(value))) it->setSelected(true);
  }
  if (list->selectedItems().isEmpty()) list->setCurrentRow(0, QItemSelectionModel::NoUpdate);
}

db::TrackFilter FilterBrowser::filter_up_to(int column_exclusive) const {
  db::TrackFilter f = base_;
  auto selected = [&](int c) {
    QList<QListWidgetItem*> out;
    for (auto* it : lists_[c]->selectedItems())
      if (it->data(kValueRole).isValid()) out.append(it);  // row 0 ("All") carries no value
    return out;
  };
  if (column_exclusive > Genre)
    for (auto* it : selected(Genre)) f.genres.push_back(stdstr(it->data(kValueRole).toString()));
  if (column_exclusive > Artist)
    for (auto* it : selected(Artist)) f.artists.push_back(stdstr(it->data(kValueRole).toString()));
  if (column_exclusive > Album) {
    auto sel = selected(Album);
    if (sel.size() == 1) {  // one album pins both artist and title; several can't be expressed, so ignore
      auto pair = sel[0]->data(kValueRole).toStringList();
      f.album_artist = stdstr(pair[0]);
      f.album = stdstr(pair[1]);
    }
  }
  return f;
}

db::TrackFilter FilterBrowser::filter() const { return filter_up_to(3); }

void FilterBrowser::set_base(db::TrackFilter base) {
  base_ = std::move(base);
  rebuild(0);
}

void FilterBrowser::rebuild(int from_column) {
  rebuilding_ = true;
  db::Query q(db_);

  if (from_column <= Genre) {
    QList<QPair<QString, QVariant>> items;
    if (auto genres = q.genres(filter_up_to(Genre)))
      for (auto& g : *genres) items.append({QString("%1 (%2)").arg(qs(g.name)).arg(g.track_count), qs(g.name)});
    fill(lists_[Genre], tr("All Genres"), items);
  }
  if (from_column <= Artist) {
    QList<QPair<QString, QVariant>> items;
    if (auto artists = q.artists(filter_up_to(Artist)))
      for (auto& a : *artists) items.append({qs(a.name), qs(a.name)});
    fill(lists_[Artist], tr("All Artists"), items);
  }
  if (from_column <= Album) {
    QList<QPair<QString, QVariant>> items;
    if (auto albums = q.albums(filter_up_to(Album)))
      for (auto& a : *albums) {
        QString title = a.key.album.empty() ? tr("[Unknown Album]") : qs(a.key.album);
        items.append({title, QStringList{qs(a.key.album_artist), qs(a.key.album)}});
      }
    fill(lists_[Album], tr("All Albums"), items);
  }
  rebuilding_ = false;
}

}  // namespace bh::ui

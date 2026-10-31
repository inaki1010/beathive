#pragma once
#include <QListWidget>
#include <QWidget>

#include "core/db/query.hpp"

namespace bh::ui {

// Three cascading lists (Genre | Artist | Album). Selecting in one narrows those to its right.
class FilterBrowser : public QWidget {
  Q_OBJECT
 public:
  explicit FilterBrowser(db::Database& db, QWidget* parent = nullptr);

  // The selection-independent part of the filter (search text, nav source rules); the lists
  // are rebuilt from it.
  void set_base(db::TrackFilter base);
  // Base filter plus whatever is selected in the lists.
  db::TrackFilter filter() const;

 signals:
  void filterChanged();

 private:
  void rebuild(int from_column);
  db::TrackFilter filter_up_to(int column_exclusive) const;
  void fill(QListWidget* list, const QString& all_label, const QList<QPair<QString, QVariant>>& items);

  db::Database& db_;
  db::TrackFilter base_;
  QListWidget* lists_[3];
  bool rebuilding_ = false;
};

}  // namespace bh::ui

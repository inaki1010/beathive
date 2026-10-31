#pragma once
#include <QDialog>
#include <QLabel>
#include <QListWidget>

#include "core/db/folder_repo.hpp"

namespace bh::ui {

// Add / remove the folders the library watches.
class LibraryDialog : public QDialog {
  Q_OBJECT
 public:
  LibraryDialog(db::Database& db, QWidget* parent = nullptr);
  bool changed() const { return changed_; }

 private:
  void reload();
  db::FolderRepo repo_;
  QListWidget* list_;
  bool changed_ = false;
};

}  // namespace bh::ui

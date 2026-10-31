#include "ui/library_dialog.hpp"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/qt_util.hpp"

namespace bh::ui {

LibraryDialog::LibraryDialog(db::Database& db, QWidget* parent) : QDialog(parent), repo_(db) {
  setWindowTitle(tr("Library Folders"));
  resize(520, 320);

  list_ = new QListWidget(this);
  auto* add = new QPushButton(tr("Add Folder…"), this);
  auto* remove = new QPushButton(tr("Remove"), this);
  remove->setEnabled(false);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

  auto* side = new QVBoxLayout;
  side->addWidget(add);
  side->addWidget(remove);
  side->addStretch();
  auto* row = new QHBoxLayout;
  row->addWidget(list_, 1);
  row->addLayout(side);
  auto* lay = new QVBoxLayout(this);
  lay->addWidget(new QLabel(tr("Music in these folders (and their subfolders) appears in the library."), this));
  lay->addLayout(row, 1);
  lay->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
  connect(list_, &QListWidget::itemSelectionChanged, this, [=, this] { remove->setEnabled(!list_->selectedItems().isEmpty()); });
  connect(add, &QPushButton::clicked, this, [this] {
    auto dir = QFileDialog::getExistingDirectory(this, tr("Add Music Folder"));
    if (dir.isEmpty()) return;
    if (auto r = repo_.add({{}, to_path(dir), true, true}); !r)
      QMessageBox::warning(this, tr("Library Folders"), qs(r.error().message));
    else
      changed_ = true;
    reload();
  });
  connect(remove, &QPushButton::clicked, this, [this] {
    auto* it = list_->currentItem();
    if (!it) return;
    if (QMessageBox::question(this, tr("Remove Folder"),
                              tr("Remove this folder and its tracks from the library?\nYour files are not deleted.")) !=
        QMessageBox::Yes)
      return;
    if (repo_.remove(FolderId{it->data(Qt::UserRole).toLongLong()})) changed_ = true;
    reload();
  });
  reload();
}

void LibraryDialog::reload() {
  list_->clear();
  if (auto folders = repo_.list())
    for (auto& f : *folders) {
      auto* it = new QListWidgetItem(from_path(f.path), list_);
      it->setData(Qt::UserRole, static_cast<qlonglong>(f.id.value));
    }
}

}  // namespace bh::ui

#include "ui/library_service.hpp"

#include "core/db/folder_repo.hpp"
#include "ui/qt_util.hpp"

namespace bh::ui {

LibraryService::LibraryService(std::filesystem::path db_path, QObject* parent)
    : QObject(parent), db_path_(std::move(db_path)) {}

LibraryService::~LibraryService() {
  worker_.request_stop();  // jthread joins on destruction
}

void LibraryService::cancel() { worker_.request_stop(); }

void LibraryService::scan_all() {
  if (scanning_.exchange(true)) return;
  emit scanStarted();
  worker_ = std::jthread([this](std::stop_token stop) {
    int added = 0, updated = 0, missing = 0, errors = 0;
    bool cancelled = false;

    auto db = db::Database::open(db_path_);  // this thread's own connection
    if (db) {
      auto folders = db::FolderRepo(*db).list();
      if (folders) {
        for (auto& f : *folders) {
          if (stop.stop_requested()) { cancelled = true; break; }
          auto name = from_path(f.path);
          auto report = library::Scanner(*db).scan(
              f, {},
              [&](const library::ScanProgress& p) {
                QMetaObject::invokeMethod(
                    this,
                    [this, name, d = static_cast<int>(p.files_done), t = static_cast<int>(p.files_to_read)] {
                      emit scanProgress(name, d, t);
                    },
                    Qt::QueuedConnection);
              },
              stop);
          if (!report) { ++errors; continue; }
          added += static_cast<int>(report->added);
          updated += static_cast<int>(report->updated);
          missing += static_cast<int>(report->newly_missing);
          errors += static_cast<int>(report->errors.size());
          cancelled |= report->cancelled;
        }
      }
    } else {
      ++errors;
    }
    QMetaObject::invokeMethod(
        this,
        [this, added, updated, missing, errors, cancelled] {
          scanning_ = false;
          emit scanFinished(added, updated, missing, errors, cancelled);
        },
        Qt::QueuedConnection);
  });
}

}  // namespace bh::ui

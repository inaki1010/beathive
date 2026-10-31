#pragma once
#include <QObject>
#include <atomic>
#include <filesystem>
#include <thread>

#include "core/library/scanner.hpp"

namespace bh::ui {

// Runs library scans on a background thread with its own database connection and reports
// progress as Qt signals (delivered on the UI thread).
class LibraryService : public QObject {
  Q_OBJECT
 public:
  LibraryService(std::filesystem::path db_path, QObject* parent = nullptr);
  ~LibraryService() override;

  bool scanning() const { return scanning_; }
  void scan_all();     // no-op if a scan is already running
  void cancel();

 signals:
  void scanStarted();
  void scanProgress(QString what, int done, int total);
  void scanFinished(int added, int updated, int missing, int errors, bool cancelled);

 private:
  void run();

  std::filesystem::path db_path_;
  std::jthread worker_;
  std::atomic<bool> scanning_{false};
};

}  // namespace bh::ui

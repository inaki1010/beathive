#pragma once
#include <filesystem>
#include <memory>

#include "core/db/sqlite.hpp"
#include "core/playback/controller.hpp"
#include "core/playback/engine.hpp"

namespace bh::ui {

// The long-lived non-visual parts of the app, created once in main().
//
// Database connections (SQLite handles concurrent readers + one writer in WAL mode):
//   db        - the UI thread: browsing, search, tag/rating edits, folder management
//   playback  - used only by the controller's resolver (guarded by the controller's lock)
// The scanner opens its own connection on its worker thread.
struct AppServices {
  std::filesystem::path data_dir;
  std::filesystem::path db_path;
  db::Database db;
  db::Database playback_db;
  playback::Engine engine;
  playback::PlaybackController controller;

  // `null_audio` uses a silent real-time device (for headless runs and screenshots).
  static Result<std::unique_ptr<AppServices>> create(const std::filesystem::path& data_dir, bool null_audio);

 private:
  AppServices(std::filesystem::path data_dir, db::Database ui_db, db::Database pb_db,
              std::unique_ptr<playback::IAudioOutput> out);
};

}  // namespace bh::ui

#pragma once
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

#include "core/db/sqlite.hpp"
#include "core/model/library.hpp"

namespace bh::library {

struct ScanOptions {
  unsigned threads = 0;           // 0 = min(hardware threads, 8)
  std::size_t chunk_size = 256;   // files read in parallel, then written in one transaction
  bool use_folder_covers = true;  // use the folder's cover image for tracks without embedded art
};

struct ScanProgress {
  enum class Phase { Walking, Reading, Finishing };
  Phase phase = Phase::Walking;
  std::size_t files_found = 0;    // audio files seen so far
  std::size_t files_to_read = 0;  // new or changed (known after the walk)
  std::size_t files_done = 0;
  std::filesystem::path current;
};

struct ScanError {
  std::filesystem::path path;
  std::string message;
};

struct ScanReport {
  std::size_t added = 0;
  std::size_t updated = 0;
  std::size_t unchanged = 0;
  std::size_t newly_missing = 0;  // in the DB but no longer on disk (flagged, not deleted)
  std::size_t restored = 0;       // flagged missing earlier, back on disk
  std::size_t covers_updated = 0; // existing tracks whose folder cover was found, changed or lost
  std::vector<ScanError> errors;  // unreadable files; the scan continues past them
  bool cancelled = false;
};

using ProgressFn = std::function<void(const ScanProgress&)>;

// Synchronous. Run it on a worker thread; it uses `db` only from the calling thread
// (tag reading fans out to helper threads that never touch the database).
// The folder must already exist in the DB (FolderRepo::add).
class Scanner {
 public:
  explicit Scanner(db::Database& db) : db_(db) {}

  // Fails with NotFound, changing nothing, if the folder root is missing (e.g. unmounted drive).
  Result<ScanReport> scan(const WatchedFolder& folder, ScanOptions opt = {}, ProgressFn progress = {},
                          std::stop_token stop = {});

 private:
  db::Database& db_;
};

}  // namespace bh::library

#include "core/library/scanner.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "core/db/art_repo.hpp"
#include "core/db/track_repo.hpp"
#include "core/library/cover_finder.hpp"
#include "core/tags/tag_io.hpp"
#include "core/util/hash.hpp"
#include "core/util/paths.hpp"
#include "core/util/text.hpp"

namespace bh::library {

namespace fs = std::filesystem;

namespace {

struct Candidate {
  fs::path path;
  std::string key;  // utf8 path, as stored in the DB
  std::int64_t size;
  std::int64_t mtime;
  bool existing;    // already in the DB (changed) vs brand new
};

std::int64_t to_unix(fs::file_time_type t) {
  auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
  return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

bool is_hidden(const fs::path& p) {
  auto n = p.filename().string();
  return n.size() > 1 && n[0] == '.';
}

constexpr std::uintmax_t kMaxCoverBytes = 20u << 20;

std::optional<std::string> hash_file(const fs::path& p) {
  std::error_code ec;
  auto size = fs::file_size(p, ec);
  if (ec || size == 0 || size > kMaxCoverBytes) return std::nullopt;
  std::ifstream in(p, std::ios::binary);
  std::vector<unsigned char> buf(size);
  if (!in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(size))) return std::nullopt;
  return content_hash(buf);
}

// Resolves the cover image for a directory's music to an art row, once per directory per scan.
// Unchanged image files (same size and mtime as last time) cost a stat(), not a read.
class FolderCovers {
 public:
  explicit FolderCovers(db::ArtRepo& art) : art_(art) {}

  Result<ArtId> for_directory(const fs::path& dir) {
    if (auto it = cache_.find(dir); it != cache_.end()) return it->second;
    ArtId id;
    if (auto image = find_cover_image(dir)) {
      std::error_code ec;
      const auto size = static_cast<std::int64_t>(fs::file_size(*image, ec));
      const auto mtime = ec ? 0 : to_unix(fs::last_write_time(*image, ec));
      if (!ec && size > 0 && static_cast<std::uintmax_t>(size) <= kMaxCoverBytes) {
        const auto key = to_utf8(*image);
        auto known = art_.find_file(key, size, mtime);
        BH_TRY(known);
        if (*known) {
          id = **known;
        } else if (auto hash = hash_file(*image)) {
          auto created = art_.get_or_create(*hash, "file:" + key);
          BH_TRY(created);
          id = *created;
          BH_TRY(art_.remember_file(key, size, mtime, id));
        }
      }
    }
    cache_.emplace(dir, id);
    return id;
  }

 private:
  db::ArtRepo& art_;
  std::map<fs::path, ArtId> cache_;
};

}  // namespace

Result<ScanReport> Scanner::scan(const WatchedFolder& folder, ScanOptions opt, ProgressFn progress,
                                 std::stop_token stop) {
  std::error_code ec;
  if (!fs::is_directory(folder.path, ec))
    return std::unexpected(Error{ErrorCode::NotFound, "folder not found: " + to_utf8(folder.path)});

  db::TrackRepo tracks(db_);
  db::ArtRepo art(db_);
  ScanReport report;
  ScanProgress prog;
  auto notify = [&] { if (progress) progress(prog); };

  // ---- 1. What does the DB already know? ----
  auto stamps_r = tracks.stamps(folder.id);
  BH_TRY(stamps_r);
  std::unordered_map<std::string, db::FileStamp> known;
  for (auto& s : *stamps_r) known.emplace(to_utf8(s.path), s);

  // ---- 2. Walk the disk ----
  std::vector<Candidate> to_read;
  std::unordered_set<std::string> seen;
  std::vector<TrackId> to_restore;

  auto visit = [&](const fs::path& p) {
    if (!tags::is_supported_extension(p)) return;
    std::error_code e;
    auto size = static_cast<std::int64_t>(fs::file_size(p, e));
    if (e) return;
    auto mtime = to_unix(fs::last_write_time(p, e));
    if (e) return;

    auto key = to_utf8(p);
    seen.insert(key);
    ++prog.files_found;
    auto it = known.find(key);
    if (it == known.end()) {
      to_read.push_back({p, std::move(key), size, mtime, false});
    } else if (it->second.size_bytes != size || it->second.mtime_unix != mtime) {
      to_read.push_back({p, std::move(key), size, mtime, true});
    } else {
      ++report.unchanged;
      if (it->second.missing) to_restore.push_back(it->second.id);
    }
    if (prog.files_found % 500 == 0) {
      prog.current = p;
      notify();
    }
  };

  const auto opts = fs::directory_options::skip_permission_denied;
  if (folder.recursive) {
    for (auto it = fs::recursive_directory_iterator(folder.path, opts, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (stop.stop_requested()) { report.cancelled = true; return report; }
      if (is_hidden(it->path())) {
        if (it->is_directory(ec)) it.disable_recursion_pending();
        continue;
      }
      if (it->is_regular_file(ec)) visit(it->path());
    }
  } else {
    for (auto it = fs::directory_iterator(folder.path, opts, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
      if (stop.stop_requested()) { report.cancelled = true; return report; }
      if (!is_hidden(it->path()) && it->is_regular_file(ec)) visit(it->path());
    }
  }

  if (stop.stop_requested()) { report.cancelled = true; return report; }

  prog.phase = ScanProgress::Phase::Reading;
  prog.files_to_read = to_read.size();
  notify();

  // ---- 3. Read tags in parallel, write in batches ----
  const Timestamp now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  unsigned n_threads = opt.threads ? opt.threads : std::min(8u, std::max(1u, std::thread::hardware_concurrency()));
  FolderCovers covers(art);

  for (std::size_t start = 0; start < to_read.size(); start += opt.chunk_size) {
    if (stop.stop_requested()) { report.cancelled = true; break; }
    const std::size_t end = std::min(to_read.size(), start + opt.chunk_size);
    std::vector<Result<tags::TagFile>> results(end - start, std::unexpected(Error{ErrorCode::Io, "not read"}));

    {
      std::atomic<std::size_t> next{start};
      auto work = [&] {
        for (std::size_t i; (i = next.fetch_add(1)) < end;)
          results[i - start] = tags::read_tags(to_read[i].path, {.want_cover = true});
      };
      std::vector<std::jthread> pool;
      for (unsigned t = 1; t < std::min<std::size_t>(n_threads, end - start); ++t) pool.emplace_back(work);
      work();  // the calling thread helps
    }

    auto tx = db::Transaction::begin(db_);
    BH_TRY(tx);
    for (std::size_t i = start; i < end; ++i) {
      auto& c = to_read[i];
      auto& r = results[i - start];
      if (!r) {
        report.errors.push_back({c.path, r.error().message});
        continue;
      }

      Track t;
      t.file = {c.path, folder.id, c.size, c.mtime, false};
      t.tags = std::move(r->tags);
      t.audio = std::move(r->audio);
      t.lib.date_added = now;  // ignored by the upsert for existing rows

      if (r->cover) {
        auto id = art.get_or_create(content_hash(r->cover->data), "embedded:" + c.key);
        BH_TRY(id);
        t.art = *id;
      } else if (opt.use_folder_covers) {
        auto id = covers.for_directory(c.path.parent_path());
        BH_TRY(id);
        t.art = *id;
      }

      BH_TRY(tracks.upsert(t));
      ++(c.existing ? report.updated : report.added);
    }
    BH_TRY(tx->commit());

    prog.files_done = end;
    prog.current = to_read[end - 1].path;
    notify();
  }

  // ---- 3b. Folder covers for tracks that were NOT re-read ----
  // A cover can appear, change or vanish without the audio files changing (or the track may have been
  // imported before it was there), so check every art-less-by-tags track against its folder again.
  if (!report.cancelled && opt.use_folder_covers) {
    std::unordered_set<std::string> reread;
    for (auto& c : to_read) reread.insert(c.key);
    std::map<fs::path, std::vector<const db::FileStamp*>> by_dir;
    for (auto& [key, stamp] : known)
      if (!stamp.has_embedded_art && !stamp.missing && seen.contains(key) && !reread.contains(key))
        by_dir[stamp.path.parent_path()].push_back(&stamp);

    auto tx = db::Transaction::begin(db_);
    BH_TRY(tx);
    for (auto& [dir, stamps] : by_dir) {
      auto wanted = covers.for_directory(dir);
      BH_TRY(wanted);
      for (const auto* stamp : stamps)
        if (stamp->art != *wanted) {
          BH_TRY(tracks.set_art(stamp->id, *wanted));
          ++report.covers_updated;
        }
    }
    BH_TRY(tx->commit());
  }

  // ---- 4. Reconcile missing / restored (skipped if cancelled: the walk was incomplete) ----
  if (!report.cancelled) {
    prog.phase = ScanProgress::Phase::Finishing;
    notify();
    auto tx = db::Transaction::begin(db_);
    BH_TRY(tx);
    for (auto id : to_restore) {
      BH_TRY(tracks.set_missing(id, false));
      ++report.restored;
    }
    for (auto& [key, stamp] : known) {
      if (!stamp.missing && !seen.contains(key)) {
        BH_TRY(tracks.set_missing(stamp.id, true));
        ++report.newly_missing;
      }
    }
    BH_TRY(tx->commit());
  }
  return report;
}

}  // namespace bh::library

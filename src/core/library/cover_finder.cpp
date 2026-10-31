#include "core/library/cover_finder.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "core/util/text.hpp"

namespace bh::library {

namespace fs = std::filesystem;

namespace {

constexpr const char* kImageExts[] = {".jpg", ".jpeg", ".png", ".webp", ".bmp"};
constexpr std::uintmax_t kMinBytes = 1024;  // smaller files are placeholders, not covers

// Lowercase and drop everything but letters/digits, so "Cover Art", "cover_art" and "CoverArt" agree.
std::string squash(const std::string& s) {
  std::string out;
  for (unsigned char c : text::to_lower(s))
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out += static_cast<char>(c);
  return out;
}

// Whole words of a file name: "Back_Cover", "BackCover" and "back cover" all give {back, cover};
// "scan01" gives {scan, 01} and "CD2" gives {cd, 2}.
// Matching words (not substrings) keeps an album called "Discovery" from looking like "disc"+"cover".
std::vector<std::string> words(const std::string& stem) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    if (!cur.empty()) out.push_back(text::to_lower(cur));
    cur.clear();
  };
  char prev = 0;
  for (char c : stem) {
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (!alnum) { flush(); prev = 0; continue; }
    const bool digit = c >= '0' && c <= '9', prev_digit = prev >= '0' && prev <= '9';
    if (c >= 'A' && c <= 'Z' && prev >= 'a' && prev <= 'z') flush();  // camelCase boundary
    else if (prev && digit != prev_digit) flush();                      // letter <-> digit boundary
    cur += c;
    prev = c;
  }
  flush();
  return out;
}

bool has(const std::vector<std::string>& w, const char* word) { return std::ranges::find(w, word) != w.end(); }

bool is_number(const std::string& t) {
  return !t.empty() && std::all_of(t.begin(), t.end(), [](char c) { return c >= '0' && c <= '9'; });
}

bool is_other_artwork(const std::vector<std::string>& w) {
  for (const char* x : {"back", "rear", "inlay", "inside", "inner", "booklet", "tray", "label", "spine", "insert",
                        "cdart", "discart", "backcover", "scan", "scans", "obi", "liner", "poster", "banner", "fanart",
                        "cd"})
    if (has(w, x)) return true;
  // "disc 2" / "disk2" label a disc; "disc" on its own inside a title is not worth the risk.
  for (std::size_t i = 0; i + 1 < w.size(); ++i)
    if ((w[i] == "disc" || w[i] == "disk") && is_number(w[i + 1])) return true;
  return false;
}

// Higher is better. 0 = not a candidate.
int score(const std::string& stem, bool& unlabelled) {
  unlabelled = false;
  const std::string n = squash(stem);
  if (n.empty()) return 0;

  // Exact, unambiguous names first.
  if (n == "cover" || n == "folder" || n == "front") return 100;
  if (n == "frontcover" || n == "coverart" || n == "albumcover" || n == "albumart" || n == "cover1" || n == "front1")
    return 95;
  if (n == "album" || n == "artwork" || n == "art" || n == "folder1") return 85;

  const auto w = words(stem);
  if (is_other_artwork(w)) return 0;

  // Windows Media Player: "AlbumArt_{GUID}_Large.jpg" is the real one, "..._Small.jpg" a thumbnail.
  if (has(w, "albumart")) return has(w, "small") ? 8 : (has(w, "large") ? 80 : 70);
  if (has(w, "small") || has(w, "thumb") || has(w, "thumbnail") || has(w, "mini")) return 5;

  if (has(w, "front")) return 75;
  if (has(w, "cover")) return 70;
  if (has(w, "folder")) return 65;
  if (has(w, "album") || has(w, "artwork")) return 55;

  unlabelled = true;  // an image, but nothing says what it is (e.g. "<album title>.jpg")
  return 20;
}

bool is_image(const fs::path& p) {
  const auto ext = text::to_lower(p.extension().string());
  return std::ranges::any_of(kImageExts, [&](const char* e) { return ext == e; });
}

std::optional<fs::path> best_in(const fs::path& dir) {
  struct Candidate {
    fs::path path;
    int score;
    bool unlabelled;
    std::uintmax_t size;
  };
  std::vector<Candidate> found;

  std::error_code ec;
  for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
       !ec && it != fs::directory_iterator(); it.increment(ec)) {
    std::error_code e;
    if (!it->is_regular_file(e) || !is_image(it->path())) continue;
    const auto size = it->file_size(e);
    if (e || size < kMinBytes) continue;
    bool unlabelled = false;
    if (int s = score(it->path().stem().string(), unlabelled); s > 0) found.push_back({it->path(), s, unlabelled, size});
  }
  if (found.empty()) return std::nullopt;

  // Several unlabelled images ("01.jpg", "scan.jpg", ...) are a guess; take the first by name,
  // which for numbered scans is normally the front. Otherwise the best score wins, then the larger file.
  std::ranges::sort(found, [](const Candidate& a, const Candidate& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.unlabelled && b.unlabelled) return a.path.filename().string() < b.path.filename().string();
    if (a.size != b.size) return a.size > b.size;
    return a.path.filename().string() < b.path.filename().string();
  });
  return found.front().path;
}

bool looks_like_disc_folder(const fs::path& dir) {
  const std::string n = squash(dir.filename().string());
  for (const char* prefix : {"cd", "disc", "disk", "dvd", "side", "volume", "vol"}) {
    const std::string p = prefix;
    if (n.size() > p.size() && n.compare(0, p.size(), p) == 0 && n[p.size()] >= '0' && n[p.size()] <= '9') return true;
  }
  return false;
}

}  // namespace

std::optional<fs::path> find_cover_image(const fs::path& dir) {
  if (auto own = best_in(dir)) return own;
  if (looks_like_disc_folder(dir) && dir.has_parent_path()) return best_in(dir.parent_path());
  return std::nullopt;
}

}  // namespace bh::library
